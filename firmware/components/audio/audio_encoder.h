/**
 * @file audio_encoder.h
 * @brief Task de encoding ADPCM — VitalKare Firmware
 *
 * Consome chunks PCM da fila audio_chunk_queue, aplica IMA ADPCM,
 * monta pacotes VK e publica na fila normal do tcp_client.
 *
 * Pipeline de áudio:
 *   [DMA I2S] → audio_ctrl_task → audio_chunk_queue
 *              → task_audio_encoder → tcp_normal_queue → tcp_client
 */

#ifndef VITALKARE_AUDIO_ENCODER_H
#define VITALKARE_AUDIO_ENCODER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================
 * ESTRUTURA DE CHUNK DE ÁUDIO (PCM)
 * ========================================================= */
typedef struct {
    uint32_t session_id;        /**< ID da sessão (novo por gravação) */
    uint16_t chunk_index;       /**< Índice sequencial dentro da sessão */
    uint16_t pcm_samples;       /**< Número de amostras int16_t no buffer */
    int16_t *pcm_data;          /**< Ponteiro para bloco do chunk pool (PSRAM) */
    bool     is_last;           /**< true = último chunk da sessão */
    uint8_t  trigger;           /**< 0=request, 1=emergency */
} audio_chunk_t;

/* =========================================================
 * API
 * ========================================================= */

/**
 * @brief Inicializa a fila de chunks e o estado do encoder.
 * @return ESP_OK em caso de sucesso.
 */
esp_err_t audio_encoder_init(void);

/**
 * @brief Retorna a fila de entrada do encoder (usada pelo audio_ctrl_task).
 * @return QueueHandle da fila de chunks PCM.
 */
QueueHandle_t audio_encoder_get_queue(void);

/**
 * @brief Task FreeRTOS do encoder — não chamar diretamente.
 * Criada em main.c com stack APP_TASK_STACK_AUDIO_ENC.
 */
void task_audio_encoder(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* VITALKARE_AUDIO_ENCODER_H */
