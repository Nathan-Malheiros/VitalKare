/**
 * @file button_handler.h
 * @brief Driver de botões com debounce por software e integração com event_dispatcher
 */

#ifndef VITALKARE_BUTTON_HANDLER_H
#define VITALKARE_BUTTON_HANDLER_H

#include "esp_err.h"

/** @brief Inicializa GPIOs e ISRs dos botões. */
esp_err_t button_handler_init(void);

/** @brief Task FreeRTOS — não chamar diretamente. */
void task_button_handler(void *arg);

#endif /* VITALKARE_BUTTON_HANDLER_H */
