/**
 * @file vk_protocol.h
 * @brief VitalKare Wire Protocol — Framing binário sobre TCP
 *
 * ESTRUTURA DO PACOTE:
 * ┌──────────┬──────────┬──────────┬──────────┬──────────────────┬──────────┐
 * │ MAGIC[0] │ MAGIC[1] │ VERSION  │ PKT_TYPE │ PAYLOAD_LEN (4B) │ SEQ (1B) │
 * │  0x56    │  0x4B    │   0x01   │  uint8   │   uint32_le      │  uint8   │
 * ├──────────┴──────────┴──────────┴──────────┴──────────────────┴──────────┤
 * │                    PAYLOAD  (0..8192 bytes)                              │
 * ├─────────────────────────────────────────────────────────────────────────┤
 * │  CRC-16/CCITT  (2 bytes LE — cobre header + payload)                    │
 * └─────────────────────────────────────────────────────────────────────────┘
 *
 * Header: 9 bytes fixos
 * Trailer: 2 bytes CRC
 * Total mínimo: 11 bytes (pacote sem payload)
 * Total máximo: 9 + 8192 + 2 = 8203 bytes
 */

#ifndef VITALKARE_VK_PROTOCOL_H
#define VITALKARE_VK_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================
 * CONSTANTES DE FRAMING
 * ========================================================= */
#define VK_MAGIC_0              0x56u       /**< 'V' */
#define VK_MAGIC_1              0x4Bu       /**< 'K' */
#define VK_PROTOCOL_VERSION     0x01u

#define VK_HEADER_SIZE          9u          /**< Bytes do header (sem CRC) */
#define VK_CRC_SIZE             2u
#define VK_MIN_FRAME_SIZE       (VK_HEADER_SIZE + VK_CRC_SIZE)
#define VK_MAX_PAYLOAD_SIZE     8192u
#define VK_MAX_FRAME_SIZE       (VK_HEADER_SIZE + VK_MAX_PAYLOAD_SIZE + VK_CRC_SIZE)

/* =========================================================
 * TIPOS DE PACOTE
 * ========================================================= */
typedef enum {
    /* ── Conexão ──────────────────────────────────────── */
    PKT_HANDSHAKE_HELLO      = 0x01,    /**< Dispositivo → Backend: identificação */
    PKT_HANDSHAKE_ACK        = 0x02,    /**< Backend → Dispositivo: aceito */
    PKT_HEARTBEAT_PING       = 0x03,    /**< Dispositivo → Backend */
    PKT_HEARTBEAT_PONG       = 0x04,    /**< Backend → Dispositivo */

    /* ── Eventos de Paciente ──────────────────────────── */
    PKT_EVENT_REQUEST        = 0x10,    /**< Solicitação normal (Botão A) */
    PKT_EVENT_EMERGENCY      = 0x11,    /**< Emergência (Botão B) */
    PKT_EVENT_ACK            = 0x12,    /**< Operador confirmou atendimento */
    PKT_EVENT_CANCEL         = 0x13,    /**< Evento cancelado */

    /* ── Áudio ────────────────────────────────────────── */
    PKT_AUDIO_SESSION_START  = 0x20,    /**< Início de sessão de áudio */
    PKT_AUDIO_CHUNK          = 0x21,    /**< Chunk ADPCM (payload = dados comprimidos) */
    PKT_AUDIO_SESSION_END    = 0x22,    /**< Fim de sessão de áudio */

    /* ── Telemetria de Sensores ───────────────────────── */
    PKT_TELEMETRY_ENV        = 0x30,    /**< Temperatura, umidade, luminosidade */
    PKT_TELEMETRY_MOTION     = 0x31,    /**< Dados do MPU6050 */
    PKT_TELEMETRY_OXIMETER   = 0x32,    /**< BPM + SpO2 do MAX30102 */
    PKT_TELEMETRY_SYSTEM     = 0x33,    /**< Heap, uptime, RSSI */

    /* ── Imagem ───────────────────────────────────────── */
    PKT_IMAGE_FRAME          = 0x40,    /**< Frame JPEG da câmera */
    PKT_IMAGE_STREAM_START   = 0x41,
    PKT_IMAGE_STREAM_STOP    = 0x42,

    /* ── Comandos (Backend → Dispositivo) ─────────────── */
    PKT_CMD_DISPLAY_TEXT     = 0x50,    /**< Exibir texto no OLED */
    PKT_CMD_DISPLAY_CLEAR    = 0x51,
    PKT_CMD_BUZZER           = 0x52,    /**< Ativar buzzer (frequência, duração) */
    PKT_CMD_LED              = 0x53,    /**< Controlar LED */
    PKT_CMD_REQUEST_IMAGE    = 0x54,    /**< Solicitar captura de imagem */
    PKT_CMD_REQUEST_TELEMETRY= 0x55,    /**< Solicitar leitura imediata de sensores */

    /* ── OTA / Sistema ────────────────────────────────── */
    PKT_OTA_START            = 0x60,    /**< Início de atualização OTA */
    PKT_OTA_CHUNK            = 0x61,    /**< Bloco de firmware */
    PKT_OTA_COMMIT           = 0x62,    /**< Confirma OTA e reinicia */
    PKT_OTA_ABORT            = 0x63,    /**< Aborta OTA */
    PKT_CMD_REBOOT           = 0x6F,    /**< Reinicia dispositivo */

    /* ── Erros ────────────────────────────────────────── */
    PKT_ERROR                = 0xFF,    /**< Pacote de erro genérico */
} pkt_type_t;

/* =========================================================
 * HEADER (packed — exatamente 9 bytes na wire)
 * ========================================================= */
#pragma pack(push, 1)

typedef struct {
    uint8_t  magic[2];      /**< {0x56, 0x4B} */
    uint8_t  version;       /**< VK_PROTOCOL_VERSION */
    uint8_t  pkt_type;      /**< pkt_type_t */
    uint32_t payload_len;   /**< Tamanho do payload em bytes (little-endian) */
    uint8_t  seq;           /**< Número de sequência — 1 byte, sizeof(vk_header_t)==VK_HEADER_SIZE==9 */
} vk_header_t;

/* ── Payloads de pacotes específicos ── */

typedef struct {
    char     device_id[32];     /**< ID único do dispositivo */
    char     ward_id[16];       /**< Identificador do leito/ala */
    char     fw_version[16];    /**< Versão do firmware */
    uint32_t capabilities;      /**< Bitmask de capacidades */
} pkt_handshake_hello_t;

typedef struct {
    uint8_t  status;            /**< 0=ok, 1=token_inválido */
    char     session_token[32]; /**< Token para autenticação subsequente */
} pkt_handshake_ack_t;

typedef struct {
    uint32_t uptime_ms;
    int8_t   rssi;
    uint8_t  padding[3];
} pkt_heartbeat_t;

typedef struct {
    uint32_t timestamp_unix;    /**< Epoch seconds */
    uint8_t  trigger;           /**< 0=request, 1=emergency */
} pkt_event_t;

typedef struct {
    uint32_t session_id;        /**< ID único da sessão de áudio */
    uint8_t  trigger;           /**< 0=request, 1=emergency */
    uint16_t sample_rate_hz;    /**< Ex: 16000 */
    uint8_t  bits_per_sample;   /**< Ex: 16 (antes da compressão) */
    uint8_t  codec;             /**< 0=PCM, 1=IMA-ADPCM */
    uint8_t  channels;          /**< 1=mono */
} pkt_audio_session_start_t;

typedef struct {
    uint32_t session_id;
    uint16_t chunk_index;       /**< Índice sequencial do chunk (0-based) */
    uint16_t chunk_len;         /**< Bytes de dados ADPCM neste pacote */
    /* seguido de chunk_len bytes de dados ADPCM no payload */
} pkt_audio_chunk_header_t;

typedef struct {
    uint32_t session_id;
    uint16_t total_chunks;
    uint32_t total_pcm_samples; /**< Total de samples PCM originais */
} pkt_audio_session_end_t;

typedef struct {
    float    temperature_c;
    float    humidity_pct;
    uint16_t light_lux;
    uint16_t noise_db;
    uint32_t timestamp_ms;
} pkt_telemetry_env_t;

typedef struct {
    uint8_t  bpm;
    uint8_t  spo2_pct;
    uint8_t  finger_detected;
    uint8_t  confidence;        /**< 0-100 */
    uint32_t timestamp_ms;
} pkt_telemetry_oximeter_t;

typedef struct {
    uint32_t uptime_ms;
    int8_t   rssi;
    uint8_t  padding[3];
    uint32_t free_heap_bytes;
} pkt_telemetry_system_t;

#pragma pack(pop)

/* =========================================================
 * CRC-16/CCITT (polinômio 0x1021, init 0xFFFF)
 * ========================================================= */

/**
 * @brief Calcula CRC-16/CCITT sobre um bloco de dados.
 * @param data  Ponteiro para os dados.
 * @param len   Número de bytes.
 * @return CRC-16 de 16 bits.
 */
uint16_t vk_crc16(const uint8_t *data, size_t len);

/* =========================================================
 * FUNÇÕES DE FRAMING
 * ========================================================= */

/**
 * @brief Monta um frame VK completo no buffer de saída.
 *
 * @param out       Buffer de saída (deve ter pelo menos VK_MIN_FRAME_SIZE +
 *                  payload_len bytes).
 * @param out_size  Tamanho do buffer de saída.
 * @param pkt_type  Tipo do pacote.
 * @param seq       Número de sequência.
 * @param payload   Ponteiro para dados de payload (NULL para len=0).
 * @param payload_len Tamanho do payload em bytes.
 * @return Número de bytes escritos em out, ou -1 em caso de erro.
 */
int vk_frame_build(uint8_t *out, size_t out_size,
                   pkt_type_t pkt_type, uint16_t seq,
                   const void *payload, uint32_t payload_len);

/**
 * @brief Verifica se um buffer contém um frame VK completo e válido.
 *
 * @param buf       Buffer contendo dados recebidos.
 * @param buf_len   Bytes disponíveis no buffer.
 * @param[out] frame_len  Preenchido com o tamanho total do frame (header+payload+CRC).
 * @return true se o frame está completo e o CRC é válido.
 */
bool vk_frame_parse(const uint8_t *buf, size_t buf_len,
                    const vk_header_t **hdr_out,
                    const uint8_t **payload_out,
                    size_t *frame_len);

#ifdef __cplusplus
}
#endif

#endif /* VITALKARE_VK_PROTOCOL_H */
