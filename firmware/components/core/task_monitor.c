/**
 * @file task_monitor.c
 * @brief Monitor de liveness por task — implementação
 *
 * IMPLEMENTAÇÃO:
 *   Um array de `_Atomic int64_t s_timestamps[TASK_ID_COUNT]` armazena o
 *   instante (µs via esp_timer_get_time()) do último heartbeat de cada task.
 *   O valor 0 significa "nunca registrou heartbeat".
 *
 *   task_monitor_stall_ms() computa:
 *     stall = (now_us - last_us) / 1000
 *   Retorna INT32_MAX se last_us == 0 (nunca registrou).
 *
 * INVARIANTE:
 *   Os timestamps só crescem. Não há reset parcial.
 *   Isso garante que um stall não seja mascarado por re-init acidental.
 */

#include "task_monitor.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <stdatomic.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

static const char *TAG = "TASK_MON";

/* =========================================================
 * ESTADO INTERNO
 * ========================================================= */
static _Atomic int64_t s_timestamps[TASK_ID_COUNT];

static const char * const s_task_names[TASK_ID_COUNT] = {
    [TASK_ID_TCP_TX]      = "tcp_tx",
    [TASK_ID_TCP_RX]      = "tcp_rx",
    [TASK_ID_AUDIO_CTRL]  = "audio_ctrl",
    [TASK_ID_AUDIO_ENC]   = "audio_enc",
    [TASK_ID_DISPATCHER]  = "dispatcher",
    [TASK_ID_SENSOR]      = "sensor",
    [TASK_ID_OXIMETER]    = "oximeter",
    [TASK_ID_DISPLAY]     = "display",
    [TASK_ID_SYS_MANAGER] = "sys_mgr",
};

/* =========================================================
 * IMPLEMENTAÇÃO
 * ========================================================= */
void task_monitor_init(void)
{
    for (int i = 0; i < TASK_ID_COUNT; i++) {
        atomic_store(&s_timestamps[i], 0LL);
    }
    ESP_LOGI(TAG, "Inicializado (%d tasks monitoradas)", TASK_ID_COUNT);
}

void task_monitor_heartbeat(task_id_t id)
{
    if ((unsigned)id >= TASK_ID_COUNT) return;
    atomic_store(&s_timestamps[id], esp_timer_get_time());
}

uint32_t task_monitor_stall_ms(task_id_t id)
{
    if ((unsigned)id >= TASK_ID_COUNT) return 0;

    int64_t last_us = atomic_load(&s_timestamps[id]);
    if (last_us == 0) return UINT32_MAX;  /* nunca registrou */

    int64_t now_us = esp_timer_get_time();
    int64_t stall_us = now_us - last_us;
    if (stall_us < 0) stall_us = 0;
    uint64_t stall_ms = (uint64_t)stall_us / 1000ULL;
    return (stall_ms > UINT32_MAX) ? UINT32_MAX : (uint32_t)stall_ms;
}

void task_monitor_get_snapshot(task_id_t id, task_monitor_snapshot_t *out)
{
    if (!out || (unsigned)id >= TASK_ID_COUNT) return;

    uint32_t stall = task_monitor_stall_ms(id);
    out->id         = id;
    out->name       = s_task_names[id];
    out->registered = (atomic_load(&s_timestamps[id]) != 0LL);
    out->stall_ms   = out->registered ? stall : 0;
    out->faulted    = out->registered &&
                      (stall >= TASK_MONITOR_FAULT_THRESHOLD_MS);
}

bool task_monitor_any_faulted(void)
{
    for (int i = 0; i < TASK_ID_COUNT; i++) {
        int64_t last_us = atomic_load(&s_timestamps[i]);
        if (last_us == 0) continue;  /* task não registrada ainda */

        int64_t stall_us = esp_timer_get_time() - last_us;
        if (stall_us > (int64_t)TASK_MONITOR_FAULT_THRESHOLD_MS * 1000LL) {
            return true;
        }
    }
    return false;
}

void task_monitor_get_faulted(task_id_t *out_ids, uint8_t *out_count)
{
    if (!out_ids || !out_count) return;
    *out_count = 0;
    int64_t now_us = esp_timer_get_time();
    for (int i = 0; i < TASK_ID_COUNT; i++) {
        int64_t last_us = atomic_load(&s_timestamps[i]);
        if (last_us == 0) continue;
        int64_t stall_us = now_us - last_us;
        if (stall_us > (int64_t)TASK_MONITOR_FAULT_THRESHOLD_MS * 1000LL) {
            out_ids[(*out_count)++] = (task_id_t)i;
        }
    }
}

const char *task_monitor_name(task_id_t id)
{
    if ((unsigned)id >= TASK_ID_COUNT) return "?";
    return s_task_names[id];
}
