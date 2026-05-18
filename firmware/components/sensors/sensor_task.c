/**
 * @file sensor_task.c
 * @brief Task de polling de sensores ambientais — 30 s, publica via HTTP REST
 */

#include "sensor_task.h"
#include "hdc1080.h"
#include "mpu6050.h"
#include "ldr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "../../config/app_config.h"
#include "vitalkare_http.h"
#include "event_dispatcher.h"

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

    for (;;) {
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

        /* Derivação de postura a partir dos dados do acelerômetro */
        const char *posture = derive_posture_label(&mpu);

        /* Nível de ruído — placeholder (ADC de microfone analógico) */
        float noise_db = 0.0f;

        ESP_LOGI(TAG, "T=%.1f°C H=%.1f%% Lux=%.0f Motion=%d Postura=%s",
                 hdc.temperature_celsius, hdc.humidity_percent, lux, motion, posture);

        /* Monta payload JSON e publica via HTTP REST */
        char json[256];
        snprintf(json, sizeof(json),
                 "{\"device_id\":\"%s\",\"ward_id\":\"%s\","
                 "\"patient_id\":\"%s\",\"firmware_version\":\"%s\","
                 "\"timestamp_ms\":%lu,"
                 "\"temperature_c\":%.2f,\"humidity_pct\":%.2f,"
                 "\"lux\":%.1f,\"noise_db\":%.1f,\"motion\":%s,"
                 "\"posture\":\"%s\"}",
                 APP_DEFAULT_DEVICE_ID, APP_DEFAULT_WARD_ID,
                 APP_DEFAULT_PATIENT_ID, APP_FIRMWARE_VERSION,
                 (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS),
                 hdc.temperature_celsius, hdc.humidity_percent,
                 lux, noise_db,
                 motion ? "true" : "false",
                 posture);

        char endpoint[96];
        snprintf(endpoint, sizeof(endpoint),
                 "/api/v1/devices/%s/environment", APP_DEFAULT_DEVICE_ID);

        esp_err_t http_ret = vitalkare_http_post(endpoint, json);
        if (http_ret != ESP_OK) {
            ESP_LOGW(TAG, "HTTP POST environment falhou: %s", esp_err_to_name(http_ret));
        }

        /* Posta evento para outros subsistemas */
        system_event_t evt = {
            .type         = EVT_SENSOR_READING,
            .priority     = EVT_PRIO_LOW,
            .timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS,
        };
        if (motion) {
            evt.type     = EVT_SENSOR_MOTION;
            evt.priority = EVT_PRIO_MEDIUM;
        }
        event_post(&evt, false);

        vTaskDelay(pdMS_TO_TICKS(APP_SENSOR_POLL_INTERVAL_MS));
    }
}
