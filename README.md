# VitalKare — Plataforma Hospitalar Inteligente

> Sistema IoT completo para monitoramento de leitos hospitalares em tempo real: sensores ambientais, captura de áudio/vídeo, alertas de emergência e dashboard web via WebSocket.

**Hardware:** KIIRA V2 (ESP32-S3) · **Firmware:** ESP-IDF 5.x + FreeRTOS · **Backend:** FastAPI · **Dashboard:** React 18

---

## Visão Geral da Arquitetura

```
┌─────────────────────────────────────────────────────────┐
│                    KIIRA V2 (ESP32-S3)                   │
│   Firmware ESP-IDF + FreeRTOS + Event-Driven Architecture│
└────────────────────────┬────────────────────────────────┘
                         │ MQTT (TLS / QoS 0-2)
                         ▼
              ┌─────────────────┐
              │  Mosquitto MQTT │
              └────────┬────────┘
                       │
              ┌────────▼────────┐
              │  FastAPI Backend│  ← WebSocket → Dashboard React
              │  (Python 3.11+) │
              └────────┬────────┘
                       │
        ┌──────────────┼──────────────┐
        ▼              ▼              ▼
  PostgreSQL 16    Redis 7        MinIO
  + TimescaleDB   (filas/cache)  (áudio/imagens)
```

---

## Stack Tecnológica

| Camada       | Tecnologia                               |
|--------------|------------------------------------------|
| Firmware     | ESP-IDF 5.x · FreeRTOS · C17             |
| Backend      | FastAPI · Python 3.11 · SQLAlchemy 2.0   |
| Dashboard    | React 18 · TypeScript · Tailwind · Zustand |
| Mensageria   | MQTT (Mosquitto/EMQX) · aiomqtt          |
| Banco        | PostgreSQL 16 + TimescaleDB              |
| Cache        | Redis 7                                  |
| Storage      | MinIO (S3-compatível)                    |
| Transcrição  | Faster-Whisper (small/pt)                |
| Infra        | Docker Compose                           |

---

## Estrutura do Repositório

```
VitalKare/
├── firmware/               # ESP-IDF — ESP32-S3 (KIIRA V2)
│   ├── config/             # hardware_config.h, app_config.h, mqtt_config.h
│   ├── components/         # Módulos FreeRTOS independentes
│   │   ├── core/           # Máquina de estados global
│   │   ├── events/         # Event dispatcher central
│   │   ├── network/        # Wi-Fi manager
│   │   ├── mqtt/           # Cliente MQTT
│   │   ├── buttons/        # Handler GPIO com debounce
│   │   ├── sensors/        # HDC1080, MPU6050, LDR
│   │   ├── display/        # OLED SSD1306 + UI renderer
│   │   ├── audio/          # Captura PDM (Fase 4)
│   │   ├── camera/         # OV5640 (Fase 5)
│   │   ├── storage/        # SD card, log, cache offline
│   │   └── watchdog/       # IWDT + monitor de tarefas
│   └── main/               # app_main — orquestrador
│
├── backend/                # FastAPI — Servidor central
│   └── app/
│       ├── api/v1/         # Rotas REST + WebSocket
│       ├── core/           # MQTT handler, event processor, heartbeat
│       ├── models/         # SQLAlchemy ORM
│       ├── utils/          # JWT, bcrypt
│       └── db/             # Sessão async
│
├── dashboard/              # React 18 + TypeScript + Tailwind
│   └── src/
│       ├── components/     # BedCard, BedGrid, EmergencyBanner
│       ├── hooks/          # useWebSocket
│       ├── pages/          # Dashboard, Login
│       ├── services/       # Axios API client
│       └── store/          # Zustand — beds, alerts, auth
│
├── docker/                 # Docker Compose + configs
│   ├── docker-compose.yml
│   └── mosquitto/
│
└── ProjetoFinalEmabarcaTech/  # Projeto original (referência — não modificar)
```

---

## Quick Start (Desenvolvimento)

### Pré-requisitos
- Docker Desktop (ou Docker Engine + Compose v2)
- Python 3.11+ (backend local)
- Node.js 20+ (dashboard local)
- ESP-IDF 5.x (firmware)

### 1. Subir serviços de infraestrutura

```bash
cd docker
cp .env.example .env
# Edite .env com suas senhas (POSTGRES_PASSWORD, MINIO_ROOT_PASSWORD, etc.)

docker compose up -d
```

### 2. Backend

```bash
cd backend
pip install -r requirements.txt
uvicorn app.main:app --reload --port 8000
```

API disponível em: http://localhost:8000/api/docs

### 3. Dashboard

```bash
cd dashboard
cp .env.local.example .env.local
# Edite VITE_BACKEND_HOST com o IP do servidor na LAN (ex: 192.168.1.100)
npm install
npm run dev
```

Dashboard em: http://localhost:5173

### 4. Firmware — Provisionamento via script

```bash
# Instale o esptool e configure IDF_PATH antes
pip install esptool
python firmware/tools/provision.py --port COM16
# O script solicita SSID, senha, IP do broker, device_id e ward_id
```

### 5. Firmware — Flash direto (credenciais em device_config.h)

```bash
# Edite firmware/config/device_config.h com WiFi e IP do broker
cd firmware
idf.py set-target esp32s3
idf.py -p COM16 -b 921600 flash monitor
```

---

## Tópicos MQTT

| Subtópico     | QoS | Descrição                  |
|---------------|-----|----------------------------|
| `status`      | 1   | Heartbeat (30s)            |
| `emergency`   | 2   | Chamada de emergência      |
| `request`     | 1   | Solicitação do paciente    |
| `environment` | 1   | Temperatura, umidade, etc. |
| `audio`       | 1   | Metadados de áudio (Fase 4)|
| `image`       | 1   | Frame JPEG (Fase 5)        |
| `log`         | 0   | Log de dispositivo         |

Padrão: `hospital/{ala}/{device_id}/{subtopico}`

---

## Estado do Projeto

| Camada | Componente | Status |
|--------|-----------|--------|
| Firmware | Wi-Fi Manager + NVS provisioning | ✅ Completo |
| Firmware | MQTT (QoS 0/1/2, offline cache) | ✅ Completo |
| Firmware | Sensores: HDC1080, MPU6050, LDR | ✅ Completo |
| Firmware | Display OLED SSD1306 + UI Renderer | ✅ Completo |
| Firmware | Captura de áudio PDM (I2S, PSRAM) | ✅ Completo |
| Firmware | Câmera OV5640 JPEG (esp32-camera) | ✅ Completo |
| Firmware | SD Card + Log + Cache Offline | ✅ Completo |
| Firmware | Watchdog + Botões GPIO | ✅ Completo |
| Backend | FastAPI + JWT + WebSocket | ✅ Completo |
| Backend | MQTT → eventos → PostgreSQL | ✅ Completo |
| Backend | Alembic migrations + Pydantic v2 | ✅ Completo |
| Dashboard | React 18 + Zustand + Tailwind | ✅ Completo |
| Dashboard | Leitos em tempo real (WebSocket) | ✅ Completo |
| Infra | Docker Compose (Postgres, Redis, MinIO, Mosquitto) | ✅ Completo |

---

## Simulação do Dashboard

Abra `dashboard-simulation.html` diretamente no navegador para ver o dashboard funcionando sem nenhum servidor.

- 20 leitos com status dinâmico, sparklines de temperatura e indicador de sinal
- Cenários automáticos: heartbeat, emergência, admissão, alta, gravação de áudio, offline/reconexão
- Filtros por status, log de eventos ao vivo e toasts de notificação
- Tema escuro com layout responsivo

## Configuração de Rede Local (LAN)

```
Notebook (Docker)          ESP32-S3 KIIRA V2
──────────────────         ─────────────────
Mosquitto :1883   ←──WiFi──  MQTT Client
FastAPI   :8000   ←──WiFi──  HTTP/WS (futuro)
Dashboard :5173              (browser no mesmo PC)
```

1. Descubra o IP do notebook na rede: `ipconfig` (Windows)
2. Edite `docker/.env` → `HOST_LAN_IP=<seu_ip>`
3. Edite `dashboard/.env.local` → `VITE_BACKEND_HOST=<seu_ip>`
4. Execute `python firmware/tools/provision.py` para gravar o IP no dispositivo via NVS

## Contribuindo

Veja [CONTRIBUTING.md](CONTRIBUTING.md) para convenções de commits e processo de PR.

---

> Projeto acadêmico — EmbarcaTech Residência em Sistemas Embarcados  
> Hardware: KIIRA V2 (ESP32-S3) · Plataforma: VitalKare
