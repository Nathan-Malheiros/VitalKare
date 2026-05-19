/**
 * @file core.h
 * @brief Estado global do sistema VitalKare e tipos fundamentais
 *
 * Define a máquina de estados principal, estruturas de dados
 * compartilhadas entre tasks e a interface da camada core.
 */

#ifndef VITALKARE_CORE_H
#define VITALKARE_CORE_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* =========================================================
 * MÁQUINA DE ESTADOS DO SISTEMA
 * ========================================================= */
typedef enum {
    SYS_STATE_BOOT = 0,
    SYS_STATE_WIFI_CONNECTING,
    SYS_STATE_TCP_CONNECTING,       /**< TCP socket em processo de conexão */
    SYS_STATE_HTTP_READY,           /**< Legado — HTTP disponível */
    SYS_STATE_OPERATIONAL,
    SYS_STATE_EMERGENCY,
    SYS_STATE_RECORDING,
    SYS_STATE_RECORDING_OFFLINE,    /**< Gravando sem conectividade (salva no SD) */
    SYS_STATE_SYNCING,              /**< Sincronizando cache offline após reconexão */
    SYS_STATE_OFFLINE,
    SYS_STATE_ERROR,
} sys_state_t;

/* =========================================================
 * STATUS DE CONECTIVIDADE
 * ========================================================= */
typedef struct {
    bool wifi_connected;
    bool http_ready;
    int8_t wifi_rssi;
    uint32_t reconnect_attempts;
    uint32_t last_http_connect_ms;
} connectivity_status_t;

/* =========================================================
 * INFORMAÇÕES DO DISPOSITIVO
 * ========================================================= */
typedef struct {
    char device_id[32];
    char ward_id[32];
    char patient_id[32];
    char firmware_version[16];
    uint32_t uptime_seconds;
    uint32_t free_heap_bytes;
    uint32_t sd_free_mb;
} device_info_t;

/* =========================================================
 * CONTEXTO GLOBAL DO SISTEMA
 * Protegido por mutex — usar core_lock() / core_unlock()
 * ========================================================= */
typedef struct {
    sys_state_t         state;
    connectivity_status_t connectivity;
    device_info_t       device;
    SemaphoreHandle_t   mutex;
} system_context_t;

/* =========================================================
 * API PÚBLICA
 * ========================================================= */

/**
 * @brief Inicializa o contexto global do sistema.
 * Deve ser chamado uma única vez no início de main().
 */
esp_err_t core_init(void);

/**
 * @brief Retorna o estado atual da máquina de estados.
 */
sys_state_t core_get_state(void);

/**
 * @brief Transita para um novo estado.
 * Registra a transição em log e notifica tasks interessadas.
 */
void core_set_state(sys_state_t new_state);

/**
 * @brief Lê uma cópia do contexto global (thread-safe).
 */
void core_get_context(system_context_t *out_ctx);

/**
 * @brief Atualiza campos de conectividade no contexto (thread-safe).
 */
void core_update_connectivity(const connectivity_status_t *conn);

/**
 * @brief Retorna ponteiro para as informações do dispositivo (somente leitura).
 * Válido apenas dentro de um bloco core_lock() / core_unlock().
 */
const device_info_t *core_get_device_info(void);

/** @brief Adquire mutex do contexto global. */
bool core_lock(TickType_t timeout_ticks);

/** @brief Libera mutex do contexto global. */
void core_unlock(void);

#endif /* VITALKARE_CORE_H */
