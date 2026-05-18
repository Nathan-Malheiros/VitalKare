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
    UI_SCREEN_CONNECTING,
    UI_SCREEN_OPERATIONAL,
    UI_SCREEN_EMERGENCY,
    UI_SCREEN_RECORDING,
    UI_SCREEN_OFFLINE,
    UI_SCREEN_ACK_RECEIVED,
} ui_screen_id_t;

typedef struct {
    float    temperature;
    float    humidity;
    float    lux;
    bool     wifi_ok;
    bool     mqtt_connected;
    uint32_t pending_events;
    char     patient_name[32];
    char     device_id[16];
} ui_operational_data_t;

/** @brief Define a tela ativa (thread-safe). */
void ui_set_screen(ui_screen_id_t screen);

/** @brief Atualiza dados da tela operacional. */
void ui_update_operational_data(const ui_operational_data_t *data);

/**
 * @brief Renderiza a tela ativa no contexto OLED e dá flush.
 * Chamado pela task_display_update a 2 Hz.
 */
void ui_render(oled_ctx_t *ctx);

/** @brief Task FreeRTOS de atualização do display. */
void task_display_update(void *arg);

#endif /* VITALKARE_UI_RENDERER_H */
