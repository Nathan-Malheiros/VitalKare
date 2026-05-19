/**
 * @file adpcm.c
 * @brief Codificador IMA ADPCM — implementação
 *
 * Tabelas e algoritmo conforme IMA Digital Audio Focus and Technical Working
 * Group ("IMA ADPCM" or "DVI ADPCM"), 1992.
 */

#include "adpcm.h"
#include <string.h>

/* =========================================================
 * TABELAS IMA ADPCM
 * ========================================================= */

/** Tabela de ajuste de step_index por nibble codificado */
static const int8_t s_index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

/** Tabela de tamanhos de passo (88 entradas) */
static const int32_t s_step_table[89] = {
        7,     8,     9,    10,    11,    12,    13,    14,
       16,    17,    19,    21,    23,    25,    28,    31,
       34,    37,    41,    45,    50,    55,    60,    66,
       73,    80,    88,    97,   107,   118,   130,   143,
      157,   173,   190,   209,   230,   253,   279,   307,
      337,   371,   408,   449,   494,   544,   598,   658,
      724,   796,   876,   963,  1060,  1166,  1282,  1411,
     1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,
     3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,
     7132,  7845,  8630,  9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767
};

/* =========================================================
 * CODIFICAÇÃO DE UMA AMOSTRA → NIBBLE
 * ========================================================= */
static inline uint8_t encode_sample(int32_t sample, adpcm_state_t *state)
{
    int32_t step      = s_step_table[state->step_index];
    int32_t diff      = sample - state->predicted;
    uint8_t nibble    = 0;

    if (diff < 0) {
        nibble = 8;
        diff   = -diff;
    }

    int32_t mask = step;
    if (diff >= mask)         { nibble |= 4; diff -= mask; }
    mask >>= 1;
    if (diff >= mask)         { nibble |= 2; diff -= mask; }
    mask >>= 1;
    if (diff >= mask)         { nibble |= 1; }

    /* Recalcula predição com o nibble codificado */
    int32_t new_diff = (step >> 3);
    if (nibble & 4) new_diff += step;
    if (nibble & 2) new_diff += (step >> 1);
    if (nibble & 1) new_diff += (step >> 2);

    if (nibble & 8) {
        state->predicted -= new_diff;
    } else {
        state->predicted += new_diff;
    }

    /* Clamp para int16 */
    if      (state->predicted >  32767) state->predicted =  32767;
    else if (state->predicted < -32768) state->predicted = -32768;

    /* Atualiza step_index */
    state->step_index += s_index_table[nibble];
    if      (state->step_index < 0)  state->step_index = 0;
    else if (state->step_index > 88) state->step_index = 88;

    return (nibble & 0x0F);
}

/* =========================================================
 * API PÚBLICA
 * ========================================================= */
void adpcm_encoder_reset(adpcm_state_t *state)
{
    if (state) {
        state->predicted   = 0;
        state->step_index  = 0;
    }
}

size_t adpcm_encode(const int16_t *pcm_in, size_t pcm_samples,
                    uint8_t *adpcm_out, adpcm_state_t *state)
{
    if (!pcm_in || !adpcm_out || !state || pcm_samples == 0) return 0;

    size_t out_bytes = 0;
    size_t i = 0;

    /* Processa pares de amostras — dois nibbles por byte de saída */
    while (i + 1 < pcm_samples) {
        uint8_t lo = encode_sample((int32_t)pcm_in[i],     state);
        uint8_t hi = encode_sample((int32_t)pcm_in[i + 1], state);
        adpcm_out[out_bytes++] = lo | (hi << 4);
        i += 2;
    }

    /* Amostra residual (pcm_samples ímpar) */
    if (i < pcm_samples) {
        uint8_t lo = encode_sample((int32_t)pcm_in[i], state);
        adpcm_out[out_bytes++] = lo;
    }

    return out_bytes;
}
