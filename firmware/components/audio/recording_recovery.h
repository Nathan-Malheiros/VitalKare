/**
 * @file recording_recovery.h
 * @brief Emergency Audio Recording Recovery FSM
 *
 * PROBLEM STATEMENT:
 *   Emergency audio recordings must NEVER be lost — this is the highest-priority
 *   safety guarantee in the VitalKare firmware. WiFi can drop, the server can
 *   restart, or TCP can disconnect at any moment during a 10-second recording.
 *   Without a recovery path, the patient's emergency call audio is silently
 *   discarded once the TCP queue overflows or the socket closes.
 *
 * DESIGN — DUAL-PATH EMERGENCY PERSISTENCE:
 *   For AUDIO_TRIGGER_EMERGENCY sessions, ADPCM chunks are ALWAYS persisted to
 *   SD card in addition to being sent over TCP. This dual-path approach means:
 *     - If TCP is alive: server receives chunks in real-time (fast path).
 *     - If TCP is dead:  all chunks are safe on SD (recovery path).
 *     - On reconnect:    SD chunks are re-uploaded; server deduplicates by
 *                        (session_id, chunk_index).
 *
 *   For AUDIO_TRIGGER_REQUEST (normal calls), SD persistence is skipped.
 *   Normal calls are best-effort; emergency recordings are guaranteed.
 *
 * FSM:
 *   IDLE → ACTIVE:       recording_recovery_session_begin() called for EMERGENCY.
 *   ACTIVE → PENDING:    is_last chunk written to SD; session complete.
 *   ACTIVE → IDLE:       is_last chunk, but trigger was REQUEST (no SD write).
 *   PENDING → UPLOADING: recording_recovery_on_tcp_connected() called while PENDING.
 *   UPLOADING → IDLE:    All chunks uploaded; SD file deleted; NVS cleared.
 *   UPLOADING → PENDING: TCP drops during upload; resume on next reconnect.
 *
 * SD CARD LAYOUT:
 *   Directory: /sdcard/audio/
 *   File:      /sdcard/audio/SSSSSSSS.bin  (SSSSSSSS = session_id in zero-padded hex)
 *
 *   Each chunk record:
 *     [MAGIC   2B LE = 0xA5A5]
 *     [chunk_index  2B LE]
 *     [adpcm_len    2B LE]
 *     [adpcm_data   adpcm_len bytes]
 *     [CRC16   2B LE — over magic + index + len + data]
 *
 * NVS PERSISTENCE (namespace "vk_rec"):
 *   "state"      u8:  rec_recovery_state_t (persists across reboot)
 *   "sess_id"    u32: session ID of pending session
 *   "sess_trig"  u8:  audio_trigger_t of pending session
 *   "chunk_cnt"  u32: total chunks written to SD
 *   "upload_cur" u32: number of chunks already queued to TCP (upload cursor)
 *
 * CONCURRENCY:
 *   ALL functions in this module are called exclusively from task_audio_encoder.
 *   There is no concurrent access. No mutex is required.
 *   s_state is plain (not atomic) — ownership is single-task.
 *
 *   recording_recovery_on_tcp_connected() is called from task_audio_encoder's
 *   idle loop (same task), not from any event handler. This avoids race
 *   conditions entirely.
 *
 * MEMORY OWNERSHIP:
 *   SD file handle: owned by this module; opened per-session; closed per-session.
 *   NVS handle: opened at init; persistent for module lifetime.
 *   Static buffers: module-internal; zero heap allocation at runtime.
 *
 * FAILURE MODES:
 *   SD full / unmounted:  chunk logged as dropped; session metadata still tracked.
 *                         Partial session on SD is ignored on upload (chunk_cnt
 *                         represents what was actually persisted).
 *   NVS write fail:       logged; upload cursor may regress (re-upload from 0).
 *                         Server handles duplicates by (session_id, chunk_index).
 *   TCP drop mid-upload:  state → PENDING; resume from NVS cursor on reconnect.
 *   Power failure:        NVS state survives; SD file may have partial last record.
 *                         Partial record detected by CRC check during upload_tick().
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "audio_capture.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================
 * CONFIGURATION
 * ========================================================= */

/** NVS namespace for recovery state */
#define REC_RECOVERY_NVS_NS         "vk_rec"

/** SD card directory for audio session files */
#define REC_RECOVERY_SD_DIR         "/sdcard/audio"

/** Magic bytes for chunk records on SD */
#define REC_RECOVERY_CHUNK_MAGIC    0xA5A5u

/** Overhead per chunk record on SD (magic + index + len + crc) */
#define REC_RECOVERY_RECORD_OVERHEAD  8u

/* =========================================================
 * FSM STATES
 * ========================================================= */
typedef enum {
    REC_RECOVERY_IDLE      = 0,  /**< No pending emergency session */
    REC_RECOVERY_ACTIVE    = 1,  /**< Emergency session recording — writing to SD */
    REC_RECOVERY_PENDING   = 2,  /**< Session complete on SD; awaiting TCP reconnect */
    REC_RECOVERY_UPLOADING = 3,  /**< Deferred upload in progress */
} rec_recovery_state_t;

/* =========================================================
 * API
 * ========================================================= */

/**
 * @brief Initialize the recovery module.
 *
 *        Loads NVS state. If a previous session was PENDING or UPLOADING
 *        (interrupted by reboot), transitions to PENDING so it will be
 *        uploaded on the next TCP reconnect.
 *
 *        Must be called after sd_manager_init() and nvs_flash_init().
 *        Must be called before task_audio_encoder starts.
 *
 * @return ESP_OK on success (even if SD is unavailable — graceful degrade).
 */
esp_err_t recording_recovery_init(void);

/**
 * @brief Begin tracking an audio session.
 *
 *        For AUDIO_TRIGGER_EMERGENCY: opens the SD file for writing.
 *        For AUDIO_TRIGGER_REQUEST: no-op (requests are TCP-only).
 *
 *        Called by task_audio_encoder at the start of each new session.
 *        Must be called before the first recording_recovery_submit_chunk().
 *
 * @param session_id  Unique session identifier.
 * @param trigger     AUDIO_TRIGGER_EMERGENCY or AUDIO_TRIGGER_REQUEST.
 */
void recording_recovery_session_begin(uint32_t session_id,
                                       audio_trigger_t trigger);

/**
 * @brief Submit one ADPCM-encoded chunk for dual-path routing.
 *
 *        For AUDIO_TRIGGER_EMERGENCY: always writes chunk to SD card.
 *        For AUDIO_TRIGGER_REQUEST:   no-op (returns false).
 *
 *        When is_last == true: closes the write file handle; transitions
 *        FSM from ACTIVE → PENDING (if session was EMERGENCY).
 *
 *        Called by task_audio_encoder for every encoded chunk.
 *        Must only be called between session_begin() and the is_last chunk.
 *
 * @param session_id   Must match the active session's session_id.
 * @param chunk_index  Monotonic 0-based index within session.
 * @param trigger      Trigger type (must match session's trigger).
 * @param adpcm_data   Encoded ADPCM byte buffer.
 * @param adpcm_len    Byte count of adpcm_data.
 * @param is_last      True if this is the final chunk of the session.
 * @return true  if the chunk was written to SD.
 * @return false if not an EMERGENCY trigger, or if SD write failed.
 */
bool recording_recovery_submit_chunk(uint32_t session_id,
                                     uint16_t chunk_index,
                                     audio_trigger_t trigger,
                                     const uint8_t *adpcm_data,
                                     uint16_t adpcm_len,
                                     bool is_last);

/**
 * @brief Trigger deferred upload when TCP becomes ACTIVE.
 *
 *        Transitions PENDING → UPLOADING.
 *        Idempotent: safe to call when not PENDING (no-op).
 *
 *        Called from task_audio_encoder's idle loop, which polls
 *        tcp_client_is_connected() and calls this function when a
 *        pending session is detected.
 */
void recording_recovery_on_tcp_connected(void);

/**
 * @brief Advance the upload FSM by one chunk.
 *
 *        Reads one chunk record from the SD file, builds a VK AUDIO_CHUNK
 *        frame, and sends it via tcp_client_send_critical().
 *
 *        Before the first chunk: sends a reconstructed PKT_AUDIO_SESSION_START.
 *        After the last chunk: sends PKT_AUDIO_SESSION_END; transitions to IDLE.
 *
 *        If tcp_client_is_connected() returns false mid-upload:
 *          transitions back to PENDING; returns false.
 *
 *        Called from task_audio_encoder's idle loop while in UPLOADING state.
 *
 * @return true  if upload is still in progress (call again next idle tick).
 * @return false if upload completed, TCP disconnected, or an error occurred.
 */
bool recording_recovery_upload_tick(void);

/**
 * @brief Returns the current FSM state (for diagnostics and idle-loop polling).
 */
rec_recovery_state_t recording_recovery_get_state(void);

/**
 * @brief Returns true if there is a session pending upload.
 */
bool recording_recovery_has_pending(void);

/**
 * @brief Human-readable state name for logging.
 */
const char *recording_recovery_state_name(rec_recovery_state_t state);

#ifdef __cplusplus
}
#endif
