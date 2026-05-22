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
#include "esp_timer.h"
#include "esp_heap_caps.h"

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

#if VK_SIMULATION_MODE
#include "simulation/sim_sensors.h"
#endif

static const char *TAG = "MAIN";

/* Contexto OLED em escopo de módulo para ser compartilhado com a task de display */
static oled_ctx_t s_oled_ctx;

/* Tracks whether SD card mounted successfully — gates storage task creation */
static bool s_sd_ok = false;

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
    log_write(LOG_LEVEL_CRITICAL, LOG_CAT_BUTTON,
              "Botao de emergencia pressionado", NULL);

    /* Send PKT_EVENT_EMERGENCY — VK_QOS_GUARANTEED: stays in pending table
     * until delivered, survives TCP reconnect. */
    pkt_event_t epkt = {
        .timestamp_unix = (uint32_t)(esp_timer_get_time() / 1000000LL),
        .trigger        = 1,
    };
    uint8_t eframe[VK_HEADER_SIZE + sizeof(pkt_event_t) + VK_CRC_SIZE];
    int eflen = vk_frame_build(eframe, sizeof(eframe),
                               PKT_EVENT_EMERGENCY, tcp_client_next_seq(),
                               &epkt, sizeof(epkt));
    if (eflen > 0) {
        vk_reliability_send(eframe, (uint16_t)eflen, VK_QOS_GUARANTEED);
    }

    audio_ctrl_start(AUDIO_TRIGGER_EMERGENCY);
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
    log_write(LOG_LEVEL_INFO, LOG_CAT_BUTTON,
              "Botao de solicitacao pressionado", NULL);

    /* Send PKT_EVENT_REQUEST — VK_QOS_RETRY: retries a few times, acceptable
     * to lose if connectivity is down for a long time. */
    pkt_event_t rpkt = {
        .timestamp_unix = (uint32_t)(esp_timer_get_time() / 1000000LL),
        .trigger        = 0,
    };
    uint8_t rframe[VK_HEADER_SIZE + sizeof(pkt_event_t) + VK_CRC_SIZE];
    int rflen = vk_frame_build(rframe, sizeof(rframe),
                               PKT_EVENT_REQUEST, tcp_client_next_seq(),
                               &rpkt, sizeof(rpkt));
    if (rflen > 0) {
        vk_reliability_send(rframe, (uint16_t)rflen, VK_QOS_RETRY);
    }

    audio_ctrl_start(AUDIO_TRIGGER_REQUEST);
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
    ui_set_screen(UI_SCREEN_TCP_CONNECTING);
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

    /* SD Card — failure is non-fatal; degrades to RAM-only operation */
    if (VK_BOARD_HAS_STORAGE) {
        if (sd_manager_init() == ESP_OK) {
            s_sd_ok = true;
            ESP_LOGI(TAG, "  [PASS] SD Card montado");
            sd_manager_mkdir(APP_SD_LOG_DIR);
            sd_manager_mkdir(APP_SD_AUDIO_DIR);
            sd_manager_mkdir(APP_SD_IMAGE_DIR);
            sd_manager_mkdir(APP_SD_CACHE_DIR);
        } else {
            s_sd_ok = false;
            ESP_LOGW(TAG, "  [WARN] SD Card ausente — operando sem storage local (nao fatal)");
        }
    } else {
        log_feature_skip("SD Card");
    }

    /* Display OLED — periférico opcional; falha não é fatal */
    if (VK_BOARD_HAS_DISPLAY) {
        if (oled_init(&s_oled_ctx) == ESP_OK) {
            ESP_LOGI(TAG, "  [PASS] OLED display (SDA=%d SCL=%d)",
                     HW_OLED_SDA_GPIO, HW_OLED_SCL_GPIO);
        } else {
            ESP_LOGW(TAG, "  [FAIL] OLED — display desativado (SDA=%d SCL=%d)",
                     HW_OLED_SDA_GPIO, HW_OLED_SCL_GPIO);
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

#if VK_SIMULATION_MODE
    /* Simulation mode: synthetic sensor task replaces real hardware drivers */
    xTaskCreate(task_sim_telemetry,
                "sim_sensors", 4096,
                NULL, TASK_PRIO_SENSORS, NULL);
#endif

    /* Display OLED — passa contexto para a task (não pode ser NULL).
     * CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384: stacks < 16 KB always use
     * internal RAM.  With WiFi + prior tasks, internal RAM may be exhausted.
     * On failure, retry with a PSRAM-allocated stack (safe for non-ISR tasks). */
    if (VK_BOARD_HAS_DISPLAY) {
        ESP_LOGI(TAG, "[DISPLAY] Creating display task — internal heap: %lu B, largest: %lu B",
                 (unsigned long)esp_get_free_internal_heap_size(),
                 (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        BaseType_t disp_rc = xTaskCreate(task_display_update,
                                         "display", TASK_STACK_DISPLAY,
                                         &s_oled_ctx, TASK_PRIO_DISPLAY, NULL);
        if (disp_rc != pdPASS) {
            ESP_LOGW(TAG, "[DISPLAY] xTaskCreate from internal RAM failed — retrying with PSRAM stack");
            /* Allocate stack from PSRAM, TCB from internal RAM */
            StackType_t   *disp_stack = heap_caps_malloc(
                TASK_STACK_DISPLAY * sizeof(StackType_t),
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            StaticTask_t  *disp_tcb   = heap_caps_malloc(
                sizeof(StaticTask_t),
                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (disp_stack && disp_tcb) {
                xTaskCreateStaticPinnedToCore(task_display_update,
                                              "display", TASK_STACK_DISPLAY,
                                              &s_oled_ctx, TASK_PRIO_DISPLAY,
                                              disp_stack, disp_tcb, tskNO_AFFINITY);
                ESP_LOGI(TAG, "[DISPLAY] Display task created with PSRAM stack");
            } else {
                ESP_LOGE(TAG, "[DISPLAY] FATAL: cannot allocate display task stack — display disabled");
                heap_caps_free(disp_stack);
                heap_caps_free(disp_tcb);
            }
        } else {
            ESP_LOGI(TAG, "[DISPLAY] Display task created OK (internal stack)");
        }
    }

    /* Storage flush — only if SD actually mounted */
    if (VK_BOARD_HAS_STORAGE && s_sd_ok) {
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

    ESP_LOGI(TAG, "Tasks criadas. Heap total: %lu B | Internal: %lu B | Largest internal block: %lu B",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)esp_get_free_internal_heap_size(),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
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

#if VK_SIMULATION_MODE
    ESP_LOGW(TAG, "============================================");
    ESP_LOGW(TAG, "  [SIMULATION] Running in simulated hardware mode");
    ESP_LOGW(TAG, "  [SIMULATION] Sensor providers replaced with virtual data");
    ESP_LOGW(TAG, "  [SIMULATION] KiiraV2 hardware access disabled");
    ESP_LOGW(TAG, "============================================");
    sim_sensors_init();
#endif

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

    /* 9b. UI init + first render — gives immediate visual feedback before tasks start.
     *  ui_init() is idempotent; the display task will call it again harmlessly.
     *  ui_render() is safe here because: (a) app_main IS a FreeRTOS task so the
     *  mutex works; (b) the display task hasn't been created yet so there is no
     *  concurrent renderer. */
    if (VK_BOARD_HAS_DISPLAY) {
        ui_init(&s_oled_ctx);
        /* Show BOOT splash immediately — first pixels on screen */
        ui_set_screen(UI_SCREEN_BOOT);
        ui_render(&s_oled_ctx);
    }

    /* 10. Offline journal — apenas se SD montou com sucesso */
    if (VK_BOARD_HAS_STORAGE && s_sd_ok) {
        offline_journal_init();
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
    /* Render immediately so the screen updates before WiFi init (which can take a
     * few hundred milliseconds before the display task gets its first CPU slice). */
    if (VK_BOARD_HAS_DISPLAY) {
        ui_render(&s_oled_ctx);
    }

    /* 12. Wi-Fi — inicializa ANTES da câmera para evitar conflito de cache
     *  (esp_wifi_init → NVS read → spi_flash_op_lock suspende ICACHE+DCACHE;
     *   câmera com DMA ativo durante essa janela causa panic_enable_cache → TG1WDT) */
    esp_err_t wifi_ret = wifi_manager_init();
    if (wifi_ret != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi nao pode ser iniciado (%s) — modo offline",
                 esp_err_to_name(wifi_ret));
        core_set_state(SYS_STATE_OFFLINE);
        ui_set_screen(UI_SCREEN_OFFLINE);
        if (VK_BOARD_HAS_DISPLAY) {
            ui_render(&s_oled_ctx);
        }
    }

    // /* 13. Câmera — inicializada APÓS WiFi */
    // if (VK_BOARD_HAS_CAMERA) {
    //     camera_driver_init();
    // } else {
    //     log_feature_skip("Camera OV5640");
    // }

    /* 14. Criar todas as tasks */
    create_tasks();

    /* app_main pode retornar — FreeRTOS scheduler assume o controle */
    ESP_LOGI(TAG, "Inicializacao concluida. Scheduler ativo.");
}
