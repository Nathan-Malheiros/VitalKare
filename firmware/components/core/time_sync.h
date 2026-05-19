/**
 * @file time_sync.h
 * @brief VitalKare Unified Clock Authority
 *
 * ARCHITECTURE RATIONALE:
 *   Every subsystem calling esp_timer_get_time() independently creates
 *   subtle inconsistencies: log entries, audio session headers, journal records,
 *   and telemetry packets all use slightly different read times, and none share
 *   a common epoch reference. This module centralizes all timestamp generation.
 *
 * DESIGN:
 *   - Base: esp_timer_get_time() — monotonic, 64-bit µs, no drift, no rollover for ~584k years
 *   - Public API returns uint32_t ms (rolls over after ~49 days of uptime — acceptable)
 *   - Epoch: optional wall-clock offset set by NTP/RTC sync
 *   - Without sync: all timestamps are uptime-relative (consistent within device session)
 *   - With sync: timestamps are UTC ms (consistent across sessions and devices)
 *
 * CONCURRENCY:
 *   All reads are lock-free. The epoch offset is an atomic int64_t written
 *   at most once per sync event. Hot-path callers see consistent snapshots.
 *
 * OWNERSHIP:
 *   - epoch_offset_us: owned by this module, written by NTP/RTC callback
 *   - All other state: read-only derived from hardware timer
 *
 * USAGE:
 *   Replace ALL direct esp_timer_get_time() calls with time_sync_get_us().
 *   Replace ALL (uint32_t)(esp_timer_get_time() / 1000) with time_sync_get_ms().
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================
 * INITIALIZATION
 * ========================================================= */

/**
 * @brief Initialize the time sync module.
 *        Must be called before any other time_sync_* function.
 *        Safe to call before FreeRTOS scheduler starts.
 */
void time_sync_init(void);

/* =========================================================
 * MONOTONIC CLOCK — uptime-relative, always available
 * ========================================================= */

/**
 * @brief Returns monotonic milliseconds since boot.
 *        Lock-free. Safe from any task or ISR context.
 *        Rolls over after ~49.7 days of uptime.
 *
 * Use for: audio session timestamps, telemetry timestamps,
 *          debounce intervals, timeout calculations.
 */
uint32_t time_sync_get_ms(void);

/**
 * @brief Returns monotonic microseconds since boot.
 *        64-bit, no rollover in practice.
 *
 * Use for: event tracing, latency profiling, precise interval measurement.
 */
int64_t time_sync_get_us(void);

/**
 * @brief Returns uptime in whole seconds since boot.
 *        Convenience wrapper. Safe from any context.
 */
uint32_t time_sync_uptime_sec(void);

/* =========================================================
 * WALL CLOCK — requires NTP/RTC sync
 * ========================================================= */

/**
 * @brief Set wall-clock epoch from NTP or RTC.
 *        Thread-safe — uses atomic write.
 *        Can be called multiple times (re-sync).
 *
 * @param epoch_ms_utc  Current UTC time in milliseconds (Unix epoch)
 */
void time_sync_set_epoch(uint64_t epoch_ms_utc);

/**
 * @brief Returns current time in UTC milliseconds if synced,
 *        or monotonic uptime milliseconds if not synced.
 *
 * Use for: log timestamps, journal record timestamps (for cross-device correlation).
 *
 * @note The return type is uint64_t to hold Unix epoch in ms (currently ~1.7e12).
 */
uint64_t time_sync_get_epoch_ms(void);

/**
 * @brief Returns true if epoch has been set via time_sync_set_epoch().
 */
bool time_sync_is_synced(void);

/**
 * @brief Returns seconds since Unix epoch (1970-01-01) if synced, else 0.
 *        Convenience wrapper for log_write() calls.
 */
uint32_t time_sync_get_epoch_sec(void);

#ifdef __cplusplus
}
#endif
