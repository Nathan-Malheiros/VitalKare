/**
 * @file audio_encoder.c
 * @brief Task de encoding IMA ADPCM para protocolo VK
 *
 * Recebe audio_chunk_t da fila de chunks, codifica com IMA ADPCM,
 * monta o frame VK e envia para a fila normal do tcp_client.
 *
 * Para cada sessão:
 *   1. Ao receber o primeiro chunk: envia PKT_AUDIO_SESSION_START
 *   2. Para cada chunk:            envia PKT_AUDIO_CHUNK
 *   3. Ao receber is_last=true:    envia PKT_AUDIO_SESSION_END
 *      e libera o buffer do pool de volta para audio_capture.
 *
 * O buffer ADPCM de saída é alocado estaticamente no stack da task
 * (APP_AUDIO_CHUNK_BYTES / 2 = 2048 bytes) — zero malloc em runtime.
 */

#include "audio_encoder.h"
#include "adpcm.h"
#include "../network/vk_protocol.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "../../config/app_config.h"
#include "../../config/hardware_config.h"
#include <string.h>

#include "../network/tcp_client.h"
#include "../core/task_monitor.h"
#include "recording_recovery.h"

static const char *TAG = "AUDIO_ENC";

#define CHUNK_QUEUE_DEPTH   APP_AUDIO_CHUNK_QUEUE_DEPTH

static QueueHandle_t s_chunk_queue = NULL;

/* =========================================================
 * INIT
 * ========================================================= */
esp_err_t audio_encoder_init(void)
{
    s_chunk_queue = xQueueCreate(CHUNK_QUEUE_DEPTH, sizeof(audio_chunk_t));
    if (!s_chunk_queue) {
        ESP_LOGE(TAG, "Falha ao criar fila de chunks");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "audio_encoder inicializado (queue depth=%d)", CHUNK_QUEUE_DEPTH);
    return ESP_OK;
}

QueueHandle_t audio_encoder_get_queue(void)
{
    return s_chunk_queue;
}

/* =========================================================
 * TASK
 * ========================================================= */
void task_audio_encoder(void *arg)
{
    audio_chunk_t chunk;
    adpcm_state_t enc_state = {0};
    uint32_t      current_session = 0;
    uint16_t      seq = 0;
    bool          session_started = false;
    uint32_t      total_pcm_samples = 0;
    uint16_t      total_chunks = 0;

    /* Buffer ADPCM estático no stack da task — APP_AUDIO_CHUNK_BYTES/2 */
    static uint8_t s_adpcm_buf[APP_AUDIO_CHUNK_BYTES / 2];

    /* Buffer de frame VK — tamanho máximo de um chunk frame */
    static uint8_t s_frame_buf[VK_HEADER_SIZE
                                + sizeof(pkt_audio_chunk_header_t)
                                + (APP_AUDIO_CHUNK_BYTES / 2)
                                + VK_CRC_SIZE];

    ESP_LOGI(TAG, "task_audio_encoder iniciada");

    for (;;) {
        task_monitor_heartbeat(TASK_ID_AUDIO_ENC);
        if (xQueueReceive(s_chunk_queue, &chunk, pdMS_TO_TICKS(100)) != pdTRUE) {
            /* No new chunk — drive deferred emergency upload during encoder idle time.
             * PENDING: TCP just reconnected; open the file and start uploading.
             * UPLOADING: advance one chunk per idle tick (100 ms granularity). */
            rec_recovery_state_t rstate = recording_recovery_get_state();
            if (rstate == REC_RECOVERY_PENDING && tcp_client_is_connected()) {
                recording_recovery_on_tcp_connected();
            } else if (rstate == REC_RECOVERY_UPLOADING) {
                recording_recovery_upload_tick();
            }
            continue;
        }

        /* Nova sessão? Reinicia estado ADPCM e envia SESSION_START */
        if (chunk.session_id != current_session || !session_started) {
            current_session  = chunk.session_id;
            session_started  = true;
            total_pcm_samples = 0;
            total_chunks     = 0;
            adpcm_encoder_reset(&enc_state);

            /* EMERGENCY sessions: open SD file for dual-path persistence.
             * REQUEST sessions: no-op — TCP-only. */
            recording_recovery_session_begin(chunk.session_id,
                                             (audio_trigger_t)chunk.trigger);

            pkt_audio_session_start_t start = {
                .session_id      = chunk.session_id,
                .trigger         = chunk.trigger,
                .sample_rate_hz  = HW_MIC_SAMPLE_RATE_HZ,
                .bits_per_sample = 16,
                .codec           = 1,   /* IMA-ADPCM */
                .channels        = 1,
            };
            int flen = vk_frame_build(s_frame_buf, sizeof(s_frame_buf),
                                      PKT_AUDIO_SESSION_START, seq++,
                                      &start, sizeof(start));
            if (flen > 0) {
                tcp_client_send_normal(s_frame_buf, (uint16_t)flen);
            }
        }

        /* Codifica PCM → ADPCM */
        size_t adpcm_len = adpcm_encode(chunk.pcm_data, chunk.pcm_samples,
                                        s_adpcm_buf, &enc_state);

        total_pcm_samples += chunk.pcm_samples;
        total_chunks++;

        /* Monta payload do chunk: header + dados ADPCM */
        pkt_audio_chunk_header_t chunk_hdr = {
            .session_id  = chunk.session_id,
            .chunk_index = chunk.chunk_index,
            .chunk_len   = (uint16_t)adpcm_len,
        };

        /* Payload = chunk_hdr + adpcm_buf concatenados */
        static uint8_t s_payload_buf[sizeof(pkt_audio_chunk_header_t)
                                     + (APP_AUDIO_CHUNK_BYTES / 2)];
        memcpy(s_payload_buf, &chunk_hdr, sizeof(chunk_hdr));
        memcpy(s_payload_buf + sizeof(chunk_hdr), s_adpcm_buf, adpcm_len);

        int flen = vk_frame_build(s_frame_buf, sizeof(s_frame_buf),
                                  PKT_AUDIO_CHUNK, seq++,
                                  s_payload_buf,
                                  (uint32_t)(sizeof(chunk_hdr) + adpcm_len));
        if (flen > 0) {
            tcp_client_send_normal(s_frame_buf, (uint16_t)flen);
        }

        /* EMERGENCY dual-path: persist encoded chunk to SD regardless of TCP outcome.
         * The server deduplicates by (session_id, chunk_index) when the deferred
         * upload re-sends chunks that already arrived via the fast TCP path. */
        if ((audio_trigger_t)chunk.trigger == AUDIO_TRIGGER_EMERGENCY) {
            recording_recovery_submit_chunk(
                chunk.session_id, chunk.chunk_index,
                (audio_trigger_t)chunk.trigger,
                s_adpcm_buf, (uint16_t)adpcm_len,
                chunk.is_last);
        }

        ESP_LOGD(TAG, "Chunk %u: %u amostras PCM → %zu bytes ADPCM (sess=%lu)",
                 chunk.chunk_index, chunk.pcm_samples, adpcm_len,
                 (unsigned long)chunk.session_id);

        /* Libera buffer do chunk pool de volta para audio_capture */
        /* Sinalização via ponteiro — audio_capture detecta NULL na fila de pool */
        extern void audio_chunk_pool_release(int16_t *buf);
        audio_chunk_pool_release(chunk.pcm_data);

        /* Fim de sessão */
        if (chunk.is_last) {
            pkt_audio_session_end_t end_pkt = {
                .session_id        = chunk.session_id,
                .total_chunks      = total_chunks,
                .total_pcm_samples = total_pcm_samples,
            };
            int elen = vk_frame_build(s_frame_buf, sizeof(s_frame_buf),
                                      PKT_AUDIO_SESSION_END, seq++,
                                      &end_pkt, sizeof(end_pkt));
            if (elen > 0) {
                tcp_client_send_normal(s_frame_buf, (uint16_t)elen);
            }
            session_started = false;
            ESP_LOGI(TAG, "Sessão %lu encerrada: %u chunks, %lu amostras",
                     (unsigned long)chunk.session_id, total_chunks,
                     (unsigned long)total_pcm_samples);
        }
    }
}
