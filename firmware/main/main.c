/**
 * @file main.c
 * @brief Ponto de entrada do firmware VitalKare
 *
 * Responsabilidades:
 *   1. Inicialização de hardware periférico
 *   2. Inicialização das camadas de software (core, events)
 *   3. Criação de todas as tasks FreeRTOS
 *   4. Inicialização da pilha de rede (Wi-Fi → MQTT)
 *
 * IMPORTANTE: Nenhuma lógica de aplicação deve estar neste arquivo.
 * main.c é apenas o orquestrador de inicialização.
 *
 * @version 1.0.0
 * @hardware KIIRA V2 — ESP32-S3
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "esp_system.h"

/* Configurações */
#include "config/app_config.h"
#include "config/hardware_config.h"
#include "config/mqtt_config.h"

/* Componentes */
#include "components/core/core.h"
#include "components/events/event_dispatcher.h"
#include "components/network/wifi_manager.h"
#include "components/mqtt/vitalkare_mqtt.h"
#include "components/buttons/button_handler.h"
#include "components/sensors/hdc1080.h"
#include "components/sensors/mpu6050.h"
#include "components/sensors/ldr.h"
#include "components/sensors/sensor_task.h"
#include "components/display/oled_driver.h"
#include "components/display/ui_renderer.h"
#include "components/storage/sd_manager.h"
#include "components/storage/log_writer.h"
#include "components/storage/offline_cache.h"
#include "components/audio/audio_capture.h"
#include "components/camera/camera_driver.h"
#include "components/watchdog/watchdog.h"

static const char *TAG = "MAIN";

/* =========================================================
 * HANDLER MQTT → EVENT DISPATCHER
 * Roteia comandos recebidos do backend para eventos internos
 * ========================================================= */
static void on_mqtt_command(const char *topic, const uint8_t *data, int data_len)
{
    ESP_LOGI(TAG, "Comando MQTT recebido: %s", topic);

    system_event_t evt = {
        .timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS,
        .priority     = EVT_PRIO_HIGH,
        .extra_ptr    = NULL,
    };

    if (strstr(topic, "/cmd/camera")) {
        evt.type = EVT_CAMERA_CAPTURE_REQ;
    } else if (strstr(topic, "/cmd/ack")) {
        evt.type = EVT_ACK_RECEIVED;
    } else if (strstr(topic, "/cmd/buzzer")) {
        evt.type = EVT_MQTT_CMD_RECEIVED;
    } else {
        evt.type = EVT_MQTT_CMD_RECEIVED;
    }

    /* Copia até 128 bytes do payload inline */
    evt.data_len = (uint16_t)(data_len < (int)sizeof(evt.data)
                              ? data_len : sizeof(evt.data));
    memcpy(evt.data, data, evt.data_len);

    event_post(&evt, false);
}

/* =========================================================
 * HANDLERS DE EVENTOS — registrados no event_dispatcher
 * ========================================================= */
static void on_emergency_event(const system_event_t *evt)
{
    ESP_LOGW(TAG, "=== EMERGÊNCIA ATIVADA ===");

    core_set_state(SYS_STATE_EMERGENCY);
    ui_set_screen(UI_SCREEN_EMERGENCY);

    /* Publica via MQTT (QoS 2) */
    vitalkare_mqtt_publish_emergency("BUTTON_B");

    /* Grava log no SD */
    log_write(LOG_LEVEL_CRITICAL, LOG_CAT_BUTTON,
              "Botão de emergência pressionado", NULL);
}

static TaskHandle_t s_audio_task_handle = NULL;

static void on_request_event(const system_event_t *evt)
{
    ESP_LOGI(TAG, "Solicitação do paciente");
    /* Notifica a task de áudio para iniciar gravação */
    if (s_audio_task_handle) {
        xTaskNotifyGive(s_audio_task_handle);
    }

    log_write(LOG_LEVEL_INFO, LOG_CAT_BUTTON,
              "Botão de solicitação pressionado", NULL);
}

static void on_wifi_connected(const system_event_t *evt)
{
    ESP_LOGI(TAG, "Wi-Fi conectado — iniciando MQTT");
    vitalkare_mqtt_init();
}

static void on_wifi_disconnected(const system_event_t *evt)
{
    ESP_LOGW(TAG, "Wi-Fi desconectado — modo offline");
    core_set_state(SYS_STATE_OFFLINE);
    ui_set_screen(UI_SCREEN_OFFLINE);
}

static void on_mqtt_connected(const system_event_t *evt)
{
    core_set_state(SYS_STATE_OPERATIONAL);
    ui_set_screen(UI_SCREEN_OPERATIONAL);

    /* Sincroniza cache offline se houver eventos pendentes */
    if (offline_cache_pending_count() > 0) {
        system_event_t sync_evt = {
            .type         = EVT_OFFLINE_SYNC_NEEDED,
            .priority     = EVT_PRIO_LOW,
            .timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS,
        };
        event_post(&sync_evt, false);
    }
}

static void on_mqtt_disconnected(const system_event_t *evt)
{
    core_set_state(SYS_STATE_OFFLINE);
    ui_set_screen(UI_SCREEN_OFFLINE);
}

static void on_ack_received(const system_event_t *evt)
{
    ESP_LOGI(TAG, "Atendimento confirmado pelo operador");
    core_set_state(SYS_STATE_OPERATIONAL);
    ui_set_screen(UI_SCREEN_ACK_RECEIVED);
    log_write(LOG_LEVEL_INFO, LOG_CAT_MQTT, "ACK recebido do backend", NULL);
}

/* =========================================================
 * INICIALIZAÇÃO GERAL
 * ========================================================= */
static esp_err_t init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS corrompida — apagando e reiniciando");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

static void init_hardware(void)
{
    ESP_LOGI(TAG, "Inicializando hardware...");

    /* SD Card */
    if (sd_manager_init() == ESP_OK) {
        sd_manager_mkdir(APP_SD_LOG_DIR);
        sd_manager_mkdir(APP_SD_AUDIO_DIR);
        sd_manager_mkdir(APP_SD_IMAGE_DIR);
        sd_manager_mkdir(APP_SD_CACHE_DIR);
    } else {
        ESP_LOGW(TAG, "SD Card não disponível — operação sem storage local");
    }

    /* Display OLED */
    static oled_ctx_t oled_ctx;
    ESP_ERROR_CHECK(oled_init(&oled_ctx));

    /* Sensores */
    hdc1080_init();
    mpu6050_init();
    ldr_init();

    /* Botões */
    ESP_ERROR_CHECK(button_handler_init());

    /* Câmera */
    camera_driver_init();

    /* Áudio */
    audio_capture_init();

    ESP_LOGI(TAG, "Hardware inicializado");
}

static void register_event_handlers(void)
{
    event_register_handler(EVT_BTN_EMERGENCY,       on_emergency_event);
    event_register_handler(EVT_BTN_REQUEST,         on_request_event);
    event_register_handler(EVT_WIFI_CONNECTED,      on_wifi_connected);
    event_register_handler(EVT_WIFI_DISCONNECTED,   on_wifi_disconnected);
    event_register_handler(EVT_MQTT_CONNECTED,      on_mqtt_connected);
    event_register_handler(EVT_MQTT_DISCONNECTED,   on_mqtt_disconnected);
    event_register_handler(EVT_ACK_RECEIVED,        on_ack_received);
}

static void create_tasks(void)
{
    /* Prioridade máxima — deve ser criada primeiro */
    xTaskCreate(task_watchdog,
                "watchdog", TASK_STACK_WATCHDOG,
                NULL, TASK_PRIO_WATCHDOG, NULL);

    /* Dispatcher de eventos */
    xTaskCreate(task_event_dispatcher,
                "events", TASK_STACK_EVENTS,
                NULL, TASK_PRIO_EVENTS, NULL);

    /* Botões */
    xTaskCreate(task_button_handler,
                "buttons", TASK_STACK_BUTTONS,
                NULL, TASK_PRIO_BUTTONS, NULL);

    /* Sensores ambientais */
    xTaskCreate(task_sensor_poll,
                "sensors", TASK_STACK_SENSORS,
                NULL, TASK_PRIO_SENSORS, NULL);

    /* Display OLED */
    xTaskCreate(task_display_update,
                "display", TASK_STACK_DISPLAY,
                NULL, TASK_PRIO_DISPLAY, NULL);

    /* Storage flush */
    xTaskCreate(task_storage_flush,
                "storage", TASK_STACK_STORAGE,
                NULL, TASK_PRIO_STORAGE, NULL);

    /* Wi-Fi manager */
    xTaskCreate(task_wifi_manager,
                "wifi", TASK_STACK_MQTT,
                NULL, TASK_PRIO_MQTT, NULL);

    /* MQTT manager — criado, mas só opera após Wi-Fi conectar */
    xTaskCreate(task_mqtt_manager,
                "mqtt", TASK_STACK_MQTT,
                NULL, TASK_PRIO_MQTT, NULL);

    /* Áudio */
    xTaskCreate(task_audio_capture, "audio", TASK_STACK_AUDIO, NULL, TASK_PRIO_AUDIO, &s_audio_task_handle);

    /* Câmera */
    xTaskCreate(task_camera_stream, "camera", TASK_STACK_CAMERA, NULL, TASK_PRIO_CAMERA, NULL);

    ESP_LOGI(TAG, "Tasks criadas. Heap livre: %lu bytes",
             (unsigned long)esp_get_free_heap_size());
}

/* =========================================================
 * PONTO DE ENTRADA
 * ========================================================= */
void app_main(void)
{
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "  VitalKare v%s — Iniciando", APP_FIRMWARE_VERSION);
    ESP_LOGI(TAG, "  Dispositivo: %s", APP_DEFAULT_DEVICE_ID);
    ESP_LOGI(TAG, "============================================");

    /* 1. NVS */
    ESP_ERROR_CHECK(init_nvs());

    /* 2. Core — estado global */
    ESP_ERROR_CHECK(core_init());
    core_set_state(SYS_STATE_BOOT);

    /* 3. Log writer */
    log_writer_init();
    log_write(LOG_LEVEL_INFO, LOG_CAT_SYSTEM, "Sistema iniciando", NULL);

    /* 4. Event dispatcher */
    ESP_ERROR_CHECK(event_dispatcher_init());

    /* 5. Registrar handlers antes de criar tasks */
    register_event_handlers();

    /* 6. Offline cache */
    offline_cache_init();

    /* 7. Hardware periférico */
    init_hardware();

    /* 8. MQTT callback */
    vitalkare_mqtt_set_message_callback(on_mqtt_command);

    /* 9. Transição para WIFI_CONNECTING */
    core_set_state(SYS_STATE_WIFI_CONNECTING);
    ui_set_screen(UI_SCREEN_CONNECTING);

    /* 10. Wi-Fi */
    esp_err_t wifi_ret = wifi_manager_init();
    if (wifi_ret != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi não pôde ser iniciado (%s) — modo offline",
                 esp_err_to_name(wifi_ret));
        core_set_state(SYS_STATE_OFFLINE);
        ui_set_screen(UI_SCREEN_OFFLINE);
    }

    /* 11. Criar todas as tasks */
    create_tasks();

    /* app_main pode retornar — FreeRTOS scheduler assume o controle */
    ESP_LOGI(TAG, "Inicialização concluída. Scheduler ativo.");
}
