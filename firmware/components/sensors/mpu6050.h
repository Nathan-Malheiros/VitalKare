/**
 * @file mpu6050.h
 * @brief Driver MPU6050 — acelerômetro + giroscópio 6 eixos (I2C)
 * @note Fase 2 — detecção de movimento e queda
 */

#ifndef VITALKARE_MPU6050_H
#define VITALKARE_MPU6050_H

#include "esp_err.h"
#include <stdbool.h>

typedef struct {
    float accel_x_g;
    float accel_y_g;
    float accel_z_g;
    float gyro_x_dps;
    float gyro_y_dps;
    float gyro_z_dps;
    float temperature_celsius;
    bool  motion_detected;
    bool  valid;
} mpu6050_reading_t;

esp_err_t mpu6050_init(void);
esp_err_t mpu6050_read(mpu6050_reading_t *out);
bool      mpu6050_is_motion_detected(void);

#endif /* VITALKARE_MPU6050_H */
