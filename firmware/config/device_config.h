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
 * NVS — namespace e chaves persistidas por provision.py
 * ========================================================= */
#define DEVICE_NVS_NAMESPACE       "vitalkare"
#define DEVICE_NVS_KEY_WIFI_SSID   "ssid"
#define DEVICE_NVS_KEY_WIFI_PASS   "pass"
#define DEVICE_NVS_KEY_WIFI_STATIC_EN "wifi_static_en"
#define DEVICE_NVS_KEY_WIFI_STATIC_IP "wifi_static_ip"
#define DEVICE_NVS_KEY_WIFI_STATIC_GW "wifi_static_gw"
#define DEVICE_NVS_KEY_WIFI_STATIC_NM "wifi_static_nm"
#define DEVICE_NVS_KEY_SERVER_HOST "server_host"
#define DEVICE_NVS_KEY_TCP_PORT    "tcp_port"
#define DEVICE_NVS_KEY_DEVICE_ID   "device_id"
#define DEVICE_NVS_KEY_WARD_ID     "ward_id"
#define DEVICE_NVS_KEY_PATIENT_ID  "patient_id"
#define DEVICE_NVS_KEY_MQTT_HOST   "mqtt_host"   /* legado */

/* =========================================================
 * REDE Wi-Fi LOCAL
 * ========================================================= */
/** SSID da rede Wi-Fi onde o dashboard e as placas operam */
#define DEVICE_WIFI_SSID        "ITSelf"
/** Senha da rede Wi-Fi */
#define DEVICE_WIFI_PASS        "code2020"

/* =========================================================
 * SERVIDOR VITALKARE (notebook/servidor na LAN)
 *
 * Execute no notebook:
 *   - Windows: ipconfig  (procure "Endereço IPv4" da sua interface Wi-Fi)
 *   - Linux:   ip -4 addr show
 * ========================================================= */
#define DEVICE_SERVER_HOST      "192.168.1.223"
#define DEVICE_TCP_PORT         7890
#define DEVICE_HTTP_BASE_URL    "http://192.168.1.223:8000"

/* Compatibilidade com nomes legados ainda usados na documentação antiga */
#define DEVICE_MQTT_HOST        DEVICE_SERVER_HOST
#define DEVICE_MQTT_PORT        1883

/* =========================================================
 * IDENTIDADE DO DISPOSITIVO (ajuste por placa)
 * ========================================================= */
#define DEVICE_ID               "LeitoKira3"
#define DEVICE_WARD_ID          "ALAKIRA"
#define DEVICE_PATIENT_ID       "PAC-000"

/* Set to 1 to overwrite NVS with the values above on every boot.
 * Use when NVS has stale values from a previous provision run.
 * Set back to 0 once the device is provisioned correctly. */
#define DEVICE_CONFIG_FORCE_APPLY  1

/* =========================================================
 * USUÁRIO MQTT (definido no mosquitto/passwd)
 * ========================================================= */
#define DEVICE_MQTT_USER        "vitalkare"
#define DEVICE_MQTT_PASS        "vitalkare_mqtt"

#endif /* VITALKARE_DEVICE_CONFIG_H */
