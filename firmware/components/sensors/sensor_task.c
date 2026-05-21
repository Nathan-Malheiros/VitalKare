/**
 * @file sensor_task.c
 * @brief Task de polling de sensores ambientais — 30 s, publica via TCP (VK Protocol)
 *
 * ARQUITETURA:
 *   Produtor de dados ambientais. A cada APP_SENSOR_POLL_INTERVAL_MS:
 *   1. Lê HDC1080 (temp, umid), LDR (lux), MPU6050 (accel/giroscópio)
 *   2. Preenche pkt_telemetry_env_t → vk_frame_build() → tcp_client_send_normal()
 *   3. Se movimento detectado → frame pkt_telemetry_motion_t na fila crítica
 *   4. Posta EVT_SENSOR_READING / EVT_SENSOR_MOTION para outros subsistemas
 *
 * RESPONSABILIDADES:
 *   - Leitura e validação dos sensores
 *   - Empacotamento em structs tipadas (sem JSON, sem HTTP)
 *   - Descarte silencioso se TCP offline (telemetria é lossy by design)
 *   - Detecção de postura por eixos do acelerômetro
 *
 * FILA:
 *   tcp_client_send_normal() → fila normal TCP (lossy, sem retransmissão)
 *   EVT_SENSOR_MOTION via event_post_critical() se movimento detectado
 */

#include "sensor_task.h"
#include "hdc1080.h"
#include "mpu6050.h"
#include "ldr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "../../config/app_config.h"
#include "../../config/device_config.h"
#include "../network/tcp_client.h"
#include "../network/vk_protocol.h"
#include "../events/event_dispatcher.h"
#include "../core/task_monitor.h"
#include "../display/ui_renderer.h"

static const char *TAG = "SENSORS";

/* =========================================================
 * DERIVAÇÃO DE POSTURA (acelerômetro MPU6050)
 * =========================================================
 * Lógica:
 *  - Movimento detectado (variação do módulo) → "Movimento Detectado"
 *  - |Az| > 0.8 g (eixo Z dominante)          → "Deitado"
 *  - |Ax| > 0.8 g ou |Ay| > 0.8 g             → "Sentado"
 *  - Caso contrário                            → "Em Repouso"
 */
#include <math.h>
static const char *derive_posture_label(const mpu6050_reading_t *mpu)
{
    if (!mpu->valid)          return "Desconhecido";
    if (mpu->motion_detected) return "Movimento Detectado";

    float az = fabsf(mpu->accel_z_g);
    float ax = fabsf(mpu->accel_x_g);
    float ay = fabsf(mpu->accel_y_g);

    if (az > 0.8f)              return "Deitado";
    if (ay > 0.8f || ax > 0.8f) return "Sentado";
    return "Em Repouso";
}

esp_err_t sensor_task_init(void)
{
    /* Sensores já inicializados em init_hardware() — nada a fazer aqui */
    return ESP_OK;
}

void task_sensor_poll(void *arg)
{
    ESP_LOGI(TAG, "Task sensor_poll iniciada");

    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        task_monitor_heartbeat(TASK_ID_SENSOR);
        /* ---- HDC1080 ---- */
        hdc1080_reading_t hdc = {0};
        if (hdc1080_read(&hdc) != ESP_OK || !hdc.valid) {
            ESP_LOGW(TAG, "HDC1080: leitura inválida");
            hdc.temperature_celsius = 0.0f;
            hdc.humidity_percent    = 0.0f;
        }

        /* ---- LDR ---- */
        float lux = 0.0f;
        ldr_read(&lux, NULL);

        /* ---- MPU6050 ---- */
        mpu6050_reading_t mpu = {0};
        bool motion = false;
        if (mpu6050_read(&mpu) == ESP_OK && mpu.valid) {
            motion = mpu.motion_detected;
        }

        /* Deriva postura a partir dos dados do acelerômetro */
        const char *posture = derive_posture_label(&mpu);

        /* Nível de ruído — placeholder (ADC de microfone analógico) */
        float noise_db = 0.0f;

        ESP_LOGI(TAG, "T=%.1f°C H=%.1f%% Lux=%.0f Motion=%d Postura=%s",
                 hdc.temperature_celsius, hdc.humidity_percent, lux, motion, posture);

        /* --- Atualiza display OLED com leituras atuais --- */
        {
            ui_operational_data_t op = {0};
            op.temperature = hdc.temperature_celsius;
            op.humidity    = hdc.humidity_percent;
            op.lux         = lux;
            strncpy(op.device_id, DEVICE_ID, sizeof(op.device_id) - 1);
            ui_update_operational_data(&op);
        }

        /* --- Envia telemetria ambiental via TCP (lossy — descarta se offline) --- */
        pkt_telemetry_env_t env_pkt = {
            .temperature_c = hdc.temperature_celsius,
            .humidity_pct  = hdc.humidity_percent,
            .light_lux     = (uint16_t)(lux > 65535.0f ? 65535 : (int)lux),
            .noise_db      = (uint16_t)noise_db,
            .timestamp_ms  = (uint32_t)(esp_timer_get_time() / 1000),
        };
        static uint8_t s_env_frame[VK_HEADER_SIZE
                                    + sizeof(pkt_telemetry_env_t)
                                    + VK_CRC_SIZE];
        int flen = vk_frame_build(s_env_frame, sizeof(s_env_frame),
                                  PKT_TELEMETRY_ENV, tcp_client_next_seq(),
                                  &env_pkt, sizeof(env_pkt));
        if (flen > 0) {
            if (!tcp_client_send_normal(s_env_frame, (uint16_t)flen)) {
                ESP_LOGD(TAG, "TCP offline — telemetria ambiental descartada");
            }
        }

        /* --- Posta evento para outros subsistemas (display, log) --- */
        system_event_t evt = {
            .type         = EVT_SENSOR_READING,
            .priority     = EVT_PRIO_LOW,
            .timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS,
        };
        if (motion) {
            evt.type     = EVT_SENSOR_MOTION;
            evt.priority = EVT_PRIO_MEDIUM;
            event_post_critical(&evt, false);   /* movimento pode indicar queda */
        } else {
            event_post_normal(&evt);
        }

        /* PROBLEMA CORRIGIDO: vTaskDelay(30 s) estava exatamente no limite de
         * TASK_MONITOR_REBOOT_THRESHOLD_MS (30 s), causando reboots intermitentes
         * por jitter de agendamento. Solução: vTaskDelayUntil com heartbeat chamado
         * a cada slice de 10 s (bem abaixo do FAULT_THRESHOLD=15 s). */
        {
            const TickType_t slice = pdMS_TO_TICKS(10000);
            TickType_t remaining   = pdMS_TO_TICKS(APP_SENSOR_POLL_INTERVAL_MS);
            while (remaining > 0) {
                TickType_t step = (remaining > slice) ? slice : remaining;
                vTaskDelayUntil(&last_wake, step);
                remaining -= step;
                task_monitor_heartbeat(TASK_ID_SENSOR);
            }
        }
    }
}
