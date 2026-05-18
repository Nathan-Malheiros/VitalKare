/**
 * @file vitalkare_http.c
 * @brief Implementação do cliente HTTP REST VitalKare
 *
 * Usa esp_http_client.h nativo do ESP-IDF.
 * Base URL configurada em app_config.h (APP_HTTP_BASE_URL).
 */

#include "vitalkare_http.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "../../config/app_config.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "HTTP";

static bool               s_initialized = false;
static SemaphoreHandle_t  s_mutex       = NULL;

/* =========================================================
 * HANDLER DE EVENTOS (apenas logging)
 * ========================================================= */
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    default:
        break;
    }
    return ESP_OK;
}

/* =========================================================
 * API PÚBLICA
 * ========================================================= */
esp_err_t vitalkare_http_init(void)
{
    if (s_initialized) return ESP_OK;

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    s_initialized = true;
    ESP_LOGI(TAG, "HTTP client inicializado (base_url=%s)", APP_HTTP_BASE_URL);
    return ESP_OK;
}

esp_err_t vitalkare_http_post(const char *endpoint, const char *json_payload)
{
    if (!s_initialized || endpoint == NULL || json_payload == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    char url[256];
    snprintf(url, sizeof(url), "%s%s", APP_HTTP_BASE_URL, endpoint);

    esp_http_client_config_t cfg = {
        .url            = url,
        .method         = HTTP_METHOD_POST,
        .timeout_ms     = 5000,
        .event_handler  = http_event_handler,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGW(TAG, "esp_http_client_init falhou");
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client,
                                   json_payload,
                                   (int)strlen(json_payload));

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(6000)) != pdTRUE) {
        ESP_LOGW(TAG, "POST %s: mutex timeout", endpoint);
        esp_http_client_cleanup(client);
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t ret = esp_http_client_perform(client);
    xSemaphoreGive(s_mutex);

    if (ret == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status < 200 || status >= 300) {
            ESP_LOGW(TAG, "POST %s → HTTP %d", endpoint, status);
            ret = ESP_FAIL;
        } else {
            ESP_LOGD(TAG, "POST %s → HTTP %d OK", endpoint, status);
        }
    } else {
        ESP_LOGW(TAG, "POST %s falhou: %s", endpoint, esp_err_to_name(ret));
    }

    esp_http_client_cleanup(client);
    return ret;
}

esp_err_t vitalkare_http_post_binary(const char *endpoint,
                                      const uint8_t *data,
                                      size_t len,
                                      const char *content_type)
{
    if (!s_initialized || endpoint == NULL || data == NULL || len == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    char url[256];
    snprintf(url, sizeof(url), "%s%s", APP_HTTP_BASE_URL, endpoint);

    esp_http_client_config_t cfg = {
        .url           = url,
        .method        = HTTP_METHOD_POST,
        .timeout_ms    = 10000,
        .event_handler = http_event_handler,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGW(TAG, "esp_http_client_init falhou");
        return ESP_FAIL;
    }

    esp_http_client_set_header(client,
                                "Content-Type",
                                content_type ? content_type : "application/octet-stream");
    esp_http_client_set_post_field(client, (const char *)data, (int)len);

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(6000)) != pdTRUE) {
        ESP_LOGW(TAG, "POST binary %s: mutex timeout", endpoint);
        esp_http_client_cleanup(client);
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t ret = esp_http_client_perform(client);
    xSemaphoreGive(s_mutex);

    if (ret == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status < 200 || status >= 300) {
            ESP_LOGW(TAG, "POST binary %s → HTTP %d", endpoint, status);
            ret = ESP_FAIL;
        } else {
            ESP_LOGD(TAG, "POST binary %s → HTTP %d OK (%zu bytes)", endpoint, status, len);
        }
    } else {
        ESP_LOGW(TAG, "POST binary %s falhou: %s", endpoint, esp_err_to_name(ret));
    }

    esp_http_client_cleanup(client);
    return ret;
}
