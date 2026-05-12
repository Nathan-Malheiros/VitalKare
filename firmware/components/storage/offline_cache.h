/**
 * @file offline_cache.h
 * @brief Cache local de eventos para modo offline
 *
 * Quando o dispositivo perde conectividade MQTT, todos os eventos
 * são enfileirados aqui. Ao reconectar, o sync é realizado em ordem
 * cronológica com os eventos P1 marcados como offline_delayed.
 */

#ifndef VITALKARE_OFFLINE_CACHE_H
#define VITALKARE_OFFLINE_CACHE_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#define OFFLINE_CACHE_MAX_EVENTS    200
#define OFFLINE_CACHE_PAYLOAD_LEN   512

typedef struct {
    uint32_t timestamp_ms;
    char     topic[128];
    char     payload[OFFLINE_CACHE_PAYLOAD_LEN];
    int      qos;
    uint8_t  priority;
    bool     synced;
} offline_event_t;

esp_err_t offline_cache_init(void);

/** @brief Adiciona evento à cache offline (persiste no SD). */
esp_err_t offline_cache_push(const offline_event_t *event);

/** @brief Retorna número de eventos não sincronizados. */
uint32_t  offline_cache_pending_count(void);

/**
 * @brief Itera sobre eventos pendentes para sincronização.
 * @param cb Callback chamado para cada evento. Retorne true para marcar como sincronizado.
 */
typedef bool (*offline_sync_cb_t)(const offline_event_t *event);
esp_err_t offline_cache_sync(offline_sync_cb_t cb);

/** @brief Remove eventos já sincronizados do arquivo. */
esp_err_t offline_cache_compact(void);

#endif /* VITALKARE_OFFLINE_CACHE_H */
