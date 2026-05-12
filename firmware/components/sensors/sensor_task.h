/**
 * @file sensor_task.h
 * @brief Task FreeRTOS de polling de sensores ambientais
 */

#ifndef VITALKARE_SENSOR_TASK_H
#define VITALKARE_SENSOR_TASK_H

#include "esp_err.h"

esp_err_t sensor_task_init(void);
void      task_sensor_poll(void *arg);

#endif /* VITALKARE_SENSOR_TASK_H */
