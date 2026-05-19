/**
 * @file backpressure_manager.h
 * @brief Resource Overload Protection — Backpressure Level Control
 *
 * ARCHITECTURE RATIONALE:
 *   Embedded systems without virtual memory die when heap is exhausted.
 *   FreeRTOS and lwIP internals fail silently or corrupt state on malloc failure.
 *   This module provides a tiered overload signal computed from real-time
 *   resource consumption (heap, PSRAM, chunk pool, queue fill) that callers
 *   check before initiating resource-intensive operations.
 *
 * DESIGN PRINCIPLES:
 *   1. LAZY EVALUATION — Level is computed on-demand from the last memory_monitor
 *      snapshot. No background task, no timer, no mutex. Cost: one struct read.
 *   2. NO FALSE ALARMS ON EMERGENCY — Emergency audio (AUDIO_TRIGGER_EMERGENCY)
 *      bypasses ALL backpressure checks. Patient safety > system resource protection.
 *   3. GRADUATED RESPONSE — Four levels with increasingly aggressive restrictions.
 *      Telemetry is shed before journal writes; journal writes before recording.
 *   4. FAIL-OPEN ON UNINIT — If memory_monitor hasn't run yet, level = NORMAL.
 *      Startup traffic must be allowed through.
 *
 * LEVEL THRESHOLDS:
 *   NORMAL    — all resources healthy
 *   WARN      — heap < 24 KB OR PSRAM < 256 KB OR chunk_pool ≤ 4 OR queue > 75%
 *   HIGH      — heap < 12 KB OR PSRAM < 128 KB OR chunk_pool ≤ 2
 *   CRITICAL  — heap < 6 KB (imminent malloc failure)
 *
 * POLICY TABLE:
 *   Level      | recording(NORMAL) | recording(EMERGENCY) | telemetry | journal
 *   NORMAL     |        ✓          |          ✓           |     ✓     |    ✓
 *   WARN       |        ✓          |          ✓           |     ✓     |    ✓
 *   HIGH       |        ✗          |          ✓           |     ✗     |    ✓
 *   CRITICAL   |        ✗          |          ✓           |     ✗     |    ✗
 *
 * CONCURRENCY:
 *   backpressure_get_level() reads the memory_monitor snapshot under a brief
 *   critical section (already done inside memory_monitor_get_snapshot).
 *   All other calls are pure computation. Thread-safe without additional locking.
 *
 * OWNERSHIP:
 *   This module is read-only from the perspective of shared resources.
 *   It only reads from memory_monitor and audio_capture pool APIs.
 */

#pragma once

#include <stdbool.h>
#include "../audio/audio_capture.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================
 * BACKPRESSURE LEVEL
 * ========================================================= */
typedef enum {
    BP_LEVEL_NORMAL   = 0,  /**< All resources healthy */
    BP_LEVEL_WARN     = 1,  /**< Early pressure — advisory only */
    BP_LEVEL_HIGH     = 2,  /**< Significant pressure — shed non-essential traffic */
    BP_LEVEL_CRITICAL = 3,  /**< Imminent failure — emergency only */
} backpressure_level_t;

/* =========================================================
 * API
 * ========================================================= */

/**
 * @brief Evaluate current backpressure level from last resource snapshot.
 *        Cheap — reads cached snapshot only, no heap allocation, no mutex.
 *        Returns BP_LEVEL_NORMAL if memory_monitor has not yet run.
 */
backpressure_level_t backpressure_get_level(void);

/**
 * @brief Returns true if recording is allowed for the given trigger.
 *        EMERGENCY trigger always returns true (safety guarantee).
 *        REQUEST trigger is blocked at BP_LEVEL_HIGH and above.
 *
 * @param trigger  Audio trigger type (EMERGENCY or REQUEST).
 */
bool backpressure_allow_recording(audio_trigger_t trigger);

/**
 * @brief Returns true if non-critical telemetry transmission is allowed.
 *        Blocked at BP_LEVEL_HIGH and above.
 */
bool backpressure_allow_telemetry(void);

/**
 * @brief Returns true if appending to the offline journal is allowed.
 *        Blocked only at BP_LEVEL_CRITICAL.
 *        Emergency event metadata is always journaled — callers must NOT
 *        gate JRN_TYPE_EVENT_EMERGENCY writes behind this check.
 */
bool backpressure_allow_journal_append(void);

/**
 * @brief Human-readable label for a backpressure level.
 */
const char *backpressure_level_name(backpressure_level_t level);

#ifdef __cplusplus
}
#endif
