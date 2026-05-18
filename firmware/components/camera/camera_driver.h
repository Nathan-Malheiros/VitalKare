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
    void    *fb;            /**< Ponteiro opaco para camera_fb_t (esp32-camera) */
    uint8_t *data;          /**< Dados do frame (JPEG) */
    size_t   size_bytes;    /**< Tamanho dos dados */
    uint32_t width;         /**< Largura em pixels */
    uint32_t height;        /**< Altura em pixels */
    bool     is_jpeg;       /**< true se o frame está em formato JPEG */
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
