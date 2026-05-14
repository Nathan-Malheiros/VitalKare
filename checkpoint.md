# VitalKare — Checkpoint (PROJETO FINALIZADO)

## ✅ Concluído — Sessão 1 (backend base)
- Alembic migrations, schemas Pydantic v2, modelos SQLAlchemy async, /health, JWT refresh

## ✅ Concluído — Sessão 2 (firmware + dashboard)

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

## ✅ Concluído — Sessão 3 (finalização)

| Item | Arquivo | Status |
|---|---|---|
| Aliases faltantes | `firmware/config/app_config.h` | ✅ |
| Driver acelerômetro | `firmware/components/sensors/mpu6050.c` | ✅ (já existia) |
| Script provisionamento NVS | `firmware/tools/provision.py` | ✅ |
| Serviço WebSocket singleton | `dashboard/src/services/websocketService.ts` | ✅ (já existia) |
| Config Vite para LAN | `dashboard/vite.config.ts` | ✅ |
| Variáveis do dashboard | `dashboard/.env.local.example` | ✅ |
| Variáveis do Docker | `docker/.env.example` | ✅ |

---

## 🚀 Para colocar em funcionamento

### 1. Configure o firmware

Edite `firmware/config/device_config.h` com os valores reais:
```c
#define DEVICE_WIFI_SSID    "MinhaRede"
#define DEVICE_WIFI_PASS    "MinhaSenha"
#define DEVICE_MQTT_HOST    "192.168.1.100"   // IP do notebook na LAN
#define DEVICE_ID           "leito01"
#define DEVICE_WARD_ID      "ala-a"
```

### 2. Suba o backend com Docker

```bash
cd docker
cp .env.example .env
# Edite .env com senhas reais se necessário
docker compose up -d
```

### 3. Configure o dashboard

```bash
cd dashboard
cp .env.local.example .env.local
# Edite VITE_BACKEND_HOST com o IP real do notebook
npm install
npm run dev
```

### 4. Flash do firmware

```bash
cd firmware
# Opção A — provisionamento via script (recomendado)
pip install esptool
python tools/provision.py --port COM16

# Opção B — flash direto (usa valores hard-coded em device_config.h)
idf.py -p COM16 -b 921600 flash monitor
```

### 5. Provisionar múltiplos dispositivos

```bash
python firmware/tools/provision.py --port COM16
# O script solicitará SSID, senha, IP do broker, device_id e ward_id interativamente
# Para apenas gerar o binário sem flash:
python firmware/tools/provision.py --port COM16 --dry-run
```

---

## Notas importantes

- **`firmware/config/device_config.h`** contém credenciais — não commitar com valores reais
- **`docker/.env`** e **`dashboard/.env.local`** — adicionar ao `.gitignore`
- O broker Mosquitto escuta em `0.0.0.0:1883` — o ESP32 conecta via IP da LAN
- A partição NVS tem offset padrão `0x9000` (24 KB) — confirme em `firmware/partitions.csv`

