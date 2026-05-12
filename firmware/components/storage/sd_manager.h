/**
 * @file sd_manager.h
 * @brief Gerenciamento do cartão SD via SDMMC
 */

#ifndef VITALKARE_SD_MANAGER_H
#define VITALKARE_SD_MANAGER_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

esp_err_t sd_manager_init(void);
bool      sd_manager_is_mounted(void);
esp_err_t sd_manager_get_free_mb(uint32_t *out_free_mb);
esp_err_t sd_manager_mkdir(const char *path);

#endif /* VITALKARE_SD_MANAGER_H */
