/**
 * @file event_dispatcher.h
 * @brief Fila central de eventos do VitalKare — Dual-Priority Queue
 *
 * Arquitetura de dois níveis:
 *  - s_critical_queue: emergências, press/release de botão, TCP status
 *  - s_normal_queue:   telemetria, sensores, display, storage
 *
 * O task_event_dispatcher drena TODA a fila crítica antes de processar
 * um evento normal, garantindo latência determinística para eventos
 * de emergência independentemente da carga de telemetria.
 *
 * Padrão: Event-Driven Architecture — Dual-Priority Dispatcher.
 */

#ifndef VITALKARE_EVENT_DISPATCHER_H
#define VITALKARE_EVENT_DISPATCHER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* =========================================================
 * TIPOS DE EVENTOS
 * ========================================================= */
typedef enum {
    EVT_NONE = 0,

    /* ── Botões (CRÍTICO) ──────────────────────────────── */
    EVT_BTN_A_PRESSED,          /**< Botão A pressionado  → gravar + solicitar */
    EVT_BTN_A_RELEASED,         /**< Botão A solto        → parar gravação */
    EVT_BTN_B_PRESSED,          /**< Botão B pressionado  → emergência + gravar */
    EVT_BTN_B_RELEASED,         /**< Botão B solto        → parar gravação */

    /* Aliases legados — mapeados internamente */
    EVT_BTN_REQUEST   = EVT_BTN_A_PRESSED,
    EVT_BTN_EMERGENCY = EVT_BTN_B_PRESSED,

    /* ── Áudio ─────────────────────────────────────────── */
    EVT_AUDIO_START_REQUEST,    /**< Inicia gravação por solicitação (A) */
    EVT_AUDIO_START_EMERGENCY,  /**< Inicia gravação por emergência (B) */
    EVT_AUDIO_STOP,             /**< Para gravação (liberação de botão ou timeout) */
    EVT_AUDIO_CHUNK_READY,      /**< Chunk de áudio pronto para encoding */
    EVT_AUDIO_SESSION_DONE,     /**< Sessão de áudio completa — todos os chunks enviados */

    /* ── Sensores ──────────────────────────────────────── */
    EVT_SENSOR_READING,         /**< Nova leitura ambiental (temp, umid, lux) */
    EVT_SENSOR_MOTION,          /**< Movimento detectado pelo MPU6050 */
    EVT_OXIMETER_READING,       /**< Nova leitura do oxímetro (BPM + SpO2) */
    EVT_OXIMETER_FINGER_LOST,   /**< Dedo removido do sensor */

    /* ── Câmera ────────────────────────────────────────── */
    EVT_CAMERA_CAPTURE_REQ,
    EVT_CAMERA_STREAM_REQ,
    EVT_CAMERA_STREAM_STOP,

    /* ── Rede ──────────────────────────────────────────── */
    EVT_WIFI_CONNECTED,
    EVT_WIFI_DISCONNECTED,
    EVT_TCP_CONNECTED,          /**< TCP socket conectado e handshake OK */
    EVT_TCP_DISCONNECTED,       /**< TCP socket perdido */
    EVT_TCP_SYNCED,             /**< Cache offline sincronizado após reconexão */
    EVT_HTTP_CONNECTED,         /**< Legado — HTTP client disponível */
    EVT_HTTP_DISCONNECTED,
    EVT_HTTP_CMD_RECEIVED,

    /* ── Sistema ───────────────────────────────────────── */
    EVT_SYSTEM_HEARTBEAT,
    EVT_ACK_RECEIVED,           /**< Operador confirmou atendimento */
    EVT_OFFLINE_SYNC_NEEDED,
    EVT_CMD_DISPLAY_UPDATE,     /**< Backend enviou comando de display */
    EVT_CMD_BUZZER,             /**< Backend enviou comando de buzzer */
    EVT_CMD_OTA_START,          /**< Backend iniciou OTA */

    EVT_TYPE_COUNT              /**< Sentinela — deve ser o último */
} event_type_t;

/* =========================================================
 * PRIORIDADES DE EVENTO
 * ========================================================= */
typedef enum {
    EVT_PRIO_CRITICAL   = 1,    /**< Emergência — usa fila crítica */
    EVT_PRIO_HIGH       = 2,    /**< Alta — usa fila crítica */
    EVT_PRIO_MEDIUM     = 3,    /**< Média — usa fila normal */
    EVT_PRIO_LOW        = 4,    /**< Baixa — usa fila normal */
    EVT_PRIO_INFO       = 5,    /**< Informativo — usa fila normal */
} event_priority_t;

/* =========================================================
 * ESTRUTURA DE EVENTO
 * ========================================================= */
typedef struct {
    event_type_t        type;
    event_priority_t    priority;
    uint32_t            timestamp_ms;
    uint8_t             data[64];       /**< Payload inline pequeno */
    uint16_t            data_len;
    void               *extra_ptr;     /**< Dados maiores (ponteiro para pool) */
} system_event_t;

/* =========================================================
 * CALLBACK DE HANDLER
 * ========================================================= */
typedef void (*event_handler_fn_t)(const system_event_t *event);

/* =========================================================
 * API PÚBLICA
 * ========================================================= */

/** @brief Inicializa o dispatcher: cria as duas filas internas. */
esp_err_t event_dispatcher_init(void);

/** @brief Task FreeRTOS do dispatcher — não chamar diretamente. */
void task_event_dispatcher(void *arg);

/**
 * @brief Publica evento em fila crítica (prioridade CRITICAL ou HIGH).
 *
 * Eventos críticos são sempre processados antes de qualquer evento normal.
 * Thread-safe; pode ser chamado de ISR (from_isr=true).
 */
bool event_post_critical(const system_event_t *event, bool from_isr);

/**
 * @brief Publica evento em fila normal (prioridade MEDIUM ou abaixo).
 * Thread-safe; NÃO deve ser chamado de ISR.
 */
bool event_post_normal(const system_event_t *event);

/**
 * @brief Seleciona automaticamente a fila com base em event->priority.
 *
 * CRITICAL/HIGH → fila crítica; demais → fila normal.
 * Thread-safe; from_isr=true habilita o caminho de ISR.
 */
bool event_post(const system_event_t *event, bool from_isr);

/** @brief Registra handler para um tipo de evento específico. */
esp_err_t event_register_handler(event_type_t type, event_handler_fn_t handler);

/** @brief Retorna a fila normal (uso interno — não usar diretamente). */
QueueHandle_t event_get_queue(void);

#endif /* VITALKARE_EVENT_DISPATCHER_H */
