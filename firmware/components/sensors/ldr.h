/**
 * @file ldr.h
 * @brief Leitura de luminosidade via ADC (LDR)
 */

#ifndef VITALKARE_LDR_H
#define VITALKARE_LDR_H

#include "esp_err.h"
#include <stdbool.h>

esp_err_t ldr_init(void);

/**
 * @brief Realiza leitura e converte para lux estimado.
 * @param out_lux  Luminosidade estimada em lux.
 * @param out_raw  Valor bruto ADC 12-bit (0–4095), nullable.
 */
esp_err_t ldr_read(float *out_lux, uint16_t *out_raw);

#endif /* VITALKARE_LDR_H */
