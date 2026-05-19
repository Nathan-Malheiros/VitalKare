/**
 * @file event_dispatcher.c
 * @brief Dual-priority event dispatcher — VitalKare Firmware
 *
 * Dois níveis de fila:
 *   s_critical_queue — capacidade 8,  processada PRIMEIRO a cada ciclo
 *   s_normal_queue   — capacidade 32, processada apenas quando crítica vazia
 *
 * Garante latência determinística para eventos de emergência:
 * o pior caso de atraso de um evento crítico é apenas o tempo de
 * execução do handler do evento crítico corrente (< 1 ms típico).
 */

#include "event_dispatcher.h"
#include "event_trace.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "../../config/app_config.h"
#include <string.h>

static const char *TAG = "EVENTS";

/* =========================================================
 * FILAS INTERNAS
 * ========================================================= */
#define CRITICAL_QUEUE_SIZE     8
#define NORMAL_QUEUE_SIZE       32
#define MAX_HANDLERS_PER_TYPE   4

/* =========================================================
 * QUEUE OWNERSHIP — s_critical_queue / s_normal_queue
 * =========================================================
 * CRITICAL QUEUE (s_critical_queue):
 *   Owner:     event_dispatcher module
 *   Created:   event_dispatcher_init()
 *   Depth:     CRITICAL_QUEUE_SIZE = 8
 *   Producers: Any task via event_post_critical() or event_post(prio<=HIGH)
 *              Expected producers: button_handler (ISR→task), tcp_client (RX task)
 *   Consumer:  task_event_dispatcher (sole consumer)
 *   ISR-safe:  YES — event_post_critical(from_isr=true) uses xQueueSendFromISR
 *   Overflow:  event logged at ESP_LOGE level; event dropped; trace records drop.
 *
 * NORMAL QUEUE (s_normal_queue):
 *   Owner:     event_dispatcher module
 *   Created:   event_dispatcher_init()
 *   Depth:     NORMAL_QUEUE_SIZE = 32
 *   Producers: Any task via event_post_normal() or event_post(prio>HIGH)
 *              Expected: sensor_task, tcp_client, system_manager
 *   Consumer:  task_event_dispatcher (sole consumer)
 *   ISR-safe:  Partial — event_post(from_isr=true) for normal events logs a
 *              warning and uses xQueueSendFromISR (functional but discouraged).
 *   Overflow:  event logged at ESP_LOGW; event dropped; trace records drop.
 *
 * DISPATCH ORDER: critical queue fully drained before each normal queue item.
 * ========================================================= */
static QueueHandle_t s_critical_queue = NULL;
static QueueHandle_t s_normal_queue   = NULL;

/* Tabela de handlers: [event_type][handler_index] */
static event_handler_fn_t s_handlers[EVT_TYPE_COUNT][MAX_HANDLERS_PER_TYPE];

/* =========================================================
 * INICIALIZAÇÃO
 * ========================================================= */
esp_err_t event_dispatcher_init(void)
{
    s_critical_queue = xQueueCreate(CRITICAL_QUEUE_SIZE, sizeof(system_event_t));
    s_normal_queue   = xQueueCreate(NORMAL_QUEUE_SIZE,   sizeof(system_event_t));

    if (!s_critical_queue || !s_normal_queue) {
        ESP_LOGE(TAG, "Falha ao criar filas de eventos");
        return ESP_ERR_NO_MEM;
    }

    memset(s_handlers, 0, sizeof(s_handlers));

    ESP_LOGI(TAG, "Event dispatcher inicializado (critical=%d, normal=%d)",
             CRITICAL_QUEUE_SIZE, NORMAL_QUEUE_SIZE);
    return ESP_OK;
}

/* =========================================================
 * REGISTRO DE HANDLERS
 * ========================================================= */
esp_err_t event_register_handler(event_type_t type, event_handler_fn_t handler)
{
    if (type >= EVT_TYPE_COUNT || handler == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    for (int i = 0; i < MAX_HANDLERS_PER_TYPE; i++) {
        if (s_handlers[type][i] == NULL) {
            s_handlers[type][i] = handler;
            return ESP_OK;
        }
    }
    ESP_LOGE(TAG, "Sem slots para evento tipo %d", type);
    return ESP_ERR_NO_MEM;
}

/* =========================================================
 * PUBLICAÇÃO
 * ========================================================= */
bool event_post_critical(const system_event_t *event, bool from_isr)
{
    if (!s_critical_queue || !event) return false;

    BaseType_t result;
    BaseType_t woken = pdFALSE;

    if (from_isr) {
        result = xQueueSendFromISR(s_critical_queue, event, &woken);
        if (woken) portYIELD_FROM_ISR();
    } else {
        result = xQueueSend(s_critical_queue, event, pdMS_TO_TICKS(5));
    }

    if (result != pdTRUE) {
        ESP_LOGE(TAG, "FILA CRITICA CHEIA — evento tipo %d PERDIDO", event->type);
        event_trace_record_enqueue(event->type, EVT_TRACE_Q_CRITICAL, true);
        return false;
    }
    event_trace_record_enqueue(event->type, EVT_TRACE_Q_CRITICAL, false);
    return true;
}

bool event_post_normal(const system_event_t *event)
{
    if (!s_normal_queue || !event) return false;

    BaseType_t result = xQueueSend(s_normal_queue, event, pdMS_TO_TICKS(10));
    if (result != pdTRUE) {
        ESP_LOGW(TAG, "Fila normal cheia — evento tipo %d descartado", event->type);
        event_trace_record_enqueue(event->type, EVT_TRACE_Q_NORMAL, true);
        return false;
    }
    event_trace_record_enqueue(event->type, EVT_TRACE_Q_NORMAL, false);
    return true;
}

bool event_post(const system_event_t *event, bool from_isr)
{
    if (!event) return false;

    if (event->priority <= EVT_PRIO_HIGH) {
        return event_post_critical(event, from_isr);
    }
    if (from_isr) {
        ESP_EARLY_LOGW(TAG, "event_post de ISR prio normal tipo=%d", event->type);
        BaseType_t woken = pdFALSE;
        BaseType_t r = xQueueSendFromISR(s_normal_queue, event, &woken);
        if (woken) portYIELD_FROM_ISR();
        return r == pdTRUE;
    }
    return event_post_normal(event);
}

QueueHandle_t event_get_queue(void)
{
    return s_normal_queue;
}

/* =========================================================
 * DISPATCH INTERNO
 * ========================================================= */
static void dispatch_event(const system_event_t *evt)
{
    if (evt->type >= EVT_TYPE_COUNT) {
        ESP_LOGW(TAG, "Tipo invalido: %d", evt->type);
        return;
    }

    ESP_LOGD(TAG, "Despachando tipo=%d prio=%d ts=%lu",
             evt->type, evt->priority, (unsigned long)evt->timestamp_ms);

    event_trace_on_dispatch_start(evt->type);

    for (int i = 0; i < MAX_HANDLERS_PER_TYPE; i++) {
        if (s_handlers[evt->type][i]) {
            s_handlers[evt->type][i](evt);
        }
    }

    event_trace_on_dispatch_end(evt->type);
}

/* =========================================================
 * TASK PRINCIPAL — Dual-Priority Loop
 * =========================================================
 * Política:
 *   1. Drena TODA a fila crítica (sem limite — prioridade absoluta)
 *   2. Processa UM evento normal
 *   3. Repete
 *
 * Nenhum evento normal pode bloquear a fila crítica.
 * Eventos normais progridem um por ciclo (sem inanição).
 * ========================================================= */
void task_event_dispatcher(void *arg)
{
    system_event_t evt;
    ESP_LOGI(TAG, "task_event_dispatcher iniciada (dual-queue)");

    for (;;) {
        /* Fase 1: drena completamente a fila crítica */
        while (xQueueReceive(s_critical_queue, &evt, 0) == pdTRUE) {
            dispatch_event(&evt);
        }

        /* Fase 2: processa um evento normal (aguarda 10 ms para novo evento) */
        if (xQueueReceive(s_normal_queue, &evt, pdMS_TO_TICKS(10)) == pdTRUE) {
            dispatch_event(&evt);
        }
    }
}
