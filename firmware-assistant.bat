@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT_DIR=%~dp0"
set "FW_DIR=%ROOT_DIR%firmware"
set "BOARD_FILE=%FW_DIR%\config\board_profile.h"
set "COM_PORT="
set "TARGET_DEFINE="
set "TARGET_NAME="
set "LAST_RESULT=[INFO] Nenhuma acao executada ainda."
set "LAST_ACTION_LABEL="
set "LAST_COMMAND="
set "LAST_EXIT_CODE=0"

if not exist "%FW_DIR%\CMakeLists.txt" (
    echo [ERRO] Nao encontrei a pasta firmware ao lado deste .bat.
    echo        Execute este script a partir da raiz do repositorio VitalKare.
    pause
    exit /b 1
)

if not exist "%BOARD_FILE%" (
    echo [ERRO] Nao encontrei %BOARD_FILE%.
    pause
    exit /b 1
)

call :detect_idf
if not defined IDF_PATH (
    echo [ERRO] Nao foi possivel localizar o ESP-IDF em C:\esp\.
    echo        Instale o ESP-IDF e tente novamente.
    pause
    exit /b 1
)

set "IDF_EXPORT=%IDF_PATH%\export.ps1"
if not exist "%IDF_EXPORT%" (
    echo [ERRO] Nao encontrei %IDF_EXPORT%.
    pause
    exit /b 1
)

call :load_current_target
goto :main_menu

:main_menu
cls
call :print_header
echo.
echo -------------------- CONFIGURACOES --------------------
echo [1] Selecionar placa alvo
echo [2] Definir porta COM
echo [3] Listar portas seriais
echo [4] Mostrar ajuda de BOOT/RESET
echo [S] Mostrar status detalhado
echo.
echo ------------------------ ACOES ------------------------
echo [5] Build
echo [6] Flash
echo [7] Monitor
echo [8] Fullclean
echo [9] Fluxo guiado (sequencia)
echo [T] Repetir ultimo comando
echo [R] Recarregar ESP-IDF
echo [0] Sair
echo.
choice /C 1234S56789TR0 /N /M "Opcao: "
if errorlevel 13 goto :exit_script
if errorlevel 12 goto :reload_idf
if errorlevel 11 goto :repeat_last
if errorlevel 10 goto :guided_flow
if errorlevel 9 goto :run_fullclean
if errorlevel 8 goto :run_monitor
if errorlevel 7 goto :run_flash
if errorlevel 6 goto :run_build
if errorlevel 5 goto :status_screen
if errorlevel 4 goto :show_boot_help
if errorlevel 3 goto :list_ports_menu
if errorlevel 2 goto :set_com_menu
if errorlevel 1 goto :set_target_menu
goto :main_menu

:print_header
echo ============================================================
echo   VitalKare - Assistente de Firmware
echo ============================================================
echo Placa ativa : !TARGET_NAME!
if defined COM_PORT (
    echo Porta COM   : !COM_PORT!
) else (
    echo Porta COM   : ^(nao definida^)
)
echo ESP-IDF     : %IDF_PATH%
echo Ultimo res. : !LAST_RESULT!
exit /b 0

:status_screen
cls
call :print_header
echo.
echo -------------------- STATUS DETALHADO --------------------
echo Board define : !TARGET_DEFINE!
if defined LAST_ACTION_LABEL (
    echo Ultima acao  : !LAST_ACTION_LABEL!
) else (
    echo Ultima acao  : ^(nenhuma^)
)
if defined LAST_COMMAND (
    echo Ult. comando : !LAST_COMMAND!
) else (
    echo Ult. comando : ^(nenhum^)
)
echo Exit code    : !LAST_EXIT_CODE!
echo Arquivo alvo : %BOARD_FILE%
echo.
pause
goto :main_menu

:set_target_menu
cls
call :print_header
echo.
echo Escolha a placa alvo:
echo   [1] KIIRA V2          - firmware completo
echo   [2] ESP32-S3 DevKit   - firmware reduzido para bancada
echo.
choice /C 12 /N /M "Opcao: "
if errorlevel 2 (
    set "TARGET_DEFINE=VK_BOARD_PROFILE_ESP32S3_DEVKIT"
    set "TARGET_NAME=ESP32-S3 DevKit"
) else (
    set "TARGET_DEFINE=VK_BOARD_PROFILE_KIIRA_V2"
    set "TARGET_NAME=KIIRA V2"
)
call :apply_target
if errorlevel 1 (
    call :set_last_result "ERRO" "Falha ao atualizar o perfil da placa." "1"
) else (
    call :set_last_result "OK" "Perfil configurado para !TARGET_NAME!." "0"
)
echo.
pause
goto :main_menu

:set_com_menu
cls
call :print_header
echo.
call :ask_com_port
if errorlevel 1 (
    call :set_last_result "INFO" "Alteracao de porta COM cancelada." "0"
)
echo.
pause
goto :main_menu

:list_ports_menu
cls
call :print_header
echo.
call :list_ports
echo.
pause
goto :main_menu

:show_boot_help
cls
call :print_header
echo.
echo Coloque a placa em modo download assim:
echo   1. Segure o botao BOOT.
echo   2. Aperte e solte RESET.
echo   3. Aguarde 1-2 segundos.
echo   4. Solte o BOOT.
echo.
echo Dica: se o flash falhar, volte ao menu e tente novamente
echo       depois de repetir esse procedimento.
echo.
pause
goto :main_menu

:run_build
call :execute_idf "Build" "idf.py build"
call :wait_and_return
goto :main_menu

:run_flash
call :ensure_com_port
if errorlevel 1 goto :main_menu
call :prepare_for_flash
if errorlevel 1 goto :main_menu
call :execute_idf "Flash" "idf.py -p !COM_PORT! flash"
call :wait_and_return
goto :main_menu

:run_monitor
call :ensure_com_port
if errorlevel 1 goto :main_menu
call :execute_idf "Monitor" "idf.py -p !COM_PORT! monitor"
call :wait_and_return
goto :main_menu

:run_fullclean
call :execute_idf "Fullclean" "idf.py fullclean"
call :wait_and_return
goto :main_menu

:guided_flow
cls
call :print_header
echo.
echo O fluxo guiado permite rodar uma sequencia e voltar ao menu se algo falhar.
echo.
call :ask_yes_no "Executar fullclean antes do build" "N"
set "FLOW_FULLCLEAN=!ANSWER!"
call :ask_yes_no "Executar build" "S"
set "FLOW_BUILD=!ANSWER!"
call :ask_yes_no "Executar flash" "S"
set "FLOW_FLASH=!ANSWER!"
call :ask_yes_no "Abrir monitor ao final" "S"
set "FLOW_MONITOR=!ANSWER!"
if /I "!FLOW_FLASH!"=="S" (
    call :ensure_com_port
    if errorlevel 1 goto :main_menu
)
echo.
echo Resumo do fluxo:
echo   Fullclean : !FLOW_FULLCLEAN!
echo   Build     : !FLOW_BUILD!
echo   Flash     : !FLOW_FLASH!
echo   Monitor   : !FLOW_MONITOR!
echo.
pause

if /I "!FLOW_FULLCLEAN!"=="S" (
    call :execute_idf "Fullclean" "idf.py fullclean"
    if errorlevel 1 goto :guided_flow_end
)

if /I "!FLOW_BUILD!"=="S" (
    call :execute_idf "Build" "idf.py build"
    if errorlevel 1 goto :guided_flow_end
)

if /I "!FLOW_FLASH!"=="S" (
    call :prepare_for_flash
    if errorlevel 1 goto :guided_flow_end
    call :execute_idf "Flash" "idf.py -p !COM_PORT! flash"
    if errorlevel 1 goto :guided_flow_end
)

if /I "!FLOW_MONITOR!"=="S" (
    call :execute_idf "Monitor" "idf.py -p !COM_PORT! monitor"
)

:guided_flow_end
call :wait_and_return
goto :main_menu

:repeat_last
if not defined LAST_COMMAND (
    call :set_last_result "INFO" "Nenhum comando anterior disponivel para repetir." "0"
    call :wait_and_return
    goto :main_menu
)
call :execute_idf "!LAST_ACTION_LABEL!" "!LAST_COMMAND!"
call :wait_and_return
goto :main_menu

:reload_idf
call :detect_idf
if not defined IDF_PATH (
    call :set_last_result "ERRO" "Nao foi possivel relocalizar o ESP-IDF." "1"
    call :wait_and_return
    goto :main_menu
)
set "IDF_EXPORT=%IDF_PATH%\export.ps1"
if not exist "%IDF_EXPORT%" (
    call :set_last_result "ERRO" "O arquivo export.ps1 nao foi encontrado no ESP-IDF localizado." "1"
    call :wait_and_return
    goto :main_menu
)
call :set_last_result "OK" "ESP-IDF recarregado com sucesso." "0"
call :wait_and_return
goto :main_menu

:load_current_target
set "TARGET_DEFINE="
for /f "usebackq delims=" %%I in (`powershell -NoLogo -NoProfile -ExecutionPolicy Bypass -Command "$m = Select-String -Path '%BOARD_FILE%' -Pattern '^\s*#define\s+VK_BOARD_PROFILE\s+(VK_BOARD_PROFILE_[A-Z0-9_]+)' | Select-Object -First 1; if ($m) { $m.Matches[0].Groups[1].Value }"`) do (
    set "TARGET_DEFINE=%%I"
)
if /I "!TARGET_DEFINE!"=="VK_BOARD_PROFILE_ESP32S3_DEVKIT" (
    set "TARGET_NAME=ESP32-S3 DevKit"
) else (
    set "TARGET_DEFINE=VK_BOARD_PROFILE_KIIRA_V2"
    set "TARGET_NAME=KIIRA V2"
)
exit /b 0

:detect_idf
set "IDF_PATH="
for /f "usebackq delims=" %%I in (`powershell -NoLogo -NoProfile -ExecutionPolicy Bypass -Command "$idf = Get-ChildItem 'C:\esp' -Directory -Filter 'v*' -ErrorAction SilentlyContinue | Sort-Object Name -Descending | ForEach-Object { Join-Path $_.FullName 'esp-idf' } | Where-Object { Test-Path (Join-Path $_ 'export.ps1') } | Select-Object -First 1; if ($idf) { $idf }"`) do (
    set "IDF_PATH=%%I"
)
exit /b 0

:apply_target
echo.
echo [INFO] Ajustando board_profile.h para !TARGET_NAME!...
powershell -NoLogo -NoProfile -ExecutionPolicy Bypass -Command ^
  "$path = '%BOARD_FILE%';" ^
  "$content = [System.IO.File]::ReadAllText($path);" ^
  "$updated = [System.Text.RegularExpressions.Regex]::Replace($content, '#define VK_BOARD_PROFILE\s+VK_BOARD_PROFILE_[A-Z0-9_]+', '#define VK_BOARD_PROFILE !TARGET_DEFINE!', 1);" ^
  "if ($updated -ne $content) { [System.IO.File]::WriteAllText($path, $updated) }" ^
  "exit 0"
exit /b %ERRORLEVEL%

:list_ports
echo Portas seriais encontradas:
powershell -NoLogo -NoProfile -ExecutionPolicy Bypass -Command ^
  "$ports = Get-CimInstance Win32_SerialPort -ErrorAction SilentlyContinue; if (-not $ports) { Write-Host '  (nenhuma porta serial encontrada)' } else { $ports | Sort-Object DeviceID | ForEach-Object { Write-Host ('  ' + $_.DeviceID + ' - ' + $_.Name) } }"
exit /b 0

:ask_com_port
echo Digite a porta COM desejada ^(ex: COM18 ou apenas 18^).
echo Digite X para cancelar.
echo.
:ask_com_port_loop
set "RAW_COM="
set /p RAW_COM=Porta COM: 
set "RAW_COM=!RAW_COM: =!"
if not defined RAW_COM (
    echo Porta COM invalida. Tente novamente.
    goto :ask_com_port_loop
)
if /I "!RAW_COM!"=="X" exit /b 1
if /I not "!RAW_COM:~0,3!"=="COM" set "RAW_COM=COM!RAW_COM!"
set "COM_PORT=!RAW_COM!"
call :set_last_result "OK" "Porta configurada para !COM_PORT!." "0"
exit /b 0

:ensure_com_port
if defined COM_PORT exit /b 0
echo.
echo [INFO] Nenhuma porta COM configurada.
call :ask_com_port
exit /b %ERRORLEVEL%

:prepare_for_flash
echo.
call :ask_yes_no "A placa ja esta em modo download/boot" "N"
if /I "!ANSWER!"=="N" (
    call :show_flash_steps
    call :ask_yes_no "Deseja continuar para o flash agora" "S"
    if /I "!ANSWER!"=="N" (
        call :set_last_result "INFO" "Flash cancelado para voce preparar a placa." "0"
        exit /b 1
    )
)
exit /b 0

:show_flash_steps
echo.
echo Passos para entrar em modo download:
echo   1. Segure BOOT.
echo   2. Aperte e solte RESET.
echo   3. Aguarde 1-2 segundos.
echo   4. Solte BOOT.
echo.
exit /b 0

:ask_yes_no
set "QUESTION=%~1"
set "DEFAULT=%~2"
:ask_yes_no_loop
set "ANSWER="
set /p "RAW=!QUESTION! [S/N] (padrao: !DEFAULT!): "
if not defined RAW (
    set "ANSWER=!DEFAULT!"
    exit /b 0
)
set "RAW=!RAW: =!"
if /I "!RAW!"=="S" set "ANSWER=S"
if /I "!RAW!"=="N" set "ANSWER=N"
if defined ANSWER exit /b 0
echo Resposta invalida. Use S ou N.
goto :ask_yes_no_loop

:execute_idf
set "ACTION_LABEL=%~1"
set "IDF_ACTION=%~2"
set "LAST_ACTION_LABEL=!ACTION_LABEL!"
set "LAST_COMMAND=!IDF_ACTION!"
echo.
echo ============================================================
echo [EXEC] !IDF_ACTION!
echo ============================================================
powershell -NoLogo -NoProfile -ExecutionPolicy Bypass -Command ^
  "& { Set-Location -LiteralPath '%FW_DIR%'; . '%IDF_EXPORT%'; !IDF_ACTION! }"
set "IDF_EXIT=!ERRORLEVEL!"
if "!IDF_EXIT!"=="0" (
    call :set_last_result "OK" "!ACTION_LABEL! concluido com sucesso." "!IDF_EXIT!"
) else (
    call :set_last_result "ERRO" "!ACTION_LABEL! falhou. Veja a saida acima e escolha o proximo passo no menu." "!IDF_EXIT!"
)
exit /b !IDF_EXIT!

:set_last_result
set "LAST_EXIT_CODE=%~3"
set "LAST_RESULT=[%~1] %~2"
exit /b 0

:wait_and_return
echo.
echo !LAST_RESULT!
echo.
pause
exit /b 0

:exit_script
echo.
echo [INFO] Encerrando o assistente.
exit /b 0
