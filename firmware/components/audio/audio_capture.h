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
#ifndef AUDIO_BUFFER_BYTES
#define AUDIO_BUFFER_BYTES      (AUDIO_SAMPLE_RATE_HZ * AUDIO_MAX_DURATION_S * \
                                 (AUDIO_BITS / 8) * AUDIO_CHANNELS)
#endif

typedef struct {
    void    *data;          /**< Buffer alocado em heap (malloc/PSRAM) */
    size_t   size_bytes;    /**< Bytes gravados */
    uint32_t sample_rate;   /**< Taxa de amostragem em Hz */
    uint8_t  bit_width;     /**< Bits por amostra (ex: 16) */
    uint8_t  channels;      /**< Número de canais (1=mono, 2=stereo) */
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
