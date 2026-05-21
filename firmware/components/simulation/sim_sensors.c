/**
 * @file sim_sensors.c
 * @brief Simulated sensor task — DevKit development/validation mode.
 *
 * Only compiled into an active code path when VK_SIMULATION_MODE is defined.
 * On KiiraV2, this file compiles to nothing — no impact on production build.
 *
 * Behavior:
 *   - Every  5 s: sends PKT_TELEMETRY_OXIMETER with synthetic HR/SpO2 data
 *   - Every 10 s: sends PKT_TELEMETRY_ENV with synthetic temp/humidity/light data
 *   - At 30 s, then every 300 s: fires virtual EVT_BTN_A_PRESSED (care request flow)
 *   - Every iteration: calls task_monitor_heartbeat for SENSOR + OXIMETER task IDs
 *     so the watchdog does not stall-reboot the device
 *
 * Sensor value ranges (clinical / lab plausible):
 *   Temperature  : 22 – 25 °C
 *   Humidity     : 45 – 65 %
 *   Light        : 100 – 600 lux
 *   Noise        : 30 – 55 dB (arbitrary, shows env channel active)
 *   Heart rate   : 65 – 87 bpm
 *   SpO2         : 96 – 100 %
 *   Confidence   : 85 – 100 %
 */

#include "../../config/board_profile.h"

#if VK_SIMULATION_MODE  /* ---- entire file is guarded ---- */

#include <math.h>
#include <stdint.h>

#include "sim_sensors.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "../../config/app_config.h"
#include "../core/task_monitor.h"
#include "../events/event_dispatcher.h"
#include "../network/tcp_client.h"
#include "../network/vk_protocol.h"

static const char *TAG = "SIM_SENSORS";

/* Interval boundaries (seconds of simulated uptime) */
#define SIM_ENV_INTERVAL_S      10
#define SIM_OXI_INTERVAL_S      5
#define SIM_BTN_A_FIRST_S       30    /* first virtual care-request after 30 s */
#define SIM_BTN_A_PERIOD_S      300   /* then every 5 min */

/* ─────────────────────────────────────────────────────────────
 * Helpers — bounded sine wave oscillation
 * center + amplitude * sin(phase)  →  [center-amp, center+amp]
 * ───────────────────────────────────────────────────────────── */
static inline float sim_wave(float center, float amplitude,
                              float uptime_s, float freq_rads)
{
    return center + amplitude * sinf(uptime_s * freq_rads);
}

static inline int sim_wave_int(int center, int amplitude,
                                float uptime_s, float freq_rads)
{
    return center + (int)roundf((float)amplitude * sinf(uptime_s * freq_rads));
}

/* ─────────────────────────────────────────────────────────────
 * Public API
 * ───────────────────────────────────────────────────────────── */
esp_err_t sim_sensors_init(void)
{
    ESP_LOGI(TAG, "[SIMULATION] sim_sensors_init — virtual sensor provider ready");
    return ESP_OK;
}

/* ─────────────────────────────────────────────────────────────
 * Main task
 * ───────────────────────────────────────────────────────────── */
void task_sim_telemetry(void *arg)
{
    (void)arg;

    ESP_LOGW(TAG, "[SIMULATION] task_sim_telemetry started");
    ESP_LOGW(TAG, "[SIMULATION] ENV packet every %d s, OXI every %d s",
             SIM_ENV_INTERVAL_S, SIM_OXI_INTERVAL_S);
    ESP_LOGW(TAG, "[SIMULATION] Virtual BTN_A (care request) at %d s, then every %d s",
             SIM_BTN_A_FIRST_S, SIM_BTN_A_PERIOD_S);

    uint32_t uptime_s = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        uptime_s++;

        float t = (float)uptime_s;

        /* Satisfy watchdog for both slot IDs this task covers */
        task_monitor_heartbeat(TASK_ID_SENSOR);
        task_monitor_heartbeat(TASK_ID_OXIMETER);

        /* ── PKT_TELEMETRY_OXIMETER — every 5 s ── */
        if (uptime_s % SIM_OXI_INTERVAL_S == 0) {
            int bpm  = sim_wave_int(76, 11, t, 0.015f);  /* 65 – 87 bpm */
            int spo2 = sim_wave_int(98,  2, t, 0.009f);  /* 96 – 100 % */
            int conf = sim_wave_int(92,  8, t, 0.021f);  /* 84 – 100 % */

            /* Clamp to physiologically plausible range */
            if (bpm  < 60)  bpm  = 60;
            if (bpm  > 100) bpm  = 100;
            if (spo2 < 94)  spo2 = 94;
            if (spo2 > 100) spo2 = 100;
            if (conf < 80)  conf = 80;
            if (conf > 100) conf = 100;

            pkt_telemetry_oximeter_t pkt = {
                .bpm            = (uint8_t)bpm,
                .spo2_pct       = (uint8_t)spo2,
                .finger_detected = 1,
                .confidence     = (uint8_t)conf,
                .timestamp_ms   = (uint32_t)(esp_timer_get_time() / 1000LL),
            };

            uint8_t frame[VK_HEADER_SIZE + sizeof(pkt_telemetry_oximeter_t) + VK_CRC_SIZE];
            int flen = vk_frame_build(frame, sizeof(frame),
                                      PKT_TELEMETRY_OXIMETER,
                                      tcp_client_next_seq(),
                                      &pkt, sizeof(pkt));
            if (flen > 0) {
                tcp_client_send_normal(frame, (uint16_t)flen);
                ESP_LOGD(TAG, "[SIMULATION] OXI bpm=%d spo2=%d conf=%d",
                         bpm, spo2, conf);
            }
        }

        /* ── PKT_TELEMETRY_ENV — every 10 s ── */
        if (uptime_s % SIM_ENV_INTERVAL_S == 0) {
            float temp     = sim_wave(23.5f, 1.5f, t, 0.003f);   /* 22 – 25 °C */
            float humidity = sim_wave(55.0f, 10.f, t, 0.005f);   /* 45 – 65 % */
            int   lux_raw  = sim_wave_int(350, 250, t, 0.007f);  /* 100 – 600 lux */
            int   noise_raw = sim_wave_int(42,  12, t, 0.011f);  /* 30 – 54 dB */

            if (lux_raw   < 0)   lux_raw   = 0;
            if (lux_raw   > 800) lux_raw   = 800;
            if (noise_raw < 0)   noise_raw = 0;

            pkt_telemetry_env_t pkt = {
                .temperature_c = temp,
                .humidity_pct  = humidity,
                .light_lux     = (uint16_t)lux_raw,
                .noise_db      = (uint16_t)noise_raw,
                .timestamp_ms  = (uint32_t)(esp_timer_get_time() / 1000LL),
            };

            uint8_t frame[VK_HEADER_SIZE + sizeof(pkt_telemetry_env_t) + VK_CRC_SIZE];
            int flen = vk_frame_build(frame, sizeof(frame),
                                      PKT_TELEMETRY_ENV,
                                      tcp_client_next_seq(),
                                      &pkt, sizeof(pkt));
            if (flen > 0) {
                tcp_client_send_normal(frame, (uint16_t)flen);
                ESP_LOGD(TAG, "[SIMULATION] ENV temp=%.1f hum=%.1f lux=%d",
                         temp, humidity, lux_raw);
            }
        }

        /* ── Virtual button A (care request) ── */
        bool fire_btn = (uptime_s == SIM_BTN_A_FIRST_S) ||
                        (uptime_s > SIM_BTN_A_FIRST_S &&
                         ((uptime_s - SIM_BTN_A_FIRST_S) % SIM_BTN_A_PERIOD_S) == 0);
        if (fire_btn) {
            ESP_LOGW(TAG, "[SIMULATION] Virtual care-request button (EVT_BTN_A_PRESSED) at %lu s",
                     (unsigned long)uptime_s);
            system_event_t btn_evt = {
                .type         = EVT_BTN_A_PRESSED,
                .priority     = EVT_PRIO_LOW,
                .timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS,
            };
            event_post_normal(&btn_evt);
        }
    }
}

#endif /* VK_SIMULATION_MODE */
