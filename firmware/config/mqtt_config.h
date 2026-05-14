/**
 * @file mqtt_config.h
 * @brief Configurações MQTT do VitalKare — broker, tópicos e QoS
 *
 * Credenciais de broker (host, user, password) NÃO devem ser
 * definidas aqui em produção. Use NVS provisionada em fábrica
 * ou certificado de cliente mTLS.
 *
 * Os tópicos seguem a convenção:
 *   hospital/{ward}/{device_id}/{subtópico}
 */

#ifndef VITALKARE_MQTT_CONFIG_H
#define VITALKARE_MQTT_CONFIG_H

#include "device_config.h"

/* =========================================================
 * BROKER
 * ========================================================= */
/** @note Substituir em produção por hostname real ou IP do broker. */
#define MQTT_BROKER_HOST_DEV        DEVICE_MQTT_HOST
#define MQTT_BROKER_PORT_PLAIN      1883    /**< Desenvolvimento — sem TLS */
#define MQTT_BROKER_PORT_TLS        8883    /**< Produção — com TLS 1.3 */
#define MQTT_KEEPALIVE_S            60
#define MQTT_CLIENT_ID_PREFIX       "vitalkare-"
#define MQTT_CLEAN_SESSION          false   /**< Sessão persistente */

/* =========================================================
 * QUALIDADE DE SERVIÇO
 * ========================================================= */
#define MQTT_QOS_EMERGENCY          2   /**< Exactly-once — eventos críticos */
#define MQTT_QOS_STATUS             1   /**< At-least-once — heartbeat */
#define MQTT_QOS_SENSOR             1   /**< At-least-once — dados ambientais */
#define MQTT_QOS_AUDIO              1   /**< At-least-once — payload de áudio */
#define MQTT_QOS_CMD                1   /**< At-least-once — comandos */
#define MQTT_QOS_LOG                0   /**< Best-effort — logs */

/* =========================================================
 * PADRÃO DE TÓPICOS
 * Uso: snprintf(buf, sizeof(buf), MQTT_TOPIC_STATUS, ward, device_id)
 * ========================================================= */
#define MQTT_TOPIC_BASE             "hospital/%s/%s"

/** Publicados pelo dispositivo → broker */
#define MQTT_TOPIC_STATUS           "hospital/%s/%s/status"
#define MQTT_TOPIC_EMERGENCY        "hospital/%s/%s/emergency"
#define MQTT_TOPIC_REQUEST          "hospital/%s/%s/request"
#define MQTT_TOPIC_AUDIO            "hospital/%s/%s/audio"
#define MQTT_TOPIC_ENVIRONMENT      "hospital/%s/%s/environment"
#define MQTT_TOPIC_IMAGE            "hospital/%s/%s/image"
#define MQTT_TOPIC_LOG              "hospital/%s/%s/log"

/** Recebidos pelo dispositivo ← broker (comandos do backend) */
#define MQTT_TOPIC_CMD_CAMERA       "hospital/%s/%s/cmd/camera"
#define MQTT_TOPIC_CMD_ACK          "hospital/%s/%s/cmd/ack"
#define MQTT_TOPIC_CMD_DISPLAY      "hospital/%s/%s/cmd/display"
#define MQTT_TOPIC_CMD_BUZZER       "hospital/%s/%s/cmd/buzzer"
#define MQTT_TOPIC_CMD_BROADCAST    "hospital/broadcast/cmd"

/* =========================================================
 * LAST WILL TESTAMENT
 * ========================================================= */
#define MQTT_LWT_TOPIC              MQTT_TOPIC_STATUS
#define MQTT_LWT_QOS                1
#define MQTT_LWT_RETAIN             true

/* =========================================================
 * TAMANHOS DE BUFFER
 * ========================================================= */
#define MQTT_MAX_TOPIC_LEN          128
#define MQTT_MAX_PAYLOAD_LEN        (96 * 1024)  /**< 96 KB — áudio ADPCM 10 s */
#define MQTT_STATUS_PAYLOAD_LEN     512
#define MQTT_EVENT_PAYLOAD_LEN      512
#define MQTT_ENV_PAYLOAD_LEN        384   /* aumentado para incluir posture_label */

#endif /* VITALKARE_MQTT_CONFIG_H */
