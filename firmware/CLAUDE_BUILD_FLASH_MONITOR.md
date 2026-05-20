# Guia de Build, Flash e Monitor — ESP-IDF no VitalKare

> **Para uso exclusivo de agentes de IA (Claude, Copilot, etc.)**  
> Este arquivo documenta os caminhos e comandos corretos para este ambiente Windows.

---

## Caminhos do ambiente

| Item | Caminho |
|---|---|
| IDF_PATH | `C:\esp\v6.0\esp-idf` |
| Python (venv) | `C:\Espressif\tools\python\v6.0\venv\Scripts\python.exe` |
| idf.py | `C:\esp\v6.0\esp-idf\tools\idf.py` |
| idf_monitor.py | `C:\esp\v6.0\esp-idf\tools\idf_monitor.py` |
| Porta serial | `COM18` |
| Baud rate | `115200` |
| ELF do firmware | `build\vitalkare-firmware.elf` |

> **ATENÇÃO**: `C:\Users\Nathan\.espressif\` **NÃO EXISTE** nesta máquina.  
> O venv está em `C:\Espressif\tools\python\v6.0\venv\`.

---

## Método 1 — Ferramenta `espIdfCommands` (preferido no Copilot)

O VS Code tem a extensão ESP-IDF configurada. Use a ferramenta nativa:

```
espIdfCommands(command: "build")
espIdfCommands(command: "flash")
espIdfCommands(command: "monitor")
```

> Ela usa automaticamente os caminhos do `settings.json` (porta `COM18`, IDF em `C:\esp\v6.0`).

**Fluxo completo:**
1. `espIdfCommands(command: "build")`
2. Se a porta COM18 estiver ocupada (erro `PermissionError`), execute o passo de liberação abaixo.
3. `espIdfCommands(command: "flash")`
4. `espIdfCommands(command: "monitor")`

---

## Método 2 — PowerShell direto (fallback)

### Build

```powershell
$env:IDF_PATH = 'C:\esp\v6.0\esp-idf'
& 'C:\Espressif\tools\python\v6.0\venv\Scripts\python.exe' `
    'C:\esp\v6.0\esp-idf\tools\idf.py' build
```

### Flash (dispositivo em modo boot — GPIO0 pressionado)

```powershell
$env:IDF_PATH = 'C:\esp\v6.0\esp-idf'
& 'C:\Espressif\tools\python\v6.0\venv\Scripts\python.exe' `
    'C:\esp\v6.0\esp-idf\tools\idf.py' -p COM18 flash
```

### Monitor serial

```powershell
$env:IDF_PATH = 'C:\esp\v6.0\esp-idf'
& 'C:\Espressif\tools\python\v6.0\venv\Scripts\python.exe' `
    "$env:IDF_PATH\tools\idf_monitor.py" `
    --port COM18 `
    "C:\Users\Nathan\Documents\EmbarcaTech Residencia - Nathan\VitalKare\firmware\build\vitalkare-firmware.elf"
```

> **IMPORTANTE**: Ao usar `idf_monitor.py`, sempre defina `$env:IDF_PATH` **antes** e passe o caminho do `.elf` como último argumento. Sem o IDF_PATH definido, o monitor falha com exit code 1.

### Build + Flash + Monitor em sequência

```powershell
$env:IDF_PATH = 'C:\esp\v6.0\esp-idf'
$PY = 'C:\Espressif\tools\python\v6.0\venv\Scripts\python.exe'
$IDF = 'C:\esp\v6.0\esp-idf\tools\idf.py'
$ELF = 'C:\Users\Nathan\Documents\EmbarcaTech Residencia - Nathan\VitalKare\firmware\build\vitalkare-firmware.elf'

& $PY $IDF build
& $PY $IDF -p COM18 flash
& $PY "$env:IDF_PATH\tools\idf_monitor.py" --port COM18 $ELF
```

---

## Liberar porta COM18 (se ocupada)

Se o flash falhar com `PermissionError(13, 'Acesso negado.')`, algum processo Python está segurando a porta. Execute:

```powershell
Get-Process python* -ErrorAction SilentlyContinue | ForEach-Object {
    $id = $_.Id
    $cmd = (Get-CimInstance Win32_Process -Filter "ProcessId=$id" -EA SilentlyContinue).CommandLine
    if ($cmd -like '*COM18*') { Stop-Process -Id $id -Force; Write-Host "Killed $id" }
}
```

---

## Hardware de referência

| Item | Valor |
|---|---|
| SoC | ESP32-S3 (QFN56, rev v0.2) |
| Placa | KIIRA V2 (custom) |
| PSRAM | Octal 8MB AP_3v3 |
| PSRAM GPIOs reservados | GPIO33–40 (DQ0–DQ7), GPIO30 (CLK), GPIO26 (CS) — **nunca usar estes GPIOs** |
| Flash | 16MB |
| USB-JTAG | Embutido (USB-Serial/JTAG) |
| MAC | 98:88:e0:11:25:50 |

---

## Problema conhecido — Boot loop por GPIO37

O firmware possui um bug crítico em `config/hardware_config.h`:

```c
#define HW_MIC_DATA_GPIO   37   // ← ERRADO! GPIO37 = OPI PSRAM DQ4/5
```

O ESP32-S3 com PSRAM Octal usa GPIO33–40 internamente para os pinos DQ do PSRAM.  
Ao tentar configurar GPIO37 como entrada PDM via `i2s_channel_init_pdm_rx_mode()`, o driver do IDF detecta o conflito e chama `abort()`, causando `TG1WDT_SYS_RST` em loop.

**Fix**: alterar `HW_MIC_DATA_GPIO` para o GPIO correto do microfone PDM no esquemático do KIIRA V2 e recompilar.
