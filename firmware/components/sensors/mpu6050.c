/**
 * @file mpu6050.c
 * @brief Driver MPU6050 — acelerômetro + giroscópio (I2C, barramento compartilhado)
 *
 * I2C já inicializado pelo oled_init() ou hdc1080_init().
 * i2c_driver_install retorna ESP_ERR_INVALID_STATE se já instalado — trata como OK.
 */

#include "mpu6050.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "../../config/hardware_config.h"
#include <math.h>
#include <string.h>

static const char *TAG = "MPU6050";

/* Registradores */
#define MPU_REG_PWR_MGMT_1   0x6B
#define MPU_REG_GYRO_CONFIG  0x1B
#define MPU_REG_ACCEL_CONFIG 0x1C
#define MPU_REG_ACCEL_XOUT_H 0x3B
#define MPU_REG_TEMP_OUT_H   0x41
#define MPU_REG_GYRO_XOUT_H  0x43
#define MPU_REG_WHO_AM_I     0x75
#define MPU_WHO_AM_I_VAL     0x68

/* Escala padrão: ±2g (accel), ±250 °/s (gyro) */
#define ACCEL_SCALE_2G       16384.0f
#define GYRO_SCALE_250       131.0f

/* Limiar de detecção de movimento (g) */
#define MOTION_THRESHOLD_G   0.3f

static bool s_initialized = false;
static float s_accel_offset[3] = {0.0f, 0.0f, 0.0f};

/* =========================================================
 * HELPERS I2C
 * ========================================================= */
static esp_err_t mpu_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (HW_MPU6050_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, buf, 2, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(HW_MPU6050_I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t mpu_read_regs(uint8_t start_reg, uint8_t *buf, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (HW_MPU6050_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, start_reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (HW_MPU6050_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    if (len > 1) i2c_master_read(cmd, buf, len - 1, I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, buf + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(HW_MPU6050_I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

/* =========================================================
 * INICIALIZAÇÃO
 * ========================================================= */
esp_err_t mpu6050_init(void)
{
    if (s_initialized) return ESP_OK;

    /* I2C — reutiliza bus já configurado */
    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = HW_OLED_SDA_GPIO,
        .scl_io_num       = HW_OLED_SCL_GPIO,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = HW_MPU6050_I2C_FREQ_HZ,
    };
    i2c_param_config(HW_MPU6050_I2C_PORT, &conf);
    esp_err_t ret = i2c_driver_install(HW_MPU6050_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "i2c_driver_install: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Verifica WHO_AM_I */
    uint8_t who_am_i = 0;
    ret = mpu_read_regs(MPU_REG_WHO_AM_I, &who_am_i, 1);
    if (ret != ESP_OK || who_am_i != MPU_WHO_AM_I_VAL) {
        ESP_LOGW(TAG, "MPU6050 não detectado (WHO_AM_I=0x%02X, esperado=0x%02X)",
                 who_am_i, MPU_WHO_AM_I_VAL);
        return (ret == ESP_OK) ? ESP_ERR_NOT_FOUND : ret;
    }

    /* Acorda o sensor (limpa sleep bit) */
    ESP_ERROR_CHECK(mpu_write_reg(MPU_REG_PWR_MGMT_1, 0x00));
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Configura gyro ±250 °/s e accel ±2g */
    ESP_ERROR_CHECK(mpu_write_reg(MPU_REG_GYRO_CONFIG,  0x00));
    ESP_ERROR_CHECK(mpu_write_reg(MPU_REG_ACCEL_CONFIG, 0x00));

    s_initialized = true;
    ESP_LOGI(TAG, "MPU6050 inicializado (addr=0x%02X)", HW_MPU6050_I2C_ADDR);
    return ESP_OK;
}

/* =========================================================
 * LEITURA
 * ========================================================= */
esp_err_t mpu6050_read(mpu6050_reading_t *out)
{
    if (!s_initialized || out == NULL) {
        if (out) {
            memset(out, 0, sizeof(*out));
            out->valid = false;
        }
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t raw[14];
    /* Lê 14 bytes: ACCEL(6) + TEMP(2) + GYRO(6) */
    esp_err_t ret = mpu_read_regs(MPU_REG_ACCEL_XOUT_H, raw, 14);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha na leitura: %s", esp_err_to_name(ret));
        out->valid = false;
        return ret;
    }

    int16_t ax = (int16_t)((raw[0] << 8) | raw[1]);
    int16_t ay = (int16_t)((raw[2] << 8) | raw[3]);
    int16_t az = (int16_t)((raw[4] << 8) | raw[5]);
    int16_t t  = (int16_t)((raw[6] << 8) | raw[7]);
    int16_t gx = (int16_t)((raw[8]  << 8) | raw[9]);
    int16_t gy = (int16_t)((raw[10] << 8) | raw[11]);
    int16_t gz = (int16_t)((raw[12] << 8) | raw[13]);

    out->accel_x_g = (float)ax / ACCEL_SCALE_2G;
    out->accel_y_g = (float)ay / ACCEL_SCALE_2G;
    out->accel_z_g = (float)az / ACCEL_SCALE_2G;

    out->gyro_x_dps = (float)gx / GYRO_SCALE_250;
    out->gyro_y_dps = (float)gy / GYRO_SCALE_250;
    out->gyro_z_dps = (float)gz / GYRO_SCALE_250;

    out->temperature_celsius = (float)t / 340.0f + 36.53f;

    /* Detecção de movimento simples: magnitude da aceleração > limiar */
    float mag = sqrtf(out->accel_x_g * out->accel_x_g +
                      out->accel_y_g * out->accel_y_g +
                      out->accel_z_g * out->accel_z_g);
    /* Subtrai 1g (gravidade) para detectar aceleração real */
    out->motion_detected = (fabsf(mag - 1.0f) > MOTION_THRESHOLD_G);
    out->valid = true;

    ESP_LOGD(TAG, "ax=%.2f ay=%.2f az=%.2f T=%.1f°C motion=%d",
             out->accel_x_g, out->accel_y_g, out->accel_z_g,
             out->temperature_celsius, out->motion_detected);
    return ESP_OK;
}

bool mpu6050_is_motion_detected(void)
{
    if (!s_initialized) return false;
    mpu6050_reading_t r;
    if (mpu6050_read(&r) != ESP_OK) return false;
    return r.motion_detected;
}
