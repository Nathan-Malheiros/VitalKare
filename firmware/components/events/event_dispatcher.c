/**
 * @file event_dispatcher.c
 * @brief Implementação da fila central de eventos
 */

#include "event_dispatcher.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "../../config/app_config.h"
#include <string.h>

static const char *TAG = "EVENTS";

#define EVENT_QUEUE_SIZE        32
#define MAX_HANDLERS_PER_TYPE   4
#define EVENT_TYPE_COUNT        32  /* >= maior valor de event_type_t */

static QueueHandle_t s_event_queue = NULL;

/* Tabela de handlers: [event_type][handler_index] */
static event_handler_fn_t s_handlers[EVENT_TYPE_COUNT][MAX_HANDLERS_PER_TYPE];

/* =========================================================
 * INICIALIZAÇÃO
 * ========================================================= */
esp_err_t event_dispatcher_init(void)
{
    s_event_queue = xQueueCreate(EVENT_QUEUE_SIZE, sizeof(system_event_t));
    if (s_event_queue == NULL) {
        ESP_LOGE(TAG, "Falha ao criar fila de eventos");
        return ESP_ERR_NO_MEM;
    }

    memset(s_handlers, 0, sizeof(s_handlers));
    ESP_LOGI(TAG, "Event dispatcher inicializado (fila=%d slots)", EVENT_QUEUE_SIZE);
    return ESP_OK;
}

/* =========================================================
 * REGISTRO DE HANDLERS
 * ========================================================= */
esp_err_t event_register_handler(event_type_t type, event_handler_fn_t handler)
{
    if (type >= EVENT_TYPE_COUNT || handler == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    for (int i = 0; i < MAX_HANDLERS_PER_TYPE; i++) {
        if (s_handlers[type][i] == NULL) {
            s_handlers[type][i] = handler;
            ESP_LOGD(TAG, "Handler registrado para evento %d (slot %d)", type, i);
            return ESP_OK;
        }
    }

    ESP_LOGE(TAG, "Sem slots disponíveis para evento tipo %d", type);
    return ESP_ERR_NO_MEM;
}

/* =========================================================
 * PUBLICAÇÃO DE EVENTOS
 * ========================================================= */
bool event_post(const system_event_t *event, bool from_isr)
{
    if (s_event_queue == NULL || event == NULL) {
        return false;
    }

    BaseType_t result;
    BaseType_t higher_prio_woken = pdFALSE;

    if (from_isr) {
        result = xQueueSendFromISR(s_event_queue, event, &higher_prio_woken);
        if (higher_prio_woken) {
            portYIELD_FROM_ISR();
        }
    } else {
        result = xQueueSend(s_event_queue, event, pdMS_TO_TICKS(10));
    }

    if (result != pdTRUE) {
        ESP_LOGW(TAG, "Fila de eventos cheia! Evento tipo %d descartado", event->type);
        return false;
    }

    return true;
}

QueueHandle_t event_get_queue(void)
{
    return s_event_queue;
}

/* =========================================================
 * TASK DO DISPATCHER
 * ========================================================= */
void task_event_dispatcher(void *arg)
{
    system_event_t event;

    ESP_LOGI(TAG, "Task event_dispatcher iniciada");

    for (;;) {
        if (xQueueReceive(s_event_queue, &event, portMAX_DELAY) == pdTRUE) {

            ESP_LOGD(TAG, "Evento recebido: type=%d prio=%d ts=%lu",
                     event.type, event.priority, event.timestamp_ms);

            if (event.type >= EVENT_TYPE_COUNT) {
                ESP_LOGW(TAG, "Tipo de evento inválido: %d", event.type);
                continue;
            }

            /* Despacha para todos os handlers registrados */
            for (int i = 0; i < MAX_HANDLERS_PER_TYPE; i++) {
                if (s_handlers[event.type][i] != NULL) {
                    s_handlers[event.type][i](&event);
                }
            }
        }
    }
}
