/**
 * @file offline_cache.c
 * @brief Cache circular de eventos para modo offline — SD-backed
 */

#include "offline_cache.h"
#include "sd_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "CACHE";

#define CACHE_FILE  "/sdcard/cache/offline.bin"

static offline_event_t  s_cache[OFFLINE_CACHE_MAX_EVENTS];
static uint32_t         s_count     = 0;
static uint32_t         s_head      = 0;   /* índice de leitura */
static SemaphoreHandle_t s_mutex    = NULL;
static bool             s_initialized = false;

esp_err_t offline_cache_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    memset(s_cache, 0, sizeof(s_cache));
    s_count = 0;
    s_head  = 0;

    /* Tenta recuperar cache do SD */
    if (sd_manager_is_mounted()) {
        FILE *f = fopen(CACHE_FILE, "rb");
        if (f) {
            uint32_t saved = 0;
            if (fread(&saved, sizeof(saved), 1, f) == 1 &&
                saved <= OFFLINE_CACHE_MAX_EVENTS) {
                s_count = (uint32_t)fread(s_cache, sizeof(offline_event_t), saved, f);
            }
            fclose(f);
            ESP_LOGI(TAG, "Cache recuperada: %lu eventos", (unsigned long)s_count);
        }
    }

    s_initialized = true;
    return ESP_OK;
}

esp_err_t offline_cache_push(const offline_event_t *event)
{
    if (!s_initialized || event == NULL) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return ESP_ERR_TIMEOUT;

    uint32_t write_idx = (s_head + s_count) % OFFLINE_CACHE_MAX_EVENTS;
    memcpy(&s_cache[write_idx], event, sizeof(offline_event_t));
    s_cache[write_idx].synced = false;

    if (s_count < OFFLINE_CACHE_MAX_EVENTS) {
        s_count++;
    } else {
        /* Buffer cheio — sobrescreve o mais antigo */
        s_head = (s_head + 1) % OFFLINE_CACHE_MAX_EVENTS;
        ESP_LOGW(TAG, "Cache cheia — descartando evento mais antigo");
    }

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

uint32_t offline_cache_pending_count(void)
{
    if (!s_initialized) return 0;
    uint32_t count = 0;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        count = s_count;
        xSemaphoreGive(s_mutex);
    }
    return count;
}

esp_err_t offline_cache_sync(offline_sync_cb_t cb)
{
    if (!s_initialized || cb == NULL) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return ESP_ERR_TIMEOUT;

    for (uint32_t i = 0; i < s_count; i++) {
        uint32_t idx = (s_head + i) % OFFLINE_CACHE_MAX_EVENTS;
        if (!s_cache[idx].synced) {
            if (cb(&s_cache[idx])) {
                s_cache[idx].synced = true;
            }
        }
    }

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t offline_cache_compact(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return ESP_ERR_TIMEOUT;

    /* Remove eventos já sincronizados */
    uint32_t new_count = 0;
    offline_event_t tmp[OFFLINE_CACHE_MAX_EVENTS];

    for (uint32_t i = 0; i < s_count; i++) {
        uint32_t idx = (s_head + i) % OFFLINE_CACHE_MAX_EVENTS;
        if (!s_cache[idx].synced) {
            tmp[new_count++] = s_cache[idx];
        }
    }
    memcpy(s_cache, tmp, sizeof(offline_event_t) * new_count);
    s_count = new_count;
    s_head  = 0;

    /* Persiste no SD */
    if (sd_manager_is_mounted()) {
        FILE *f = fopen(CACHE_FILE, "wb");
        if (f) {
            fwrite(&s_count, sizeof(s_count), 1, f);
            fwrite(s_cache, sizeof(offline_event_t), s_count, f);
            fclose(f);
        }
    }

    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "Cache compactada: %lu eventos restantes", (unsigned long)s_count);
    return ESP_OK;
}
