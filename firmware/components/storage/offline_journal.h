/**
 * @file offline_journal.h
 * @brief Journal binário append-only para eventos offline (protocolo VK)
 *
 * ARQUITETURA:
 *   Armazena frames VK completos no SD card quando o TCP está desconectado.
 *   Format de cada registro:
 *     [CRC16 2B LE][type 1B][timestamp_ms 4B LE][payload_len 2B LE][payload]
 *   CRC é calculado sobre todos os campos EXCETO o próprio CRC.
 *   CRC é escrito por ÚLTIMO → record incompleto (queda de energia) detectável.
 *
 *   Arquivo: /sdcard/vk_journal.bin (append-only, nunca truncado durante escrita)
 *   Cursor de replay: NVS key "jrn_cursor" (uint32 = byte offset)
 *
 *   Replay:
 *     Ao reconectar (EVT_TCP_SYNCED), offline_journal_replay() lê do cursor
 *     até EOF, re-enfileira cada record via vk_reliability_send(GUARANTEED).
 *     Ao completar, atualiza cursor = EOF.
 *
 *   Purge:
 *     offline_journal_purge() apaga o arquivo e reseta cursor.
 *     Chamar somente após replay bem-sucedido confirmado pelo backend.
 *
 * CONCORRÊNCIA:
 *   - offline_journal_append(): mutex interno; NÃO chamar de ISR.
 *   - offline_journal_replay(): chamado apenas pelo task de sync (exclusivo).
 *   - offline_journal_purge(): chamar após confirmação de sync.
 *   - Escritas e leituras são exclusivas via s_mutex.
 *
 * PROPRIEDADE DE MEMÓRIA:
 *   Buffer de replay de 512B na stack (tamanho máximo de payload suportado).
 *   NVS handle alocado pelo ESP-IDF (não liberado — ciclo de vida = processo).
 *   Nenhuma alocação dinâmica.
 *
 * MODO DE FALHA:
 *   SD não montado → ESP_ERR_INVALID_STATE.
 *   Record corrompido (CRC inválido) → skipped, cursor avança.
 *   Espaço esgotado → ESP_ERR_NO_MEM; sistema chama system_manager_report_fault.
 */

#ifndef VITALKARE_OFFLINE_JOURNAL_H
#define VITALKARE_OFFLINE_JOURNAL_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* =========================================================
 * CONFIGURAÇÃO
 * ========================================================= */
#define OFFLINE_JOURNAL_PATH        "/sdcard/vk_journal.bin"
#define OFFLINE_JOURNAL_NVS_NS      "vk_jrn"
#define OFFLINE_JOURNAL_NVS_CURSOR  "cursor"
#define OFFLINE_JOURNAL_MAX_PAYLOAD 512U    /**< Max payload por record */
#define OFFLINE_JOURNAL_MAGIC       0xA55AU /**< Sanity check no header do arquivo */

/* =========================================================
 * TIPOS DE RECORD
 * ========================================================= */
typedef enum {
    JRN_TYPE_EVENT_REQUEST   = 0x10,  /**< PKT_EVENT_REQUEST (Botão A) */
    JRN_TYPE_EVENT_EMERGENCY = 0x11,  /**< PKT_EVENT_EMERGENCY (Botão B) */
    JRN_TYPE_TELEMETRY_ENV   = 0x30,  /**< PKT_TELEMETRY_ENV */
    JRN_TYPE_TELEMETRY_OXI   = 0x32,  /**< PKT_TELEMETRY_OXIMETER */
    JRN_TYPE_AUDIO_MARKER    = 0x20,  /**< Marcador: sessão de áudio foi gravada offline */
} journal_record_type_t;

/* =========================================================
 * API PÚBLICA
 * ========================================================= */

/**
 * @brief Inicializa o módulo: abre NVS, cria mutex, lê cursor.
 * Chamar após sd_manager_init() e nvs_flash_init().
 */
esp_err_t offline_journal_init(void);

/**
 * @brief Appends um frame VK serializado ao journal.
 *
 * Escreve o record com CRC no final para detecção de truncamento.
 * Thread-safe. Não-ISR.
 *
 * @param type       Tipo do record (journal_record_type_t).
 * @param payload    Ponteiro para os dados (pode ser NULL se len==0).
 * @param len        Tamanho do payload (max OFFLINE_JOURNAL_MAX_PAYLOAD).
 * @return ESP_OK, ESP_ERR_INVALID_STATE (SD não montado), ESP_ERR_NO_MEM (espaço).
 */
esp_err_t offline_journal_append(journal_record_type_t type,
                                  const uint8_t *payload, uint16_t len);

/**
 * @brief Replay de todos os records não sincronizados.
 *
 * Lê do cursor atual até EOF. Para cada record:
 *   - CRC OK → chama cb(type, payload, len)
 *   - CRC inválido → skipa (log de aviso), avança cursor
 * Após completar, salva cursor = EOF no NVS.
 *
 * @param cb  Callback chamado para cada record válido.
 *            Deve retornar true para continuar, false para abortar replay.
 * @param arg Argumento passado ao callback.
 * @return Número de records reproduzidos (≥ 0), ou negativo em erro.
 */
typedef bool (*journal_record_cb_t)(journal_record_type_t type,
                                     const uint8_t *payload, uint16_t len,
                                     void *arg);
int offline_journal_replay(journal_record_cb_t cb, void *arg);

/**
 * @brief Retorna número de bytes pendentes (após cursor até EOF).
 * Não é o número exato de records — use como estimativa de backlog.
 */
uint32_t offline_journal_pending_bytes(void);

/**
 * @brief Apaga o arquivo de journal e reseta cursor no NVS.
 * Chamar somente após replay confirmado pelo backend.
 * Thread-safe.
 */
esp_err_t offline_journal_purge(void);

/** @brief Retorna true se o módulo foi inicializado com sucesso. */
bool offline_journal_is_available(void);

#endif /* VITALKARE_OFFLINE_JOURNAL_H */
