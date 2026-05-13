/**
 * @file button_handler.c
 * @brief Gerenciamento de botões com debounce e ISR segura para FreeRTOS
 *
 * Botão A (GPIO 45) → EVT_BTN_REQUEST  (solicitação do paciente)
 * Botão B (GPIO 46) → EVT_BTN_EMERGENCY (emergência)
 *
 * Debounce implementado por software: ISR apenas sinaliza uma queue;
 * a task faz a filtragem de bouncing.
 */

#include "button_handler.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "../../config/hardware_config.h"
#include "../../config/app_config.h"
#include "../events/event_dispatcher.h"

static const char *TAG = "BUTTONS";

/* Fila interna para despacho de ISR → task */
#define BTN_QUEUE_SIZE 8
static QueueHandle_t s_btn_queue = NULL;

typedef struct {
    uint32_t gpio_num;
    uint32_t timestamp_ms;
} btn_isr_event_t;

/* =========================================================
 * ISR — Executada em contexto de interrupção
 * ========================================================= */
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)(uintptr_t)arg;
    btn_isr_event_t evt = {
        .gpio_num     = gpio_num,
        .timestamp_ms = xTaskGetTickCountFromISR() * portTICK_PERIOD_MS,
    };
    BaseType_t higher_prio_woken = pdFALSE;
    xQueueSendFromISR(s_btn_queue, &evt, &higher_prio_woken);
    portYIELD_FROM_ISR(higher_prio_woken);
}

/* =========================================================
 * INICIALIZAÇÃO
 * ========================================================= */
esp_err_t button_handler_init(void)
{
    s_btn_queue = xQueueCreate(BTN_QUEUE_SIZE, sizeof(btn_isr_event_t));
    if (s_btn_queue == NULL) return ESP_ERR_NO_MEM;

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << HW_BTN_A_GPIO) | (1ULL << HW_BTN_B_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,  /* Borda de descida — ativo baixo */
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(HW_BTN_A_GPIO, gpio_isr_handler,
                                          (void *)(uintptr_t)HW_BTN_A_GPIO));
    ESP_ERROR_CHECK(gpio_isr_handler_add(HW_BTN_B_GPIO, gpio_isr_handler,
                                          (void *)(uintptr_t)HW_BTN_B_GPIO));

    ESP_LOGI(TAG, "Botões inicializados (A=GPIO%d, B=GPIO%d)",
             HW_BTN_A_GPIO, HW_BTN_B_GPIO);
    return ESP_OK;
}

/* =========================================================
 * TASK
 * ========================================================= */
void task_button_handler(void *arg)
{
    btn_isr_event_t isr_evt;
    uint32_t last_press_a_ms = 0;
    uint32_t last_press_b_ms = 0;

    ESP_LOGI(TAG, "Task button_handler iniciada");

    for (;;) {
        if (xQueueReceive(s_btn_queue, &isr_evt, portMAX_DELAY) == pdTRUE) {

            uint32_t *last_ms = (isr_evt.gpio_num == HW_BTN_A_GPIO)
                                ? &last_press_a_ms : &last_press_b_ms;

            /* Debounce: ignora eventos mais próximos que HW_BTN_DEBOUNCE_MS */
            if ((isr_evt.timestamp_ms - *last_ms) < HW_BTN_DEBOUNCE_MS) {
                continue;
            }
            *last_ms = isr_evt.timestamp_ms;

            system_event_t evt = {
                .timestamp_ms = isr_evt.timestamp_ms,
                .data_len     = 0,
                .extra_ptr    = NULL,
            };

            if (isr_evt.gpio_num == HW_BTN_A_GPIO) {
                evt.type     = EVT_BTN_REQUEST;
                evt.priority = EVT_PRIO_MEDIUM;
                ESP_LOGI(TAG, "Botão A pressionado (solicitação)");
            } else {
                evt.type     = EVT_BTN_EMERGENCY;
                evt.priority = EVT_PRIO_CRITICAL;
                ESP_LOGW(TAG, "Botão B pressionado (EMERGÊNCIA)");
            }

            event_post(&evt, false);
        }
    }
}
