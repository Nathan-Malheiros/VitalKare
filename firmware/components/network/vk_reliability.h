/**
 * @file vk_reliability.h
 * @brief Camada de confiabilidade de pacotes — QoS, retransmissão, buffer de reconexão
 *
 * ARQUITETURA:
 *   O protocolo VK não possui ACK de entrega por pacote (PKT_EVENT_ACK é
 *   confirmação do operador, não de delivery de rede). Portanto a semântica
 *   de confiabilidade é baseada em reconexão:
 *
 *   VK_QOS_FIRE_AND_FORGET:
 *     Enfileira via tcp_client_send_normal(). Se a fila estiver cheia ou
 *     TCP desconectado, o frame é descartado.
 *
 *   VK_QOS_RETRY:
 *     Frame armazenado no pending_table[]. Se tcp_client_send_critical()
 *     retornar false (fila cheia ou desconectado), re-tenta até
 *     VK_RELIABILITY_MAX_RETRIES vezes com intervalo de
 *     VK_RELIABILITY_RETRY_INTERVAL_MS. Após max_retries, descarta e
 *     incrementa dropped_packets.
 *
 *   VK_QOS_GUARANTEED:
 *     Frame nunca descartado. Fica no pending_table[] até ser confirmado.
 *     "Confirmado" = tcp_client_send_critical() retornou true E a conexão
 *     TCP permaneceu ativa. Na reconexão, todos os pending GUARANTEED são
 *     re-enfileirados automaticamente.
 *     Uso: PKT_EVENT_EMERGENCY, PKT_AUDIO_SESSION_START.
 *
 * CONCORRÊNCIA:
 *   - vk_reliability_send(): callable de qualquer task; adquire mutex.
 *   - task_vk_reliability(): scanner periódico; adquire mutex para varrer.
 *   - vk_reliability_on_reconnect(): chamado por event handler de
 *     EVT_TCP_CONNECTED; re-enfileira frames GUARANTEED pendentes.
 *   - NÃO chamar de ISR.
 *
 * PROPRIEDADE DE MEMÓRIA:
 *   Static pending_table[VK_RELIABILITY_TABLE_SIZE] em DRAM.
 *   Cada slot: TCP_FRAME_BUF_SIZE (≈290B) + 20B metadata ≈ 310B.
 *   Tabela total: 8 × 310 = ~2.5KB DRAM.
 *   Nenhuma alocação dinâmica.
 *
 * MODO DE FALHA:
 *   Tabela cheia + novo frame GUARANTEED → task bloqueia yield até slot abrir
 *   (max VK_RELIABILITY_BLOCK_TICKS). Se exceder, frame é logado e descartado.
 *   Tabela cheia + novo RETRY → fire-and-forget + inc dropped_packets.
 */

#ifndef VITALKARE_VK_RELIABILITY_H
#define VITALKARE_VK_RELIABILITY_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "../network/tcp_client.h"

/* =========================================================
 * CONFIGURAÇÃO
 * ========================================================= */
#define VK_RELIABILITY_TABLE_SIZE       8U
#define VK_RELIABILITY_MAX_RETRIES      3U
#define VK_RELIABILITY_RETRY_INTERVAL_MS 2000U
#define VK_RELIABILITY_SCAN_INTERVAL_MS  500U
#define VK_RELIABILITY_BLOCK_TICKS      pdMS_TO_TICKS(200U)

/* =========================================================
 * QoS LEVELS
 * ========================================================= */
typedef enum {
    VK_QOS_FIRE_AND_FORGET = 0,  /**< Sem garantia — descarta se fila cheia */
    VK_QOS_RETRY           = 1,  /**< Retenta MAX_RETRIES vezes */
    VK_QOS_GUARANTEED      = 2,  /**< Retenta indefinidamente; sobrevive reconexão */
} vk_qos_t;

/* =========================================================
 * API PÚBLICA
 * ========================================================= */

/** @brief Inicializa o módulo (mutex, zera tabela). Chamar antes das tasks. */
esp_err_t vk_reliability_init(void);

/**
 * @brief Envia um frame VK com o nível de QoS especificado.
 *
 * Thread-safe. Para VK_QOS_FIRE_AND_FORGET: chamada é não-bloqueante.
 * Para VK_QOS_RETRY/GUARANTEED: pode bloquear brevemente esperando slot
 * na tabela (max VK_RELIABILITY_BLOCK_TICKS).
 *
 * @param frame   Buffer do frame já construído (vk_frame_build).
 * @param len     Tamanho do frame.
 * @param qos     Nível de QoS.
 * @return ESP_OK se enfileirado; ESP_ERR_NO_MEM se tabela cheia e GUARANTEED.
 */
esp_err_t vk_reliability_send(const uint8_t *frame, uint16_t len, vk_qos_t qos);

/**
 * @brief Chamado quando EVT_TCP_CONNECTED chega.
 * Re-enfileira todos os frames VK_QOS_GUARANTEED pendentes.
 * Thread-safe.
 */
void vk_reliability_on_reconnect(void);

/**
 * @brief Chamado quando EVT_TCP_DISCONNECTED chega.
 * Cancela tentativas ativas (frames RETRY não GUARANTEED serão descartados
 * se max_retries for excedido antes da reconexão).
 * Thread-safe.
 */
void vk_reliability_on_disconnect(void);

/** @brief Retorna número de frames pendentes na tabela. Thread-safe. */
uint8_t vk_reliability_pending_count(void);

/** @brief Task FreeRTOS do scanner de retransmissão. */
void task_vk_reliability(void *arg);

#endif /* VITALKARE_VK_RELIABILITY_H */
