@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"

echo.
echo  =========================================
echo   VitalKare MVP - Backend Server
echo  =========================================
echo.

:: ── Check ports 7890 and 8000 already in use ───────────────────
set PORT_BUSY=0
set PID_7890=
set PID_8000=

for /f "tokens=5" %%p in ('netstat -ano 2^>nul ^| findstr ":7890 " ^| findstr "LISTENING"') do set PID_7890=%%p
for /f "tokens=5" %%p in ('netstat -ano 2^>nul ^| findstr ":8000 " ^| findstr "LISTENING"') do set PID_8000=%%p

if defined PID_7890 set PORT_BUSY=1
if defined PID_8000 set PORT_BUSY=1

if "%PORT_BUSY%"=="1" (
    echo [AVISO] Uma ou mais portas ja estao em uso:
    if defined PID_7890 echo          Porta 7890 - PID %PID_7890%
    if defined PID_8000 echo          Porta 8000 - PID %PID_8000%
    echo.
    choice /c SN /m "Encerrar os processos e continuar? S=Sim N=Cancelar"
    if errorlevel 2 (
        echo Operacao cancelada.
        pause
        exit /b 0
    )
    if defined PID_7890 (
        taskkill /PID %PID_7890% /F >nul 2>&1
        echo [OK] Processo %PID_7890% encerrado - porta 7890 liberada.
    )
    if defined PID_8000 (
        taskkill /PID %PID_8000% /F >nul 2>&1
        echo [OK] Processo %PID_8000% encerrado - porta 8000 liberada.
    )
    timeout /t 1 /nobreak >nul
    echo.
)

:: ── Check Python ────────────────────────────────────────────────
python --version >nul 2>&1
if errorlevel 1 (
    echo [ERRO] Python nao encontrado no PATH.
    echo        Instale Python 3.11+ em https://www.python.org/downloads/
    echo        Marque a opcao "Add Python to PATH" durante a instalacao.
    echo.
    pause
    exit /b 1
)

for /f "tokens=2 delims= " %%v in ('python --version 2^>^&1') do set PY_VER=%%v
echo [OK] Python %PY_VER% encontrado.

:: ── Create virtual environment ───────────────────────────────────
if not exist "venv\Scripts\python.exe" (
    echo [..] Criando ambiente virtual...
    python -m venv venv
    if errorlevel 1 (
        echo [ERRO] Falha ao criar ambiente virtual.
        pause
        exit /b 1
    )
    echo [OK] Ambiente virtual criado.
) else (
    echo [OK] Ambiente virtual existente encontrado.
)

:: ── Install / update dependencies ───────────────────────────────
echo [..] Instalando dependencias Python...
"venv\Scripts\pip.exe" install -q -r requirements.txt
if errorlevel 1 (
    echo [ERRO] Falha ao instalar dependencias.
    pause
    exit /b 1
)
echo [OK] Dependencias instaladas.

:: ── Print connection info ────────────────────────────────────────
echo.
echo  -----------------------------------------
echo   Servidor iniciando...
echo  -----------------------------------------
echo   HTTP / REST  : http://localhost:8000
echo   Swagger UI   : http://localhost:8000/docs
echo   TCP (VK)     : 0.0.0.0:7890
echo   WebSocket    : ws://localhost:8000/ws/dashboard
echo   Banco de dados: vitalkare_mvp.db (gerado automaticamente)
echo  -----------------------------------------
echo   Login admin  : admin / admin
echo  -----------------------------------------
echo.
echo  Pressione Ctrl+C para parar o servidor.
echo.

:: ── Start server ─────────────────────────────────────────────────
"venv\Scripts\python.exe" server.py

echo.
echo  Servidor encerrado.
pause
