/**
 * @file command_processor.c
 * @brief Processador de comandos do backend — implementação
 */

#include "command_processor.h"
#include "tcp_client.h"
#include "vk_protocol.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "../../config/app_config.h"
#include "../events/event_dispatcher.h"
#include <string.h>

static const char *TAG = "CMD_PROC";

#define CMD_QUEUE_DEPTH     APP_TCP_INCOMING_CMD_DEPTH

static QueueHandle_t s_cmd_queue = NULL;

/* =========================================================
 * INIT
 * ========================================================= */
esp_err_t cmd_processor_init(void)
{
    s_cmd_queue = xQueueCreate(CMD_QUEUE_DEPTH, sizeof(tcp_queued_frame_t));
    if (!s_cmd_queue) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "command_processor inicializado");
    return ESP_OK;
}

QueueHandle_t cmd_processor_get_queue(void)
{
    return s_cmd_queue;
}

/* =========================================================
 * HANDLERS
 * ========================================================= */
static void handle_display_text(const uint8_t *payload, uint32_t len)
{
    if (!payload || len == 0) return;
    char text[64] = {0};
    size_t copy = len < sizeof(text) - 1 ? len : sizeof(text) - 1;
    memcpy(text, payload, copy);
    ESP_LOGI(TAG, "CMD DISPLAY: %s", text);

    system_event_t evt = {
        .type         = EVT_CMD_DISPLAY_UPDATE,
        .priority     = EVT_PRIO_MEDIUM,
        .timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS,
        .data_len     = (uint16_t)copy,
    };
    memcpy(evt.data, text, copy);
    event_post_normal(&evt);
}

static void handle_buzzer(const uint8_t *payload, uint32_t len)
{
    if (!payload || len < 2) return;
    uint8_t freq_code = payload[0];
    uint8_t duration_ds = payload[1]; /* décimos de segundo */
    ESP_LOGI(TAG, "CMD BUZZER: freq_code=%d dur=%d00ms", freq_code, duration_ds);

    system_event_t evt = {
        .type         = EVT_CMD_BUZZER,
        .priority     = EVT_PRIO_MEDIUM,
        .timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS,
        .data_len     = 2,
    };
    evt.data[0] = freq_code;
    evt.data[1] = duration_ds;
    event_post_normal(&evt);
}

static void handle_event_ack(const uint8_t *payload, uint32_t len)
{
    ESP_LOGI(TAG, "ACK recebido do operador");
    system_event_t evt = {
        .type         = EVT_ACK_RECEIVED,
        .priority     = EVT_PRIO_HIGH,
        .timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS,
    };
    event_post_critical(&evt, false);
}

static void handle_reboot(void)
{
    ESP_LOGW(TAG, "Comando de reboot recebido — reiniciando em 1 s");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

static void handle_ota_start(const uint8_t *payload, uint32_t len)
{
    /* Placeholder — OTA via esp_ota_* requer implementação dedicada */
    ESP_LOGW(TAG, "OTA_START recebido — funcionalidade não implementada nesta versão");
    system_event_t evt = {
        .type         = EVT_CMD_OTA_START,
        .priority     = EVT_PRIO_MEDIUM,
        .timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS,
    };
    event_post_normal(&evt);
}

/* =========================================================
 * TASK
 * ========================================================= */
void task_command_processor(void *arg)
{
    static tcp_queued_frame_t item;
    ESP_LOGI(TAG, "task_command_processor iniciada");

    for (;;) {
        if (xQueueReceive(s_cmd_queue, &item, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        /* Valida e extrai header + payload */
        const vk_header_t *hdr   = NULL;
        const uint8_t     *pload = NULL;
        size_t             flen  = 0;

        if (!vk_frame_parse(item.frame, item.frame_len, &hdr, &pload, &flen)) {
            ESP_LOGW(TAG, "Frame inválido recebido no command_processor");
            continue;
        }

        pkt_type_t type = (pkt_type_t)hdr->pkt_type;
        uint32_t   plen = hdr->payload_len;

        ESP_LOGD(TAG, "Processando comando tipo=0x%02X payload=%lu bytes",
                 (unsigned)type, (unsigned long)plen);

        switch (type) {
        case PKT_CMD_DISPLAY_TEXT:
            handle_display_text(pload, plen);
            break;
        case PKT_CMD_DISPLAY_CLEAR: {
            system_event_t evt = {
                .type         = EVT_CMD_DISPLAY_UPDATE,
                .priority     = EVT_PRIO_LOW,
                .timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS,
                .data_len     = 0,
            };
            event_post_normal(&evt);
            break;
        }
        case PKT_CMD_BUZZER:
            handle_buzzer(pload, plen);
            break;
        case PKT_EVENT_ACK:
            handle_event_ack(pload, plen);
            break;
        case PKT_CMD_REBOOT:
            handle_reboot();
            break;
        case PKT_OTA_START:
            handle_ota_start(pload, plen);
            break;
        default:
            ESP_LOGW(TAG, "Tipo de comando desconhecido: 0x%02X", (unsigned)type);
            break;
        }
    }
}
