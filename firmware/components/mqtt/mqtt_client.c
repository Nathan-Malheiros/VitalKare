/**
 * @file mqtt_client.c
 * @brief Implementação do cliente MQTT VitalKare
 */

#include "mqtt_client.h"      /* esp-mqtt (componente oficial) */
#include "vitalkare_mqtt.h"  /* interface VitalKare */
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "../../config/app_config.h"
#include "../../config/mqtt_config.h"
#include "../core/core.h"
#include "../events/event_dispatcher.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "MQTT";

static esp_mqtt_client_handle_t s_client = NULL;
static mqtt_msg_cb_t            s_msg_cb = NULL;
static bool                     s_connected = false;
static SemaphoreHandle_t        s_mutex = NULL;

/* =========================================================
 * HANDLERS DE EVENTOS MQTT
 * ========================================================= */
static void on_mqtt_event(void *handler_args, esp_event_base_t base,
                          int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {

    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT conectado ao broker");
        s_connected = true;

        /* Assinar tópicos de comando */
        char topic_buf[MQTT_MAX_TOPIC_LEN];
        const device_info_t *dev = core_get_device_info();

        snprintf(topic_buf, sizeof(topic_buf), MQTT_TOPIC_CMD_ACK,
                 dev->ward_id, dev->device_id);
        esp_mqtt_client_subscribe(s_client, topic_buf, MQTT_QOS_CMD);

        snprintf(topic_buf, sizeof(topic_buf), MQTT_TOPIC_CMD_DISPLAY,
                 dev->ward_id, dev->device_id);
        esp_mqtt_client_subscribe(s_client, topic_buf, MQTT_QOS_CMD);

        snprintf(topic_buf, sizeof(topic_buf), MQTT_TOPIC_CMD_CAMERA,
                 dev->ward_id, dev->device_id);
        esp_mqtt_client_subscribe(s_client, topic_buf, MQTT_QOS_CMD);

        snprintf(topic_buf, sizeof(topic_buf), MQTT_TOPIC_CMD_BUZZER,
                 dev->ward_id, dev->device_id);
        esp_mqtt_client_subscribe(s_client, topic_buf, MQTT_QOS_CMD);

        esp_mqtt_client_subscribe(s_client, MQTT_TOPIC_CMD_BROADCAST, MQTT_QOS_CMD);

        /* Publica evento de conexão */
        system_event_t evt = {
            .type      = EVT_MQTT_CONNECTED,
            .priority  = EVT_PRIO_MEDIUM,
            .timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS,
        };
        event_post(&evt, false);

        /* Publica status imediatamente após conexão */
        vitalkare_mqtt_publish_status();
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT desconectado");
        s_connected = false;
        {
            system_event_t evt2 = {
                .type     = EVT_MQTT_DISCONNECTED,
                .priority = EVT_PRIO_HIGH,
                .timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS,
            };
            event_post(&evt2, false);
        }
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGD(TAG, "Mensagem recebida: topic=%.*s",
                 event->topic_len, event->topic);
        if (s_msg_cb) {
            /* Garantir string terminada em NULL para o tópico */
            char topic_null[MQTT_MAX_TOPIC_LEN];
            int tlen = event->topic_len < MQTT_MAX_TOPIC_LEN - 1
                       ? event->topic_len : MQTT_MAX_TOPIC_LEN - 1;
            memcpy(topic_null, event->topic, tlen);
            topic_null[tlen] = '\0';
            s_msg_cb(topic_null, (const uint8_t *)event->data, event->data_len);
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "Erro MQTT: type=%d",
                 event->error_handle->error_type);
        break;

    default:
        break;
    }
}

/* =========================================================
 * INICIALIZAÇÃO
 * ========================================================= */
esp_err_t vitalkare_mqtt_init(void)
{
    s_mutex = xSemaphoreCreateMutex();

    const device_info_t *dev = core_get_device_info();

    /* Monta Client ID único */
    char client_id[64];
    snprintf(client_id, sizeof(client_id), "%s%s",
             MQTT_CLIENT_ID_PREFIX, dev->device_id);

    /* Monta payload do LWT */
    char lwt_payload[MQTT_STATUS_PAYLOAD_LEN];
    char lwt_topic[MQTT_MAX_TOPIC_LEN];
    snprintf(lwt_topic, sizeof(lwt_topic), MQTT_LWT_TOPIC,
             dev->ward_id, dev->device_id);
    snprintf(lwt_payload, sizeof(lwt_payload),
             "{\"device_id\":\"%s\",\"ward\":\"%s\",\"online\":false,"
             "\"reason\":\"unexpected_disconnect\"}",
             dev->device_id, dev->ward_id);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.hostname = MQTT_BROKER_HOST_DEV,
        .broker.address.port     = MQTT_BROKER_PORT_PLAIN,
        .broker.address.transport = MQTT_TRANSPORT_OVER_TCP,
        .credentials.client_id   = client_id,
        .session.keepalive        = MQTT_KEEPALIVE_S,
        .session.disable_clean_session = !MQTT_CLEAN_SESSION,
        .session.last_will.topic  = lwt_topic,
        .session.last_will.msg    = lwt_payload,
        .session.last_will.msg_len = strlen(lwt_payload),
        .session.last_will.qos    = MQTT_LWT_QOS,
        .session.last_will.retain = MQTT_LWT_RETAIN,
        .network.reconnect_timeout_ms = APP_MQTT_CONNECT_TIMEOUT_MS,
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "Falha ao criar cliente MQTT");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                   on_mqtt_event, NULL);
    esp_mqtt_client_start(s_client);

    ESP_LOGI(TAG, "MQTT iniciado (broker=%s:%d client_id=%s)",
             MQTT_BROKER_HOST_DEV, MQTT_BROKER_PORT_PLAIN, client_id);

    return ESP_OK;
}

/* =========================================================
 * API
 * ========================================================= */
void vitalkare_mqtt_set_message_callback(mqtt_msg_cb_t cb) { s_msg_cb = cb; }
bool vitalkare_mqtt_is_connected(void) { return s_connected; }

int vitalkare_mqtt_publish(const char *topic, const void *data,
                           int len, int qos, bool retain)
{
    if (!s_connected || s_client == NULL) {
        ESP_LOGW(TAG, "Publish ignorado — MQTT offline (topic=%s)", topic);
        return -1;
    }
    return esp_mqtt_client_publish(s_client, topic, data, len, qos, retain ? 1 : 0);
}

esp_err_t vitalkare_mqtt_publish_status(void)
{
    if (!s_connected) return ESP_ERR_INVALID_STATE;

    const device_info_t *dev = core_get_device_info();
    char topic[MQTT_MAX_TOPIC_LEN];
    char payload[MQTT_STATUS_PAYLOAD_LEN];

    snprintf(topic, sizeof(topic), MQTT_TOPIC_STATUS,
             dev->ward_id, dev->device_id);

    snprintf(payload, sizeof(payload),
             "{\"device_id\":\"%s\",\"ward\":\"%s\",\"patient_id\":\"%s\","
             "\"online\":true,\"firmware_version\":\"%s\","
             "\"uptime_seconds\":%lu,\"free_heap_bytes\":%lu}",
             dev->device_id, dev->ward_id, dev->patient_id,
             dev->firmware_version,
             (unsigned long)dev->uptime_seconds,
             (unsigned long)dev->free_heap_bytes);

    int ret = vitalkare_mqtt_publish(topic, payload, strlen(payload),
                                     MQTT_QOS_STATUS, true);
    return (ret >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t vitalkare_mqtt_publish_emergency(const char *trigger)
{
    const device_info_t *dev = core_get_device_info();
    char topic[MQTT_MAX_TOPIC_LEN];
    char payload[MQTT_EVENT_PAYLOAD_LEN];

    snprintf(topic, sizeof(topic), MQTT_TOPIC_EMERGENCY,
             dev->ward_id, dev->device_id);

    snprintf(payload, sizeof(payload),
             "{\"device_id\":\"%s\",\"ward\":\"%s\",\"patient_id\":\"%s\","
             "\"event_type\":\"EMERGENCY\",\"priority\":1,"
             "\"trigger\":\"%s\",\"acknowledged\":false}",
             dev->device_id, dev->ward_id, dev->patient_id,
             trigger ? trigger : "UNKNOWN");

    int ret = vitalkare_mqtt_publish(topic, payload, strlen(payload),
                                     MQTT_QOS_EMERGENCY, false);
    return (ret >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t vitalkare_mqtt_publish_environment(float temp_c, float humidity_pct,
                                              float lux, float noise_db,
                                              bool motion,
                                              const char *posture_label)
{
    const device_info_t *dev = core_get_device_info();
    char topic[MQTT_MAX_TOPIC_LEN];
    char payload[MQTT_ENV_PAYLOAD_LEN];

    snprintf(topic, sizeof(topic), MQTT_TOPIC_ENVIRONMENT,
             dev->ward_id, dev->device_id);

    snprintf(payload, sizeof(payload),
             "{\"device_id\":\"%s\",\"ward\":\"%s\","
             "\"temperature_celsius\":%.1f,\"humidity_percent\":%.1f,"
             "\"luminosity_lux\":%.0f,\"noise_level_db\":%.1f,"
             "\"motion_detected\":%s,\"posture_label\":\"%s\"}",
             dev->device_id, dev->ward_id,
             temp_c, humidity_pct, lux, noise_db,
             motion ? "true" : "false",
             posture_label ? posture_label : "Desconhecido");

    int ret = vitalkare_mqtt_publish(topic, payload, strlen(payload),
                                     MQTT_QOS_SENSOR, true);
    return (ret >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t vitalkare_mqtt_publish_audio(const audio_recording_t *rec)
{
    if (!rec || !rec->data || rec->size_bytes == 0) return ESP_ERR_INVALID_ARG;
    /* Publica payload binário bruto — backend decodifica como PCM 16-bit */
    const device_info_t *dev = core_get_device_info();
    char topic[MQTT_MAX_TOPIC_LEN];
    snprintf(topic, sizeof(topic), MQTT_TOPIC_AUDIO,
             dev->ward_id, dev->device_id);
    int ret = vitalkare_mqtt_publish(topic, rec->data, (int)rec->size_bytes,
                                     MQTT_QOS_AUDIO, false);
    return (ret >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t vitalkare_mqtt_publish_camera(const camera_frame_t *frame)
{
    if (!frame || !frame->data || frame->size_bytes == 0) return ESP_ERR_INVALID_ARG;
    const device_info_t *dev = core_get_device_info();
    char topic[MQTT_MAX_TOPIC_LEN];
    snprintf(topic, sizeof(topic), MQTT_TOPIC_IMAGE,
             dev->ward_id, dev->device_id);
    int ret = vitalkare_mqtt_publish(topic, frame->data, (int)frame->size_bytes,
                                     MQTT_QOS_STATUS, false);
    return (ret >= 0) ? ESP_OK : ESP_FAIL;
}

void task_mqtt_manager(void *arg)
{
    ESP_LOGI(TAG, "Task MQTT manager iniciada");

    uint32_t last_heartbeat_ms = 0;

    for (;;) {
        uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

        if (s_connected && (now_ms - last_heartbeat_ms >= APP_HEARTBEAT_INTERVAL_MS)) {
            vitalkare_mqtt_publish_status();
            last_heartbeat_ms = now_ms;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
