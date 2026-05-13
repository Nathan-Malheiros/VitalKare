/**
 * @file watchdog.c
 * @brief Watchdog de software + hardware para tasks críticas (ESP-IDF 5.x)
 *
 * Cada task crítica chama watchdog_feed_task() periodicamente.
 * Se alguma task falhar por mais de APP_WDT_TIMEOUT_MS, o sistema reinicia.
 */

#include "watchdog.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "../../config/app_config.h"

static const char *TAG = "WDT";

/* Timestamp da última alimentação de cada task (ms) */
static volatile uint32_t s_last_feed_ms[WDT_TASK_COUNT] = {0};
static bool s_initialized = false;

esp_err_t watchdog_init(void)
{
    /* Configura o WDT de hardware */
    esp_task_wdt_config_t cfg = {
        .timeout_ms      = APP_WDT_TIMEOUT_MS,
        .idle_core_mask  = 0,
        .trigger_panic   = true,
    };
    esp_err_t ret = esp_task_wdt_reconfigure(&cfg);
    if (ret == ESP_ERR_NOT_FOUND) {
        /* WDT não estava iniciado — inicia agora */
        ret = esp_task_wdt_init(&cfg);
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "esp_task_wdt_init: %s (continuando sem HW WDT)",
                 esp_err_to_name(ret));
    }

    /* Registra a própria task watchdog */
    esp_task_wdt_add(NULL);

    /* Inicializa timestamps com o momento atual */
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    for (int i = 0; i < WDT_TASK_COUNT; i++) {
        s_last_feed_ms[i] = now;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Watchdog inicializado (timeout=%d ms)", APP_WDT_TIMEOUT_MS);
    return ESP_OK;
}

void watchdog_feed_task(wdt_task_id_t task_id)
{
    if (task_id >= WDT_TASK_COUNT) return;
    s_last_feed_ms[task_id] = xTaskGetTickCount() * portTICK_PERIOD_MS;
}

void task_watchdog(void *arg)
{
    ESP_LOGI(TAG, "Task watchdog iniciada (prio máxima)");

    watchdog_init();

    for (;;) {
        uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

        /* Verifica todas as tasks críticas */
        bool all_alive = true;
        for (int i = 0; i < WDT_TASK_COUNT; i++) {
            uint32_t delta = now_ms - s_last_feed_ms[i];
            if (delta > APP_WDT_TIMEOUT_MS) {
                ESP_LOGE(TAG, "Task %d não alimentou o WDT há %lu ms — reiniciando",
                         i, (unsigned long)delta);
                all_alive = false;
                break;
            }
        }

        if (!all_alive) {
            vTaskDelay(pdMS_TO_TICKS(100));
            esp_restart();
        }

        /* Alimenta o WDT de hardware */
        esp_task_wdt_reset();

        vTaskDelay(pdMS_TO_TICKS(APP_TASK_HEARTBEAT_PERIOD_MS));
    }
}
