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
 * IDENTIDADE DO DISPOSITIVO (fallback se NVS não provisionada)
 *
 * Estes valores são usados APENAS quando a NVS não contém um valor
 * válido (primeiro boot sem provision.py).  Em operação normal a NVS
 * é a fonte de verdade — use provision.py para gravar os valores reais.
 *
 * IMPORTANTE: device_id e ward_id devem corresponder aos valores
 * informados ao provision.py (padrão: "leito01" / "ala-a").
 * ========================================================= */
#define DEVICE_ID               "leito01"
#define DEVICE_WARD_ID          "ala-a"
#define DEVICE_PATIENT_ID       "PAC-000"

/* DEVICE_CONFIG_FORCE_APPLY = 1 reescreve a NVS com os valores acima
 * em CADA boot.  Isso desfaz qualquer gravação feita pelo provision.py
 * e força o servidor para DEVICE_SERVER_HOST, mesmo que o IP real seja
 * diferente.
 *
 * Use 1 apenas para recuperar uma NVS corrompida ou fazer o primeiro
 * boot sem provision.py.  Volte para 0 imediatamente após, ou o
 * provision.py não terá efeito e a placa sempre usará os valores acima.
 *
 * FLUXO RECOMENDADO:
 *   1. Mantenha DEVICE_CONFIG_FORCE_APPLY 0 (padrão).
 *   2. Use provision.py para gravar SSID, senha, IP do servidor e
 *      device_id na NVS da placa.
 *   3. A placa lê a NVS em cada boot e se conecta ao servidor correto.
 */
#define DEVICE_CONFIG_FORCE_APPLY  0

/* =========================================================
 * USUÁRIO MQTT (definido no mosquitto/passwd)
 * ========================================================= */
#define DEVICE_MQTT_USER        "vitalkare"
#define DEVICE_MQTT_PASS        "vitalkare_mqtt"

#endif /* VITALKARE_DEVICE_CONFIG_H */
