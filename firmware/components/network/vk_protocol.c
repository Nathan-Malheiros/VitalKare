/**
 * @file vk_protocol.c
 * @brief VitalKare Wire Protocol — CRC-16 e funções de framing
 */

#include "vk_protocol.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "VK_PROTO";

/* =========================================================
 * CRC-16/CCITT — polinômio 0x1021, init 0xFFFF
 * ========================================================= */
uint16_t vk_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000u) {
                crc = (crc << 1) ^ 0x1021u;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

/* =========================================================
 * FRAMING — BUILD
 * ========================================================= */
int vk_frame_build(uint8_t *out, size_t out_size,
                   pkt_type_t pkt_type, uint16_t seq,
                   const void *payload, uint32_t payload_len)
{
    if (!out) return -1;
    if (payload_len > VK_MAX_PAYLOAD_SIZE) {
        ESP_LOGE(TAG, "payload_len %lu > max %u", (unsigned long)payload_len,
                 VK_MAX_PAYLOAD_SIZE);
        return -1;
    }

    size_t total = VK_HEADER_SIZE + payload_len + VK_CRC_SIZE;
    if (out_size < total) {
        ESP_LOGE(TAG, "buffer pequeno: %zu < %zu", out_size, total);
        return -1;
    }

    /* Header */
    vk_header_t *hdr = (vk_header_t *)out;
    hdr->magic[0]    = VK_MAGIC_0;
    hdr->magic[1]    = VK_MAGIC_1;
    hdr->version     = VK_PROTOCOL_VERSION;
    hdr->pkt_type    = (uint8_t)pkt_type;
    hdr->payload_len = payload_len;     /* little-endian no ESP32 */
    hdr->seq         = seq;

    /* Payload */
    if (payload && payload_len > 0) {
        memcpy(out + VK_HEADER_SIZE, payload, payload_len);
    }

    /* CRC — cobre header + payload */
    uint16_t crc = vk_crc16(out, VK_HEADER_SIZE + payload_len);
    out[VK_HEADER_SIZE + payload_len]     = (uint8_t)(crc & 0xFF);
    out[VK_HEADER_SIZE + payload_len + 1] = (uint8_t)(crc >> 8);

    return (int)total;
}

/* =========================================================
 * FRAMING — PARSE
 * ========================================================= */
bool vk_frame_parse(const uint8_t *buf, size_t buf_len,
                    const vk_header_t **hdr_out,
                    const uint8_t **payload_out,
                    size_t *frame_len)
{
    if (!buf || buf_len < VK_MIN_FRAME_SIZE) return false;

    /* Verifica magic */
    if (buf[0] != VK_MAGIC_0 || buf[1] != VK_MAGIC_1) {
        ESP_LOGW(TAG, "Magic inválido: 0x%02X 0x%02X", buf[0], buf[1]);
        return false;
    }

    const vk_header_t *hdr = (const vk_header_t *)buf;

    /* Verifica versão */
    if (hdr->version != VK_PROTOCOL_VERSION) {
        ESP_LOGW(TAG, "Versão desconhecida: %d", hdr->version);
        return false;
    }

    uint32_t plen = hdr->payload_len;
    if (plen > VK_MAX_PAYLOAD_SIZE) {
        ESP_LOGE(TAG, "payload_len anômalo: %lu", (unsigned long)plen);
        return false;
    }

    size_t total = VK_HEADER_SIZE + plen + VK_CRC_SIZE;
    if (buf_len < total) {
        /* Frame incompleto — aguardar mais dados */
        return false;
    }

    /* Verifica CRC */
    uint16_t expected = vk_crc16(buf, VK_HEADER_SIZE + plen);
    uint16_t received = (uint16_t)buf[VK_HEADER_SIZE + plen] |
                        ((uint16_t)buf[VK_HEADER_SIZE + plen + 1] << 8);
    if (expected != received) {
        ESP_LOGE(TAG, "CRC incorreto: exp=0x%04X recv=0x%04X", expected, received);
        return false;
    }

    if (hdr_out)     *hdr_out     = hdr;
    if (payload_out) *payload_out = (plen > 0) ? (buf + VK_HEADER_SIZE) : NULL;
    if (frame_len)   *frame_len   = total;

    return true;
}
