# ENTREGA 1 — MASTER PLAN: VitalKare

| Campo | Valor |
|---|---|
| **Arquiteto** | GitHub Copilot · Claude Sonnet 4.6 |
| **Data** | 13 de Maio de 2026 |
| **Versão** | 1.0.0 — Baseline Oficial |

---

## SEÇÃO 1 — ANÁLISE TÉCNICA DO DOCUMENTO

### 1.1 Requisitos Explícitos Identificados

| # | Requisito | Módulo | Status Atual |
|---|---|---|---|
| R01 | Botão A → solicitação do paciente (gravação de áudio) | Firmware | Estrutura criada, `audio_capture_start()` comentado |
| R02 | Botão B → emergência crítica (QoS 2, OLED, Buzzer) | Firmware | Fluxo implementado |
| R03 | Sensores ambientais: HDC1080, LDR, MPU6050 | Firmware | Drivers com header; implementação parcial |
| R04 | Display OLED SSD1306 (status, dados, telas) | Firmware | Estrutura com `oled_driver.h` |
| R05 | Armazenamento SD (logs, áudio, imagens, cache offline) | Firmware | `sd_manager.h`, `log_writer.h` presentes |
| R06 | MQTT/TLS com LWT, QoS 0/1/2, reconexão exponencial | Firmware | Estrutura com `mqtt_client.h` |
| R07 | Backend FastAPI + PostgreSQL + Redis | Backend | Esqueleto funcional |
| R08 | MQTT subscriber com roteamento de tópicos | Backend | `mqtt_handler.py` implementado |
| R09 | WebSocket para push em tempo real ao dashboard | Backend | `websocket.py` presente |
| R10 | Heartbeat monitor (90 s offline, 300 s crítico) | Backend | `heartbeat_monitor.py` presente |
| R11 | Autenticação JWT + RBAC (ADMIN, NURSE, DOCTOR, VIEWER) | Backend | `auth.py` route presente |
| R12 | Transcrição Faster-Whisper | Backend | ❌ Não implementado |
| R13 | Dashboard React + Tailwind + Zustand + WebSocket | Frontend | Esqueleto funcional |
| R14 | BedCard com status, sensores, alertas, ações | Frontend | `BedCard.tsx` e `BedGrid.tsx` presentes |
| R15 | EmergencyBanner + fila de alertas | Frontend | `EmergencyBanner.tsx` presente |
| R16 | Camera OV5640 sob demanda com TTL | Firmware/Backend | ❌ Não implementado |
| R17 | OTA de firmware seguro | Firmware | Partições OTA configuradas |
| R18 | Containerização com Docker Compose completo | Infra | `docker-compose.yml` completo |

### 1.2 Requisitos Implícitos Detectados

| # | Requisito Implícito | Risco se Ignorado |
|---|---|---|
| I01 | Sincronização de relógio (NTP) no ESP32-S3 para timestamps precisos | Logs com timestamp errado, auditoria inválida |
| I02 | Paginação nas rotas REST de eventos/leitos | Query explodindo com muitos dados |
| I03 | Rate limiting na API (proteção contra bursts) | Saturação com N dispositivos simultâneos |
| I04 | Validação e sanitização de payloads MQTT no backend | Injeção de dados maliciosos via broker |
| I05 | Migração Alembic inicial para criação do schema | Banco sem estrutura no primeiro deploy |
| I06 | Variáveis de ambiente e `.env.example` documentado | Dev desconhece configurações necessárias |
| I07 | Índices no banco para `bed_id`, `event_type`, `created_at` | Queries lentas em produção |
| I08 | Compressão de payload de áudio (ADPCM/Opus) no firmware | Estouro do buffer MQTT (máx. ~256 KB) |
| I09 | TTL de stream de câmera com desconexão automática | Transmissão contínua desnecessária consome rede |
| I10 | Configuração NVS do ESP32 para Wi-Fi/MQTT (sem hardcode) | Credenciais expostas no binário |
| I11 | Healthcheck endpoint `/health` no backend | Docker não sabe se o serviço está saudável |
| I12 | CORS estrito em produção | Acesso cross-origin não autorizado |

### 1.3 Inconsistências e Riscos Detectados

| # | Tipo | Descrição | Severidade |
|---|---|---|---|
| C01 | Inconsistência | `config.py` usa `algorithm: HS256` (HMAC simétrico), mas a spec menciona RS256 (assimétrico). Para multi-serviço, RS256 é superior. | Média |
| C02 | Risco | Payload de áudio via MQTT base64 (~80 KB) pode exceder limites do broker sem fragmentação. | Alta |
| C03 | Risco | `datetime.utcnow()` (deprecated no Python 3.12+) usado nos models SQLAlchemy. Deve ser `datetime.now(timezone.utc)`. | Média |
| C04 | Lacuna | `app/api/v1/__init__.py` deve registrar todos os subrouters, mas apenas `websocket.py` é importado em `main.py`. | Alta |
| C05 | Inconsistência | Docker Compose tem TimescaleDB mas não há migration criando hipertabelas. | Média |
| C06 | Risco | `EmergencyBanner.tsx` importa mas não há implementação visível no arquivo. | Alta |
| C07 | Lacuna | Não há `schemas/` (Pydantic v2) definidos no backend atual — rotas retornam dados sem validação de saída. | Alta |
| C08 | Risco | Dashboard usa `useAlertsStore.getState()` direto dentro de handler — antipadrão Zustand, cria stale closure. | Média |
| C09 | Lacuna | Firmware tem apenas headers (`.h`) para vários componentes — implementações `.c` ausentes ou com stub mínimo. | Alta |
| C10 | Risco | JWT `secret_key` com default inseguro em config — warning implementado, mas não bloqueia startup em prod. | Alta |

---

## SEÇÃO 2 — ARQUITETURA COMPLETA DO SISTEMA

### 2.1 Visão Macro — Arquitetura em Camadas

```
┌─────────────────────────────────────────────────────────────────────┐
│                     CAMADA DE APRESENTAÇÃO                          │
│  Dashboard Web (React 18 + TypeScript + Tailwind + Zustand)         │
│  WebSocket Client │ REST Client (React Query + Axios)               │
└───────────────────────────────┬─────────────────────────────────────┘
                                │ WSS / HTTPS
┌───────────────────────────────▼─────────────────────────────────────┐
│                     CAMADA DE API / GATEWAY                         │
│  FastAPI (Python 3.11+) │ Nginx Reverse Proxy                       │
│  REST /api/v1/* │ WebSocket /ws/v1/dashboard │ OpenAPI Docs         │
└──────┬────────────────────────┬───────────────────────┬─────────────┘
       │ SQLAlchemy async        │ aiomqtt               │ Redis
┌──────▼──────┐  ┌──────────────▼──────────┐  ┌────────▼────────────┐
│ PostgreSQL  │  │   MQTT Broker           │  │  Redis              │
│ (Timescale) │  │   (Mosquitto / EMQX)    │  │  Cache + Pub/Sub    │
│ Port 5432   │  │   Port 8883 (TLS)       │  │  Port 6379          │
└─────────────┘  └─────────────┬───────────┘  └─────────────────────┘
                                │ MQTT/TLS 8883
┌───────────────────────────────▼─────────────────────────────────────┐
│                     CAMADA EDGE / IoT                               │
│  KIIRA V2 × N (ESP32-S3 + FreeRTOS)                                 │
│  ┌──────────┐ ┌────────────┐ ┌──────────────┐ ┌─────────────────┐  │
│  │ Botões   │ │ Sensores   │ │ Áudio (PDM)  │ │ Câmera OV5640   │  │
│  │ A/B      │ │ HDC/LDR/MPU│ │ I2S CLK:2/37 │ │ Paralelo+I2C    │  │
│  └──────────┘ └────────────┘ └──────────────┘ └─────────────────┘  │
│  ┌──────────┐ ┌────────────┐ ┌──────────────┐                       │
│  │ OLED     │ │ SD Card    │ │ Watchdog     │                       │
│  │ SSD1306  │ │ SDMMC      │ │ IWDT + Task  │                       │
│  └──────────┘ └────────────┘ └──────────────┘                       │
└─────────────────────────────────────────────────────────────────────┘
                                           ┌─────────────────────────┐
                                           │  MinIO (Object Storage) │
                                           │  Áudios / Imagens       │
                                           │  Port 9000              │
                                           └─────────────────────────┘
```

### 2.2 Padrões Arquiteturais Adotados

| Padrão | Aplicação | Justificativa |
|---|---|---|
| Event-Driven Architecture | Core do backend — todos os eventos MQTT disparam handlers assíncronos | Desacoplamento natural da IoT |
| Repository Pattern | Acesso ao banco via SQLAlchemy encapsulado em repositories | Testabilidade e troca de DB |
| Dependency Injection | FastAPI `Depends()` para DB session, Redis, Auth | SOLID — inversão de dependência |
| CQRS lite | Separação entre rotas de leitura (GET) e escrita (POST/PATCH) com schemas distintos | Clareza e performance futura |
| State Machine | Firmware ESP32 — estados bem definidos (`BOOT→WIFI→MQTT→OPERATIONAL→OFFLINE`) | Comportamento previsível |
| Observer | Zustand stores notificando componentes React via WebSocket push | Tempo real sem polling |
| Layered Architecture | Firmware: HAL → Drivers → Components → Tasks → main | Portabilidade e testabilidade |
| Offline-First | Firmware: SD cache → sync ao reconectar | Resiliência crítica para ambiente hospitalar |

---

## SEÇÃO 3 — MÓDULOS DO SISTEMA E RESPONSABILIDADES

### 3.1 Backend — Mapa de Módulos

```
backend/app/
├── api/v1/                  RESPONSABILIDADE: Contratos HTTP e WebSocket
│   ├── routes/auth.py       → Login, refresh, revogar token
│   ├── routes/beds.py       → CRUD leitos + status atual
│   ├── routes/patients.py   → CRUD pacientes + alocação de leitos
│   ├── routes/events.py     → Listagem, filtragem, ACK de eventos
│   ├── routes/audio.py      → Upload, listagem, download de áudios [A CRIAR]
│   ├── routes/media.py      → Imagens, controle de câmera       [A CRIAR]
│   ├── routes/metrics.py    → KPIs, relatórios, exportação CSV  [A CRIAR]
│   └── websocket.py         → Push tempo real para clientes WS
│
├── core/                    RESPONSABILIDADE: Lógica de sistema
│   ├── mqtt_handler.py      → Subscriber, reconexão, dispatch
│   ├── event_processor.py   → Cria eventos no DB, escalona alertas
│   ├── heartbeat_monitor.py → Loop de detecção offline
│   └── notification.py      → Broadcast WebSocket a clientes conectados
│
├── services/                RESPONSABILIDADE: Lógica de negócio [A CRIAR]
│   ├── audio_service.py     → storage → transcrição → DB
│   ├── transcription.py     → Interface Faster-Whisper
│   ├── media_service.py     → Processamento de imagens
│   └── alert_service.py     → Escalonamento, TTL, re-alertas
│
├── models/                  RESPONSABILIDADE: ORM SQLAlchemy
│   ├── bed.py               → Bed, Ward
│   ├── patient.py           → Patient, BedAssignment
│   ├── event.py             → Event, Acknowledgement
│   ├── operator.py          → Operator (usuário do sistema)
│   └── [a criar]            → AudioRecord, Transcription, EnvironmentReading
│
├── schemas/                 RESPONSABILIDADE: Validação I/O [CRIADO — Fase 0]
│   ├── bed.py               → BedCreate, BedRead, BedUpdate
│   ├── event.py             → EventRead, AckCreate
│   ├── patient.py           → PatientCreate, PatientRead
│   └── auth.py              → TokenResponse, LoginRequest
│
└── db/
    ├── session.py           → Engine async + Base declarativa
    └── migrations/          → Alembic [CRIADO — Fase 0]
```

### 3.2 Firmware — Mapa de Componentes

```
firmware/components/
├── core/                    Máquina de estados global, configuração
├── events/                  Fila central (FreeRTOS queue) — coração do firmware
├── network/                 Wi-Fi com backoff exponencial
├── mqtt/                    Publish/subscribe MQTT + LWT
├── buttons/                 ISR + debounce por software (50 ms)
├── sensors/                 HDC1080 + MPU6050 + LDR (ADC)
├── audio/                   Captura PDM I2S, buffer DMA, compressão ADPCM
├── camera/                  OV5640 init, JPEG capture, MJPEG stream
├── display/                 Driver SSD1306 + renderizador de UI
├── storage/                 SD SDMMC + log writer + offline cache
└── watchdog/                IWDT feed + monitoramento de tasks
```

### 3.3 Frontend — Mapa de Componentes

```
dashboard/src/
├── pages/
│   ├── Dashboard.tsx        Grid de leitos + WebSocket handler
│   ├── Login.tsx            Formulário de autenticação
│   └── [a criar]
│       ├── BedDetails.tsx   Histórico, sensores, câmera, transcrições
│       ├── Events.tsx       Fila completa de eventos + filtros
│       └── Reports.tsx      Métricas + gráficos + exportação
│
├── components/
│   ├── beds/                BedCard, BedGrid, BedStatusBadge
│   ├── alerts/              EmergencyBanner, AlertQueue
│   └── [a criar]
│       ├── events/          EventList, EventCard, AcknowledgeModal
│       ├── sensors/         EnvironmentPanel, SensorChart
│       ├── audio/           TranscriptionCard, AudioPlayer
│       ├── camera/          CameraModal, CameraStream
│       └── layout/          Sidebar, TopBar, Layout
│
├── hooks/
│   ├── useWebSocket.ts      Hook WS com auto-reconnect
│   └── [a criar]            useBeds, useEvents, useAuth
│
├── store/
│   ├── bedsStore.ts         Estado global de leitos
│   ├── alertsStore.ts       Fila de alertas e emergências
│   └── authStore.ts         Token JWT, operador logado
│
└── services/
    ├── api.ts               Axios com interceptors + JWT refresh
    └── [a criar]            websocketService.ts (singleton)
```

---

## SEÇÃO 4 — FLUXO DE DADOS E EVENTOS

### 4.1 Fluxo Principal — Emergência End-to-End

```
[Paciente pressiona Botão B]
         │
         ▼ (ISR GPIO 46 → debounce 50ms)
[task_button_handler] → post(EVT_EMERGENCY) na fila interna
         │
         ▼
[task_event_dispatcher]
   ├── core_set_state(SYS_STATE_EMERGENCY)
   ├── ui_set_screen(UI_SCREEN_EMERGENCY)
   ├── buzzer PWM 800Hz × 3 ciclos
   ├── log_write(CRITICAL, BUTTON, "Emergência")
   └── vitalkare_mqtt_publish_emergency("BUTTON_B") QoS 2
         │
         ▼ MQTT/TLS 8883
[MQTT Broker] → tópico hospital/ala-a/leito01/emergency
         │
         ▼
[Backend: mqtt_handler._dispatch()]
   └── event_processor.process_emergency(topic, payload)
         │
         ▼
[event_processor]
   ├── INSERT events (priority=1, status=PENDING)
   ├── UPDATE beds SET status=EMERGENCY
   ├── alert_service.escalate_emergency(event)
   └── notification.broadcast_emergency(event)
         │
         ▼ WebSocket (wss://)
[Dashboard React]
   ├── useWebSocket → handleMessage(type="emergency")
   ├── alertsStore.addAlert()
   ├── bedsStore.updateBedStatus(bedId, "EMERGENCY")
   ├── EmergencyBanner visível (piscante)
   └── Audio alert (Web Audio API)
```

### 4.2 Fluxo de Sincronização Offline

```
[Wi-Fi perdido]
    │
    ▼
core_set_state(SYS_STATE_OFFLINE)
    │
    ├── Eventos → offline_cache_push(event) → SD /cache/events.json
    ├── OLED → UI_SCREEN_OFFLINE (indicador visual)
    └── Buzzer → 1 bip a cada 60 s

[Reconexão Wi-Fi → MQTT estabelecido]
    │
    ▼
on_mqtt_connected()
    │
    └── if(offline_cache_pending_count() > 0)
            → post(EVT_OFFLINE_SYNC_NEEDED)
            → task_event_dispatcher processa cache
            → republica eventos em ordem cronológica
            → flag offline_delayed=true nos eventos P1
```

---

## SEÇÃO 5 — ESTRUTURA DEFINITIVA DO MONOREPO

```
VitalKare/
├── README.md
├── CONTRIBUTING.md
├── VitalKare_Especificacao_Tecnica.md
│
├── firmware/                               ← ESP32-S3 (ESP-IDF + FreeRTOS)
│   ├── CMakeLists.txt
│   ├── partitions.csv
│   ├── sdkconfig.defaults
│   ├── main/
│   ├── components/
│   └── config/
│
├── backend/                                ← Python 3.11 + FastAPI
│   ├── Dockerfile
│   ├── requirements.txt
│   ├── .env.example                        ✅ [FASE 0]
│   ├── alembic.ini                         ✅ [FASE 0]
│   └── app/
│       ├── main.py
│       ├── config.py
│       ├── dependencies.py
│       ├── api/v1/
│       │   ├── __init__.py                 ✅ Todos os routers registrados
│       │   ├── websocket.py
│       │   └── routes/
│       │       ├── auth.py
│       │       ├── beds.py
│       │       ├── patients.py
│       │       ├── events.py
│       │       ├── audio.py                [A CRIAR — Fase 4]
│       │       ├── media.py                [A CRIAR — Fase 5]
│       │       └── metrics.py              [A CRIAR — Fase 6]
│       ├── core/
│       │   ├── mqtt_handler.py
│       │   ├── event_processor.py
│       │   ├── heartbeat_monitor.py
│       │   └── notification.py             ✅ Broadcast implementado
│       ├── services/                       [A CRIAR — Fases 4/5/6]
│       ├── models/
│       │   ├── bed.py
│       │   ├── event.py
│       │   ├── patient.py
│       │   ├── operator.py
│       │   ├── audio_record.py             ✅ [FASE 0]
│       │   └── environment_reading.py      ✅ [FASE 0]
│       ├── schemas/                        ✅ [FASE 0]
│       │   ├── bed.py
│       │   ├── event.py
│       │   ├── patient.py
│       │   ├── auth.py
│       │   ├── audio.py
│       │   └── environment.py
│       ├── db/
│       │   ├── session.py
│       │   └── migrations/                 ✅ [FASE 0]
│       └── utils/
│           ├── security.py
│           └── storage.py                  [A CRIAR — Fase 4]
│
├── dashboard/                              ← React 18 + TypeScript + Tailwind
│   └── src/
│       ├── pages/
│       │   ├── Dashboard.tsx
│       │   ├── Login.tsx
│       │   ├── BedDetails.tsx              [A CRIAR — Fase 3]
│       │   ├── Events.tsx                  [A CRIAR — Fase 3]
│       │   └── Reports.tsx                 [A CRIAR — Fase 6]
│       ├── components/
│       ├── hooks/
│       ├── store/
│       └── services/
│           ├── api.ts
│           └── websocketService.ts         [A CRIAR — Fase 0]
│
├── docker/
│   ├── docker-compose.yml
│   ├── docker-compose.prod.yml             [A CRIAR]
│   └── mosquitto/
│
├── docs/                                   [A CRIAR]
└── scripts/                                [A CRIAR]
```

---

## SEÇÃO 6 — BANCO DE DADOS — SCHEMA DEFINITIVO

```sql
-- Extensões necessárias
CREATE EXTENSION IF NOT EXISTS "uuid-ossp";
CREATE EXTENSION IF NOT EXISTS timescaledb;

-- wards (alas hospitalares)
CREATE TABLE wards (
    id          UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    name        VARCHAR(100) NOT NULL UNIQUE,
    floor       INT NOT NULL DEFAULT 1,
    capacity    INT NOT NULL DEFAULT 10,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- beds (leitos)
CREATE TABLE beds (
    id               UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    ward_id          UUID NOT NULL REFERENCES wards(id),
    bed_number       VARCHAR(20) NOT NULL UNIQUE,
    device_id        VARCHAR(50) NOT NULL UNIQUE,
    status           VARCHAR(20) NOT NULL DEFAULT 'FREE',
    last_heartbeat   TIMESTAMPTZ,
    firmware_version VARCHAR(20),
    metadata         JSONB DEFAULT '{}',
    created_at       TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
CREATE INDEX idx_beds_ward_id   ON beds(ward_id);
CREATE INDEX idx_beds_status    ON beds(status);
CREATE INDEX idx_beds_device_id ON beds(device_id);

-- events
CREATE TABLE events (
    id           UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    bed_id       UUID NOT NULL REFERENCES beds(id),
    event_type   VARCHAR(20) NOT NULL,
    priority     INT NOT NULL DEFAULT 5,
    status       VARCHAR(20) NOT NULL DEFAULT 'PENDING',
    payload      JSONB,
    source_topic VARCHAR(200),
    created_at   TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    resolved_at  TIMESTAMPTZ
);
CREATE INDEX idx_events_bed_id      ON events(bed_id);
CREATE INDEX idx_events_status      ON events(status);
CREATE INDEX idx_events_priority    ON events(priority);
CREATE INDEX idx_events_created_at  ON events(created_at DESC);
CREATE INDEX idx_events_type_status ON events(event_type, status);

-- environment_readings (time-series — hipertabela TimescaleDB)
CREATE TABLE environment_readings (
    id                  UUID DEFAULT uuid_generate_v4(),
    bed_id              UUID NOT NULL REFERENCES beds(id),
    temperature_celsius FLOAT,
    humidity_percent    FLOAT,
    luminosity_lux      FLOAT,
    noise_level_db      FLOAT,
    motion_detected     BOOLEAN DEFAULT FALSE,
    recorded_at         TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
SELECT create_hypertable('environment_readings', 'recorded_at');
CREATE INDEX idx_env_bed_id ON environment_readings(bed_id, recorded_at DESC);
```

---

## SEÇÃO 7 — ESTRATÉGIAS TRANSVERSAIS

### 7.1 Segurança

| Camada | Mecanismo | Implementação |
|---|---|---|
| Firmware | NVS criptografada (ESP32-S3 flash encryption) | `CONFIG_FLASH_ENCRYPTION_ENABLED` |
| MQTT | TLS 1.3 porta 8883 + autenticação usuario/senha | Mosquitto + certificado CA interno |
| API REST | JWT HS256 (dev) / RS256 (prod) + HTTPS | FastAPI + python-jose |
| WebSocket | WSS + token validado no handshake | Header `Authorization: Bearer <token>` |
| RBAC | `require_role(["NURSE", "ADMIN"])` nas rotas | FastAPI dependency |
| Senhas | bcrypt (cost factor 12) | `passlib[bcrypt]` |
| OTA | Firmware assinado com chave privada | ESP-IDF Secure Boot v2 |
| Rate Limiting | Redis sliding window (100 req/min por IP) | slowapi |
| CORS | Whitelist estrita em produção | `settings.cors_origins` validado |

### 7.2 Tolerância a Falhas

| Falha | Comportamento | Recuperação |
|---|---|---|
| Wi-Fi perdido | Modo offline + cache SD | Backoff exponencial 2-120s + jitter |
| MQTT perdido | Idem (Wi-Fi OK mas broker caiu) | Reconnect loop |
| SD Card falho | Log apenas em RAM (circular buffer) | Operação degradada |
| Sensor falho | Envia null, evento SYSTEM gerado | Driver com retry 3x |
| Watchdog trigger | Hard reset do ESP32-S3 | NVS preservada |
| Backend caindo | Broker continua, mensagens persistidas | Consome fila ao reiniciar |
| DB indisponível | Backend retorna 503, buffer em Redis | Health check + retry |

### 7.3 Estratégia OTA

```
Partições: factory (fallback) | ota_0 (ativo) | ota_1 (novo)

Fluxo:
  1. Backend detecta versão desatualizada via heartbeat
  2. Publica hospital/{ward}/{id}/cmd/ota com URL + SHA256
  3. Firmware inicia esp_https_ota() para ota_1
  4. Verifica hash SHA256 antes de commitar
  5. esp_ota_set_boot_partition(ota_1) → restart
  6. Se falhar: rollback automático para ota_0
```

---

## SEÇÃO 8 — ROADMAP DE DESENVOLVIMENTO

### 8.1 Estado Atual

| Módulo | Completude |
|---|---|
| Firmware — estrutura base | ~25% |
| Backend — FastAPI setup | ~35% |
| Backend — rotas REST | ~20% |
| Backend — WebSocket | ~30% |
| Backend — services | ~0% |
| Backend — migrations Alembic | ~0% |
| Frontend — estrutura base | ~30% |
| Frontend — páginas extras | ~15% |
| Infra — Docker Compose | ~85% |
| Documentação | ~90% |

### 8.2 Fases de Desenvolvimento

#### ✅ FASE 0 — FUNDAÇÕES CONCLUÍDA (Semana 1–2)

| # | Tarefa | Status |
|---|---|---|
| 0.1 | Backend — Alembic + Migração inicial do schema completo | ✅ Concluído |
| 0.2 | Backend — Schemas Pydantic v2 para todos os models | ✅ Concluído |
| 0.3 | Backend — Fix: registrar todos os routers em `api/v1/__init__` | ✅ Já estava correto |
| 0.4 | Backend — Fix: `datetime.now(timezone.utc)` nos models | ✅ Concluído |
| 0.5 | Backend — `.env.example` documentado | ✅ Concluído |
| 0.6 | Backend — `/health` endpoint | ✅ Concluído |
| 0.7 | Backend — Models `AudioRecord`, `EnvironmentReading` | ✅ Concluído |
| 0.8 | Frontend — `websocketService.ts` singleton | ✅ Concluído |
| 0.9 | Frontend — `api.ts` com interceptors JWT + refresh | ✅ Concluído |

#### 🔄 FASE 1 — MVP FUNCIONAL — EM ANDAMENTO (Semanas 3–6)

> **Critério de aceitação:** Botão emergência → aparece no dashboard em < 2 s

| # | Tarefa | Status |
|---|---|---|
| 1.1 | Backend — `event_processor.py` completo | ⏳ Pendente |
| 1.2 | Backend — `heartbeat_monitor.py` detecção offline | ⏳ Pendente |
| 1.3 | Backend — `auth.py` login + refresh + RBAC | ✅ Implementado |
| 1.4 | Backend — Rotas `beds.py` e `events.py` com schemas | ⏳ Pendente |
| 1.5 | Firmware — `wifi_manager.c` backoff exponencial | ⏳ Pendente |
| 1.6 | Firmware — `mqtt_client.c` publish/subscribe + LWT | ⏳ Pendente |
| 1.7 | Firmware — `button_handler.c` ISR + debounce | ⏳ Pendente |
| 1.8 | Firmware — `event_dispatcher.c` fila FreeRTOS | ⏳ Pendente |
| 1.9 | Frontend — `EmergencyBanner.tsx` implementado | ⏳ Pendente |
| 1.10 | Frontend — `BedCard.tsx` completo | ⏳ Pendente |
| 1.11 | Frontend — `Login.tsx` com JWT | ⏳ Pendente |

#### ⏳ FASE 2 — SENSORES E ARMAZENAMENTO (Semanas 7–10)

> **Critério:** Dashboard mostra temp/umidade em tempo real

#### ⏳ FASE 3 — DISPLAY E UX (Semanas 11–12)

> **Critério:** OLED exibe status correto em todos os estados

#### ⏳ FASE 4 — ÁUDIO E TRANSCRIÇÃO (Semanas 13–18)

> **Critério:** Paciente fala → texto aparece no dashboard em < 5 s

#### ⏳ FASE 5 — CÂMERA E STREAMING (Semanas 19–25)

> **Critério:** Operador solicita → vê câmera em < 3 s; log registrado

#### ⏳ FASE 6 — RELATÓRIOS E ANALYTICS (Semanas 26–30)

> **Critério:** Gestão extrai relatório de turno em < 30 s

#### ⏳ FASE 7 — IA EMBARCADA — TinyML (Semanas 31–42)

> **Critério:** Queda detectada → evento P1 sem intervenção do paciente

### 8.3 Dependências Críticas

```
Fase 0 → BLOQUEIA tudo.
Fase 1 → Depende de Fase 0.
Fase 2 → Pode iniciar em paralelo com Fase 1 (firmware independente).
Fase 3 → Depende de Fase 1 + Fase 2.
Fase 4 → Depende de Fase 3.
Fase 5 → Depende de Fase 4.
Fase 6 → Depende de Fase 1–4.
Fase 7 → Depende de Fase 2 + Fase 4.
```

### 8.4 Riscos Técnicos

| Risco | Probabilidade | Impacto | Mitigação |
|---|---|---|---|
| Payload de áudio MQTT > limite do broker | Alta | Alto | Fragmentação em chunks de 32 KB |
| OV5640 conflito de barramento | Média | Alto | Interface paralela para dados, I2C apenas para config |
| Faster-Whisper lento em CPU | Alta | Médio | Modelo `tiny/base` para latência < 3s |
| TimescaleDB incompatível com Alembic | Média | Médio | `create_hypertable` em raw SQL na migration |
| Memória insuficiente para áudio + câmera simultâneos | Alta | Alto | Tasks com prioridade; câmera suspende áudio |
| PSRAM necessária para buffer de câmera | Certa | Alto | `CAMERA_FB_IN_PSRAM` — KIIRA V2 tem PSRAM |

---

## SEÇÃO 9 — VARIÁVEIS DE AMBIENTE

```bash
# backend/.env.example
DATABASE_URL=postgresql+asyncpg://vitalkare:SENHA@postgres:5432/vitalkare
REDIS_URL=redis://redis:6379/0
MQTT_HOST=mosquitto
MQTT_PORT=8883
MQTT_USE_TLS=true
MQTT_USERNAME=vitalkare-backend
MQTT_PASSWORD=SENHA_MQTT
SECRET_KEY=<256-bit random hex>
ALGORITHM=HS256
ACCESS_TOKEN_EXPIRE_MINUTES=60
REFRESH_TOKEN_EXPIRE_HOURS=24
STORAGE_ENDPOINT=minio:9000
STORAGE_ACCESS_KEY=CHAVE_MINIO
STORAGE_SECRET_KEY=SENHA_MINIO
WHISPER_MODEL=small
WHISPER_LANGUAGE=pt
WHISPER_DEVICE=cpu
ENVIRONMENT=production
DEBUG=false
CORS_ORIGINS=https://vitalkare.hospital.local
```

---

## SEÇÃO 10 — CONVENÇÕES DO PROJETO

| Domínio | Convenção |
|---|---|
| Python (Backend) | PEP 8, Black formatter, isort, type hints obrigatórios |
| TypeScript (Frontend) | ESLint + Prettier, strict mode, interfaces para todos os tipos |
| C (Firmware) | `snake_case` para funções, `SCREAMING_SNAKE_CASE` para macros, prefixo por módulo |
| SQL | UPPERCASE para keywords, `snake_case` para nomes |
| MQTT topics | `hospital/{ward}/{device_id}/{subtopic}` — lowercase |
| Branches Git | `main`, `develop`, `feature/<nome>`, `fix/<nome>` |
| Commits | Conventional Commits: `feat:`, `fix:`, `docs:`, `chore:`, `test:` |
| Versão | Semantic Versioning: MAJOR.MINOR.PATCH |

---

## RESUMO EXECUTIVO

| Dimensão | Estado Atual | Meta Fase 1 | Meta Final |
|---|---|---|---|
| Firmware | ~25% | ~50% | 100% |
| Backend | ~35% | ~60% | 100% |
| Frontend | ~30% | ~55% | 100% |
| Infra | ~85% | ~90% | 100% |
| Testes | ~0% | ~30% | ~80% |
| Documentação | ~95% | ~95% | 100% |
