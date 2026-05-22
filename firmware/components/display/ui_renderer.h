/**
 * @file ui_renderer.h
 * @brief Renderizador de telas do VitalKare no OLED SSD1306
 *
 * Cada tela é uma função que escreve no buffer do oled_ctx_t.
 * A task_display_update chama a tela ativa a cada ciclo.
 */

#ifndef VITALKARE_UI_RENDERER_H
#define VITALKARE_UI_RENDERER_H

#include "oled_driver.h"
#include "../core/core.h"
#include <stdint.h>

typedef enum {
    UI_SCREEN_BOOT = 0,
    UI_SCREEN_CONNECTING,        /**< WiFi connecting */
    UI_SCREEN_TCP_CONNECTING,    /**< WiFi OK, TCP connecting to backend */
    UI_SCREEN_OPERATIONAL,
    UI_SCREEN_EMERGENCY,
    UI_SCREEN_RECORDING,
    UI_SCREEN_OFFLINE,
    UI_SCREEN_ACK_RECEIVED,
    UI_SCREEN_DIAGNOSTICS,       /**< Página de diagnóstico em tempo real */
} ui_screen_id_t;

typedef struct {
    float    temperature;
    float    humidity;
    float    lux;
    bool     wifi_ok;
    bool     tcp_connected;     /**< Backend TCP reachable */
    bool     mqtt_connected;    /**< Legacy alias — set equal to tcp_connected */
    uint32_t pending_events;
    char     patient_name[32];
    char     device_id[16];
    char     ip_addr[16];       /**< Device IP, e.g. "192.168.1.151" */
} ui_operational_data_t;

/**
 * @brief Dados para a tela de diagnóstico em tempo real.
 * Populado pelo system_manager + task_monitor + memory_monitor.
 */
typedef struct {
    /* Conectividade */
    int8_t   wifi_rssi;
    bool     tcp_ok;
    uint8_t  reconnect_count;   /**< últimas reconexões (saturado em 99) */

    /* Memória */
    uint32_t heap_free_kb;
    uint32_t psram_free_kb;

    /* Sistema */
    uint32_t uptime_sec;
    uint8_t  faulted_tasks;

    /* Filas */
    uint8_t  critical_q_pct;    /**< % de ocupação da fila crítica */
    uint8_t  normal_q_pct;      /**< % de ocupação da fila normal */

    /* Áudio */
    uint8_t  audio_state;       /**< audio_state_t cast para uint8 */
} ui_diagnostics_data_t;

/**
 * @brief Inicializa o renderer UI com o contexto OLED.
 * Idempotente — chamadas subsequentes são no-op.
 */
esp_err_t ui_init(oled_ctx_t *ctx);

/** @brief Define a tela ativa (thread-safe). */
void ui_set_screen(ui_screen_id_t screen);

/** @brief Atualiza dados da tela operacional. */
void ui_update_operational_data(const ui_operational_data_t *data);

/** @brief Atualiza apenas os campos de sensor (temp/hum/lux) sem tocar em WiFi/IP. */
void ui_update_sensor_data(float temperature, float humidity, float lux);

/** @brief Atualiza dados da tela de diagnóstico. Thread-safe. */
void ui_update_diagnostics_data(const ui_diagnostics_data_t *data);

/**
 * @brief Renderiza a tela ativa no contexto OLED e dá flush.
 * Chamado pela task_display_update a 2 Hz.
 */
void ui_render(oled_ctx_t *ctx);

/** @brief Task FreeRTOS de atualização do display. */
void task_display_update(void *arg);

#endif /* VITALKARE_UI_RENDERER_H */
