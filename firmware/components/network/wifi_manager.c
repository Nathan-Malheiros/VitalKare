/**
 * @file wifi_manager.c
 * @brief Gerenciamento Wi-Fi com reconexão automática por backoff exponencial
 *
 * Lê SSID e senha da partição NVS (chave "wifi_ssid" / "wifi_pass").
 * Fallback para valores hardcoded apenas em modo de desenvolvimento.
 */

#include "wifi_manager.h"
#include "esp_log.h"
#include "../../config/device_config.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "../../config/app_config.h"
#include "../events/event_dispatcher.h"
#include <string.h>

static const char *TAG = "WIFI";

/* Bit de grupo de eventos — IP obtido */
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static EventGroupHandle_t   s_wifi_event_group = NULL;
static wifi_state_cb_t      s_state_callback    = NULL;
static bool                 s_connected         = false;
static int8_t               s_rssi              = 0;
static uint32_t             s_retry_count       = 0;

/* =========================================================
 * HANDLERS DE EVENTOS DO ESP-IDF
 * ========================================================= */
static void on_wifi_event(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    if (id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();

    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        s_rssi = 0;

        /* Backoff exponencial: min 2 s, max 120 s */
        uint32_t delay_ms = APP_RECONNECT_MIN_DELAY_MS * (1u << s_retry_count);
        if (delay_ms > APP_RECONNECT_MAX_DELAY_MS) {
            delay_ms = APP_RECONNECT_MAX_DELAY_MS;
        }
        s_retry_count++;

        ESP_LOGW(TAG, "Wi-Fi desconectado. Tentativa %lu em %lu ms",
                 s_retry_count, delay_ms);

        /* Publica evento de desconexão */
        system_event_t evt = {
            .type = EVT_WIFI_DISCONNECTED,
            .priority = EVT_PRIO_HIGH,
            .timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS,
        };
        event_post(&evt, false);

        vTaskDelay(pdMS_TO_TICKS(delay_ms));
        esp_wifi_connect();
    }
}

static void on_ip_event(void *arg, esp_event_base_t base,
                        int32_t id, void *data)
{
    if (id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "IP obtido: " IPSTR, IP2STR(&event->ip_info.ip));

        s_connected  = true;
        s_retry_count = 0;

        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            s_rssi = ap_info.rssi;
        }

        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        /* Publica evento de conexão */
        system_event_t evt = {
            .type = EVT_WIFI_CONNECTED,
            .priority = EVT_PRIO_MEDIUM,
            .timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS,
        };
        event_post(&evt, false);

        if (s_state_callback) {
            s_state_callback(true, s_rssi);
        }
    }
}

/* =========================================================
 * INICIALIZAÇÃO
 * ========================================================= */
esp_err_t wifi_manager_init(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_event, NULL, NULL));

    /* Lê credenciais da NVS */
    nvs_handle_t nvs;
    char ssid[64] = {0};
    char pass[64] = {0};
    size_t ssid_len = sizeof(ssid);
    size_t pass_len = sizeof(pass);

    if (nvs_open("wifi_cfg", NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_str(nvs, "ssid", ssid, &ssid_len);
        nvs_get_str(nvs, "pass", pass, &pass_len);
        nvs_close(nvs);
    }


    if (strlen(ssid) == 0) {
        /* Fallback para credenciais compiladas em device_config.h */
        if (DEVICE_WIFI_SSID[0] != '\0') {
            ESP_LOGW(TAG, "NVS vazia — usando credenciais de device_config.h (%s)", DEVICE_WIFI_SSID);
            strncpy(ssid, DEVICE_WIFI_SSID, sizeof(ssid) - 1);
            strncpy(pass, DEVICE_WIFI_PASS, sizeof(pass) - 1);
        } else {
            // --- Modo de configuração via serial ---
            printf("\n--- CONFIGURAÇÃO WI-FI VIA SERIAL ---\n");
            printf("Digite o SSID: ");
            fflush(stdout);
            if (fgets(ssid, sizeof(ssid), stdin) == NULL) {
                ESP_LOGE(TAG, "Erro ao ler SSID");
                return ESP_ERR_INVALID_ARG;
            }
            ssid[strcspn(ssid, "\n")] = 0;
            printf("Digite a senha: ");
            fflush(stdout);
            if (fgets(pass, sizeof(pass), stdin) == NULL) {
                ESP_LOGE(TAG, "Erro ao ler senha");
                return ESP_ERR_INVALID_ARG;
            }
            pass[strcspn(pass, "\n")] = 0;
            if (strlen(ssid) == 0) {
                ESP_LOGE(TAG, "SSID inválido");
                return ESP_ERR_INVALID_ARG;
            }
            // Salva na NVS
            nvs_handle_t nvs;
            if (nvs_open("wifi_cfg", NVS_READWRITE, &nvs) == ESP_OK) {
                nvs_set_str(nvs, "ssid", ssid);
                nvs_set_str(nvs, "pass", pass);
                nvs_commit(nvs);
                nvs_close(nvs);
                printf("\nCredenciais salvas! Reinicie o dispositivo para conectar ao Wi-Fi.\n");
            } else {
                ESP_LOGE(TAG, "Falha ao abrir NVS para escrita");
                return ESP_FAIL;
            }
            return ESP_ERR_NOT_FOUND; // Não tenta conectar agora, pede reboot
        }
    }

    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, pass, sizeof(wifi_cfg.sta.password) - 1);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_cfg.sta.pmf_cfg.capable    = true;
    wifi_cfg.sta.pmf_cfg.required   = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi iniciado. Conectando a '%s'...", ssid);
    return ESP_OK;
}

/* =========================================================
 * API
 * ========================================================= */
void wifi_manager_set_callback(wifi_state_cb_t cb) { s_state_callback = cb; }
bool wifi_manager_is_connected(void)               { return s_connected; }
int8_t wifi_manager_get_rssi(void)                 { return s_rssi; }

void wifi_manager_reconnect(void)
{
    s_retry_count = 0;
    esp_wifi_disconnect();
}

void task_wifi_manager(void *arg)
{
    /* Esta task é opcional — o loop principal de reconexão
     * já é gerenciado pelos event handlers. Pode ser usada
     * para monitoramento periódico do RSSI. */
    for (;;) {
        if (s_connected) {
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                s_rssi = ap_info.rssi;
                ESP_LOGD(TAG, "RSSI: %d dBm", s_rssi);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(30000)); /* Verifica RSSI a cada 30 s */
    }
}
