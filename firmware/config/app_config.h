/**
 * @file app_config.h
 * @brief Configurações gerais da aplicação VitalKare (firmware)
 *
 * Identidade do dispositivo, intervalos de polling, timeouts e
 * parâmetros funcionais do sistema. Valores que variam por instalação
 * devem ser armazenados em NVS e lidos em runtime.
 */

#ifndef VITALKARE_APP_CONFIG_H
#define VITALKARE_APP_CONFIG_H

/* =========================================================
 * IDENTIDADE DO SISTEMA
 * ========================================================= */
#define APP_FIRMWARE_VERSION        "1.0.0"
#define APP_SYSTEM_NAME             "VitalKare"

/**
 * @note device_id e ward_id devem ser lidos da partição NVS em
 * produção. Os valores abaixo são usados apenas durante desenvolvimento
 * quando NVS não está provisionada.
 */
#define APP_DEFAULT_DEVICE_ID       "leito01"
#define APP_DEFAULT_WARD_ID         "ala-a"
#define APP_DEFAULT_PATIENT_ID      "PAC-000"

/* =========================================================
 * TASKS FREERTOS — PRIORIDADES E STACKS
 * ========================================================= */
#define TASK_PRIO_WATCHDOG          6
#define TASK_PRIO_NETWORK           5   /**< Wi-Fi manager task priority */
#define TASK_PRIO_MQTT              5   /**< Kept for backward compat */
#define TASK_PRIO_EVENTS            5
#define TASK_PRIO_BUTTONS           4
#define TASK_PRIO_AUDIO             4
#define TASK_PRIO_SENSORS           3
#define TASK_PRIO_CAMERA            3
#define TASK_PRIO_DISPLAY           2
#define TASK_PRIO_STORAGE           1

#define TASK_STACK_WATCHDOG         4096
#define TASK_STACK_NETWORK          8192   /**< Wi-Fi manager task stack */
#define TASK_STACK_MQTT             8192   /**< Kept for backward compat */
#define TASK_STACK_EVENTS           4096
#define TASK_STACK_BUTTONS          2048
#define TASK_STACK_AUDIO            16384
#define TASK_STACK_SENSORS          4096
#define TASK_STACK_CAMERA           32768
#define TASK_STACK_DISPLAY          4096
#define TASK_STACK_STORAGE          4096

/* =========================================================
 * INTERVALOS E TIMEOUTS
 * ========================================================= */
#define APP_HEARTBEAT_INTERVAL_MS       30000   /**< Publicar status a cada 30 s */
#define APP_SENSOR_POLL_INTERVAL_MS     30000   /**< Ler sensores a cada 30 s */
#define APP_MPU6050_POLL_INTERVAL_MS    100     /**< MPU6050 a 10 Hz */
#define APP_DISPLAY_REFRESH_INTERVAL_MS 500     /**< OLED 2 Hz */
#define APP_STORAGE_FLUSH_INTERVAL_MS   60000   /**< Flush logs no SD a cada 60 s */
#define APP_WIFI_CONNECT_TIMEOUT_MS     15000   /**< Timeout de conexão Wi-Fi */
#define APP_MQTT_CONNECT_TIMEOUT_MS     10000   /**< Timeout de conexão MQTT */
#define APP_RECONNECT_MIN_DELAY_MS      2000    /**< Delay mínimo de reconexão */
#define APP_RECONNECT_MAX_DELAY_MS      120000  /**< Delay máximo de reconexão (2 min) */

/* =========================================================
 * ÁUDIO
 * ========================================================= */
#define APP_AUDIO_MAX_DURATION_S        10      /**< Duração máxima de gravação */
#define APP_AUDIO_RECORD_MS             (APP_AUDIO_MAX_DURATION_S * 1000)
#define APP_AUDIO_BUFFER_SIZE_BYTES     (HW_MIC_SAMPLE_RATE_HZ * APP_AUDIO_MAX_DURATION_S * 2)
#ifndef AUDIO_BUFFER_BYTES
#define AUDIO_BUFFER_BYTES              APP_AUDIO_BUFFER_SIZE_BYTES
#endif

/* =========================================================
 * CÂMERA
 * ========================================================= */
#define APP_CAMERA_FRAME_INTERVAL_MS    2000    /**< Captura frame a cada 2 s no modo contínuo */

/* =========================================================
 * REDE — HTTP REST
 * ========================================================= */
/**
 * @note Em produção substituir pelo IP/hostname do servidor backend.
 * Pode ser sobrescrito por NVS.
 */
#define APP_HTTP_BASE_URL           "http://192.168.1.100:8000"
#define APP_HTTP_CONNECT_TIMEOUT_MS APP_WIFI_CONNECT_TIMEOUT_MS

/* =========================================================
 * ARMAZENAMENTO LOCAL
 * ========================================================= */
#define APP_SD_LOG_DIR              "/sdcard/logs"
#define APP_SD_AUDIO_DIR            "/sdcard/audio"
#define APP_SD_IMAGE_DIR            "/sdcard/images"
#define APP_SD_CACHE_DIR            "/sdcard/cache"
#define APP_SD_LOG_MAX_SIZE_MB      50
#define APP_OFFLINE_CACHE_MAX_EVENTS 200

/* =========================================================
 * DISPLAY
 * ========================================================= */
#define APP_OFFLINE_BLINK_INTERVAL_MS   1000
#define APP_EMERGENCY_BLINK_INTERVAL_MS 300
#define APP_DISPLAY_REFRESH_MS          APP_DISPLAY_REFRESH_INTERVAL_MS

/* =========================================================
 * WATCHDOG
 * ========================================================= */
#define APP_WDT_TIMEOUT_MS          30000   /**< 30 s sem feed → reset */
#define APP_TASK_HEARTBEAT_PERIOD_MS 5000   /**< Tasks devem registrar alive a cada 5 s */

#endif /* VITALKARE_APP_CONFIG_H */
