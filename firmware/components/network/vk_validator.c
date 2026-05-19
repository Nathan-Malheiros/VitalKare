/**
 * @file vk_validator.c
 * @brief VK Protocol Frame Validation — implementation
 *
 * SEQUENCE WINDOW:
 *   s_seq_window[] holds the last VK_VALIDATOR_SEQ_WINDOW sequence numbers seen.
 *   A new frame is rejected if its seq appears in the window.
 *   On each new connection, vk_validator_reset() clears the window.
 *
 *   The window uses a linear search (VK_VALIDATOR_SEQ_WINDOW = 16) — O(n) is
 *   acceptable for the per-frame validation cost vs. the TCP processing overhead.
 *
 *   Large sequence gaps (> VK_VALIDATOR_MAX_SEQ_GAP) clear the window automatically
 *   to handle legitimate uint16 rollovers without false replay alarms.
 *
 * CALLER CONTRACT (task_tcp_rx):
 *   while (rx_bytes_available >= VK_MIN_FRAME_SIZE) {
 *       const vk_header_t *hdr; const uint8_t *payload; size_t frame_len;
 *       vk_validation_result_t r = vk_validate_frame(buf, avail, &hdr, &payload, &frame_len);
 *       if (r == VK_VALID) {
 *           // consume frame_len bytes, dispatch hdr+payload
 *       } else if (r == VK_ERR_INCOMPLETE) {
 *           break; // wait for more data
 *       } else {
 *           vk_validator_log_error(r, buf, avail);
 *           buf++; avail--; // resync: advance 1 byte and retry
 *       }
 *   }
 */

#include "vk_validator.h"
#include "esp_log.h"
#include <string.h>
#include <stdatomic.h>

static const char *TAG = "VALID";

/* =========================================================
 * INTERNAL STATE
 * ========================================================= */
static uint16_t            s_seq_window[VK_VALIDATOR_SEQ_WINDOW];
static uint32_t            s_seq_window_head = 0;
static uint32_t            s_seq_window_count = 0;
static uint16_t            s_last_seq = 0;
static bool                s_seq_initialized = false;

static vk_validator_metrics_t s_metrics;

/* =========================================================
 * RESET (call on each new TCP connection)
 * ========================================================= */
void vk_validator_reset(void)
{
    memset(s_seq_window, 0, sizeof(s_seq_window));
    s_seq_window_head  = 0;
    s_seq_window_count = 0;
    s_seq_initialized  = false;
    s_last_seq         = 0;
    /* Intentionally do NOT reset s_metrics — cumulative across sessions */
    ESP_LOGI(TAG, "Validator reset for new session");
}

/* =========================================================
 * KNOWN TYPE CHECK
 * ========================================================= */
static bool is_known_type(uint8_t t)
{
    /* Known ranges per vk_protocol.h:
     *   0x01..0x04  connection
     *   0x10..0x13  patient events
     *   0x20..0x22  audio
     *   0x30..0x33  telemetry
     *   0x40..0x42  image
     *   0x50..0x55  commands
     *   0x60..0x63  OTA
     *   0x6F        reboot
     *   0xFF        error
     */
    if (t >= 0x01u && t <= 0x04u) return true;
    if (t >= 0x10u && t <= 0x13u) return true;
    if (t >= 0x20u && t <= 0x22u) return true;
    if (t >= 0x30u && t <= 0x33u) return true;
    if (t >= 0x40u && t <= 0x42u) return true;
    if (t >= 0x50u && t <= 0x55u) return true;
    if (t >= 0x60u && t <= 0x63u) return true;
    if (t == 0x6Fu) return true;
    if (t == 0xFFu) return true;
    return false;
}

/* =========================================================
 * SEQUENCE WINDOW
 * ========================================================= */
static bool seq_is_replay(uint16_t seq)
{
    if (!s_seq_initialized) {
        /* First packet after reset — always accept */
        s_seq_initialized = true;
        s_last_seq = seq;
        return false;
    }

    /* Large gap = probable rollover or reconnect — reset window */
    uint16_t gap = (uint16_t)((int32_t)seq - (int32_t)s_last_seq);
    if (gap > VK_VALIDATOR_MAX_SEQ_GAP) {
        ESP_LOGW(TAG, "Seq gap %u (last=%u cur=%u) — resetting window",
                 (unsigned)gap, (unsigned)s_last_seq, (unsigned)seq);
        s_seq_window_count = 0;
        s_seq_window_head  = 0;
    }

    /* Check window for duplicate */
    for (uint32_t i = 0; i < s_seq_window_count; i++) {
        if (s_seq_window[i] == seq) {
            return true;  /* Replay detected */
        }
    }

    /* Record this seq in the window */
    s_seq_window[s_seq_window_head % VK_VALIDATOR_SEQ_WINDOW] = seq;
    s_seq_window_head++;
    if (s_seq_window_count < VK_VALIDATOR_SEQ_WINDOW) {
        s_seq_window_count++;
    }
    s_last_seq = seq;
    return false;
}

/* =========================================================
 * MAIN VALIDATION ENTRY POINT
 * ========================================================= */
vk_validation_result_t vk_validate_frame(const uint8_t        *buf,
                                          size_t                available,
                                          const vk_header_t   **hdr,
                                          const uint8_t       **payload,
                                          size_t               *frame_len)
{
    /* ── Step 1: Enough bytes for header? ── */
    if (available < VK_MIN_FRAME_SIZE) {
        return VK_ERR_INCOMPLETE;
    }

    const vk_header_t *h = (const vk_header_t *)buf;

    /* ── Step 2: Magic bytes ── */
    if (h->magic[0] != VK_MAGIC_0 || h->magic[1] != VK_MAGIC_1) {
        s_metrics.err_magic++;
        return VK_ERR_MAGIC;
    }

    /* ── Step 3: Protocol version ── */
    if (h->version != VK_PROTOCOL_VERSION) {
        s_metrics.err_version++;
        return VK_ERR_VERSION;
    }

    /* ── Step 4: Payload length sanity ── */
    uint32_t plen = h->payload_len;  /* already LE on wire, struct is packed */
    if (plen > VK_MAX_PAYLOAD_SIZE) {
        s_metrics.err_payload_len++;
        return VK_ERR_PAYLOAD_LEN;
    }

    /* ── Step 5: Complete frame available? ── */
    size_t total = VK_HEADER_SIZE + plen + VK_CRC_SIZE;
    if (available < total) {
        return VK_ERR_INCOMPLETE;
    }

    /* ── Step 6: CRC validation ── */
    uint16_t crc_received = (uint16_t)(buf[total - 2] | ((uint16_t)buf[total - 1] << 8));
    uint16_t crc_computed  = vk_crc16(buf, VK_HEADER_SIZE + plen);
    if (crc_received != crc_computed) {
        s_metrics.err_crc++;
        return VK_ERR_CRC;
    }

    /* ── Step 7: Packet type range ── */
    if (!is_known_type(h->pkt_type)) {
        /* Warn but do NOT reject — future protocol versions may add types.
         * Counting allows monitoring without breaking forward compatibility. */
        s_metrics.err_unknown_type++;
        ESP_LOGW(TAG, "Unknown packet type 0x%02X (seq=%u) — passing through",
                 h->pkt_type, (unsigned)h->seq);
    }

    /* ── Step 8: Replay detection ── */
    if (seq_is_replay(h->seq)) {
        s_metrics.err_replay++;
        return VK_ERR_REPLAY;
    }

    /* ── All checks passed ── */
    s_metrics.frames_valid++;
    if (hdr)       *hdr       = h;
    if (payload)   *payload   = buf + VK_HEADER_SIZE;
    if (frame_len) *frame_len = total;
    return VK_VALID;
}

/* =========================================================
 * ERROR LOGGING
 * ========================================================= */
void vk_validator_log_error(vk_validation_result_t result,
                             const uint8_t *buf, size_t len)
{
    if (result == VK_ERR_INCOMPLETE) return;  /* Not an error */

    size_t dump_len = (len > 8) ? 8 : len;
    if (dump_len >= 4 && buf) {
        ESP_LOGE(TAG, "Validation failed: %s | first bytes: %02X %02X %02X %02X",
                 vk_validation_result_name(result),
                 buf[0], buf[1], buf[2], buf[3]);
    } else {
        ESP_LOGE(TAG, "Validation failed: %s (only %u bytes available)",
                 vk_validation_result_name(result), (unsigned)len);
    }
}

/* =========================================================
 * METRICS SNAPSHOT
 * ========================================================= */
void vk_validator_get_metrics(vk_validator_metrics_t *out)
{
    if (out) {
        memcpy(out, &s_metrics, sizeof(s_metrics));
    }
}

/* =========================================================
 * RESULT NAME
 * ========================================================= */
const char *vk_validation_result_name(vk_validation_result_t r)
{
    switch (r) {
        case VK_VALID:            return "VK_VALID";
        case VK_ERR_INCOMPLETE:   return "INCOMPLETE";
        case VK_ERR_MAGIC:        return "BAD_MAGIC";
        case VK_ERR_VERSION:      return "BAD_VERSION";
        case VK_ERR_PAYLOAD_LEN:  return "OVERSIZED_PAYLOAD";
        case VK_ERR_CRC:          return "CRC_FAIL";
        case VK_ERR_REPLAY:       return "REPLAY";
        case VK_ERR_UNKNOWN_TYPE: return "UNKNOWN_TYPE";
        default:                  return "UNKNOWN_ERROR";
    }
}
