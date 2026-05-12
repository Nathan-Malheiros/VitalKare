/**
 * @file camera_driver.h
 * @brief Driver câmera OV5640 — Fase 5
 *
 * Interface definida para estruturação do projeto.
 * Implementação usa esp32-camera component.
 */

#ifndef VITALKARE_CAMERA_DRIVER_H
#define VITALKARE_CAMERA_DRIVER_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t *jpeg_buf;
    uint32_t jpeg_len;
} camera_frame_t;

esp_err_t     camera_driver_init(void);
esp_err_t     camera_capture_jpeg(camera_frame_t *out);
void          camera_frame_free(camera_frame_t *frame);
bool          camera_is_streaming(void);
esp_err_t     camera_stream_start(void);
void          camera_stream_stop(void);

/** @brief Task de stream — não chamar diretamente. */
void          task_camera_stream(void *arg);

#endif /* VITALKARE_CAMERA_DRIVER_H */
