/**
 * @file audio_capture.c
 * @brief Captura de áudio PDM via I2S — DMA Streaming com Chunk Pool
 *
 * ARQUITETURA:
 *   - Pool de APP_AUDIO_CHUNK_POOL_COUNT buffers int16_t pré-alocados na PSRAM
 *   - DMA lê chunks de APP_AUDIO_CHUNK_BYTES bytes por vez (não-bloqueante)
 *   - Cada chunk é postado na fila do audio_encoder
 *   - O encoder devolve o buffer ao pool via audio_chunk_pool_release()
 *   - Timeout de 10 s via esp_timer — sem vTaskDelay
 *   - Stop por edge (liberação do botão) via audio_ctrl_stop()
 *
 * GPIO: CLK=GPIO2, DATA=GPIO37, I2S_NUM_0, PDM RX, 16kHz/16bit/mono
 */

#include "audio_capture.h"
#include "audio_encoder.h"
#include "driver/i2s_pdm.h"
#include "driver/i2s_common.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "../../config/hardware_config.h"
#include "../../config/app_config.h"
#include "../events/event_dispatcher.h"
#include "../core/system_manager.h"
#include "../core/task_monitor.h"
#include "../core/vk_assert.h"
#include <string.h>
#include <stdatomic.h>

static const char *TAG = "AUDIO";

/* =========================================================
 * CHUNK POOL — pré-alocado na PSRAM
 * ========================================================= */
#define POOL_COUNT  APP_AUDIO_CHUNK_POOL_COUNT
#define CHUNK_BYTES APP_AUDIO_CHUNK_BYTES
#define CHUNK_SAMPLES (CHUNK_BYTES / 2)     /* int16_t por chunk */

static int16_t  *s_pool_bufs[POOL_COUNT];   /* ponteiros para blocos PSRAM */
static QueueHandle_t s_pool_queue = NULL;   /* pool de buffers disponíveis */

static esp_err_t pool_init(void)
{
    s_pool_queue = xQueueCreate(POOL_COUNT, sizeof(int16_t *));
    if (!s_pool_queue) return ESP_ERR_NO_MEM;

    for (int i = 0; i < POOL_COUNT; i++) {
        s_pool_bufs[i] = heap_caps_malloc(CHUNK_BYTES,
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_pool_bufs[i]) {
            ESP_LOGE(TAG, "Falha ao alocar chunk %d na PSRAM", i);
            return ESP_ERR_NO_MEM;
        }
        int16_t *p = s_pool_bufs[i];
        xQueueSend(s_pool_queue, &p, 0);
    }
    ESP_LOGI(TAG, "Chunk pool: %d x %d bytes na PSRAM", POOL_COUNT, CHUNK_BYTES);
    return ESP_OK;
}

static int16_t *pool_acquire(TickType_t timeout)
{
    int16_t *buf = NULL;
    xQueueReceive(s_pool_queue, &buf, timeout);
    return buf;
}

void audio_chunk_pool_release(int16_t *buf)
{
    if (buf && s_pool_queue) {
        xQueueSend(s_pool_queue, &buf, 0);
    }
}

uint8_t audio_chunk_pool_available(void)
{
    if (!s_pool_queue) return 0;
    UBaseType_t n = uxQueueMessagesWaiting(s_pool_queue);
    return (n > 255u) ? 255u : (uint8_t)n;
}

QueueHandle_t audio_chunk_pool_get_queue(void)
{
    return s_pool_queue;
}

/* =========================================================
 * ESTADO INTERNO
 * ========================================================= */
static i2s_chan_handle_t    s_rx_chan          = NULL;
static bool                 s_initialized      = false;
static SemaphoreHandle_t    s_mutex            = NULL;
static _Atomic audio_state_t s_state           = AUDIO_STATE_IDLE;
static volatile audio_trigger_t s_trigger      = AUDIO_TRIGGER_REQUEST;
static esp_timer_handle_t   s_timeout_timer    = NULL;
static uint32_t             s_session_id       = 0;
static int64_t              s_last_start_us    = 0;   /**< Debounce timestamp */

/* Notificação de parada para a task de captura */
static TaskHandle_t s_ctrl_task_handle = NULL;
#define NOTIFY_STOP_BIT     BIT(0)
#define NOTIFY_START_BIT    BIT(1)

/* =========================================================
 * TIMER DE TIMEOUT (10 s)
 * ========================================================= */
static void timeout_timer_cb(void *arg)
{
    audio_state_t expected = AUDIO_STATE_RECORDING;
    if (atomic_compare_exchange_strong(&s_state, &expected, AUDIO_STATE_STOPPING)) {
        ESP_LOGW(TAG, "Timeout de gravacao — parando automaticamente");
        if (s_ctrl_task_handle) {
            BaseType_t woken = pdFALSE;
            xTaskNotifyFromISR(s_ctrl_task_handle, NOTIFY_STOP_BIT,
                               eSetBits, &woken);
            portYIELD_FROM_ISR(woken);
        }
    }
}

/* =========================================================
 * INICIALIZAÇÃO
 * ========================================================= */
esp_err_t audio_capture_init(void)
{
    if (s_initialized) return ESP_OK;

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    esp_err_t ret = pool_init();
    if (ret != ESP_OK) return ret;

    /* Canal RX */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(HW_I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;

    ret = i2s_new_channel(&chan_cfg, NULL, &s_rx_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Configuração PDM RX */
    i2s_pdm_rx_config_t pdm_cfg = {
        .clk_cfg  = I2S_PDM_RX_CLK_DEFAULT_CONFIG(HW_AUDIO_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                     I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk  = HW_AUDIO_CLK_GPIO,
            .din  = HW_AUDIO_DATA_GPIO,
            .invert_flags = { .clk_inv = false },
        },
    };

    ret = i2s_channel_init_pdm_rx_mode(s_rx_chan, &pdm_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_pdm_rx_mode: %s", esp_err_to_name(ret));
        i2s_del_channel(s_rx_chan);
        s_rx_chan = NULL;
        return ret;
    }

    /* Timer de timeout — criado uma vez, reutilizado por sessão */
    esp_timer_create_args_t timer_args = {
        .callback = timeout_timer_cb,
        .name     = "audio_timeout",
    };
    esp_timer_create(&timer_args, &s_timeout_timer);

    s_initialized = true;
    ESP_LOGI(TAG, "Audio PDM inicializado (%d Hz, CLK=%d, DATA=%d)",
             HW_AUDIO_SAMPLE_RATE_HZ, HW_AUDIO_CLK_GPIO, HW_AUDIO_DATA_GPIO);
    return ESP_OK;
}

/* =========================================================
 * CONTROLE DE SESSÃO (thread-safe)
 * ========================================================= */
esp_err_t audio_ctrl_start(audio_trigger_t trigger)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    /* --- Verifica debounce e transiciona atomicamente IDLE→ARMED --- */
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    /* Debounce: rejeita chamadas dentro de APP_AUDIO_DEBOUNCE_MS */
    int64_t now_us = esp_timer_get_time();
    if ((now_us - s_last_start_us) < (int64_t)(APP_AUDIO_DEBOUNCE_MS * 1000LL)) {
        xSemaphoreGive(s_mutex);
        ESP_LOGD(TAG, "audio_ctrl_start ignorado — debounce ativo (%lld ms desde ultimo)",
                 (long long)((now_us - s_last_start_us) / 1000));
        return ESP_ERR_INVALID_STATE;
    }

    audio_state_t expected = AUDIO_STATE_IDLE;
    if (!atomic_compare_exchange_strong(&s_state, &expected, AUDIO_STATE_ARMED)) {
        xSemaphoreGive(s_mutex);
        ESP_LOGW(TAG, "audio_ctrl_start: estado atual=%d — nao IDLE", (int)expected);
        return ESP_ERR_INVALID_STATE;
    }

    s_trigger       = trigger;
    s_session_id++;
    s_last_start_us = now_us;
    xSemaphoreGive(s_mutex);

    if (s_ctrl_task_handle) {
        xTaskNotify(s_ctrl_task_handle, NOTIFY_START_BIT, eSetBits);
    }
    ESP_LOGI(TAG, "Gravacao ARMED — sessao=%lu trigger=%d",
             (unsigned long)s_session_id, trigger);
    return ESP_OK;
}

esp_err_t audio_ctrl_stop(void)
{
    audio_state_t cur = atomic_load(&s_state);

    /* Válido apenas em ARMED ou RECORDING */
    if (cur != AUDIO_STATE_ARMED && cur != AUDIO_STATE_RECORDING) {
        VK_ASSERT_WARN(false,
            "audio_ctrl_stop: invalid state %d (expected ARMED or RECORDING)",
            (int)cur);
        return ESP_ERR_INVALID_STATE;
    }

    /* CAS: tenta ARMED→STOPPING ou RECORDING→STOPPING */
    audio_state_t expected = AUDIO_STATE_ARMED;
    if (!atomic_compare_exchange_strong(&s_state, &expected, AUDIO_STATE_STOPPING)) {
        expected = AUDIO_STATE_RECORDING;
        atomic_compare_exchange_strong(&s_state, &expected, AUDIO_STATE_STOPPING);
    }

    if (s_ctrl_task_handle) {
        xTaskNotify(s_ctrl_task_handle, NOTIFY_STOP_BIT, eSetBits);
    }
    ESP_LOGI(TAG, "Gravacao STOPPING solicitado");
    return ESP_OK;
}

audio_state_t audio_ctrl_get_state(void)
{
    return atomic_load(&s_state);
}

/* =========================================================
 * TASK DE CONTROLE — DMA STREAMING LOOP
 * ========================================================= */
void task_audio_ctrl(void *arg)
{
    s_ctrl_task_handle = xTaskGetCurrentTaskHandle();
    QueueHandle_t enc_queue = audio_encoder_get_queue();
    ESP_LOGI(TAG, "task_audio_ctrl iniciada");

    for (;;) {
        task_monitor_heartbeat(TASK_ID_AUDIO_CTRL);
        /* Aguarda sinal de início (notificação do event handler) */
        uint32_t notif = 0;
        xTaskNotifyWait(0, UINT32_MAX, &notif, portMAX_DELAY);

        if (!(notif & NOTIFY_START_BIT)) continue;

        /* Verifica que ainda estamos em ARMED (não houve stop entre o notify e agora) */
        audio_state_t arm_expected = AUDIO_STATE_ARMED;
        if (!atomic_compare_exchange_strong(&s_state, &arm_expected, AUDIO_STATE_RECORDING)) {
            VK_ASSERT_FSM(false,
                "NOTIFY_START: expected ARMED got %d — concurrent stop/timeout race?",
                (int)arm_expected);
            atomic_store(&s_state, AUDIO_STATE_IDLE);
            continue;
        }

        /* Habilita canal I2S */
        esp_err_t i2s_ret = i2s_channel_enable(s_rx_chan);
        if (i2s_ret != ESP_OK) {
            ESP_LOGE(TAG, "i2s_channel_enable falhou: %s — entrando em ERROR",
                     esp_err_to_name(i2s_ret));
            atomic_store(&s_state, AUDIO_STATE_ERROR);
            goto error_recovery;
        }

        /* Inicia timer de 10 s */
        esp_timer_stop(s_timeout_timer);
        esp_timer_start_once(s_timeout_timer,
                             (uint64_t)APP_AUDIO_MAX_DURATION_MS * 1000ULL);

        uint32_t local_session   = s_session_id;
        audio_trigger_t local_trigger = s_trigger;
        uint16_t chunk_index = 0;
        int i2s_errors = 0;
        const int I2S_ERROR_LIMIT = 5;  /* erros consecutivos antes de entrar em ERROR */

        ESP_LOGI(TAG, "DMA loop iniciado (sessao=%lu)", (unsigned long)local_session);

        /* ── DMA CHUNK LOOP ── */
        while (atomic_load(&s_state) == AUDIO_STATE_RECORDING) {
            /* Tenta obter um buffer do pool (timeout de 50 ms) */
            int16_t *buf = pool_acquire(pdMS_TO_TICKS(50));
            if (!buf) {
                ESP_LOGW(TAG, "Pool esgotado — encoder lento, incrementando overrun");
                system_manager_inc_audio_overrun();
                continue;
            }

            /* Lê exatamente CHUNK_BYTES do DMA (timeout 200 ms) */
            size_t bytes_read = 0;
            esp_err_t ret = i2s_channel_read(s_rx_chan, buf, CHUNK_BYTES,
                                             &bytes_read, pdMS_TO_TICKS(200));

            if (ret != ESP_OK || bytes_read == 0) {
                audio_chunk_pool_release(buf);
                i2s_errors++;
                if (i2s_errors >= I2S_ERROR_LIMIT) {
                    ESP_LOGE(TAG, "DMA: %d erros consecutivos — entrando em ERROR",
                             i2s_errors);
                    atomic_store(&s_state, AUDIO_STATE_ERROR);
                }
                continue;
            }
            i2s_errors = 0;  /* reset contador a cada leitura OK */

            audio_chunk_t chunk = {
                .session_id  = local_session,
                .chunk_index = chunk_index++,
                .pcm_samples = (uint16_t)(bytes_read / 2),
                .pcm_data    = buf,
                .is_last     = false,
                .trigger     = (uint8_t)local_trigger,
            };

            if (xQueueSend(enc_queue, &chunk, pdMS_TO_TICKS(20)) != pdTRUE) {
                ESP_LOGW(TAG, "Fila do encoder cheia — chunk %u descartado",
                         chunk.chunk_index);
                audio_chunk_pool_release(buf);
                system_manager_inc_audio_overrun();
            }
        }

        /* ── FIM DA SESSÃO ── */
        esp_timer_stop(s_timeout_timer);
        i2s_channel_disable(s_rx_chan);

        /* Sinaliza último chunk no encoder */
        int16_t *flush_buf = pool_acquire(pdMS_TO_TICKS(100));
        if (flush_buf) {
            audio_chunk_t last = {
                .session_id  = local_session,
                .chunk_index = chunk_index,
                .pcm_samples = 0,
                .pcm_data    = flush_buf,
                .is_last     = true,
                .trigger     = (uint8_t)local_trigger,
            };
            xQueueSend(enc_queue, &last, pdMS_TO_TICKS(100));
        }

        /* Publica evento de sessão encerrada */
        system_event_t evt = {
            .type         = EVT_AUDIO_SESSION_DONE,
            .priority     = EVT_PRIO_MEDIUM,
            .timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS,
            .data_len     = 0,
        };
        event_post_normal(&evt);

        atomic_store(&s_state, AUDIO_STATE_IDLE);
        ESP_LOGI(TAG, "Sessao %lu encerrada (%u chunks)",
                 (unsigned long)local_session, chunk_index);
        continue;

        /* ---- RECUPERAÇÃO DE ERRO ---- */
error_recovery:
        esp_timer_stop(s_timeout_timer);
        i2s_channel_disable(s_rx_chan);
        ESP_LOGE(TAG, "Erro de hardware — aguardando %d ms para recovery",
                 APP_AUDIO_ERROR_RECOVERY_MS);
        vTaskDelay(pdMS_TO_TICKS(APP_AUDIO_ERROR_RECOVERY_MS));
        atomic_store(&s_state, AUDIO_STATE_IDLE);
        s_last_start_us = esp_timer_get_time(); /* previne start imediato após erro */
        ESP_LOGW(TAG, "Recovery completo — voltando a IDLE");
    }
}

/* =========================================================
 * API LEGADA — stub para compatibilidade durante migração
 * ========================================================= */
esp_err_t audio_capture_start(void)
{
    return audio_ctrl_start(AUDIO_TRIGGER_REQUEST);
}

esp_err_t audio_capture_stop(audio_recording_t *out)
{
    if (out) { out->data = NULL; out->size_bytes = 0; }
    return audio_ctrl_stop();
}

void audio_capture_free(audio_recording_t *rec)
{
    if (rec && rec->data) {
        free(rec->data);
        rec->data       = NULL;
        rec->size_bytes = 0;
    }
}

void task_audio_capture(void *arg)
{
    /* Redireciona para a implementação nova */
    task_audio_ctrl(arg);
}

