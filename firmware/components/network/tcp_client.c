/**
 * @file tcp_client.c
 * @brief Cliente TCP persistente — TX e RX em tasks separadas
 *
 * ARQUITETURA:
 *   task_tcp_tx  (dono do socket fd): FSM de conexão, heartbeat, envio de filas.
 *   task_tcp_rx  (leitura pura): recv() → ring buffer → parse → cmd_processor.
 *
 *   Comunicação entre tasks via EventGroup:
 *     TCP_BIT_ACTIVE    → TX notifica RX: socket pronto para recv
 *     TCP_BIT_RX_ERROR  → RX notifica TX: recv() falhou
 *     TCP_BIT_RX_PONG   → RX notifica TX: PONG recebido
 *     TCP_BIT_CONNECT   → externo → TX: iniciar conexão
 *     TCP_BIT_DISCONNECT → externo → TX: desconectar
 *
 * THREAD SAFETY:
 *   s_sock escrito apenas por task_tx; lido por task_rx com barreira de memória.
 *   s_state _Atomic: leitura de qualquer task; escrita apenas por task_tx.
 */

#include "tcp_client.h"
#include "vk_protocol.h"
#include "vk_validator.h"
#include "command_processor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "../../config/app_config.h"
#include "../../config/board_profile.h"
#include "../../config/device_config.h"
#include "../events/event_dispatcher.h"
#include "../core/task_monitor.h"
#include "../core/vk_assert.h"
#include "wifi_manager.h"
#include <string.h>
#include <stdatomic.h>

static const char *TAG_TX = "TCP_TX";
static const char *TAG_RX = "TCP_RX";

/* =========================================================
 * EVENT GROUP BITS
 * ========================================================= */
#define TCP_BIT_ACTIVE      BIT(0)
#define TCP_BIT_RX_ERROR    BIT(1)
#define TCP_BIT_RX_PONG     BIT(2)
#define TCP_BIT_CONNECT     BIT(3)
#define TCP_BIT_DISCONNECT  BIT(4)

/* =========================================================
 * ESTADO COMPARTILHADO
 * ========================================================= */
static volatile int            s_sock           = -1;
static _Atomic tcp_conn_state_t s_state         = TCP_STATE_DISCONNECTED;
static EventGroupHandle_t      s_event_grp      = NULL;
/*
 * Written by task_tcp_rx (reset on HANDSHAKE_ACK) and read-modified by
 * task_tcp_tx (exponential backoff in schedule_reconnect).  On dual-core
 * ESP32-S3 both tasks can run simultaneously — _Atomic ensures sequential
 * consistency with no torn reads/writes.
 */
static _Atomic uint32_t        s_reconnect_delay_ms = APP_TCP_RECONNECT_MIN_MS;
static esp_timer_handle_t      s_reconnect_timer    = NULL;

/* Heartbeat — used by task_tcp_tx only */
static int64_t  s_last_ping_us = 0;
static uint8_t  s_ping_misses  = 0;

/* Número de sequência */
static _Atomic uint16_t s_seq = 0;

static char     s_server_host[64] = DEVICE_SERVER_HOST;
static uint16_t s_server_port     = DEVICE_TCP_PORT;
static char     s_device_id[sizeof(((pkt_handshake_hello_t *)0)->device_id)] = DEVICE_ID;
static char     s_ward_id[sizeof(((pkt_handshake_hello_t *)0)->ward_id)]      = DEVICE_WARD_ID;

static void load_runtime_string(nvs_handle_t nvs, const char *key, char *out, size_t out_size)
{
    out[0] = '\0';
    size_t len = out_size;
    if (nvs_get_str(nvs, key, out, &len) != ESP_OK) {
        out[out_size - 1] = '\0';
    }
}

#if DEVICE_CONFIG_FORCE_APPLY
/* Writes compiled device_config.h values to NVS, overwriting whatever is there.
 * Called once in tcp_client_init() when DEVICE_CONFIG_FORCE_APPLY=1.
 * Use to recover from stale NVS values after a failed provision run. */
static void force_apply_config_to_nvs(void)
{
    nvs_handle_t nvs;
    if (nvs_open(DEVICE_NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        ESP_LOGW(TAG_TX, "force_apply: cannot open NVS RW — compiled defaults will be used");
        return;
    }
    nvs_set_str(nvs, DEVICE_NVS_KEY_DEVICE_ID,   DEVICE_ID);
    nvs_set_str(nvs, DEVICE_NVS_KEY_WARD_ID,     DEVICE_WARD_ID);
    nvs_set_str(nvs, DEVICE_NVS_KEY_SERVER_HOST, DEVICE_SERVER_HOST);
    uint16_t port = DEVICE_TCP_PORT;
    nvs_set_u16(nvs, DEVICE_NVS_KEY_TCP_PORT, port);
    nvs_commit(nvs);
    nvs_close(nvs);
    ESP_LOGI(TAG_TX, "force_apply: NVS <- device_id=%s ward=%s host=%s port=%u",
             DEVICE_ID, DEVICE_WARD_ID, DEVICE_SERVER_HOST, (unsigned)DEVICE_TCP_PORT);
}
#endif

static void load_runtime_config(void)
{
    nvs_handle_t nvs;
    if (nvs_open(DEVICE_NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        ESP_LOGI(TAG_TX, "Usando endpoint/identidade compilados em device_config.h");
        return;
    }

    load_runtime_string(nvs, DEVICE_NVS_KEY_SERVER_HOST, s_server_host, sizeof(s_server_host));
    if (s_server_host[0] == '\0') {
        load_runtime_string(nvs, DEVICE_NVS_KEY_MQTT_HOST, s_server_host, sizeof(s_server_host));
    }
    if (s_server_host[0] == '\0') {
        strncpy(s_server_host, DEVICE_SERVER_HOST, sizeof(s_server_host) - 1);
        s_server_host[sizeof(s_server_host) - 1] = '\0';
    }

    uint16_t tcp_port = DEVICE_TCP_PORT;
    if (nvs_get_u16(nvs, DEVICE_NVS_KEY_TCP_PORT, &tcp_port) == ESP_OK && tcp_port > 0) {
        s_server_port = tcp_port;
    }

    load_runtime_string(nvs, DEVICE_NVS_KEY_DEVICE_ID, s_device_id, sizeof(s_device_id));
    if (s_device_id[0] == '\0') {
        strncpy(s_device_id, DEVICE_ID, sizeof(s_device_id) - 1);
        s_device_id[sizeof(s_device_id) - 1] = '\0';
    }

    load_runtime_string(nvs, DEVICE_NVS_KEY_WARD_ID, s_ward_id, sizeof(s_ward_id));
    if (s_ward_id[0] == '\0') {
        strncpy(s_ward_id, DEVICE_WARD_ID, sizeof(s_ward_id) - 1);
        s_ward_id[sizeof(s_ward_id) - 1] = '\0';
    }

    nvs_close(nvs);

    ESP_LOGI(TAG_TX, "Runtime config: host=%s tcp_port=%u device_id=%s ward=%s",
             s_server_host, (unsigned)s_server_port, s_device_id, s_ward_id);
}

/* =========================================================
 * QUEUE OWNERSHIP — s_critical_queue / s_normal_queue
 * =========================================================
 * CRITICAL QUEUE (s_critical_queue):
 *   Owner:     tcp_client module
 *   Created:   tcp_client_init() — before tasks start
 *   Depth:     APP_TCP_CRITICAL_QUEUE_DEPTH = 8
 *   Producers: Any task via tcp_client_send_critical()
 *              Priority: EMERGENCY events, ACK packets, session control
 *   Consumer:  task_tcp_tx (sole consumer)
 *   Overflow policy: xQueueSend with pdMS_TO_TICKS(5) timeout.
 *              On failure: packet dropped, s_dropped_critical incremented.
 *              Caller receives ESP_ERR_TIMEOUT and must handle (e.g., journal).
 *   Thread safety: xQueueSend/Receive are FreeRTOS-safe. task_tcp_tx is
 *              the only consumer — no concurrent recv() needed.
 *
 * NORMAL QUEUE (s_normal_queue):
 *   Owner:     tcp_client module
 *   Created:   tcp_client_init()
 *   Depth:     APP_TCP_NORMAL_QUEUE_DEPTH = 8  (reduced from 32 — see Bug P1)
 *   Producers: Any task via tcp_client_send_normal()
 *              Priority: telemetry, audio chunks, heartbeat pings
 *   Consumer:  task_tcp_tx (sole consumer, processes after draining critical)
 *   Overflow policy: xQueueSend with pdMS_TO_TICKS(10) timeout.
 *              On failure: packet dropped, logged. Caller should check
 *              backpressure_allow_telemetry() before sending telemetry.
 *
 * DUAL-QUEUE DISPATCH ORDER in task_tcp_tx:
 *   1. Drain ENTIRE critical queue (no limit — absolute priority)
 *   2. Process ONE normal queue entry
 *   3. Repeat → normal queue can never starve critical, but always progresses.
 * ========================================================= */

/* Filas de saída */
static QueueHandle_t    s_critical_queue = NULL;
static QueueHandle_t    s_normal_queue   = NULL;

/* Frame estático de handshake (task_tcp_tx apenas) */
static uint8_t  s_handshake_frame[VK_HEADER_SIZE
                                   + sizeof(pkt_handshake_hello_t)
                                   + VK_CRC_SIZE];

/* =========================================================
 * UTILITÁRIOS
 * ========================================================= */
uint16_t tcp_client_next_seq(void)
{
    return atomic_fetch_add(&s_seq, 1);
}

tcp_conn_state_t tcp_client_get_state(void)
{
    return atomic_load(&s_state);
}

bool tcp_client_is_connected(void)
{
    return atomic_load(&s_state) == TCP_STATE_ACTIVE;
}

QueueHandle_t tcp_client_get_normal_queue(void)
{
    return s_normal_queue;
}

/* =========================================================
 * FILAS DE SAÍDA
 * ========================================================= */
static bool enqueue_frame(QueueHandle_t q, const uint8_t *frame, uint16_t len,
                           TickType_t timeout)
{
    if (!q || !frame || len == 0) return false;
    if (len > TCP_FRAME_BUF_SIZE) {
        ESP_LOGE(TAG_TX, "Frame muito grande para fila: %u", len);
        return false;
    }
    tcp_queued_frame_t item;
    item.frame_len = len;
    memcpy(item.frame, frame, len);
    return xQueueSend(q, &item, timeout) == pdTRUE;
}

bool tcp_client_send_critical(const uint8_t *frame, uint16_t len)
{
    return enqueue_frame(s_critical_queue, frame, len, pdMS_TO_TICKS(5));
}

bool tcp_client_send_normal(const uint8_t *frame, uint16_t len)
{
    bool ok = enqueue_frame(s_normal_queue, frame, len, pdMS_TO_TICKS(20));
    if (!ok) {
        ESP_LOGW(TAG_TX, "Fila normal cheia — frame %u B descartado", len);
    }
    return ok;
}

/* =========================================================
 * ENVIO PELO SOCKET
 * ========================================================= */
static bool socket_send_all(const uint8_t *data, size_t len)
{
    int fd = s_sock;
    if (fd < 0) return false;
    size_t sent = 0;
    while (sent < len) {
        int n = send(fd, data + sent, len - sent, 0);
        if (n <= 0) {
            ESP_LOGE(TAG_TX, "send() falhou: errno=%d", errno);
            return false;
        }
        sent += n;
    }
    return true;
}

static bool drain_queue_to_socket(QueueHandle_t q)
{
    tcp_queued_frame_t item;
    while (xQueueReceive(q, &item, 0) == pdTRUE) {
        if (!socket_send_all(item.frame, item.frame_len)) {
            /* Falha: recoloca o item na fila e retorna erro */
            xQueueSendToFront(q, &item, 0);
            return false;
        }
    }
    return true;
}

/* =========================================================
 * TIMER DE RECONEXÃO
 * ========================================================= */
static void reconnect_timer_cb(void *arg)
{
    if (s_event_grp) {
        xEventGroupSetBits(s_event_grp, TCP_BIT_CONNECT);
    }
}

static void schedule_reconnect(void)
{
    if (s_reconnect_timer) {
        esp_timer_stop(s_reconnect_timer);
    } else {
        esp_timer_create_args_t args = {
            .callback = reconnect_timer_cb,
            .name     = "tcp_retry",
        };
        esp_timer_create(&args, &s_reconnect_timer);
    }
    ESP_LOGW(TAG_TX, "Reconectando em %lu ms", (unsigned long)s_reconnect_delay_ms);
    esp_timer_start_once(s_reconnect_timer,
                         (uint64_t)atomic_load(&s_reconnect_delay_ms) * 1000ULL);

    /* Backoff exponencial: load → compute → store, all via _Atomic */
    uint32_t cur  = atomic_load(&s_reconnect_delay_ms);
    uint32_t next = (cur <= APP_TCP_RECONNECT_MAX_MS / 2u)
                    ? cur * 2u : APP_TCP_RECONNECT_MAX_MS;
    atomic_store(&s_reconnect_delay_ms, next);
}

/* =========================================================
 * DESCONEXÃO
 *
 * VALID TRANSITIONS INTO DISCONNECTED:
 *   CONNECTING    → DISCONNECTED  (connect() failed)
 *   CONNECTED     → DISCONNECTED  (setsockopt/handshake failed)
 *   HANDSHAKE     → DISCONNECTED  (no ACK, timeout)
 *   ACTIVE        → DISCONNECTED  (recv error, ping timeout, external request)
 *   DISCONNECTED  → DISCONNECTED  (idempotent — reconnect timer may call twice)
 * ========================================================= */
static void do_disconnect(bool schedule)
{
    tcp_conn_state_t prev = atomic_load(&s_state);
    /* All states are valid predecessors for disconnection — idempotent */
    VK_ASSERT_FSM(prev < TCP_STATE_COUNT,
                  "do_disconnect: invalid prior state %d", (int)prev);

    int fd = s_sock;
    if (fd >= 0) {
        s_sock = -1;   /* task_rx verá -1 e sairá do loop */
        close(fd);
    }
    atomic_store(&s_state, TCP_STATE_DISCONNECTED);
    if (s_event_grp) xEventGroupClearBits(s_event_grp, TCP_BIT_ACTIVE);
    s_ping_misses = 0;

    system_event_t evt = {
        .type         = EVT_TCP_DISCONNECTED,
        .priority     = EVT_PRIO_HIGH,
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000),
    };
    event_post_critical(&evt, false);
    ESP_LOGW(TAG_TX, "Desconectado");

    if (schedule) schedule_reconnect();
}

/* =========================================================
 * CONEXÃO E HANDSHAKE
 *
 * VALID TRANSITIONS:
 *   DISCONNECTED → CONNECTING → CONNECTED → HANDSHAKE → (RX task sets ACTIVE)
 *
 * INVALID (guarded by VK_ASSERT_FSM):
 *   ACTIVE → do_connect()  would open a second socket — must call do_disconnect first.
 *   HANDSHAKE → do_connect() same issue.
 *
 * FAILURE MODES:
 *   getaddrinfo fail → DISCONNECTED (DNS not yet ready)
 *   socket() fail    → DISCONNECTED (lwIP out of sockets — heap low?)
 *   connect() fail   → DISCONNECTED (server unreachable — schedule_reconnect)
 *   HELLO send fail  → DISCONNECTED (socket closed before data sent)
 * ========================================================= */
static esp_err_t do_connect(void)
{
    tcp_conn_state_t prev = atomic_load(&s_state);
    VK_ASSERT_FSM(prev == TCP_STATE_DISCONNECTED,
                  "do_connect called from non-DISCONNECTED state: %d", (int)prev);

    ESP_LOGI(TAG_TX, "Conectando ao backend %s:%u ...",
             s_server_host, (unsigned)s_server_port);
    atomic_store(&s_state, TCP_STATE_CONNECTING);

    struct addrinfo hints = {
        .ai_family   = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res = NULL;
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", (unsigned)s_server_port);

    if (getaddrinfo(s_server_host, port_str, &hints, &res) != 0 || !res) {
        ESP_LOGE(TAG_TX, "getaddrinfo falhou para %s:%u — servidor atingivel?",
                 s_server_host, (unsigned)s_server_port);
        atomic_store(&s_state, TCP_STATE_DISCONNECTED);
        return ESP_FAIL;
    }

    int sock = socket(res->ai_family, res->ai_socktype, 0);
    if (sock < 0) {
        freeaddrinfo(res);
        atomic_store(&s_state, TCP_STATE_DISCONNECTED);
        return ESP_FAIL;
    }

    /* Timeout para recv no socket (usado por task_rx) */
    struct timeval tv = { .tv_sec = APP_TCP_RECV_TIMEOUT_MS / 1000,
                          .tv_usec = (APP_TCP_RECV_TIMEOUT_MS % 1000) * 1000 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    /* Timeout de send — TX usa send() com timeout de 5s */
    struct timeval tv_send = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv_send, sizeof(tv_send));

    int flag = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    if (connect(sock, res->ai_addr, res->ai_addrlen) != 0) {
        ESP_LOGE(TAG_TX, "connect() falhou: errno=%d", errno);
        close(sock);
        freeaddrinfo(res);
        atomic_store(&s_state, TCP_STATE_DISCONNECTED);
        return ESP_FAIL;
    }
    freeaddrinfo(res);

    s_sock  = sock;
    atomic_store(&s_state, TCP_STATE_CONNECTED);
    ESP_LOGI(TAG_TX, "Socket conectado a %s:%u", s_server_host, (unsigned)s_server_port);

    /* Envia HELLO */
    pkt_handshake_hello_t hello = {0};
    strncpy(hello.device_id,    s_device_id,            sizeof(hello.device_id) - 1);
    strncpy(hello.ward_id,      s_ward_id,              sizeof(hello.ward_id) - 1);
    strncpy(hello.fw_version,   APP_FIRMWARE_VERSION,   sizeof(hello.fw_version) - 1);
    hello.capabilities = VK_BOARD_CAPABILITIES;

    int flen = vk_frame_build(s_handshake_frame, sizeof(s_handshake_frame),
                              PKT_HANDSHAKE_HELLO, tcp_client_next_seq(),
                              &hello, sizeof(hello));
    if (flen <= 0 || !socket_send_all(s_handshake_frame, (size_t)flen)) {
        do_disconnect(false);
        return ESP_FAIL;
    }

    atomic_store(&s_state, TCP_STATE_HANDSHAKE);
    /* Ativa TCP_BIT_ACTIVE para que task_rx comece a ler */
    if (s_event_grp) xEventGroupSetBits(s_event_grp, TCP_BIT_ACTIVE);
    ESP_LOGI(TAG_TX, "HELLO enviado — aguardando ACK");
    return ESP_OK;
}

/* =========================================================
 * INICIALIZAÇÃO E API DE CONTROLE
 * ========================================================= */
esp_err_t tcp_client_init(void)
{
    s_event_grp      = xEventGroupCreate();
    s_critical_queue = xQueueCreate(APP_TCP_CRITICAL_QUEUE_DEPTH,
                                    sizeof(tcp_queued_frame_t));
    s_normal_queue   = xQueueCreate(APP_TCP_NORMAL_QUEUE_DEPTH,
                                    sizeof(tcp_queued_frame_t));

    if (!s_event_grp || !s_critical_queue || !s_normal_queue) {
        return ESP_ERR_NO_MEM;
    }
#if DEVICE_CONFIG_FORCE_APPLY
    force_apply_config_to_nvs();
#endif
    load_runtime_config();
    ESP_LOGI(TAG_TX, "tcp_client inicializado");
    return ESP_OK;
}

esp_err_t tcp_client_connect(void)
{
    if (!s_event_grp) return ESP_ERR_INVALID_STATE;
    xEventGroupSetBits(s_event_grp, TCP_BIT_CONNECT);
    return ESP_OK;
}

void tcp_client_disconnect(void)
{
    if (s_event_grp) xEventGroupSetBits(s_event_grp, TCP_BIT_DISCONNECT);
}

/* =========================================================
 * task_tcp_tx — FSM de conexão, heartbeat, envio de filas
 * ========================================================= */
void task_tcp_tx(void *arg)
{
    ESP_LOGI(TAG_TX, "task_tcp_tx iniciada");

    static uint8_t s_ping_frame[VK_HEADER_SIZE + sizeof(pkt_heartbeat_t) + VK_CRC_SIZE];
    static int64_t s_wifi_idle_start_us = 0;   /* for WiFi-up fallback */

    for (;;) {
        task_monitor_heartbeat(TASK_ID_TCP_TX);
        EventBits_t bits = xEventGroupWaitBits(
            s_event_grp,
            TCP_BIT_CONNECT | TCP_BIT_DISCONNECT | TCP_BIT_RX_ERROR | TCP_BIT_RX_PONG,
            pdTRUE, pdFALSE, pdMS_TO_TICKS(50)
        );

        if (bits & TCP_BIT_DISCONNECT) {
            do_disconnect(false);
            continue;
        }

        if (bits & TCP_BIT_RX_ERROR) {
            if (atomic_load(&s_state) >= TCP_STATE_HANDSHAKE) {
                ESP_LOGE(TAG_TX, "Erro de RX — reconectando");
                do_disconnect(true);
            }
            continue;
        }

        if (bits & TCP_BIT_RX_PONG) {
            s_ping_misses = 0;
        }

        if ((bits & TCP_BIT_CONNECT) &&
            atomic_load(&s_state) == TCP_STATE_DISCONNECTED) {
            if (do_connect() != ESP_OK) {
                schedule_reconnect();
            }
            s_wifi_idle_start_us = 0;   /* reset fallback timer after explicit connect */
            continue;
        }

        /* WiFi-up fallback: if EVT_WIFI_CONNECTED was missed (event queue overflow or
         * dispatcher race), force a connection attempt after 5 s of WiFi being up. */
        if (atomic_load(&s_state) == TCP_STATE_DISCONNECTED) {
            if (wifi_manager_is_connected()) {
                int64_t now_us = esp_timer_get_time();
                if (s_wifi_idle_start_us == 0) {
                    s_wifi_idle_start_us = now_us;
                    ESP_LOGI(TAG_TX, "WiFi conectado, TCP desconectado — aguardando evento");
                } else if ((now_us - s_wifi_idle_start_us) > 5000000LL) {
                    s_wifi_idle_start_us = now_us;
                    ESP_LOGW(TAG_TX, "EVT_WIFI_CONNECTED nao recebido — forcando conexao TCP");
                    if (do_connect() != ESP_OK) schedule_reconnect();
                    continue;
                }
            } else {
                s_wifi_idle_start_us = 0;
            }
        } else {
            s_wifi_idle_start_us = 0;
        }

        tcp_conn_state_t cur = atomic_load(&s_state);
        if (cur < TCP_STATE_HANDSHAKE) continue;

        /* Heartbeat */
        int64_t now_us = esp_timer_get_time();
        int64_t interval_us = (int64_t)APP_TCP_HEARTBEAT_INTERVAL_MS * 1000LL;

        if (cur == TCP_STATE_ACTIVE && (now_us - s_last_ping_us) >= interval_us) {
            pkt_heartbeat_t ping = {
                .uptime_ms = (uint32_t)(now_us / 1000),
                .rssi      = wifi_manager_get_rssi(),
            };
            int flen = vk_frame_build(s_ping_frame, sizeof(s_ping_frame),
                                      PKT_HEARTBEAT_PING, tcp_client_next_seq(),
                                      &ping, sizeof(ping));
            if (flen > 0) socket_send_all(s_ping_frame, flen);
            s_last_ping_us = now_us;
            s_ping_misses++;

            if (s_ping_misses > APP_TCP_HEARTBEAT_MISS_MAX) {
                ESP_LOGE(TAG_TX, "Heartbeat timeout (%d misses)", s_ping_misses);
                do_disconnect(true);
                continue;
            }
        }

        /* Envio de filas (apenas quando ACTIVE) */
        if (cur == TCP_STATE_ACTIVE) {
            if (!drain_queue_to_socket(s_critical_queue) ||
                !drain_queue_to_socket(s_normal_queue)) {
                ESP_LOGE(TAG_TX, "Falha ao enviar — reconectando");
                do_disconnect(true);
            }
        }
    }
}

/* =========================================================
 * task_tcp_rx — recv puro, parsing, roteamento para cmd_processor
 * ========================================================= */
#define RECV_BUF_SIZE  (VK_MAX_FRAME_SIZE + 256)

void task_tcp_rx(void *arg)
{
    ESP_LOGI(TAG_RX, "task_tcp_rx iniciada");

    static uint8_t s_recv_buf[RECV_BUF_SIZE];
    size_t         recv_head = 0;

    for (;;) {
        /* Aguarda socket ACTIVE com timeout para renovar heartbeat enquanto
         * desconectado. PROBLEMA CORRIGIDO: portMAX_DELAY bloqueava sem chamar
         * task_monitor_heartbeat → stall detectado em 15 s → reboot em 30 s
         * sempre que o TCP estava desconectado. */
        task_monitor_heartbeat(TASK_ID_TCP_RX);
        EventBits_t active = xEventGroupWaitBits(
            s_event_grp, TCP_BIT_ACTIVE, pdFALSE, pdTRUE,
            pdMS_TO_TICKS(10000));   /* 10 s — bem abaixo de FAULT_THRESHOLD=15 s */
        if (!(active & TCP_BIT_ACTIVE)) continue;

        recv_head = 0;
        vk_validator_reset();  /* Clear sequence window — new session, new seq space */
        ESP_LOGD(TAG_RX, "Socket ACTIVE — recv loop");

        for (;;) {
            int fd = s_sock;
            if (fd < 0) break;
            if (!(xEventGroupGetBits(s_event_grp) & TCP_BIT_ACTIVE)) break;

            size_t space = RECV_BUF_SIZE - recv_head;
            if (space == 0) {
                ESP_LOGE(TAG_RX, "Ring buffer cheio — descartando");
                recv_head = 0;
                continue;
            }

            task_monitor_heartbeat(TASK_ID_TCP_RX);
            int n = recv(fd, s_recv_buf + recv_head, space, 0);

            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
                ESP_LOGE(TAG_RX, "recv() errno=%d — notificando TX", errno);
                xEventGroupSetBits(s_event_grp, TCP_BIT_RX_ERROR);
                break;
            }
            if (n == 0) {
                ESP_LOGW(TAG_RX, "Peer fechou conexao");
                xEventGroupSetBits(s_event_grp, TCP_BIT_RX_ERROR);
                break;
            }
            recv_head += n;

            size_t consumed = 0;
            while (consumed < recv_head) {
                const vk_header_t *hdr   = NULL;
                const uint8_t     *pload = NULL;
                size_t             flen  = 0;

                /*
                 * Full frame validation: magic, version, payload length,
                 * CRC-16, replay window, and packet type range check.
                 * On any error other than INCOMPLETE: advance 1 byte
                 * ("magic byte resync") to recover stream alignment.
                 */
                vk_validation_result_t vr = vk_validate_frame(
                    s_recv_buf + consumed, recv_head - consumed,
                    &hdr, &pload, &flen);

                if (vr == VK_ERR_INCOMPLETE) {
                    break;  /* Need more bytes — wait for next recv() */
                }
                if (vr != VK_VALID) {
                    vk_validator_log_error(vr, s_recv_buf + consumed,
                                           recv_head - consumed);
                    consumed++;  /* Advance 1 byte — attempt magic resync */
                    continue;
                }

                pkt_type_t ptype = (pkt_type_t)hdr->pkt_type;

                if (ptype == PKT_HANDSHAKE_ACK &&
                    atomic_load(&s_state) == TCP_STATE_HANDSHAKE) {

                    atomic_store(&s_state, TCP_STATE_ACTIVE);
                    atomic_store(&s_reconnect_delay_ms, APP_TCP_RECONNECT_MIN_MS);
                    ESP_LOGI(TAG_RX, "Handshake OK — ACTIVE");

                    system_event_t evt = {
                        .type         = EVT_TCP_CONNECTED,
                        .priority     = EVT_PRIO_HIGH,
                        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000),
                    };
                    event_post_critical(&evt, false);

                } else if (ptype == PKT_HEARTBEAT_PONG) {
                    xEventGroupSetBits(s_event_grp, TCP_BIT_RX_PONG);

                } else {
                    QueueHandle_t cq = cmd_processor_get_queue();
                    if (cq) {
                        tcp_queued_frame_t item;
                        size_t copy_len = flen < TCP_FRAME_BUF_SIZE
                                          ? flen : TCP_FRAME_BUF_SIZE;
                        item.frame_len = (uint16_t)copy_len;
                        memcpy(item.frame, s_recv_buf + consumed, copy_len);
                        xQueueSend(cq, &item, 0);
                    }
                }
                consumed += flen;
            }

            if (consumed > 0 && consumed < recv_head) {
                memmove(s_recv_buf, s_recv_buf + consumed, recv_head - consumed);
            }
            recv_head -= consumed;
        }
    }
}

/* =========================================================
 * ALIAS LEGADO
 * ========================================================= */
void task_tcp_client(void *arg)
{
    xTaskCreate(task_tcp_rx, "tcp_rx", 4096, NULL, 4, NULL);
    task_tcp_tx(arg);
}
