/**
 * @file main.c
 * @brief Ponto de entrada do firmware VitalKare
 *
 * Responsabilidades:
 *   1. Inicialização de hardware periférico
 *   2. Inicialização das camadas de software (core, events)
 *   3. Criação de todas as tasks FreeRTOS
 *   4. Inicialização da pilha de rede (Wi-Fi → HTTP REST)
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

/* Componentes */
#include "core/core.h"
#include "events/event_dispatcher.h"
#include "network/wifi_manager.h"
#include "http_client/vitalkare_http.h"
#include "buttons/button_handler.h"
#include "sensors/hdc1080.h"
#include "sensors/mpu6050.h"
#include "sensors/ldr.h"
#include "sensors/sensor_task.h"
#include "display/oled_driver.h"
#include "display/ui_renderer.h"
#include "storage/sd_manager.h"
#include "storage/log_writer.h"
#include "storage/offline_cache.h"
#include "audio/audio_capture.h"
#include "camera/camera_driver.h"
#include "watchdog/watchdog.h"

static const char *TAG = "MAIN";

/* =========================================================
 * HANDLER DE COMANDOS HTTP (poll de comandos pendentes do backend)
 * ========================================================= */
static void process_http_command(const char *cmd_type)
{
    ESP_LOGI(TAG, "Comando HTTP recebido: %s", cmd_type);

    system_event_t evt = {
        .timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS,
        .priority     = EVT_PRIO_HIGH,
        .extra_ptr    = NULL,
    };

    if (strstr(cmd_type, "camera")) {
        evt.type = EVT_CAMERA_CAPTURE_REQ;
    } else if (strstr(cmd_type, "ack")) {
        evt.type = EVT_ACK_RECEIVED;
    } else {
        evt.type = EVT_HTTP_CMD_RECEIVED;
    }

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

    /* Publica via HTTP — apenas se já conectado e HTTP inicializado */
    sys_state_t state = core_get_state();
    if (state == SYS_STATE_HTTP_READY || state == SYS_STATE_OPERATIONAL) {
        char json[192];
        snprintf(json, sizeof(json),
                 "{\"device_id\":\"%s\",\"ward_id\":\"%s\","
                 "\"patient_id\":\"%s\",\"firmware_version\":\"%s\","
                 "\"timestamp_ms\":%lu,\"trigger\":\"BUTTON_B\"}",
                 APP_DEFAULT_DEVICE_ID, APP_DEFAULT_WARD_ID,
                 APP_DEFAULT_PATIENT_ID, APP_FIRMWARE_VERSION,
                 (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS));

        char endpoint[96];
        snprintf(endpoint, sizeof(endpoint),
                 "/api/v1/devices/%s/events", APP_DEFAULT_DEVICE_ID);
        vitalkare_http_post(endpoint, json);

        /* Grava log no SD — disponível apenas com SD inicializado */
        log_write(LOG_LEVEL_CRITICAL, LOG_CAT_BUTTON,
                  "Botão de emergência pressionado", NULL);
    } else {
        ESP_LOGW(TAG, "Emergência sem conectividade — gravando em cache offline");
    }
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
    ESP_LOGI(TAG, "Wi-Fi conectado — iniciando HTTP client");
    vitalkare_http_init();
    core_set_state(SYS_STATE_HTTP_READY);

    /* Sincroniza cache offline se houver eventos pendentes */
    if (offline_cache_pending_count() > 0) {
        system_event_t sync_evt = {
            .type         = EVT_OFFLINE_SYNC_NEEDED,
            .priority     = EVT_PRIO_LOW,
            .timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS,
        };
        event_post(&sync_evt, false);
    }

    core_set_state(SYS_STATE_OPERATIONAL);
    ui_set_screen(UI_SCREEN_OPERATIONAL);
}

static void on_wifi_disconnected(const system_event_t *evt)
{
    ESP_LOGW(TAG, "Wi-Fi desconectado — modo offline");
    core_set_state(SYS_STATE_OFFLINE);
    ui_set_screen(UI_SCREEN_OFFLINE);
}

static void on_ack_received(const system_event_t *evt)
{
    ESP_LOGI(TAG, "Atendimento confirmado pelo operador");
    core_set_state(SYS_STATE_OPERATIONAL);
    ui_set_screen(UI_SCREEN_ACK_RECEIVED);
    log_write(LOG_LEVEL_INFO, LOG_CAT_NETWORK, "ACK recebido do backend", NULL);
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

    /* Display OLED — periférico opcional; falha não é fatal */
    static oled_ctx_t oled_ctx;
    if (oled_init(&oled_ctx) != ESP_OK) {
        ESP_LOGW(TAG, "OLED não disponível — display desativado");
    }

    /* Sensores */
    hdc1080_init();
    mpu6050_init();
    ldr_init();

    /* Botões */
    ESP_ERROR_CHECK(button_handler_init());

    /* Áudio — apenas configura I2S, não inicia DMA */
    audio_capture_init();

    ESP_LOGI(TAG, "Hardware inicializado");
}

static void register_event_handlers(void)
{
    event_register_handler(EVT_BTN_EMERGENCY,       on_emergency_event);
    event_register_handler(EVT_BTN_REQUEST,         on_request_event);
    event_register_handler(EVT_WIFI_CONNECTED,      on_wifi_connected);
    event_register_handler(EVT_WIFI_DISCONNECTED,   on_wifi_disconnected);
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
                "wifi", TASK_STACK_NETWORK,
                NULL, TASK_PRIO_NETWORK, NULL);

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

    /* 8. Transição para WIFI_CONNECTING */
    core_set_state(SYS_STATE_WIFI_CONNECTING);
    ui_set_screen(UI_SCREEN_CONNECTING);

    /* 10. Wi-Fi — inicializa ANTES da câmera para evitar conflito de cache
     * (esp_wifi_init → NVS read → spi_flash_op_lock suspende ICACHE+DCACHE;
     *  câmera com DMA ativo durante esse janela causa panic_enable_cache → TG1WDT) */
    esp_err_t wifi_ret = wifi_manager_init();
    if (wifi_ret != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi não pôde ser iniciado (%s) — modo offline",
                 esp_err_to_name(wifi_ret));
        core_set_state(SYS_STATE_OFFLINE);
        ui_set_screen(UI_SCREEN_OFFLINE);
    }

    /* 11. Câmera — inicializada APÓS WiFi para evitar DMA durante cache lock */
    camera_driver_init();

    /* 12. Criar todas as tasks */
    create_tasks();

    /* app_main pode retornar — FreeRTOS scheduler assume o controle */
    ESP_LOGI(TAG, "Inicialização concluída. Scheduler ativo.");
}
