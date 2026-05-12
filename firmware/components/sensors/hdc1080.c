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
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "../../config/hardware_config.h"
#include <math.h>

static const char *TAG = "HDC1080";

/* Registradores */
#define HDC1080_REG_TEMPERATURE     0x00
#define HDC1080_REG_HUMIDITY        0x01
#define HDC1080_REG_CONFIG          0x02
#define HDC1080_REG_MANUFACTURER_ID 0xFE
#define HDC1080_MANUFACTURER_ID     0x5449  /**< "TI" */
#define HDC1080_CONVERSION_DELAY_MS 20

static bool s_initialized = false;

/* =========================================================
 * HELPERS I2C
 * ========================================================= */
static esp_err_t i2c_write_byte(uint8_t reg)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (HW_HDC1080_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(HW_HDC1080_I2C_PORT, cmd,
                                          pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t i2c_read_bytes(uint8_t *buf, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (HW_HDC1080_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, buf, len, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(HW_HDC1080_I2C_PORT, cmd,
                                          pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

/* =========================================================
 * INICIALIZAÇÃO
 * ========================================================= */
esp_err_t hdc1080_init(void)
{
    /* Configura I2C master (compartilhado — apenas se não inicializado) */
    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = HW_OLED_SDA_GPIO,
        .scl_io_num       = HW_OLED_SCL_GPIO,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = HW_HDC1080_I2C_FREQ_HZ,
    };

    esp_err_t ret = i2c_param_config(HW_HDC1080_I2C_PORT, &conf);
    if (ret != ESP_OK) return ret;

    ret = i2c_driver_install(HW_HDC1080_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        /* ESP_ERR_INVALID_STATE → driver já instalado, OK */
        return ret;
    }

    /* Verifica ID do fabricante */
    /* TODO: Implementar leitura do registrador 0xFE para validação */

    s_initialized = true;
    ESP_LOGI(TAG, "HDC1080 inicializado (I2C addr=0x%02X)", HW_HDC1080_I2C_ADDR);
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
