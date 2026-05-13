/**
 * @file vitalkare_mqtt.h
 * @brief Interface do cliente MQTT VitalKare
 *
 * Renomeado de mqtt_client.h para evitar conflito com o header esp-mqtt
 * do mesmo nome (esp-idf/components/mqtt/esp-mqtt/include/mqtt_client.h).
 *
 * Inclua este arquivo nos módulos que precisam publicar via MQTT.
 */

#ifndef VITALKARE_VITALKARE_MQTT_H
#define VITALKARE_VITALKARE_MQTT_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Tipos de recording/frame declarados aqui para desacoplar headers */
typedef struct {
    void    *data;
    size_t   size_bytes;
    uint32_t sample_rate;
    uint8_t  bit_width;
    uint8_t  channels;
} audio_recording_t;

typedef struct {
    void    *fb;          /**< Opaque pointer to camera_fb_t */
    uint8_t *data;
    size_t   size_bytes;
    uint32_t width;
    uint32_t height;
    bool     is_jpeg;
} camera_frame_t;

/** @brief Callback para mensagens recebidas (tópicos cmd/*). */
typedef void (*mqtt_msg_cb_t)(const char *topic, const uint8_t *data, int data_len);

/** @brief Inicializa e conecta o cliente MQTT. */
esp_err_t vitalkare_mqtt_init(void);

/** @brief Registra callback de mensagens recebidas. */
void vitalkare_mqtt_set_message_callback(mqtt_msg_cb_t cb);

/** @brief Retorna true se MQTT está conectado. */
bool vitalkare_mqtt_is_connected(void);

/**
 * @brief Publica mensagem em um tópico.
 * @return msg_id >= 0 em sucesso; -1 em erro.
 */
int vitalkare_mqtt_publish(const char *topic, const void *data,
                           int len, int qos, bool retain);

/** @brief Publica payload JSON de status/heartbeat. */
esp_err_t vitalkare_mqtt_publish_status(void);

/** @brief Publica evento de emergência com QoS 2. */
esp_err_t vitalkare_mqtt_publish_emergency(const char *trigger);

/** @brief Publica leituras ambientais. */
esp_err_t vitalkare_mqtt_publish_environment(float temp_c, float humidity_pct,
                                              float lux, float noise_db,
                                              bool motion);

/** @brief Publica frame de áudio (base64 encoded). */
esp_err_t vitalkare_mqtt_publish_audio(const audio_recording_t *rec);

/** @brief Publica frame JPEG da câmera (binário). */
esp_err_t vitalkare_mqtt_publish_camera(const camera_frame_t *frame);

/** @brief Task FreeRTOS do MQTT manager — não chamar diretamente. */
void task_mqtt_manager(void *arg);

#endif /* VITALKARE_VITALKARE_MQTT_H */
