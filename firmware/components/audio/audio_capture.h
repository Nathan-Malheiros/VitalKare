/**
 * @file audio_capture.h
 * @brief Captura de áudio PDM via I2S — Fase 4
 *
 * Implementação futura: microfone PDM GPIO 2 (CLK) / 37 (DATA).
 * Interface definida para estruturação do projeto.
 */

#ifndef VITALKARE_AUDIO_CAPTURE_H
#define VITALKARE_AUDIO_CAPTURE_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#define AUDIO_MAX_DURATION_S    10
#define AUDIO_SAMPLE_RATE_HZ    16000
#define AUDIO_BITS              16
#define AUDIO_CHANNELS          1
#define AUDIO_BUFFER_BYTES      (AUDIO_SAMPLE_RATE_HZ * AUDIO_MAX_DURATION_S * \
                                 (AUDIO_BITS / 8) * AUDIO_CHANNELS)

typedef struct {
    uint8_t *buffer;        /**< Buffer alocado em heap */
    uint32_t size_bytes;    /**< Bytes gravados */
    uint32_t duration_ms;   /**< Duração real da gravação */
    bool     valid;
} audio_recording_t;

/** @brief Inicializa driver I2S PDM. */
esp_err_t audio_capture_init(void);

/** @brief Inicia gravação (não-bloqueante). */
esp_err_t audio_capture_start(void);

/** @brief Para gravação e retorna buffer preenchido. */
esp_err_t audio_capture_stop(audio_recording_t *out);

/** @brief Libera buffer de gravação. */
void audio_capture_free(audio_recording_t *rec);

/** @brief Task de captura — não chamar diretamente. */
void task_audio_capture(void *arg);

#endif /* VITALKARE_AUDIO_CAPTURE_H */
