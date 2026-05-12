/**
 * @file mqtt_client.h
 * @brief Interface do cliente MQTT do VitalKare
 *
 * Encapsula o esp-mqtt com suporte a:
 * - Reconexão automática com backoff
 * - Last Will Testament configurado no connect
 * - Publicação com QoS configurável
 * - Assinatura de tópicos de comando do backend
 */

#ifndef VITALKARE_MQTT_CLIENT_H
#define VITALKARE_MQTT_CLIENT_H

#include "esp_err.h"
#include "mqtt_client.h"  /* esp-mqtt */
#include <stdbool.h>
#include <stdint.h>

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
 * @param topic  Tópico completo (não-formatado).
 * @param data   Payload (pode ser NULL para publicação vazia).
 * @param len    Tamanho do payload.
 * @param qos    0, 1 ou 2.
 * @param retain Mensagem retida no broker.
 * @return msg_id >= 0 em sucesso; -1 em erro.
 */
int vitalkare_mqtt_publish(const char *topic, const void *data,
                           int len, int qos, bool retain);

/**
 * @brief Publica payload JSON de status/heartbeat.
 * Constrói automaticamente o JSON com informações do dispositivo.
 */
esp_err_t vitalkare_mqtt_publish_status(void);

/**
 * @brief Publica evento de emergência com QoS 2.
 * @param trigger  Identificador da origem (ex: "BUTTON_B").
 */
esp_err_t vitalkare_mqtt_publish_emergency(const char *trigger);

/**
 * @brief Publica leituras ambientais.
 */
esp_err_t vitalkare_mqtt_publish_environment(float temp_c, float humidity_pct,
                                              float lux, float noise_db,
                                              bool motion);

/** @brief Task FreeRTOS do MQTT manager — não chamar diretamente. */
void task_mqtt_manager(void *arg);

#endif /* VITALKARE_MQTT_CLIENT_H */
