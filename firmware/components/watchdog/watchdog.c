/**
 * @file watchdog.c
 * @brief Hardware WDT keeper + delegação de stall detection ao task_monitor
 *
 * ARQUITETURA CORRIGIDA:
 *   - A detecção de stall lógico de tasks é feita pelo task_monitor +
 *     system_manager (via task_monitor_heartbeat / task_monitor_get_faulted).
 *   - Este módulo cuida APENAS do hardware WDT (TWDT do ESP-IDF):
 *       1. Configura timeout e trigger_panic.
 *       2. Registra a task_watchdog no TWDT.
 *       3. Chama esp_task_wdt_reset() periodicamente para manter o HW WDT vivo.
 *
 * RAZÃO DA MUDANÇA:
 *   O array s_last_feed_ms[] era inicializado no boot e nunca atualizado pelas
 *   tasks (elas chamam task_monitor_heartbeat(), não watchdog_feed_task()), o
 *   que causava um esp_restart() garantido a cada APP_WDT_TIMEOUT_MS (30 s).
 *   O task_monitor já implementa stall detection com thresholds corretos;
 *   duplicar a lógica aqui só introduzia erros.
 *
 * watchdog_feed_task() é mantida como no-op para compatibilidade de API com
 * eventuais chamadas legadas sem recompilar todo o projeto.
 */

#include "watchdog.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "../../config/app_config.h"

static const char *TAG = "WDT";

esp_err_t watchdog_init(void)
{
    /* idle_core_mask=(1<<0)|(1<<1): keep both IDLE tasks subscribed to the TWDT.
     * Setting mask=0 removes idle tasks from TWDT subscription while ESP-IDF
     * internally still calls esp_task_wdt_reset() for them, producing the
     * "task not found" warning on every idle tick. */
    esp_task_wdt_config_t cfg = {
        .timeout_ms      = APP_WDT_TIMEOUT_MS,
        .idle_core_mask  = (1 << 0) | (1 << 1),
        .trigger_panic   = true,
    };
    esp_err_t ret = esp_task_wdt_reconfigure(&cfg);
    if (ret == ESP_ERR_NOT_FOUND) {
        ret = esp_task_wdt_init(&cfg);
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "esp_task_wdt_init: %s (continuando sem HW WDT)",
                 esp_err_to_name(ret));
    }

    /* Registra apenas a própria task watchdog no TWDT */
    esp_task_wdt_add(NULL);

    ESP_LOGI(TAG, "HW WDT configurado (timeout=%d ms, panic=true)", APP_WDT_TIMEOUT_MS);
    return ESP_OK;
}

/* No-op mantido para compatibilidade de API. Detecção de stall delegada ao task_monitor. */
void watchdog_feed_task(wdt_task_id_t task_id)
{
    (void)task_id;
}

void task_watchdog(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "task_watchdog iniciada (prio=%d)", TASK_PRIO_WATCHDOG);

    watchdog_init();

    for (;;) {
        /* Mantém o hardware WDT vivo. Stall detection é responsabilidade do
         * task_monitor + system_manager (task_monitor_get_faulted). */
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(APP_TASK_HEARTBEAT_PERIOD_MS));
    }
}
