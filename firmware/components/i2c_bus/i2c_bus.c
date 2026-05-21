/**
 * @file i2c_bus.c
 * @brief Barramento I2C master compartilhado — ESP-IDF v6
 *
 * Inicializa I2C_NUM_0 (SDA=GPIO14, SCL=GPIO21) usando a nova API
 * driver/i2c_master.h. Inicialização é thread-safe e acontece
 * exatamente uma vez independente de quantos drivers chamem i2c_bus_init().
 */

#include "i2c_bus.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "../../config/hardware_config.h"

static const char *TAG = "I2C_BUS";

/* GPIO do barramento compartilhado (OLED, HDC1080, MPU6050) */
#define I2C_BUS_SDA_GPIO    HW_OLED_SDA_GPIO
#define I2C_BUS_SCL_GPIO    HW_OLED_SCL_GPIO
#define I2C_BUS_PORT        I2C_NUM_0

static i2c_master_bus_handle_t s_bus_handle  = NULL;
static bool                    s_initialized = false;
static SemaphoreHandle_t       s_mutex       = NULL;

/* Mutex criado antes do scheduler via portMUX estático */
static StaticSemaphore_t       s_mutex_buf;

esp_err_t i2c_bus_init(void)
{
    /* Cria o mutex uma única vez (safe antes do scheduler) */
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_buf);
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (s_initialized) {
        xSemaphoreGive(s_mutex);
        return ESP_OK;
    }

    i2c_master_bus_config_t cfg = {
        .i2c_port            = I2C_BUS_PORT,
        .sda_io_num          = I2C_BUS_SDA_GPIO,
        .scl_io_num          = I2C_BUS_SCL_GPIO,
        .clk_source          = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt   = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t ret = i2c_new_master_bus(&cfg, &s_bus_handle);
    if (ret == ESP_OK) {
        s_initialized = true;
        ESP_LOGI(TAG, "I2C master bus inicializado (port=%d SDA=GPIO%d SCL=GPIO%d)",
                 I2C_BUS_PORT, I2C_BUS_SDA_GPIO, I2C_BUS_SCL_GPIO);
    } else {
        ESP_LOGE(TAG, "i2c_new_master_bus falhou: %s", esp_err_to_name(ret));
    }

    xSemaphoreGive(s_mutex);
    return ret;
}

i2c_master_bus_handle_t i2c_bus_get_handle(void)
{
    return s_bus_handle;
}
