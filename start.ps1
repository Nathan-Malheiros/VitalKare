# VitalKare MVP - Startup Script (Windows PowerShell)
# Usage: .\start.ps1

$root      = Split-Path -Parent $MyInvocation.MyCommand.Path
$serverDir = Join-Path $root 'server'
$dashDir   = Join-Path $root 'dashboard'

Write-Host ''
Write-Host '=======================================' -ForegroundColor Cyan
Write-Host '  VitalKare MVP - Starting All Services' -ForegroundColor Cyan
Write-Host '=======================================' -ForegroundColor Cyan
Write-Host ''

# ── Check Python ──────────────────────────────────────────────────
if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
    Write-Host '[ERRO] Python nao encontrado.' -ForegroundColor Red
    Write-Host '       Instale Python 3.11+ em https://www.python.org/downloads/' -ForegroundColor Red
    Write-Host '       Marque a opcao "Add Python to PATH" durante a instalacao.' -ForegroundColor Red
    pause; exit 1
}

# ── Check Node.js ─────────────────────────────────────────────────
if (-not (Get-Command node -ErrorAction SilentlyContinue)) {
    Write-Host '[ERRO] Node.js nao encontrado.' -ForegroundColor Red
    Write-Host '       Instale Node.js 18+ em https://nodejs.org/' -ForegroundColor Red
    pause; exit 1
}

# ── Check ports 7890 and 8000 ─────────────────────────────────────
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
    if ($ans -notmatch '^[sS]$') {
        Write-Host 'Operacao cancelada.' ; exit 0
    }
    foreach ($b in $portsBusy) {
        Stop-Process -Id $b.PID -Force -ErrorAction SilentlyContinue
        Write-Host "[OK] Processo $($b.PID) encerrado (porta $($b.Port))." -ForegroundColor Green
    }
    Start-Sleep -Seconds 1
    Write-Host ''
}

# ── 1. Backend server ─────────────────────────────────────────────
Write-Host '[1/2] Preparando backend Python...' -ForegroundColor Yellow
Set-Location $serverDir

if (-not (Test-Path 'venv\Scripts\python.exe')) {
    Write-Host '      Criando ambiente virtual...'
    python -m venv venv
    if ($LASTEXITCODE -ne 0) {
        Write-Host '[ERRO] Falha ao criar venv.' -ForegroundColor Red ; pause; exit 1
    }
}

Write-Host '      Instalando dependencias...'
& "$serverDir\venv\Scripts\pip.exe" install -q -r requirements.txt
if ($LASTEXITCODE -ne 0) {
    Write-Host '[ERRO] Falha ao instalar dependencias Python.' -ForegroundColor Red ; pause; exit 1
}
Write-Host '      [OK] Backend pronto.' -ForegroundColor Green

Start-Process powershell -ArgumentList '-NoExit', '-Command', "Set-Location '$serverDir'; & '$serverDir\venv\Scripts\python.exe' server.py" -WindowStyle Normal
Write-Host '      Servidor iniciado em http://localhost:8000' -ForegroundColor Green
Write-Host ''

Start-Sleep -Seconds 2

# ── 2. Dashboard ──────────────────────────────────────────────────
Write-Host '[2/2] Preparando dashboard React...' -ForegroundColor Yellow
Set-Location $dashDir

Write-Host '      Instalando dependencias npm...'
npm install --prefer-offline 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) {
    Write-Host '[AVISO] npm install retornou erro - tentando continuar...' -ForegroundColor Yellow
}
Write-Host '      [OK] Dashboard pronto.' -ForegroundColor Green

Start-Process powershell -ArgumentList '-NoExit', '-Command', "Set-Location '$dashDir'; npm run dev" -WindowStyle Normal
Write-Host '      Dashboard iniciado em http://localhost:5173' -ForegroundColor Green

Start-Sleep -Seconds 1

# ── Summary ───────────────────────────────────────────────────────
Write-Host ''
Write-Host '=======================================' -ForegroundColor Cyan
Write-Host '  Todos os servicos iniciados!' -ForegroundColor Cyan
Write-Host '=======================================' -ForegroundColor Cyan
Write-Host ''
Write-Host '  Dashboard : http://localhost:5173' -ForegroundColor White
Write-Host '  API docs  : http://localhost:8000/docs' -ForegroundColor White
Write-Host '  Admin     : admin / admin' -ForegroundColor White
Write-Host ''
Write-Host '  Firmware: edite APP_TCP_SERVER_IP em firmware/config/app_config.h' -ForegroundColor DarkGray
Write-Host '            com o IP do seu computador na rede local.' -ForegroundColor DarkGray
Write-Host ''

Set-Location $root
