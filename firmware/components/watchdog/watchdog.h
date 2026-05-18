/**
 * @file watchdog.h
 * @brief Watchdog de hardware e monitoramento de tasks
 *
 * Alimenta o IWDT (Independent Watchdog Timer) periodicamente.
 * Cada task registra um "heartbeat ticket" e o watchdog verifica
 * que todas as tasks críticas estão vivas antes de fazer o feed.
 */

#ifndef VITALKARE_WATCHDOG_H
#define VITALKARE_WATCHDOG_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    WDT_TASK_NETWORK = 0,   /**< Wi-Fi / HTTP network manager */
    WDT_TASK_EVENTS,
    WDT_TASK_BUTTONS,
    WDT_TASK_SENSORS,
    WDT_TASK_DISPLAY,
    WDT_TASK_COUNT,
} wdt_task_id_t;

esp_err_t watchdog_init(void);

/**
 * @brief Registra que a task está viva (deve ser chamado periodicamente).
 * @param task_id  Identificador da task.
 */
void watchdog_feed_task(wdt_task_id_t task_id);

/** @brief Task do watchdog — prioridade máxima. */
void task_watchdog(void *arg);

#endif /* VITALKARE_WATCHDOG_H */
