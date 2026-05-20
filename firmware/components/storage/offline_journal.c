/**
 * @file offline_journal.c
 * @brief Journal binário append-only — implementação
 *
 * RECORD FORMAT ON DISK (v2 — power-failure safe):
 *   Byte 0:     sentinel 0xAA — torn-write detector (if missing → record incomplete)
 *   Byte 1-2:   CRC16/CCITT (poly 0x1021, init 0xFFFF) — sobre bytes 3..end
 *   Byte 3:     type (journal_record_type_t)
 *   Byte 4-7:   timestamp_ms (uint32 LE)
 *   Byte 8-9:   payload_len (uint16 LE)
 *   Byte 10+:   payload[payload_len]
 *
 *   Total: 10 + payload_len bytes por record.
 *   CRC sobre: bytes 3..10+payload_len-1 (type + ts + len + payload).
 *
 * POWER-FAILURE SAFETY:
 *   fflush() + fsync() called after every write to flush FatFS sector cache
 *   to SD card hardware buffer. Sentinel byte 0xAA is written before CRC and
 *   body — if power is lost after sentinel but before body, replay detects
 *   the missing sentinel on the next read (reads back 0xFF from unwritten flash)
 *   and resyncs forward. CRC still catches all partial body writes.
 *
 * CURSOR:
 *   NVS key "cursor" no namespace "vk_jrn" = posição do próximo record a ler.
 *   Atualizado APENAS após replay bem-sucedido.
 *   Não atualizado durante append → perda de energia não corrompe posição.
 *
 * DETECÇÃO DE TRUNCAMENTO:
 *   Ao tentar ler um record: se read() retornar menos bytes do que o header
 *   indica, ou se CRC não bater → record inválido → avança 1 byte e tenta
 *   próximo (backward scan).
 */

#include "offline_journal.h"
#include "../core/system_manager.h"
#include "sd_manager.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>     /* fsync(), fileno() */
#include <errno.h>

static const char *TAG = "JRNL";

/* =========================================================
 * ESTADO INTERNO
 * ========================================================= */
static SemaphoreHandle_t s_mutex;
static bool              s_initialized = false;
static uint32_t          s_cursor = 0;  /* byte offset no arquivo */

/* =========================================================
 * CRC-16/CCITT
 * (Mesma implementação de vk_protocol.c — copiada localmente
 *  para evitar dependência circular com o módulo de rede)
 * ========================================================= */
static uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/* =========================================================
 * NVS — cursor persistence
 * ========================================================= */
static esp_err_t cursor_load(void)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(OFFLINE_JOURNAL_NVS_NS, NVS_READONLY, &h);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        s_cursor = 0;
        return ESP_OK;
    }
    if (ret != ESP_OK) return ret;

    uint32_t val = 0;
    ret = nvs_get_u32(h, OFFLINE_JOURNAL_NVS_CURSOR, &val);
    nvs_close(h);
    if (ret == ESP_ERR_NVS_NOT_FOUND) { s_cursor = 0; return ESP_OK; }
    if (ret != ESP_OK) return ret;
    s_cursor = val;
    return ESP_OK;
}

static esp_err_t cursor_save(uint32_t value)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(OFFLINE_JOURNAL_NVS_NS, NVS_READWRITE, &h);
    if (ret != ESP_OK) return ret;
    ret = nvs_set_u32(h, OFFLINE_JOURNAL_NVS_CURSOR, value);
    if (ret == ESP_OK) ret = nvs_commit(h);
    nvs_close(h);
    return ret;
}

/* =========================================================
 * TRUNCATION — remove partial trailing records on init
 *
 * WHY: A power failure that occurs between fwrite(sentinel) and
 * fwrite(body) leaves a partial record at the file tail.  On every
 * subsequent replay, the resync loop walks over these bytes, generates
 * log noise, and wastes I/O.  This scan-and-truncate runs once at init
 * and cuts the file back to the byte immediately after the last valid,
 * CRC-verified record.
 *
 * ALGORITHM:
 *   Walk all records from byte 0 (not from cursor — the cursor only
 *   tracks replay progress, not file integrity).  Track `good_end` =
 *   file offset after the last successfully validated record.  If
 *   file_size > good_end, truncate there.
 *
 * SAFETY:
 *   Uses truncate(2) which is supported by the ESP-IDF FatFS VFS
 *   driver (available since IDF 4.4).  If truncation fails (e.g. SD
 *   card write-protected), the init still succeeds — the resync loop
 *   during replay handles the partial tail anyway.
 * ========================================================= */
static void journal_truncate_at_last_good_record(void)
{
    FILE *f = fopen(OFFLINE_JOURNAL_PATH, "rb");
    if (!f) return;  /* File does not exist yet — nothing to truncate */

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    rewind(f);

    if (file_size <= 0) { fclose(f); return; }

    long good_end     = 0;
    int  resync_count = 0;
    const int MAX_RESYNC = 1000;

    uint8_t header[10];
    uint8_t payload_buf[OFFLINE_JOURNAL_MAX_PAYLOAD];

    while (resync_count < MAX_RESYNC) {
        long record_start = ftell(f);
        size_t n = fread(header, 1, 10, f);
        if (n < 10) break;  /* EOF or short header — stop scan */

        if (header[0] != 0xAAu) {
            fseek(f, record_start + 1, SEEK_SET);
            resync_count++;
            continue;
        }

        uint16_t stored_crc  = (uint16_t)(header[1] | ((uint16_t)header[2] << 8));
        uint16_t payload_len = (uint16_t)(header[8] | ((uint16_t)header[9] << 8));

        if (payload_len > OFFLINE_JOURNAL_MAX_PAYLOAD) {
            fseek(f, record_start + 1, SEEK_SET);
            resync_count++;
            continue;
        }

        if (payload_len > 0) {
            size_t pr = fread(payload_buf, 1, payload_len, f);
            if (pr < payload_len) break;  /* Partial payload — truncation boundary */
        }

        /* Verify CRC over: type(1) + ts(4) + len(2) + payload */
        uint8_t body_for_crc[7 + OFFLINE_JOURNAL_MAX_PAYLOAD];
        memcpy(body_for_crc, &header[3], 7);
        if (payload_len > 0) memcpy(&body_for_crc[7], payload_buf, payload_len);
        uint16_t calc_crc = crc16_ccitt(body_for_crc, 7u + payload_len);

        if (calc_crc != stored_crc) {
            fseek(f, record_start + 1, SEEK_SET);
            resync_count++;
            continue;
        }

        /* Valid complete record — advance the good boundary */
        good_end = ftell(f);
        resync_count = 0;
    }

    fclose(f);

    /* Adjust cursor if it somehow points past the good boundary
     * (e.g. file was partially truncated externally) */
    if (s_cursor > (uint32_t)good_end) {
        ESP_LOGW(TAG, "Cursor %lu > good_end %ld — resetting cursor",
                 (unsigned long)s_cursor, good_end);
        s_cursor = (uint32_t)good_end;
        cursor_save(s_cursor);
    }

    if (file_size > good_end) {
        ESP_LOGW(TAG, "Journal tail: %ld garbage bytes after last good record "
                 "(file_size=%ld good_end=%ld) — truncating",
                 file_size - good_end, file_size, good_end);
        if (truncate(OFFLINE_JOURNAL_PATH, (off_t)good_end) != 0) {
            /* Non-fatal: replay resync handles partial tail anyway */
            ESP_LOGW(TAG, "truncate() failed errno=%d — replay will resync", errno);
        }
    }
}

/* =========================================================
 * INICIALIZAÇÃO
 * ========================================================= */
esp_err_t offline_journal_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    esp_err_t ret = cursor_load();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Não foi possível carregar cursor NVS: %s", esp_err_to_name(ret));
        s_cursor = 0;
    }

    s_initialized = sd_manager_is_mounted();
    if (!s_initialized) {
        ESP_LOGW(TAG, "SD não montado — journal indisponível");
        return ESP_ERR_INVALID_STATE;
    }

    /* Truncate any partial trailing records left by a prior power failure.
     * This is a one-time O(file_size) scan — acceptable at boot time.
     * Prevents resync noise on every subsequent replay call. */
    journal_truncate_at_last_good_record();

    ESP_LOGI(TAG, "Inicializado (cursor=%lu)", (unsigned long)s_cursor);
    system_manager_report_subsystem(SUBSYS_JOURNAL, true);
    return ESP_OK;
}

bool offline_journal_is_available(void)
{
    return s_initialized && sd_manager_is_mounted();
}

/* =========================================================
 * APPEND
 * ========================================================= */
esp_err_t offline_journal_append(journal_record_type_t type,
                                  const uint8_t *payload, uint16_t len)
{
    if (!offline_journal_is_available()) return ESP_ERR_INVALID_STATE;
    if (len > OFFLINE_JOURNAL_MAX_PAYLOAD) return ESP_ERR_INVALID_ARG;

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_OK;
    FILE *f = fopen(OFFLINE_JOURNAL_PATH, "ab");
    if (!f) {
        ESP_LOGE(TAG, "Falha ao abrir journal para append");
        xSemaphoreGive(s_mutex);
        return ESP_FAIL;
    }

    /* RECORD FORMAT ON DISK (v2 — power-failure safe):
     *   Byte 0:   sentinel 0xAA — torn-write detector
     *   Byte 1-2: CRC16/CCITT (over bytes 3..end)
     *   Byte 3:   type
     *   Byte 4-7: ts_ms
     *   Byte 8-9: payload_len
     *   Byte 10+: payload[payload_len]
     */
    const uint8_t sentinel = 0xAAu;

    /* Montar record body (sem CRC e sem sentinel) */
    /* body = [type(1)][ts(4)][len(2)][payload] */
    uint8_t body[9 + OFFLINE_JOURNAL_MAX_PAYLOAD];
    uint32_t ts_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

    body[0] = (uint8_t)type;
    body[1] = (uint8_t)(ts_ms & 0xFF);
    body[2] = (uint8_t)((ts_ms >> 8)  & 0xFF);
    body[3] = (uint8_t)((ts_ms >> 16) & 0xFF);
    body[4] = (uint8_t)((ts_ms >> 24) & 0xFF);
    body[5] = (uint8_t)(len & 0xFF);
    body[6] = (uint8_t)((len >> 8) & 0xFF);
    if (len > 0 && payload) {
        memcpy(&body[7], payload, len);
    }
    size_t body_len = 7u + len;

    /* CRC sobre o body */
    uint16_t crc = crc16_ccitt(body, body_len);
    uint8_t crc_bytes[2] = { (uint8_t)(crc & 0xFF), (uint8_t)((crc >> 8) & 0xFF) };

    /* Escrever: sentinel | CRC | body */
    if (fwrite(&sentinel, 1, 1, f) != 1 ||
        fwrite(crc_bytes, 1, 2, f) != 2 ||
        fwrite(body, 1, body_len, f) != body_len) {
        ESP_LOGE(TAG, "Falha ao escrever record");
        ret = ESP_FAIL;
    }

    if (ret == ESP_OK) {
        /* Flush to FAT layer + fsync to SD card hardware buffer.
         * Required for power-failure safety: without fsync, the write
         * may still be in the FatFS sector cache and lost on power loss. */
        if (fflush(f) != 0 || fsync(fileno(f)) != 0) {
            ESP_LOGW(TAG, "fsync falhou — record pode nao ser persistido");
            /* Do NOT fail the return — best-effort persistence */
        }
    }

    fclose(f);
    xSemaphoreGive(s_mutex);

    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "Record appended type=0x%02X len=%u", (unsigned)type, (unsigned)len);
    }
    return ret;
}

/* =========================================================
 * REPLAY
 * ========================================================= */
int offline_journal_replay(journal_record_cb_t cb, void *arg)
{
    if (!offline_journal_is_available() || !cb) return -1;

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return -1;

    FILE *f = fopen(OFFLINE_JOURNAL_PATH, "rb");
    if (!f) {
        xSemaphoreGive(s_mutex);
        return 0;
    }

    if (fseek(f, (long)s_cursor, SEEK_SET) != 0) {
        fclose(f);
        xSemaphoreGive(s_mutex);
        return -1;
    }

    int replayed = 0;
    uint8_t header[10];  /* sentinel(1) + CRC(2) + type(1) + ts(4) + len(2) */
    uint8_t payload_buf[OFFLINE_JOURNAL_MAX_PAYLOAD];
    int resync_count = 0;
    const int MAX_RESYNC = 1000;  /* guard against infinite loop on corrupt file */

    while (resync_count < MAX_RESYNC) {
        size_t n = fread(header, 1, 10, f);
        if (n < 10) break;  /* EOF or truncation in header */

        /* Sentinel check — detect torn writes */
        if (header[0] != 0xAAu) {
            ESP_LOGW(TAG, "Sentinel faltando (0x%02X) — possivel truncamento", header[0]);
            fseek(f, -(long)(n - 1), SEEK_CUR);
            resync_count++;
            continue;
        }

        uint16_t stored_crc = (uint16_t)(header[1] | ((uint16_t)header[2] << 8));
        uint16_t payload_len = (uint16_t)(header[8] | ((uint16_t)header[9] << 8));

        if (payload_len > OFFLINE_JOURNAL_MAX_PAYLOAD) {
            /* Record inválido — avançar 1 byte e re-tentar */
            ESP_LOGW(TAG, "Record inválido (len=%u) — skipping", payload_len);
            fseek(f, -(long)(10 - 1), SEEK_CUR);
            resync_count++;
            continue;
        }

        if (payload_len > 0) {
            size_t pr = fread(payload_buf, 1, payload_len, f);
            if (pr < payload_len) break;  /* Truncado */
        }

        /* Verificar CRC: calculado sobre header[3..9] (type+ts+len) + payload */
        uint8_t body_for_crc[9 + OFFLINE_JOURNAL_MAX_PAYLOAD];
        memcpy(body_for_crc, &header[3], 7);  /* type + ts + len */
        if (payload_len > 0) memcpy(&body_for_crc[7], payload_buf, payload_len);
        uint16_t calc_crc = crc16_ccitt(body_for_crc, 7u + payload_len);

        if (calc_crc != stored_crc) {
            ESP_LOGW(TAG, "CRC inválido — record skipped");
            /* Retroceder e avançar 1 byte para tentar re-sync */
            long pos = ftell(f);
            fseek(f, pos - (long)(10 + payload_len - 1), SEEK_SET);
            resync_count++;
            continue;
        }

        journal_record_type_t type = (journal_record_type_t)header[3];
        bool cont = cb(type, payload_buf, payload_len, arg);
        replayed++;
        resync_count = 0;  /* Reset resync counter on successful record */

        if (!cont) break;
    }

    if (resync_count >= MAX_RESYNC) {
        ESP_LOGE(TAG, "Replay abortado: limite de resync atingido (arquivo corrompido?)");
    }

    /* Salvar novo cursor = posição atual */
    long new_cursor = ftell(f);
    if (new_cursor > 0) {
        s_cursor = (uint32_t)new_cursor;
        cursor_save(s_cursor);
    }

    fclose(f);
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "Replay: %d records processados (cursor=%lu)", replayed, (unsigned long)s_cursor);
    return replayed;
}

/* =========================================================
 * PENDING BYTES
 * ========================================================= */
uint32_t offline_journal_pending_bytes(void)
{
    if (!offline_journal_is_available()) return 0;

    FILE *f = fopen(OFFLINE_JOURNAL_PATH, "rb");
    if (!f) return 0;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);

    if (size < 0 || (uint32_t)size <= s_cursor) return 0;
    return (uint32_t)size - s_cursor;
}

/* =========================================================
 * PURGE
 * ========================================================= */
esp_err_t offline_journal_purge(void)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return ESP_ERR_TIMEOUT;

    remove(OFFLINE_JOURNAL_PATH);
    s_cursor = 0;
    esp_err_t ret = cursor_save(0);

    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "Journal purgado");
    return ret;
}
