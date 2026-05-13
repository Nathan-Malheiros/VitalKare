/**
 * @file device_config.h
 * @brief Configuração local do dispositivo VitalKare para desenvolvimento
 *
 * ATENÇÃO: Preencha WIFI_SSID, WIFI_PASS e MQTT_HOST com os valores
 * da sua rede local. Em produção as credenciais vêm da NVS.
 *
 * Para gravar na NVS sem alterar este arquivo, use:
 *   python firmware/tools/provision.py --port COM16
 */

#ifndef VITALKARE_DEVICE_CONFIG_H
#define VITALKARE_DEVICE_CONFIG_H

/* =========================================================
 * REDE Wi-Fi LOCAL
 * ========================================================= */
/** SSID da rede Wi-Fi onde o dashboard e as placas operam */
#define DEVICE_WIFI_SSID        "SUA_REDE_WIFI"
/** Senha da rede Wi-Fi */
#define DEVICE_WIFI_PASS        "SUA_SENHA_WIFI"

/* =========================================================
 * BROKER MQTT (IP do notebook/servidor na LAN)
 *
 * Execute no notebook:
 *   - Windows: ipconfig  (procure "Endereço IPv4" da sua interface Wi-Fi)
 *   - Linux:   ip -4 addr show
 * ========================================================= */
#define DEVICE_MQTT_HOST        "192.168.1.100"
#define DEVICE_MQTT_PORT        1883

/* =========================================================
 * IDENTIDADE DO DISPOSITIVO (ajuste por placa)
 * ========================================================= */
#define DEVICE_ID               "leito01"
#define DEVICE_WARD_ID          "ala-a"
#define DEVICE_PATIENT_ID       "PAC-000"

/* =========================================================
 * USUÁRIO MQTT (definido no mosquitto/passwd)
 * ========================================================= */
#define DEVICE_MQTT_USER        "vitalkare"
#define DEVICE_MQTT_PASS        "vitalkare_mqtt"

#endif /* VITALKARE_DEVICE_CONFIG_H */
