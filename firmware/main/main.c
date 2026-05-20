/**
 * @file main.c
 * @brief Ponto de entrada do firmware VitalKare
 *
 * Responsabilidades:
 *   1. Inicialização de hardware periférico
 *   2. Inicialização das camadas de software (core, events)
 *   3. Criação de todas as tasks FreeRTOS
 *   4. Inicialização da pilha de rede (Wi-Fi → TCP persistente)
 *
 * IMPORTANTE: Nenhuma lógica de aplicação deve estar neste arquivo.
 * main.c é apenas o orquestrador de inicialização.
 *
 * @version 2.0.0
 * @hardware Perfis suportados: KIIRA V2 completa ou ESP32-S3 DevKit reduzido
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
#include "config/board_profile.h"
#include "config/hardware_config.h"

/* Componentes */
#include "core/core.h"
#include "core/system_manager.h"
#include "core/task_monitor.h"
#include "core/memory_monitor.h"
#include "events/event_dispatcher.h"
#include "network/wifi_manager.h"
#include "network/tcp_client.h"
#include "network/command_processor.h"
#include "network/vk_reliability.h"
#include "buttons/button_handler.h"
#include "sensors/hdc1080.h"
#include "sensors/mpu6050.h"
#include "sensors/ldr.h"
#include "sensors/sensor_task.h"
#include "sensors/max30102.h"
#include "display/oled_driver.h"
#include "display/ui_renderer.h"
#include "storage/sd_manager.h"
#include "storage/log_writer.h"
#include "storage/offline_cache.h"
#include "storage/offline_journal.h"
#include "audio/audio_capture.h"
#include "audio/audio_encoder.h"
#include "camera/camera_driver.h"
#include "watchdog/watchdog.h"

static const char *TAG = "MAIN";

static void log_feature_skip(const char *feature)
{
    ESP_LOGI(TAG, "  [SKIP] %s — indisponivel no perfil %s", feature, VK_BOARD_TAG);
}

/* =========================================================
 * HANDLERS DE EVENTOS — registrados no event_dispatcher
 * ========================================================= */

static void on_btn_b_pressed(const system_event_t *evt)
{
    ESP_LOGW(TAG, "=== EMERGENCIA ATIVADA ===");
    core_set_state(SYS_STATE_EMERGENCY);
    ui_set_screen(UI_SCREEN_EMERGENCY);
    audio_ctrl_start(AUDIO_TRIGGER_EMERGENCY);
    log_write(LOG_LEVEL_CRITICAL, LOG_CAT_BUTTON,
              "Botao de emergencia pressionado", NULL);
}

static void on_btn_b_released(const system_event_t *evt)
{
    /* Para gravação apenas se ainda estiver em emergência
     * (evita parar uma gravação iniciada por timer ou outro motivo) */
    if (audio_ctrl_get_state() == AUDIO_STATE_RECORDING) {
        audio_ctrl_stop();
    }
}

static void on_btn_a_pressed(const system_event_t *evt)
{
    ESP_LOGI(TAG, "Solicitacao do paciente");
    audio_ctrl_start(AUDIO_TRIGGER_REQUEST);
    log_write(LOG_LEVEL_INFO, LOG_CAT_BUTTON,
              "Botao de solicitacao pressionado", NULL);
}

static void on_btn_a_released(const system_event_t *evt)
{
    if (audio_ctrl_get_state() == AUDIO_STATE_RECORDING) {
        audio_ctrl_stop();
    }
}

static void on_wifi_connected(const system_event_t *evt)
{
    ESP_LOGI(TAG, "Wi-Fi conectado — iniciando TCP client");
    core_set_state(SYS_STATE_TCP_CONNECTING);
    system_manager_report_subsystem(SUBSYS_WIFI, true);
    tcp_client_connect();
}

static void on_wifi_disconnected(const system_event_t *evt)
{
    ESP_LOGW(TAG, "Wi-Fi desconectado — modo offline");
    tcp_client_disconnect();
    core_set_state(SYS_STATE_OFFLINE);
    ui_set_screen(UI_SCREEN_OFFLINE);
}

static void on_tcp_connected(const system_event_t *evt)
{
    ESP_LOGI(TAG, "TCP conectado — estado OPERATIONAL");
    core_set_state(SYS_STATE_OPERATIONAL);
    ui_set_screen(UI_SCREEN_OPERATIONAL);
    system_manager_report_subsystem(SUBSYS_TCP, true);
    system_manager_inc_reconnect();
    vk_reliability_on_reconnect();

    /* Inicia sincronização do cache offline se necessário */
    if (offline_cache_pending_count() > 0) {
        core_set_state(SYS_STATE_SYNCING);
        system_event_t sync_evt = {
            .type         = EVT_OFFLINE_SYNC_NEEDED,
            .priority     = EVT_PRIO_LOW,
            .timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS,
        };
        event_post_normal(&sync_evt);
    }
}

static void on_tcp_disconnected(const system_event_t *evt)
{
    ESP_LOGW(TAG, "TCP desconectado — aguardando reconexao");
    system_manager_report_subsystem(SUBSYS_TCP, false);
    vk_reliability_on_disconnect();
    /* O tcp_client agendará reconexão automaticamente */
    if (core_get_state() != SYS_STATE_OFFLINE) {
        core_set_state(SYS_STATE_TCP_CONNECTING);
    }
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
    ESP_LOGI(TAG, "=== Hardware Init Start | Board: %s (%s) ===",
             VK_BOARD_NAME, VK_BOARD_RUNTIME_MODE);
    bool all_ok = true;

    /* SD Card */
    if (VK_BOARD_HAS_STORAGE) {
        if (sd_manager_init() == ESP_OK) {
            ESP_LOGI(TAG, "  [PASS] SD Card montado");
            sd_manager_mkdir(APP_SD_LOG_DIR);
            sd_manager_mkdir(APP_SD_AUDIO_DIR);
            sd_manager_mkdir(APP_SD_IMAGE_DIR);
            sd_manager_mkdir(APP_SD_CACHE_DIR);
        } else {
            ESP_LOGW(TAG, "  [FAIL] SD Card — operacao sem storage local");
            all_ok = false;
        }
    } else {
        log_feature_skip("SD Card");
    }

    /* Display OLED — periférico opcional; falha não é fatal */
    if (VK_BOARD_HAS_DISPLAY) {
        static oled_ctx_t oled_ctx;
        if (oled_init(&oled_ctx) == ESP_OK) {
            ESP_LOGI(TAG, "  [PASS] OLED display");
        } else {
            ESP_LOGW(TAG, "  [FAIL] OLED — display desativado");
        }
    } else {
        log_feature_skip("OLED display");
    }

    /* I2C Sensores */
    if (VK_BOARD_HAS_ENV_SENSORS) {
        esp_err_t hdc_ret = hdc1080_init();
        if (hdc_ret == ESP_OK) {
            ESP_LOGI(TAG, "  [PASS] HDC1080 (temp/humidity)");
        } else {
            ESP_LOGW(TAG, "  [FAIL] HDC1080 err=%s", esp_err_to_name(hdc_ret));
            all_ok = false;
        }

        esp_err_t mpu_ret = mpu6050_init();
        if (mpu_ret == ESP_OK) {
            ESP_LOGI(TAG, "  [PASS] MPU6050 (motion)");
        } else {
            ESP_LOGW(TAG, "  [FAIL] MPU6050 err=%s", esp_err_to_name(mpu_ret));
        }

        ldr_init();
        ESP_LOGI(TAG, "  [PASS] LDR (ambient light)");
    } else {
        log_feature_skip("Sensores ambientais");
    }

    /* Oxímetro MAX30102 */
    if (VK_BOARD_HAS_OXIMETER) {
        if (max30102_init() == ESP_OK) {
            ESP_LOGI(TAG, "  [PASS] MAX30102 (oximeter)");
        } else {
            ESP_LOGW(TAG, "  [FAIL] MAX30102 — oximetro desativado");
        }
    } else {
        log_feature_skip("Oximetro MAX30102");
    }

    /* Botões — interrupções ANYEDGE */
    if (VK_BOARD_HAS_BUTTONS) {
        if (button_handler_init() == ESP_OK) {
            ESP_LOGI(TAG, "  [PASS] Button handler (GPIO %d, %d)",
                     HW_BTN_A_GPIO, HW_BTN_B_GPIO);
        } else {
            ESP_LOGW(TAG, "  [FAIL] Button handler — continuando sem botoes");
            all_ok = false;
        }
    } else {
        log_feature_skip("Buttons");
    }

    /* Áudio */
    if (!VK_BOARD_HAS_AUDIO) {
        log_feature_skip("Audio I2S");
    }
#if 0
    if (VK_BOARD_HAS_AUDIO) {
        if (audio_capture_init() == ESP_OK) {
            ESP_LOGI(TAG, "  [PASS] Audio I2S PDM + PSRAM chunk pool");
        } else {
            ESP_LOGE(TAG, "  [FAIL] Audio capture init — FATAL");
            esp_restart();
        }

        if (audio_encoder_init() == ESP_OK) {
            ESP_LOGI(TAG, "  [PASS] Audio ADPCM encoder");
        } else {
            ESP_LOGE(TAG, "  [FAIL] Audio encoder init — FATAL");
            esp_restart();
        }
    }
#else
    if (VK_BOARD_HAS_AUDIO) {
        ESP_LOGW(TAG, "  [SKIP] Audio disabled — GPIO37/PSRAM conflict");
    }
#endif

    ESP_LOGI(TAG, "=== Hardware Init %s | Heap: %lu B | PSRAM: %lu B ===",
             all_ok ? "COMPLETE" : "DEGRADED",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)esp_get_free_internal_heap_size());
}

static void register_event_handlers(void)
{
    /* Botões — novos eventos com PRESSED + RELEASED */
    if (VK_BOARD_HAS_BUTTONS) {
        event_register_handler(EVT_BTN_A_PRESSED,       on_btn_a_pressed);
        event_register_handler(EVT_BTN_A_RELEASED,      on_btn_a_released);
        event_register_handler(EVT_BTN_B_PRESSED,       on_btn_b_pressed);
        event_register_handler(EVT_BTN_B_RELEASED,      on_btn_b_released);
    }

    /* Rede — TCP substitui HTTP */
    event_register_handler(EVT_WIFI_CONNECTED,      on_wifi_connected);
    event_register_handler(EVT_WIFI_DISCONNECTED,   on_wifi_disconnected);
    event_register_handler(EVT_TCP_CONNECTED,       on_tcp_connected);
    event_register_handler(EVT_TCP_DISCONNECTED,    on_tcp_disconnected);

    /* Aplicação */
    event_register_handler(EVT_ACK_RECEIVED,        on_ack_received);
}

static void create_tasks(void)
{
    /* Watchdog — prioridade máxima */
    xTaskCreate(task_watchdog,
                "watchdog", TASK_STACK_WATCHDOG,
                NULL, TASK_PRIO_WATCHDOG, NULL);

    /* Event dispatcher */
    xTaskCreate(task_event_dispatcher,
                "events", TASK_STACK_EVENTS,
                NULL, TASK_PRIO_EVENTS, NULL);

    if (VK_BOARD_HAS_BUTTONS) {
        xTaskCreate(task_button_handler,
                    "buttons", TASK_STACK_BUTTONS,
                    NULL, TASK_PRIO_BUTTONS, NULL);
    }

    /* Sensores ambientais */
    if (VK_BOARD_HAS_ENV_SENSORS) {
        xTaskCreate(task_sensor_poll,
                    "sensors", TASK_STACK_SENSORS,
                    NULL, TASK_PRIO_SENSORS, NULL);
    }

    /* Oxímetro MAX30102 */
    if (VK_BOARD_HAS_OXIMETER) {
        xTaskCreate(task_oximeter_poll,
                    "oximeter", 4096,
                    NULL, TASK_PRIO_SENSORS, NULL);
    }

    /* Display OLED */
    if (VK_BOARD_HAS_DISPLAY) {
        xTaskCreate(task_display_update,
                    "display", TASK_STACK_DISPLAY,
                    NULL, TASK_PRIO_DISPLAY, NULL);
    }

    /* Storage flush */
    if (VK_BOARD_HAS_STORAGE) {
        xTaskCreate(task_storage_flush,
                    "storage", TASK_STACK_STORAGE,
                    NULL, TASK_PRIO_STORAGE, NULL);
    }

    /* Wi-Fi manager */
    xTaskCreate(task_wifi_manager,
                "wifi", TASK_STACK_NETWORK,
                NULL, TASK_PRIO_NETWORK, NULL);

    /* TCP TX task — dono do socket, FSM, heartbeat, fila de saída */
    xTaskCreate(task_tcp_tx,
                "tcp_tx", TASK_STACK_NETWORK,
                NULL, TASK_PRIO_NETWORK, NULL);

    /* TCP RX task — recv puro, parsing, roteamento (core 0, menor prio) */
    xTaskCreate(task_tcp_rx,
                "tcp_rx", TASK_STACK_NETWORK,
                NULL, TASK_PRIO_NETWORK - 1, NULL);

    /* Command processor (comandos do backend) */
    xTaskCreate(task_command_processor,
                "cmd_proc", TASK_STACK_CMD_PROC,
                NULL, TASK_PRIO_CMD_PROC, NULL);

    /* Audio tasks disabled — see hardware_config.h HW_MIC_DATA_GPIO */
#if 0
    xTaskCreate(task_audio_ctrl,
                "audio_ctrl", TASK_STACK_AUDIO,
                NULL, TASK_PRIO_AUDIO, NULL);

    xTaskCreate(task_audio_encoder,
                "audio_enc", TASK_STACK_AUDIO,
                NULL, TASK_PRIO_AUDIO - 1, NULL);
#endif

    if (VK_BOARD_HAS_CAMERA) {
        xTaskCreate(task_camera_stream,
                    "camera", TASK_STACK_CAMERA,
                    NULL, TASK_PRIO_CAMERA, NULL);
    }

    /* System manager monitor — baixa prioridade, varre saúde periódica */
    xTaskCreate(task_sys_manager,
                "sys_mgr", 4096,
                NULL, TASK_PRIO_STORAGE, NULL);

    /* VK Reliability scanner — retransmite frames pendentes */
    xTaskCreate(task_vk_reliability,
                "vk_rel", 3072,
                NULL, TASK_PRIO_STORAGE - 1, NULL);

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
    ESP_LOGI(TAG, "  Perfil de placa: %s (%s)", VK_BOARD_NAME, VK_BOARD_RUNTIME_MODE);
    ESP_LOGI(TAG, "============================================");

    /* 1. system_manager_init() — task_monitor + memory_monitor + mutex */
    ESP_ERROR_CHECK(system_manager_init());

    /* 2. NVS */
    ESP_ERROR_CHECK(init_nvs());

    /* 3. Core — estado global */
    ESP_ERROR_CHECK(core_init());
    core_set_state(SYS_STATE_BOOT);

    /* 4. Log writer */
    log_writer_init();
    log_write(LOG_LEVEL_INFO, LOG_CAT_SYSTEM, "Sistema iniciando", NULL);

    /* 5. Event dispatcher (dual-queue) */
    ESP_ERROR_CHECK(event_dispatcher_init());

    /* 6. Registrar handlers antes de criar tasks */
    register_event_handlers();

    /* 7. Offline cache (MQTT legado) + offline journal (binário VK) */
    offline_cache_init();

    /* 8. TCP client + command processor + reliability */
    ESP_ERROR_CHECK(tcp_client_init());
    ESP_ERROR_CHECK(cmd_processor_init());
    ESP_ERROR_CHECK(vk_reliability_init());

    /* 9. Hardware periférico */
    init_hardware();

    /* 10. Offline journal — requer SD montado (feito em init_hardware) */
    if (VK_BOARD_HAS_STORAGE) {
        offline_journal_init();  /* Falha graciosamente se SD indisponível */
    }

    /* Audio chunk pool registration disabled — audio init skipped */
#if 0
    memory_monitor_register_queue("chunk_pool",
                                  audio_chunk_pool_get_queue(),
                                  APP_AUDIO_CHUNK_POOL_COUNT);
#endif

    /* 11. Transição para WIFI_CONNECTING */
    core_set_state(SYS_STATE_WIFI_CONNECTING);
    ui_set_screen(UI_SCREEN_CONNECTING);

    /* 12. Wi-Fi — inicializa ANTES da câmera para evitar conflito de cache
     *  (esp_wifi_init → NVS read → spi_flash_op_lock suspende ICACHE+DCACHE;
     *   câmera com DMA ativo durante essa janela causa panic_enable_cache → TG1WDT) */
    esp_err_t wifi_ret = wifi_manager_init();
    if (wifi_ret != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi nao pode ser iniciado (%s) — modo offline",
                 esp_err_to_name(wifi_ret));
        core_set_state(SYS_STATE_OFFLINE);
        ui_set_screen(UI_SCREEN_OFFLINE);
    }

    /* 13. Câmera — inicializada APÓS WiFi */
    if (VK_BOARD_HAS_CAMERA) {
        camera_driver_init();
    } else {
        log_feature_skip("Camera OV5640");
    }

    /* 14. Criar todas as tasks */
    create_tasks();

    /* app_main pode retornar — FreeRTOS scheduler assume o controle */
    ESP_LOGI(TAG, "Inicializacao concluida. Scheduler ativo.");
}
