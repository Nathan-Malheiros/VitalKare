# VitalKare — Checkpoint (13/05/2026)

## ✅ Concluído nesta sessão

### Arquivos criados
| Arquivo | Descrição |
|---|---|
| `firmware/config/device_config.h` | WiFi SSID/PASS, MQTT HOST/PORT, DEVICE_ID, WARD_ID |
| `firmware/components/sensors/ldr.c` | ADC oneshot ESP-IDF 5.x, conversão para lux |
| `firmware/components/sensors/sensor_task.c` | Poll HDC1080 + LDR + MPU6050 a cada 30 s, publica MQTT |
| `firmware/components/watchdog/watchdog.c` | `esp_task_wdt` + heartbeat por task |
| `firmware/components/storage/sd_manager.c` | SDMMC 1-bit, FAT32, mkdir |
| `firmware/components/storage/log_writer.c` | JSON Lines em `/sdcard/logs/boot_NNN.jsonl` |
| `firmware/components/storage/offline_cache.c` | Ring buffer 200 eventos, persistido no SD |
| `firmware/components/display/oled_driver.c` | SSD1306 I2C, font 5×7 embarcada, primitivas gráficas |
| `firmware/components/display/ui_renderer.c` | 7 telas: BOOT / CONNECTING / OPERATIONAL / EMERGENCY / RECORDING / OFFLINE / ACK |
| `firmware/components/audio/audio_capture.c` | I2S PDM (nova API ESP-IDF 5.x), buffer PSRAM, task com `ulTaskNotifyTake` |
| `firmware/components/camera/camera_driver.c` | `esp32-camera`, OV5640, JPEG VGA, double-buffer PSRAM |
| `firmware/components/mqtt/vitalkare_mqtt.h` | Header renomeado — evita conflito de nome com `esp-mqtt` |
| `firmware/main/idf_component.yml` | Dependência gerenciada: `espressif/esp32-camera >= 2.0.0` |

### Arquivos corrigidos
| Arquivo | Bug corrigido |
|---|---|
| `firmware/components/buttons/button_handler.c` | `portYIELD_FROM_ISR_ARG` → `portYIELD_FROM_ISR` |
| `firmware/components/network/wifi_manager.c` | Fallback NVS → `device_config.h` quando NVS vazia |
| `firmware/config/mqtt_config.h` | `MQTT_BROKER_HOST_DEV` agora usa `DEVICE_MQTT_HOST` |
| `firmware/components/mqtt/mqtt_client.c` | Includes duplicados removidos; `publish_audio` e `publish_camera` adicionados |
| `firmware/main/CMakeLists.txt` | Todos os `.c` de componentes listados em SRCS; `".."` adicionado ao INCLUDE_DIRS; `esp_adc` e `ledc` adicionados ao REQUIRES |
| `firmware/main/main.c` | Include `mqtt_client.h` → `vitalkare_mqtt.h`; camera/áudio descomentados; `s_audio_task_handle` para notify |
| `firmware/config/hardware_config.h` | Aliases: `HW_I2S_PORT`, `HW_AUDIO_CLK_GPIO`, `HW_AUDIO_DATA_GPIO`, `HW_AUDIO_SAMPLE_RATE_HZ`, `HW_CAM_JPEG_QUALITY` |

---

## ❌ Ainda falta

### 1 · `firmware/config/app_config.h`
Adicionar os aliases ausentes (não interrompe o build, mas é referenciado pelos `.c` novos):
```c
#define APP_AUDIO_RECORD_MS             (APP_AUDIO_MAX_DURATION_S * 1000)
#define AUDIO_BUFFER_BYTES              (HW_MIC_SAMPLE_RATE_HZ * APP_AUDIO_MAX_DURATION_S * 2)
#define APP_CAMERA_FRAME_INTERVAL_MS    2000
#define APP_DISPLAY_REFRESH_MS          APP_DISPLAY_REFRESH_INTERVAL_MS
```

### 2 · `firmware/components/sensors/mpu6050.c` — CRIAR
- MPU6050 via I2C_NUM_0, addr `0x68`
- Registros: `PWR_MGMT_1` (0x6B), `ACCEL_XOUT_H` (0x3B), `GYRO_XOUT_H` (0x43)
- Tratar `ESP_ERR_INVALID_STATE` (barramento I2C já instalado pelo OLED)
- Funções: `mpu6050_init()`, `mpu6050_read(mpu6050_reading_t*)`, `mpu6050_is_motion_detected()`

### 3 · `firmware/tools/provision.py` — CRIAR
- Script Python para gravar credenciais na partição NVS via serial
- Porta: COM16 (ou argumento `--port`)
- Prompts interativos: SSID, senha, IP do broker MQTT, porta, device_id, ward_id
- Usa `esptool` + `nvs_partition_gen.py`

### 4 · `dashboard/src/services/websocketService.ts` — CRIAR
- Singleton: `WebSocketService.getInstance()`
- Métodos: `connect(token)`, `disconnect()`, `on(type, cb)`, `off(type, cb)`, `send(data)`
- Auto-reconnect com backoff exponencial
- URL base: `${VITE_WS_URL || 'ws://localhost:8000'}/ws/dashboard`

### 5 · `dashboard/vite.config.ts` — ATUALIZAR
- Proxy do backend deve usar variável de ambiente:
  ```ts
  target: `http://${process.env.VITE_BACKEND_HOST || 'localhost'}:8000`
  ```

### 6 · `dashboard/.env.local.example` — CRIAR
```
VITE_BACKEND_HOST=192.168.1.100
VITE_WS_URL=ws://192.168.1.100:8000
```

### 7 · `docker/.env.example` — CRIAR
```
POSTGRES_PASSWORD=vitalkare_pg
MINIO_ROOT_USER=vitalkare
MINIO_ROOT_PASSWORD=vitalkare_minio
REDIS_PASSWORD=vitalkare_redis
```

### 8 · Flash / Teste final
1. Preencher `firmware/config/device_config.h` com WiFi real + IP do notebook na LAN
2. Rodar: `cd firmware && idf.py -p COM16 -b 921600 flash monitor`

---

## Ordem recomendada ao retornar
1. `app_config.h` aliases  
2. `mpu6050.c`  
3. `provision.py`  
4. `websocketService.ts`  
5. `vite.config.ts` + `.env.local.example`  
6. `docker/.env.example`  
7. Preencher `device_config.h` → **flash**
