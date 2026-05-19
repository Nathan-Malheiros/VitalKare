/**
 * @file command_processor.h
 * @brief Processador de comandos recebidos do backend via TCP
 *
 * O tcp_client posta frames recebidos na fila interna deste módulo.
 * A task roteia cada pacote para o handler correto:
 *   PKT_CMD_DISPLAY_TEXT  → display_task (via evento)
 *   PKT_CMD_BUZZER        → GPIO / PWM buzzer
 *   PKT_CMD_REBOOT        → esp_restart()
 *   PKT_OTA_START         → OTA update (esp_ota_*)
 *   PKT_EVENT_ACK         → EVT_ACK_RECEIVED para o event dispatcher
 */

#ifndef VITALKARE_COMMAND_PROCESSOR_H
#define VITALKARE_COMMAND_PROCESSOR_H

#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Inicializa a fila interna do command processor. */
esp_err_t cmd_processor_init(void);

/** @brief Retorna a fila de entrada (usada pelo tcp_client). */
QueueHandle_t cmd_processor_get_queue(void);

/** @brief Task FreeRTOS do command processor — não chamar diretamente. */
void task_command_processor(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* VITALKARE_COMMAND_PROCESSOR_H */
