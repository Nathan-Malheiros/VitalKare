/**
 * @file max30102.h
 * @brief Driver para oxímetro de pulso MAX30102 — VitalKare Firmware
 *
 * Sensor de SpO2 e BPM via I2C, endereço 0x57.
 * Usa o barramento I2C compartilhado (i2c_bus) já inicializado
 * com SDA=GPIO14, SCL=GPIO21.
 *
 * FUNCIONALIDADES:
 *   - Inicialização com configuração otimizada para SpO2 + BPM
 *   - Leitura contínua do FIFO (RED + IR, 18-bit por sample)
 *   - Detecção de dedo por limiar do canal IR
 *   - Cálculo de BPM por detecção de picos no sinal IR
 *   - Cálculo de SpO2 pela razão AC/DC de RED vs IR
 *   - Task de polling a 1 Hz com reporte via event dispatcher
 */

#ifndef VITALKARE_MAX30102_H
#define VITALKARE_MAX30102_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================
 * RESULTADO DE UMA LEITURA
 * ========================================================= */
typedef struct {
    uint8_t  bpm;               /**< Frequência cardíaca (batimentos/min) */
    uint8_t  spo2_pct;          /**< SpO2 (%) */
    uint8_t  confidence;        /**< 0-100 — qualidade do sinal */
    bool     finger_detected;   /**< true se dedo detectado */
    uint32_t ir_raw;            /**< Valor bruto IR (último sample) */
    uint32_t red_raw;           /**< Valor bruto RED (último sample) */
} max30102_reading_t;

/* =========================================================
 * API PÚBLICA
 * ========================================================= */

/**
 * @brief Inicializa o MAX30102 via I2C.
 * Verifica Part ID (0x15), configura modo SpO2 + LED currents.
 * @return ESP_OK se o sensor foi encontrado e configurado.
 */
esp_err_t max30102_init(void);

/**
 * @brief Lê amostras do FIFO e atualiza a struct de leitura.
 * @param[out] out Preenchido com BPM, SpO2 e flags de qualidade.
 * @return ESP_OK se a leitura foi bem-sucedida.
 */
esp_err_t max30102_read(max30102_reading_t *out);

/** @brief Task de polling do oxímetro — não chamar diretamente. */
void task_oximeter_poll(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* VITALKARE_MAX30102_H */
