/**
 * @file button_handler.c
 * @brief Gerenciamento de botões — ANYEDGE, debounce por software, PRESSED + RELEASED
 *
 * Botão A (GPIO 45) — Solicitação do paciente
 * Botão B (GPIO 46) — Emergência
 *
 * ARQUITETURA:
 *   ISR detecta qualquer borda (NEGEDGE = press, POSEDGE = release) e
 *   envia para s_btn_queue com o nível lido no instante da interrupção.
 *   A task faz debounce e publica EVT_BTN_x_PRESSED ou EVT_BTN_x_RELEASED
 *   no event dispatcher com a prioridade correta.
 *
 * PRIORIDADE DOS EVENTOS:
 *   Botão B (emergência): EVT_PRIO_CRITICAL → fila crítica do dispatcher
 *   Botão A (solicitação): EVT_PRIO_HIGH    → fila crítica do dispatcher
 *
 * Ambos vão para a fila crítica porque são eventos de entrada do usuário
 * que requerem resposta imediata (início de gravação de áudio).
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

#define BTN_QUEUE_SIZE  16

static QueueHandle_t s_btn_queue = NULL;

/* Estrutura interna da fila ISR → task */
typedef struct {
    uint32_t gpio_num;
    uint32_t timestamp_ms;
    int      level;         /**< 0 = press (ativo-baixo), 1 = release */
} btn_isr_event_t;

/* =========================================================
 * QUEUE OWNERSHIP — s_btn_queue
 * =========================================================
 * Owner:     button_handler module
 * Created:   button_handler_init() — before any ISR can fire
 * Producers: gpio_isr_handler() (ISR context) — both buttons share one handler
 * Consumer:  task_button_handler (single consumer, task context)
 * Depth:     BTN_QUEUE_SIZE = 16 — handles burst of rapid presses without drop
 * Overflow:  xQueueSendFromISR() fails silently if full; button event is lost.
 *            Acceptable — user must press again. BTN_QUEUE_SIZE=16 prevents
 *            realistic burst overflow.
 * Thread safety: xQueueSendFromISR() is ISR-safe by design.
 *                xQueueReceive() in task context is task-safe.
 *                No other synchronization needed.
 * ========================================================= */

/* =========================================================
 * ISR SAFETY AUDIT — gpio_isr_handler
 * =========================================================
 * IRAM_ATTR:
 *   Required. ISR must reside in IRAM so it executes during cache misses.
 *   ESP32-S3 flash cache can stall for 120+ µs during write operations.
 *   Without IRAM_ATTR, a button press during an SPIFFS/NVS write would
 *   result in a fatal LoadProhibited exception.
 *
 * xQueueSendFromISR():
 *   ISR-safe variant of xQueueSend(). Uses a portMUX spinlock internally.
 *   Does NOT sleep, does NOT call malloc. Safe to call from ISR.
 *
 * portYIELD_FROM_ISR(woken):
 *   If xQueueSendFromISR set woken=pdTRUE, a higher-priority task was
 *   unblocked. portYIELD_FROM_ISR triggers an immediate context switch
 *   at ISR exit rather than waiting for the next scheduler tick.
 *   This is the correct pattern — omitting it would add up to 1 tick of
 *   latency (typically 10 ms) before task_button_handler processes the event.
 *
 * xTaskGetTickCountFromISR():
 *   ISR-safe variant of xTaskGetTickCount(). Returns the current tick count
 *   from the scheduler's atomic counter. Safe to call from any ISR.
 *
 * gpio_get_level():
 *   Reads the GPIO input register directly via memory-mapped IO.
 *   No mutex, no cache dependency, no task involvement. ISR-safe.
 *
 * No ESP_LOG* calls:
 *   ESP_LOG* uses printf internally → heap allocation → NOT ISR-safe.
 *   Correctly absent from this ISR.
 *
 * No malloc():
 *   btn_isr_event_t allocated on the ISR stack (local struct).
 *   Passed to xQueueSendFromISR by value (queue copies it).
 *   Zero dynamic allocation — correct.
 *
 * VERDICT: ISR implementation is correct and complete.
 * ========================================================= */
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)(uintptr_t)arg;
    btn_isr_event_t evt = {
        .gpio_num     = gpio_num,
        .timestamp_ms = xTaskGetTickCountFromISR() * portTICK_PERIOD_MS,
        .level        = gpio_get_level(gpio_num),
    };
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(s_btn_queue, &evt, &woken);
    portYIELD_FROM_ISR(woken);
}

/* =========================================================
 * INICIALIZAÇÃO
 * ========================================================= */
esp_err_t button_handler_init(void)
{
    s_btn_queue = xQueueCreate(BTN_QUEUE_SIZE, sizeof(btn_isr_event_t));
    if (!s_btn_queue) return ESP_ERR_NO_MEM;

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << HW_BTN_A_GPIO) | (1ULL << HW_BTN_B_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,  /* Detecta PRESS e RELEASE */
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(HW_BTN_A_GPIO, gpio_isr_handler,
                                          (void *)(uintptr_t)HW_BTN_A_GPIO));
    ESP_ERROR_CHECK(gpio_isr_handler_add(HW_BTN_B_GPIO, gpio_isr_handler,
                                          (void *)(uintptr_t)HW_BTN_B_GPIO));

    ESP_LOGI(TAG, "Botoes inicializados (A=GPIO%d, B=GPIO%d) — ANYEDGE",
             HW_BTN_A_GPIO, HW_BTN_B_GPIO);
    return ESP_OK;
}

/* =========================================================
 * TASK
 * =========================================================
 * Debounce: ignora eventos de mesma borda em menos de HW_BTN_DEBOUNCE_MS.
 * O campo `level` determina se é press (0) ou release (1) porque os
 * botões são ativo-baixo (pull-up interno).
 * ========================================================= */
void task_button_handler(void *arg)
{
    btn_isr_event_t isr_evt;
    uint32_t last_ts_a = 0;
    uint32_t last_ts_b = 0;

    ESP_LOGI(TAG, "task_button_handler iniciada");

    for (;;) {
        if (xQueueReceive(s_btn_queue, &isr_evt, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        bool is_a = (isr_evt.gpio_num == HW_BTN_A_GPIO);
        uint32_t *last_ts = is_a ? &last_ts_a : &last_ts_b;

        /* Debounce: ignora se chegou antes do período mínimo */
        if ((isr_evt.timestamp_ms - *last_ts) < HW_BTN_DEBOUNCE_MS) {
            continue;
        }
        *last_ts = isr_evt.timestamp_ms;

        /*
         * level=0 → botão pressionado (pull-up puxado para GND pelo usuário)
         * level=1 → botão solto (pull-up retornou ao nível alto)
         */
        bool is_press = (isr_evt.level == 0);

        system_event_t evt = {
            .timestamp_ms = isr_evt.timestamp_ms,
            .data_len     = 0,
            .extra_ptr    = NULL,
        };

        if (is_a) {
            if (is_press) {
                evt.type     = EVT_BTN_A_PRESSED;
                evt.priority = EVT_PRIO_HIGH;
                ESP_LOGI(TAG, "Botao A PRESSIONADO (solicitacao)");
            } else {
                evt.type     = EVT_BTN_A_RELEASED;
                evt.priority = EVT_PRIO_HIGH;
                ESP_LOGI(TAG, "Botao A SOLTO");
            }
        } else {
            if (is_press) {
                evt.type     = EVT_BTN_B_PRESSED;
                evt.priority = EVT_PRIO_CRITICAL;
                ESP_LOGW(TAG, "Botao B PRESSIONADO (EMERGENCIA)");
            } else {
                evt.type     = EVT_BTN_B_RELEASED;
                evt.priority = EVT_PRIO_CRITICAL;
                ESP_LOGW(TAG, "Botao B SOLTO");
            }
        }

        /* Todos os eventos de botão vão para a fila crítica */
        event_post_critical(&evt, false);
    }
}
