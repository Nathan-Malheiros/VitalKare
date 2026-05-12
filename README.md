# VitalKare — Plataforma Hospitalar Inteligente

> Sistema IoT para monitoramento de leitos hospitalares com alertas em tempo real, captura de áudio e análise ambiental.

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
cp ../backend/.env.example ../backend/.env
# Edite ../backend/.env com suas senhas

docker compose up -d postgres redis mosquitto minio
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
npm install
npm run dev
```

Dashboard em: http://localhost:5173

### 4. Firmware

```bash
cd firmware
idf.py set-target esp32s3
idf.py menuconfig   # Configure Wi-Fi e MQTT em: Component config → VitalKare
idf.py build flash monitor
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

## Fases de Implementação

| Fase | Escopo                                  | Status     |
|------|-----------------------------------------|------------|
| 1    | Estrutura base + MQTT + Dashboard       | 🔄 Em progresso |
| 2    | Sensores (HDC1080, MPU6050, LDR)        | ⏳ Planejado |
| 3    | Autenticação JWT + RBAC + Alembic       | ⏳ Planejado |
| 4    | Captura de áudio + Transcrição Whisper  | ⏳ Planejado |
| 5    | Câmera OV5640 + Streaming               | ⏳ Planejado |

---

## Contribuindo

Veja [CONTRIBUTING.md](CONTRIBUTING.md) para convenções de commits e processo de PR.

---

> Projeto acadêmico — EmbarcaTech Residência em Sistemas Embarcados  
> Hardware: KIIRA V2 (ESP32-S3) · Plataforma: VitalKare
