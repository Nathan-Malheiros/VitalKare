/**
 * @file log_writer.c
 * @brief Logs estruturados em JSON Lines no cartão SD
 *
 * Escreve em /sdcard/logs/boot_NNN.jsonl (incrementa por boot).
 * Sem SD disponível, apenas loga via ESP_LOG (serial).
 */

#include "log_writer.h"
#include "sd_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "../../config/app_config.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "LOG_WRITER";

#define LOG_BUFFER_LINES  16
#define LOG_LINE_MAX      320

static char       s_buf[LOG_BUFFER_LINES][LOG_LINE_MAX];
static int        s_buf_head  = 0;
static int        s_buf_count = 0;
static FILE      *s_file      = NULL;
static char       s_filepath[64];
static SemaphoreHandle_t s_mutex = NULL;

static const char *level_names[] = {
    [LOG_LEVEL_DEBUG]    = "DEBUG",
    [LOG_LEVEL_INFO]     = "INFO",
    [LOG_LEVEL_WARN]     = "WARN",
    [LOG_LEVEL_ERROR]    = "ERROR",
    [LOG_LEVEL_CRITICAL] = "CRITICAL",
};
static const char *cat_names[] = {
    [LOG_CAT_SYSTEM]  = "system",
    [LOG_CAT_NETWORK] = "network",
    [LOG_CAT_MQTT]    = "mqtt",
    [LOG_CAT_SENSOR]  = "sensor",
    [LOG_CAT_AUDIO]   = "audio",
    [LOG_CAT_CAMERA]  = "camera",
    [LOG_CAT_BUTTON]  = "button",
    [LOG_CAT_STORAGE] = "storage",
};

esp_err_t log_writer_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) return ESP_ERR_NO_MEM;

    if (!sd_manager_is_mounted()) {
        ESP_LOGW(TAG, "SD não disponível — logs apenas via serial");
        return ESP_OK;
    }

    /* Encontra próximo índice de arquivo (boot_000.jsonl ... boot_999.jsonl) */
    int idx = 0;
    struct stat st;
    char path[64];
    do {
        snprintf(path, sizeof(path), "%s/boot_%03d.jsonl", APP_SD_LOG_DIR, idx++);
    } while (stat(path, &st) == 0 && idx < 1000);

    snprintf(s_filepath, sizeof(s_filepath), "%s", path);
    s_file = fopen(s_filepath, "w");
    if (s_file == NULL) {
        ESP_LOGW(TAG, "Não foi possível criar %s", s_filepath);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Log gravando em %s", s_filepath);
    return ESP_OK;
}

esp_err_t log_write(log_level_t level, log_category_t category,
                     const char *message, const char *payload)
{
    if (message == NULL) return ESP_ERR_INVALID_ARG;

    const char *lvl = (level < (int)(sizeof(level_names)/sizeof(level_names[0])))
                      ? level_names[level] : "UNKNOWN";
    const char *cat = (category < (int)(sizeof(cat_names)/sizeof(cat_names[0])))
                      ? cat_names[category] : "unknown";

    int64_t ts_us = esp_timer_get_time();

    char line[LOG_LINE_MAX];
    if (payload && payload[0]) {
        snprintf(line, sizeof(line),
                 "{\"ts\":%lld,\"level\":\"%s\",\"cat\":\"%s\",\"msg\":\"%s\",\"data\":%s}\n",
                 ts_us, lvl, cat, message, payload);
    } else {
        snprintf(line, sizeof(line),
                 "{\"ts\":%lld,\"level\":\"%s\",\"cat\":\"%s\",\"msg\":\"%s\"}\n",
                 ts_us, lvl, cat, message);
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        /* Buffer circular em RAM */
        int slot = (s_buf_head + s_buf_count) % LOG_BUFFER_LINES;
        if (s_buf_count < LOG_BUFFER_LINES) {
            s_buf_count++;
        } else {
            s_buf_head = (s_buf_head + 1) % LOG_BUFFER_LINES;
        }
        strncpy(s_buf[slot], line, LOG_LINE_MAX - 1);
        xSemaphoreGive(s_mutex);
    }

    return ESP_OK;
}

esp_err_t log_flush(void)
{
    if (!sd_manager_is_mounted() || s_file == NULL) return ESP_ERR_INVALID_STATE;

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        while (s_buf_count > 0) {
            fputs(s_buf[s_buf_head], s_file);
            s_buf_head  = (s_buf_head + 1) % LOG_BUFFER_LINES;
            s_buf_count--;
        }
        fflush(s_file);
        xSemaphoreGive(s_mutex);
    }

    return ESP_OK;
}

void task_storage_flush(void *arg)
{
    ESP_LOGI(TAG, "Task storage_flush iniciada");
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(APP_STORAGE_FLUSH_INTERVAL_MS));
        log_flush();
    }
}
