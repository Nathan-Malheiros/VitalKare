# VitalKare — Firmware Architecture Review & Migration Plan

> **Tipo:** Revisão Arquitetural de Sistema Embarcado — Nível de Produção  
> **Plataforma:** KIIRA V2 · ESP32-S3 · ESP-IDF 5.x · FreeRTOS  
> **Data:** Maio de 2026  
> **Versão do Documento:** 1.0.0

---

## Índice

1. [Firmware Architecture Assessment](#1-firmware-architecture-assessment)
2. [Critical Problems Found](#2-critical-problems-found)
3. [Required Refactors](#3-required-refactors)
4. [Proposed TCP Architecture](#4-proposed-tcp-architecture)
5. [Proposed Packet Protocol](#5-proposed-packet-protocol)
6. [FreeRTOS Task Topology](#6-freertos-task-topology)
7. [Event Flow Redesign](#7-event-flow-redesign)
8. [Audio Pipeline Redesign](#8-audio-pipeline-redesign)
9. [State Machine Redesign](#9-state-machine-redesign)
10. [Memory & Performance Analysis](#10-memory--performance-analysis)
11. [Recommended File/Module Changes](#11-recommended-filemodule-changes)
12. [Migration Roadmap](#12-migration-roadmap)
13. [Risks & Engineering Tradeoffs](#13-risks--engineering-tradeoffs)
14. [Production-Readiness Recommendations](#14-production-readiness-recommendations)

---

## 1. Firmware Architecture Assessment

### 1.1 What Was Built — Summary

A análise do código atual revela uma arquitetura de base sólida: o dispatcher de eventos (`event_dispatcher.c`) é corretamente implementado com ISR-safe `xQueueSendFromISR`, os drivers de hardware (SSD1306, HDC1080, MPU6050, LDR, SD) são modulares e bem isolados, e a máquina de estados global em `core.h/c` define os estados necessários. O `wifi_manager.c` com backoff exponencial está estruturalmente correto.

No entanto, a camada de aplicação — o plano entre hardware e rede — contém falhas de concorrência e design que **tornam o sistema incapaz de atender os requisitos reais de operação hospitalar** conforme especificados. Estas não são falhas menores: são falhas estruturais na pipeline de áudio e no modelo de comunicação.

### 1.2 Architecture Quality Matrix

| Módulo | Qualidade Estrutural | Qualidade Comportamental | Nota |
|---|---|---|---|
| `event_dispatcher.c/h` | ✅ Boa | ⚠️ Parcial | Fila FIFO ignora prioridade |
| `core.c/h` | ✅ Boa | ⚠️ Parcial | Estado RECORDING nunca é entrado |
| `wifi_manager.c` | ✅ Boa | ❌ Falha crítica | `vTaskDelay` no event handler |
| `button_handler.c` | ✅ Boa | ❌ Falha crítica | Sem detecção de release |
| `audio_capture.c` | ⚠️ Parcial | ❌ Falha crítica | Gravação fixa, não edge-triggered |
| `vitalkare_http.c` | ⚠️ Parcial | ❌ Falha crítica | Síncrono + bloqueante |
| `watchdog.c` | ✅ Boa | ✅ Boa | Funcionamento correto |
| `sensor_task.c` | ✅ Boa | ✅ Boa | Polling periódico adequado |
| `oled_driver.c/ui_renderer.c` | ✅ Boa | ✅ Boa | Estrutura sólida |
| `sd_manager.c / offline_cache.c` | ✅ Boa | ✅ Boa | Funcional |

### 1.3 Connectivity Model Assessment

O modelo atual é **REST/HTTP request-response síncrono**. Cada evento gera uma nova conexão TCP, uma requisição bloqueante, e encerra a conexão. Isso é aceitável para telemetria não-crítica de baixa frequência, mas é completamente inadequado para:

- Upload de áudio em tempo real (até 320 KB por gravação)
- Recebimento de comandos do backend (polling necessário)
- Eventos de emergência com garantia de entrega e baixa latência
- Feedback de ACK bidirecional

A migração para TCP persistente com protocolo binário framing é a abordagem correta e será detalhada na Seção 4.

---

## 2. Critical Problems Found

### PROBLEMA #1 — CRÍTICO: Button Handler sem detecção de Release

**Arquivo:** `firmware/components/buttons/button_handler.c`

```c
// CÓDIGO ATUAL — FALHA CRÍTICA
gpio_config_t cfg = {
    .intr_type = GPIO_INTR_NEGEDGE,  // Apenas borda de descida (press)
    // ...
};
```

**Diagnóstico:**
O GPIO é configurado para interrupção apenas em borda de descida (`NEGEDGE`). O sistema só detecta o ato de pressionar o botão, nunca o de soltar. Isso torna **impossível** implementar o requisito central:

> "Recording STOPS when button is released OR when recording reaches 10-second limit"

Não existe nenhum mecanismo no firmware atual para detectar quando o botão foi liberado. O sistema simplesmente não tem essa informação.

**Impacto no sistema:** P0 — Bloqueia o requisito funcional mais crítico.

---

### PROBLEMA #2 — CRÍTICO: Gravação de Áudio de Duração Fixa Bloqueante

**Arquivo:** `firmware/components/audio/audio_capture.c`, linha ~145

```c
void task_audio_capture(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // Espera trigger

        audio_capture_start();
        vTaskDelay(pdMS_TO_TICKS(APP_AUDIO_RECORD_MS));  // ← BLOQUEIA 10 SEGUNDOS
        audio_capture_stop(&rec);                         // Lê buffer acumulado
        vitalkare_http_post_binary(...);                  // ← BLOQUEIA DURANTE UPLOAD
    }
}
```

**Problemas identificados:**

1. **`vTaskDelay(10s)`**: A task fica completamente suspensa por 10 segundos independentemente de qualquer coisa — botão solto, emergência iniciada, sistema em modo offline. É impossível interromper ou modificar a gravação durante esse período.

2. **Alocação tardia e gigante**: `audio_capture_stop()` chama `heap_caps_malloc(AUDIO_BUFFER_BYTES)` = até 320 KB no momento de parar. Se a PSRAM estiver fragmentada (provável após operação prolongada), a alocação falha silenciosamente e o áudio é perdido.

3. **Upload dentro da task de captura**: `vitalkare_http_post_binary()` dentro da task de áudio — durante o upload (que pode levar vários segundos em redes congestionadas), a task está completamente indisponível para aceitar um novo `ulTaskNotifyTake()`. Se o paciente pressionar o botão durante o upload, a solicitação é silenciosamente perdida.

4. **`i2s_channel_read()` como batch read**: A API I2S está sendo usada como buffer acumulador, não como pipeline de streaming. O DMA não está sendo consumido em tempo real — os dados ficam no buffer DMA do hardware até a leitura em bloco ao final.

**Impacto no sistema:** P0 — Implementação funcional impossível com este design.

---

### PROBLEMA #3 — CRÍTICO: Emergência não inicia Gravação de Áudio

**Arquivo:** `firmware/main/main.c`, função `on_emergency_event()`

```c
static void on_emergency_event(const system_event_t *evt)
{
    core_set_state(SYS_STATE_EMERGENCY);
    ui_set_screen(UI_SCREEN_EMERGENCY);

    // POST do evento JSON
    vitalkare_http_post(endpoint, json);
    log_write(...);
    // FIM — nenhuma gravação de áudio iniciada
}
```

**Diagnóstico:**
O handler de emergência não inicia captura de áudio em nenhuma circunstância. O Botão B é completamente ignorado como gatilho de gravação. O requisito:

> "Button B — Emergency: Audio recording MUST start immediately"

Não tem nenhuma implementação no código atual.

**Impacto no sistema:** P0 — Requisito funcional completamente ausente.

---

### PROBLEMA #4 — CRÍTICO: `vTaskDelay()` no Handler de Eventos Wi-Fi

**Arquivo:** `firmware/components/network/wifi_manager.c`, função `on_wifi_event()`

```c
static void on_wifi_event(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    if (id == WIFI_EVENT_STA_DISCONNECTED) {
        // ...
        uint32_t delay_ms = /* backoff calculado */;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));  // ← BLOQUEIA O EVENT LOOP
        esp_wifi_connect();
    }
}
```

**Diagnóstico:**
`vTaskDelay()` dentro de um handler de eventos ESP-IDF é um erro grave. O handler executa na task `esp_event_loop_default`, que é a mesma task responsável por processar **todos os eventos Wi-Fi e IP do sistema**. Ao chamar `vTaskDelay()` aqui:

- O loop de eventos fica suspenso por até 120 segundos
- Nenhum outro evento Wi-Fi ou IP pode ser processado durante esse período
- O evento `WIFI_EVENT_STA_START` não será processado se disparar durante o delay
- Comportamento indefinido e potencial deadlock

**Solução correta:** Usar `esp_timer_start_once()` para agendar a reconexão de forma não-bloqueante.

**Impacto no sistema:** P1 — Pode causar deadlock de conectividade.

---

### PROBLEMA #5 — ALTO: Event Dispatcher sem Prioridade

**Arquivo:** `firmware/components/events/event_dispatcher.c`

```c
#define EVENT_QUEUE_SIZE 32
static QueueHandle_t s_event_queue = NULL;
// FreeRTOS queue = FIFO puro
result = xQueueSend(s_event_queue, event, pdMS_TO_TICKS(10));
```

**Diagnóstico:**
A fila de eventos é um FIFO puro. O campo `priority` em `system_event_t` é preenchido corretamente pelos produtores, mas nunca é usado pelo dispatcher — a ordem de processamento é exclusivamente FIFO.

**Cenário de falha real:**
1. `task_sensor_poll` posta 20 leituras de sensores na fila
2. Paciente pressiona Botão B (emergência)
3. O evento de emergência entra na posição 21 da fila
4. O dispatcher processa as 20 leituras antes de tratar a emergência
5. Latência de emergência = 20 × tempo_de_processamento_de_sensor

Para eventos de emergência críticos em um ambiente hospitalar, isso é inaceitável.

**Solução:** Duas filas separadas (prioridade alta e baixa) ou uso de `xQueueSendToFront()` para eventos críticos.

**Impacto:** P1 — Latência de emergência não determinística.

---

### PROBLEMA #6 — ALTO: HTTP Client Síncrono Monopoliza a Thread

**Arquivo:** `firmware/components/http_client/vitalkare_http.c`

```c
if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(6000)) != pdTRUE) { ... }
esp_err_t ret = esp_http_client_perform(client);  // ← BLOQUEIA ATÉ CONCLUSÃO
xSemaphoreGive(s_mutex);
```

**Diagnóstico:**
`esp_http_client_perform()` é uma chamada bloqueante que aguarda toda a transação HTTP (connect + request + response). Para upload de áudio binário (até 320 KB), o tempo de transmissão em uma rede LAN de 10 Mbps é ~250 ms em condições ideais, mas pode ser 2–5 segundos em redes congestionadas ou com latência de roteamento.

Durante esse período, **a task que chamou `vitalkare_http_post_binary()` está completamente bloqueada**. No design atual, essa task é `task_audio_capture` — o que significa que nenhum novo trigger de gravação pode ser aceito durante o upload.

O mutex de 6 segundos adiciona um segundo vetor de bloqueio: se duas tasks tentarem fazer HTTP simultaneamente, uma delas espera até 6 segundos ou falha silenciosamente.

**Impacto:** P1 — Pipeline de áudio não-reentrante, eventos perdidos durante upload.

---

### PROBLEMA #7 — MÉDIO: `s_audio_task_handle` como Acoplamento Fraco

**Arquivo:** `firmware/main/main.c`

```c
static TaskHandle_t s_audio_task_handle = NULL;

static void on_request_event(const system_event_t *evt)
{
    if (s_audio_task_handle) {
        xTaskNotifyGive(s_audio_task_handle);  // Acoplamento direto
    }
}
```

**Diagnóstico:**
O handler de evento usa `xTaskNotifyGive()` diretamente no `TaskHandle_t` da task de áudio, criando um acoplamento forte entre `main.c` e `audio_capture.c`. Isso viola o princípio de isolamento de módulos e cria problemas:

1. Se a task de áudio for recriada (ex: após erro de I2S), o handle fica stale
2. `xTaskNotify` não tem semântica de fila — múltiplos `xTaskNotifyGive()` sem intervalo equivalem a um único notify (contagem máxima depende do modo)
3. O mecanismo bypassa completamente o event dispatcher, criando dois caminhos paralelos de comunicação

**Solução correta:** A task de áudio deve consumir da fila central de eventos, não de notificações diretas.

---

### PROBLEMA #8 — MÉDIO: Estado `SYS_STATE_RECORDING` Nunca Entrado

**Arquivo:** `firmware/components/core/core.h` e `firmware/main/main.c`

```c
// Definido em core.h:
SYS_STATE_RECORDING,  // Estado existe...

// Mas em on_request_event():
// core_set_state(SYS_STATE_RECORDING) nunca é chamado
xTaskNotifyGive(s_audio_task_handle);  // Bypassa o state machine
```

**Diagnóstico:**
O estado `SYS_STATE_RECORDING` existe na enumeração mas nunca é entrado. A máquina de estados não reflete o estado real do sistema durante uma gravação. Isso significa que o OLED nunca exibe `UI_SCREEN_RECORDING` de forma controlada pelo fluxo correto, e outras partes do sistema não podem inibir comportamentos incompatíveis (ex: câmera não sabe que áudio está em andamento).

---

### PROBLEMA #9 — MÉDIO: I2C Compartilhado sem Lock Centralizado

**Diagnóstico:**
OLED (SSD1306), HDC1080 e MPU6050 compartilham o barramento I2C_NUM_0. Os drivers de cada periférico fazem transações I2C de forma independente. Se `task_display_update` e `task_sensor_poll` executarem concorrentemente e ambas tentarem acessar I2C sem um mutex de barramento compartilhado, haverá corrupção de transação.

A análise do código atual não encontrou um `i2c_bus_mutex` centralizado — cada driver gerencia sua própria inicialização mas não há proteção de acesso concorrente ao barramento.

**Impacto:** P2 — Leituras de sensor corrompidas, tela com artefatos, potencial travamento do I2C.

---

### PROBLEMA #10 — BAIXO: Sem Mecanismo de Deduplicação no Offline Cache

**Diagnóstico:**
`offline_cache.c` implementa um ring buffer de 200 eventos. Ao reconectar, os eventos são reenviados em ordem. Contudo, não há:
- Identificador único de evento (`event_id`) nos pacotes
- Lógica de deduplicação no backend
- Timestamp de expiração por evento

Em cenário de reconexão parcial (envio parcial + falha + reconexão), os mesmos eventos podem ser enviados múltiplas vezes.

---

## 3. Required Refactors

### 3.1 Prioridade P0 — Bloqueantes para Requisito Mínimo

| # | Módulo | Mudança |
|---|---|---|
| R01 | `button_handler.c` | Mudar para `GPIO_INTR_ANYEDGE`; publicar `EVT_BTN_PRESSED` + `EVT_BTN_RELEASED` com `gpio_num` |
| R02 | `audio_capture.c` | Reescrever pipeline: DMA ring buffer contínuo + máquina de estados interna de recording |
| R03 | `audio_capture.c` | Separar em 3 tasks: `capture`, `encoder`, `upload` |
| R04 | `main.c` | `on_emergency_event()` deve iniciar gravação de áudio com prioridade máxima |
| R05 | `main.c` | `on_request_event()` deve transitar para `SYS_STATE_RECORDING` |
| R06 | `event_dispatcher.c` | Implementar fila de prioridade ou dual-queue (critical/normal) |

### 3.2 Prioridade P1 — Estabilidade de Rede

| # | Módulo | Mudança |
|---|---|---|
| R07 | `wifi_manager.c` | Substituir `vTaskDelay` por `esp_timer_start_once` no event handler |
| R08 | `vitalkare_http.c` | Refatorar para TCP persistente (ver Seção 4) |
| R09 | `vitalkare_http.c` | Separar thread de upload (fila de pacotes outgoing) |

### 3.3 Prioridade P2 — Qualidade e Robustez

| # | Módulo | Mudança |
|---|---|---|
| R10 | Todos os drivers I2C | Introduzir `i2c_bus.c` com mutex compartilhado |
| R11 | `audio_capture.c` | Pré-alocar buffer de áudio em PSRAM no boot, nunca alocar dinamicamente |
| R12 | `offline_cache.c` | Adicionar `event_id` UUID-like e TTL nos pacotes cacheados |
| R13 | `core.c` | Garantir transição correta para `SYS_STATE_RECORDING` |

---

## 4. Proposed TCP Architecture

### 4.1 Justificativa

O sistema opera exclusivamente em LAN hospitalar controlada. Neste contexto:

| Critério | HTTP REST | MQTT Broker | **TCP Persistente** |
|---|---|---|---|
| Overhead de conexão | Alto (TCP+HTTP por request) | Médio (broker como intermediário) | **Zero (sessão persistente)** |
| Latência de evento | 50–500 ms | 10–100 ms | **< 10 ms** |
| Streaming de áudio | ❌ Inadequado | ❌ Limite ~256KB | **✅ Nativo** |
| Comandos do backend | Polling necessário | ✅ Sub-topic | **✅ Bidirecional** |
| Dependência externa | Servidor HTTP | Broker MQTT | **Apenas servidor** |
| Controle de backpressure | Não nativo | Não granular | **✅ TCP window** |
| Determinismo | ❌ Baixo | ❌ Depende do broker | **✅ Alto** |

### 4.2 Modelo de Conexão

```
ESP32-S3 (KIIRA V2)                    Backend FastAPI
┌─────────────────────┐                ┌──────────────────────────┐
│                     │                │                          │
│  task_tcp_client    │◄──── TCP ─────►│  TCP Listener            │
│  (persistent conn)  │  Port 7890     │  (asyncio StreamReader/  │
│                     │                │   StreamWriter)          │
│  Outgoing Queue     │──── Packets ──►│  Packet Router           │
│  (xQueue, 32 slots) │                │     ├── Event Processor  │
│                     │                │     ├── Audio Service    │
│  Incoming Buffer    │◄─── Commands ──│     └── Command Sender   │
│  (ring buf, 2KB)    │                │                          │
└─────────────────────┘                └──────────────────────────┘
```

### 4.3 Modelo de Socket

**Uma única conexão TCP persistente por dispositivo.** Todos os tipos de tráfego (telemetria, áudio, emergência, comandos, heartbeat) são multiplexados sobre essa conexão via tipo de pacote no header.

**Por que não múltiplos sockets?**
- O ESP32-S3 tem limite de ~16 sockets simultâneos, mas cada socket com TLS consome ~20–40 KB de heap
- A complexidade de reconexão multiplica com o número de sockets
- Em redes hospitalares, portas adicionais precisam de liberação no firewall
- Um único socket com multiplexing por tipo de pacote é mais simples e mais robusto

**Design da task TCP:**

```
task_tcp_client (prio 5, stack 8192):
  ├── state: DISCONNECTED → CONNECTING → CONNECTED → HANDSHAKE → ACTIVE
  ├── loop:
  │     ├── Se DISCONNECTED: conecta com backoff exponencial
  │     ├── Se ACTIVE:
  │     │     ├── xQueueReceive(outgoing_queue, 0ms) → envia próximo pacote
  │     │     ├── recv_nonblock() → acumula no ring buffer de entrada
  │     │     ├── Processa pacotes completos do ring buffer
  │     │     └── Verifica heartbeat timeout
  │     └── Se erro de socket: transita para DISCONNECTED
  └── watchdog_feed_task(WDT_TCP)
```

### 4.4 Fila de Saída — Outgoing Packet Queue

```c
// Estrutura na fila de saída
typedef struct {
    uint8_t  *data;       // Ponteiro para buffer alocado pelo produtor
    uint16_t  len;        // Tamanho em bytes
    uint8_t   priority;   // 0=normal, 1=high, 2=critical
    bool      free_data;  // Se true, task_tcp_client libera o buffer após envio
} outgoing_packet_t;
```

Para áudio (pacotes grandes), o produtor aloca o buffer na PSRAM, coloca o ponteiro na fila, e a task TCP envia e libera. Isso evita cópias desnecessárias.

Para pacotes pequenos (eventos, heartbeat), o buffer pode ser stack-alocado no produtor e copiado na fila (`free_data = false`).

**Garantia de ordem de emergência:** A fila de saída deve ser implementada como **duas filas** separadas:
- `s_critical_queue` (capacidade 8) — para `PKT_EMERGENCY`, `PKT_HEARTBEAT`
- `s_normal_queue` (capacidade 32) — para telemetria, áudio, comandos

A task TCP sempre drena a fila crítica antes de verificar a fila normal.

### 4.5 Reconexão TCP

```
DISCONNECTED
    │
    ├── connect() falhou → timer(backoff) → DISCONNECTED
    │
    └── connect() OK → CONNECTED
           │
           ├── send(PKT_HANDSHAKE) → aguarda PKT_HANDSHAKE_ACK (timeout: 3s)
           │
           ├── ACK recebido → ACTIVE
           │         ├── Drena offline cache (replay offline events)
           │         └── Retoma pipeline normal
           │
           └── Timeout/NACK → fecha socket → DISCONNECTED
```

---

## 5. Proposed Packet Protocol

### 5.1 Framing

```
┌──────────────────────────────────────────────────────────┐
│                    VitalKare Wire Packet                  │
├────────┬──────────┬──────────┬─────────────┬─────────────┤
│ MAGIC  │  TYPE    │  SEQ     │  LENGTH     │  PAYLOAD    │
│ 2B     │  1B      │  2B      │  2B         │  N bytes    │
├────────┴──────────┴──────────┴─────────────┴─────────────┤
│                    CRC16 (2B) — sobre TYPE+SEQ+LEN+PAYLOAD│
└──────────────────────────────────────────────────────────┘

Total header: 9 bytes
CRC: 2 bytes
Total overhead per packet: 11 bytes
```

**Magic bytes:** `0xVK` → `0x56 0x4B` (ASCII "VK")

**Função do Magic:** Permite sincronização após corrupção de stream. O receptor descarta bytes até encontrar `0x56 0x4B` seguido de um TYPE válido.

**SEQ (Sequence Number):** Contador monotônico por sessão (0–65535, wraps). Permite:
- Detecção de pacotes perdidos
- Deduplicação no servidor
- Ordenação de chunks de áudio

**LENGTH:** Tamanho do PAYLOAD em bytes (0–65535). Limita o payload máximo a 64 KB por pacote — suficiente para chunks de áudio.

### 5.2 Packet Types

```c
typedef enum {
    // ── Sessão ──────────────────────────────────────────
    PKT_HANDSHAKE           = 0x01,  // C→S: device_id, firmware_version, ward_id
    PKT_HANDSHAKE_ACK       = 0x02,  // S→C: server_time, session_id
    PKT_HEARTBEAT           = 0x03,  // C→S: uptime, free_heap, rssi, sd_free
    PKT_HEARTBEAT_ACK       = 0x04,  // S→C: server_time (NTP sync)
    PKT_DISCONNECT          = 0x05,  // C→S or S→C: graceful disconnect

    // ── Eventos ─────────────────────────────────────────
    PKT_EVENT_EMERGENCY     = 0x10,  // C→S: priority=CRITICAL, trigger=BUTTON_B
    PKT_EVENT_REQUEST       = 0x11,  // C→S: priority=NORMAL,   trigger=BUTTON_A
    PKT_EVENT_ENVIRONMENT   = 0x12,  // C→S: temp, humidity, lux, motion
    PKT_EVENT_STATUS        = 0x13,  // C→S: bed status update
    PKT_EVENT_ACK           = 0x14,  // S→C: event acknowledged by operator
    PKT_EVENT_CONFIRM       = 0x15,  // S→C: server confirmed receipt of event

    // ── Áudio ────────────────────────────────────────────
    PKT_AUDIO_START         = 0x20,  // C→S: session_id, sample_rate, encoding, priority
    PKT_AUDIO_CHUNK         = 0x21,  // C→S: chunk_seq, data[], is_last
    PKT_AUDIO_END           = 0x22,  // C→S: total_chunks, total_bytes, crc_full
    PKT_AUDIO_ACK           = 0x23,  // S→C: received_chunks, missing[]

    // ── Câmera ───────────────────────────────────────────
    PKT_CAMERA_REQUEST      = 0x30,  // S→C: operator solicita captura
    PKT_IMAGE_START         = 0x31,  // C→S: width, height, encoding
    PKT_IMAGE_CHUNK         = 0x32,  // C→S: chunk_seq, jpeg_data[]
    PKT_IMAGE_END           = 0x33,  // C→S: total_bytes, crc_full

    // ── Comandos ─────────────────────────────────────────
    PKT_CMD_DISPLAY         = 0x40,  // S→C: atualiza OLED remotamente
    PKT_CMD_BUZZER          = 0x41,  // S→C: ativa/desativa buzzer
    PKT_CMD_OTA_START       = 0x42,  // S→C: inicia fluxo OTA
    PKT_CMD_REBOOT          = 0x43,  // S→C: reinicia dispositivo

    // ── Offline Sync ─────────────────────────────────────
    PKT_OFFLINE_BATCH_START = 0x50,  // C→S: início do replay de cache offline
    PKT_OFFLINE_EVENT       = 0x51,  // C→S: evento cacheado com timestamp original
    PKT_OFFLINE_BATCH_END   = 0x52,  // C→S: fim do replay

    // ── Sistema ──────────────────────────────────────────
    PKT_ERROR               = 0xFE,  // Any: código de erro
    PKT_NACK                = 0xFF,  // Any: NACK com SEQ de referência
} pkt_type_t;
```

### 5.3 Payload Definitions

**PKT_HANDSHAKE (C→S):**
```c
typedef struct __attribute__((packed)) {
    char     device_id[32];        // "leito01"
    char     ward_id[32];          // "ala-a"
    char     firmware_version[16]; // "1.0.0"
    uint8_t  protocol_version;     // 1
    uint32_t boot_timestamp_ms;    // ms desde boot
} pkt_handshake_t;  // 83 bytes
```

**PKT_EVENT_EMERGENCY (C→S):**
```c
typedef struct __attribute__((packed)) {
    uint8_t  event_id[16];    // UUID v4 (128-bit, gerado no firmware)
    uint32_t timestamp_ms;    // ms desde boot
    uint8_t  trigger;         // 0=BUTTON_B, 1=SENSOR_MOTION, 2=AUDIO_DETECT
    uint8_t  priority;        // sempre 1 (CRITICAL)
    char     patient_id[32];  // do NVS
    uint8_t  rssi_dbm;        // sinal Wi-Fi atual
} pkt_event_emergency_t;  // 54 bytes
```

**PKT_AUDIO_START (C→S):**
```c
typedef struct __attribute__((packed)) {
    uint8_t  session_id[8];   // ID único desta gravação
    uint8_t  trigger_type;    // 0=REQUEST(A), 1=EMERGENCY(B)
    uint16_t sample_rate;     // 16000
    uint8_t  bit_depth;       // 16
    uint8_t  channels;        // 1 (mono)
    uint8_t  encoding;        // 0=PCM_RAW, 1=ADPCM, 2=OPUS
    uint8_t  priority;        // 0=normal (A), 2=critical (B)
    uint8_t  event_id[16];    // Referência ao evento pai (emergency/request)
} pkt_audio_start_t;  // 33 bytes
```

**PKT_AUDIO_CHUNK (C→S):**
```c
typedef struct __attribute__((packed)) {
    uint8_t  session_id[8];
    uint16_t chunk_seq;       // Sequência dentro desta sessão de áudio
    uint16_t chunk_len;       // Bytes de áudio neste chunk
    uint8_t  is_last;         // 1 se este é o último chunk
    uint8_t  audio_data[];    // Dados de áudio (PCM/ADPCM/Opus)
} pkt_audio_chunk_t;
```

**Tamanho de chunk recomendado:** 4 KB por pacote → ~16 ms de áudio a 16 kHz 16-bit. Isso mantém o RTT de um chunk dentro de 1-2 ms em LAN Gigabit.

### 5.4 CRC Strategy

Usar **CRC-16/CCITT** (polinômio 0x1021, init 0xFFFF). É:
- Eficiente para implementação em C sem lookup table
- Detecta todos os erros de burst até 16 bits
- Amplamente usado em protocolos embedded (XMODEM, Bluetooth, etc.)

```c
uint16_t crc16_ccitt(const uint8_t *data, size_t len);
// Calcular sobre: type(1) + seq(2) + length(2) + payload(N)
// Armazenar nos 2 bytes finais do pacote
```

### 5.5 ACK/NACK Behavior

| Tipo de Pacote | ACK Requerido | Comportamento sem ACK |
|---|---|---|
| `PKT_HANDSHAKE` | Sim (`PKT_HANDSHAKE_ACK`) | Fecha socket após 3 s |
| `PKT_EVENT_EMERGENCY` | Sim (`PKT_EVENT_CONFIRM`) | Reenvia 3x com backoff 1 s, depois cacheia offline |
| `PKT_EVENT_REQUEST` | Sim (`PKT_EVENT_CONFIRM`) | Reenvia 2x, depois cacheia offline |
| `PKT_EVENT_ENVIRONMENT` | Não | Best-effort, perde se fila cheia |
| `PKT_AUDIO_CHUNK` | Implícito (window-based) | Backend solicita retransmissão via `PKT_AUDIO_ACK` |
| `PKT_HEARTBEAT` | Sim (`PKT_HEARTBEAT_ACK`) | Conta como timeout após 3 missess |

---

## 6. FreeRTOS Task Topology

### 6.1 Task Map Completo

```
                         ┌─────────────────┐
                         │  task_watchdog  │ prio=6, stack=4096
                         │  (alimenta IWDT)│
                         └────────┬────────┘
                                  │ watchdog_feed por todas as tasks
        ┌─────────────────────────▼─────────────────────────────┐
        │                   task_button_handler                  │
        │ prio=5, stack=2048                                      │
        │ Consome: s_btn_isr_queue (ISR → task)                  │
        │ Produz: s_event_queue (EVT_BTN_PRESSED/RELEASED)       │
        └─────────────────────────┬──────────────────────────────┘
                                  │ event_post()
        ┌─────────────────────────▼──────────────────────────────┐
        │                task_event_dispatcher                    │
        │ prio=5, stack=4096                                       │
        │ Consome: s_critical_queue (prio) + s_normal_queue       │
        │ Roteia para handlers registrados                         │
        │ Publica em: audio_ctrl_queue, tcp_outgoing_queue        │
        └──────────┬──────────────────────┬──────────────────────┘
                   │                      │
    ┌──────────────▼──────┐   ┌───────────▼────────────────────┐
    │  task_audio_ctrl    │   │      task_tcp_client           │
    │  prio=5, stack=4096 │   │  prio=5, stack=8192            │
    │  Recording FSM      │   │  TCP persistent connection     │
    │  Consome: audio_    │   │  Consome: tcp_critical_queue   │
    │  ctrl_queue         │   │           tcp_normal_queue     │
    │  Produz: audio_     │   │  Produz: incoming_cmd_queue    │
    │  chunk_queue        │   └───────────┬────────────────────┘
    └──────┬──────────────┘               │
           │ DMA-ready chunks             │ Comandos recebidos
    ┌──────▼──────────────┐   ┌───────────▼────────────────────┐
    │  task_audio_encoder │   │   task_command_processor       │
    │  prio=4, stack=8192 │   │   prio=4, stack=4096           │
    │  ADPCM/Opus         │   │   Roteia cmds: câmera, ACK,    │
    │  Consome: audio_    │   │   display, buzzer, OTA         │
    │  chunk_queue        │   └────────────────────────────────┘
    │  Produz: tcp_       │
    │  normal_queue       │
    └─────────────────────┘

    ┌─────────────────────┐   ┌────────────────────────────────┐
    │  task_sensor_poll   │   │     task_display_update        │
    │  prio=3, stack=4096 │   │     prio=2, stack=4096         │
    │  HDC1080+LDR: 30s   │   │     2 Hz, não-bloqueante       │
    │  MPU6050: 10Hz      │   │     Lê ui_state (atômica)      │
    │  Produz: tcp_normal │   └────────────────────────────────┘
    └─────────────────────┘
    ┌─────────────────────┐
    │  task_storage_flush │
    │  prio=1, stack=4096 │
    │  Flush logs SD 60s  │
    └─────────────────────┘
```

### 6.2 Tabela de Tasks — Especificação Completa

| Task | Prio | Stack | Core | Período | Bloqueia em | Produz para |
|---|---|---|---|---|---|---|
| `task_watchdog` | 6 | 4096 | Any | 1 s | `vTaskDelay(1000)` | — |
| `task_button_handler` | 5 | 2048 | Any | Event-driven | `xQueueReceive(btn_isr_q)` | `s_critical_queue` (B), `s_normal_queue` (A) |
| `task_event_dispatcher` | 5 | 4096 | Any | Event-driven | `xQueueReceive` (dual-q) | `audio_ctrl_queue`, `tcp_queues` |
| `task_tcp_client` | 5 | 8192 | Core1 | Event-driven | `xQueueReceive` + `recv` (non-block) | `incoming_cmd_queue` |
| `task_audio_ctrl` | 5 | 4096 | Core0 | Event-driven | `xQueueReceive(audio_ctrl_q)` | `audio_chunk_queue` |
| `task_audio_encoder` | 4 | 8192 | Core0 | Event-driven | `xQueueReceive(audio_chunk_q)` | `tcp_normal_queue` |
| `task_command_processor` | 4 | 4096 | Any | Event-driven | `xQueueReceive(incoming_cmd_q)` | `event_queue`, `ui_state` |
| `task_sensor_poll` | 3 | 4096 | Any | 30 s / 100 ms | `vTaskDelay` | `tcp_normal_queue` |
| `task_display_update` | 2 | 4096 | Any | 500 ms | `vTaskDelay(500)` | — (lê `ui_state`) |
| `task_storage_flush` | 1 | 4096 | Any | 60 s | `vTaskDelay(60000)` | — |

**Nota sobre pinning de core:**
- `task_tcp_client` → Core 1: isolado de tarefas de hardware (I2S, I2C rodam no Core 0 via drivers)
- `task_audio_ctrl` e `task_audio_encoder` → Core 0: acesso direto ao DMA do I2S
- Todas as demais → Core auto (FreeRTOS scheduler escolhe)

### 6.3 Filas Inter-Task — Especificação

| Fila | Tamanho | Item Size | Producer | Consumer |
|---|---|---|---|---|
| `s_btn_isr_queue` | 8 | `btn_isr_event_t` (12B) | GPIO ISR | `task_button_handler` |
| `s_critical_queue` | 8 | `system_event_t` (148B) | `task_button_handler` (B), `task_tcp_client` | `task_event_dispatcher` |
| `s_normal_queue` | 32 | `system_event_t` (148B) | Múltiplos | `task_event_dispatcher` |
| `audio_ctrl_queue` | 4 | `audio_ctrl_msg_t` (8B) | `task_event_dispatcher` | `task_audio_ctrl` |
| `audio_chunk_queue` | 16 | `audio_chunk_t` (16B ptr) | `task_audio_ctrl` | `task_audio_encoder` |
| `tcp_critical_queue` | 8 | `outgoing_packet_t` (16B ptr) | Múltiplos | `task_tcp_client` |
| `tcp_normal_queue` | 32 | `outgoing_packet_t` (16B ptr) | Múltiplos | `task_tcp_client` |
| `incoming_cmd_queue` | 8 | `incoming_cmd_t` (64B) | `task_tcp_client` | `task_command_processor` |

### 6.4 Mutex Strategy

| Recurso Compartilhado | Mutex | Quem usa |
|---|---|---|
| `core_context_t` (estado global) | `core_mutex` (já existe) | Todas as tasks |
| Barramento I2C_NUM_0 | `i2c_bus_mutex` (a criar) | `task_display_update`, `task_sensor_poll` |
| SD Card | `sd_mutex` (a verificar) | `task_storage_flush`, `task_audio_ctrl` |
| I2S channel | Exclusividade via `task_audio_ctrl` (single owner) | Apenas `task_audio_ctrl` |

**Regra de ouro:** Nunca adquirir dois mutexes simultaneamente. Se necessário, estabelecer ordem canônica (ex: `i2c_bus_mutex` sempre antes de `core_mutex`) para evitar deadlock.

---

## 7. Event Flow Redesign

### 7.1 Dual-Priority Queue

```c
// event_dispatcher.h — ADIÇÃO
// Fila crítica para emergências e eventos de sistema
// Fila normal para telemetria, sensores, áudio
bool event_post_critical(const system_event_t *event, bool from_isr);
bool event_post_normal(const system_event_t *event, bool from_isr);

// task_event_dispatcher — LOOP MODIFICADO
void task_event_dispatcher(void *arg) {
    system_event_t evt;
    for (;;) {
        // Drena TODA a fila crítica primeiro
        while (xQueueReceive(s_critical_queue, &evt, 0) == pdTRUE) {
            dispatch_event(&evt);
        }
        // Processa UM evento normal (para não starvar)
        if (xQueueReceive(s_normal_queue, &evt, pdMS_TO_TICKS(10)) == pdTRUE) {
            dispatch_event(&evt);
        }
        watchdog_feed_task(WDT_EVENTS);
    }
}
```

### 7.2 Novo Fluxo de Emergência (Button B)

```
[GPIO 46 NEGEDGE — press]
    │ ISR: xQueueSendFromISR(btn_isr_q, {GPIO=46, ts, EDGE_FALL})
    ▼
task_button_handler
    ├── Debounce OK?
    ├── Posta EVT_BTN_PRESSED(B, ts) → s_critical_queue
    └── Configura GPIO para detectar POSEDGE (release)

task_event_dispatcher recebe EVT_BTN_PRESSED(B)
    ├── core_set_state(SYS_STATE_EMERGENCY)
    ├── ui_set_screen(UI_SCREEN_EMERGENCY)
    ├── event_post_critical(EVT_AUDIO_START_EMERGENCY)
    └── Monta PKT_EVENT_EMERGENCY → tcp_critical_queue

task_audio_ctrl recebe EVT_AUDIO_START_EMERGENCY
    ├── audio_ctrl_state = RECORDING
    ├── i2s_channel_enable(s_rx_chan)
    └── timer_start(recording_timeout, 10s)

task_audio_ctrl loop (DMA consumer):
    while (audio_ctrl_state == RECORDING):
        i2s_channel_read(chunk_buf, 4096, &read, 50ms)
        if read > 0:
            audio_chunk_t chunk = {buf, read, session_id, seq++}
            xQueueSend(audio_chunk_queue, &chunk, 0)

[GPIO 46 POSEDGE — release] OU [timer 10s expirou]
    │ ISR: xQueueSendFromISR(btn_isr_q, {GPIO=46, ts, EDGE_RISE})
    ▼
task_button_handler posta EVT_BTN_RELEASED(B) → s_critical_queue

task_event_dispatcher recebe EVT_BTN_RELEASED(B)
    └── event_post(EVT_AUDIO_STOP) → audio_ctrl_queue

task_audio_ctrl recebe EVT_AUDIO_STOP
    ├── audio_ctrl_state = IDLE
    ├── i2s_channel_disable()
    └── Posta PKT_AUDIO_END → tcp_normal_queue
```

### 7.3 Novo Fluxo de Solicitação (Button A)

Idêntico ao fluxo de emergência acima, exceto:
- `core_set_state(SYS_STATE_RECORDING)` em vez de `EMERGENCY`
- `ui_set_screen(UI_SCREEN_RECORDING)` em vez de `EMERGENCY`
- `PKT_EVENT_REQUEST` → `tcp_normal_queue` (não critical)
- `PKT_AUDIO_START.priority = 0` (normal)
- Sem transição para estado de emergência

### 7.4 Routing Map de Eventos

```
EVT_BTN_PRESSED(A)      → [handlers]: set_state(RECORDING), audio_ctrl_start(NORMAL), tcp_send_request
EVT_BTN_PRESSED(B)      → [handlers]: set_state(EMERGENCY), audio_ctrl_start(CRITICAL), tcp_send_emergency, buzzer
EVT_BTN_RELEASED(A/B)   → [handlers]: audio_ctrl_stop
EVT_AUDIO_READY         → [handlers]: encoder task notified (via queue)
EVT_SENSOR_READING      → [handlers]: tcp_send_environment, display_update
EVT_WIFI_CONNECTED      → [handlers]: tcp_reconnect_trigger, sync_offline_cache
EVT_WIFI_DISCONNECTED   → [handlers]: set_state(OFFLINE), display_update
EVT_TCP_CONNECTED       → [handlers]: set_state(OPERATIONAL), sync_offline_cache
EVT_TCP_DISCONNECTED    → [handlers]: set_state(OFFLINE)
EVT_ACK_RECEIVED        → [handlers]: set_state(OPERATIONAL), display_update(ACK)
EVT_OFFLINE_SYNC_NEEDED → [handlers]: tcp_replay_cache
```

---

## 8. Audio Pipeline Redesign

### 8.1 Problema Central — Streaming vs. Batch

O design atual é **batch**: acumula todo o áudio no buffer, para a gravação, lê o buffer, envia. O design requerido é **streaming**: produz chunks contínuos durante a gravação e os envia progressivamente.

```
ANTES (batch):
  [press] → [gravar 10s inteiros] → [copiar 320KB] → [enviar bloco]

DEPOIS (streaming):
  [press] → [chunk 4KB @64ms] → [encodar] → [enviar]
           → [chunk 4KB @64ms] → [encodar] → [enviar]
           → [chunk 4KB @64ms] → [encodar] → [enviar] ...
           → [release] → [enviar PKT_AUDIO_END]
```

### 8.2 DMA Buffer Configuration

```c
// hardware_config.h — MODIFICAR
#define HW_MIC_DMA_BUF_COUNT    4     // 4 buffers DMA
#define HW_MIC_DMA_BUF_LEN     2048  // 2048 samples por buffer

// Cada buffer = 2048 samples × 2 bytes = 4096 bytes
// Taxa de leitura: 16000 Hz → 8 ms por buffer DMA
// 4 buffers × 8 ms = 32 ms de lookahead antes de overflow
// Tamanho de chunk para TCP: 2 buffers DMA = 4096 bytes = ~128 ms de áudio
```

**Por que 4 buffers DMA?**
O DMA opera em ping-pong: enquanto o hardware preenche o buffer N, o software deve esvaziar o buffer N-1. Com 4 buffers, mesmo que `task_audio_ctrl` seja preemptada por 16 ms, o hardware continua gravando sem perder samples.

### 8.3 Audio Chunk Buffer — PSRAM Pre-Allocation

```c
// audio_capture.c — INICIALIZAÇÃO NO BOOT
#define AUDIO_CHUNK_POOL_SIZE    16   // Pool de 16 chunks simultâneos
#define AUDIO_CHUNK_DATA_SIZE    4096 // 4KB por chunk

typedef struct {
    uint8_t  data[AUDIO_CHUNK_DATA_SIZE];
    size_t   len;
    uint8_t  session_id[8];
    uint16_t seq;
    bool     in_use;
} audio_chunk_buf_t;

// Alocado UMA VEZ no boot em PSRAM
static audio_chunk_buf_t s_chunk_pool[AUDIO_CHUNK_POOL_SIZE]
    __attribute__((section(".ext_ram.bss")));  // Forçar PSRAM
```

**Benefício:** Elimina `heap_caps_malloc()` durante operação. Cada chunk é adquirido do pool (marcado `in_use=true`), processado, enviado, e liberado (marcado `in_use=false`). Zero alocação dinâmica durante gravação ativa.

### 8.4 Recording State Machine (task_audio_ctrl)

```
         ┌──────────┐
         │   IDLE   │
         └─────┬────┘
               │ EVT_AUDIO_START received
               │ (type: REQUEST or EMERGENCY)
         ┌─────▼────────────┐
         │   RECORDING      │◄── DMA reads, chunk posts
         │  (loop: read DMA │
         │   → post chunk)  │
         └──────┬─────┬─────┘
                │     │
    EVT_AUDIO_  │     │ Timer 10s expired
    STOP recvd  │     │ (hard limit)
                ▼     ▼
         ┌────────────────┐
         │  FLUSHING      │ ← Lê últimos bytes do DMA
         └───────┬────────┘
                 │ Último chunk enviado
         ┌───────▼────────┐
         │  POST_PROCESS  │ ← PKT_AUDIO_END + PKT_EVENT_CONFIRM
         └───────┬────────┘
                 │
         ┌───────▼────────┐
         │   IDLE         │
         └────────────────┘
```

### 8.5 Encoder Task (task_audio_encoder)

A task de encoding é separada da captura para que a latência de compressão não introduza jitter no DMA:

```
task_audio_encoder:
  for (;;):
    xQueueReceive(audio_chunk_queue, &chunk_ptr, portMAX_DELAY)
    
    // Comprimir in-place no mesmo buffer (ADPCM reduz 4:1)
    size_t encoded_len = adpcm_encode(chunk_ptr->data, chunk_ptr->len)
    chunk_ptr->len = encoded_len
    
    // Montar pacote TCP
    outgoing_packet_t pkt = {
        .data = chunk_ptr,           // Ponteiro direto — zero cópia
        .len = AUDIO_CHUNK_HEADER + encoded_len,
        .priority = (session.type == EMERGENCY) ? 2 : 0,
        .free_data = true            // tcp_client libera após envio
    }
    xQueueSend(tcp_normal_queue, &pkt, pdMS_TO_TICKS(50))
```

### 8.6 ADPCM vs. Opus — Tradeoff

| Critério | PCM Bruto | ADPCM | Opus |
|---|---|---|---|
| Razão de compressão | 1:1 | 4:1 | 8:1–16:1 |
| Latência de encoding | ~0 ms | ~1 ms/chunk | 20–40 ms/chunk |
| Complexidade de impl. | Trivial | Baixa (IMA ADPCM) | Alta (libopus ~200 KB) |
| Qualidade de voz | Referência | Boa | Excelente |
| CPU no ESP32-S3 | Trivial | ~2% | ~15–25% |
| RAM extra requerida | 0 | ~1 KB state | ~20–80 KB |

**Recomendação:** Usar **ADPCM (IMA/DVI4)** como padrão. É bem documentado, tem implementações públicas domínio público de ~200 linhas de C, e oferece a compressão necessária para manter os chunks dentro do MTU da rede local. Opus pode ser adicionado futuramente como opção configurável.

---

## 9. State Machine Redesign

### 9.1 Estados Expandidos para TCP

```
SYS_STATE_BOOT
  │ hardware_init() completo
  ▼
SYS_STATE_WIFI_CONNECTING
  │ IP obtido
  ▼
SYS_STATE_TCP_CONNECTING         ← NOVO
  │ TCP connect() + handshake OK
  ▼
SYS_STATE_SYNCING                ← NOVO — replaying offline cache
  │ Cache vazio ou sync completo
  ▼
SYS_STATE_OPERATIONAL
  ├──────────────────────────────────────────────────┐
  │ EVT_BTN_PRESSED(A)                                │ EVT_BTN_PRESSED(B)
  ▼                                                   ▼
SYS_STATE_RECORDING                          SYS_STATE_EMERGENCY
  │ EVT_AUDIO_STOP                                    │ EVT_AUDIO_STOP + EVT_ACK
  │ (release ou 10s)                                  │
  └──────────────────────────────────────────────────►▼
                                             SYS_STATE_OPERATIONAL

SYS_STATE_OPERATIONAL ─── TCP/Wi-Fi perdido ──►  SYS_STATE_OFFLINE
SYS_STATE_OFFLINE     ─── reconnect OK      ──►  SYS_STATE_TCP_CONNECTING
SYS_STATE_RECORDING   ─── TCP perdido       ──►  SYS_STATE_RECORDING_OFFLINE ← NOVO
SYS_STATE_EMERGENCY   ─── TCP perdido       ──►  SYS_STATE_EMERGENCY (não interrompe)
```

### 9.2 Novos Estados e Comportamentos

| Estado | Áudio | Display | TCP | SD |
|---|---|---|---|---|
| `BOOT` | Off | BOOT screen | Off | Init |
| `WIFI_CONNECTING` | Off | Connecting… | Off | Logs |
| `TCP_CONNECTING` | Off | Connecting… | Tentando | Logs |
| `SYNCING` | Off | "Sincronizando…" | Ativo | Lendo cache |
| `OPERATIONAL` | Stand-by | Normal (temp/ID) | Ativo | Logs |
| `RECORDING` | **Capturando** | **"GRAVANDO…"** | **Enviando chunks** | Salvando |
| `EMERGENCY` | **Capturando** | **"EMERGÊNCIA"** | **Priority queue** | Salvando |
| `RECORDING_OFFLINE` | Capturando | "GRAVANDO (offline)" | Off | **Salvando tudo** |
| `OFFLINE` | Stand-by | "OFFLINE" | Reconectando | Cache |
| `ERROR` | Off | Código de erro | Off | Dump de erro |

### 9.3 Transições Concorrentes — Regras

**Regra 1:** `EMERGENCY` tem prioridade sobre qualquer estado. Se o Botão B for pressionado durante `RECORDING`, o sistema transita para `EMERGENCY` imediatamente — a sessão de áudio atual é encerrada, um novo `PKT_AUDIO_END` é enviado para a sessão A, e uma nova sessão B é iniciada.

**Regra 2:** `core_set_state()` deve ser protegido por mutex (`core_mutex`). Múltiplos handlers de eventos podem tentar transitar o estado simultaneamente.

**Regra 3:** O estado `RECORDING_OFFLINE` permite que a gravação continue mesmo sem TCP. O áudio é armazenado integralmente no SD. Ao reconectar, o arquivo de áudio é enviado como parte do batch de offline sync.

---

## 10. Memory & Performance Analysis

### 10.1 Budget de Memória — Cenário de Pior Caso

```
Cenário: Recording ativo + TCP upload + sensor poll + display update

RAM Interna (520 KB disponível no ESP32-S3):
  ├── FreeRTOS kernel + scheduler:     ~32 KB
  ├── Stacks de tasks (soma):          ~52 KB
  │     watchdog(4)+button(2)+dispatcher(4)+tcp(8)+
  │     audio_ctrl(4)+audio_enc(8)+cmd_proc(4)+
  │     sensor(4)+display(4)+storage(4)+wifi(8) = 54 KB
  ├── Filas FreeRTOS (soma):           ~8 KB
  ├── TCP socket buffers (lwIP):       ~16 KB (padrão)
  ├── Wi-Fi driver:                    ~80 KB (reservado pelo driver)
  ├── I2S DMA buffers:                 ~16 KB (4 × 2KB × 2 palavras)
  ├── HTTP/IDF internals:              ~20 KB
  ├── Stack do app_main:               ~4 KB
  └── Heap livre estimado:            ~288 KB ← margem adequada

PSRAM (alocação estática no boot):
  ├── Chunk pool áudio (16 × 4 KB):   ~64 KB
  ├── Buffer câmera (1 frame JPEG VGA): ~80 KB
  ├── Offline cache ring buffer:       ~30 KB  (200 eventos × 148B)
  └── PSRAM livre estimada:           ~8 MB - 174 KB ≈ 8 MB ← ampla margem
```

**Conclusão:** A PSRAM do ESP32-S3 (tipicamente 2–8 MB) absorve confortavelmente todos os buffers de operação. O risco está na RAM interna, que é consumida pelos drivers de Wi-Fi (~80 KB reservado) e pilhas de lwIP. **Não alocar buffers de áudio na RAM interna.**

### 10.2 Stack Usage Analysis

| Task | Stack Alocado | Uso Típico Estimado | Margem |
|---|---|---|---|
| `task_watchdog` | 4096 B | ~512 B | ✅ Ampla |
| `task_button_handler` | 2048 B | ~512 B | ✅ Adequada |
| `task_event_dispatcher` | 4096 B | ~1024 B | ✅ Adequada |
| `task_tcp_client` | 8192 B | ~3072 B (buffers locais) | ✅ Adequada |
| `task_audio_ctrl` | 4096 B | ~1024 B | ✅ Adequada |
| `task_audio_encoder` | 8192 B | ~4096 B (state ADPCM) | ✅ Adequada |
| `task_sensor_poll` | 4096 B | ~1024 B | ✅ Adequada |
| `task_display_update` | 4096 B | ~1024 B | ✅ Adequada |
| `task_storage_flush` | 4096 B | ~1024 B | ✅ Adequada |

> **Ferramenta de monitoramento:** Usar `uxTaskGetStackHighWaterMark()` nas primeiras versões de desenvolvimento para confirmar as estimativas.

### 10.3 Timing Analysis — Emergency Latency Budget

```
Botão B pressionado
    │
    ▼ ~0 µs (ISR em hardware)
GPIO ISR dispara
    │
    ▼ <100 µs (portYIELD_FROM_ISR)
task_button_handler acorda
    │
    ▼ <1 ms (debounce + event_post)
task_event_dispatcher acorda (prio=5, same prio que button)
    │
    ▼ <500 µs (handler execution)
core_set_state(EMERGENCY) + ui_set_screen(EMERGENCY)
PKT_EVENT_EMERGENCY colocado em tcp_critical_queue
    │
    ▼ <1 ms (task_tcp_client loop tick)
PKT_EVENT_EMERGENCY serializado e passado para TCP stack
    │
    ▼ <5 ms (LAN RTT + backend processing)
Backend recebe, processa, envia WebSocket push ao dashboard
    │
    ▼
TOTAL DEVICE→DASHBOARD: < 10 ms em condições normais de LAN
```

**Comparativo com arquitetura anterior (HTTP síncrono):**

| Etapa | HTTP Anterior | TCP Novo |
|---|---|---|
| ISR → task wakeup | ~1 ms | ~1 ms |
| Event dispatch | ~2 ms | ~1 ms |
| Connection setup | ~50–200 ms (TCP+HTTP handshake) | **0 ms (socket persistente)** |
| Transmission | ~10–50 ms | ~5–10 ms |
| **Total** | **~65–255 ms** | **~7–12 ms** |

### 10.4 Heap Fragmentation Mitigation

**Regra #1:** Buffers de áudio NUNCA alocados dinamicamente durante operação — pool estático em PSRAM.

**Regra #2:** Pacotes TCP pequenos (< 200 bytes) devem ser alocados na stack do produtor e copiados para a fila. Pacotes grandes (áudio, imagem) usam ponteiro para buffer do pool.

**Regra #3:** Strings de log devem usar buffers estáticos ou stack. Nunca `malloc()` para formar strings de log.

**Regra #4:** Monitorar `esp_get_free_heap_size()` e `esp_get_minimum_free_heap_size()` no heartbeat. Alertar se mínimo < 50 KB.

---

## 11. Recommended File/Module Changes

### 11.1 Arquivos a Modificar

| Arquivo | Mudança Necessária | Prioridade |
|---|---|---|
| `components/buttons/button_handler.c` | `GPIO_INTR_ANYEDGE`; publicar PRESSED e RELEASED separadamente com tipo de borda | P0 |
| `components/audio/audio_capture.c` | Reescrever completamente: DMA streaming, chunk pool, recording state machine; eliminar `vTaskDelay` e alocação dinâmica | P0 |
| `components/audio/audio_capture.h` | Nova API: `audio_ctrl_start(type)`, `audio_ctrl_stop()`, `audio_ctrl_get_state()` | P0 |
| `main/main.c` | `on_emergency_event()`: chamar `audio_ctrl_start(AUDIO_TYPE_EMERGENCY)`; `on_request_event()`: chamar `audio_ctrl_start(AUDIO_TYPE_REQUEST)`; remover `s_audio_task_handle`; `on_wifi_connected()`: trigger TCP connect | P0 |
| `components/events/event_dispatcher.c` | Implementar dual-queue: `s_critical_queue` e `s_normal_queue`; loop que drena crítica antes de normal | P0 |
| `components/events/event_dispatcher.h` | Adicionar `EVT_BTN_RELEASED`, `EVT_TCP_CONNECTED`, `EVT_TCP_DISCONNECTED`, `EVT_AUDIO_CHUNK_READY`; adicionar `event_post_critical()` | P0 |
| `components/network/wifi_manager.c` | Substituir `vTaskDelay` por `esp_timer_start_once()` no event handler de desconexão | P1 |
| `components/core/core.h` | Adicionar `SYS_STATE_TCP_CONNECTING`, `SYS_STATE_SYNCING`, `SYS_STATE_RECORDING_OFFLINE` | P1 |

### 11.2 Arquivos a Criar

| Arquivo | Conteúdo | Prioridade |
|---|---|---|
| `components/network/tcp_client.c/h` | Task TCP persistente: connect/reconnect FSM, framing, dual outgoing queue, incoming buffer | P0 |
| `components/network/vk_protocol.c/h` | Serialização/deserialização de pacotes VK, CRC16, `pkt_type_t` | P0 |
| `components/audio/audio_encoder.c/h` | Task de encoding ADPCM; interface: consume `audio_chunk_queue`, produce `tcp_normal_queue` | P0 |
| `components/audio/adpcm.c/h` | Implementação IMA ADPCM encoder/decoder (~200 linhas) | P0 |
| `components/core/i2c_bus.c/h` | Mutex centralizado para barramento I2C_NUM_0; wrappers `i2c_bus_write()`, `i2c_bus_read()` | P1 |
| `components/network/command_processor.c/h` | Task que processa comandos recebidos do backend (camera req, ack, display update, OTA) | P1 |

### 11.3 Arquivos a Remover/Deprecar

| Arquivo | Motivo | Substituto |
|---|---|---|
| `components/http_client/vitalkare_http.c/h` | Substituído pelo TCP client | `components/network/tcp_client.c/h` |
| `components/mqtt/mqtt_client.c` e `vitalkare_mqtt.h` | Substituído pelo TCP client | `components/network/tcp_client.c/h` |

**Nota:** Manter os arquivos de MQTT/HTTP com flag `#ifdef LEGACY_TRANSPORT` durante a fase de transição para permitir rollback fácil se necessário.

### 11.4 Mudanças em app_config.h

```c
// ADICIONAR em firmware/config/app_config.h:

/* TCP Client */
#define APP_TCP_SERVER_PORT         7890
#define APP_TCP_CONNECT_TIMEOUT_MS  5000
#define APP_TCP_HEARTBEAT_INTERVAL_MS  15000   // 15s
#define APP_TCP_HEARTBEAT_MISS_MAX   3          // Desconecta após 3 missess
#define APP_TCP_RECONNECT_MIN_MS    2000
#define APP_TCP_RECONNECT_MAX_MS    120000

/* Audio Pipeline */
#define APP_AUDIO_CHUNK_SIZE_BYTES   4096       // 4KB por chunk DMA
#define APP_AUDIO_CHUNK_POOL_COUNT   16         // Pool de 16 chunks na PSRAM
#define APP_AUDIO_MAX_DURATION_MS    10000      // Limite hard de gravação
#define APP_AUDIO_ENCODING           ENCODING_ADPCM  // ADPCM padrão

/* Protocol */
#define APP_PROTOCOL_MAGIC_0    0x56   // 'V'
#define APP_PROTOCOL_MAGIC_1    0x4B   // 'K'
#define APP_PROTOCOL_VERSION    1
```

---

## 12. Migration Roadmap

### Fase A — Correções Críticas (1–2 semanas)
> **Critério:** Botão A e B gravam áudio corretamente; recording para ao soltar; emergência inclui áudio

| # | Tarefa | Arquivos Afetados | Effort |
|---|---|---|---|
| A1 | `button_handler.c`: `GPIO_INTR_ANYEDGE` + publicar PRESSED/RELEASED | `button_handler.c` | 2h |
| A2 | `event_dispatcher.h`: adicionar `EVT_BTN_RELEASED`, `EVT_AUDIO_START/STOP` | `event_dispatcher.h` | 1h |
| A3 | `audio_capture.c`: recording state machine com DMA streaming (eliminar `vTaskDelay`) | `audio_capture.c/h` | 1 dia |
| A4 | `audio_capture.c`: chunk pool pré-alocado na PSRAM | `audio_capture.c` | 4h |
| A5 | `main.c`: `on_emergency_event()` → `audio_ctrl_start(EMERGENCY)` | `main.c` | 1h |
| A6 | `main.c`: `on_request_event()` → `audio_ctrl_start(REQUEST)` + `set_state(RECORDING)` | `main.c` | 1h |
| A7 | Criar `audio_encoder.c` + `adpcm.c` | novo | 1 dia |
| A8 | `event_dispatcher.c`: dual-queue crítica/normal | `event_dispatcher.c` | 4h |
| A9 | Teste integrado: Botão A + Botão B + release + emergência durante recording | — | 1 dia |

### Fase B — TCP Architecture (2–3 semanas)
> **Critério:** Sistema usa TCP persistente; zero HTTP; emergência em < 10 ms

| # | Tarefa | Arquivos Afetados | Effort |
|---|---|---|---|
| B1 | Criar `vk_protocol.c/h`: serialização completa de todos os tipos de pacote + CRC16 | novo | 2 dias |
| B2 | Criar `tcp_client.c/h`: connect/reconnect FSM, framing, dual outgoing queue | novo | 3 dias |
| B3 | `wifi_manager.c`: fix `vTaskDelay` → `esp_timer_start_once` | `wifi_manager.c` | 2h |
| B4 | `main.c`: substituir `vitalkare_http_init()` por `tcp_client_init()` | `main.c` | 2h |
| B5 | `main.c`: `on_wifi_connected()` → `tcp_client_connect()` | `main.c` | 1h |
| B6 | Todos os pontos de `vitalkare_http_post()` → `tcp_send_packet()` | múltiplos | 2h |
| B7 | Criar `command_processor.c/h`: processa PKT_CMD_* recebidos | novo | 1 dia |
| B8 | Backend: adicionar `asyncio` TCP listener na porta 7890 | `backend/` | 2 dias |
| B9 | Teste: emergência end-to-end; medir latência device→dashboard | — | 1 dia |

### Fase C — Robustez e Produção (1–2 semanas)
> **Critério:** Modo offline funcional; watchdog TCP; I2C sem race condition

| # | Tarefa | Arquivos Afetados | Effort |
|---|---|---|---|
| C1 | Criar `i2c_bus.c/h`: mutex centralizado para I2C_NUM_0 | novo | 4h |
| C2 | Adaptar `oled_driver.c`, `hdc1080.c`, `mpu6050.c` para usar `i2c_bus_*()` | 3 arquivos | 4h |
| C3 | `offline_cache.c`: adicionar `event_id` + TTL; lógica de replay TCP | `offline_cache.c/h` | 1 dia |
| C4 | `watchdog.c`: adicionar `WDT_TCP_CLIENT` ao conjunto monitorado | `watchdog.c/h` | 2h |
| C5 | `core.h`: adicionar estados `TCP_CONNECTING`, `SYNCING`, `RECORDING_OFFLINE` | `core.h/c` | 2h |
| C6 | `ui_renderer.c`: adicionar telas para novos estados | `ui_renderer.c` | 4h |
| C7 | Teste de modo offline: desconectar LAN durante gravação; verificar cache SD | — | 1 dia |
| C8 | Teste de stress: 50 eventos em sequência; verificar heap mínima | — | 4h |

---

## 13. Risks & Engineering Tradeoffs

### 13.1 TCP vs. MQTT — Tradeoffs Detalhados

**Prós do TCP direto:**
- Latência < 10 ms garantida (sem overhead de broker)
- Streaming de áudio nativo (chunks contínuos)
- Zero dependência de infraestrutura intermediária
- Controle total do protocolo (packet prioritization)
- Reconexão determinística (sem "will" message, estado controlado pela app)

**Contras do TCP direto e como mitigar:**
- **Reconexão mais complexa:** O broker MQTT gerenciava Last Will Testament automaticamente. Com TCP, o backend deve detectar timeout de heartbeat (60 s) como substituto. **Mitigação:** `PKT_HEARTBEAT` obrigatório a cada 15 s + timeout de detecção de offline em 60 s.
- **Sem fan-out nativo:** MQTT permite múltiplos assinantes em um tópico. Com TCP direto, cada dispositivo tem uma única conexão com o backend, e o backend é responsável por distribuir ao dashboard. **Mitigação:** Nenhuma — o backend já tem essa responsabilidade via WebSocket.
- **QoS não nativo:** MQTT oferece QoS 2 (exactly-once) nativamente. Com TCP, implementa-se ACK por tipo de pacote na aplicação. **Mitigação:** Para emergência, implementar retransmissão com SEQ + `PKT_EVENT_CONFIRM`.

### 13.2 ADPCM vs. Opus — Decisão Final

**Recomendação: ADPCM para MVP, Opus para fase de produção.**

ADPCM é a escolha correta para a Fase A porque:
- Implementação de ~200 linhas de C sem dependências externas
- Determinístico: latência de encoding é O(N) com constante pequena
- Compressão 4:1 suficiente para chunks de 4 KB (1 KB comprimido por chunk)
- Reversível: decoder simples no backend Python

Opus oferece qualidade superior e compressão 8:1+, mas:
- Requer `libopus` (~200 KB de flash)
- Latência de encoding com frame de 20 ms (mínimo do codec)
- Adiciona complexidade ao sistema que não é necessária para transcription

### 13.3 Single TCP Socket vs. Multiple

**Decisão: Single socket com multiplexing.**

Com múltiplos sockets (ex: um para eventos, um para áudio), haveria:
- Consumo de ~40–80 KB de heap adicional por socket TLS
- Complexidade de sincronização de reconexão (todos os sockets devem reconectar juntos)
- Risco de race condition: evento enviado no socket A referencia sessão de áudio no socket B

**Exceção futura:** Se câmera e áudio precisarem ser transmitidos simultaneamente (fora do escopo atual), um segundo socket dedicado a mídia seria justificável.

### 13.4 Blocking I2C vs. Non-Blocking

Os drivers ESP-IDF de I2C são síncronos (blocking). Para `task_display_update` (prio=2) e `task_sensor_poll` (prio=3), chamadas de I2C bloqueiam a task enquanto a transação ocorre (tipicamente < 2 ms a 100 kHz).

**Risco:** Se o barramento I2C travar (dispositivo em estado inválido), ambas as tasks ficam indefinidamente bloqueadas. O watchdog de task detectará isso e causará um reset — comportamento correto, mas o reset deve ser logado para diagnóstico.

**Mitigação:** Usar timeout de I2C explícito (ESP-IDF permite configurar `timeout_ticks` na transação).

### 13.5 Audio During TCP Disconnect

**Cenário crítico:** Paciente pressiona Botão B durante queda de Wi-Fi.

**Comportamento esperado:**
1. `task_audio_ctrl` começa a gravar normalmente (DMA não depende de rede)
2. `task_audio_encoder` produz chunks normalmente
3. `task_tcp_client` está em estado `DISCONNECTED` — drena a `tcp_critical_queue` mas falha ao enviar; coloca os pacotes no `offline_cache`
4. OLED exibe `UI_SCREEN_EMERGENCY` + indicador de "offline"
5. Buzzer ativa localmente (independente de rede)
6. Ao reconectar, o cache é enviado com timestamps originais

O áudio da emergência nunca é perdido — é o caso mais crítico do sistema.

### 13.6 Potential Race Condition: Button A durante Emergency (Button B)

**Cenário:** Paciente pressiona B (emergência ativa), depois pressiona A.

**Comportamento desejado:** A deve ser ignorado durante emergência ativa.

**Implementação:**

```c
// em task_event_dispatcher handler de EVT_BTN_PRESSED(A):
if (core_get_state() == SYS_STATE_EMERGENCY) {
    ESP_LOGD(TAG, "Botão A ignorado durante emergência ativa");
    return;  // Não inicia nova gravação
}
```

---

## 14. Production-Readiness Recommendations

### 14.1 Segurança da Comunicação TCP

Para ambiente hospitalar, mesmo em LAN interna, os dados de voz e eventos de pacientes são dados sensíveis (LGPD Art. 11 — dados de saúde são dados sensíveis).

**Recomendação:** Implementar TLS 1.3 no socket TCP usando `esp_tls_t` do ESP-IDF:

```c
esp_tls_cfg_t tls_cfg = {
    .cacert_pem_buf = server_ca_cert,      // CA interna do hospital
    .cacert_pem_bytes = sizeof(server_ca_cert),
    .clientcert_pem_buf = device_cert,     // Certificado único por dispositivo
    .clientcert_pem_bytes = sizeof(device_cert),
};
esp_tls_t *tls = esp_tls_init();
esp_tls_conn_new_sync(server_ip, strlen(server_ip), port, &tls_cfg, tls);
```

**Custo:** ~25 KB RAM adicional por sessão TLS. **Justificativa:** Obrigatório para conformidade LGPD.

### 14.2 Unique Device Identity

Cada dispositivo deve ter um certificado X.509 único (não credenciais SSID/password compartilhadas). O certificado deve ser:
- Gravado na NVS ou em partição `nvs_keys` criptografada
- Emitido por uma CA interna do sistema hospitalar
- Com CN = `device_id` (ex: `leito01.hospital.local`)

Isso elimina o risco de credenciais compartilhadas e permite revogação individual de dispositivos comprometidos.

### 14.3 Firmware Signing (OTA)

O processo OTA planejado (partições `ota_0`, `ota_1` já presentes) deve usar ESP-IDF Secure Boot v2:
- Chave privada de assinatura mantida FORA do repositório git (ambiente CI/CD)
- Verificação de hash SHA256 antes de commitar nova partição
- Rollback automático via `esp_ota_mark_app_invalid_rollback_and_reboot()` se o novo firmware não feeder o watchdog em < 60 s

### 14.4 Logging e Auditoria

Todos os eventos clínicos (emergência, solicitação de áudio, ACK) devem ser logados com:
- `device_id`, `event_type`, `timestamp_utc` (sincronizado via `PKT_HEARTBEAT_ACK`)
- Hash SHA256 do payload de áudio (para integridade)
- Signature do operador que fez o ACK

Os logs em SD devem usar o formato JSON Lines já implementado. Adicionar campo `"integrity_hash"` no log de eventos de áudio.

### 14.5 Stress Testing Checklist

Antes de qualquer deployment, executar os seguintes testes:

| Teste | Critério de Aprovação |
|---|---|
| 100 pressões consecutivas do Botão B | Zero eventos perdidos; timestamp monotônico |
| Gravação de áudio durante queda de Wi-Fi | Áudio salvo no SD; enviado ao reconectar |
| 8 horas de operação contínua | `esp_get_minimum_free_heap_size()` > 50 KB |
| Sensor poll + áudio simultâneos | Sem glitch de áudio; leituras de sensor corretas |
| Reconexão TCP durante upload de áudio | Upload retomado ou cacheado corretamente |
| Botão B durante gravação de Botão A | Estado correto; apenas um áudio ativo |
| SD Card removido durante operação | Operação degradada; sem crash |
| 20 dispositivos simultâneos no backend | Latência de emergência < 100 ms |

### 14.6 Observability

Adicionar ao heartbeat (`PKT_HEARTBEAT`) os campos:
- `min_heap_bytes`: `esp_get_minimum_free_heap_size()` desde o boot
- `task_wdt_feeds`: contagem de feeds por task (detecta tasks mortas que não travaram o WDT)
- `tcp_reconnect_count`: número de reconexões TCP desde o boot
- `audio_sessions_total`: número de gravações completadas
- `audio_chunks_dropped`: número de chunks descartados por fila cheia (indica overload)
- `offline_events_pending`: tamanho atual do cache offline

Esses campos permitem diagnosticar problemas remotamente sem acesso físico ao dispositivo.

---

## Apêndice — Diagrama de Fluxo Completo da Nova Arquitetura

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                     KIIRA V2 — Nova Arquitetura TCP                         │
│                                                                             │
│  GPIO ISR ──► btn_isr_queue ──► task_button_handler                         │
│                                        │                                    │
│                               PRESSED(A/B) + RELEASED(A/B)                  │
│                                        │                                    │
│                               s_critical_queue / s_normal_queue             │
│                                        │                                    │
│                              task_event_dispatcher (prio=5)                 │
│                             ┌──────────┴──────────┐                        │
│                             │                     │                        │
│                    audio_ctrl_queue          tcp_critical_queue /           │
│                             │                tcp_normal_queue               │
│                             │                     │                        │
│                    task_audio_ctrl          task_tcp_client (prio=5)        │
│                    (recording FSM)          (persistent TCP socket)         │
│                             │               ┌─────┴──────────┐             │
│                    audio_chunk_queue         │           incoming_cmd_queue │
│                             │          TCP wire             │               │
│                    task_audio_encoder        │       task_command_processor │
│                    (ADPCM encoder)           │       (handles PKT_CMD_*)    │
│                             │               │                               │
│                        tcp_normal_queue ────┘                               │
│                                                                             │
│  task_sensor_poll ────────► tcp_normal_queue                                │
│  task_display_update ◄───── ui_state (sem fila — leitura atômica)           │
│  task_storage_flush ◄────── sd_write_queue                                  │
│  task_watchdog ────────────► monitora todas as tasks acima                  │
└─────────────────────────────────────────────────────────────────────────────┘
                                        │
                                    TCP/TLS 7890
                                        │
┌───────────────────────────────────────▼─────────────────────────────────────┐
│                         Backend FastAPI                                      │
│  asyncio TCP Listener ──► Packet Router ──► Event Processor                 │
│                                         ──► Audio Service (Whisper)         │
│                                         ──► Command Sender                  │
│                      ──► WebSocket Push ──► Dashboard React                 │
└─────────────────────────────────────────────────────────────────────────────┘
```
