/**
 * @file audio_capture.h
 * @brief Captura de áudio PDM via I2S — DMA Streaming com Chunk Pool
 *
 * ARQUITETURA:
 *   Pool de APP_AUDIO_CHUNK_POOL_COUNT buffers pré-alocados na PSRAM.
 *   DMA lê o I2S em chunks de APP_AUDIO_CHUNK_BYTES bytes (≈128ms por chunk).
 *   audio_ctrl_task posta cada chunk na fila do audio_encoder.
 *   O encoder devolve os buffers ao pool após a codificação.
 *
 * FSM COMPLETA:
 *   IDLE ──(audio_ctrl_start)──→ ARMED ──(I2S OK)──→ RECORDING
 *   RECORDING ──(ctrl_stop ou timeout)──→ STOPPING ──(último chunk)──→ IDLE
 *   Qualquer estado ──(erro hardware)──→ ERROR ──(recovery)──→ IDLE
 *
 * PROTEÇÃO CONTRA RACE CONDITION:
 *   audio_ctrl_start() usa atomic_compare_exchange_strong: apenas uma
 *   chamada simultânea pode transicionar IDLE→ARMED, mesmo que dois
 *   ISRs de botão disparem no mesmo tick.
 *
 *   Debounce de 150ms: chamadas subsequentes dentro da janela retornam
 *   ESP_ERR_INVALID_STATE sem alterar o estado da FSM.
 *
 * PROPRIEDADE DE MEMÓRIA:
 *   Buffers alocados em PSRAM no pool_init() — NUNCA desalocados.
 *   Produzidos por: task_audio_ctrl (via pool_acquire)
 *   Consumidos por: task_audio_encoder
 *   Devolvidos por: audio_chunk_pool_release() após encoding
 *
 * GPIO: CLK=GPIO2, DATA=GPIO37, canal I2S_NUM_0 em modo PDM RX
 */

#ifndef VITALKARE_AUDIO_CAPTURE_H
#define VITALKARE_AUDIO_CAPTURE_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/* Aliases de compatibilidade com código legado */
#define AUDIO_MAX_DURATION_S    10
#define AUDIO_SAMPLE_RATE_HZ    16000
#define AUDIO_BITS              16
#define AUDIO_CHANNELS          1
#ifndef AUDIO_BUFFER_BYTES
#define AUDIO_BUFFER_BYTES      (AUDIO_SAMPLE_RATE_HZ * AUDIO_MAX_DURATION_S * \
                                 (AUDIO_BITS / 8) * AUDIO_CHANNELS)
#endif

/* =========================================================
 * ESTADO DA FSM DE GRAVAÇÃO
 * ========================================================= */
typedef enum {
    AUDIO_STATE_IDLE      = 0,  /**< Aguardando trigger */
    AUDIO_STATE_ARMED     = 1,  /**< Start solicitado — I2S sendo habilitado */
    AUDIO_STATE_RECORDING = 2,  /**< DMA ativo, chunks sendo produzidos */
    AUDIO_STATE_STOPPING  = 3,  /**< Stop solicitado — aguardando flush */
    AUDIO_STATE_ERROR     = 4,  /**< Erro de hardware — aguardando recovery */
} audio_state_t;

typedef enum {
    AUDIO_TRIGGER_REQUEST   = 0,    /**< Botão A — solicitação */
    AUDIO_TRIGGER_EMERGENCY = 1,    /**< Botão B — emergência */
} audio_trigger_t;

/* =========================================================
 * API — STREAMING COM CHUNK POOL
 * ========================================================= */

/** @brief Inicializa driver I2S PDM e pool de chunks na PSRAM. */
esp_err_t audio_capture_init(void);

/**
 * @brief Inicia gravação com o trigger informado.
 *
 * Thread-safe; ISR-safe via task notification (não bloqueia ISR).
 * A transição IDLE→ARMED usa atomic_compare_exchange_strong para
 * garantir que apenas um chamador vença a corrida.
 *
 * Debounce: retorna ESP_ERR_INVALID_STATE se chamado dentro de
 * APP_AUDIO_DEBOUNCE_MS após a última transição de estado.
 *
 * @return ESP_OK            se a transição foi aceita
 * @return ESP_ERR_INVALID_STATE se FSM não está em IDLE ou debounce ativo
 */
esp_err_t audio_ctrl_start(audio_trigger_t trigger);

/**
 * @brief Para a gravação antes do timeout.
 *
 * Thread-safe. Válido apenas nos estados ARMED e RECORDING.
 * Nos demais estados retorna ESP_ERR_INVALID_STATE silenciosamente.
 */
esp_err_t audio_ctrl_stop(void);

/** @brief Retorna o estado atual da FSM de gravação (leitura atômica). */
audio_state_t audio_ctrl_get_state(void);

/**
 * @brief Libera um buffer do chunk pool de volta para reutilização.
 *
 * PROPRIEDADE: chamado EXCLUSIVAMENTE pelo task_audio_encoder após
 * a codificação do chunk. Não chamar de outros contextos.
 */
void audio_chunk_pool_release(int16_t *buf);

/**
 * @brief Retorna número de buffers livres no chunk pool.
 *
 * Thread-safe. Usado por memory_monitor e system_manager para medir
 * pressão no pipeline de áudio. Valor 0 = pool esgotado.
 * Valor ≤ 2 = backpressure — encoder está lento.
 */
uint8_t audio_chunk_pool_available(void);

/**
 * @brief Retorna o handle da fila do pool (para registro no memory_monitor).
 * Chamar durante init, antes de criar tasks.
 */
QueueHandle_t audio_chunk_pool_get_queue(void);

/** @brief Task de controle de áudio — não chamar diretamente. */
void task_audio_ctrl(void *arg);

/* =========================================================
 * API LEGADA — mantida para compatibilidade durante migração
 * ========================================================= */
typedef struct {
    void    *data;
    size_t   size_bytes;
    uint32_t sample_rate;
    uint8_t  bit_width;
    uint8_t  channels;
} audio_recording_t;

/** @deprecated Use audio_ctrl_start(). */
esp_err_t audio_capture_start(void);

/** @deprecated Use audio_ctrl_stop(). */
esp_err_t audio_capture_stop(audio_recording_t *out);

/** @deprecated Libera buffer retornado pela API legada. */
void audio_capture_free(audio_recording_t *rec);

/** @deprecated Use task_audio_ctrl(). */
void task_audio_capture(void *arg);

#endif /* VITALKARE_AUDIO_CAPTURE_H */
