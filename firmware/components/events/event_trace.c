/**
 * @file event_trace.c
 * @brief VitalKare Event Traceability — implementation
 *
 * CONCURRENCY MODEL:
 *   s_spinlock (portMUX) guards all writes to the ring buffer and stats.
 *   Hold time < 1 µs — writes a few integers and increment an index.
 *   portMUX is chosen over mutex to avoid priority inversion on the hot path
 *   and to allow future ISR-context tracing if needed.
 *
 * RING BUFFER SEMANTICS:
 *   s_head: next slot to write (wraps mod RING_SIZE).
 *   Entries are always overwritten on overflow — we preserve the most recent
 *   EVENT_TRACE_RING_SIZE events, which are the most diagnostically useful.
 *
 * DISPATCH TRACKING:
 *   s_dispatch_start_us[EVT_TYPE_MAX] holds the timestamp at dispatch start for
 *   the currently-executing handler of each event type.
 *   This is safe because the dispatcher is single-threaded (one task).
 */

#include "event_trace.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdatomic.h>

static const char *TAG = "TRACE";

/* =========================================================
 * INTERNAL STATE
 * ========================================================= */
#define RING_MASK   (EVENT_TRACE_RING_SIZE - 1u)

/* Ring buffer — circular, overwrite on full */
static event_trace_entry_t s_ring[EVENT_TRACE_RING_SIZE];
static uint32_t            s_head = 0;  /* Next write slot */

/* Per-type dispatch start timestamp (single-threaded dispatcher) */
#define EVT_TYPE_MAX  64u  /* Must be >= largest event_type_t value */
static int64_t s_dispatch_start_us[EVT_TYPE_MAX];

/* Aggregate statistics */
static event_trace_stats_t s_stats;

/* Spinlock for ring + stats access */
static portMUX_TYPE s_spinlock = portMUX_INITIALIZER_UNLOCKED;

static bool s_initialized = false;

/* =========================================================
 * INIT
 * ========================================================= */
void event_trace_init(void)
{
    memset(s_ring, 0, sizeof(s_ring));
    memset(s_dispatch_start_us, 0, sizeof(s_dispatch_start_us));
    memset(&s_stats, 0, sizeof(s_stats));
    s_head = 0;
    s_initialized = true;
    ESP_LOGI(TAG, "Event trace initialized (ring=%u slots)", EVENT_TRACE_RING_SIZE);
}

/* =========================================================
 * ENQUEUE RECORD
 * ========================================================= */
void event_trace_record_enqueue(event_type_t type,
                                event_trace_queue_t queue,
                                bool dropped)
{
    if (!s_initialized) return;

    int64_t now_us = esp_timer_get_time();

    taskENTER_CRITICAL(&s_spinlock);

    if (dropped) {
        if (queue == EVT_TRACE_Q_CRITICAL) s_stats.dropped_critical++;
        else                               s_stats.dropped_normal++;
    } else {
        /* Write ring slot */
        event_trace_entry_t *slot = &s_ring[s_head & RING_MASK];
        slot->type        = type;
        slot->queue       = queue;
        slot->enqueue_us  = now_us;
        slot->dispatch_us = 0;
        slot->complete_us = 0;
        slot->valid       = true;
        s_head++;
    }

    taskEXIT_CRITICAL(&s_spinlock);
}

/* =========================================================
 * DISPATCH START
 * ========================================================= */
void event_trace_on_dispatch_start(event_type_t type)
{
    if (!s_initialized) return;
    if ((uint32_t)type >= EVT_TYPE_MAX) return;

    int64_t now_us = esp_timer_get_time();

    taskENTER_CRITICAL(&s_spinlock);
    s_dispatch_start_us[type] = now_us;

    /* Annotate the matching ring entry (walk backwards to find last enqueue of this type) */
    uint32_t idx = (s_head - 1u) & RING_MASK;
    for (uint32_t i = 0; i < EVENT_TRACE_RING_SIZE; i++) {
        event_trace_entry_t *e = &s_ring[idx];
        if (e->valid && e->type == type && e->dispatch_us == 0) {
            e->dispatch_us = now_us;

            /* Update congestion stat: queue wait > 10 ms is notable */
            int64_t wait = now_us - e->enqueue_us;
            if (wait > 10000LL) {
                s_stats.congestion_events++;
                if (wait > s_stats.max_queue_wait_us) {
                    s_stats.max_queue_wait_us = wait;
                }
            }
            break;
        }
        idx = (idx - 1u) & RING_MASK;
    }

    taskEXIT_CRITICAL(&s_spinlock);
}

/* =========================================================
 * DISPATCH END
 * ========================================================= */
void event_trace_on_dispatch_end(event_type_t type)
{
    if (!s_initialized) return;
    if ((uint32_t)type >= EVT_TYPE_MAX) return;

    int64_t now_us    = esp_timer_get_time();
    int64_t start_us  = s_dispatch_start_us[type];
    int64_t handler_us = (start_us > 0) ? (now_us - start_us) : 0;

    taskENTER_CRITICAL(&s_spinlock);

    s_stats.total_dispatched++;
    if (handler_us > s_stats.max_handler_us) {
        s_stats.max_handler_us = handler_us;
    }
    /* Running average (× 1024 for precision without float) */
    s_stats.avg_handler_us =
        (s_stats.avg_handler_us * 63LL + handler_us * 1024LL) / 64LL;

    /* Annotate ring entry */
    uint32_t idx = (s_head - 1u) & RING_MASK;
    for (uint32_t i = 0; i < EVENT_TRACE_RING_SIZE; i++) {
        event_trace_entry_t *e = &s_ring[idx];
        if (e->valid && e->type == type && e->dispatch_us > 0 && e->complete_us == 0) {
            e->complete_us = now_us;
            break;
        }
        idx = (idx - 1u) & RING_MASK;
    }

    taskEXIT_CRITICAL(&s_spinlock);

    /* Log if handler took suspiciously long */
    if (handler_us > 20000LL) {
        ESP_LOGW(TAG, "Slow handler: type=%d took %lld us", (int)type, (long long)handler_us);
    }
}

/* =========================================================
 * GET STATISTICS
 * ========================================================= */
void event_trace_get_stats(event_trace_stats_t *out)
{
    if (!out) return;

    taskENTER_CRITICAL(&s_spinlock);
    *out = s_stats;
    taskEXIT_CRITICAL(&s_spinlock);
}

/* =========================================================
 * GET RECENT ENTRIES
 * ========================================================= */
uint32_t event_trace_get_recent(event_trace_entry_t *out, uint32_t count)
{
    if (!out || count == 0) return 0;
    if (count > EVENT_TRACE_RING_SIZE) count = EVENT_TRACE_RING_SIZE;

    uint32_t copied = 0;

    taskENTER_CRITICAL(&s_spinlock);
    /* Walk backwards from most-recent entry */
    uint32_t idx = (s_head - 1u) & RING_MASK;
    for (uint32_t i = 0; i < count; i++) {
        if (s_ring[idx].valid) {
            out[copied++] = s_ring[idx];
        }
        idx = (idx - 1u) & RING_MASK;
    }
    taskEXIT_CRITICAL(&s_spinlock);

    return copied;
}

/* =========================================================
 * RESET STATS
 * ========================================================= */
void event_trace_reset_stats(void)
{
    taskENTER_CRITICAL(&s_spinlock);
    memset(&s_stats, 0, sizeof(s_stats));
    taskEXIT_CRITICAL(&s_spinlock);
    ESP_LOGI(TAG, "Stats reset");
}
