/**
 * @file audio_capture.c
 * @brief Captura de áudio PDM via I2S (ESP-IDF 5.x nova API i2s_pdm)
 *
 * Microfone INMP441/MP34DT01 ou similar.
 * CLK=GPIO2, DATA=GPIO37, mono, 16kHz, 16-bit.
 * Buffer alocado na PSRAM para não comprimir a heap interna.
 */

#include "audio_capture.h"
#include "driver/i2s_pdm.h"
#include "driver/i2s_common.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "../../config/hardware_config.h"
#include "../../config/app_config.h"
#include "../mqtt/vitalkare_mqtt.h"
#include <string.h>

static const char *TAG = "AUDIO";

/* =========================================================
 * ESTADO
 * ========================================================= */
static i2s_chan_handle_t  s_rx_chan     = NULL;
static bool               s_initialized = false;
static bool               s_recording   = false;
static SemaphoreHandle_t  s_mutex       = NULL;

/* =========================================================
 * INICIALIZAÇÃO
 * ========================================================= */
esp_err_t audio_capture_init(void)
{
    if (s_initialized) return ESP_OK;

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    /* Canal RX */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(HW_I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;

    esp_err_t ret = i2s_new_channel(&chan_cfg, NULL, &s_rx_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Configuração PDM RX */
    i2s_pdm_rx_config_t pdm_cfg = {
        .clk_cfg  = I2S_PDM_RX_CLK_DEFAULT_CONFIG(HW_AUDIO_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                     I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk  = HW_AUDIO_CLK_GPIO,
            .din  = HW_AUDIO_DATA_GPIO,
            .invert_flags = {
                .clk_inv = false,
            },
        },
    };

    ret = i2s_channel_init_pdm_rx_mode(s_rx_chan, &pdm_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_pdm_rx_mode: %s", esp_err_to_name(ret));
        i2s_del_channel(s_rx_chan);
        s_rx_chan = NULL;
        return ret;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Áudio PDM inicializado (%d Hz, CLK=%d, DATA=%d)",
             HW_AUDIO_SAMPLE_RATE_HZ, HW_AUDIO_CLK_GPIO, HW_AUDIO_DATA_GPIO);
    return ESP_OK;
}

/* =========================================================
 * START / STOP
 * ========================================================= */
esp_err_t audio_capture_start(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (s_recording)    return ESP_OK;

    esp_err_t ret = i2s_channel_enable(s_rx_chan);
    if (ret == ESP_OK) s_recording = true;
    return ret;
}

esp_err_t audio_capture_stop(audio_recording_t *out)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (!s_recording) {
        if (out) { out->data = NULL; out->size_bytes = 0; }
        return ESP_OK;
    }

    if (out) {
        size_t buf_size = AUDIO_BUFFER_BYTES;
        void *buf = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!buf) buf = malloc(buf_size);  /* fallback para RAM interna */

        size_t bytes_read = 0;
        if (buf) {
            i2s_channel_read(s_rx_chan, buf, buf_size, &bytes_read, pdMS_TO_TICKS(2000));
        }
        out->data        = buf;
        out->size_bytes  = bytes_read;
        out->sample_rate = HW_AUDIO_SAMPLE_RATE_HZ;
        out->bit_width   = 16;
        out->channels    = 1;
    }

    i2s_channel_disable(s_rx_chan);
    s_recording = false;
    return ESP_OK;
}

void audio_capture_free(audio_recording_t *rec)
{
    if (rec && rec->data) {
        free(rec->data);
        rec->data       = NULL;
        rec->size_bytes = 0;
    }
}

/* =========================================================
 * TASK
 * ========================================================= */
void task_audio_capture(void *arg)
{
    ESP_LOGI(TAG, "Task audio_capture aguardando trigger");

    for (;;) {
        /* Fica suspensa até ser notificada por on_request_event() */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        ESP_LOGI(TAG, "Gravando %d ms de áudio...", APP_AUDIO_RECORD_MS);

        audio_recording_t rec = {0};
        audio_capture_start();
        vTaskDelay(pdMS_TO_TICKS(APP_AUDIO_RECORD_MS));
        audio_capture_stop(&rec);

        if (rec.data && rec.size_bytes > 0) {
            ESP_LOGI(TAG, "Gravados %zu bytes — publicando via MQTT",
                     rec.size_bytes);
            vitalkare_mqtt_publish_audio(&rec);
        }

        audio_capture_free(&rec);
    }
}
