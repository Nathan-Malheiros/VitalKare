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

#include "device_config.h"

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
#define APP_DEFAULT_DEVICE_ID       DEVICE_ID
#define APP_DEFAULT_WARD_ID         DEVICE_WARD_ID
#define APP_DEFAULT_PATIENT_ID      DEVICE_PATIENT_ID

/* =========================================================
 * TASKS FREERTOS — PRIORIDADES E STACKS
 * ========================================================= */
#define TASK_PRIO_WATCHDOG          6
#define TASK_PRIO_NETWORK           5
#define TASK_PRIO_TCP               5
#define TASK_PRIO_EVENTS            5
#define TASK_PRIO_BUTTONS           5   /**< Elevado: resposta a ISR crítica */
#define TASK_PRIO_AUDIO_CTRL        5   /**< Controle de gravação — tempo-real */
#define TASK_PRIO_AUDIO_ENC         4   /**< Encoding ADPCM */
#define TASK_PRIO_CMD_PROC          4   /**< Processa comandos recebidos */
#define TASK_PRIO_SENSORS           3
#define TASK_PRIO_CAMERA            3
#define TASK_PRIO_DISPLAY           2
#define TASK_PRIO_STORAGE           1

/* Legacy aliases */
#define TASK_PRIO_MQTT              5
#define TASK_PRIO_AUDIO             TASK_PRIO_AUDIO_CTRL

#define TASK_STACK_WATCHDOG         4096
#define TASK_STACK_NETWORK          8192
#define TASK_STACK_TCP              8192
#define TASK_STACK_EVENTS           4096
#define TASK_STACK_BUTTONS          3072
#define TASK_STACK_AUDIO_CTRL       4096
#define TASK_STACK_AUDIO_ENC        8192
#define TASK_STACK_CMD_PROC         6144
#define TASK_STACK_SENSORS          4096
#define TASK_STACK_CAMERA           32768
#define TASK_STACK_DISPLAY          4096
#define TASK_STACK_STORAGE          4096

/* Legacy aliases */
#define TASK_STACK_MQTT             8192
#define TASK_STACK_AUDIO            TASK_STACK_AUDIO_CTRL

/* =========================================================
 * INTERVALOS E TIMEOUTS
 * ========================================================= */
#define APP_HEARTBEAT_INTERVAL_MS       30000
#define APP_SENSOR_POLL_INTERVAL_MS     30000
#define APP_MPU6050_POLL_INTERVAL_MS    100
#define APP_DISPLAY_REFRESH_INTERVAL_MS 500
#define APP_STORAGE_FLUSH_INTERVAL_MS   60000
#define APP_WIFI_CONNECT_TIMEOUT_MS     15000
#define APP_MQTT_CONNECT_TIMEOUT_MS     10000
#define APP_RECONNECT_MIN_DELAY_MS      2000
#define APP_RECONNECT_MAX_DELAY_MS      120000

/* =========================================================
 * ÁUDIO — NOVA ARQUITETURA DE STREAMING
 * ========================================================= */
#define APP_AUDIO_MAX_DURATION_MS       10000   /**< Hard limit: 10 s */
#define APP_AUDIO_MAX_DURATION_S        10
#define APP_AUDIO_RECORD_MS             APP_AUDIO_MAX_DURATION_MS

/** Janela de debounce: ignora audio_ctrl_start() dentro deste intervalo */
#define APP_AUDIO_DEBOUNCE_MS           150

/** Tempo para recovery após erro de hardware (I2S reset) */
#define APP_AUDIO_ERROR_RECOVERY_MS     500

/** Chunk de 4096 bytes = 128 ms a 16 kHz/16-bit/mono */
#define APP_AUDIO_CHUNK_BYTES           4096
/** Pool de 16 chunks pré-alocados na PSRAM — zero malloc durante gravação */
#define APP_AUDIO_CHUNK_POOL_COUNT      16
/** Fila entre audio_ctrl e audio_encoder */
#define APP_AUDIO_CHUNK_QUEUE_DEPTH     12

/* Aliases de compatibilidade */
#define APP_AUDIO_BUFFER_SIZE_BYTES     (16000 * 10 * 2)
#ifndef AUDIO_BUFFER_BYTES
#define AUDIO_BUFFER_BYTES              APP_AUDIO_BUFFER_SIZE_BYTES
#endif

/* =========================================================
 * TCP CLIENT — PROTOCOLO PERSISTENTE
 * ========================================================= */
#define APP_TCP_SERVER_IP           DEVICE_SERVER_HOST
#define APP_TCP_SERVER_PORT         DEVICE_TCP_PORT
#define APP_TCP_CONNECT_TIMEOUT_MS  5000
#define APP_TCP_RECV_TIMEOUT_MS     100         /**< recv non-blocking tick */
#define APP_TCP_HEARTBEAT_INTERVAL_MS  15000    /**< Heartbeat a cada 15 s */
#define APP_TCP_HEARTBEAT_MISS_MAX  3           /**< Desconecta após 3 misses */
#define APP_TCP_RECONNECT_MIN_MS    2000
#define APP_TCP_RECONNECT_MAX_MS    120000
/* Each queue slot = sizeof(tcp_queued_frame_t) ≈ 2178 B (includes 2176 B audio
 * frame buffer). With audio disabled the max frame is ~512 B, but the struct
 * is fixed-size. TODO: move queue storage to PSRAM to restore depth=32.
 * Reduced from 32→8 to fit within ~120 KB available internal DRAM at boot
 * (32 KB is reserved for DMA + NVS + FreeRTOS internals consume ~40 KB). */
#define APP_TCP_CRITICAL_QUEUE_DEPTH  8
#define APP_TCP_NORMAL_QUEUE_DEPTH    8
#define APP_TCP_INCOMING_CMD_DEPTH    8
#define APP_TCP_MAX_PACKET_PAYLOAD  8192        /**< Payload máximo por pacote */

/* Protocolo Wire VK */
#define APP_PROTOCOL_MAGIC_0        0x56        /**< 'V' */
#define APP_PROTOCOL_MAGIC_1        0x4B        /**< 'K' */
#define APP_PROTOCOL_VERSION        1

/* =========================================================
 * HTTP REST (legado — usado durante migração)
 * ========================================================= */
#define APP_HTTP_BASE_URL           DEVICE_HTTP_BASE_URL
#define APP_HTTP_CONNECT_TIMEOUT_MS APP_WIFI_CONNECT_TIMEOUT_MS

/* =========================================================
 * TELEMETRIA DE SISTEMA
 * ========================================================= */
#define APP_SYSTEM_TELEMETRY_INTERVAL_MS 15000

/* =========================================================
 * CÂMERA
 * ========================================================= */
#define APP_CAMERA_FRAME_INTERVAL_MS    2000

/* =========================================================
 * OXÍMETRO — MAX30102
 * ========================================================= */
#define APP_OXIMETER_SAMPLE_RATE_HZ     100     /**< 100 amostras/s */
#define APP_OXIMETER_POLL_INTERVAL_MS   1000    /**< Atualiza BPM/SpO2 a 1 Hz */
#define APP_OXIMETER_FINGER_THRESHOLD   50000   /**< IR mín. para detecção de dedo */
#define APP_OXIMETER_REPORT_INTERVAL_MS 5000    /**< Envia telemetria a cada 5 s */

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
#define APP_WDT_TIMEOUT_MS              30000
#define APP_TASK_HEARTBEAT_PERIOD_MS    5000

#endif /* VITALKARE_APP_CONFIG_H */
