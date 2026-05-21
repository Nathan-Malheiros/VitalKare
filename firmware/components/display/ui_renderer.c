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
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "../../config/app_config.h"
#include "../../config/device_config.h"
#include "../core/task_monitor.h"
#include "../network/wifi_manager.h"
#include "../network/tcp_client.h"
#include "../core/system_manager.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "UI";

static ui_screen_id_t       s_screen      = UI_SCREEN_BOOT;
static ui_operational_data_t s_op_data    = {0};
static ui_diagnostics_data_t s_diag_data  = {0};
static SemaphoreHandle_t    s_mutex       = NULL;
static bool                 s_initialized = false;

/* =========================================================
 * INICIALIZAÇÃO
 * ========================================================= */
esp_err_t ui_init(oled_ctx_t *ctx)
{
    /* Idempotente: seguro chamar antes e depois do scheduler */
    if (s_initialized) return ESP_OK;
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

void ui_update_diagnostics_data(const ui_diagnostics_data_t *data)
{
    if (!s_initialized || data == NULL) return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        memcpy(&s_diag_data, data, sizeof(s_diag_data));
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
    ui_diagnostics_data_t diag;

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
    screen = s_screen;
    memcpy(&op, &s_op_data, sizeof(op));
    memcpy(&diag, &s_diag_data, sizeof(diag));
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

    /* ---- OPERATIONAL — dashboard de 8 linhas ---- */
    case UI_SCREEN_OPERATIONAL:
        /* Linha 0 (y=0):  VitalKare */
        oled_draw_string(ctx, 22, 0, "VitalKare");

        /* Linha 1 (y=8):  device_id */
        oled_draw_string(ctx, 0, 8,
                         op.device_id[0] ? op.device_id : DEVICE_ID);

        /* Linha 2 (y=16): WiFi status */
        snprintf(buf, sizeof(buf), "WiFi: %s", op.wifi_ok ? "OK" : "OFFLINE");
        oled_draw_string(ctx, 0, 16, buf);

        /* Linha 3 (y=24): IP address */
        snprintf(buf, sizeof(buf), "IP: %s",
                 op.ip_addr[0] ? op.ip_addr : "---");
        oled_draw_string(ctx, 0, 24, buf);

        /* Linha 4 (y=32): Temperatura + Umidade */
        snprintf(buf, sizeof(buf), "T:%.1fC H:%.0f%%",
                 op.temperature, op.humidity);
        oled_draw_string(ctx, 0, 32, buf);

        /* Linha 5 (y=40): Luminosidade */
        snprintf(buf, sizeof(buf), "Lux : %.0f", op.lux);
        oled_draw_string(ctx, 0, 40, buf);

        /* Linha 6 (y=48): Backend status */
        snprintf(buf, sizeof(buf), "Backend: %s",
                 op.tcp_connected ? "ONLINE" : "OFFLINE");
        oled_draw_string(ctx, 0, 48, buf);

        /* Linha 7 (y=56): paciente ou vazio */
        if (op.patient_name[0]) {
            snprintf(buf, sizeof(buf), "Pac: %.14s", op.patient_name);
            oled_draw_string(ctx, 0, 56, buf);
        }
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

    /* ---- DIAGNOSTICS ---- */
    case UI_SCREEN_DIAGNOSTICS:
        /* Linha 0: Header */
        oled_draw_string(ctx, 20, 0, "-- DIAG --");
        oled_draw_hline( ctx,  0, 9, 128);
        /* Linha 1: WiFi RSSI + TCP */
        snprintf(buf, sizeof(buf), "W:%ddBm TCP:%s",
                 diag.wifi_rssi, diag.tcp_ok ? "OK" : "--");
        oled_draw_string(ctx, 0, 11, buf);
        /* Linha 2: Heap + PSRAM */
        snprintf(buf, sizeof(buf), "H:%luK P:%luK",
                 (unsigned long)diag.heap_free_kb,
                 (unsigned long)diag.psram_free_kb);
        oled_draw_string(ctx, 0, 21, buf);
        /* Linha 3: Uptime + reconexões */
        snprintf(buf, sizeof(buf), "Up:%lus Rx:%u",
                 (unsigned long)diag.uptime_sec,
                 (unsigned)diag.reconnect_count);
        oled_draw_string(ctx, 0, 31, buf);
        /* Linha 4: Filas crítica/normal */
        snprintf(buf, sizeof(buf), "Qc:%u%% Qn:%u%%",
                 diag.critical_q_pct, diag.normal_q_pct);
        oled_draw_string(ctx, 0, 41, buf);
        /* Linha 5: Tasks faultadas + áudio state */
        snprintf(buf, sizeof(buf), "Flt:%u Aud:%u",
                 diag.faulted_tasks, diag.audio_state);
        oled_draw_string(ctx, 0, 51, buf);
        break;

    default:
        oled_draw_string(ctx, 0, 24, "???");
        break;
    }

    oled_flush(ctx);
}

/* =========================================================
 * HELPERS — live status para dashboard
 * ========================================================= */
static void refresh_live_status(ui_operational_data_t *op)
{
    op->wifi_ok       = wifi_manager_is_connected();
    op->tcp_connected = tcp_client_is_connected();
    op->mqtt_connected = op->tcp_connected;

    /* IP do netif Wi-Fi STA */
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_ip_info_t ip = {0};
        if (esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr != 0) {
            snprintf(op->ip_addr, sizeof(op->ip_addr), IPSTR,
                     IP2STR(&ip.ip));
        }
    }
}

/* =========================================================
 * TASK
 * ========================================================= */
void task_display_update(void *arg)
{
    oled_ctx_t *ctx = (oled_ctx_t *)arg;
    ESP_LOGI(TAG, "OLED task started (ctx=%p)", (void *)ctx);

    if (!ctx) {
        ESP_LOGE(TAG, "OLED ctx NULL — task encerrada");
        vTaskDelete(NULL);
        return;
    }

    if (oled_init(ctx) != ESP_OK) {
        ESP_LOGE(TAG, "OLED init falhou na task — task encerrada");
        system_manager_report_subsystem(SUBSYS_DISPLAY, false);
        vTaskDelete(NULL);
        return;
    }

    system_manager_report_subsystem(SUBSYS_DISPLAY, true);
    ui_init(ctx);

    /* Belt-and-suspenders: garantir display ligado após init do scheduler */
    oled_display_on(ctx);
    ESP_LOGI(TAG, "OLED display ON enviado");

    for (;;) {
        task_monitor_heartbeat(TASK_ID_DISPLAY);
        ESP_LOGD(TAG, "OLED render tick");

        /* Atualiza status de conectividade diretamente no op_data */
        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            refresh_live_status(&s_op_data);
            xSemaphoreGive(s_mutex);
        }

        ui_render(ctx);
        vTaskDelay(pdMS_TO_TICKS(APP_DISPLAY_REFRESH_MS));
    }
}
