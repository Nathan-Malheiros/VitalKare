/**
 * @file ldr.c
 * @brief Leitura do sensor de luminosidade LDR via ADC (ESP-IDF 5.x)
 */

#include "ldr.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "../../config/hardware_config.h"

static const char *TAG = "LDR";

static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t         s_cali_handle = NULL;
static bool                      s_calibrated  = false;
static bool                      s_initialized = false;

esp_err_t ldr_init(void)
{
    if (s_initialized) return ESP_OK;

    /* ---- ADC unit ---- */
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id  = HW_LDR_ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t ret = adc_oneshot_new_unit(&init_cfg, &s_adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit falhou: %s", esp_err_to_name(ret));
        return ret;
    }

    /* ---- Canal ---- */
    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = HW_LDR_ADC_BITS,
        .atten    = HW_LDR_ADC_ATTENUATION,
    };
    ret = adc_oneshot_config_channel(s_adc_handle, HW_LDR_ADC_CHANNEL, &chan_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_config_channel falhou: %s", esp_err_to_name(ret));
        adc_oneshot_del_unit(s_adc_handle);
        s_adc_handle = NULL;
        return ret;
    }

    /* ---- Calibração (Line Fitting) ---- */
    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id  = HW_LDR_ADC_UNIT,
        .atten    = HW_LDR_ADC_ATTENUATION,
        .bitwidth = HW_LDR_ADC_BITS,
    };
    if (adc_cali_create_scheme_line_fitting(&cali_cfg, &s_cali_handle) == ESP_OK) {
        s_calibrated = true;
    } else {
        ESP_LOGW(TAG, "Calibração ADC não disponível — usando conversão raw");
    }

    s_initialized = true;
    ESP_LOGI(TAG, "LDR inicializado (ADC%d CH%d, cal=%s)",
             HW_LDR_ADC_UNIT + 1, HW_LDR_ADC_CHANNEL,
             s_calibrated ? "sim" : "não");
    return ESP_OK;
}

esp_err_t ldr_read(float *out_lux, uint16_t *out_raw)
{
    if (!s_initialized || out_lux == NULL) return ESP_ERR_INVALID_STATE;

    int raw = 0;
    esp_err_t ret = adc_oneshot_read(s_adc_handle, HW_LDR_ADC_CHANNEL, &raw);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "leitura ADC falhou: %s", esp_err_to_name(ret));
        return ret;
    }
    if (out_raw) *out_raw = (uint16_t)raw;

    /* Converte para lux.
     * O LDR forma um divisor resistivo com resistor de pull-up.
     * Com mais luz, a resistência do LDR cai → tensão no pino cai.
     * Aproximação empírica linear: 0 lux ~ ADC máx, 1000 lux ~ ADC mín.
     * Ajuste os coeficientes conforme o LDR e resistor usados. */
    int max_raw = (1 << 12) - 1;  /* 4095 para 12-bit */

    if (s_calibrated && s_cali_handle) {
        int voltage_mv = 0;
        adc_cali_raw_to_voltage(s_cali_handle, raw, &voltage_mv);
        /* 3300mV = 0 lux (escuro), 0mV = 1000 lux (luz intensa) */
        float fraction = 1.0f - (float)voltage_mv / 3300.0f;
        if (fraction < 0.0f) fraction = 0.0f;
        if (fraction > 1.0f) fraction = 1.0f;
        *out_lux = fraction * 1000.0f;
    } else {
        float fraction = 1.0f - (float)raw / (float)max_raw;
        if (fraction < 0.0f) fraction = 0.0f;
        *out_lux = fraction * 1000.0f;
    }

    ESP_LOGD(TAG, "LDR raw=%d lux=%.1f", raw, *out_lux);
    return ESP_OK;
}
