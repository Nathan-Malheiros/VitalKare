/**
 * @file sim_sensors.h
 * @brief Simulated sensor task for ESP32-S3 DevKit development mode.
 *
 * Active only when VK_SIMULATION_MODE is set (DevKit board profile).
 * Replaces real sensor hardware with time-varying synthetic data so the
 * full system stack (TCP, dashboard, telemetry) can be validated without
 * KiiraV2 hardware.
 */

#ifndef VITALKARE_SIM_SENSORS_H
#define VITALKARE_SIM_SENSORS_H

#include "esp_err.h"

/**
 * Initialize simulation sensor bookkeeping.
 * Must be called before task_sim_telemetry is created.
 */
esp_err_t sim_sensors_init(void);

/**
 * FreeRTOS task — generates synthetic ENV + OXIMETER telemetry.
 * Replaces task_sensor_poll and task_oximeter_poll on DevKit.
 * Stack: 4096 bytes. Priority: TASK_PRIO_SENSORS.
 */
void task_sim_telemetry(void *arg);

#endif /* VITALKARE_SIM_SENSORS_H */
