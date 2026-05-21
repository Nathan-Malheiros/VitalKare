/**
 * @file hdc1080.c
 * @brief Implementação do driver HDC1080
 *
 * Datasheet: https://www.ti.com/lit/ds/symlink/hdc1080.pdf
 * Endereço I2C: 0x40 (fixo)
 *
 * Protocolo:
 *   1. Escreve endereço do registrador (0x00 = Temperature, 0x01 = Humidity)
 *   2. Aguarda tempo de conversão (~15 ms para 14-bit)
 *   3. Lê 2 bytes do resultado
 */

#include "hdc1080.h"
#include "driver/i2c_master.h"
#include "i2c_bus.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "HDC1080";

/* Endereço I2C */
#define HDC1080_I2C_ADDR            0x40

/* Registradores */
#define HDC1080_REG_TEMPERATURE     0x00
#define HDC1080_REG_HUMIDITY        0x01
#define HDC1080_REG_CONFIG          0x02
#define HDC1080_REG_MANUFACTURER_ID 0xFE
#define HDC1080_MANUFACTURER_ID     0x5449  /**< "TI" */
#define HDC1080_CONVERSION_DELAY_MS 20

static bool s_initialized = false;
static i2c_master_dev_handle_t s_dev_handle = NULL;

/* =========================================================
 * HELPERS I2C (novo driver)
 * ========================================================= */
static esp_err_t i2c_write_byte(uint8_t reg)
{
    return i2c_master_transmit(s_dev_handle, &reg, 1, pdMS_TO_TICKS(100));
}

static esp_err_t i2c_read_bytes(uint8_t *buf, size_t len)
{
    return i2c_master_receive(s_dev_handle, buf, len, pdMS_TO_TICKS(100));
}

/* =========================================================
 * INICIALIZAÇÃO
 * ========================================================= */
esp_err_t hdc1080_init(void)
{
    if (s_initialized) return ESP_OK;

    /* Inicializa barramento compartilhado (idempotente) */
    esp_err_t ret = i2c_bus_init();
    if (ret != ESP_OK) return ret;

    /* Adiciona dispositivo HDC1080 no barramento */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = HDC1080_I2C_ADDR,
        .scl_speed_hz    = 100000,   /* 100 kHz — limite do HDC1080 */
    };

    ret = i2c_master_bus_add_device(i2c_bus_get_handle(), &dev_cfg, &s_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device: %s", esp_err_to_name(ret));
        return ret;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "[SENSOR] Environment sensor initialized (HDC1080 addr=0x%02X)", HDC1080_I2C_ADDR);
    return ESP_OK;
}

/* =========================================================
 * LEITURA
 * ========================================================= */
esp_err_t hdc1080_read(hdc1080_reading_t *out)
{
    if (!s_initialized || out == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    out->valid = false;
    uint8_t raw[2];
    esp_err_t ret;

    /* --- Temperatura --- */
    ret = i2c_write_byte(HDC1080_REG_TEMPERATURE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao apontar registrador de temperatura: %s",
                 esp_err_to_name(ret));
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(HDC1080_CONVERSION_DELAY_MS));

    ret = i2c_read_bytes(raw, 2);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao ler temperatura: %s", esp_err_to_name(ret));
        return ret;
    }
    uint16_t raw_temp = ((uint16_t)raw[0] << 8) | raw[1];
    out->temperature_celsius = ((float)raw_temp / 65536.0f) * 165.0f - 40.0f;

    /* --- Umidade --- */
    ret = i2c_write_byte(HDC1080_REG_HUMIDITY);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(HDC1080_CONVERSION_DELAY_MS));

    ret = i2c_read_bytes(raw, 2);
    if (ret != ESP_OK) return ret;
    uint16_t raw_hum = ((uint16_t)raw[0] << 8) | raw[1];
    out->humidity_percent = ((float)raw_hum / 65536.0f) * 100.0f;

    out->valid = true;
    ESP_LOGD(TAG, "Leitura: T=%.1f°C H=%.1f%%",
             out->temperature_celsius, out->humidity_percent);
    return ESP_OK;
}
