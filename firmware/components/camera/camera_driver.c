/**
 * @file camera_driver.c
 * @brief Driver OV5640 via esp32-camera (esp-idf managed component)
 *
 * Depende do componente espressif/esp32-camera.
 * Adicione firmware/idf_component.yml com:
 *   dependencies:
 *     espressif/esp32-camera: ">=2.0.0"
 *
 * ENVIO DE IMAGEM:
 *   Frames JPEG são enviados sobre TCP usando o VK Protocol.
 *   Como um frame VGA pode ultrapassar TCP_FRAME_BUF_SIZE, o frame é fragmentado
 *   em chunks de CAMERA_CHUNK_PAYLOAD_SIZE bytes:
 *     1. PKT_IMAGE_STREAM_START — metadados (largura, altura, tamanho total)
 *     2. N × PKT_IMAGE_FRAME    — chunks JPEG consecutivos
 *   O backend reassembla pela ordem de chegada (sem índice de chunk no payload).
 *   Imagens são descartadas silenciosamente quando o TCP está offline.
 *
 * NOTA: vitalkare_http.h foi removido — o sistema usa exclusivamente TCP persistente.
 */

#include "camera_driver.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "../../config/hardware_config.h"
#include "../../config/app_config.h"
#include "../network/tcp_client.h"
#include "../network/vk_protocol.h"
#include <string.h>

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
        /* CORRIGIDO: LEDC_TIMER_0/CHANNEL_0 eram compartilhados com o buzzer
         * (HW_BUZZER_PWM_TIMER/CHANNEL em hardware_config.h). Conflito causava
         * corrupção do clock XCLK da câmera ao acionar o buzzer.
         * Câmera usa TIMER_1/CHANNEL_1; buzzer mantém TIMER_0/CHANNEL_0. */
        .ledc_timer   = LEDC_TIMER_1,
        .ledc_channel = LEDC_CHANNEL_1,

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
 * ENVIO DE FRAME JPEG SOBRE TCP (VK Protocol)
 * =========================================================
 * Um frame VGA JPEG pode ter 15-40 KB, o que supera TCP_FRAME_BUF_SIZE.
 * O frame é fragmentado em chunks de CAMERA_CHUNK_PAYLOAD_SIZE bytes cada.
 * ========================================================= */

/* Cabeçalho de metadados enviado antes dos chunks de dados */
#pragma pack(push, 1)
typedef struct {
    uint32_t total_len;     /**< Tamanho total do JPEG em bytes */
    uint16_t width;
    uint16_t height;
} pkt_image_stream_start_t;
#pragma pack(pop)

/* Payload máximo por chunk: deixa espaço para header VK + CRC */
#define CAMERA_CHUNK_PAYLOAD_SIZE  (TCP_FRAME_BUF_SIZE - VK_HEADER_SIZE - VK_CRC_SIZE)

static void send_jpeg_over_tcp(const uint8_t *jpeg_data, size_t jpeg_len,
                                uint16_t width, uint16_t height)
{
    if (!tcp_client_is_connected()) return;

    /* Frame de metadados (PKT_IMAGE_STREAM_START) */
    static uint8_t s_meta_frame[VK_HEADER_SIZE + sizeof(pkt_image_stream_start_t) + VK_CRC_SIZE];
    pkt_image_stream_start_t meta = {
        .total_len = (uint32_t)jpeg_len,
        .width     = width,
        .height    = height,
    };
    int flen = vk_frame_build(s_meta_frame, sizeof(s_meta_frame),
                              PKT_IMAGE_STREAM_START, tcp_client_next_seq(),
                              &meta, sizeof(meta));
    if (flen > 0) {
        tcp_client_send_normal(s_meta_frame, (uint16_t)flen);
    }

    /* Chunks de dados JPEG (PKT_IMAGE_FRAME) */
    static uint8_t s_chunk_frame[TCP_FRAME_BUF_SIZE];
    size_t offset = 0;
    while (offset < jpeg_len) {
        if (!tcp_client_is_connected()) break;   /* Aborta se TCP cair durante envio */

        size_t chunk = jpeg_len - offset;
        if (chunk > CAMERA_CHUNK_PAYLOAD_SIZE) chunk = CAMERA_CHUNK_PAYLOAD_SIZE;

        flen = vk_frame_build(s_chunk_frame, sizeof(s_chunk_frame),
                              PKT_IMAGE_FRAME, tcp_client_next_seq(),
                              jpeg_data + offset, (uint32_t)chunk);
        if (flen > 0) {
            tcp_client_send_normal(s_chunk_frame, (uint16_t)flen);
        }
        offset += chunk;
    }
}

/* =========================================================
 * TASK DE STREAMING
 * ========================================================= */
void task_camera_stream(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "task_camera_stream iniciada (%d ms/frame, chunk=%u B)",
             APP_CAMERA_FRAME_INTERVAL_MS, (unsigned)CAMERA_CHUNK_PAYLOAD_SIZE);

    for (;;) {
        camera_frame_t frame = {0};
        esp_err_t ret = camera_capture_jpeg(&frame);

        if (ret == ESP_OK) {
            /* Envia via TCP apenas quando conectado — descarte silencioso offline */
            if (tcp_client_is_connected()) {
                send_jpeg_over_tcp(frame.data, frame.size_bytes,
                                   (uint16_t)frame.width, (uint16_t)frame.height);
                ESP_LOGD(TAG, "Frame enviado: %dx%d %zu B",
                         frame.width, frame.height, frame.size_bytes);
            }
            camera_frame_free(&frame);
        } else {
            ESP_LOGW(TAG, "Captura falhou: %s", esp_err_to_name(ret));
        }

        vTaskDelay(pdMS_TO_TICKS(APP_CAMERA_FRAME_INTERVAL_MS));
    }
}
