/**
 * @file time_sync.c
 * @brief VitalKare Unified Clock Authority — implementation
 *
 * CONCURRENCY MODEL:
 *   - s_epoch_offset_us: _Atomic int64_t, written by time_sync_set_epoch(),
 *     read by time_sync_get_epoch_ms(). No mutex needed — atomic 64-bit on Xtensa LX7.
 *   - s_synced: _Atomic bool, written once on sync, read anywhere.
 *   - All other functions are pure derivations of esp_timer_get_time() — inherently safe.
 *
 * HOT PATH PERFORMANCE:
 *   time_sync_get_ms()  → single esp_timer_get_time() call + divide. ~40 ns.
 *   time_sync_get_us()  → single esp_timer_get_time() call. ~20 ns.
 *   Both are safe to call from audio DMA context at 128ms/chunk intervals.
 *
 * EPOCH SEMANTICS:
 *   epoch_offset_us = (NTP_epoch_ms * 1000) - esp_timer_get_time_at_sync
 *   get_epoch_ms    = (esp_timer_get_time() + epoch_offset_us) / 1000
 *
 *   This means: even after a reconnect, the epoch offset remains valid
 *   because esp_timer_get_time() is monotonic and never reset (only system restart resets it).
 */

#include "time_sync.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <stdatomic.h>

static const char *TAG = "TIME";

/* Epoch offset in microseconds:
 *   0 = not synced (use monotonic)
 *  >0 = NTP-derived offset to add to esp_timer_get_time() */
static _Atomic int64_t s_epoch_offset_us = 0;
static _Atomic bool    s_synced          = false;

/* =========================================================
 * INITIALIZATION
 * ========================================================= */
void time_sync_init(void)
{
    /* esp_timer is always running; no explicit start needed in ESP-IDF 5.x.
     * Reset epoch state to monotonic-only mode. */
    atomic_store(&s_epoch_offset_us, 0LL);
    atomic_store(&s_synced, false);
    ESP_LOGI(TAG, "Time sync initialized (monotonic-only mode)");
}

/* =========================================================
 * MONOTONIC CLOCK
 * ========================================================= */
uint32_t time_sync_get_ms(void)
{
    /* Cast to uint32 — rolls over after ~49.7 days.
     * Acceptable: patient monitoring devices are rebooted for maintenance
     * well before that window. */
    return (uint32_t)(esp_timer_get_time() / 1000LL);
}

int64_t time_sync_get_us(void)
{
    return esp_timer_get_time();
}

uint32_t time_sync_uptime_sec(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000LL);
}

/* =========================================================
 * WALL CLOCK
 * ========================================================= */
void time_sync_set_epoch(uint64_t epoch_ms_utc)
{
    int64_t now_us    = esp_timer_get_time();
    int64_t offset_us = (int64_t)(epoch_ms_utc * 1000ULL) - now_us;

    atomic_store(&s_epoch_offset_us, offset_us);
    atomic_store(&s_synced, true);

    ESP_LOGI(TAG, "Epoch set: UTC=%llu ms (offset=%lld us)",
             (unsigned long long)epoch_ms_utc, (long long)offset_us);
}

uint64_t time_sync_get_epoch_ms(void)
{
    int64_t now_us    = esp_timer_get_time();
    int64_t offset_us = atomic_load(&s_epoch_offset_us);

    if (offset_us == 0) {
        /* Not synced: return monotonic ms */
        return (uint64_t)(now_us / 1000LL);
    }

    int64_t epoch_us = now_us + offset_us;
    return (uint64_t)(epoch_us / 1000LL);
}

bool time_sync_is_synced(void)
{
    return atomic_load(&s_synced);
}

uint32_t time_sync_get_epoch_sec(void)
{
    uint64_t ms = time_sync_get_epoch_ms();
    return (uint32_t)(ms / 1000ULL);
}
