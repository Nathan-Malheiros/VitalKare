/**
 * @file i2c_bus.h
 * @brief Barramento I2C master compartilhado — ESP-IDF v6 (driver/i2c_master.h)
 *
 * Inicializa o barramento I2C_NUM_0 uma única vez:
 *   SDA = GPIO14
 *   SCL = GPIO21
 *
 * Todos os drivers de sensores e display devem obter o handle via
 * i2c_bus_get_handle() e registrar seu dispositivo com
 * i2c_master_bus_add_device().
 */

#ifndef VITALKARE_I2C_BUS_H
#define VITALKARE_I2C_BUS_H

#include "esp_err.h"
#include "driver/i2c_master.h"

/**
 * @brief Inicializa o barramento I2C master (idempotente — thread-safe).
 *
 * Pode ser chamado múltiplas vezes com segurança; a inicialização
 * acontece apenas na primeira invocação.
 *
 * @return ESP_OK em sucesso.
 */
esp_err_t i2c_bus_init(void);

/**
 * @brief Retorna o handle do barramento I2C master.
 *
 * @note Deve ser chamado após i2c_bus_init().
 * @return Handle válido ou NULL se não inicializado.
 */
i2c_master_bus_handle_t i2c_bus_get_handle(void);

#endif /* VITALKARE_I2C_BUS_H */
