# VitalKare — Plataforma Hospitalar Inteligente

> Sistema IoT distribuído para monitoramento de leitos hospitalares em tempo real: sensores ambientais, captura de áudio com transcrição por IA, câmera sob demanda, alertas de emergência e dashboard web via WebSocket.

**Hardware:** KIIRA V2 (ESP32-S3) · **Firmware:** ESP-IDF 5.x + FreeRTOS · **Backend:** FastAPI · **Dashboard:** React 18

> **Origem:** Residência EmbarcaTech — Evolução do projeto final (BitDogLab/RP2040 + UDP) para arquitetura de produção hospitalar.

---

## Documentação

| Documento | Descrição |
|---|---|
| [ESPECIFICACOES_COMPLETAS.md](ESPECIFICACOES_COMPLETAS.md) | Especificações completas: hardware, firmware, backend, dashboard, fluxos, banco de dados e glossário |
| [VitalKare_Especificacao_Tecnica.md](VitalKare_Especificacao_Tecnica.md) | Especificação técnica arquitetural detalhada (baseline oficial) |
| [Mapeamento.md](Mapeamento.md) | Master plan: análise de requisitos, fases e tarefas |
| [checkpoint.md](checkpoint.md) | Estado de desenvolvimento por sessão |
| [dashboard-simulation.html](dashboard-simulation.html) | Simulação do dashboard (abre direto no navegador, sem servidor) |

---

## Visão Geral da Arquitetura

```
┌─────────────────────────────────────────────────────────┐
│              KIIRA V2 (ESP32-S3) — Por leito             │
│   FreeRTOS · Botões A/B · HDC1080 · MPU6050 · LDR       │
│   Microfone PDM · Câmera OV5640 · OLED · SD Card        │
└────────────────────────┬────────────────────────────────┘
                         │ MQTT/TLS (QoS 0/1/2)
                         ▼
              ┌─────────────────┐
              │ Mosquitto MQTT  │
              └────────┬────────┘
                       │
              ┌────────▼────────┐
              │  FastAPI Backend│ ← WebSocket → Dashboard React
              │  (Python 3.11+) │
              └────────┬────────┘
                       │
        ┌──────────────┼──────────────┐
        ▼              ▼              ▼
  PostgreSQL 16    Redis 7        MinIO
  + TimescaleDB  (cache/filas)  (áudio/imagens)
                       │
              Faster-Whisper (transcrição PT)
```

---

## Stack Tecnológica

| Camada | Tecnologia |
|---|---|
| Firmware | ESP-IDF 5.x · FreeRTOS · C17 |
| Backend | FastAPI · Python 3.11 · SQLAlchemy 2.0 async |
| Dashboard | React 18 · TypeScript · Tailwind CSS · Zustand |
| Mensageria | MQTT (Mosquitto/EMQX) · aiomqtt |
| Banco | PostgreSQL 16 + TimescaleDB |
| Cache | Redis 7 |
| Storage | MinIO (S3-compatível) |
| Transcrição IA | Faster-Whisper (modelo small/pt — self-hosted) |
| Infra | Docker Compose |

---

## Estrutura do Repositório

```
VitalKare/
├── firmware/               # ESP-IDF — KIIRA V2 (ESP32-S3)
│   ├── config/             # app_config.h, device_config.h, hardware_config.h, mqtt_config.h
│   ├── main/               # main.c — orquestrador de tasks FreeRTOS
│   ├── components/         # Módulos FreeRTOS independentes
│   │   ├── core/           # Máquina de estados global
│   │   ├── events/         # Event dispatcher central (FreeRTOS queue)
│   │   ├── network/        # Wi-Fi manager com backoff exponencial
│   │   ├── mqtt/           # Cliente MQTT (QoS 0/1/2, LWT, offline cache)
│   │   ├── http_client/    # Cliente HTTP REST alternativo
│   │   ├── buttons/        # Handler GPIO com debounce por software
│   │   ├── sensors/        # HDC1080 (temp/umidade), MPU6050, LDR
│   │   ├── display/        # Driver OLED SSD1306 + 7 telas de UI
│   │   ├── audio/          # Captura PDM I2S (16 kHz, até 10 s, PSRAM)
│   │   ├── camera/         # OV5640 JPEG/MJPEG (esp32-camera, PSRAM)
│   │   ├── storage/        # SD SDMMC + log JSON Lines + cache offline
│   │   └── watchdog/       # IWDT + monitoramento de tasks
│   └── tools/
│       └── provision.py    # Provisiona NVS via esptool (sem hardcode de credenciais)
│
├── backend/                # FastAPI — Servidor central
│   └── app/
│       ├── api/v1/         # Rotas REST + WebSocket (/ws/v1/dashboard)
│       ├── core/           # MQTT handler, event processor, heartbeat monitor
│       ├── models/         # SQLAlchemy ORM (beds, events, patients, audio, env)
│       ├── schemas/        # Pydantic v2 — validação request/response
│       ├── utils/          # JWT (python-jose), bcrypt (passlib)
│       └── db/             # Sessão async + Alembic migrations
│
├── dashboard/              # React 18 + TypeScript + Tailwind
│   └── src/
│       ├── components/     # BedCard, BedGrid, EmergencyBanner
│       ├── hooks/          # useWebSocket (auto-reconnect)
│       ├── pages/          # Dashboard, Login
│       ├── services/       # Axios + interceptors JWT, WebSocket singleton
│       └── store/          # Zustand — beds, alerts, auth
│
└── docker/                 # Docker Compose + configs
    ├── docker-compose.yml  # Postgres, Redis, MinIO, Mosquitto, Backend, Dashboard
    └── mosquitto/
        └── mosquitto.conf
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
# O script solicita SSID, senha, IP do servidor VitalKare (TCP/HTTP), device_id e ward_id
```

### 5. Firmware — Flash direto (credenciais em device_config.h)

```bash
# Edite firmware/config/device_config.h com WiFi e IP do broker
cd firmware
idf.py set-target esp32s3
idf.py -p COM16 -b 921600 flash monitor
```

### 6. Perfis de placa — KIIRA V2 x ESP32-S3 DevKit

O firmware agora suporta dois perfis de placa no arquivo `firmware/config/board_profile.h`:

| Perfil | Objetivo | O que sobe |
|---|---|---|
| `VK_BOARD_PROFILE_KIIRA_V2` | Firmware completo da placa alvo | Wi-Fi, TCP, botões, sensores, oxímetro, OLED, storage, câmera |
| `VK_BOARD_PROFILE_ESP32S3_DEVKIT` | Bancada de validação no DevKit | Wi-Fi, TCP, handshake, heartbeat, telemetria básica de sistema |

Para usar o **ESP32-S3 DevKit**, altere a linha:

```c
#define VK_BOARD_PROFILE VK_BOARD_PROFILE_KIIRA_V2
```

para:

```c
#define VK_BOARD_PROFILE VK_BOARD_PROFILE_ESP32S3_DEVKIT
```

No perfil reduzido, o firmware ignora periféricos exclusivos da KIIRA e mantém o caminho útil para testes de:

1. associação Wi-Fi
2. handshake TCP com `server/server.py`
3. heartbeat/reconexão
4. chegada de telemetria básica no dashboard

### 7. Assistente interativo para Windows

Na raiz do repositório existe o script:

```bat
firmware-assistant.bat
```

Ele guia o fluxo de:

1. escolha do alvo (**KIIRA V2** ou **ESP32-S3 DevKit**)
2. seleção da porta COM
3. `fullclean` opcional
4. build
5. flash
6. abertura opcional do monitor serial

Agora ele funciona como um **menu persistente**, então você pode:

1. configurar alvo e COM uma vez
2. rodar **build**, **flash**, **monitor** e **fullclean** separadamente
3. executar um **fluxo guiado** em sequência
4. ver o último resultado no topo da tela
5. voltar ao menu após erro para tentar de novo, inclusive reabrindo o monitor

Ele também lembra o passo de colocar a placa em **modo download/boot** antes do flash.

### 8. Inicializacao do sistema e provisionamento da placa

Na raiz do repositório existe outro atalho:

```bat
system-startup-assistant.bat
```

Ele abre um fluxo operacional de bancada para:

1. detectar o **IP LAN** do notebook/PC
2. atualizar `dashboard/.env.local` com esse host
3. subir o **server** e o **dashboard**
4. perguntar **SSID**, **senha**, porta COM e identidade da placa
5. gravar essas informacoes na NVS da **KIIRA** ou **ESP32-S3 DevKit**
6. opcionalmente escutar a serial e capturar o **IP obtido** pela placa
7. registrar esse IP no backend, criar/atualizar o slot provisionado e aprovar o dispositivo para ele aparecer no dashboard publico

No perfil **ESP32-S3 DevKit**, o firmware agora exige credenciais provisionadas em NVS para evitar cair silenciosamente em um SSID hardcoded incorreto de bancada.

---

## Tópicos MQTT

Padrão: `hospital/{ala}/{device_id}/{subtopico}`

| Subtópico | QoS | Direção | Descrição |
|---|---|---|---|
| `status` | 1 | Leito → Broker | Heartbeat a cada 30 s (RSSI, heap, versão) |
| `emergency` | 2 | Leito → Broker | Chamada de emergência — entrega garantida |
| `request` | 1 | Leito → Broker | Solicitação do paciente (botão A) |
| `environment` | 1 | Leito → Broker | Temperatura, umidade, luminosidade, movimento |
| `audio` | 1 | Leito → Broker | Payload de áudio base64 (ADPCM, até 80 KB) |
| `image` | 1 | Leito → Broker | Frame JPEG base64 da câmera OV5640 |
| `log` | 0 | Leito → Broker | Logs do sistema (best-effort) |
| `cmd/camera` | 1 | Broker → Leito | Solicita captura de imagem ou stream |
| `cmd/ack` | 1 | Broker → Leito | Confirmação de atendimento (exibe no OLED) |
| `cmd/buzzer` | 1 | Broker → Leito | Ativa/desativa buzzer remotamente |

---

## Estado do Projeto

| Camada | Componente | Status |
|---|---|---|
| Firmware | Wi-Fi Manager + NVS provisioning | ✅ Completo |
| Firmware | HTTP Client REST (comunicação com backend) | ✅ Completo |
| Firmware | Sensores: HDC1080, MPU6050, LDR | ✅ Completo |
| Firmware | Display OLED SSD1306 + UI Renderer (7 telas) | ✅ Completo |
| Firmware | Captura de áudio PDM I2S (PSRAM, até 10 s) | ✅ Completo |
| Firmware | Câmera OV5640 JPEG (esp32-camera, PSRAM) | ✅ Completo |
| Firmware | SD Card SDMMC + Log JSON Lines + Cache Offline | ✅ Completo |
| Firmware | Watchdog IWDT + Botões GPIO com debounce | ✅ Completo |
| Firmware | MQTT Client (QoS 0/1/2, LWT, offline cache) | ✅ Completo |
| Backend | FastAPI + JWT (RBAC) + WebSocket | ✅ Completo |
| Backend | MQTT handler → event processor → PostgreSQL | ✅ Completo |
| Backend | Alembic migrations + Pydantic v2 schemas | ✅ Completo |
| Backend | Heartbeat monitor (threshold 90 s) | ✅ Completo |
| Backend | Serviço de transcrição Faster-Whisper | ⏳ Pendente |
| Backend | Rotas de áudio, mídia e relatórios | ⏳ Pendente |
| Dashboard | React 18 + Zustand + Tailwind + WebSocket | ✅ Completo |
| Dashboard | BedCard + BedGrid + EmergencyBanner | ✅ Completo |
| Dashboard | Páginas de detalhes, eventos e relatórios | ⏳ Pendente |
| Infra | Docker Compose (Postgres, Redis, MinIO, Mosquitto) | ✅ Completo |

---

## Simulação do Dashboard

Abra [dashboard-simulation.html](dashboard-simulation.html) diretamente no navegador — sem nenhum servidor necessário.

- 20 leitos com status dinâmico, sparklines de temperatura e indicador de sinal
- Cenários automáticos: heartbeat, emergência, admissão, alta, gravação de áudio, offline/reconexão
- Filtros por status, log de eventos ao vivo e toasts de notificação
- Tema escuro com layout responsivo

---

## Configuração de Rede Local (LAN)

```
Notebook (Docker)           ESP32-S3 KIIRA V2
───────────────────         ──────────────────
Mosquitto  :1883   ←─ Wi-Fi ─  MQTT Client
FastAPI    :8000   ←─ Wi-Fi ─  HTTP REST Client
Dashboard  :5173             (browser no mesmo PC)
```

1. Descubra o IP do notebook: `ipconfig` (Windows)
2. Edite `docker/.env` → `HOST_LAN_IP=<seu_ip>`
3. Edite `dashboard/.env.local` → `VITE_BACKEND_HOST=<seu_ip>`
4. Provisione o firmware: `python firmware/tools/provision.py --port COM16`

> **Observação para bancada DevKit:** o firmware publica um perfil reduzido e telemetria de sistema para validar conectividade mesmo sem OLED, sensores, SD ou câmera.

---

## Contribuindo

Veja [CONTRIBUTING.md](CONTRIBUTING.md) para convenções de commits e processo de PR.

---

> Projeto acadêmico — EmbarcaTech Residência em Sistemas Embarcados  
> Hardware: KIIRA V2 (ESP32-S3) · Plataforma: VitalKare · Maio 2026
