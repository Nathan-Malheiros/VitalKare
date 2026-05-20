/**
 * @file wifi_manager.c
 * @brief Gerenciamento Wi-Fi com reconexão automática por backoff exponencial
 *
 * Lê SSID e senha da partição NVS provisionada pelo projeto.
 * Fallback para valores hardcoded apenas em modo de desenvolvimento.
 */

#include "wifi_manager.h"
#include "esp_log.h"
#include "../../config/device_config.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "../../config/app_config.h"
#include "../../config/board_profile.h"
#include "../events/event_dispatcher.h"
#include <string.h>

static const char *TAG = "WIFI";

/* Bit de grupo de eventos — IP obtido */
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static EventGroupHandle_t   s_wifi_event_group = NULL;
static wifi_state_cb_t      s_state_callback    = NULL;
static bool                 s_connected         = false;
static int8_t               s_rssi              = 0;
static uint32_t             s_retry_count       = 0;
static bool                 s_static_ip_active  = false;

/* =========================================================
 * TIMER DE RECONEXÃO — evita vTaskDelay dentro de event handler
 * ========================================================= */
static esp_timer_handle_t s_retry_timer = NULL;

typedef struct {
    bool enabled;
    char ip[16];
    char gateway[16];
    char netmask[16];
} wifi_static_ip_config_t;

static void load_nvs_string(nvs_handle_t nvs, const char *key, char *out, size_t out_size)
{
    out[0] = '\0';
    size_t len = out_size;
    if (nvs_get_str(nvs, key, out, &len) != ESP_OK) {
        out[0] = '\0';
        out[out_size - 1] = '\0';
        return;
    }
    out[out_size - 1] = '\0';
}

static bool load_wifi_credentials_from_namespace(const char *ns,
                                                 char *ssid, size_t ssid_size,
                                                 char *pass, size_t pass_size)
{
    nvs_handle_t nvs;
    size_t ssid_len = ssid_size;
    size_t pass_len = pass_size;

    if (nvs_open(ns, NVS_READONLY, &nvs) != ESP_OK) {
        return false;
    }

    esp_err_t ssid_ret = nvs_get_str(nvs, DEVICE_NVS_KEY_WIFI_SSID, ssid, &ssid_len);
    esp_err_t pass_ret = nvs_get_str(nvs, DEVICE_NVS_KEY_WIFI_PASS, pass, &pass_len);
    nvs_close(nvs);

    return ssid_ret == ESP_OK && pass_ret == ESP_OK && ssid[0] != '\0';
}

static bool load_static_ip_config(wifi_static_ip_config_t *cfg)
{
    if (!cfg) {
        return false;
    }

    memset(cfg, 0, sizeof(*cfg));

    nvs_handle_t nvs;
    if (nvs_open(DEVICE_NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return false;
    }

    uint8_t enabled = 0;
    if (nvs_get_u8(nvs, DEVICE_NVS_KEY_WIFI_STATIC_EN, &enabled) != ESP_OK || enabled == 0) {
        nvs_close(nvs);
        return false;
    }

    cfg->enabled = true;
    load_nvs_string(nvs, DEVICE_NVS_KEY_WIFI_STATIC_IP, cfg->ip, sizeof(cfg->ip));
    load_nvs_string(nvs, DEVICE_NVS_KEY_WIFI_STATIC_GW, cfg->gateway, sizeof(cfg->gateway));
    load_nvs_string(nvs, DEVICE_NVS_KEY_WIFI_STATIC_NM, cfg->netmask, sizeof(cfg->netmask));
    nvs_close(nvs);

    return cfg->ip[0] != '\0' && cfg->gateway[0] != '\0' && cfg->netmask[0] != '\0';
}

static bool parse_ipv4_string(const char *value, esp_ip4_addr_t *out)
{
    if (!value || !out) {
        return false;
    }
    return esp_netif_str_to_ip4(value, out) == ESP_OK;
}

static bool apply_static_ip_if_configured(void)
{
    wifi_static_ip_config_t cfg;
    if (!load_static_ip_config(&cfg)) {
        s_static_ip_active = false;
        return false;
    }

    esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!sta_netif) {
        ESP_LOGE(TAG, "Configuracao de IP fixo encontrada, mas a interface STA nao foi localizada");
        s_static_ip_active = false;
        return false;
    }

    esp_netif_ip_info_t ip_info = {0};
    if (!parse_ipv4_string(cfg.ip, &ip_info.ip)
        || !parse_ipv4_string(cfg.gateway, &ip_info.gw)
        || !parse_ipv4_string(cfg.netmask, &ip_info.netmask)) {
        ESP_LOGE(TAG, "Configuracao de IP fixo invalida na NVS (ip=%s gw=%s mask=%s) — usando DHCP",
                 cfg.ip, cfg.gateway, cfg.netmask);
        s_static_ip_active = false;
        return false;
    }

    esp_err_t ret = esp_netif_dhcpc_stop(sta_netif);
    if (ret != ESP_OK && ret != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        ESP_LOGE(TAG, "Falha ao parar DHCP antes de aplicar IP fixo: %s", esp_err_to_name(ret));
        s_static_ip_active = false;
        return false;
    }

    ret = esp_netif_set_ip_info(sta_netif, &ip_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao aplicar IP fixo %s: %s", cfg.ip, esp_err_to_name(ret));
        s_static_ip_active = false;
        return false;
    }

    s_static_ip_active = true;
    ESP_LOGI(TAG, "IP fixo configurado antes do start: ip=%s gw=%s mask=%s",
             cfg.ip, cfg.gateway, cfg.netmask);
    return true;
}

static void reconnect_timer_cb(void *arg)
{
    /* NOTA: esp_timer_delete() NÃO pode ser chamado de dentro de um callback do
     * próprio timer — causaria panic/assertion no esp_timer_dispatch_task.
     * O timer one-shot já está "expirado" ao chegar aqui; apenas zeramos o
     * ponteiro para que schedule_reconnect() saiba que pode criar um novo.
     * O delete real acontece em schedule_reconnect() antes de criar o próximo. */
    s_retry_timer = NULL;
    ESP_LOGI(TAG, "Timer de reconexao disparado — chamando esp_wifi_connect()");
    esp_wifi_connect();
}

static void schedule_reconnect(uint32_t delay_ms)
{
    /* Cancela timer anterior se ainda estiver pendente */
    if (s_retry_timer) {
        esp_timer_stop(s_retry_timer);
        esp_timer_delete(s_retry_timer);
        s_retry_timer = NULL;
    }

    esp_timer_create_args_t args = {
        .callback = reconnect_timer_cb,
        .arg      = NULL,
        .name     = "wifi_retry",
    };
    if (esp_timer_create(&args, &s_retry_timer) == ESP_OK) {
        esp_timer_start_once(s_retry_timer, (uint64_t)delay_ms * 1000ULL);
    } else {
        ESP_LOGE(TAG, "Falha ao criar timer de reconexao — reconectando imediatamente");
        esp_wifi_connect();
    }
}

/* =========================================================
 * HANDLERS DE EVENTOS DO ESP-IDF
 * ========================================================= */
static void on_wifi_event(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    if (id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();

    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)data;
        s_connected = false;
        s_rssi = 0;

        /* Backoff exponencial: min 2 s, max 120 s.
         * Limita o shift a 6 bits (64×) para evitar overflow de uint32_t
         * antes da comparação com APP_RECONNECT_MAX_DELAY_MS. */
        uint32_t shift = (s_retry_count < 6u) ? s_retry_count : 6u;
        uint32_t delay_ms = APP_RECONNECT_MIN_DELAY_MS * (1u << shift);
        if (delay_ms > APP_RECONNECT_MAX_DELAY_MS) {
            delay_ms = APP_RECONNECT_MAX_DELAY_MS;
        }
        s_retry_count++;

        ESP_LOGW(TAG, "Wi-Fi desconectado (reason=%u). Tentativa %lu em %lu ms",
                 event ? (unsigned)event->reason : 0u,
                 s_retry_count, delay_ms);

        /* Publica evento de desconexão */
        system_event_t evt = {
            .type         = EVT_WIFI_DISCONNECTED,
            .priority     = EVT_PRIO_HIGH,
            .timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS,
        };
        event_post(&evt, false);

        /* CORREÇÃO P0: usa timer assíncrono — NÃO bloqueia o event loop */
        schedule_reconnect(delay_ms);
    }
}

static void on_ip_event(void *arg, esp_event_base_t base,
                        int32_t id, void *data)
{
    if (id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        const char *mode = s_static_ip_active ? "FIXO" : "DHCP";
        ESP_LOGI(TAG, "IP obtido (%s): " IPSTR, mode, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "VK_IP: mode=%s ip=" IPSTR " gw=" IPSTR " netmask=" IPSTR,
                 mode,
                 IP2STR(&event->ip_info.ip),
                 IP2STR(&event->ip_info.gw),
                 IP2STR(&event->ip_info.netmask));

        s_connected  = true;
        s_retry_count = 0;

        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            s_rssi = ap_info.rssi;
        }

        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        /* Publica evento de conexão */
        system_event_t evt = {
            .type = EVT_WIFI_CONNECTED,
            .priority = EVT_PRIO_MEDIUM,
            .timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS,
        };
        event_post(&evt, false);

        if (s_state_callback) {
            s_state_callback(true, s_rssi);
        }
    }
}

/* =========================================================
 * INICIALIZAÇÃO
 * ========================================================= */
esp_err_t wifi_manager_init(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_event, NULL, NULL));

    char ssid[64] = {0};
    char pass[64] = {0};
    bool loaded_from_nvs =
        load_wifi_credentials_from_namespace(DEVICE_NVS_NAMESPACE,
                                             ssid, sizeof(ssid),
                                             pass, sizeof(pass))
        || load_wifi_credentials_from_namespace("wifi_cfg",
                                                ssid, sizeof(ssid),
                                                pass, sizeof(pass));

    if (!loaded_from_nvs || strlen(ssid) == 0) {
        if (VK_BOARD_PROFILE == VK_BOARD_PROFILE_ESP32S3_DEVKIT) {
            ESP_LOGW(TAG, "NVS vazia no perfil DevKit — provisione SSID/senha via provision.py ou system-startup-assistant.bat");
            return ESP_ERR_NOT_FOUND;
        }

        /* Fallback para credenciais compiladas em device_config.h */
        if (DEVICE_WIFI_SSID[0] != '\0') {
            ESP_LOGW(TAG, "NVS vazia — usando credenciais de device_config.h (%s)", DEVICE_WIFI_SSID);
            strncpy(ssid, DEVICE_WIFI_SSID, sizeof(ssid) - 1);
            strncpy(pass, DEVICE_WIFI_PASS, sizeof(pass) - 1);
        } else {
            // --- Modo de configuração via serial ---
            printf("\n--- CONFIGURAÇÃO WI-FI VIA SERIAL ---\n");
            printf("Digite o SSID: ");
            fflush(stdout);
            if (fgets(ssid, sizeof(ssid), stdin) == NULL) {
                ESP_LOGE(TAG, "Erro ao ler SSID");
                return ESP_ERR_INVALID_ARG;
            }
            ssid[strcspn(ssid, "\n")] = 0;
            printf("Digite a senha: ");
            fflush(stdout);
            if (fgets(pass, sizeof(pass), stdin) == NULL) {
                ESP_LOGE(TAG, "Erro ao ler senha");
                return ESP_ERR_INVALID_ARG;
            }
            pass[strcspn(pass, "\n")] = 0;
            if (strlen(ssid) == 0) {
                ESP_LOGE(TAG, "SSID inválido");
                return ESP_ERR_INVALID_ARG;
            }
            // Salva na NVS
            nvs_handle_t nvs;
            if (nvs_open(DEVICE_NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
                nvs_set_str(nvs, DEVICE_NVS_KEY_WIFI_SSID, ssid);
                nvs_set_str(nvs, DEVICE_NVS_KEY_WIFI_PASS, pass);
                nvs_commit(nvs);
                nvs_close(nvs);
                printf("\nCredenciais salvas! Reinicie o dispositivo para conectar ao Wi-Fi.\n");
            } else {
                ESP_LOGE(TAG, "Falha ao abrir NVS para escrita");
                return ESP_FAIL;
            }
            return ESP_ERR_NOT_FOUND; // Não tenta conectar agora, pede reboot
        }
    }

    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, pass, sizeof(wifi_cfg.sta.password) - 1);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_cfg.sta.pmf_cfg.capable    = true;
    wifi_cfg.sta.pmf_cfg.required   = false;

    if (apply_static_ip_if_configured()) {
        ESP_LOGI(TAG, "Wi-Fi sera iniciado com IP fixo persistido em NVS");
    } else {
        ESP_LOGI(TAG, "Wi-Fi sera iniciado usando DHCP");
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi iniciado. Conectando a '%s'...", ssid);
    return ESP_OK;
}

/* =========================================================
 * API
 * ========================================================= */
void wifi_manager_set_callback(wifi_state_cb_t cb) { s_state_callback = cb; }
bool wifi_manager_is_connected(void)               { return s_connected; }
int8_t wifi_manager_get_rssi(void)                 { return s_rssi; }

void wifi_manager_reconnect(void)
{
    s_retry_count = 0;
    esp_wifi_disconnect();
}

void task_wifi_manager(void *arg)
{
    /* Esta task é opcional — o loop principal de reconexão
     * já é gerenciado pelos event handlers. Pode ser usada
     * para monitoramento periódico do RSSI. */
    for (;;) {
        if (s_connected) {
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                s_rssi = ap_info.rssi;
                ESP_LOGD(TAG, "RSSI: %d dBm", s_rssi);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(30000)); /* Verifica RSSI a cada 30 s */
    }
}
