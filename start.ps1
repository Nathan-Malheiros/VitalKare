# VitalKare MVP - Startup Script (Windows PowerShell)
# Usage: .\start.ps1
#
# Automatically detects this machine's WiFi IP and updates
# firmware/config/device_config.h if the IP has changed,
# then starts the backend server and (optionally) the dashboard.

$root       = Split-Path -Parent $MyInvocation.MyCommand.Path
$serverDir  = Join-Path $root 'server'
$dashDir    = Join-Path $root 'dashboard'
$configFile = Join-Path $root 'firmware\config\device_config.h'

Write-Host ''
Write-Host '=======================================' -ForegroundColor Cyan
Write-Host '  VitalKare MVP - Startup' -ForegroundColor Cyan
Write-Host '=======================================' -ForegroundColor Cyan
Write-Host ''

# ── 1. Detect this machine's IP on the local network ─────────────────
$myIP = $null

# Prefer DHCP addresses on physical (non-virtual) adapters
$candidates = Get-NetIPAddress -AddressFamily IPv4 |
    Where-Object {
        $_.PrefixOrigin -eq 'Dhcp' -and
        $_.IPAddress -notlike '169.254.*' -and
        $_.IPAddress -notlike '127.*'
    } | Sort-Object -Property InterfaceAlias

# Pick the first Wi-Fi / Ethernet adapter; skip VirtualBox, WSL, VMware
foreach ($addr in $candidates) {
    $iface = $addr.InterfaceAlias
    if ($iface -notmatch 'Virtual|Loopback|WSL|vEthernet|VMware|Bluetooth') {
        $myIP = $addr.IPAddress
        Write-Host "  IP detectado : $myIP  ($iface)" -ForegroundColor Green
        break
    }
}

if (-not $myIP) {
    Write-Host '[AVISO] Nao foi possivel detectar o IP automaticamente.' -ForegroundColor Yellow
    Write-Host '        Verifique com  ipconfig  e confirme o IP abaixo.' -ForegroundColor Yellow
    $myIP = Read-Host '  Digite o IP desta maquina'
}

# ── 2. Read current DEVICE_SERVER_HOST from device_config.h ──────────
$firmwareIP = $null
if (Test-Path $configFile) {
    $content   = Get-Content $configFile -Raw
    $m         = [regex]::Match($content, '#define\s+DEVICE_SERVER_HOST\s+"([^"]+)"')
    if ($m.Success) { $firmwareIP = $m.Groups[1].Value }
}

Write-Host "  IP no firmware: $firmwareIP" -ForegroundColor $(if ($firmwareIP -eq $myIP) { 'Green' } else { 'Yellow' })

# ── 3. Update device_config.h if IP changed ───────────────────────────
$needRebuild = $false
if ($myIP -and $firmwareIP -and ($myIP -ne $firmwareIP)) {
    Write-Host ''
    Write-Host "  [ATENCAO] IP mudou: $firmwareIP -> $myIP" -ForegroundColor Yellow
    Write-Host '  Atualizando firmware/config/device_config.h...' -ForegroundColor Yellow

    $updated = $content `
        -replace '#define\s+DEVICE_SERVER_HOST\s+"[^"]+"',  "#define DEVICE_SERVER_HOST      `"$myIP`"" `
        -replace '#define\s+DEVICE_HTTP_BASE_URL\s+"[^"]+"', "#define DEVICE_HTTP_BASE_URL    `"http://${myIP}:8000`""

    Set-Content -Path $configFile -Value $updated -NoNewline
    Write-Host "  [OK] device_config.h atualizado com $myIP" -ForegroundColor Green
    $needRebuild = $true

} elseif ($myIP -eq $firmwareIP) {
    Write-Host '  [OK] IP do firmware ja esta correto — nenhuma alteracao necessaria.' -ForegroundColor Green
}

if ($needRebuild) {
    Write-Host ''
    Write-Host '  *** ATENCAO: O IP do servidor mudou! ***' -ForegroundColor Red
    Write-Host '  Voce precisa recompilar e gravar o firmware:' -ForegroundColor Red
    Write-Host '    1. Abra o assistente:  .\firmware-assistant.bat' -ForegroundColor White
    Write-Host '    2. Opcao [8] Fullclean' -ForegroundColor White
    Write-Host '    3. Opcao [5] Build' -ForegroundColor White
    Write-Host '    4. Opcao [6] Flash' -ForegroundColor White
}

# ── 4. Check ports ────────────────────────────────────────────────────
Write-Host ''
$portsBusy = @()
$netstat = netstat -ano 2>$null
foreach ($port in @(7890, 8000)) {
    $match = $netstat | Select-String ":$port\s" | Select-String 'LISTENING' | Select-Object -First 1
    if ($match) {
        $pid_ = ($match -split '\s+')[-1]
        $portsBusy += [PSCustomObject]@{ Port = $port; PID = $pid_ }
    }
}
if ($portsBusy.Count -gt 0) {
    Write-Host '[AVISO] Portas ja em uso:' -ForegroundColor Yellow
    foreach ($b in $portsBusy) {
        Write-Host "        Porta $($b.Port) - PID $($b.PID)" -ForegroundColor Yellow
    }
    Write-Host ''
    $ans = Read-Host 'Encerrar os processos e continuar? (s/n)'
    if ($ans -notmatch '^[sS]$') { Write-Host 'Cancelado.'; exit 0 }
    foreach ($b in $portsBusy) {
        Stop-Process -Id $b.PID -Force -ErrorAction SilentlyContinue
        Write-Host "[OK] Processo $($b.PID) encerrado (porta $($b.Port))." -ForegroundColor Green
    }
    Start-Sleep -Seconds 1
}

# ── 5. Windows Firewall — open inbound ports 7890 and 8000 ───────────
$fwPorts = @(
    @{ Port = 7890; Name = 'VitalKare TCP 7890 (ESP32 devices)' },
    @{ Port = 8000; Name = 'VitalKare HTTP 8000 (API / WebSocket)' }
)
foreach ($fw in $fwPorts) {
    $existing = Get-NetFirewallRule -DisplayName $fw.Name -ErrorAction SilentlyContinue
    if (-not $existing) {
        try {
            New-NetFirewallRule -DisplayName $fw.Name `
                -Direction Inbound -Protocol TCP `
                -LocalPort $fw.Port -Action Allow `
                -Profile Any -ErrorAction Stop | Out-Null
            Write-Host "  [OK] Regra de firewall criada: porta $($fw.Port)" -ForegroundColor Green
        } catch {
            Write-Host "  [AVISO] Nao foi possivel criar regra de firewall para porta $($fw.Port)." -ForegroundColor Yellow
            Write-Host '          Execute start.ps1 como Administrador para aplicar automaticamente.' -ForegroundColor Yellow
        }
    } else {
        Write-Host "  [OK] Firewall: porta $($fw.Port) ja liberada." -ForegroundColor DarkGray
    }
}
Write-Host ''

# ── 6. Check Python ───────────────────────────────────────────────────
if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
    Write-Host '[ERRO] Python nao encontrado no PATH.' -ForegroundColor Red
    pause; exit 1
}

# ── 7. Prepare venv and install deps ─────────────────────────────────
Write-Host '[..] Preparando backend Python...' -ForegroundColor Yellow
Set-Location $serverDir

if (-not (Test-Path 'venv\Scripts\python.exe')) {
    python -m venv venv
    if ($LASTEXITCODE -ne 0) {
        Write-Host '[ERRO] Falha ao criar venv.' -ForegroundColor Red; pause; exit 1
    }
}

& "$serverDir\venv\Scripts\pip.exe" install -q -r requirements.txt
if ($LASTEXITCODE -ne 0) {
    Write-Host '[ERRO] Falha ao instalar dependencias Python.' -ForegroundColor Red; pause; exit 1
}
Write-Host '[OK] Backend pronto.' -ForegroundColor Green

# ── 8. Start backend server ───────────────────────────────────────────
Start-Process powershell -ArgumentList '-NoExit', '-Command', `
    "Set-Location '$serverDir'; Write-Host 'VitalKare Backend' -ForegroundColor Cyan; & '$serverDir\venv\Scripts\python.exe' server.py" `
    -WindowStyle Normal

Write-Host '[OK] Servidor TCP/HTTP iniciado.' -ForegroundColor Green
Start-Sleep -Seconds 2

# ── 9. Optionally start dashboard ─────────────────────────────────────
if (Get-Command node -ErrorAction SilentlyContinue) {
    $ans = Read-Host 'Iniciar dashboard React? (s/n)'
    if ($ans -match '^[sS]$') {
        Set-Location $dashDir
        npm install --prefer-offline 2>&1 | Out-Null
        Start-Process powershell -ArgumentList '-NoExit', '-Command', `
            "Set-Location '$dashDir'; npm run dev" -WindowStyle Normal
        Write-Host '[OK] Dashboard iniciado.' -ForegroundColor Green
    }
} else {
    Write-Host '[INFO] Node.js nao encontrado — dashboard nao iniciado.' -ForegroundColor DarkGray
}

# ── 10. Summary ───────────────────────────────────────────────────────
Write-Host ''
Write-Host '=======================================' -ForegroundColor Cyan
Write-Host '  VitalKare - Servicos ativos' -ForegroundColor Cyan
Write-Host '=======================================' -ForegroundColor Cyan
Write-Host "  Backend TCP  : ${myIP}:7890  (dispositivos ESP32)" -ForegroundColor White
Write-Host "  Dashboard    : http://localhost:5173" -ForegroundColor White
Write-Host "  API / Swagger: http://${myIP}:8000/docs" -ForegroundColor White
Write-Host "  Admin login  : admin / admin" -ForegroundColor White
Write-Host ''
if ($needRebuild) {
    Write-Host '  *** Rebuild necessario! Abra firmware-assistant.bat ***' -ForegroundColor Red
} else {
    Write-Host "  Firmware configurado para: $myIP — nenhum rebuild necessario." -ForegroundColor Green
}
Write-Host ''

Set-Location $root
