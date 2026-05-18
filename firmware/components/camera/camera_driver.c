/**
 * @file camera_driver.c
 * @brief Driver OV5640 via esp32-camera (esp-idf managed component)
 *
 * Depende do componente espressif/esp32-camera.
 * Adicione firmware/idf_component.yml com:
 *   dependencies:
 *     espressif/esp32-camera: ">=2.0.0"
 */

#include "camera_driver.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "vitalkare_http.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "../../config/hardware_config.h"
#include "../../config/app_config.h"

static const char *TAG = "CAMERA";

static bool s_initialized = false;

/* =========================================================
 * INICIALIZAÇÃO
 * ========================================================= */
esp_err_t camera_driver_init(void)
{
    if (s_initialized) return ESP_OK;

    /* Nota: não criamos o barramento I2C manualmente aqui.
     * O componente esp32-camera gerencia o SCCB internamente via pin_sccb_sda/scl.
     * Criar i2c_new_master_bus() antes causaria "I2C bus id already acquired".
     */
    esp_err_t ret;

    camera_config_t cfg = {
        .pin_pwdn    = -1,
        .pin_reset   = -1,
        .pin_xclk    = HW_CAM_XCLK_GPIO,
        .pin_sccb_sda = HW_CAM_SIOD_GPIO,
        .pin_sccb_scl = HW_CAM_SIOC_GPIO,

        .pin_d7      = HW_CAM_Y9_GPIO,
        .pin_d6      = HW_CAM_Y8_GPIO,
        .pin_d5      = HW_CAM_Y7_GPIO,
        .pin_d4      = HW_CAM_Y6_GPIO,
        .pin_d3      = HW_CAM_Y5_GPIO,
        .pin_d2      = HW_CAM_Y4_GPIO,
        .pin_d1      = HW_CAM_Y3_GPIO,
        .pin_d0      = HW_CAM_Y2_GPIO,

        .pin_vsync   = HW_CAM_VSYNC_GPIO,
        .pin_href    = HW_CAM_HREF_GPIO,
        .pin_pclk    = HW_CAM_PCLK_GPIO,

        .xclk_freq_hz = HW_CAM_XCLK_FREQ_HZ,
        .ledc_timer   = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,

        .pixel_format = PIXFORMAT_JPEG,
        .frame_size   = FRAMESIZE_VGA,   /* 640×480 */
        .jpeg_quality = HW_CAM_JPEG_QUALITY,
        .fb_count     = 2,               /* double buffering */
        .fb_location  = CAMERA_FB_IN_PSRAM,
        .grab_mode    = CAMERA_GRAB_WHEN_EMPTY,
    };

    ret = esp_camera_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "esp_camera_init: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Configurações do sensor OV5640 */
    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor) {
        sensor->set_brightness(sensor, 0);
        sensor->set_contrast(sensor, 0);
        sensor->set_saturation(sensor, 0);
        sensor->set_whitebal(sensor, 1);    /* auto white balance */
        sensor->set_exposure_ctrl(sensor, 1); /* auto exposure */
        sensor->set_gain_ctrl(sensor, 1);   /* auto gain */
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Camera OV5640 inicializada (VGA JPEG, PSRAM framebuffer)");
    return ESP_OK;
}

/* =========================================================
 * CAPTURA
 * ========================================================= */
esp_err_t camera_capture_jpeg(camera_frame_t *out)
{
    if (!s_initialized || out == NULL) return ESP_ERR_INVALID_STATE;

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Falha ao capturar frame");
        return ESP_FAIL;
    }

    if (fb->format != PIXFORMAT_JPEG) {
        ESP_LOGW(TAG, "Frame não é JPEG (format=%d)", fb->format);
        esp_camera_fb_return(fb);
        return ESP_ERR_NOT_SUPPORTED;
    }

    out->fb          = fb;
    out->data        = fb->buf;
    out->size_bytes  = fb->len;
    out->width       = fb->width;
    out->height      = fb->height;
    out->is_jpeg     = true;

    ESP_LOGD(TAG, "Frame capturado: %dx%d, %zu bytes",
             fb->width, fb->height, fb->len);
    return ESP_OK;
}

void camera_frame_free(camera_frame_t *frame)
{
    if (frame && frame->fb) {
        esp_camera_fb_return((camera_fb_t *)frame->fb);
        frame->fb   = NULL;
        frame->data = NULL;
    }
}

/* =========================================================
 * TASK DE STREAMING
 * ========================================================= */
void task_camera_stream(void *arg)
{
    ESP_LOGI(TAG, "Task camera_stream iniciada (%d ms/frame)",
             APP_CAMERA_FRAME_INTERVAL_MS);

    for (;;) {
        /* Captura frame a cada APP_CAMERA_FRAME_INTERVAL_MS */
        camera_frame_t frame = {0};
        esp_err_t ret = camera_capture_jpeg(&frame);
        if (ret == ESP_OK) {
            /* Serializa frame JPEG como binário via HTTP */
            char endpoint[96];
            snprintf(endpoint, sizeof(endpoint),
                     "/api/v1/devices/%s/image", APP_DEFAULT_DEVICE_ID);
            vitalkare_http_post_binary(endpoint,
                                       frame.data,
                                       frame.size_bytes,
                                       "image/jpeg");
            camera_frame_free(&frame);
        } else {
            ESP_LOGW(TAG, "Captura falhou: %s", esp_err_to_name(ret));
        }
        vTaskDelay(pdMS_TO_TICKS(APP_CAMERA_FRAME_INTERVAL_MS));
    }
}
