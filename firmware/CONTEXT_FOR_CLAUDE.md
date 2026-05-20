# VitalKare — Contexto Completo para Agente IA

> Gerado em 2026-05-20. Use como ponto de partida para qualquer agente IA novo neste projeto.

---

## 1. Visão Geral do Sistema

**VitalKare** é um sistema de monitoramento hospitalar composto por:

| Componente | Tecnologia | Estado |
|---|---|---|
| Firmware do dispositivo | ESP32-S3, ESP-IDF v6.0 | Em desenvolvimento ativo |
| Dashboard (frontend) | Vite + React (porta 5173) | Rodando |
| Backend do dashboard | Python + uvicorn/FastAPI | Rodando (porta WebSocket) |

### Hardware alvo: KIIRA V2

| Item | Valor |
|---|---|
| SoC | ESP32-S3 (QFN56, rev v0.2) |
| PSRAM | Octal 8MB AP_3v3 |
| Flash | 16MB |
| MAC | 98:88:e0:11:25:50 |
| Porta serial (JTAG) | COM18 |
| GPIOs reservados pelo PSRAM | GPIO33–40 (DQ0–DQ7), GPIO30 (CLK), GPIO26 (CS) — **nunca usar** |

---

## 2. Arquitetura do Firmware

```
firmware/
├── main/main.c              ← ponto de entrada, sequência de init + tasks
├── config/
│   ├── hardware_config.h    ← todos os GPIOs da KIIRA V2
│   ├── app_config.h         ← parâmetros de rede, filas, protocolos
│   └── device_config.h      ← WiFi SSID/senha, IP do servidor, device_id
├── components/
│   ├── audio/               ← captura PDM I2S + encoder ADPCM
│   ├── camera/              ← driver OV5640
│   ├── core/                ← state machine, task monitor, memory monitor
│   ├── events/              ← event dispatcher dual-priority
│   ├── network/
│   │   ├── tcp_client.c     ← cliente TCP persistente (raw socket, VK protocol)
│   │   ├── vk_protocol.h    ← protocolo binário VK (header 9B + payload + CRC)
│   │   ├── vk_reliability.c ← retransmissão com ACK
│   │   └── command_processor.c
│   ├── sensors/             ← HDC1080, MPU6050, MAX30102, LDR
│   ├── storage/             ← SD card, offline journal, offline cache
│   └── watchdog/
```

### Sequência de boot (`app_main`)

1. `system_manager_init()` — task monitor + memory monitor
2. `init_nvs()`
3. `core_init()` — state machine global
4. `log_writer_init()`
5. `event_dispatcher_init()` — dual-priority queue
6. `register_event_handlers()`
7. `offline_cache_init()`
8. `tcp_client_init()` ← **bug crítico resolvido aqui (ver seção 4)**
9. `cmd_processor_init()` + `vk_reliability_init()`
10. `init_hardware()` — sensores, display, botões, câmera
11. WiFi init → TCP connect → criar todas as tasks

---

## 3. Protocolo de Comunicação Firmware ↔ Dashboard

### Firmware (ESP32-S3)
- Usa **raw TCP socket** (lwIP) na porta `APP_TCP_SERVER_PORT = 7890`
- Protocolo binário **VK Protocol**:
  - Header: 9 bytes (magic "VK", versão, tipo de pacote, seq, payload_len)
  - Payload: variável (até 8192 bytes)
  - CRC: 2 bytes (CRC-16)
- Conecta em `APP_TCP_SERVER_IP` definido em `config/app_config.h`
- **NÃO usa WebSocket** — é TCP raw com frames binários

### Dashboard (notebook)
- Frontend Vite em `http://localhost:5173`
- Backend uvicorn/FastAPI aceita WebSocket em `/ws/dashboard`
- **Problema identificado**: o backend só escuta WebSocket de `127.0.0.1` (Vite proxy). Não há servidor TCP raw na porta 7890 acessível pela rede para o ESP32 conectar.

### ⚠️ Incompatibilidade de protocolo a resolver
O firmware usa **TCP raw + VK protocol binário**.
O backend usa **WebSocket**.
Precisa de uma das soluções:
- **Opção A**: Adicionar servidor TCP raw na porta 7890 no backend que aceite conexões do ESP32 e faça bridge para WebSocket (recomendado — não altera firmware)
- **Opção B**: Migrar o firmware para WebSocket (grande mudança, não recomendado agora)

---

## 4. Bugs Encontrados e Status

### Bug P0 — GPIO37 conflita com PSRAM Octal → boot loop [TEMPORARIAMENTE CONTORNADO]

**Arquivo**: `config/hardware_config.h` linha 53
```c
#define HW_MIC_DATA_GPIO   37   // ← ERRADO! GPIO37 = PSRAM DQ4/5
```
O driver I2S PDM chama `abort()` ao tentar configurar GPIO37, causando `TG1WDT_SYS_RST` em loop.

**Contorno atual**: init de áudio desabilitado com `#if 0` em `main/main.c`.

**Fix definitivo necessário**: verificar no esquemático do KIIRA V2 qual GPIO está fisicamente conectado ao DATA do microfone PDM e atualizar `HW_MIC_DATA_GPIO`.

---

### Bug P1 — TCP queue depth esgota heap interna → `ESP_ERR_NO_MEM` [CORRIGIDO]

**Arquivo**: `config/app_config.h`

**Causa raiz**:
- `sizeof(tcp_queued_frame_t) = 2178 bytes` (frame buffer fixo de 2176B para suportar chunks de áudio ADPCM)
- `APP_TCP_NORMAL_QUEUE_DEPTH = 32` → 32 × 2178 = **69 KB** alocado de uma vez via `xQueueCreate`
- Heap interna disponível após boot: ~120 KB. Após NVS init (~12 KB) + FreeRTOS (~10 KB) + DMA reservation (32 KB), restava < 70 KB contíguos
- `xQueueCreate` falha → `tcp_client_init()` retorna `ESP_ERR_NO_MEM` → `ESP_ERROR_CHECK` causa panic

**Correção aplicada**:
```c
// app_config.h
#define APP_TCP_NORMAL_QUEUE_DEPTH    8   // era 32
```
**TODO de longo prazo**: mover o storage dos frames de áudio para PSRAM (`xQueueCreateStatic` com buffer em `heap_caps_malloc(MALLOC_CAP_SPIRAM)`), restaurando `APP_TCP_NORMAL_QUEUE_DEPTH = 32`.

---

### Bug P2 — Botão handler era FATAL [CORRIGIDO para dev kit]

**Arquivo**: `main/main.c`

O handler de botões tinha `esp_restart()` em caso de falha. Na KIIRA V2 isso é aceitável, mas no dev kit (sem botões físicos nos GPIOs corretos) impedia o boot.

**Correção**: transformado em warning não-fatal (só seta `all_ok = false`).

**Nota**: na KIIRA V2 o button_handler inicializa com sucesso em GPIO45/GPIO46.

---

### Bug P3 — SD Card GPIOs conflitam com PSRAM [NÃO CORRIGIDO]

**Arquivo**: `config/hardware_config.h`
```c
#define HW_SD_CLK_GPIO   39   // dentro da faixa PSRAM 33–40
#define HW_SD_CMD_GPIO   38   // dentro da faixa PSRAM 33–40
#define HW_SD_D0_GPIO    40   // dentro da faixa PSRAM 33–40
```
O SD card nunca vai funcionar com esses GPIOs enquanto a PSRAM Octal estiver habilitada. Verificar no esquemático os GPIOs reais do SD na KIIRA V2.

---

## 5. Estado do Teste com Dev Kit ESP32-S3

**Hardware**: ESP32-S3 dev kit genérico com CH343 USB-Serial
- MAC: `dc:b4:d9:17:12:dc`
- CH343 → COM16 (flash only — console não sai por aqui)
- USB-JTAG nativo → COM20 (**console aqui**)

**Resultado do boot no dev kit** (observado em COM20):

```
[PASS] tcp_client inicializado        ← fix P1 funcionou
[PASS] WiFi "ITSelf" conectado        ← IP 192.168.1.236, RSSI -39
[FAIL] SD Card                        ← esperado, sem SD
[FAIL] OLED                           ← esperado, sem display
[PASS] HDC1080 (I2C init OK, leituras falham — GPIO14/21 livres no dev kit)
[FAIL] MPU6050                        ← WHO_AM_I=0x24 (errado)
[PASS] LDR
[FAIL] MAX30102                       ← esperado
[PASS] Button handler (GPIO45/46)     ← GPIOs floating, mas init OK
[SKIP] Audio                          ← desabilitado (bug P0)
[TCP]  WIFI_CONNECTING → TCP_CONNECTING → sem resposta do servidor em 192.168.1.119:7890
```

**Problema ativo**: o servidor TCP do dashboard não está acessível na rede. Ver seção 3.

---

## 6. Configuração de Build (Windows)

### Caminhos do ambiente (OBRIGATÓRIO definir todos antes de `idf.py`)

```powershell
$env:IDF_PATH            = 'C:\esp\v6.0\esp-idf'
$env:IDF_TOOLS_PATH      = 'C:\Espressif\tools'
$env:IDF_PYTHON_ENV_PATH = 'C:\Espressif\tools\python\v6.0\venv'
$env:ESP_IDF_VERSION     = '6.0'
$env:PATH = "C:\Espressif\tools\ccache\4.12.1\ccache-4.12.1-windows-x86_64;" +
            "C:\Espressif\tools\cmake\4.0.3\bin;" +
            "C:\Espressif\tools\ninja\1.12.1;" +
            "C:\Espressif\tools\xtensa-esp-elf\esp-15.2.0_20251204\xtensa-esp-elf\bin;" +
            "C:\Espressif\tools\xtensa-esp-elf\esp-15.2.0_20251204\xtensa-esp-elf\xtensa-esp-elf\bin;" +
            "C:\Espressif\tools\python\v6.0\venv\Scripts;" +
            $env:PATH

$PY  = 'C:\Espressif\tools\python\v6.0\venv\Scripts\python.exe'
$IDF = 'C:\esp\v6.0\esp-idf\tools\idf.py'
```

> ⚠️ `C:\Users\Nathan\.espressif\` **NÃO EXISTE** nesta máquina. Não usar.

### Build

```powershell
Set-Location 'C:\Users\Nathan\Documents\EmbarcaTech Residencia - Nathan\VitalKare\firmware'
& $PY $IDF build
```

### Flash KIIRA V2 (COM18, deve estar em boot mode)

```powershell
& $PY -m esptool --chip esp32s3 -p COM18 -b 460800 --before no-reset --after hard-reset `
  write-flash --flash-mode dio --flash-size 8MB --flash-freq 80m "@build/flash_args"
```

### Flash dev kit (COM16, deve estar em boot mode — hold BOOT + RESET)

```powershell
Set-Location 'C:\Users\Nathan\Documents\EmbarcaTech Residencia - Nathan\VitalKare\firmware\build'
& $PY -m esptool --chip esp32s3 -p COM16 -b 460800 --before no-reset --after hard-reset `
  write-flash --flash-mode dio --flash-size 8MB --flash-freq 80m "@flash_args"
```

### Monitor serial dev kit (COM20 = USB-JTAG nativo)

```python
# Rodar com: python -c "..."
import serial, time, sys
s = serial.Serial('COM20', 115200, timeout=0.2)
s.dtr = False; s.rts = False
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
end = time.time() + 60
while time.time() < end:
    data = s.read(512)
    if data: sys.stdout.write(data.decode('utf-8', errors='replace')); sys.stdout.flush()
    else: time.sleep(0.02)
s.close()
```

---

## 7. Próximos Passos Prioritários

| Prioridade | Tarefa |
|---|---|
| 🔴 P0 | Resolver incompatibilidade TCP raw vs WebSocket entre firmware e dashboard |
| 🔴 P0 | Descobrir GPIO correto do microfone PDM no esquemático da KIIRA V2 e corrigir `HW_MIC_DATA_GPIO` |
| 🟠 P1 | Verificar e corrigir GPIOs do SD card (`HW_SD_CLK/CMD/D0`) no esquemático |
| 🟠 P1 | Migrar filas TCP para PSRAM (`xQueueCreateStatic` + `MALLOC_CAP_SPIRAM`) e restaurar `NORMAL_QUEUE_DEPTH=32` |
| 🟡 P2 | Suprimir spam de warnings do MAX30102 quando sensor não está presente |
| 🟡 P2 | Atualizar `CLAUDE_BUILD_FLASH_MONITOR.md` com variáveis de ambiente corretas |
