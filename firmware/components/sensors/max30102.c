/**
 * @file max30102.c
 * @brief Driver MAX30102 — SpO2 + BPM via I2C
 *
 * Mapa de registradores utilizado:
 *   0x00 — Interrupt Status 1
 *   0x08 — FIFO Write Pointer
 *   0x09 — Overflow Counter
 *   0x0A — FIFO Read Pointer
 *   0x07 — FIFO Data Register (leitura de 6 bytes = RED[18b] + IR[18b])
 *   0x09 — FIFO Configuration (SPO2 mode: 16 samples avg, no rollover)
 *   0x0C — Mode Configuration (SpO2 mode = 0x03)
 *   0x0D — SpO2 Configuration (100 sps, 411µs pulse width, 4096 ADC range)
 *   0x0E — LED1 Pulse Amplitude (RED)
 *   0x0F — LED2 Pulse Amplitude (IR)
 *   0xFE — Revision ID
 *   0xFF — Part ID (deve ser 0x15)
 */

#include "max30102.h"
#include "../i2c_bus/i2c_bus.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "../../config/app_config.h"
#include "../events/event_dispatcher.h"
#include "../network/tcp_client.h"
#include "../network/vk_protocol.h"
#include <string.h>
#include <math.h>

static const char *TAG = "MAX30102";

/* =========================================================
 * CONSTANTES DO MAX30102
 * ========================================================= */
#define MAX30102_I2C_ADDR           0x57

#define REG_INT_STATUS1             0x00
#define REG_FIFO_WR_PTR             0x04
#define REG_OVF_COUNTER             0x05
#define REG_FIFO_RD_PTR             0x06
#define REG_FIFO_DATA               0x07
#define REG_FIFO_CONFIG             0x08
#define REG_MODE_CONFIG             0x09
#define REG_SPO2_CONFIG             0x0A
#define REG_LED1_PA                 0x0C    /* RED LED pulse amplitude */
#define REG_LED2_PA                 0x0D    /* IR LED pulse amplitude */
#define REG_PART_ID                 0xFF
#define REG_REV_ID                  0xFE

#define MAX30102_PART_ID            0x15
#define MODE_SPO2                   0x03
#define FIFO_SAMPLES_PER_READ       4       /* Amostras por leitura de FIFO */
#define DC_FILTER_ALPHA             0.95f   /* Coeficiente filtro passa-alta */
#define BPM_WINDOW_SAMPLES          (APP_OXIMETER_SAMPLE_RATE_HZ * 6)  /* 6 s */

/* =========================================================
 * ESTADO INTERNO
 * ========================================================= */
static bool                    s_initialized = false;
/* Handle do dispositivo I2C — inicializado uma única vez em max30102_init()
 * CORRIGIDO: anteriormente criado/destruído em cada reg_read/reg_write (a cada 1 s),
 * causando fragmentação contínua do heap interno. */
static i2c_master_dev_handle_t s_dev_handle  = NULL;

/* Filtro DC (passa-alta) para detecção de picos */
static float    s_ir_dc   = 0.0f;
static float    s_red_dc  = 0.0f;

/* Buffer circular para BPM */
#define PEAK_BUF  32
static uint32_t s_ir_buf[BPM_WINDOW_SAMPLES];
static int      s_buf_idx   = 0;
static int      s_buf_count = 0;

/* Última leitura calculada */
static max30102_reading_t s_last = {0};

/* =========================================================
 * HELPERS I2C
 * ========================================================= */
static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    if (!s_dev_handle) return ESP_ERR_INVALID_STATE;
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(s_dev_handle, buf, 2, pdMS_TO_TICKS(20));
}

static esp_err_t reg_read(uint8_t reg, uint8_t *data, size_t len)
{
    if (!s_dev_handle) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit_receive(s_dev_handle, &reg, 1, data, len,
                                       pdMS_TO_TICKS(20));
}

/* =========================================================
 * INICIALIZAÇÃO
 * ========================================================= */
esp_err_t max30102_init(void)
{
    if (s_initialized) return ESP_OK;

    /* Cria e armazena o handle do dispositivo I2C uma única vez */
    i2c_master_bus_handle_t bus = i2c_bus_get_handle();
    if (!bus) {
        ESP_LOGE(TAG, "I2C bus não inicializado");
        return ESP_ERR_INVALID_STATE;
    }
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = MAX30102_I2C_ADDR,
        .scl_speed_hz    = 400000,
    };
    esp_err_t ret = i2c_master_bus_add_device(bus, &dev_cfg, &s_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Verifica Part ID */
    uint8_t part_id = 0;
    ret = reg_read(REG_PART_ID, &part_id, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao ler Part ID: %s", esp_err_to_name(ret));
        return ret;
    }
    if (part_id != MAX30102_PART_ID) {
        ESP_LOGE(TAG, "Part ID incorreto: 0x%02X (esperado 0x%02X)",
                 part_id, MAX30102_PART_ID);
        i2c_master_bus_rm_device(s_dev_handle);
        s_dev_handle = NULL;
        return ESP_ERR_NOT_FOUND;
    }

    /* Reset suave */
    reg_write(REG_MODE_CONFIG, 0x40);
    vTaskDelay(pdMS_TO_TICKS(10));

    /* FIFO: 4 amostras média, sem rollover */
    reg_write(REG_FIFO_CONFIG, 0x4F);   /* SMP_AVE=4, FIFO_ROLLOVER=0, FIFO_A_FULL=15 */

    /* Modo SpO2 */
    reg_write(REG_MODE_CONFIG, MODE_SPO2);

    /* SpO2: 100 sps, 411µs, ADC_RGE=4096 nA */
    reg_write(REG_SPO2_CONFIG, 0x27);   /* SPO2_ADC_RGE=0b11, SPO2_SR=0b001, LED_PW=0b11 */

    /* Amplitude dos LEDs: ~7 mA */
    reg_write(REG_LED1_PA, 0x24);
    reg_write(REG_LED2_PA, 0x24);

    s_initialized = true;
    ESP_LOGI(TAG, "MAX30102 inicializado (addr=0x%02X)", MAX30102_I2C_ADDR);
    return ESP_OK;
}

/* =========================================================
 * DETECÇÃO DE PICOS → BPM
 * ========================================================= */
static uint8_t calc_bpm(void)
{
    if (s_buf_count < APP_OXIMETER_SAMPLE_RATE_HZ * 2) return 0; /* min 2 s */

    /* Conta cruzamentos ascendentes pelo zero do sinal filtrado */
    int window = (s_buf_count < BPM_WINDOW_SAMPLES)
                 ? s_buf_count : BPM_WINDOW_SAMPLES;
    int peaks = 0;
    int start = (s_buf_idx - window + BPM_WINDOW_SAMPLES) % BPM_WINDOW_SAMPLES;

    /* Calcula média do sinal para threshold adaptativo */
    float mean = 0.0f;
    for (int i = 0; i < window; i++) {
        int idx = (start + i) % BPM_WINDOW_SAMPLES;
        mean += (float)s_ir_buf[idx];
    }
    mean /= window;

    bool was_above = false;
    for (int i = 0; i < window; i++) {
        int idx = (start + i) % BPM_WINDOW_SAMPLES;
        bool above = (float)s_ir_buf[idx] > mean;
        if (above && !was_above) peaks++;
        was_above = above;
    }

    float duration_s = (float)window / (float)APP_OXIMETER_SAMPLE_RATE_HZ;
    float bpm = ((float)peaks / duration_s) * 60.0f;

    if (bpm < 30.0f || bpm > 220.0f) return 0;
    return (uint8_t)bpm;
}

/* =========================================================
 * CÁLCULO SpO2 (razão R = (AC_RED/DC_RED) / (AC_IR/DC_IR))
 * Aproximação: SpO2 = 110 - 25 * R  (calibração empírica típica)
 * ========================================================= */
static uint8_t calc_spo2(float ir_ac, float ir_dc, float red_ac, float red_dc)
{
    if (ir_dc < 1.0f || red_dc < 1.0f) return 0;

    float ratio_r = (red_ac / red_dc) / (ir_ac / ir_dc);
    float spo2    = 110.0f - 25.0f * ratio_r;

    if (spo2 < 70.0f)  spo2 = 70.0f;
    if (spo2 > 100.0f) spo2 = 100.0f;
    return (uint8_t)spo2;
}

/* =========================================================
 * LEITURA DO FIFO
 * ========================================================= */
esp_err_t max30102_read(max30102_reading_t *out)
{
    if (!s_initialized || !out) return ESP_ERR_INVALID_STATE;

    uint8_t wr_ptr, rd_ptr, ovf;
    reg_read(REG_FIFO_WR_PTR, &wr_ptr, 1);
    reg_read(REG_OVF_COUNTER, &ovf, 1);
    reg_read(REG_FIFO_RD_PTR, &rd_ptr, 1);

    int num_samples = ((int)wr_ptr - (int)rd_ptr + 32) % 32;
    if (num_samples == 0) {
        *out = s_last;
        return ESP_OK;
    }

    float ir_ac_sum = 0.0f, red_ac_sum = 0.0f;
    uint32_t last_ir = 0, last_red = 0;
    int valid = 0;

    for (int i = 0; i < num_samples && i < FIFO_SAMPLES_PER_READ; i++) {
        uint8_t raw[6];
        if (reg_read(REG_FIFO_DATA, raw, 6) != ESP_OK) break;

        uint32_t red = ((uint32_t)(raw[0] & 0x03) << 16)
                     | ((uint32_t)raw[1] << 8) | raw[2];
        uint32_t ir  = ((uint32_t)(raw[3] & 0x03) << 16)
                     | ((uint32_t)raw[4] << 8) | raw[5];

        /* Filtro DC (passa-alta via IIR) */
        s_ir_dc  = s_ir_dc  * DC_FILTER_ALPHA + (float)ir  * (1.0f - DC_FILTER_ALPHA);
        s_red_dc = s_red_dc * DC_FILTER_ALPHA + (float)red * (1.0f - DC_FILTER_ALPHA);

        float ir_ac  = (float)ir  - s_ir_dc;
        float red_ac = (float)red - s_red_dc;

        ir_ac_sum  += fabsf(ir_ac);
        red_ac_sum += fabsf(red_ac);

        /* Buffer BPM */
        s_ir_buf[s_buf_idx] = ir;
        s_buf_idx = (s_buf_idx + 1) % BPM_WINDOW_SAMPLES;
        if (s_buf_count < BPM_WINDOW_SAMPLES) s_buf_count++;

        last_ir  = ir;
        last_red = red;
        valid++;
    }

    if (valid == 0) {
        *out = s_last;
        return ESP_OK;
    }

    float ir_ac_mean  = ir_ac_sum  / valid;
    float red_ac_mean = red_ac_sum / valid;

    bool finger = (last_ir > APP_OXIMETER_FINGER_THRESHOLD);

    out->ir_raw          = last_ir;
    out->red_raw         = last_red;
    out->finger_detected = finger;

    if (finger) {
        out->bpm      = calc_bpm();
        out->spo2_pct = calc_spo2(ir_ac_mean, s_ir_dc, red_ac_mean, s_red_dc);
        out->confidence = (out->bpm > 0) ? 80 : 30;
    } else {
        out->bpm        = 0;
        out->spo2_pct   = 0;
        out->confidence = 0;
    }

    s_last = *out;
    return ESP_OK;
}

/* =========================================================
 * TASK DE POLLING
 * ========================================================= */
void task_oximeter_poll(void *arg)
{
    ESP_LOGI(TAG, "task_oximeter_poll iniciada");

    if (!s_initialized) {
        ESP_LOGW(TAG, "MAX30102 nao disponivel — task encerrada");
        vTaskDelete(NULL);
        return;
    }

    TickType_t  last_report = xTaskGetTickCount();
    const TickType_t report_ticks = pdMS_TO_TICKS(APP_OXIMETER_REPORT_INTERVAL_MS);
    bool        last_finger = false;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(APP_OXIMETER_POLL_INTERVAL_MS));

        max30102_reading_t reading = {0};
        esp_err_t ret = max30102_read(&reading);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Falha na leitura: %s", esp_err_to_name(ret));
            continue;
        }

        /* Detecta remoção do dedo */
        if (!reading.finger_detected && last_finger) {
            system_event_t evt = {
                .type         = EVT_OXIMETER_FINGER_LOST,
                .priority     = EVT_PRIO_MEDIUM,
                .timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS,
            };
            event_post_normal(&evt);
        }
        last_finger = reading.finger_detected;

        /* Envia telemetria periodicamente se dedo detectado */
        if (reading.finger_detected &&
            (xTaskGetTickCount() - last_report) >= report_ticks) {

            last_report = xTaskGetTickCount();

            pkt_telemetry_oximeter_t pkt = {
                .bpm            = reading.bpm,
                .spo2_pct       = reading.spo2_pct,
                .finger_detected= (uint8_t)reading.finger_detected,
                .confidence     = reading.confidence,
                .timestamp_ms   = xTaskGetTickCount() * portTICK_PERIOD_MS,
            };

            static uint8_t frame_buf[VK_HEADER_SIZE
                                     + sizeof(pkt_telemetry_oximeter_t)
                                     + VK_CRC_SIZE];
            int flen = vk_frame_build(frame_buf, sizeof(frame_buf),
                                      PKT_TELEMETRY_OXIMETER,
                                      tcp_client_next_seq(),
                                      &pkt, sizeof(pkt));
            if (flen > 0) {
                tcp_client_send_normal(frame_buf, (uint16_t)flen);
            }

            ESP_LOGI(TAG, "BPM=%d SpO2=%d%% confianca=%d",
                     reading.bpm, reading.spo2_pct, reading.confidence);
        }
    }
}
