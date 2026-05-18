/**
 * @file core.c
 * @brief Implementação do gerenciamento de estado global do VitalKare
 */

#include "core.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "../../config/app_config.h"
#include <string.h>

static const char *TAG = "CORE";

static system_context_t s_ctx = {0};

static const char *state_names[] = {
    [SYS_STATE_BOOT]             = "BOOT",
    [SYS_STATE_WIFI_CONNECTING]  = "WIFI_CONNECTING",
    [SYS_STATE_HTTP_READY]       = "HTTP_READY",
    [SYS_STATE_OPERATIONAL]      = "OPERATIONAL",
    [SYS_STATE_EMERGENCY]        = "EMERGENCY",
    [SYS_STATE_RECORDING]        = "RECORDING",
    [SYS_STATE_OFFLINE]          = "OFFLINE",
    [SYS_STATE_ERROR]            = "ERROR",
};

/* =========================================================
 * INICIALIZAÇÃO
 * ========================================================= */
esp_err_t core_init(void)
{
    s_ctx.mutex = xSemaphoreCreateMutex();
    if (s_ctx.mutex == NULL) {
        ESP_LOGE(TAG, "Falha ao criar mutex do contexto global");
        return ESP_ERR_NO_MEM;
    }

    s_ctx.state = SYS_STATE_BOOT;
    memset(&s_ctx.connectivity, 0, sizeof(s_ctx.connectivity));

    strncpy(s_ctx.device.firmware_version, APP_FIRMWARE_VERSION,
            sizeof(s_ctx.device.firmware_version) - 1);
    strncpy(s_ctx.device.device_id, APP_DEFAULT_DEVICE_ID,
            sizeof(s_ctx.device.device_id) - 1);
    strncpy(s_ctx.device.ward_id, APP_DEFAULT_WARD_ID,
            sizeof(s_ctx.device.ward_id) - 1);
    strncpy(s_ctx.device.patient_id, APP_DEFAULT_PATIENT_ID,
            sizeof(s_ctx.device.patient_id) - 1);

    ESP_LOGI(TAG, "Core inicializado — device_id=%s ward=%s fw=%s",
             s_ctx.device.device_id,
             s_ctx.device.ward_id,
             s_ctx.device.firmware_version);

    return ESP_OK;
}

/* =========================================================
 * MÁQUINA DE ESTADOS
 * ========================================================= */
sys_state_t core_get_state(void)
{
    /* Leitura atômica — enum cabe em um word */
    return s_ctx.state;
}

void core_set_state(sys_state_t new_state)
{
    if (core_lock(pdMS_TO_TICKS(100))) {
        sys_state_t old_state = s_ctx.state;
        s_ctx.state = new_state;
        core_unlock();

        if (old_state != new_state) {
            ESP_LOGI(TAG, "Estado: %s → %s",
                     state_names[old_state],
                     state_names[new_state]);
        }
    } else {
        ESP_LOGW(TAG, "core_set_state: timeout ao adquirir mutex");
    }
}

/* =========================================================
 * ACESSO AO CONTEXTO
 * ========================================================= */
void core_get_context(system_context_t *out_ctx)
{
    if (core_lock(pdMS_TO_TICKS(100))) {
        memcpy(out_ctx, &s_ctx, sizeof(system_context_t));
        out_ctx->mutex = NULL; /* Não expor o mutex internamente */
        core_unlock();
    }
}

void core_update_connectivity(const connectivity_status_t *conn)
{
    if (core_lock(pdMS_TO_TICKS(100))) {
        memcpy(&s_ctx.connectivity, conn, sizeof(connectivity_status_t));
        core_unlock();
    }
}

const device_info_t *core_get_device_info(void)
{
    /* Chamador deve estar dentro de core_lock() */
    return &s_ctx.device;
}

/* =========================================================
 * MUTEX
 * ========================================================= */
bool core_lock(TickType_t timeout_ticks)
{
    return xSemaphoreTake(s_ctx.mutex, timeout_ticks) == pdTRUE;
}

void core_unlock(void)
{
    xSemaphoreGive(s_ctx.mutex);
}
