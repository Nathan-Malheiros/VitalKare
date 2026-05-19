/**
 * @file system_manager.c
 * @brief Orquestrador do sistema VitalKare — implementação
 *
 * SEQUÊNCIA DE BOOT:
 *   Cada passo é tentado; falhas são registradas mas não bloqueiam
 *   subsistemas independentes. O grafo de dependências é:
 *
 *   nvs_flash → (obrigatório)
 *   sd_manager_init → marca SUBSYS_SD (opcional, não bloqueia outros)
 *   i2c_bus_init    → obrigatório para sensores/OLED/oxímetro
 *   audio_capture_init → depende de I2S (independente de I2C)
 *   audio_encoder_init → depende de audio_capture_init
 *   oled_init      → depende de i2c_bus_init
 *   wifi_manager_init → inicia Wi-Fi (assíncrono, evento chegará depois)
 *   tcp_client_init → registra filas, aguarda Wi-Fi conectar
 *   sensor_task_init → depende de i2c_bus_init
 *   max30102_init   → depende de i2c_bus_init
 *
 * MONITOR PASS (a cada 5s):
 *   1. task_monitor_get_faulted() — detectar stalls
 *   2. memory_monitor_refresh()   — snapshot de memória
 *   3. Escalada: stall ≥ REBOOT_THRESHOLD → system_manager_shutdown()
 *   4. task_monitor_heartbeat(TASK_ID_SYS_MANAGER) — reportar própria saúde
 */

#include "system_manager.h"
#include "task_monitor.h"
#include "memory_monitor.h"
#include "core.h"
#include "../events/event_dispatcher.h"
#include "../network/wifi_manager.h"
#include "../network/tcp_client.h"
#include "../audio/audio_capture.h"
#include "../audio/audio_encoder.h"
#include "../storage/sd_manager.h"
#include "../i2c_bus/i2c_bus.h"
#include "../sensors/sensor_task.h"
#include "../sensors/max30102.h"
#include "../sensors/hdc1080.h"
#include "../sensors/mpu6050.h"
#include "../display/oled_driver.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdatomic.h>

static const char *TAG = "SYS_MGR";

/* =========================================================
 * ESTADO INTERNO
 * ========================================================= */
static system_health_t  s_health;
static SemaphoreHandle_t s_mutex;
static int64_t           s_boot_time_us;

/* Contadores hot-path — atômicos para evitar mutex em caminhos críticos */
static _Atomic uint32_t s_dropped_packets = 0;
static _Atomic uint32_t s_audio_overruns  = 0;
static _Atomic uint32_t s_reconnect_count = 0;
static _Atomic uint32_t s_alloc_failures  = 0;

#define MONITOR_INTERVAL_MS   5000U
#define SHUTDOWN_WAIT_AUDIO_MS 3000U
#define SHUTDOWN_WAIT_TCP_MS   2000U

/* =========================================================
 * INICIALIZAÇÃO
 * ========================================================= */
esp_err_t system_manager_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "Falha ao criar mutex");
        return ESP_ERR_NO_MEM;
    }

    memset(&s_health, 0, sizeof(s_health));
    s_boot_time_us = esp_timer_get_time();

    task_monitor_init();
    memory_monitor_init();

    ESP_LOGI(TAG, "system_manager inicializado");
    return ESP_OK;
}

/* =========================================================
 * SEQUÊNCIA DE BOOT
 * ========================================================= */
esp_err_t system_manager_boot_sequence(void)
{
    esp_err_t ret;

    /* ── NVS (obrigatório) ─────────────────────────────── */
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS: erasando e reinicializando");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init falhou: %s — sistema não pode continuar", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "NVS: ok");

    /* ── core_init (contexto global) ──────────────────── */
    ret = core_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "core_init falhou: %s", esp_err_to_name(ret));
        return ret;
    }

    /* ── event_dispatcher_init ─────────────────────────── */
    ret = event_dispatcher_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "event_dispatcher_init falhou: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Event dispatcher: ok");

    /* ── SD card (opcional — degradado se falhar) ───────── */
    ret = sd_manager_init();
    system_manager_report_subsystem(SUBSYS_SD, ret == ESP_OK);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SD: indisponível (%s) — modo offline desativado", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "SD: ok");
    }

    /* ── I2C bus (necessário para sensores + OLED) ───────── */
    ret = i2c_bus_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "I2C: falhou (%s) — sensores/OLED desativados", esp_err_to_name(ret));
        system_manager_report_subsystem(SUBSYS_TEMP,    false);
        system_manager_report_subsystem(SUBSYS_OXIMETER, false);
        system_manager_report_subsystem(SUBSYS_DISPLAY,  false);
    } else {
        ESP_LOGI(TAG, "I2C: ok");

        /* OLED */
        /* oled_init() requer contexto externo — será chamado por task_display_update */

        /* HDC1080 + MPU6050 */
        ret = hdc1080_init();
        system_manager_report_subsystem(SUBSYS_TEMP, ret == ESP_OK);
        if (ret != ESP_OK) ESP_LOGW(TAG, "HDC1080: falhou (%s)", esp_err_to_name(ret));

        /* MAX30102 */
        ret = max30102_init();
        system_manager_report_subsystem(SUBSYS_OXIMETER, ret == ESP_OK);
        if (ret != ESP_OK) ESP_LOGW(TAG, "MAX30102: falhou (%s)", esp_err_to_name(ret));
    }

    /* ── Áudio (I2S PDM — independente de I2C) ─────────── */
    ret = audio_capture_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "audio_capture: falhou (%s)", esp_err_to_name(ret));
        system_manager_report_subsystem(SUBSYS_AUDIO, false);
    } else {
        ret = audio_encoder_init();
        system_manager_report_subsystem(SUBSYS_AUDIO, ret == ESP_OK);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "audio_encoder: falhou (%s)", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "Áudio: ok");
        }
    }

    /* ── Sensor task init ───────────────────────────────── */
    ret = sensor_task_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "sensor_task_init: falhou (%s)", esp_err_to_name(ret));
    }

    /* ── TCP client init (filas, sem conectar ainda) ────── */
    ret = tcp_client_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "tcp_client_init falhou: %s", esp_err_to_name(ret));
        system_manager_report_subsystem(SUBSYS_TCP, false);
    } else {
        ESP_LOGI(TAG, "TCP client: filas criadas");
    }

    /* ── Wi-Fi (inicia stack assíncrona) ────────────────── */
    ret = wifi_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "wifi_manager_init falhou: %s", esp_err_to_name(ret));
        system_manager_report_subsystem(SUBSYS_WIFI, false);
        core_set_state(SYS_STATE_OFFLINE);
    } else {
        ESP_LOGI(TAG, "Wi-Fi: iniciando (aguardando evento)");
        core_set_state(SYS_STATE_WIFI_CONNECTING);
    }

    ESP_LOGI(TAG, "Boot sequence completa");
    return ESP_OK;
}

/* =========================================================
 * RELATÓRIOS DE SUBSISTEMA E FALHAS
 * ========================================================= */
void system_manager_report_subsystem(subsystem_id_t id, bool ok)
{
    if (!s_mutex) return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;

    switch (id) {
        case SUBSYS_WIFI:     s_health.wifi_ok      = ok; break;
        case SUBSYS_TCP:      s_health.tcp_ok        = ok; break;
        case SUBSYS_AUDIO:    s_health.audio_ok      = ok; break;
        case SUBSYS_SD:       s_health.sd_ok         = ok; break;
        case SUBSYS_OXIMETER: s_health.oximeter_ok   = ok; break;
        case SUBSYS_TEMP:     s_health.temp_sensor_ok= ok; break;
        case SUBSYS_DISPLAY:  s_health.display_ok    = ok; break;
        case SUBSYS_JOURNAL:  s_health.journal_ok    = ok; break;
        default: break;
    }

    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "Subsys[%d] = %s", id, ok ? "OK" : "FAIL");
}

void system_manager_report_fault(subsystem_id_t subsys, const char *reason)
{
    ESP_LOGE(TAG, "FAULT subsys=%d: %s", (int)subsys, reason ? reason : "?");
    system_manager_report_subsystem(subsys, false);
}

/* =========================================================
 * CONTADORES HOT-PATH (operações atômicas)
 * ========================================================= */
void system_manager_inc_dropped_packets(void)
{
    atomic_fetch_add(&s_dropped_packets, 1);
}

void system_manager_inc_audio_overrun(void)
{
    atomic_fetch_add(&s_audio_overruns, 1);
}

void system_manager_inc_reconnect(void)
{
    atomic_fetch_add(&s_reconnect_count, 1);
}

/* =========================================================
 * LEITURA DO HEALTH MODEL
 * ========================================================= */
void system_manager_get_health(system_health_t *out)
{
    if (!out || !s_mutex) return;

    /* Flush contadores atômicos para a struct antes de copiar */
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_health.dropped_packets  = atomic_load(&s_dropped_packets);
        s_health.audio_overruns   = atomic_load(&s_audio_overruns);
        s_health.reconnect_count  = atomic_load(&s_reconnect_count);
        s_health.alloc_failures   = atomic_load(&s_alloc_failures);
        s_health.uptime_sec       = system_manager_get_uptime_sec();

        *out = s_health;
        xSemaphoreGive(s_mutex);
    }
}

uint32_t system_manager_get_uptime_sec(void)
{
    int64_t elapsed_us = esp_timer_get_time() - s_boot_time_us;
    return (uint32_t)(elapsed_us / 1000000LL);
}

/* =========================================================
 * SHUTDOWN ORDENADO
 * ========================================================= */
void system_manager_shutdown(const char *reason)
{
    ESP_LOGW(TAG, "SHUTDOWN iniciado: %s", reason ? reason : "?");

    /* 1. Parar áudio */
    audio_ctrl_stop();
    vTaskDelay(pdMS_TO_TICKS(SHUTDOWN_WAIT_AUDIO_MS));

    /* 2. Aguardar TCP drenar (sem novo connect) */
    vTaskDelay(pdMS_TO_TICKS(SHUTDOWN_WAIT_TCP_MS));

    /* 3. SD desmontado implicitamente pelo FATFS ao reiniciar */

    ESP_LOGW(TAG, "Reiniciando dispositivo...");
    esp_restart();
}

/* =========================================================
 * TASK DO SYSTEM MANAGER — monitor pass periódico
 * ========================================================= */
void task_sys_manager(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "task_sys_manager iniciada");

    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        task_monitor_heartbeat(TASK_ID_SYS_MANAGER);

        /* ── 1. Detectar stalls de tasks ───────────────── */
        task_id_t faulted[TASK_ID_COUNT];
        uint8_t   faulted_count = 0;
        task_monitor_get_faulted(faulted, &faulted_count);

        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            s_health.faulted_task_count = faulted_count;
            s_health.any_task_faulted   = (faulted_count > 0);
            xSemaphoreGive(s_mutex);
        }

        for (uint8_t i = 0; i < faulted_count; i++) {
            uint32_t stall = task_monitor_stall_ms(faulted[i]);
            ESP_LOGE(TAG, "Task '%s' stalled %lu ms",
                     task_monitor_name(faulted[i]), (unsigned long)stall);

            if (stall >= TASK_MONITOR_REBOOT_THRESHOLD_MS) {
                char reason[64];
                snprintf(reason, sizeof(reason), "Task '%s' stalled %lu ms",
                         task_monitor_name(faulted[i]), (unsigned long)stall);
                system_manager_shutdown(reason);
                /* Não retorna — esp_restart() chamado dentro */
            }
        }

        /* ── 2. Snapshot de memória ─────────────────────── */
        memory_monitor_refresh();

        /* ── 3. Sincronizar contadores atômicos → health ── */
        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            s_health.dropped_packets = atomic_load(&s_dropped_packets);
            s_health.audio_overruns  = atomic_load(&s_audio_overruns);
            s_health.reconnect_count = atomic_load(&s_reconnect_count);
            s_health.alloc_failures  = atomic_load(&s_alloc_failures);
            s_health.uptime_sec      = system_manager_get_uptime_sec();
            xSemaphoreGive(s_mutex);
        }

        /* ── 4. Log periódico de saúde ──────────────────── */
        memory_snapshot_t mem;
        memory_monitor_get_snapshot(&mem);
        ESP_LOGD(TAG, "Uptime=%lu s heap=%lu B psram=%lu B tasks_faulted=%d",
                 (unsigned long)system_manager_get_uptime_sec(),
                 (unsigned long)mem.heap_free_bytes,
                 (unsigned long)mem.psram_free_bytes,
                 faulted_count);

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(MONITOR_INTERVAL_MS));
    }
}
