/**
 * @file event_dispatcher.h
 * @brief Fila central de eventos do VitalKare
 *
 * Todas as tasks publicam eventos nesta fila central.
 * A task_event_dispatcher consome e roteia cada evento
 * para os handlers registrados (MQTT publish, log, OLED, etc.).
 *
 * Padrão: Event-Driven Architecture no firmware.
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

    /* Botões */
    EVT_BTN_REQUEST,            /**< Botão A pressionado — solicitação */
    EVT_BTN_EMERGENCY,          /**< Botão B pressionado — emergência */

    /* Sensores */
    EVT_SENSOR_READING,         /**< Nova leitura ambiental (temp, umid, lux) */
    EVT_SENSOR_MOTION,          /**< Movimento detectado pelo MPU6050 */
    EVT_SENSOR_NOISE_ALERT,     /**< Nível de ruído acima do limiar */

    /* Áudio */
    EVT_AUDIO_CAPTURE_START,    /**< Iniciar gravação */
    EVT_AUDIO_CAPTURE_STOP,     /**< Parar gravação e processar */
    EVT_AUDIO_READY,            /**< Áudio pronto para transmissão */

    /* Câmera */
    EVT_CAMERA_CAPTURE_REQ,     /**< Backend solicitou captura */
    EVT_CAMERA_STREAM_REQ,      /**< Backend solicitou stream */
    EVT_CAMERA_STREAM_STOP,     /**< Backend encerrou stream */

    /* Rede */
    EVT_WIFI_CONNECTED,
    EVT_WIFI_DISCONNECTED,
    EVT_MQTT_CONNECTED,
    EVT_MQTT_DISCONNECTED,
    EVT_MQTT_CMD_RECEIVED,      /**< Comando recebido do backend via MQTT */

    /* Sistema */
    EVT_SYSTEM_HEARTBEAT,       /**< Timer de heartbeat disparou */
    EVT_ACK_RECEIVED,           /**< Confirmação de atendimento recebida */
    EVT_OFFLINE_SYNC_NEEDED,    /**< Cache offline pronto para sync */

} event_type_t;

/* =========================================================
 * PRIORIDADES DE EVENTO
 * ========================================================= */
typedef enum {
    EVT_PRIO_CRITICAL   = 1,    /**< Emergência */
    EVT_PRIO_HIGH       = 2,    /**< Alta */
    EVT_PRIO_MEDIUM     = 3,    /**< Média */
    EVT_PRIO_LOW        = 4,    /**< Baixa */
    EVT_PRIO_INFO       = 5,    /**< Informativo */
} event_priority_t;

/* =========================================================
 * ESTRUTURA DE EVENTO
 * ========================================================= */
typedef struct {
    event_type_t        type;
    event_priority_t    priority;
    uint32_t            timestamp_ms;   /**< to_ms_since_boot */
    uint8_t             data[128];      /**< Payload inline pequeno */
    uint16_t            data_len;
    void               *extra_ptr;     /**< Ponteiro para dados maiores (ex: buffer áudio) */
} system_event_t;

/* =========================================================
 * CALLBACK DE HANDLER
 * ========================================================= */
typedef void (*event_handler_fn_t)(const system_event_t *event);

/* =========================================================
 * API PÚBLICA
 * ========================================================= */

/** @brief Inicializa o dispatcher e cria a fila interna. */
esp_err_t event_dispatcher_init(void);

/** @brief Task FreeRTOS do dispatcher — não chamar diretamente. */
void task_event_dispatcher(void *arg);

/**
 * @brief Publica um evento na fila (thread-safe, pode ser chamado de ISR).
 * @param event Evento a ser publicado.
 * @param from_isr true se chamado de ISR; usa xQueueSendFromISR internamente.
 * @return true se enfileirado com sucesso.
 */
bool event_post(const system_event_t *event, bool from_isr);

/**
 * @brief Registra um handler para um tipo específico de evento.
 * Múltiplos handlers podem ser registrados para o mesmo tipo.
 */
esp_err_t event_register_handler(event_type_t type, event_handler_fn_t handler);

/** @brief Retorna o handle da fila para uso direto (ex: xQueueSendFromISR). */
QueueHandle_t event_get_queue(void);

#endif /* VITALKARE_EVENT_DISPATCHER_H */
