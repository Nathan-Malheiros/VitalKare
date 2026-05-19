/**
 * @file vk_reliability.c
 * @brief Camada de confiabilidade de pacotes — implementação
 *
 * PENDING TABLE:
 *   Array estático de VK_RELIABILITY_TABLE_SIZE slots.
 *   Cada slot: { frame[], len, qos, retries_left, last_attempt_us, active }
 *
 *   Operações:
 *   - Inserção: vk_reliability_send() — aloca slot livre, preenche, tenta envio imediato.
 *   - Varredura: task_vk_reliability() a cada SCAN_INTERVAL_MS —
 *     para cada slot ativo: se (now - last_attempt) ≥ RETRY_INTERVAL → re-tentar.
 *   - Limpeza: slot liberado quando tcp_client_send_*() retorna true (enviado ao kernel).
 *
 * RECONNECT LOGIC:
 *   vk_reliability_on_reconnect() varre slots GUARANTEED cujo last_attempt_us
 *   seja do passado (desconectado) e os re-tenta imediatamente.
 */

#include "vk_reliability.h"
#include "../core/system_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdatomic.h>

static const char *TAG = "VK_REL";

/* =========================================================
 * ESTADO INTERNO
 * ========================================================= */
typedef struct {
    uint8_t  frame[TCP_FRAME_BUF_SIZE];
    uint16_t len;
    vk_qos_t qos;
    uint8_t  retries_left;
    int64_t  last_attempt_us;
    bool     active;
} pending_slot_t;

static pending_slot_t  s_table[VK_RELIABILITY_TABLE_SIZE];
static SemaphoreHandle_t s_mutex;

/* =========================================================
 * HELPERS INTERNOS
 * ========================================================= */
static int alloc_slot(void)
{
    for (int i = 0; i < VK_RELIABILITY_TABLE_SIZE; i++) {
        if (!s_table[i].active) return i;
    }
    return -1;
}

static void free_slot(int idx)
{
    s_table[idx].active = false;
}

static bool try_send_slot(int idx)
{
    bool sent;
    if (s_table[idx].qos == VK_QOS_GUARANTEED) {
        sent = tcp_client_send_critical(s_table[idx].frame, s_table[idx].len);
    } else {
        sent = tcp_client_send_critical(s_table[idx].frame, s_table[idx].len);
    }
    s_table[idx].last_attempt_us = esp_timer_get_time();
    return sent;
}

/* =========================================================
 * IMPLEMENTAÇÃO
 * ========================================================= */
esp_err_t vk_reliability_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;
    memset(s_table, 0, sizeof(s_table));
    ESP_LOGI(TAG, "Inicializado (tabela=%d slots)", VK_RELIABILITY_TABLE_SIZE);
    return ESP_OK;
}

esp_err_t vk_reliability_send(const uint8_t *frame, uint16_t len, vk_qos_t qos)
{
    if (!frame || len == 0 || len > TCP_FRAME_BUF_SIZE) return ESP_ERR_INVALID_ARG;

    /* Fire-and-forget: sem tabela */
    if (qos == VK_QOS_FIRE_AND_FORGET) {
        if (!tcp_client_send_normal(frame, len)) {
            system_manager_inc_dropped_packets();
        }
        return ESP_OK;
    }

    /* RETRY / GUARANTEED: alocar slot */
    TickType_t deadline = xTaskGetTickCount() + VK_RELIABILITY_BLOCK_TICKS;
    int slot = -1;

    while (true) {
        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            slot = alloc_slot();
            if (slot >= 0) break;
            xSemaphoreGive(s_mutex);
        }
        if (xTaskGetTickCount() >= deadline) {
            /* Tabela cheia — degradar */
            ESP_LOGW(TAG, "Tabela cheia — degradando QoS=%d para fire-and-forget", qos);
            system_manager_inc_dropped_packets();
            if (!tcp_client_send_critical(frame, len)) {
                system_manager_inc_dropped_packets();
            }
            return (qos == VK_QOS_GUARANTEED) ? ESP_ERR_NO_MEM : ESP_OK;
        }
        taskYIELD();
    }

    /* Preencher slot (mutex já obtido) */
    memcpy(s_table[slot].frame, frame, len);
    s_table[slot].len           = len;
    s_table[slot].qos           = qos;
    s_table[slot].retries_left  = (qos == VK_QOS_GUARANTEED)
                                  ? UINT8_MAX
                                  : VK_RELIABILITY_MAX_RETRIES;
    s_table[slot].last_attempt_us = 0LL;
    s_table[slot].active        = true;

    bool sent = try_send_slot(slot);
    if (sent) {
        /* RETRY: entregue ao kernel TCP — liberamos slot */
        if (qos == VK_QOS_RETRY) {
            free_slot(slot);
        }
        /* GUARANTEED: mantemos no slot até reconexão confirmar */
        else {
            /* Slot marcado como enviado — ao reconectar re-tentamos */
            free_slot(slot);  /* Se enviou com sucesso, remove da tabela */
        }
    }

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

void vk_reliability_on_reconnect(void)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) != pdTRUE) return;

    int re_enqueued = 0;
    for (int i = 0; i < VK_RELIABILITY_TABLE_SIZE; i++) {
        if (!s_table[i].active) continue;
        if (s_table[i].qos != VK_QOS_GUARANTEED) continue;

        bool sent = try_send_slot(i);
        if (sent) {
            free_slot(i);
            re_enqueued++;
        }
    }

    xSemaphoreGive(s_mutex);

    if (re_enqueued > 0) {
        ESP_LOGI(TAG, "Reconexão: %d frames GUARANTEED re-enfileirados", re_enqueued);
    }
}

void vk_reliability_on_disconnect(void)
{
    /* RETRY frames expirados após desconexão são descartados no próximo scan */
    ESP_LOGD(TAG, "Desconectado — RETRY frames podem expirar");
}

uint8_t vk_reliability_pending_count(void)
{
    uint8_t count = 0;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        for (int i = 0; i < VK_RELIABILITY_TABLE_SIZE; i++) {
            if (s_table[i].active) count++;
        }
        xSemaphoreGive(s_mutex);
    }
    return count;
}

/* =========================================================
 * TASK SCANNER
 * ========================================================= */
void task_vk_reliability(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "task_vk_reliability iniciada");

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(VK_RELIABILITY_SCAN_INTERVAL_MS));

        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) continue;

        int64_t now_us = esp_timer_get_time();

        for (int i = 0; i < VK_RELIABILITY_TABLE_SIZE; i++) {
            if (!s_table[i].active) continue;

            int64_t age_ms = (now_us - s_table[i].last_attempt_us) / 1000LL;

            if (age_ms < (int64_t)VK_RELIABILITY_RETRY_INTERVAL_MS) continue;

            /* Timeout de retry */
            if (s_table[i].retries_left == 0) {
                /* RETRY esgotado */
                ESP_LOGW(TAG, "Slot %d: retries esgotados — descartando", i);
                system_manager_inc_dropped_packets();
                free_slot(i);
                continue;
            }

            bool sent = try_send_slot(i);
            if (sent) {
                if (s_table[i].qos == VK_QOS_GUARANTEED) {
                    /* Enviado com sucesso — remove */
                    free_slot(i);
                } else {
                    /* RETRY bem-sucedido */
                    free_slot(i);
                }
            } else {
                if (s_table[i].retries_left != UINT8_MAX) {
                    s_table[i].retries_left--;
                }
                ESP_LOGD(TAG, "Slot %d: re-tentativa falhou (%d restantes)",
                         i, s_table[i].retries_left);
            }
        }

        xSemaphoreGive(s_mutex);
    }
}
