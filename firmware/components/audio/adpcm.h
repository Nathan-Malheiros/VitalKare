/**
 * @file adpcm.h
 * @brief Codificador IMA ADPCM — 4:1 compressão para áudio 16-bit mono
 *
 * Converte amostras PCM de 16 bits (int16_t) para fluxo ADPCM de 4 bits
 * (nibble por amostra = 2 amostras por byte), reduzindo 320 KB de áudio
 * bruto (10s @ 16kHz/16bit) para ~80 KB.
 *
 * Padrão: IMA ADPCM (conforme Microsoft Multimedia Standards Update)
 * Thread-safety: cada chamada usa seu próprio `adpcm_state_t` — seguro.
 */

#ifndef VITALKARE_ADPCM_H
#define VITALKARE_ADPCM_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================
 * ESTADO DO CODIFICADOR
 * ========================================================= */
typedef struct {
    int32_t  predicted;     /**< Valor previsto (amostra predicada) */
    int      step_index;    /**< Índice na tabela de passos (0..88) */
} adpcm_state_t;

/* =========================================================
 * API
 * ========================================================= */

/** @brief Reinicia o estado do codificador para o início de uma sessão. */
void adpcm_encoder_reset(adpcm_state_t *state);

/**
 * @brief Codifica amostras PCM para IMA ADPCM.
 *
 * @param pcm_in       Buffer de entrada com amostras int16_t (little-endian).
 * @param pcm_samples  Número de amostras de entrada.
 * @param adpcm_out    Buffer de saída (deve ter pelo menos pcm_samples/2 bytes).
 * @param state        Estado do codificador (persistente entre chamadas).
 * @return Número de bytes escritos em adpcm_out.
 *
 * @note pcm_samples deve ser par; valores ímpares são truncados.
 */
size_t adpcm_encode(const int16_t *pcm_in, size_t pcm_samples,
                    uint8_t *adpcm_out, adpcm_state_t *state);

/**
 * @brief Retorna o tamanho do buffer ADPCM necessário para N amostras PCM.
 * @param pcm_samples Número de amostras PCM de entrada.
 * @return Bytes necessários no buffer de saída.
 */
static inline size_t adpcm_output_size(size_t pcm_samples)
{
    return (pcm_samples + 1) / 2;
}

#ifdef __cplusplus
}
#endif

#endif /* VITALKARE_ADPCM_H */
