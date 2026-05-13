/**
 * @file ui_renderer.c
 * @brief Renderizador de telas OLED para VitalKare
 *
 * Thread-safe via mutex. Chame ui_set_screen() de qualquer task.
 */

#include "ui_renderer.h"
#include "oled_driver.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "../../config/app_config.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "UI";

static ui_screen_id_t       s_screen      = UI_SCREEN_BOOT;
static ui_operational_data_t s_op_data    = {0};
static SemaphoreHandle_t    s_mutex       = NULL;
static bool                 s_initialized = false;

/* =========================================================
 * INICIALIZAÇÃO
 * ========================================================= */
esp_err_t ui_init(oled_ctx_t *ctx)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;
    s_screen = UI_SCREEN_BOOT;
    memset(&s_op_data, 0, sizeof(s_op_data));
    s_initialized = true;
    ESP_LOGI(TAG, "UI inicializado");
    return ESP_OK;
}

/* =========================================================
 * CONTROLE DE TELA
 * ========================================================= */
void ui_set_screen(ui_screen_id_t screen_id)
{
    if (!s_initialized) return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        s_screen = screen_id;
        xSemaphoreGive(s_mutex);
    }
}

ui_screen_id_t ui_get_screen(void) { return s_screen; }

void ui_update_operational_data(const ui_operational_data_t *data)
{
    if (!s_initialized || data == NULL) return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        memcpy(&s_op_data, data, sizeof(s_op_data));
        xSemaphoreGive(s_mutex);
    }
}

/* =========================================================
 * RENDERIZAÇÃO
 * ========================================================= */
void ui_render(oled_ctx_t *ctx)
{
    if (!s_initialized || ctx == NULL) return;

    ui_screen_id_t screen;
    ui_operational_data_t op;

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
    screen = s_screen;
    memcpy(&op, &s_op_data, sizeof(op));
    xSemaphoreGive(s_mutex);

    oled_clear(ctx);
    char buf[22];

    switch (screen) {
    /* ---- BOOT ---- */
    case UI_SCREEN_BOOT:
        oled_draw_string(ctx, 20, 0,  "  VitalKare  ");
        oled_draw_hline( ctx,  0, 10, 128);
        oled_draw_string(ctx,  8, 16, "Inicializando...");
        oled_draw_string(ctx, 28, 48, "v1.0");
        break;

    /* ---- CONNECTING ---- */
    case UI_SCREEN_CONNECTING:
        oled_draw_string(ctx, 20, 0, "  VitalKare  ");
        oled_draw_hline( ctx,  0, 10, 128);
        oled_draw_string(ctx,  4, 20, "Conectando WiFi");
        oled_draw_string(ctx,  4, 36, "Aguarde...");
        break;

    /* ---- OPERATIONAL ---- */
    case UI_SCREEN_OPERATIONAL:
        /* Linha 0: Leito + status MQTT */
        snprintf(buf, sizeof(buf), "%s  MQTT:%s",
                 op.device_id[0] ? op.device_id : "---",
                 op.mqtt_connected ? "OK" : "--");
        oled_draw_string(ctx, 0, 0, buf);
        oled_draw_hline(ctx, 0, 9, 128);

        /* Temperatura / Umidade */
        snprintf(buf, sizeof(buf), "T:%.1fC H:%.0f%%",
                 op.temperature, op.humidity);
        oled_draw_string(ctx, 0, 13, buf);

        /* Luminosidade */
        snprintf(buf, sizeof(buf), "Lux: %.0f", op.lux);
        oled_draw_string(ctx, 0, 25, buf);

        /* Paciente */
        if (op.patient_name[0]) {
            snprintf(buf, sizeof(buf), "Pac: %.14s", op.patient_name);
            oled_draw_string(ctx, 0, 37, buf);
        }

        /* Status */
        oled_draw_hline(ctx, 0, 54, 128);
        oled_draw_string(ctx, 0, 56, "Sistema OK");
        break;

    /* ---- EMERGENCY ---- */
    case UI_SCREEN_EMERGENCY:
        oled_invert(ctx, true);
        oled_draw_string(ctx,  8, 0,  "!!! EMERGENCIA !!!");
        oled_draw_hline( ctx,  0, 10, 128);
        oled_draw_string(ctx,  4, 18, "Pedido de auxilio");
        oled_draw_string(ctx,  4, 32, "Equipe acionada");
        oled_draw_string(ctx, 20, 48, "Aguardando ACK");
        oled_flush(ctx);
        return;  /* flush já feito */

    /* ---- RECORDING ---- */
    case UI_SCREEN_RECORDING:
        oled_draw_string(ctx,  8, 0, " Gravando Audio ");
        oled_draw_hline( ctx,  0, 10, 128);
        oled_draw_string(ctx, 20, 24, "[==========]");
        oled_draw_string(ctx,  0, 48, "Aguarde...");
        break;

    /* ---- OFFLINE ---- */
    case UI_SCREEN_OFFLINE:
        oled_draw_string(ctx,  8, 0, "  OFFLINE  ");
        oled_draw_hline( ctx,  0, 10, 128);
        oled_draw_string(ctx,  0, 20, "Sem conexao MQTT");
        snprintf(buf, sizeof(buf), "Cache: %lu evts",
                 (unsigned long)op.pending_events);
        oled_draw_string(ctx, 0, 36, buf);
        oled_draw_string(ctx, 0, 52, "Funcionando local");
        break;

    /* ---- ACK_RECEIVED ---- */
    case UI_SCREEN_ACK_RECEIVED:
        oled_draw_string(ctx, 12, 0, " CONFIRMADO ");
        oled_draw_hline( ctx,  0, 10, 128);
        oled_draw_string(ctx,  4, 24, "Equipe a caminho!");
        oled_draw_rect(  ctx,  0,  0, 128, 64);
        break;

    default:
        oled_draw_string(ctx, 0, 24, "???");
        break;
    }

    oled_flush(ctx);
}

/* =========================================================
 * TASK
 * ========================================================= */
void task_display_update(void *arg)
{
    oled_ctx_t *ctx = (oled_ctx_t *)arg;
    ESP_LOGI(TAG, "Task display_update iniciada");

    if (!ctx || oled_init(ctx) != ESP_OK) {
        ESP_LOGE(TAG, "OLED init falhou — task encerrada");
        vTaskDelete(NULL);
        return;
    }

    ui_init(ctx);

    for (;;) {
        ui_render(ctx);
        vTaskDelay(pdMS_TO_TICKS(APP_DISPLAY_REFRESH_MS));
    }
}
