/**
 * @file hdc1080.h
 * @brief Driver para sensor de temperatura e umidade HDC1080 (I2C)
 */

#ifndef VITALKARE_HDC1080_H
#define VITALKARE_HDC1080_H

#include "esp_err.h"
#include <stdbool.h>

typedef struct {
    float temperature_celsius;
    float humidity_percent;
    bool  valid;
} hdc1080_reading_t;

/** @brief Inicializa o barramento I2C e verifica presença do HDC1080. */
esp_err_t hdc1080_init(void);

/**
 * @brief Realiza uma leitura de temperatura e umidade.
 * @param out Estrutura de saída preenchida com os valores lidos.
 * @return ESP_OK em sucesso; ESP_ERR_TIMEOUT se sensor não responder.
 */
esp_err_t hdc1080_read(hdc1080_reading_t *out);

#endif /* VITALKARE_HDC1080_H */
