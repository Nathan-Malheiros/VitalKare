/**
 * @file vk_validator.h
 * @brief VK Protocol Frame Validation Layer
 *
 * ARCHITECTURE RATIONALE:
 *   The TCP receive path parses raw bytes from the network. Any malformed
 *   input — truncated frames, wrong magic, bad CRC, oversized payload, sequence
 *   replay — must be caught before it reaches the command processor or event queue.
 *   This layer provides complete validation with typed error codes and metrics,
 *   acting as a firewall between the network and the application layer.
 *
 * RESPONSIBILITIES:
 *   - Magic byte validation (0x56 0x4B)
 *   - Protocol version validation (0x01)
 *   - Payload length sanity check (0 ≤ len ≤ VK_MAX_PAYLOAD_SIZE)
 *   - CRC-16/CCITT validation (header + payload)
 *   - Sequence number anti-replay (sliding window of last VK_VALIDATOR_SEQ_WINDOW entries)
 *   - Packet type range check (known vs unknown types)
 *
 * CONCURRENCY:
 *   NOT thread-safe. Must be called exclusively from task_tcp_rx.
 *   The sequence window is per-session state, not protected by any lock.
 *   Call vk_validator_reset() on each new TCP connection.
 *
 * OWNERSHIP:
 *   All state is internal static. No heap allocation.
 *   The validated frame pointer references the caller's buffer — zero copy.
 *
 * FAILURE CONTAINMENT:
 *   Every error type increments a named counter.
 *   On validation failure, the caller advances by 1 byte and retries —
 *   this is the standard "magic byte resync" strategy for stream parsers.
 *
 * USAGE:
 *   vk_validation_result_t r = vk_validate_frame(buf, len, &hdr, &payload, &frame_len);
 *   if (r != VK_VALID) {
 *       vk_validator_log_error(r, buf, len);
 *       // advance by 1 and retry
 *   }
 */

#pragma once

#include "vk_protocol.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================
 * CONFIGURATION
 * ========================================================= */

/** Size of the anti-replay sequence window. */
#define VK_VALIDATOR_SEQ_WINDOW     16u

/** Maximum allowed sequence gap before resetting window
 *  (handles legitimate wraps and reconnects). */
#define VK_VALIDATOR_MAX_SEQ_GAP    512u

/* =========================================================
 * VALIDATION RESULT CODES
 * ========================================================= */
typedef enum {
    VK_VALID             = 0,   /**< Frame is valid and complete */
    VK_ERR_INCOMPLETE    = 1,   /**< Not enough bytes for a complete frame yet */
    VK_ERR_MAGIC         = 2,   /**< Magic bytes do not match 0x56 0x4B */
    VK_ERR_VERSION       = 3,   /**< Protocol version != VK_PROTOCOL_VERSION */
    VK_ERR_PAYLOAD_LEN   = 4,   /**< payload_len > VK_MAX_PAYLOAD_SIZE */
    VK_ERR_CRC           = 5,   /**< CRC-16 mismatch */
    VK_ERR_REPLAY        = 6,   /**< Sequence number already seen (replay attack / duplicate) */
    VK_ERR_UNKNOWN_TYPE  = 7,   /**< Packet type not in known range */
} vk_validation_result_t;

/* =========================================================
 * METRICS — cumulative counters
 * ========================================================= */
typedef struct {
    uint32_t frames_valid;        /**< Total frames that passed validation */
    uint32_t frames_incomplete;   /**< Partial frames (expected, not an error) */
    uint32_t err_magic;           /**< Magic byte mismatches */
    uint32_t err_version;         /**< Protocol version mismatches */
    uint32_t err_payload_len;     /**< Oversized payload rejections */
    uint32_t err_crc;             /**< CRC failures */
    uint32_t err_replay;          /**< Replay / duplicate sequence rejections */
    uint32_t err_unknown_type;    /**< Unknown packet type warnings */
} vk_validator_metrics_t;

/* =========================================================
 * API
 * ========================================================= */

/**
 * @brief Reset validator state for a new TCP session.
 *        Must be called each time the TCP connection is established.
 *        Clears the sequence window (reconnect resets expected seq).
 *        Does NOT reset metrics (cumulative across sessions).
 */
void vk_validator_reset(void);

/**
 * @brief Validate a frame in the receive buffer.
 *
 * @param buf        Pointer to start of receive buffer.
 * @param available  Number of bytes available in buffer.
 * @param[out] hdr   Set to pointer into buf at the header, if valid.
 * @param[out] payload Set to pointer into buf at payload start, if valid.
 * @param[out] frame_len Set to total frame size (header + payload + CRC), if valid.
 *
 * @return VK_VALID if frame is complete and valid.
 *         VK_ERR_INCOMPLETE if more bytes are needed (not an error — wait for more data).
 *         Other VK_ERR_* if the frame is malformed (advance 1 byte and retry).
 *
 * @note On VK_ERR_INCOMPLETE, hdr/payload/frame_len are NOT modified.
 * @note On any error except INCOMPLETE, advance the buffer by 1 byte and retry.
 */
vk_validation_result_t vk_validate_frame(const uint8_t        *buf,
                                          size_t                available,
                                          const vk_header_t   **hdr,
                                          const uint8_t       **payload,
                                          size_t               *frame_len);

/**
 * @brief Log a validation error with context bytes.
 *        Call when vk_validate_frame() returns a non-VK_VALID, non-INCOMPLETE result.
 *
 * @param result   The error code from vk_validate_frame().
 * @param buf      Start of the offending bytes.
 * @param len      How many bytes are available (for hex dump context).
 */
void vk_validator_log_error(vk_validation_result_t result,
                             const uint8_t *buf, size_t len);

/**
 * @brief Get a snapshot of cumulative validation metrics.
 */
void vk_validator_get_metrics(vk_validator_metrics_t *out);

/**
 * @brief Human-readable name for a validation result code.
 */
const char *vk_validation_result_name(vk_validation_result_t r);

#ifdef __cplusplus
}
#endif
