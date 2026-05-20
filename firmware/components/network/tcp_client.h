/**
 * @file tcp_client.h
 * @brief Cliente TCP persistente — VitalKare Wire Protocol
 *
 * ARQUITETURA — TX/RX SEPARADOS:
 *   FSM de conexão: DISCONNECTED → CONNECTING → CONNECTED → HANDSHAKE → ACTIVE
 *   Reconexão automática com backoff exponencial via esp_timer.
 *
 *   task_tcp_tx  — dono do socket fd, conexão FSM, heartbeat, envio de filas.
 *   task_tcp_rx  — leitura pura: recv() → ring buffer → vk_frame_parse → roteamento.
 *
 *   RX nunca bloqueia TX: tasks independentes em cores separados se disponível.
 *   TX notifica RX quando o socket fica ACTIVE (bit TCP_SOCKREADY_BIT).
 *   RX notifica TX quando recv() retorna erro (bit TCP_RX_ERROR_BIT).
 *
 * FILAS DE SAÍDA:
 *   - s_critical_queue (APP_TCP_CRITICAL_QUEUE_DEPTH) — PKT_EVENT_EMERGENCY, ACK
 *   - s_normal_queue   (APP_TCP_NORMAL_QUEUE_DEPTH)   — telemetria, áudio
 *   Ambas consumidas exclusivamente por task_tcp_tx.
 *
 * THREAD SAFETY:
 *   tcp_client_send_critical / tcp_client_send_normal: thread-safe, qualquer task.
 *   tcp_client_connect / disconnect: thread-safe, qualquer task.
 *   O fd do socket nunca é exposto fora deste módulo.
 */

#ifndef VITALKARE_TCP_CLIENT_H
#define VITALKARE_TCP_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "vk_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================
 * ESTADOS DA FSM
 * ========================================================= */
typedef enum {
    TCP_STATE_DISCONNECTED = 0,
    TCP_STATE_CONNECTING,
    TCP_STATE_CONNECTED,
    TCP_STATE_HANDSHAKE,
    TCP_STATE_ACTIVE,
    TCP_STATE_COUNT,            /**< Sentinel — used for FSM range validation */
} tcp_conn_state_t;

/* =========================================================
 * FRAME ENFILEIRADO — item nas filas de saída
 * =========================================================
 * CORRIGIDO: o valor anterior (VK_HEADER_SIZE + 256 + VK_CRC_SIZE = 267 bytes)
 * era menor que um frame de áudio ADPCM típico:
 *   VK_HEADER(9) + pkt_audio_chunk_header(8) + ADPCM(2048) + CRC(2) = 2067 bytes
 * Resultado: enqueue_frame() rejeitava silenciosamente TODOS os chunks de áudio
 * com "Frame muito grande para fila" → nenhum áudio chegava ao servidor.
 *
 * Novo tamanho: 2176 bytes acomoda o maior frame regular do sistema (áudio ADPCM).
 * Custo: normal_queue(32) + critical_queue(8) = 40 × 2178 bytes ≈ 87 KB de heap,
 * aceitável para o ESP32-S3 com 512 KB de SRAM interna.
 * ========================================================= */
#define TCP_FRAME_BUF_SIZE  2176u

typedef struct {
    uint8_t  frame[TCP_FRAME_BUF_SIZE];
    uint16_t frame_len;
} tcp_queued_frame_t;

/* =========================================================
 * API PÚBLICA
 * ========================================================= */

/** @brief Inicializa filas, event group e estruturas internas. */
esp_err_t tcp_client_init(void);

/** @brief Solicita conexão com o servidor (thread-safe). */
esp_err_t tcp_client_connect(void);

/** @brief Solicita desconexão imediata (thread-safe). */
void tcp_client_disconnect(void);

/** @brief Retorna o estado atual da conexão. */
tcp_conn_state_t tcp_client_get_state(void);

/** @brief true se a conexão está em estado ACTIVE. */
bool tcp_client_is_connected(void);

/**
 * @brief Enfileira um frame VK na fila crítica para envio.
 * Thread-safe; timeout de 5 ms (não bloqueia o chamador por muito tempo).
 * @return true se enfileirado com sucesso.
 */
bool tcp_client_send_critical(const uint8_t *frame, uint16_t len);

/**
 * @brief Enfileira um frame VK na fila normal para envio.
 * Thread-safe; timeout de 20 ms.
 * @return true se enfileirado com sucesso.
 */
bool tcp_client_send_normal(const uint8_t *frame, uint16_t len);

/**
 * @brief Retorna a fila normal (para uso do audio_encoder — evita cópia extra).
 * O chamador deve usar a fila apenas via xQueueSend, nunca recv.
 */
QueueHandle_t tcp_client_get_normal_queue(void);

/** @brief Retorna o próximo número de sequência (incrementa atomicamente). */
uint16_t tcp_client_next_seq(void);

/**
 * @brief Task TX — dono do socket, FSM de conexão, heartbeat, envio de filas.
 * Criada pelo main.c; não chamar diretamente.
 */
void task_tcp_tx(void *arg);

/**
 * @brief Task RX — leitura pura do socket, parsing, roteamento para cmd_processor.
 * Criada pelo main.c; não chamar diretamente.
 * Deve ser criada APÓS task_tcp_tx (usa handles do event group).
 */
void task_tcp_rx(void *arg);

/**
 * @brief Alias legado — internamente chama task_tcp_tx.
 * @deprecated Migrar para task_tcp_tx + task_tcp_rx.
 */
void task_tcp_client(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* VITALKARE_TCP_CLIENT_H */
