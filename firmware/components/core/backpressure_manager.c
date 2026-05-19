/**
 * @file backpressure_manager.c
 * @brief Resource Overload Protection — implementation
 *
 * THRESHOLD RATIONALE:
 *   These thresholds are derived from observed ESP32-S3 crash points:
 *
 *   HEAP_WARN_BYTES (24 KB):
 *     tcp_client needs ~8 KB for TX/RX buffers; FreeRTOS needs ~4 KB for task
 *     creation overhead; lwIP TCP stack peaks at ~6 KB during connection setup.
 *     24 KB provides a safety margin before any allocation could fail.
 *
 *   HEAP_HIGH_BYTES (12 KB):
 *     At this point, lwIP cannot open new TCP connections. Recording a new
 *     session would require allocating the I2S descriptor ring (~2 KB) and
 *     at least one chunk from PSRAM. We block normal recordings here.
 *
 *   HEAP_CRITICAL_BYTES (6 KB):
 *     FreeRTOS context switch and queue operations themselves require ~2-4 KB.
 *     Below 6 KB, any allocation risks a fatal exception. Block journal writes.
 *
 *   PSRAM_LOW_BYTES (128 KB):
 *     Each audio chunk = 4 KB. 128 KB = 32 free chunks. Emergency session
 *     needs at most APP_AUDIO_CHUNK_POOL_COUNT = 16 chunks. Keep 2× reserve.
 *
 *   QUEUE_CONGESTION_PCT (75):
 *     A queue at 75% fill is a sign of producer/consumer imbalance.
 *     This does not block traffic — only raises the level to WARN for early
 *     warning before the queue becomes full and drops events.
 *
 *   CHUNK_POOL_HIGH_THRESHOLD (2):
 *     If only 2 chunks remain free, a new normal session would quickly exhaust
 *     the pool. Emergency sessions use pre-reserved chunks (if implemented).
 */

#include "backpressure_manager.h"
#include "memory_monitor.h"
#include "../../components/audio/audio_capture.h"
#include "esp_log.h"

static const char *TAG = "BP";

/* =========================================================
 * THRESHOLDS (bytes unless noted)
 * ========================================================= */
#define HEAP_WARN_BYTES         (24u * 1024u)
#define HEAP_HIGH_BYTES         (12u * 1024u)
#define HEAP_CRITICAL_BYTES     ( 6u * 1024u)
#define PSRAM_WARN_BYTES        (256u * 1024u)
#define PSRAM_LOW_BYTES         (128u * 1024u)
#define QUEUE_CONGESTION_PCT    75u
#define CHUNK_POOL_WARN         4u
#define CHUNK_POOL_HIGH         2u

/* =========================================================
 * LEVEL EVALUATION
 * ========================================================= */
backpressure_level_t backpressure_get_level(void)
{
    memory_snapshot_t snap;
    memory_monitor_get_snapshot(&snap);

    /* Snapshot timestamp == 0 → memory_monitor has not run yet */
    if (snap.timestamp_ms == 0) {
        return BP_LEVEL_NORMAL;
    }

    /* CRITICAL: heap barely alive */
    if (snap.heap_free_bytes < HEAP_CRITICAL_BYTES) {
        ESP_LOGW(TAG, "CRITICAL: heap=%lu B", (unsigned long)snap.heap_free_bytes);
        return BP_LEVEL_CRITICAL;
    }

    /* HIGH: recording unsafe */
    if (snap.heap_free_bytes < HEAP_HIGH_BYTES) {
        return BP_LEVEL_HIGH;
    }
    if (snap.psram_free_bytes < PSRAM_LOW_BYTES) {
        return BP_LEVEL_HIGH;
    }
    uint8_t pool_free = audio_chunk_pool_available();
    if (pool_free <= CHUNK_POOL_HIGH) {
        return BP_LEVEL_HIGH;
    }

    /* WARN: early indicators */
    if (snap.heap_free_bytes < HEAP_WARN_BYTES) {
        return BP_LEVEL_WARN;
    }
    if (snap.psram_free_bytes < PSRAM_WARN_BYTES) {
        return BP_LEVEL_WARN;
    }
    if (pool_free <= CHUNK_POOL_WARN) {
        return BP_LEVEL_WARN;
    }

    /* Check any registered queue for congestion */
    for (uint8_t i = 0; i < snap.queue_count; i++) {
        if (snap.queues[i].current_fill >= QUEUE_CONGESTION_PCT) {
            return BP_LEVEL_WARN;
        }
    }

    return BP_LEVEL_NORMAL;
}

/* =========================================================
 * POLICY DECISIONS
 * ========================================================= */
bool backpressure_allow_recording(audio_trigger_t trigger)
{
    /* EMERGENCY is never blocked — patient safety guarantee */
    if (trigger == AUDIO_TRIGGER_EMERGENCY) {
        return true;
    }

    backpressure_level_t level = backpressure_get_level();
    if (level >= BP_LEVEL_HIGH) {
        ESP_LOGW(TAG, "Recording blocked (level=%s, trigger=NORMAL)",
                 backpressure_level_name(level));
        return false;
    }
    return true;
}

bool backpressure_allow_telemetry(void)
{
    backpressure_level_t level = backpressure_get_level();
    return level < BP_LEVEL_HIGH;
}

bool backpressure_allow_journal_append(void)
{
    backpressure_level_t level = backpressure_get_level();
    return level < BP_LEVEL_CRITICAL;
}

/* =========================================================
 * LEVEL NAME
 * ========================================================= */
const char *backpressure_level_name(backpressure_level_t level)
{
    switch (level) {
        case BP_LEVEL_NORMAL:   return "NORMAL";
        case BP_LEVEL_WARN:     return "WARN";
        case BP_LEVEL_HIGH:     return "HIGH";
        case BP_LEVEL_CRITICAL: return "CRITICAL";
        default:                return "UNKNOWN";
    }
}
