/**
 * @file event_trace.h
 * @brief VitalKare Event Traceability & Latency Profiling
 *
 * ARCHITECTURE RATIONALE:
 *   Queue starvation, handler latency spikes, and dropped events are invisible
 *   in production without tracing. This module captures the full lifecycle of
 *   every event: enqueue → dispatch_start → dispatch_end, stored in a circular
 *   ring buffer for live diagnostics and post-mortem analysis.
 *
 * DESIGN:
 *   - Ring buffer: EVENT_TRACE_RING_SIZE entries (default 64), circular overwrite
 *   - Each entry: event_type, queue_level, enqueue_us, dispatch_us, complete_us
 *   - Stats: total_dispatched, dropped_events, max_handler_us, max_queue_wait_us
 *   - Compile-time disable: define EVENT_TRACE_DISABLED to strip all overhead
 *
 * CONCURRENCY:
 *   - All trace functions use a portMUX spinlock (< 1µs) — safe from task context
 *   - NOT safe from ISR context (ESP_LOG is not ISR-safe; use event_trace_record_enqueue_fromISR if needed)
 *   - The ring buffer is owned exclusively by this module
 *
 * USAGE:
 *   Call from event_dispatcher.c:
 *     event_trace_record_enqueue(evt->type, EVT_QUEUE_CRITICAL);
 *     event_trace_on_dispatch_start(evt->type);
 *     // ... handler executes ...
 *     event_trace_on_dispatch_end(evt->type);
 *
 *   Read stats periodically from system_manager's monitoring task.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "../events/event_dispatcher.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================
 * CONFIGURATION
 * ========================================================= */

/** Depth of the circular trace ring. Must be power-of-2. */
#define EVENT_TRACE_RING_SIZE   64u

/** Queue level identifiers for trace entries. */
typedef enum {
    EVT_TRACE_Q_CRITICAL = 0,
    EVT_TRACE_Q_NORMAL   = 1,
} event_trace_queue_t;

/* =========================================================
 * TRACE ENTRY — per-event lifecycle record
 * ========================================================= */
typedef struct {
    event_type_t         type;           /**< Event type being traced */
    event_trace_queue_t  queue;          /**< Which queue was used */
    int64_t              enqueue_us;     /**< esp_timer time at enqueue */
    int64_t              dispatch_us;    /**< esp_timer time at dispatch start */
    int64_t              complete_us;    /**< esp_timer time at dispatch end */
    bool                 valid;          /**< Entry has been written at least once */
} event_trace_entry_t;

/* =========================================================
 * AGGREGATE STATISTICS — reset-on-read or cumulative
 * ========================================================= */
typedef struct {
    uint32_t total_dispatched;      /**< Total events processed */
    uint32_t dropped_critical;      /**< Events dropped from critical queue (full) */
    uint32_t dropped_normal;        /**< Events dropped from normal queue (full) */
    uint32_t congestion_events;     /**< Times queue was >75% full at enqueue */
    int64_t  max_handler_us;        /**< Longest single handler execution */
    int64_t  max_queue_wait_us;     /**< Longest time between enqueue and dispatch */
    int64_t  avg_handler_us;        /**< Running average handler time (× 100 for precision) */
} event_trace_stats_t;

/* =========================================================
 * API
 * ========================================================= */

/** Initialize the trace module. Must be called after FreeRTOS scheduler starts. */
void event_trace_init(void);

/**
 * @brief Record event enqueue.
 *        Called from event_post_critical() / event_post_normal().
 *        @param type       Event type being enqueued.
 *        @param queue      Which queue level.
 *        @param dropped    True if xQueueSend failed (event was dropped).
 */
void event_trace_record_enqueue(event_type_t type,
                                event_trace_queue_t queue,
                                bool dropped);

/**
 * @brief Record dispatch start (just before handler is called).
 *        Call from task_event_dispatcher(), before dispatch_event().
 *        @param type  Event type being dispatched.
 */
void event_trace_on_dispatch_start(event_type_t type);

/**
 * @brief Record dispatch end (handler returned).
 *        Call from task_event_dispatcher(), after dispatch_event() returns.
 *        @param type  Event type that was dispatched.
 */
void event_trace_on_dispatch_end(event_type_t type);

/**
 * @brief Get a snapshot of aggregate statistics.
 *        Thread-safe. Does NOT reset counters.
 */
void event_trace_get_stats(event_trace_stats_t *out);

/**
 * @brief Copy last N entries from the ring buffer (most recent first).
 *        @param out    Buffer to receive entries.
 *        @param count  How many entries to copy (capped at EVENT_TRACE_RING_SIZE).
 *        @return       Number of entries actually copied.
 */
uint32_t event_trace_get_recent(event_trace_entry_t *out, uint32_t count);

/**
 * @brief Reset all statistics counters.
 *        Does NOT clear the ring buffer (historical entries preserved).
 */
void event_trace_reset_stats(void);

#ifdef __cplusplus
}
#endif
