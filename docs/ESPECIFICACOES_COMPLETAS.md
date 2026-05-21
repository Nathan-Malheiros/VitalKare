# VitalKare — Especificações Completas do Projeto

> **Versão:** 2.0.0 — Documento Consolidado  
> **Data:** Maio de 2026  
> **Residência:** EmbarcaTech — Capacitação em Sistemas Embarcados  
> **Hardware alvo:** KIIRA V2 (ESP32-S3)  
> **Autor:** Nathan

---

## Sumário

1. [Visão Geral e Ideia Central](#1-visão-geral-e-ideia-central)
2. [Origem — Projeto EmbarcaTech Original](#2-origem--projeto-embarcatech-original)
3. [Evolução para VitalKare](#3-evolução-para-vitalkare)
4. [Hardware — Placa KIIRA V2](#4-hardware--placa-kiira-v2)
5. [Arquitetura Completa do Sistema](#5-arquitetura-completa-do-sistema)
6. [Estrutura de Pastas do Repositório](#6-estrutura-de-pastas-do-repositório)
7. [Firmware — ESP32-S3 + FreeRTOS](#7-firmware--esp32-s3--freertos)
8. [Backend — FastAPI + PostgreSQL](#8-backend--fastapi--postgresql)
9. [Dashboard — React 18 + TypeScript](#9-dashboard--react-18--typescript)
10. [Infraestrutura — Docker Compose](#10-infraestrutura--docker-compose)
11. [Protocolo MQTT — Tópicos e Payloads](#11-protocolo-mqtt--tópicos-e-payloads)
12. [Banco de Dados — Schema Completo](#12-banco-de-dados--schema-completo)
13. [Transcrição de Áudio com IA](#13-transcrição-de-áudio-com-ia)
14. [Segurança](#14-segurança)
15. [Tolerância a Falhas e Modo Offline](#15-tolerância-a-falhas-e-modo-offline)
16. [Fluxos de Operação End-to-End](#16-fluxos-de-operação-end-to-end)
17. [Roadmap e Estado Atual](#17-roadmap-e-estado-atual)
18. [Variáveis de Ambiente](#18-variáveis-de-ambiente)
19. [Glossário Técnico](#19-glossário-técnico)

---

## 1. Visão Geral e Ideia Central

### 1.1 O Problema

Ambientes hospitalares modernos sofrem com dois problemas críticos que impactam diretamente a qualidade do atendimento:

- **Fadiga de alarmes:** volume excessivo de alertas de baixa urgência reduz a capacidade de resposta da equipe clínica a situações realmente críticas.
- **Comunicação assimétrica:** pacientes têm dificuldade em expressar suas necessidades de forma rápida, clara e rastreável, enquanto a equipe muitas vezes não sabe o motivo de uma chamada antes de se deslocar ao leito.

### 1.2 A Solução VitalKare

O **VitalKare** é uma plataforma hospitalar inteligente e distribuída que substitui o sistema tradicional de chamada de enfermagem por uma rede de **nós inteligentes** instalados em cada leito. Cada nó é capaz de:

- Capturar áudio do paciente e transcrever automaticamente com IA
- Monitorar continuamente temperatura, umidade, luminosidade e movimento do quarto
- Transmitir vídeo sob demanda ao profissional de saúde
- Emitir alertas de emergência com prioridade máxima e entrega garantida
- Operar de forma autônoma mesmo sem conectividade (modo offline com sincronização posterior)
- Exibir informações localmente no display OLED do leito

Todos os dados fluem em tempo real para um **dashboard web centralizado**, acessível de qualquer estação da rede hospitalar, mostrando o estado de todos os leitos simultaneamente.

### 1.3 Escopo Funcional Completo

| Funcionalidade | Descrição | Prioridade |
|---|---|---|
| Botão de Emergência | Alerta crítico QoS 2, prioridade máxima, OLED + Buzzer | P1 — Crítico |
| Gravação de Solicitação | Captura de áudio + transcrição automática Whisper | P1 — Crítico |
| Monitoramento Ambiental | Temperatura, umidade, luminosidade, ruído (a cada 30 s) | P2 — Alta |
| Dashboard Central | Grid de leitos em tempo real via WebSocket | P1 — Crítico |
| Câmera Sob Demanda | JPEG/MJPEG stream ativado pelo operador | P3 — Média |
| Display OLED Local | Status, dados do paciente, estado do sistema | P2 — Alta |
| Armazenamento Local SD | Logs, áudios, imagens, cache offline | P2 — Alta |
| Heartbeat e Offline | Detecção automática de dispositivos offline (90 s) | P1 — Crítico |
| Autenticação RBAC | Roles: ADMIN, NURSE, DOCTOR, VIEWER | P1 — Crítico |
| Log e Auditoria | Registro completo com timestamp para conformidade | P2 — Alta |
| OTA de Firmware | Atualização segura remota com rollback automático | P3 — Média |
| Relatórios | Métricas, KPIs de turno, exportação CSV | P4 — Baixa |
| TinyML Embarcado | Detecção de queda, padrões anômalos (fase futura) | P5 — Futuro |

---

## 2. Origem — Projeto EmbarcaTech Original

### 2.1 Plataforma Original

O projeto foi originalmente desenvolvido para a capacitação **EmbarcaTech** utilizando:

- **Hardware:** BitDogLab (Raspberry Pi Pico W / RP2040)
- **Firmware:** C/C++ puro, sem RTOS
- **Comunicação:** UDP broadcast
- **Dashboard:** Python + Tkinter (desktop)

### 2.2 Funcionalidades do Sistema Original

- Terminal de leito com menu de solicitações via joystick
- Display OLED SSD1306 para feedback visual
- Matriz de LEDs WS2812 para sinalização colorida
- Buzzer para alertas sonoros locais
- Detecção de som alto via ADC do microfone
- Envio de mensagens via UDP para dashboard Python/Tkinter
- Dashboard com visualização de eventos e fila de prioridades

### 2.3 Limitações Identificadas

| Limitação | Impacto Real |
|---|---|
| Comunicação UDP sem confirmação de entrega | Eventos críticos podem ser perdidos silenciosamente |
| IP do servidor hardcoded no firmware | Inviabiliza implantação em escala |
| Credenciais Wi-Fi no código-fonte | Risco de segurança em produção |
| Ausência de reconexão automática | Leito fica offline sem notificação |
| Dashboard desktop (Tkinter) | Sem acesso remoto, limitado a um operador |
| Sem armazenamento local | Logs perdidos sem conectividade |
| Sem captura de áudio real | Menu fixo de opções pré-definidas |
| Sem monitoramento ambiental contínuo | Impossível detectar riscos passivamente |
| Sem câmera | Sem visibilidade remota do quarto |
| Sem autenticação | Qualquer dispositivo na rede pode enviar eventos |
| Sem RTOS | Comportamento não determinístico em eventos simultâneos |

---

## 3. Evolução para VitalKare

### 3.1 Comparativo Completo

| Dimensão | Sistema Original | VitalKare |
|---|---|---|
| Plataforma | Raspberry Pi Pico W (RP2040) | KIIRA V2 (ESP32-S3) |
| Firmware | C/C++ sem RTOS | ESP-IDF 5.x + FreeRTOS |
| Comunicação | UDP (sem garantia) | MQTT/TLS + WebSocket + REST |
| Segurança | Nenhuma | TLS, JWT, RBAC, bcrypt |
| Dashboard | Tkinter desktop | React 18 Web (responsivo) |
| Áudio | Detecção ADC básica | Microfone I2S PDM + gravação até 10 s |
| Câmera | Não disponível | OV5640 integrada JPEG/MJPEG |
| Sensores | Microfone analógico | HDC1080 + LDR + MPU6050 |
| Armazenamento | Nenhum | Cartão SD SDMMC + MinIO |
| IA | Nenhuma | Transcrição Whisper (PT) |
| Multidispositivo | Não escalável | Arquitetura distribuída — N leitos |
| Reconexão | Não implementada | Backoff exponencial 2–120 s + jitter |
| Modo Offline | Nenhum | Cache SD + sync posterior |
| Detecção offline | Nenhuma | Heartbeat 30 s, alerta em 90 s |
| OTA | Não disponível | ESP-IDF Secure Boot v2 |

---

## 4. Hardware — Placa KIIRA V2

### 4.1 Descrição

A **KIIRA V2** é uma plataforma de desenvolvimento educacional baseada no **ESP32-S3 (Dual-core Xtensa LX7)**. Integra câmera, microfone digital, display OLED, sensores ambientais, cartão SD, buzzer, relés e outros periféricos em um único PCB, eliminando a necessidade de módulos externos.

### 4.2 Especificações do Microcontrolador

| Parâmetro | Especificação |
|---|---|
| Microcontrolador | ESP32-S3 |
| Núcleos | Dual-core Xtensa LX7 |
| Frequência máxima | 240 MHz |
| Conectividade | Wi-Fi 802.11 b/g/n + Bluetooth 5.0 LE |
| Memória PSRAM | Integrada (necessária para áudio e câmera) |
| Tensão de operação | 3,3 V |
| Interface de programação | USB-C (CDC/JTAG) |
| Aceleração IA | Suporte nativo a operações de inferência (TinyML) |
| Alimentação | USB-C, Vin 9–12 V ou Bateria 18650 |
| Flash | 8 MB (mínimo) — particionada para OTA |

### 4.3 Mapa Completo de Periféricos e GPIOs

| Periférico | Componente | GPIO(s) | Protocolo | Observações |
|---|---|---|---|---|
| Câmera | OV5640 | XCLK:15, SIOD:4, SIOC:5, Y9–Y2:16,17,18,12,10,8,9,11, VSYNC:6, HREF:7, PCLK:13 | Paralelo + I2C | Alta resolução, visão computacional |
| Microfone Digital | PDM MIC I2S | CLK:2, DATA:37 | PDM/I2S | 16 kHz, áudio de qualidade |
| Display OLED | SSD1306 | SDA:14, SCL:21 | I2C1 (400 kHz) | 128×64 pixels |
| Sensor Temp/Umidade | HDC1080 | SDA:14, SCL:21 | I2C0 (100 kHz), end.: 0x40 | Alta precisão |
| Acelerômetro/Giroscópio | MPU6050 | SDA:14, SCL:21 | I2C0 (400 kHz) | 6 eixos, detecção de queda |
| Cartão SD | SDMMC | CLK:39, CMD:38, D0:40 | SDMMC 1-bit (25 MHz) | Logs, áudio, imagens |
| LDR (Luminosidade) | Fotoresistor | GPIO 1 (ADC1_CH0) | ADC 12-bit (0–4095) | Monitoramento ambiental |
| Botão A | Push-button | GPIO 45 | Digital, pull-up interno | Solicitação do paciente |
| Botão B | Push-button | GPIO 46 | Digital, pull-up interno | Emergência |
| Buzzer/LED | PWM | GPIO 3 | PWM 5 kHz, 10-bit | Selecionável por chave |
| Relé 1 | Relé | GPIO 47 | Nível baixo ativo | Acionamento de cargas externas |
| Relé 2 | Relé | GPIO 48 | Nível baixo ativo | Controle de potência |
| Entrada Fotoacoplada | Optoacoplador | GPIO 35 | Digital | Input isolado |

> **Nota crítica de design:** O display SSD1306, o sensor HDC1080 e o MPU6050 compartilham o mesmo barramento I2C (SDA:14, SCL:21). O HDC1080 opera a no máximo 100 kHz, limitando o barramento inteiro a essa velocidade. O gerenciamento de endereços I2C (0x3C OLED / 0x40 HDC1080 / 0x68 MPU6050) é feito por software.

### 4.4 Por que a KIIRA V2 Supera o BitDogLab para este projeto

| Recurso necessário | BitDogLab (RP2040) | KIIRA V2 (ESP32-S3) |
|---|---|---|
| Microfone PDM I2S | Externo | Integrado |
| Câmera | Não disponível | OV5640 integrada |
| Sensor Temp/Umidade | Externo | HDC1080 integrado |
| Acelerômetro | Externo | MPU6050 integrado |
| SD Card | Externo | SDMMC integrado |
| PSRAM para buffers | Não disponível | Integrada |
| Aceleração de IA | Não disponível | Suporte nativo |
| Bluetooth LE | Não disponível | BLE 5.0 integrado |

---

## 5. Arquitetura Completa do Sistema

### 5.1 Visão em Camadas

```
┌─────────────────────────────────────────────────────────────────────┐
│                     CAMADA DE APRESENTAÇÃO                          │
│  Dashboard Web (React 18 + TypeScript + Tailwind + Zustand)         │
│  WebSocket Client │ REST Client (Axios + React Query)               │
└───────────────────────────────┬─────────────────────────────────────┘
                                │ WSS / HTTPS
┌───────────────────────────────▼─────────────────────────────────────┐
│                     CAMADA DE API / GATEWAY                         │
│  FastAPI (Python 3.11+) │ Nginx Reverse Proxy                       │
│  REST /api/v1/* │ WebSocket /ws/v1/dashboard │ Docs /api/docs        │
└──────┬────────────────────────┬───────────────────────┬─────────────┘
       │ SQLAlchemy async        │ aiomqtt               │ Redis
┌──────▼──────┐  ┌──────────────▼──────────┐  ┌────────▼────────────┐
│ PostgreSQL  │  │   MQTT Broker           │  │  Redis              │
│ 16 +        │  │ (Mosquitto / EMQX)      │  │  Cache + Pub/Sub    │
│ TimescaleDB │  │   Porta 8883 (TLS)      │  │  Porta 6379         │
└─────────────┘  └─────────────┬───────────┘  └─────────────────────┘
                 ┌─────────────▼──────────┐
                 │  MinIO (Object Storage) │
                 │  Áudios / Imagens       │
                 │  Porta 9000             │
                 └─────────────┬───────────┘
                                │ MQTT/TLS 8883
┌───────────────────────────────▼─────────────────────────────────────┐
│                     CAMADA EDGE / IoT                               │
│  KIIRA V2 × N (ESP32-S3 + FreeRTOS) — Um por leito hospitalar       │
│  ┌──────────┐ ┌────────────┐ ┌──────────────┐ ┌─────────────────┐  │
│  │ Botão A  │ │ HDC1080    │ │ Microfone    │ │ Câmera OV5640   │  │
│  │ Solicit. │ │ Temp/Umid. │ │ PDM I2S      │ │ JPEG/MJPEG      │  │
│  ├──────────┤ ├────────────┤ ├──────────────┤ ├─────────────────┤  │
│  │ Botão B  │ │ MPU6050    │ │ OLED SSD1306 │ │ SD Card SDMMC   │  │
│  │ Emergênc.│ │ Aceler.    │ │ 128×64 px    │ │ Logs/Cache      │  │
│  └──────────┘ └────────────┘ └──────────────┘ └─────────────────┘  │
└─────────────────────────────────────────────────────────────────────┘
```

### 5.2 Stack Tecnológica Completa

| Camada | Tecnologia | Versão | Justificativa |
|---|---|---|---|
| Firmware MCU | ESP-IDF + FreeRTOS | 5.x | RTOS determinístico, suporte total ao ESP32-S3 |
| Linguagem Firmware | C17 | — | Performance, acesso a hardware de baixo nível |
| Framework Web Backend | FastAPI | 0.111+ | Async nativo, OpenAPI automático |
| Linguagem Backend | Python | 3.11+ | Ecossistema IA, async maduro |
| ORM | SQLAlchemy | 2.0 (async) | Type-safe, async-first |
| Migração DB | Alembic | — | Versionamento de schema |
| Banco de Dados | PostgreSQL + TimescaleDB | 16 | Robusto, JSONB, time-series nativo |
| Cache / Filas | Redis | 7 | Sub-milissegundo, pub/sub, TTL |
| Object Storage | MinIO | — | S3-compatível, self-hosted |
| Mensageria IoT | MQTT (Mosquitto/EMQX) + aiomqtt | 3.1.1/5.0 | Pub/sub leve, QoS, LWT |
| Frontend Framework | React 18 + TypeScript | — | Componentização, tipagem forte |
| Estilização | Tailwind CSS | 3 | Utility-first, responsivo |
| Estado Global | Zustand | — | Leve, async-friendly |
| Transcrição IA | Faster-Whisper | — | Inferência otimizada, self-hosted |
| Infra | Docker + Docker Compose | — | Portabilidade, isolamento |
| WebSocket | FastAPI WebSocket + Native WS API | — | Tempo real, push sem polling |

### 5.3 Padrões Arquiteturais Adotados

| Padrão | Onde é Aplicado | Justificativa |
|---|---|---|
| Event-Driven Architecture | Backend: todos os eventos MQTT disparam handlers async | Desacoplamento natural da IoT |
| Repository Pattern | Acesso ao banco via SQLAlchemy encapsulado | Testabilidade e troca de DB |
| Dependency Injection | FastAPI `Depends()` para DB session, Redis, Auth | SOLID — inversão de dependência |
| State Machine | Firmware ESP32: estados BOOT→WIFI→MQTT→OPERATIONAL→OFFLINE | Comportamento previsível |
| Observer | Zustand stores notificando componentes via WebSocket push | Tempo real sem polling |
| Layered Architecture | Firmware: HAL → Drivers → Components → Tasks → main | Portabilidade |
| Offline-First | Firmware: SD cache → sync ao reconectar | Resiliência crítica hospitalar |
| CQRS lite | Separação entre rotas de leitura (GET) e escrita (POST/PATCH) | Clareza e performance |

---

## 6. Estrutura de Pastas do Repositório

```
VitalKare/                              ← Raiz do monorepo
│
├── README.md                           ← Visão geral e quick start
├── CONTRIBUTING.md                     ← Guia de contribuição
├── dashboard-simulation.html           ← Simulação do dashboard (sem servidor)
├── docs/
│   ├── ESPECIFICACOES_COMPLETAS.md         ← Este documento
│   ├── VitalKare_Especificacao_Tecnica.md  ← Especificação técnica detalhada
│   ├── Mapeamento.md                       ← Master plan de fases e tarefas
│   └── checkpoint.md                       ← Estado atual por sessão de desenvolvimento
│
├── firmware/                           ← ESP-IDF — KIIRA V2 (ESP32-S3)
│   ├── CMakeLists.txt                  ← Build root do ESP-IDF
│   ├── partitions.csv                  ← Mapa de partições: NVS, factory, ota_0, ota_1, spiffs
│   ├── sdkconfig                       ← Configuração gerada pelo menuconfig
│   ├── sdkconfig.defaults              ← Defaults do projeto
│   │
│   ├── config/                         ← Configurações centralizadas
│   │   ├── app_config.h                ← Identidade, intervalos, timeouts, stacks de tasks
│   │   ├── device_config.h             ← Wi-Fi SSID/PASS, MQTT HOST/PORT, DEVICE_ID, WARD_ID
│   │   ├── hardware_config.h           ← GPIOs, protocolos, frequências de todos os periféricos
│   │   └── mqtt_config.h               ← Tópicos MQTT, QoS, parâmetros de conexão
│   │
│   ├── main/                           ← Ponto de entrada do firmware
│   │   ├── CMakeLists.txt              ← Registra todos os .c de componentes no build
│   │   ├── idf_component.yml           ← Dependência gerenciada: esp32-camera >= 2.0.0
│   │   └── main.c                      ← Orquestrador: init hardware + criação de todas as tasks
│   │
│   ├── components/                     ← Módulos FreeRTOS independentes
│   │   ├── core/                       ← Máquina de estados global do sistema
│   │   │   ├── core.c / core.h         ← sys_state_t: BOOT, WIFI_CONNECTING, OPERATIONAL, EMERGENCY...
│   │   │   └── system_config.h         ← Constantes e macros globais do sistema
│   │   │
│   │   ├── events/                     ← Event dispatcher central (coração do firmware)
│   │   │   ├── event_dispatcher.c      ← Fila FreeRTOS, registro de handlers, task dispatcher
│   │   │   └── event_dispatcher.h      ← system_event_t, event_type_t, event_post()
│   │   │
│   │   ├── network/                    ← Conectividade Wi-Fi
│   │   │   ├── wifi_manager.c          ← Conexão com backoff exponencial + jitter, fallback NVS
│   │   │   └── wifi_manager.h
│   │   │
│   │   ├── mqtt/                       ← Cliente MQTT sobre Wi-Fi
│   │   │   ├── mqtt_client.c           ← Publish/subscribe, LWT, reconexão, QoS 0/1/2
│   │   │   └── vitalkare_mqtt.h        ← Header renomeado para evitar conflito com esp-mqtt
│   │   │
│   │   ├── http_client/                ← Cliente HTTP REST (alternativa ao MQTT)
│   │   │   ├── vitalkare_http.c        ← Requisições POST/GET ao backend FastAPI
│   │   │   └── vitalkare_http.h
│   │   │
│   │   ├── buttons/                    ← Tratamento de botões físicos
│   │   │   ├── button_handler.c        ← ISR GPIO + debounce 50 ms por software
│   │   │   └── button_handler.h        ← Botão A (solicitação) e Botão B (emergência)
│   │   │
│   │   ├── sensors/                    ← Drivers de sensores ambientais
│   │   │   ├── hdc1080.c / hdc1080.h   ← Driver HDC1080: temperatura + umidade via I2C
│   │   │   ├── mpu6050.c / mpu6050.h   ← Driver MPU6050: acelerômetro 6 eixos via I2C
│   │   │   ├── ldr.c / ldr.h           ← ADC oneshot ESP-IDF 5.x, conversão lux
│   │   │   └── sensor_task.c           ← Task: poll HDC1080 + LDR a cada 30 s; MPU6050 a 10 Hz
│   │   │
│   │   ├── display/                    ← Interface visual no leito
│   │   │   ├── oled_driver.c           ← Driver SSD1306 I2C, font 5×7 embarcada, primitivas
│   │   │   ├── oled_driver.h
│   │   │   ├── ui_renderer.c           ← 7 telas: BOOT / CONNECTING / OPERATIONAL / EMERGENCY /
│   │   │   └── ui_renderer.h           │           RECORDING / OFFLINE / ACK
│   │   │
│   │   ├── audio/                      ← Captura de voz do paciente
│   │   │   ├── audio_capture.c         ← I2S PDM (API nova ESP-IDF 5.x), buffer PSRAM, task notify
│   │   │   └── audio_capture.h         ← Até 10 s, 16 kHz, 16-bit mono
│   │   │
│   │   ├── camera/                     ← Visão remota do quarto
│   │   │   ├── camera_driver.c         ← esp32-camera, OV5640, JPEG VGA, double-buffer PSRAM
│   │   │   └── camera_driver.h         ← Capture sob demanda (cmd MQTT) + modo stream
│   │   │
│   │   ├── storage/                    ← Persistência local
│   │   │   ├── sd_manager.c            ← SDMMC 1-bit, FAT32, criação de diretórios
│   │   │   ├── sd_manager.h
│   │   │   ├── log_writer.c            ← JSON Lines em /sdcard/logs/boot_NNN.jsonl
│   │   │   ├── log_writer.h
│   │   │   ├── offline_cache.c         ← Ring buffer 200 eventos, persistido no SD
│   │   │   └── offline_cache.h         ← Sync automático ao reconectar
│   │   │
│   │   └── watchdog/                   ← Resiliência do sistema
│   │       ├── watchdog.c              ← esp_task_wdt, heartbeat por task, hard reset em travamento
│   │       └── watchdog.h
│   │
│   ├── tools/
│   │   └── provision.py               ← Script Python: provisiona NVS via esptool (Wi-Fi + MQTT)
│   │
│   └── managed_components/            ← Componentes gerenciados pelo IDF Component Manager
│       ├── espressif__esp_jpeg/        ← Codificação/decodificação JPEG
│       └── espressif__esp32-camera/    ← Driver oficial da câmera OV5640
│
├── backend/                           ← FastAPI — Servidor central
│   ├── Dockerfile
│   ├── requirements.txt
│   ├── alembic.ini
│   └── app/
│       ├── main.py                    ← Ponto de entrada FastAPI, lifespan, CORS, middlewares
│       ├── config.py                  ← Settings via pydantic-settings (lê .env)
│       ├── dependencies.py            ← FastAPI Depends: get_db(), get_redis(), get_current_user()
│       │
│       ├── api/v1/                    ← Contratos HTTP e WebSocket
│       │   ├── __init__.py            ← Registra todos os routers
│       │   ├── websocket.py           ← Endpoint /ws/v1/dashboard — push tempo real
│       │   └── routes/
│       │       ├── auth.py            ← POST /login, POST /refresh, POST /logout
│       │       ├── beds.py            ← CRUD leitos + status atual
│       │       ├── patients.py        ← CRUD pacientes + alocação de leitos
│       │       ├── events.py          ← Listagem, filtragem, ACK de eventos
│       │       ├── audio.py           ← Upload, listagem, download de áudios [Fase 4]
│       │       ├── media.py           ← Imagens, controle de câmera [Fase 5]
│       │       └── metrics.py         ← KPIs, relatórios, exportação CSV [Fase 6]
│       │
│       ├── core/                      ← Lógica de sistema (não é negócio, é infra)
│       │   ├── mqtt_handler.py        ← Subscriber MQTT, roteamento por tópico, dispatch
│       │   ├── event_processor.py     ← Cria eventos no DB, escalona alertas, atualiza leitos
│       │   ├── heartbeat_monitor.py   ← Loop 15 s: detecta leitos offline (threshold: 90 s)
│       │   └── notification.py        ← Broadcast WebSocket a todos os clientes conectados
│       │
│       ├── models/                    ← ORM SQLAlchemy (declarativos, async)
│       │   ├── bed.py                 ← Bed, Ward
│       │   ├── patient.py             ← Patient, BedAssignment
│       │   ├── event.py               ← Event, Acknowledgement
│       │   ├── operator.py            ← Operator (usuário do sistema) com RBAC
│       │   ├── audio_record.py        ← AudioRecord, Transcription
│       │   └── environment_reading.py ← EnvironmentReading (time-series)
│       │
│       ├── schemas/                   ← Pydantic v2 — validação de request/response
│       │   ├── __init__.py
│       │   ├── bed.py                 ← BedCreate, BedRead, BedUpdate, BedStatus
│       │   ├── event.py               ← EventRead, AckCreate, EventFilter
│       │   ├── patient.py             ← PatientCreate, PatientRead
│       │   ├── auth.py                ← TokenResponse, LoginRequest, TokenRefresh
│       │   ├── audio.py               ← AudioUpload, AudioRead, TranscriptionRead
│       │   └── environment.py         ← EnvironmentReading, EnvironmentStats
│       │
│       ├── db/
│       │   ├── session.py             ← Engine async SQLAlchemy + Base declarativa
│       │   └── migrations/            ← Alembic: versionamento do schema do banco
│       │
│       └── utils/
│           └── security.py            ← JWT (encode/decode), bcrypt (hash/verify)
│
├── dashboard/                         ← React 18 + TypeScript + Tailwind
│   ├── Dockerfile
│   ├── index.html
│   ├── nginx.conf
│   ├── package.json
│   ├── tailwind.config.ts
│   ├── tsconfig.json
│   ├── vite.config.ts                 ← Config Vite com proxy para LAN
│   └── src/
│       ├── App.tsx                    ← Roteamento React Router v6, layout global
│       ├── main.tsx                   ← Entry point React, providers
│       ├── index.css                  ← Tailwind directives + tema escuro
│       │
│       ├── pages/                     ← Páginas da SPA
│       │   ├── Dashboard.tsx          ← Grid de leitos + handler WebSocket
│       │   ├── Login.tsx              ← Formulário de autenticação JWT
│       │   ├── BedDetails.tsx         ← Histórico, sensores, câmera, transcrições [Fase 3]
│       │   ├── Events.tsx             ← Fila completa de eventos + filtros [Fase 3]
│       │   └── Reports.tsx            ← Métricas + gráficos + exportação [Fase 6]
│       │
│       ├── components/
│       │   ├── beds/
│       │   │   ├── BedCard.tsx        ← Card individual: status, sensor, alertas, ações
│       │   │   └── BedGrid.tsx        ← Grid responsivo de todos os leitos
│       │   └── alerts/
│       │       └── EmergencyBanner.tsx ← Banner piscante de emergência com fila
│       │
│       ├── hooks/
│       │   └── useWebSocket.ts        ← Hook WS com auto-reconnect e handlers de mensagem
│       │
│       ├── store/                     ← Zustand stores (estado global)
│       │   ├── bedsStore.ts           ← Estado de todos os leitos
│       │   ├── alertsStore.ts         ← Fila de alertas e emergências
│       │   └── authStore.ts           ← Token JWT, operador logado, expiração
│       │
│       └── services/
│           ├── api.ts                 ← Axios com interceptors JWT + refresh automático
│           └── websocketService.ts    ← Singleton WS com reconexão automática
│
├── docker/                            ← Infra containerizada
│   ├── docker-compose.yml             ← Postgres, Redis, MinIO, Mosquitto, Backend, Dashboard
│   └── mosquitto/
│       └── mosquitto.conf             ← Config do broker MQTT (portas, autenticação, logging)
│
└── Images/                            ← Imagens e recursos visuais do projeto
```

---

## 7. Firmware — ESP32-S3 + FreeRTOS

### 7.1 Justificativa do FreeRTOS

O uso de RTOS é mandatório para o VitalKare pelos seguintes motivos:

- **Determinismo temporal:** eventos críticos (emergência, botão) devem ser processados com latência previsível, independente de outras operações em curso
- **Concorrência real:** captura de áudio, leitura de sensores, comunicação MQTT/HTTP e exibição no OLED são tarefas completamente independentes que devem ocorrer simultaneamente
- **Priorização:** tarefas de emergência preemptam tarefas de baixa prioridade sem comprometer a estabilidade
- **Watchdog por task:** o FreeRTOS facilita a implementação de watchdogs por task, detectando travamentos individuais

### 7.2 Tasks FreeRTOS — Tabela Completa

| Task | Prioridade | Stack (bytes) | Função |
|---|---|---|---|
| `task_watchdog` | 6 (máxima) | 4096 | Feed do IWDT, monitora heartbeat das outras tasks |
| `task_network_manager` | 5 | 8192 | Gerencia conexão Wi-Fi, reconexão com backoff |
| `task_event_dispatcher` | 5 | 4096 | Processa fila central de eventos e roteia ações |
| `task_button_handler` | 4 | 2048 | Processa IRQs de botões com debounce por software |
| `task_audio_capture` | 4 | 16384 | Captura áudio PDM (I2S) quando ativada |
| `task_sensor_poll` | 3 | 4096 | Lê HDC1080, LDR a cada 30 s; MPU6050 a 10 Hz |
| `task_camera_stream` | 3 | 32768 | Captura e transmite frames JPEG sob demanda |
| `task_display_update` | 2 | 4096 | Renderiza tela OLED a 2 Hz |
| `task_storage_flush` | 1 (baixa) | 4096 | Flush periódico do buffer de logs para SD |

### 7.3 Máquina de Estados do Firmware

```
  ┌─────┐
  │BOOT │ ← Inicialização de hardware, NVS, partições
  └──┬──┘
     │ Hardware OK
     ▼
┌────────────────┐
│ WIFI_CONNECTING│ ← Tenta conectar com backoff exponencial
└───────┬────────┘
        │ Wi-Fi OK          │ Timeout
        ▼                   ▼
┌───────────────┐   ┌──────────────┐
│MQTT_CONNECTING│   │ OFFLINE_MODE │ ←── ─── ─── ─ ─┐
└───────┬───────┘   └──────┬───────┘                │
        │ MQTT OK           │ Reconexão              │
        ▼                   │                        │
┌───────────────┐           │                        │
│  OPERATIONAL  │ ←─────────┘                        │
└───┬───────┬───┘                                    │
    │       │ Conexão perdida ───────────────────────┘
    │       ▼
    │  ┌──────────┐
    │  │ OFFLINE  │
    │  └──────────┘
    │
    ├── Botão A → RECORDING ── Áudio enviado ──► OPERATIONAL
    │
    └── Botão B → EMERGENCY ── ACK confirmado ─► OPERATIONAL
```

### 7.4 Configuração de Partições da Flash

```
# firmware/partitions.csv
# Name,     Type,   SubType,  Offset,    Size
nvs,         data,  nvs,      0x9000,    0x6000     # Credenciais Wi-Fi, MQTT, device_id
phy_init,    data,  phy,      0xf000,    0x1000     # Calibração PHY do rádio
factory,     app,   factory,  0x10000,   0x300000   # Firmware de recuperação (fallback OTA)
ota_0,       app,   ota_0,    0x310000,  0x300000   # Partição ativa (firmware corrente)
ota_1,       app,   ota_1,    0x610000,  0x300000   # Nova versão durante OTA
spiffs,      data,  spiffs,   0x910000,  0x6F0000   # Sistema de arquivos auxiliar
```

### 7.5 Reconexão com Backoff Exponencial

```
Tentativa 1 → aguarda  2 s + jitter(0–5 s)
Tentativa 2 → aguarda  4 s + jitter(0–5 s)
Tentativa 3 → aguarda  8 s + jitter(0–5 s)
Tentativa 4 → aguarda 16 s + jitter(0–5 s)
Tentativa N → aguarda min(2^N, 120) s + jitter(0–5 s)
```

Durante o modo offline, todos os eventos são armazenados no SD e sincronizados ao restaurar a conexão, em ordem cronológica, com flag `offline_delayed=true` no payload.

### 7.6 Telas do Display OLED SSD1306

| Tela | Quando exibida | Conteúdo |
|---|---|---|
| `UI_SCREEN_BOOT` | Inicialização | Logo VitalKare + versão firmware |
| `UI_SCREEN_CONNECTING` | Conectando Wi-Fi/MQTT | Animação de progresso + SSID |
| `UI_SCREEN_OPERATIONAL` | Normal | Temp/Umidade, ID do paciente, hora NTP |
| `UI_SCREEN_EMERGENCY` | Botão B pressionado | "EMERGÊNCIA" piscante + ID leito |
| `UI_SCREEN_RECORDING` | Gravando áudio | "GRAVANDO..." + barra de progresso |
| `UI_SCREEN_OFFLINE` | Sem conectividade | "OFFLINE" + eventos em cache |
| `UI_SCREEN_ACK` | Atendimento confirmado | "ATENDIDO" + operador + hora |

### 7.7 Provisionamento de Dispositivos

O script `firmware/tools/provision.py` provisiona a partição NVS de cada dispositivo via `esptool`, evitando credenciais hardcoded no binário:

```bash
# Provisionar interativamente
python firmware/tools/provision.py --port COM16

# Gerar binário NVS sem flash (dry-run)
python firmware/tools/provision.py --port COM16 --dry-run
```

O script solicita interativamente: Wi-Fi SSID, Wi-Fi Password, IP do broker MQTT, `device_id` (ex: `leito01`) e `ward_id` (ex: `ala-a`).

---

## 8. Backend — FastAPI + PostgreSQL

### 8.1 Rotas REST

| Método | Rota | Descrição | Auth |
|---|---|---|---|
| POST | `/api/v1/auth/login` | Login + JWT | Pública |
| POST | `/api/v1/auth/refresh` | Refresh token | JWT |
| GET | `/api/v1/beds` | Lista todos os leitos | NURSE+ |
| GET | `/api/v1/beds/{id}` | Detalhes de um leito | NURSE+ |
| PATCH | `/api/v1/beds/{id}` | Atualiza status do leito | NURSE+ |
| GET | `/api/v1/patients` | Lista pacientes | NURSE+ |
| POST | `/api/v1/patients` | Cria paciente | NURSE+ |
| GET | `/api/v1/events` | Lista eventos com filtros | NURSE+ |
| POST | `/api/v1/events/{id}/ack` | Confirma atendimento | NURSE+ |
| GET | `/api/v1/audio/{id}` | Download de áudio | NURSE+ |
| POST | `/api/v1/media/camera/{bed_id}` | Solicita captura câmera | NURSE+ |
| GET | `/api/v1/metrics/summary` | Resumo de KPIs | ADMIN |
| GET | `/health` | Health check | Pública |
| WS | `/ws/v1/dashboard` | Stream tempo real | JWT |

### 8.2 Roles RBAC

| Role | Permissões |
|---|---|
| `ADMIN` | Acesso total: usuários, configuração, relatórios, todos os leitos |
| `NURSE` | Ver e interagir com leitos, ACK de eventos, câmera |
| `DOCTOR` | Leitura completa + ACK de eventos + câmera |
| `VIEWER` | Apenas leitura do dashboard |

### 8.3 Heartbeat Monitor

O serviço `heartbeat_monitor.py` roda em loop com intervalo de 15 s e verifica o timestamp do último `status` recebido de cada leito:

| Threshold | Ação |
|---|---|
| > 90 s sem heartbeat | Leito marcado como `OFFLINE`, evento emitido ao dashboard |
| > 300 s sem heartbeat | Alerta `DEVICE_CRITICAL` — provável falha de hardware |

### 8.4 Fluxo de Processamento MQTT → Dashboard

```
MQTT Broker recebe tópico hospital/ala-a/leito01/emergency
    │
    ▼ mqtt_handler.py
_dispatch(topic, payload)
    │
    ▼ event_processor.py
process_emergency(payload, topic)
    ├── INSERT events (priority=1, status=PENDING) → PostgreSQL
    ├── UPDATE beds SET status=EMERGENCY → PostgreSQL
    ├── alert_service.escalate_emergency(event)
    └── notification.broadcast_emergency(event)
        │
        ▼ WebSocket (wss://)
Dashboard React
    ├── useWebSocket.handleMessage(type="emergency")
    ├── alertsStore.addAlert()
    ├── bedsStore.updateBedStatus(bedId, "EMERGENCY")
    ├── EmergencyBanner visível (animação piscante)
    └── Audio alert (Web Audio API)
```

---

## 9. Dashboard — React 18 + TypeScript

### 9.1 Páginas da Aplicação

| Rota | Página | Descrição |
|---|---|---|
| `/login` | Login | Autenticação do operador com JWT |
| `/` | Dashboard Principal | Grid de todos os leitos em tempo real |
| `/bed/:id` | Detalhes do Leito | Histórico, sensores, câmera, transcrições |
| `/events` | Central de Eventos | Fila completa com filtros e ACK |
| `/reports` | Relatórios | Métricas, gráficos e exportação CSV |
| `/settings` | Configurações | Gestão de usuários e leitos |

### 9.2 Componentes Principais

**BedCard** — Card individual de leito exibindo:
- Status visual colorido (LIVRE / OCUPADO / EMERGÊNCIA / OFFLINE / GRAVANDO)
- Temperatura e umidade em tempo real
- Último evento recebido
- Botões de ação: ACK, Câmera, Detalhes
- Indicador de sinal Wi-Fi (RSSI)
- Sparkline de temperatura

**BedGrid** — Grid responsivo que agrupa os BedCards por ala, com filtros de status e ordenação por urgência.

**EmergencyBanner** — Banner persistente piscante no topo da tela para emergências não atendidas, com fila e priorização automática.

### 9.3 Mensagens WebSocket (Backend → Dashboard)

| Tipo | Payload | Ação no Dashboard |
|---|---|---|
| `emergency` | `{bed_id, patient_id, timestamp}` | Banner + alerta sonoro + mover ao topo |
| `bed_update` | `{bed_id, status, ...}` | Atualiza BedCard |
| `environment` | `{bed_id, temp, humidity, lux}` | Atualiza sensores no card |
| `audio_transcription` | `{bed_id, text, confidence}` | Toast + card de transcrição |
| `bed_offline` | `{bed_id, last_seen}` | Card cinza + badge OFFLINE |
| `bed_online` | `{bed_id}` | Restaura card ao estado normal |
| `ack_confirmed` | `{event_id, ack_by}` | Remove da fila de alertas |

---

## 10. Infraestrutura — Docker Compose

### 10.1 Serviços

| Serviço | Imagem | Porta | Função |
|---|---|---|---|
| `postgres` | timescale/timescaledb:2.x-pg16 | 5432 | Banco de dados principal |
| `redis` | redis:7-alpine | 6379 | Cache, sessões, rate limiting |
| `minio` | minio/minio | 9000 / 9001 | Object storage (áudios, imagens) |
| `mosquitto` | eclipse-mosquitto:2 | 1883 / 8883 | MQTT Broker (dev/TLS) |
| `backend` | (build local) | 8000 | FastAPI + serviços Python |
| `dashboard` | (build local) | 80 | React + Nginx |

### 10.2 Comandos de Operação

```bash
# Subir todos os serviços
cd docker
cp .env.example .env   # Edite com senhas reais
docker compose up -d

# Ver logs
docker compose logs -f backend

# Parar
docker compose down

# Parar e remover volumes (DESTRUTIVO — apaga banco)
docker compose down -v
```

---

## 11. Protocolo MQTT — Tópicos e Payloads

### 11.1 Padrão de Tópicos

```
hospital/{ward_id}/{device_id}/{subtopic}
```

Exemplo: `hospital/ala-a/leito01/emergency`

### 11.2 Tópicos Publicados pelo Dispositivo

| Subtópico | QoS | Retain | Frequência | Descrição |
|---|---|---|---|---|
| `status` | 1 | Sim | 30 s | Heartbeat, Wi-Fi RSSI, heap livre, versão firmware |
| `emergency` | 2 | Não | Evento | Botão B — prioridade máxima, entrega garantida |
| `request` | 1 | Não | Evento | Botão A — solicitação do paciente |
| `audio` | 1 | Não | Evento | Payload de áudio base64 (comprimido ADPCM) |
| `environment` | 1 | Sim | 30 s | Temperatura, umidade, luminosidade, movimento |
| `image` | 1 | Não | Sob demanda | Frame JPEG base64 da câmera OV5640 |
| `log` | 0 | Não | Contínuo | Logs do sistema (best-effort) |

### 11.3 Tópicos Publicados pelo Backend (Comandos ao Leito)

| Subtópico | QoS | Descrição |
|---|---|---|
| `cmd/camera` | 1 | Solicita captura de imagem ou stream MJPEG |
| `cmd/ack` | 1 | Confirmação de atendimento para exibir no OLED |
| `cmd/display` | 1 | Atualiza dados exibidos no OLED remotamente |
| `cmd/buzzer` | 1 | Ativa/desativa buzzer remotamente |
| `hospital/broadcast/cmd` | 1 | Comando para todos os leitos simultaneamente |

### 11.4 Last Will Testament (LWT)

Cada dispositivo registra um LWT ao conectar ao broker. Em caso de desconexão abrupta, o broker publica automaticamente:

```json
{
  "device_id": "leito01",
  "ward": "ala-a",
  "online": false,
  "reason": "unexpected_disconnect",
  "timestamp": null
}
```

O backend detecta esse LWT e atualiza o status do leito para `OFFLINE`.

### 11.5 Parâmetros MQTT

| Parâmetro | Valor |
|---|---|
| Protocolo | MQTT 3.1.1 / MQTT 5.0 |
| Porta TLS | 8883 |
| Porta dev (sem TLS) | 1883 |
| Keep-alive | 60 s |
| Clean Session | false (sessão persistente) |
| QoS Emergência | 2 (exactly once) |
| QoS Heartbeat/Sensores | 1 (at least once) |
| QoS Logs | 0 (best effort) |

---

## 12. Banco de Dados — Schema Completo

### 12.1 Tabelas Principais

```sql
-- Extensões
CREATE EXTENSION IF NOT EXISTS "uuid-ossp";
CREATE EXTENSION IF NOT EXISTS timescaledb;

-- Alas hospitalares
CREATE TABLE wards (
    id         UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    name       VARCHAR(100) NOT NULL UNIQUE,   -- ex: "Ala A", "UTI"
    floor      INT NOT NULL DEFAULT 1,
    capacity   INT NOT NULL DEFAULT 10,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- Leitos
CREATE TABLE beds (
    id               UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    ward_id          UUID NOT NULL REFERENCES wards(id),
    bed_number       VARCHAR(20) NOT NULL UNIQUE,  -- ex: "leito01"
    device_id        VARCHAR(50) NOT NULL UNIQUE,  -- device_id NVS do ESP32
    status           VARCHAR(20) NOT NULL DEFAULT 'FREE',
    -- Status: FREE | OCCUPIED | EMERGENCY | RECORDING | OFFLINE
    last_heartbeat   TIMESTAMPTZ,
    firmware_version VARCHAR(20),
    metadata         JSONB DEFAULT '{}',
    created_at       TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
CREATE INDEX idx_beds_ward_id   ON beds(ward_id);
CREATE INDEX idx_beds_status    ON beds(status);
CREATE INDEX idx_beds_device_id ON beds(device_id);

-- Pacientes
CREATE TABLE patients (
    id          UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    name        VARCHAR(200) NOT NULL,
    patient_code VARCHAR(50) UNIQUE,   -- ex: "PAC-2024-0042"
    bed_id      UUID REFERENCES beds(id),
    admitted_at TIMESTAMPTZ,
    discharged_at TIMESTAMPTZ,
    metadata    JSONB DEFAULT '{}'
);

-- Eventos
CREATE TABLE events (
    id           UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    bed_id       UUID NOT NULL REFERENCES beds(id),
    event_type   VARCHAR(20) NOT NULL,
    -- Tipos: EMERGENCY | AUDIO_REQUEST | ENVIRONMENT | STATUS | SYSTEM
    priority     INT NOT NULL DEFAULT 5,  -- 1=crítico, 5=baixo
    status       VARCHAR(20) NOT NULL DEFAULT 'PENDING',
    -- Status: PENDING | ACKNOWLEDGED | RESOLVED | EXPIRED
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

-- Leituras ambientais (time-series com TimescaleDB)
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

-- Registros de áudio e transcrições
CREATE TABLE audio_records (
    id               UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    event_id         UUID REFERENCES events(id),
    bed_id           UUID NOT NULL REFERENCES beds(id),
    patient_id       UUID REFERENCES patients(id),
    duration_seconds FLOAT,
    sample_rate      INT DEFAULT 16000,
    encoding         VARCHAR(20) DEFAULT 'adpcm',
    storage_path     VARCHAR(500),   -- caminho no MinIO/S3
    transcription    TEXT,
    confidence       FLOAT,
    language         VARCHAR(10) DEFAULT 'pt',
    created_at       TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- Operadores do sistema
CREATE TABLE operators (
    id           UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    username     VARCHAR(100) UNIQUE NOT NULL,
    password_hash VARCHAR(255) NOT NULL,  -- bcrypt cost 12
    full_name    VARCHAR(200),
    role         VARCHAR(20) NOT NULL DEFAULT 'VIEWER',
    -- Roles: ADMIN | NURSE | DOCTOR | VIEWER
    is_active    BOOLEAN DEFAULT TRUE,
    created_at   TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    last_login   TIMESTAMPTZ
);
```

---

## 13. Transcrição de Áudio com IA

### 13.1 Fluxo Completo

```
Microfone PDM (16 kHz, 16-bit, mono)
    │ I2S CLK:2 / DATA:37
    ▼
Buffer DMA na PSRAM (até 10 s = ~320 KB PCM bruto)
    │
    ▼ Compressão ADPCM (4:1)
Arquivo WAV local no SD (/sdcard/audio/YYYYMMDD_HHMMSS_leito01.wav)
    │
    ▼ MQTT QoS 1 (payload base64, ~80 KB comprimido)
Backend audio_service.py
    ├── Salva arquivo original → MinIO
    └── Envia para Faster-Whisper
        │
        ▼
Faster-Whisper (modelo small-pt, self-hosted)
    └── Retorna texto + confiança + idioma
        │
        ▼
PostgreSQL (tabela audio_records)
    │
    ▼ WebSocket push
Dashboard: "💬 Leito 01 — João Silva: 'Preciso de água, por favor.'"
```

### 13.2 Comparativo de Engines de Transcrição

| Engine | Tipo | Latência | Custo | Privacidade | Recomendação |
|---|---|---|---|---|---|
| **Faster-Whisper** | Self-hosted | 1–3 s (CPU) / <1 s (GPU) | Zero | Total | ✅ Recomendado |
| OpenAI Whisper API | Cloud | 1–2 s | ~$0.006/min | Dados externos | Apenas dev |
| Vosk | Self-hosted offline | <1 s | Zero | Total | Alternativa leve |
| Azure Speech | Cloud | 0,5–1 s | Pago | LGPD compliance | Corporativo |

**Justificativa:** Faster-Whisper local garante conformidade com a **LGPD** — nenhum dado de paciente sai da rede hospitalar.

### 13.3 Parâmetros de Captura

| Parâmetro | Valor |
|---|---|
| Taxa de amostragem | 16.000 Hz |
| Profundidade de bits | 16 bits |
| Canais | Mono |
| Duração máxima | 10 s por gravação |
| Formato local (SD) | WAV PCM |
| Formato de transmissão | ADPCM (4:1) ou Opus |
| Tamanho máximo payload MQTT | ~80 KB (10 s ADPCM) |

---

## 14. Segurança

### 14.1 Mecanismos por Camada

| Camada | Mecanismo | Detalhe |
|---|---|---|
| Firmware (credenciais) | NVS criptografada (Flash Encryption) | `CONFIG_FLASH_ENCRYPTION_ENABLED` no sdkconfig |
| Firmware (OTA) | Secure Boot v2 | Binário assinado com chave privada, rollback automático |
| MQTT (transporte) | TLS 1.3 (porta 8883) | Mosquitto + certificado CA interno da rede hospitalar |
| MQTT (autenticação) | Usuário + senha por dispositivo | Credenciais provisionadas via NVS |
| API REST | JWT HS256 (dev) / RS256 (prod) + HTTPS | `python-jose`, expiração configurável |
| WebSocket | WSS + token validado no handshake | Header `Authorization: Bearer <token>` |
| RBAC | Roles por rota: `require_role(["NURSE","ADMIN"])` | FastAPI Dependency |
| Senhas de operadores | bcrypt (cost factor 12) | `passlib[bcrypt]` |
| Rate Limiting | Redis sliding window (100 req/min por IP) | `slowapi` |
| CORS | Whitelist estrita em produção | `settings.cors_origins` — sem wildcard `*` |
| Validação MQTT | Pydantic v2 em todos os payloads recebidos | Proteção contra injeção de dados via broker |

---

## 15. Tolerância a Falhas e Modo Offline

### 15.1 Cenários e Comportamentos

| Falha | Comportamento do Firmware | Recuperação |
|---|---|---|
| Wi-Fi perdido | Modo OFFLINE, cache SD, OLED mostra "OFFLINE", bip a cada 60 s | Backoff exponencial 2–120 s + jitter |
| MQTT caiu (Wi-Fi OK) | Idem modo offline, continua tentando reconexão | Reconnect loop |
| SD Card falho | Log apenas em RAM (circular buffer), operação degradada | Driver com retry 3x |
| Sensor falho | Envia `null` no campo, gera evento SYSTEM no backend | Driver com retry 3x, alerta ao operador |
| Watchdog trigger | Hard reset do ESP32-S3, NVS preservada | Boot automático, reins das tasks |
| Backend caindo | Broker continua, mensagens persistidas na fila MQTT | Consome fila ao reiniciar |
| DB indisponível | Backend retorna 503, buffer temporário em Redis | Health check + retry automático |

### 15.2 Cache Offline (Ring Buffer SD)

O componente `offline_cache.c` mantém um ring buffer de 200 eventos na memória RAM e persiste no SD ao fazer flush. Ao reconectar:

1. `on_mqtt_connected()` verifica `offline_cache_pending_count()`
2. Se há eventos pendentes, posta `EVT_OFFLINE_SYNC_NEEDED` na fila
3. O `task_event_dispatcher` republica todos os eventos em ordem cronológica
4. Cada evento recebe a flag `"offline_delayed": true` no payload

---

## 16. Fluxos de Operação End-to-End

### 16.1 Fluxo de Emergência (Botão B)

```
1. Paciente pressiona Botão B (GPIO 46)
2. ISR GPIO_IRQ_EDGE_FALL → debounce 50 ms
3. task_button_handler posta EVT_EMERGENCY na fila interna
4. task_event_dispatcher recebe evento:
   ├── core_set_state(SYS_STATE_EMERGENCY)
   ├── ui_set_screen(UI_SCREEN_EMERGENCY) — OLED piscante
   ├── Buzzer PWM 800 Hz × 3 ciclos
   ├── log_write(CRITICAL, "Emergência button B")
   └── vitalkare_mqtt_publish_emergency("BUTTON_B") — QoS 2
5. MQTT Broker entrega garantida ao Backend
6. mqtt_handler.py → event_processor.process_emergency()
   ├── INSERT events (priority=1, status=PENDING) no PostgreSQL
   ├── UPDATE beds SET status=EMERGENCY
   └── notification.broadcast_emergency(event)
7. WebSocket push para todos os clientes conectados
8. Dashboard React:
   ├── EmergencyBanner visível e piscante
   ├── Alerta sonoro (Web Audio API)
   └── Leito movido ao topo da fila
```

### 16.2 Fluxo de Gravação de Áudio (Botão A)

```
1. Paciente mantém Botão A pressionado (GPIO 45) por 1 s
2. Firmware exibe UI_SCREEN_RECORDING no OLED
3. task_audio_capture inicia captura I2S PDM (16 kHz, 16-bit)
4. Buffer preenchido na PSRAM (até 10 s ou botão liberado)
5. Firmware salva WAV no SD /sdcard/audio/...
6. Comprime em ADPCM → publica via MQTT QoS 1 (base64)
7. Backend audio_service.py:
   ├── Salva arquivo original no MinIO
   └── Envia para Faster-Whisper (modelo small-pt)
8. Whisper retorna: {"text": "Preciso de água, por favor.", confidence: 0.94}
9. Backend salva transcrição no PostgreSQL (tabela audio_records)
10. WebSocket push:
    Dashboard exibe: "💬 Leito 01 — João Silva: 'Preciso de água, por favor.'"
```

### 16.3 Fluxo de Monitoramento Ambiental

```
1. task_sensor_poll (a cada 30 s):
   ├── hdc1080_read() → temperatura + umidade
   ├── ldr_read_lux() → luminosidade
   └── mpu6050_read() → aceleração (detecção de queda)
2. Monta payload JSON de ambiente
3. Publica hospital/{ward}/{id}/environment — QoS 1
4. Backend persiste em environment_readings (TimescaleDB)
5. WebSocket push atualiza sensores no BedCard do dashboard
```

### 16.4 Fluxo de Câmera Sob Demanda

```
1. Operador clica em "Ver Câmera" no BedCard do dashboard
2. Dashboard POST /api/v1/media/camera/{bed_id}
3. Backend publica hospital/{ward}/{id}/cmd/camera — QoS 1
4. Firmware task_camera_stream recebe comando:
   ├── camera_driver_capture_jpeg()
   └── Publica frame JPEG base64 em hospital/{ward}/{id}/image
5. Backend media_service salva imagem no MinIO
6. WebSocket push com URL da imagem ao dashboard
7. Dashboard exibe modal com foto do quarto
```

---

## 17. Roadmap e Estado Atual

### 17.1 Estado Atual por Componente (Maio 2026)

| Componente | Status | Completude |
|---|---|---|
| Firmware — Estrutura base (tasks, states, events) | ✅ Completo | 100% |
| Firmware — Wi-Fi Manager + NVS provisioning | ✅ Completo | 100% |
| Firmware — Sensores: HDC1080, MPU6050, LDR | ✅ Completo | 100% |
| Firmware — Display OLED SSD1306 + UI Renderer | ✅ Completo | 100% |
| Firmware — Captura de áudio PDM (I2S, PSRAM) | ✅ Completo | 100% |
| Firmware — Câmera OV5640 JPEG (esp32-camera) | ✅ Completo | 100% |
| Firmware — SD Card + Log + Cache Offline | ✅ Completo | 100% |
| Firmware — Watchdog + Botões GPIO | ✅ Completo | 100% |
| Firmware — HTTP Client (alternativa ao MQTT) | ✅ Completo | 100% |
| Backend — FastAPI + JWT + WebSocket | ✅ Completo | 100% |
| Backend — Schemas Pydantic v2 | ✅ Completo | 100% |
| Backend — Modelos SQLAlchemy | ✅ Completo | 100% |
| Backend — Alembic migrations | ✅ Completo | 100% |
| Backend — MQTT handler + event processor | ✅ Completo | 100% |
| Backend — Heartbeat monitor | ✅ Completo | 100% |
| Backend — Serviço de transcrição Whisper | ⏳ Pendente | 0% |
| Backend — Rotas de áudio e mídia | ⏳ Pendente | 0% |
| Backend — Relatórios e métricas | ⏳ Pendente | 0% |
| Dashboard — React 18 + Zustand + Tailwind | ✅ Completo | 100% |
| Dashboard — BedCard + BedGrid + WebSocket | ✅ Completo | 100% |
| Dashboard — EmergencyBanner | ✅ Completo | 100% |
| Dashboard — Página de detalhes do leito | ⏳ Pendente | 0% |
| Dashboard — Central de eventos | ⏳ Pendente | 0% |
| Dashboard — Relatórios | ⏳ Pendente | 0% |
| Infra — Docker Compose completo | ✅ Completo | 100% |

### 17.2 Fases de Desenvolvimento

| Fase | Período | Critério de Aceitação | Status |
|---|---|---|---|
| **Fase 0** — Fundações | Semanas 1–2 | Banco migrado, schemas Pydantic, Docker rodando | ✅ Concluída |
| **Fase 1** — MVP | Semanas 3–6 | Botão emergência → dashboard em < 2 s | 🔄 Em andamento |
| **Fase 2** — Sensores | Semanas 7–10 | Temp/umidade em tempo real no dashboard | ⏳ Pendente |
| **Fase 3** — Display/UX | Semanas 11–12 | OLED correto em todos os estados do sistema | ⏳ Pendente |
| **Fase 4** — Áudio/Transcrição | Semanas 13–18 | Paciente fala → texto no dashboard em < 5 s | ⏳ Pendente |
| **Fase 5** — Câmera/Stream | Semanas 19–25 | Operador solicita → câmera em < 3 s | ⏳ Pendente |
| **Fase 6** — Relatórios | Semanas 26–30 | Gestão extrai relatório de turno em < 30 s | ⏳ Pendente |
| **Fase 7** — TinyML | Semanas 31–42 | Queda detectada → evento P1 sem ação do paciente | ⏳ Futuro |

### 17.3 Riscos Técnicos Mapeados

| Risco | Probabilidade | Impacto | Mitigação |
|---|---|---|---|
| Payload de áudio MQTT > limite do broker | Alta | Alto | Fragmentação em chunks de 32 KB |
| OV5640 conflito de barramento I2C/paralelo | Média | Alto | Interface paralela para dados; I2C apenas para config |
| Faster-Whisper lento em CPU sem GPU | Alta | Médio | Modelo `tiny` ou `base` para latência < 3 s |
| TimescaleDB incompatível com Alembic | Média | Médio | `create_hypertable` em raw SQL na migration |
| Memória insuficiente para áudio + câmera simultâneos | Alta | Alto | Camera suspende áudio durante stream (prioridade) |
| PSRAM necessária para buffer de câmera | Certa | Alto | `CAMERA_FB_IN_PSRAM` — KIIRA V2 tem PSRAM integrada |

---

## 18. Variáveis de Ambiente

### 18.1 Backend (`backend/.env`)

```bash
# Banco de dados
DATABASE_URL=postgresql+asyncpg://vitalkare:SENHA@postgres:5432/vitalkare

# Redis
REDIS_URL=redis://redis:6379/0

# MinIO (Object Storage)
MINIO_ENDPOINT=minio:9000
MINIO_ACCESS_KEY=minioadmin
MINIO_SECRET_KEY=SENHA_MINIO
MINIO_BUCKET_AUDIO=vitalkare-audio
MINIO_BUCKET_IMAGES=vitalkare-images

# MQTT Broker
MQTT_BROKER_HOST=mosquitto
MQTT_BROKER_PORT=1883
MQTT_USERNAME=vitalkare_backend
MQTT_PASSWORD=SENHA_MQTT

# Segurança JWT
SECRET_KEY=GERE_UMA_CHAVE_SEGURA_DE_32_BYTES_MIN
ALGORITHM=HS256
ACCESS_TOKEN_EXPIRE_MINUTES=30
REFRESH_TOKEN_EXPIRE_DAYS=7

# CORS
CORS_ORIGINS=["http://localhost:5173","http://localhost:80"]

# Transcrição
WHISPER_MODEL=small
WHISPER_LANGUAGE=pt
```

### 18.2 Dashboard (`dashboard/.env.local`)

```bash
VITE_BACKEND_HOST=localhost       # IP do servidor na LAN (ex: 192.168.1.100)
VITE_BACKEND_PORT=8000
VITE_WS_PROTOCOL=ws              # ws para dev, wss para produção
```

### 18.3 Firmware (`firmware/config/device_config.h`)

```c
#define DEVICE_WIFI_SSID    "NomeDaRedeHospitalar"
#define DEVICE_WIFI_PASS    "SenhaWifi"
#define DEVICE_MQTT_HOST    "192.168.1.100"   // IP do servidor na LAN
#define DEVICE_MQTT_PORT    1883
#define DEVICE_ID           "leito01"
#define DEVICE_WARD_ID      "ala-a"
```

> **Atenção:** Em produção, use o script de provisionamento NVS (`firmware/tools/provision.py`) — nunca compile credenciais reais no binário.

---

## 19. Glossário Técnico

| Termo | Definição |
|---|---|
| **KIIRA V2** | Placa de desenvolvimento ESP32-S3 com periféricos integrados (câmera, microfone, sensores, SD) |
| **ESP32-S3** | Microcontrolador Dual-core Xtensa LX7 240 MHz com Wi-Fi, BLE e aceleração de IA da Espressif |
| **ESP-IDF** | Espressif IoT Development Framework — SDK oficial para programação do ESP32 em C |
| **FreeRTOS** | Real-Time Operating System utilizado no ESP32 para gerenciamento de tasks e concorrência |
| **MQTT** | Message Queuing Telemetry Transport — protocolo de mensageria leve para IoT com pub/sub |
| **QoS** | Quality of Service — nível de garantia de entrega do MQTT: 0 (best effort), 1 (at least once), 2 (exactly once) |
| **LWT** | Last Will Testament — mensagem automática publicada pelo broker quando um dispositivo desconecta abruptamente |
| **PDM** | Pulse Density Modulation — formato de sinal do microfone digital, convertido para PCM via I2S |
| **I2S** | Inter-IC Sound — protocolo serial para áudio digital entre MCU e periféricos de som |
| **PSRAM** | Pseudo-Static RAM — memória RAM externa ao chip, necessária para buffers de câmera e áudio |
| **SDMMC** | Interface de acesso a cartão SD via protocolo MMC (mais rápido que SPI) |
| **HDC1080** | Sensor de temperatura e umidade de alta precisão da Texas Instruments, interface I2C |
| **MPU6050** | IMU (Inertial Measurement Unit) de 6 eixos (acelerômetro + giroscópio) da InvenSense, interface I2C |
| **OV5640** | Sensor de câmera CMOS 5 MP da OmniVision, usado na KIIRA V2 com interface paralela + I2C |
| **SSD1306** | Controlador de display OLED 128×64 pixels monocromático, interface I2C ou SPI |
| **NVS** | Non-Volatile Storage — partição de flash do ESP32 para armazenar configurações que persistem entre resets |
| **OTA** | Over-The-Air — atualização de firmware remota sem acesso físico ao dispositivo |
| **FastAPI** | Framework web assíncrono Python de alta performance com documentação OpenAPI automática |
| **SQLAlchemy** | ORM Python com suporte a queries assíncronas e múltiplos bancos de dados |
| **Alembic** | Ferramenta de migração de schema de banco de dados para SQLAlchemy |
| **TimescaleDB** | Extensão PostgreSQL para séries temporais (time-series) com hipertabelas automáticas |
| **Faster-Whisper** | Implementação otimizada do modelo Whisper da OpenAI para transcrição de fala para texto |
| **MinIO** | Object Storage S3-compatível self-hosted para armazenamento de arquivos binários |
| **Zustand** | Biblioteca de gerenciamento de estado global leve para React |
| **JWT** | JSON Web Token — padrão de autenticação stateless com payload assinado |
| **RBAC** | Role-Based Access Control — controle de acesso baseado em papéis/funções |
| **LGPD** | Lei Geral de Proteção de Dados — legislação brasileira de privacidade de dados |
| **ADPCM** | Adaptive Differential Pulse Code Modulation — algoritmo de compressão de áudio lossless 4:1 |
| **backoff exponencial** | Estratégia de reconexão que dobra o tempo de espera a cada falha, evitando sobrecarga do servidor |
| **WDT / IWDT** | Watchdog Timer / Interrupt Watchdog Timer — circuito de segurança que reseta o MCU em caso de travamento |
