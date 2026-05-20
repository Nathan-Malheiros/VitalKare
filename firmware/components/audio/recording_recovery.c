/**
 * @file recording_recovery.c
 * @brief Emergency Audio Recording Recovery FSM — implementation
 *
 * SD CHUNK RECORD FORMAT (per chunk):
 *   Offset  Size  Field
 *   0       2     magic = 0xA5A5 (LE) — torn-write detection
 *   2       2     chunk_index (LE)
 *   4       2     adpcm_len (LE)
 *   6       N     adpcm_data[adpcm_len]
 *   6+N     2     CRC-16/CCITT over bytes 0..4+N-1
 *
 *   CRC covers magic + index + len + data.
 *   Written sequentially; upload reads linearly.
 *
 * UPLOAD SEQUENCE (per session):
 *   1. Send PKT_AUDIO_SESSION_START (reconstructed from NVS: session_id, trigger,
 *      constant sample_rate/codec/channels)
 *   2. For each chunk_index in [upload_cursor, chunk_cnt):
 *      a. Read record from SD file
 *      b. Validate CRC (skip on failure — at-least-once delivery, server deduplicates)
 *      c. Build PKT_AUDIO_CHUNK frame
 *      d. Send via tcp_client_send_critical()
 *      e. Advance upload_cursor + save to NVS
 *   3. Send PKT_AUDIO_SESSION_END
 *   4. Delete SD file, clear NVS, transition to IDLE
 *
 * OWNERSHIP:
 *   All functions run exclusively in task_audio_encoder context.
 *   No mutex required — single-task ownership throughout.
 */

#include "recording_recovery.h"
#include "../network/vk_protocol.h"
#include "../network/tcp_client.h"
#include "../storage/sd_manager.h"
#include "../core/vk_assert.h"
#include "../../config/app_config.h"
#include "../../config/hardware_config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>

static const char *TAG = "REC_REC";

/* =========================================================
 * NVS KEY NAMES
 * ========================================================= */
#define NVS_KEY_STATE      "state"
#define NVS_KEY_SESS_ID    "sess_id"
#define NVS_KEY_SESS_TRIG  "sess_trig"
#define NVS_KEY_CHUNK_CNT  "chunk_cnt"
#define NVS_KEY_UPLOAD_CUR "upload_cur"

/* =========================================================
 * INTERNAL STATE
 *
 * All state is owned by task_audio_encoder. No locking.
 * ========================================================= */
static rec_recovery_state_t  s_state         = REC_RECOVERY_IDLE;
static uint32_t              s_session_id    = 0;
static uint8_t               s_trigger       = AUDIO_TRIGGER_REQUEST;
static uint32_t              s_chunk_count   = 0;  /* Total chunks stored to SD */
static uint32_t              s_upload_cursor = 0;  /* Chunks already sent during upload */
static bool                  s_upload_session_started = false; /* SESSION_START sent? */

static nvs_handle_t  s_nvs_h      = 0;
static bool          s_nvs_ok     = false;
static bool          s_initialized = false;

/* Active write handle — open during ACTIVE state */
static FILE *s_write_fp  = NULL;

/* Upload read handle — open during UPLOADING state */
static FILE *s_upload_fp = NULL;

/* Static buffers owned by this module — kept off the encoder task stack.
 * Encoder stack is 8 KB; these buffers are 2+ KB each. */
static uint8_t s_adpcm_rd_buf[APP_AUDIO_CHUNK_BYTES / 2];

static uint8_t s_vk_chunk_frame[VK_HEADER_SIZE
                                 + sizeof(pkt_audio_chunk_header_t)
                                 + (APP_AUDIO_CHUNK_BYTES / 2)
                                 + VK_CRC_SIZE];

static uint8_t s_vk_ctrl_frame[VK_HEADER_SIZE
                                + sizeof(pkt_audio_session_start_t)
                                + VK_CRC_SIZE];

/* =========================================================
 * INTERNAL: CRC-16/CCITT (poly 0x1021, init 0xFFFF)
 * Matches vk_protocol.c — duplicated to avoid circular dependency.
 * ========================================================= */
static uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000u)
                ? (uint16_t)((crc << 1) ^ 0x1021u)
                : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/* =========================================================
 * INTERNAL: SD FILE PATH
 * Path: /sdcard/audio/SSSSSSSS.bin  (8-char zero-padded hex session_id)
 * ========================================================= */
static void session_file_path(char *buf, size_t bufsz, uint32_t session_id)
{
    snprintf(buf, bufsz, "%s/%08lX.bin",
             REC_RECOVERY_SD_DIR, (unsigned long)session_id);
}

/* =========================================================
 * INTERNAL: NVS HELPERS
 * ========================================================= */
static void nvs_persist_state(void)
{
    if (!s_nvs_ok) return;

    nvs_set_u8 (s_nvs_h, NVS_KEY_STATE,      (uint8_t)s_state);
    nvs_set_u32(s_nvs_h, NVS_KEY_SESS_ID,    s_session_id);
    nvs_set_u8 (s_nvs_h, NVS_KEY_SESS_TRIG,  s_trigger);
    nvs_set_u32(s_nvs_h, NVS_KEY_CHUNK_CNT,  s_chunk_count);
    nvs_set_u32(s_nvs_h, NVS_KEY_UPLOAD_CUR, s_upload_cursor);
    esp_err_t e = nvs_commit(s_nvs_h);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "NVS commit failed: %s", esp_err_to_name(e));
    }
}

/* Persist only the upload cursor (hot path — called per uploaded chunk) */
static void nvs_persist_upload_cursor(void)
{
    if (!s_nvs_ok) return;
    nvs_set_u32(s_nvs_h, NVS_KEY_UPLOAD_CUR, s_upload_cursor);
    nvs_commit(s_nvs_h);  /* Best-effort; failure means cursor regresses on crash */
}

static void nvs_load_state(void)
{
    if (!s_nvs_ok) return;

    uint8_t  st  = 0; nvs_get_u8 (s_nvs_h, NVS_KEY_STATE,      &st);
    uint32_t sid = 0; nvs_get_u32(s_nvs_h, NVS_KEY_SESS_ID,    &sid);
    uint8_t  trg = 0; nvs_get_u8 (s_nvs_h, NVS_KEY_SESS_TRIG,  &trg);
    uint32_t cnt = 0; nvs_get_u32(s_nvs_h, NVS_KEY_CHUNK_CNT,  &cnt);
    uint32_t cur = 0; nvs_get_u32(s_nvs_h, NVS_KEY_UPLOAD_CUR, &cur);

    s_state         = (rec_recovery_state_t)st;
    s_session_id    = sid;
    s_trigger       = trg;
    s_chunk_count   = cnt;
    s_upload_cursor = cur;
}

static void nvs_clear(void)
{
    if (!s_nvs_ok) return;
    nvs_set_u8 (s_nvs_h, NVS_KEY_STATE,      (uint8_t)REC_RECOVERY_IDLE);
    nvs_set_u32(s_nvs_h, NVS_KEY_SESS_ID,    0);
    nvs_set_u8 (s_nvs_h, NVS_KEY_SESS_TRIG,  0);
    nvs_set_u32(s_nvs_h, NVS_KEY_CHUNK_CNT,  0);
    nvs_set_u32(s_nvs_h, NVS_KEY_UPLOAD_CUR, 0);
    nvs_commit(s_nvs_h);
}

/* =========================================================
 * INTERNAL: SD DIRECTORY SETUP
 * ========================================================= */
static void ensure_sd_dir(void)
{
    struct stat st;
    if (stat(REC_RECOVERY_SD_DIR, &st) != 0) {
        if (mkdir(REC_RECOVERY_SD_DIR, 0777) != 0 && errno != EEXIST) {
            ESP_LOGW(TAG, "mkdir %s failed: errno=%d", REC_RECOVERY_SD_DIR, errno);
        }
    }
}

/* =========================================================
 * INTERNAL: WRITE ONE CHUNK RECORD TO SD
 *
 * Record layout:
 *   [magic 2B LE][chunk_index 2B LE][adpcm_len 2B LE][data N][crc 2B LE]
 *
 * CRC covers bytes 0..6+N-1 (magic + index + len + data).
 * Power-failure model: partial writes detectable by CRC mismatch.
 * ========================================================= */
static bool write_chunk_to_sd(uint16_t chunk_index,
                               const uint8_t *adpcm_data, uint16_t adpcm_len)
{
    if (!s_write_fp) return false;

    /* Build header bytes */
    uint8_t hdr[6];
    hdr[0] = (uint8_t)(REC_RECOVERY_CHUNK_MAGIC & 0xFF);
    hdr[1] = (uint8_t)(REC_RECOVERY_CHUNK_MAGIC >> 8);
    hdr[2] = (uint8_t)(chunk_index & 0xFF);
    hdr[3] = (uint8_t)(chunk_index >> 8);
    hdr[4] = (uint8_t)(adpcm_len & 0xFF);
    hdr[5] = (uint8_t)(adpcm_len >> 8);

    /* CRC over header + data */
    uint16_t crc = crc16_ccitt(hdr, 6);
    /* CRC update for data portion — feed data into running CRC */
    for (uint16_t i = 0; i < adpcm_len; i++) {
        uint8_t b = adpcm_data[i];
        crc ^= (uint16_t)b << 8;
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000u)
                ? (uint16_t)((crc << 1) ^ 0x1021u)
                : (uint16_t)(crc << 1);
        }
    }
    uint8_t crc_bytes[2] = { (uint8_t)(crc & 0xFF), (uint8_t)(crc >> 8) };

    /* Write atomically: header + data + crc */
    bool ok = (fwrite(hdr,        1, 6,         s_write_fp) == 6)
           && (fwrite(adpcm_data, 1, adpcm_len, s_write_fp) == adpcm_len)
           && (fwrite(crc_bytes,  1, 2,         s_write_fp) == 2);

    if (ok) {
        fflush(s_write_fp);
        fsync(fileno(s_write_fp));
    } else {
        ESP_LOGE(TAG, "write_chunk_to_sd failed (chunk_index=%u)", chunk_index);
    }
    return ok;
}

/* =========================================================
 * INTERNAL: READ ONE CHUNK RECORD FROM SD
 *
 * Advances file position past the record on success.
 * On CRC failure or short read: signals caller to skip.
 * ========================================================= */
static bool read_chunk_from_sd(uint16_t *chunk_index_out,
                                uint16_t *adpcm_len_out)
{
    /* Read header */
    uint8_t hdr[6];
    if (fread(hdr, 1, 6, s_upload_fp) != 6) return false;

    uint16_t magic      = (uint16_t)(hdr[0] | ((uint16_t)hdr[1] << 8));
    uint16_t chunk_idx  = (uint16_t)(hdr[2] | ((uint16_t)hdr[3] << 8));
    uint16_t adpcm_len  = (uint16_t)(hdr[4] | ((uint16_t)hdr[5] << 8));

    if (magic != REC_RECOVERY_CHUNK_MAGIC) {
        ESP_LOGW(TAG, "Bad magic 0x%04X — skipping record", magic);
        return false;
    }
    if (adpcm_len == 0 || adpcm_len > (APP_AUDIO_CHUNK_BYTES / 2)) {
        ESP_LOGW(TAG, "Implausible adpcm_len=%u — skipping", adpcm_len);
        return false;
    }

    /* Read data */
    if (fread(s_adpcm_rd_buf, 1, adpcm_len, s_upload_fp) != adpcm_len) {
        ESP_LOGW(TAG, "Short read for chunk %u data", chunk_idx);
        return false;
    }

    /* Read and verify CRC */
    uint8_t crc_bytes[2];
    if (fread(crc_bytes, 1, 2, s_upload_fp) != 2) return false;

    uint16_t stored_crc = (uint16_t)(crc_bytes[0] | ((uint16_t)crc_bytes[1] << 8));
    uint16_t calc_crc   = crc16_ccitt(hdr, 6);
    for (uint16_t i = 0; i < adpcm_len; i++) {
        uint8_t b = s_adpcm_rd_buf[i];
        calc_crc ^= (uint16_t)b << 8;
        for (int bit = 0; bit < 8; bit++) {
            calc_crc = (calc_crc & 0x8000u)
                     ? (uint16_t)((calc_crc << 1) ^ 0x1021u)
                     : (uint16_t)(calc_crc << 1);
        }
    }

    if (calc_crc != stored_crc) {
        ESP_LOGW(TAG, "CRC mismatch chunk %u (stored=%04X calc=%04X)",
                 chunk_idx, stored_crc, calc_crc);
        return false;  /* Caller will skip this chunk — partial power-failure write */
    }

    *chunk_index_out = chunk_idx;
    *adpcm_len_out   = adpcm_len;
    return true;
}

/* =========================================================
 * INTERNAL: UPLOAD — SEND SESSION_START
 * Reconstructs the SESSION_START frame from NVS metadata.
 * ========================================================= */
static bool upload_send_session_start(void)
{
    pkt_audio_session_start_t start = {
        .session_id      = s_session_id,
        .trigger         = s_trigger,
        .sample_rate_hz  = HW_MIC_SAMPLE_RATE_HZ,
        .bits_per_sample = 16,
        .codec           = 1,   /* IMA-ADPCM */
        .channels        = 1,
    };
    int flen = vk_frame_build(s_vk_ctrl_frame, sizeof(s_vk_ctrl_frame),
                              PKT_AUDIO_SESSION_START, tcp_client_next_seq(),
                              &start, sizeof(start));
    if (flen <= 0) return false;

    /* Emergency audio always uses the critical queue — bypasses telemetry backlog */
    bool sent = tcp_client_send_critical(s_vk_ctrl_frame, (uint16_t)flen);
    if (!sent) {
        ESP_LOGW(TAG, "Critical queue full sending SESSION_START (sess=%lu)",
                 (unsigned long)s_session_id);
    }
    return sent;
}

/* =========================================================
 * INTERNAL: UPLOAD — SEND SESSION_END
 * ========================================================= */
static bool upload_send_session_end(void)
{
    pkt_audio_session_end_t end_pkt = {
        .session_id        = s_session_id,
        .total_chunks      = (uint16_t)s_chunk_count,
        /* total_pcm_samples: estimated from chunk count × samples/chunk.
         * Not exact (last chunk may be shorter), but informational only. */
        .total_pcm_samples = s_chunk_count * (APP_AUDIO_CHUNK_BYTES / sizeof(int16_t)),
    };
    int flen = vk_frame_build(s_vk_ctrl_frame, sizeof(s_vk_ctrl_frame),
                              PKT_AUDIO_SESSION_END, tcp_client_next_seq(),
                              &end_pkt, sizeof(end_pkt));
    if (flen <= 0) return false;
    return tcp_client_send_critical(s_vk_ctrl_frame, (uint16_t)flen);
}

/* =========================================================
 * INTERNAL: CLEANUP AFTER SUCCESSFUL UPLOAD
 * Deletes SD file, clears NVS, transitions to IDLE.
 * ========================================================= */
static void upload_complete_cleanup(void)
{
    if (s_upload_fp) {
        fclose(s_upload_fp);
        s_upload_fp = NULL;
    }

    char path[64];
    session_file_path(path, sizeof(path), s_session_id);
    if (remove(path) != 0) {
        ESP_LOGW(TAG, "Failed to delete session file %s: errno=%d", path, errno);
    } else {
        ESP_LOGI(TAG, "Session %lu: file deleted after upload", (unsigned long)s_session_id);
    }

    s_state         = REC_RECOVERY_IDLE;
    s_session_id    = 0;
    s_trigger       = AUDIO_TRIGGER_REQUEST;
    s_chunk_count   = 0;
    s_upload_cursor = 0;
    s_upload_session_started = false;
    nvs_clear();
    ESP_LOGI(TAG, "Upload complete — session cleaned up");
}

/* =========================================================
 * PUBLIC API: INIT
 * ========================================================= */
esp_err_t recording_recovery_init(void)
{
    /* Open NVS namespace for read-write */
    esp_err_t ret = nvs_open(REC_RECOVERY_NVS_NS, NVS_READWRITE, &s_nvs_h);
    if (ret == ESP_OK) {
        s_nvs_ok = true;
        nvs_load_state();
    } else {
        ESP_LOGW(TAG, "NVS open failed (%s) — recovery state not persistent",
                 esp_err_to_name(ret));
        s_nvs_ok = false;
        s_state  = REC_RECOVERY_IDLE;
    }

    /* If a previous session was interrupted mid-upload or left PENDING,
     * transition to PENDING so it uploads on next TCP reconnect.
     * If ACTIVE (interrupted during recording), we have a partial session —
     * keep the chunks that made it to SD and still attempt upload. */
    if (s_state == REC_RECOVERY_UPLOADING || s_state == REC_RECOVERY_ACTIVE) {
        ESP_LOGW(TAG, "Recovered interrupted session %lu (%u chunks) — marking PENDING",
                 (unsigned long)s_session_id, (unsigned)s_chunk_count);
        s_state = REC_RECOVERY_PENDING;
        nvs_persist_state();
    } else if (s_state == REC_RECOVERY_PENDING && s_chunk_count == 0) {
        /* Corrupted state: pending but no chunks — clear */
        s_state = REC_RECOVERY_IDLE;
        nvs_clear();
    }

    /* Ensure SD directory exists (best-effort — SD may not be mounted yet) */
    if (sd_manager_is_mounted()) {
        ensure_sd_dir();
    }

    s_initialized = true;
    ESP_LOGI(TAG, "recording_recovery initialized (state=%s, sess=%lu, chunks=%lu)",
             recording_recovery_state_name(s_state),
             (unsigned long)s_session_id,
             (unsigned long)s_chunk_count);
    return ESP_OK;
}

/* =========================================================
 * PUBLIC API: SESSION BEGIN
 * ========================================================= */
void recording_recovery_session_begin(uint32_t session_id,
                                       audio_trigger_t trigger)
{
    if (!s_initialized) return;

    /* Only EMERGENCY sessions use the SD dual-path */
    if (trigger != AUDIO_TRIGGER_EMERGENCY) return;

    /* Guard: if we somehow get here while PENDING/UPLOADING, do not disturb
     * the existing upload. The new session will be recorded TCP-only. */
    if (s_state == REC_RECOVERY_PENDING || s_state == REC_RECOVERY_UPLOADING) {
        ESP_LOGW(TAG, "New EMERGENCY session %lu during pending upload %lu — SD write skipped",
                 (unsigned long)session_id, (unsigned long)s_session_id);
        return;
    }

    /* Ensure SD is ready */
    if (!sd_manager_is_mounted()) {
        ESP_LOGW(TAG, "SD not mounted — EMERGENCY session %lu will be TCP-only",
                 (unsigned long)session_id);
        return;
    }
    ensure_sd_dir();

    /* Open SD file for writing */
    char path[64];
    session_file_path(path, sizeof(path), session_id);
    s_write_fp = fopen(path, "wb");
    if (!s_write_fp) {
        ESP_LOGE(TAG, "Cannot open %s for write: errno=%d", path, errno);
        return;
    }

    /* Initialize session tracking */
    s_session_id    = session_id;
    s_trigger       = (uint8_t)trigger;
    s_chunk_count   = 0;
    s_upload_cursor = 0;
    s_upload_session_started = false;
    s_state         = REC_RECOVERY_ACTIVE;

    /* Persist immediately — if we power-cycle during recording, we know we were ACTIVE */
    nvs_persist_state();

    ESP_LOGI(TAG, "EMERGENCY session %lu: SD write started at %s",
             (unsigned long)session_id, path);
}

/* =========================================================
 * PUBLIC API: SUBMIT CHUNK (DUAL-PATH)
 * ========================================================= */
bool recording_recovery_submit_chunk(uint32_t session_id,
                                     uint16_t chunk_index,
                                     audio_trigger_t trigger,
                                     const uint8_t *adpcm_data,
                                     uint16_t adpcm_len,
                                     bool is_last)
{
    /* Only EMERGENCY triggers use the SD path */
    if (trigger != AUDIO_TRIGGER_EMERGENCY) return false;
    if (s_state != REC_RECOVERY_ACTIVE)     return false;
    if (session_id != s_session_id)         return false;
    if (!s_write_fp)                        return false;

    VK_ASSERT_WARN(adpcm_data != NULL, "submit_chunk: adpcm_data is NULL");
    if (!adpcm_data || adpcm_len == 0) return false;

    bool written = write_chunk_to_sd(chunk_index, adpcm_data, adpcm_len);
    if (written) {
        s_chunk_count++;
        /* Persist chunk_count periodically (every 8 chunks) to limit NVS wear.
         * A crash between persists means we may re-upload already-synced chunks —
         * the server handles (session_id, chunk_index) deduplication. */
        if ((s_chunk_count & 7u) == 0) {
            nvs_persist_state();
        }
    }

    if (is_last) {
        /* Close write file — session recording complete */
        fflush(s_write_fp);
        fsync(fileno(s_write_fp));
        fclose(s_write_fp);
        s_write_fp = NULL;

        if (s_chunk_count > 0) {
            s_state = REC_RECOVERY_PENDING;
            nvs_persist_state();
            ESP_LOGI(TAG, "EMERGENCY session %lu: %lu chunks on SD — PENDING upload",
                     (unsigned long)session_id, (unsigned long)s_chunk_count);
        } else {
            /* All SD writes failed — nothing to recover */
            s_state = REC_RECOVERY_IDLE;
            nvs_clear();
            ESP_LOGW(TAG, "Session %lu: no chunks written to SD — IDLE",
                     (unsigned long)session_id);
        }
    }

    return written;
}

/* =========================================================
 * PUBLIC API: TCP CONNECTED — TRIGGER UPLOAD
 * ========================================================= */
void recording_recovery_on_tcp_connected(void)
{
    if (s_state != REC_RECOVERY_PENDING) return;

    ESP_LOGI(TAG, "TCP connected — starting deferred upload of session %lu (%lu chunks, cursor=%lu)",
             (unsigned long)s_session_id,
             (unsigned long)s_chunk_count,
             (unsigned long)s_upload_cursor);

    /* Transition to UPLOADING — next upload_tick() will open the file and begin */
    s_state = REC_RECOVERY_UPLOADING;
    s_upload_session_started = false;
    nvs_persist_state();

    /* Open the SD file for reading */
    char path[64];
    session_file_path(path, sizeof(path), s_session_id);
    s_upload_fp = fopen(path, "rb");
    if (!s_upload_fp) {
        ESP_LOGE(TAG, "Cannot open session file %s: errno=%d — marking IDLE", path, errno);
        /* File missing (maybe SD was reformatted) — clear state and give up */
        s_state = REC_RECOVERY_IDLE;
        nvs_clear();
        return;
    }

    /* Seek past already-uploaded chunks (crash-recovery: NVS cursor) */
    if (s_upload_cursor > 0) {
        ESP_LOGI(TAG, "Resuming from chunk %lu (scanning forward)",
                 (unsigned long)s_upload_cursor);
        for (uint32_t i = 0; i < s_upload_cursor; i++) {
            uint16_t dummy_idx, dummy_len;
            if (!read_chunk_from_sd(&dummy_idx, &dummy_len)) {
                /* Corrupt record at or before cursor — restart from beginning */
                ESP_LOGW(TAG, "Corrupt record while seeking to cursor %lu — restarting",
                         (unsigned long)s_upload_cursor);
                s_upload_cursor = 0;
                rewind(s_upload_fp);
                break;
            }
        }
    }
}

/* =========================================================
 * PUBLIC API: UPLOAD TICK (one chunk per call)
 *
 * Drives the upload one chunk at a time from task_audio_encoder's idle loop.
 * Returns true while upload is in progress (keep calling).
 * Returns false when done, on TCP disconnect, or on error.
 * ========================================================= */
bool recording_recovery_upload_tick(void)
{
    if (s_state != REC_RECOVERY_UPLOADING) return false;

    /* Abort if TCP went offline since last tick */
    if (!tcp_client_is_connected()) {
        ESP_LOGW(TAG, "TCP disconnected mid-upload (sent %lu/%lu chunks) — PENDING",
                 (unsigned long)s_upload_cursor, (unsigned long)s_chunk_count);
        if (s_upload_fp) { fclose(s_upload_fp); s_upload_fp = NULL; }
        s_state = REC_RECOVERY_PENDING;
        nvs_persist_state();
        return false;
    }

    /* Send SESSION_START before the first chunk */
    if (!s_upload_session_started) {
        if (!upload_send_session_start()) {
            /* Critical queue full — retry next tick */
            ESP_LOGW(TAG, "SESSION_START send failed (critical queue full) — retrying");
            return true;  /* Stay in UPLOADING, try again */
        }
        s_upload_session_started = true;
        ESP_LOGI(TAG, "Upload: SESSION_START sent for session %lu",
                 (unsigned long)s_session_id);
    }

    /* All chunks uploaded? */
    if (s_upload_cursor >= s_chunk_count) {
        upload_send_session_end();
        upload_complete_cleanup();
        ESP_LOGI(TAG, "Deferred upload complete");
        return false;  /* Done */
    }

    /* Read next chunk from SD */
    uint16_t chunk_idx  = 0;
    uint16_t adpcm_len  = 0;
    if (!read_chunk_from_sd(&chunk_idx, &adpcm_len)) {
        /* Corrupt or partial record — skip it (at-least-once, server deduplicates) */
        ESP_LOGW(TAG, "Corrupt SD record at upload_cursor=%lu — skipping",
                 (unsigned long)s_upload_cursor);
        s_upload_cursor++;
        nvs_persist_upload_cursor();
        return (s_upload_cursor < s_chunk_count);
    }

    /* Build VK AUDIO_CHUNK frame.
     * Payload = pkt_audio_chunk_header_t + adpcm_data concatenated. */
    static uint8_t s_chunk_payload[sizeof(pkt_audio_chunk_header_t)
                                    + (APP_AUDIO_CHUNK_BYTES / 2)];

    pkt_audio_chunk_header_t hdr_struct = {
        .session_id  = s_session_id,
        .chunk_index = chunk_idx,
        .chunk_len   = adpcm_len,
    };
    memcpy(s_chunk_payload, &hdr_struct, sizeof(hdr_struct));
    memcpy(s_chunk_payload + sizeof(hdr_struct), s_adpcm_rd_buf, adpcm_len);

    int flen = vk_frame_build(s_vk_chunk_frame, sizeof(s_vk_chunk_frame),
                              PKT_AUDIO_CHUNK, tcp_client_next_seq(),
                              s_chunk_payload,
                              (uint32_t)(sizeof(hdr_struct) + adpcm_len));
    if (flen > 0) {
        bool sent = tcp_client_send_critical(s_vk_chunk_frame, (uint16_t)flen);
        if (!sent) {
            /* Critical queue full — back off: restore file position and retry next tick */
            ESP_LOGW(TAG, "Critical queue full at chunk %u — retrying", chunk_idx);
            /* Rewind file position back to this record start */
            long rewind_bytes = (long)(6 + adpcm_len + 2);  /* header + data + crc */
            fseek(s_upload_fp, -rewind_bytes, SEEK_CUR);
            vTaskDelay(pdMS_TO_TICKS(20));  /* Brief back-off */
            return true;
        }
    }

    s_upload_cursor++;
    nvs_persist_upload_cursor();

    ESP_LOGD(TAG, "Uploaded chunk %u/%lu (sess=%lu, %u bytes ADPCM)",
             chunk_idx, (unsigned long)s_chunk_count,
             (unsigned long)s_session_id, adpcm_len);
    return true;  /* More chunks to send */
}

/* =========================================================
 * PUBLIC API: STATUS QUERIES
 * ========================================================= */
rec_recovery_state_t recording_recovery_get_state(void)
{
    return s_state;
}

bool recording_recovery_has_pending(void)
{
    return (s_state == REC_RECOVERY_PENDING);
}

const char *recording_recovery_state_name(rec_recovery_state_t state)
{
    switch (state) {
        case REC_RECOVERY_IDLE:      return "IDLE";
        case REC_RECOVERY_ACTIVE:    return "ACTIVE";
        case REC_RECOVERY_PENDING:   return "PENDING";
        case REC_RECOVERY_UPLOADING: return "UPLOADING";
        default:                     return "UNKNOWN";
    }
}
