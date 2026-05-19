/**
 * @file memory_monitor.c
 * @brief Monitor de memória — implementação
 *
 * NOTAS DE IMPLEMENTAÇÃO:
 *   heap_caps_get_free_size()     — O(1), thread-safe, não-ISR
 *   heap_caps_get_minimum_free_size() — O(1), watermark histórica
 *   uxQueueMessagesWaiting()      — O(1), thread-safe
 *
 *   O snapshot copia o resultado para s_snapshot.
 *   Leitores fazem taskENTER_CRITICAL + memcpy + taskEXIT_CRITICAL.
 *   Esse bloco é < 1µs no ESP32-S3.
 */

#include "memory_monitor.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdatomic.h>

static const char *TAG = "MEM_MON";

/* =========================================================
 * ESTADO INTERNO
 * ========================================================= */
static memory_snapshot_t s_snapshot;
static portMUX_TYPE      s_spinlock = portMUX_INITIALIZER_UNLOCKED;
static _Atomic uint32_t  s_alloc_failures = 0;

static monitored_queue_t s_queues[MEMORY_MONITOR_MAX_QUEUES];
static uint8_t           s_queue_count = 0;

/* =========================================================
 * IMPLEMENTAÇÃO
 * ========================================================= */
void memory_monitor_init(void)
{
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_queue_count = 0;
    atomic_store(&s_alloc_failures, 0);
    ESP_LOGI(TAG, "Inicializado");
}

bool memory_monitor_register_queue(const char *name, QueueHandle_t q,
                                   uint8_t max_depth)
{
    if (s_queue_count >= MEMORY_MONITOR_MAX_QUEUES) {
        ESP_LOGW(TAG, "Limite de filas atingido (%d)", MEMORY_MONITOR_MAX_QUEUES);
        return false;
    }
    s_queues[s_queue_count].name      = name;
    s_queues[s_queue_count].handle    = q;
    s_queues[s_queue_count].max_depth = max_depth;
    s_queues[s_queue_count].current_fill = 0;
    s_queue_count++;
    return true;
}

void memory_monitor_refresh(void)
{
    memory_snapshot_t snap;

    snap.heap_free_bytes     = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    snap.heap_min_ever_bytes = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    snap.psram_total_bytes   = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    snap.psram_free_bytes    = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    snap.alloc_failures      = atomic_load(&s_alloc_failures);
    snap.timestamp_ms        = (uint32_t)(esp_timer_get_time() / 1000ULL);

    snap.heap_low      = snap.heap_free_bytes < (MEMORY_MONITOR_HEAP_LOW_KB  * 1024U);
    snap.heap_critical = snap.heap_free_bytes < (MEMORY_MONITOR_HEAP_CRIT_KB * 1024U);
    snap.psram_low     = snap.psram_free_bytes < (MEMORY_MONITOR_PSRAM_LOW_KB * 1024U);

    /* Filas */
    snap.queue_count = s_queue_count;
    for (uint8_t i = 0; i < s_queue_count; i++) {
        snap.queues[i]  = s_queues[i];
        uint32_t filled = uxQueueMessagesWaiting(s_queues[i].handle);
        uint8_t  pct    = s_queues[i].max_depth > 0
                          ? (uint8_t)((filled * 100U) / s_queues[i].max_depth)
                          : 0;
        snap.queues[i].current_fill = pct;
    }

    taskENTER_CRITICAL(&s_spinlock);
    s_snapshot = snap;
    taskEXIT_CRITICAL(&s_spinlock);

    if (snap.heap_critical) {
        ESP_LOGE(TAG, "HEAP CRITICA: %lu B livres (min ever: %lu B)",
                 (unsigned long)snap.heap_free_bytes,
                 (unsigned long)snap.heap_min_ever_bytes);
    } else if (snap.heap_low) {
        ESP_LOGW(TAG, "Heap baixa: %lu B livres", (unsigned long)snap.heap_free_bytes);
    }
    if (snap.psram_low) {
        ESP_LOGW(TAG, "PSRAM baixa: %lu B livres / %lu total",
                 (unsigned long)snap.psram_free_bytes,
                 (unsigned long)snap.psram_total_bytes);
    }
}

void memory_monitor_get_snapshot(memory_snapshot_t *out)
{
    if (!out) return;
    taskENTER_CRITICAL(&s_spinlock);
    *out = s_snapshot;
    taskEXIT_CRITICAL(&s_spinlock);
}

void memory_monitor_alloc_failed(void)
{
    atomic_fetch_add(&s_alloc_failures, 1);
}

bool memory_monitor_is_heap_critical(void)
{
    taskENTER_CRITICAL(&s_spinlock);
    bool crit = s_snapshot.heap_critical;
    taskEXIT_CRITICAL(&s_spinlock);
    return crit;
}

bool memory_monitor_is_psram_low(void)
{
    taskENTER_CRITICAL(&s_spinlock);
    bool low = s_snapshot.psram_low;
    taskEXIT_CRITICAL(&s_spinlock);
    return low;
}
