$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$serverDir = Join-Path $root 'server'
$dashDir = Join-Path $root 'dashboard'
$firmwareDir = Join-Path $root 'firmware'
$dashboardEnv = Join-Path $dashDir '.env.local'
$provisionScript = Join-Path $firmwareDir 'tools\provision.py'
$captureScript = Join-Path $firmwareDir 'tools\capture_device_ip.py'
$script:IdfEnvironmentLoaded = $false

function Write-Section($title) {
    Write-Host ''
    Write-Host '============================================================' -ForegroundColor Cyan
    Write-Host "  $title" -ForegroundColor Cyan
    Write-Host '============================================================' -ForegroundColor Cyan
}

function Ask-YesNo($question, $default = 'S') {
    while ($true) {
        $raw = Read-Host "$question [S/N] (padrao: $default)"
        if ([string]::IsNullOrWhiteSpace($raw)) { return $default.ToUpper() -eq 'S' }
        switch ($raw.Trim().ToUpper()) {
            'S' { return $true }
            'N' { return $false }
            default { Write-Host 'Resposta invalida. Use S ou N.' -ForegroundColor Yellow }
        }
    }
}

function Ask-Value($question, $default = $null, [switch]$Required) {
    while ($true) {
        if ($null -ne $default -and $default -ne '') {
            $value = Read-Host "$question [$default]"
            if ([string]::IsNullOrWhiteSpace($value)) { return $default }
            return $value.Trim()
        }

        $value = Read-Host $question
        if (-not $Required -or -not [string]::IsNullOrWhiteSpace($value)) {
            return $value.Trim()
        }

        Write-Host 'Campo obrigatorio.' -ForegroundColor Yellow
    }
}

function Get-IdfPath {
    $candidate = Get-ChildItem 'C:\esp' -Directory -Filter 'v*' -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending |
        ForEach-Object { Join-Path $_.FullName 'esp-idf' } |
        Where-Object { Test-Path (Join-Path $_ 'export.ps1') } |
        Select-Object -First 1
    return $candidate
}

function Get-LanCandidates {
    $excluded = 'Loopback|Bluetooth|vEthernet|WSL|Virtual|VMware|Hyper-V|Tailscale|ZeroTier'
    Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
        Where-Object {
            $_.IPAddress -ne '127.0.0.1' -and
            $_.IPAddress -notlike '169.254*' -and
            $_.InterfaceAlias -notmatch $excluded
        } |
        Sort-Object InterfaceAlias, IPAddress
}

function Select-HostIp {
    $candidates = @(Get-LanCandidates)
    if ($candidates.Count -eq 0) {
        throw 'Nenhum IPv4 de LAN foi encontrado automaticamente.'
    }

    if ($candidates.Count -eq 1) {
        return $candidates[0].IPAddress
    }

    Write-Host ''
    Write-Host 'IPs locais encontrados:'
    for ($i = 0; $i -lt $candidates.Count; $i++) {
        $item = $candidates[$i]
        Write-Host ("  [{0}] {1} - {2}" -f ($i + 1), $item.IPAddress, $item.InterfaceAlias)
    }

    while ($true) {
        $raw = Read-Host 'Qual IP deve ser usado pelo servidor/dashboard'
        $index = 0
        $ok = [int]::TryParse($raw, [ref]$index)
        if ($ok) {
            if ($index -ge 1 -and $index -le $candidates.Count) {
                return $candidates[$index - 1].IPAddress
            }
        }
        Write-Host 'Opcao invalida.' -ForegroundColor Yellow
    }
}

function ConvertTo-SubnetMask($prefixLength) {
    $bytes = New-Object byte[] 4
    for ($i = 0; $i -lt $prefixLength; $i++) {
        $byteIndex = [math]::Floor($i / 8)
        $bitIndex = 7 - ($i % 8)
        $bytes[$byteIndex] = $bytes[$byteIndex] -bor (1 -shl $bitIndex)
    }
    return ([System.Net.IPAddress]::new($bytes)).ToString()
}

function Get-HostNetworkProfile($hostIp) {
    $address = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
        Where-Object { $_.IPAddress -eq $hostIp } |
        Select-Object -First 1
    if (-not $address) {
        return $null
    }

    $defaultRoute = Get-NetRoute -AddressFamily IPv4 -InterfaceIndex $address.InterfaceIndex -DestinationPrefix '0.0.0.0/0' -ErrorAction SilentlyContinue |
        Sort-Object RouteMetric, ifMetric |
        Select-Object -First 1

    return [PSCustomObject]@{
        HostIp         = $hostIp
        InterfaceAlias = $address.InterfaceAlias
        PrefixLength   = $address.PrefixLength
        Netmask        = ConvertTo-SubnetMask $address.PrefixLength
        Gateway        = $(if ($defaultRoute) { $defaultRoute.NextHop } else { $null })
    }
}

function Update-DashboardEnv($hostIp) {
    $content = @(
        "# Gerado por system-startup-assistant.ps1"
        "VITE_BACKEND_HOST=$hostIp"
        "VITE_WS_URL=ws://$hostIp`:8000"
    )
    Set-Content -Path $dashboardEnv -Value $content -Encoding ASCII
}

function Wait-HttpReady($url, $timeoutSec = 40) {
    $deadline = (Get-Date).AddSeconds($timeoutSec)
    do {
        try {
            $response = Invoke-WebRequest -Uri $url -UseBasicParsing -TimeoutSec 5
            if ($response.StatusCode -ge 200 -and $response.StatusCode -lt 500) {
                return
            }
        }
        catch {
        }
        Start-Sleep -Seconds 1
    } while ((Get-Date) -lt $deadline)

    throw "Servidor HTTP nao respondeu a tempo em $url"
}

function Get-AdminToken {
    $body = @{
        username = 'admin'
        password = 'admin'
    } | ConvertTo-Json

    $response = Invoke-RestMethod `
        -Method Post `
        -Uri 'http://127.0.0.1:8000/api/auth/login' `
        -ContentType 'application/json' `
        -Body $body

    if (-not $response.access_token) {
        throw 'Nao foi possivel obter token admin do backend.'
    }

    return $response.access_token
}

function Invoke-ServerApi {
    param(
        [Parameter(Mandatory = $true)][string]$Method,
        [Parameter(Mandatory = $true)][string]$Path,
        $Body = $null,
        [Parameter(Mandatory = $true)][string]$Token
    )

    $headers = @{ Authorization = "Bearer $Token" }
    $uri = "http://127.0.0.1:8000/api$Path"

    if ($null -eq $Body) {
        return Invoke-RestMethod -Method $Method -Uri $uri -Headers $headers
    }

    $json = $Body | ConvertTo-Json -Depth 6
    return Invoke-RestMethod `
        -Method $Method `
        -Uri $uri `
        -Headers $headers `
        -ContentType 'application/json' `
        -Body $json
}

function Wait-DeviceSeen($deviceId, $token, $timeoutSec = 30) {
    $deadline = (Get-Date).AddSeconds($timeoutSec)
    do {
        try {
            $devices = @(Invoke-ServerApi -Method Get -Path '/admin/devices' -Token $token)
            $device = $devices | Where-Object { $_.device_id -eq $deviceId } | Select-Object -First 1
            if ($device) {
                return $device
            }
        }
        catch {
        }
        Start-Sleep -Seconds 1
    } while ((Get-Date) -lt $deadline)

    return $null
}

function Sync-DeviceToBackend($deviceId, $wardId, $roomLabel, $patientName, $patientId, $deviceIp, $boardName) {
    Write-Section 'Registrando dispositivo no backend/dashboard'

    Wait-HttpReady 'http://127.0.0.1:8000/api/devices'
    $token = Get-AdminToken

    $device = Wait-DeviceSeen -deviceId $deviceId -token $token
    if (-not $device) {
        Write-Host "[AVISO] O backend ainda nao viu o dispositivo $deviceId. Aprovacao automatica ignorada por enquanto." -ForegroundColor Yellow
        return
    }

    if ($deviceIp) {
        $slots = @(Invoke-ServerApi -Method Get -Path '/admin/provisioned' -Token $token)
        $slot = $slots | Where-Object {
            $_.device_id -eq $deviceId -or
            $_.bed_label -eq $roomLabel -or
            $_.expected_ip -eq $deviceIp
        } | Select-Object -First 1

        $slotBody = @{
            bed_label    = $roomLabel
            expected_ip  = $deviceIp
            ward_id      = $wardId
            patient_name = $patientName
            notes        = "board=$boardName; device_id=$deviceId; patient_id=$patientId"
        }

        if ($slot) {
            Invoke-ServerApi -Method Put -Path "/admin/provisioned/$($slot.id)" -Body $slotBody -Token $token | Out-Null
            Write-Host "[OK] Slot provisionado atualizado para $roomLabel ($deviceIp)." -ForegroundColor Green
        }
        else {
            Invoke-ServerApi -Method Post -Path '/admin/provisioned' -Body $slotBody -Token $token | Out-Null
            Write-Host "[OK] Slot provisionado criado para $roomLabel ($deviceIp)." -ForegroundColor Green
        }
    }
    else {
        Write-Host '[AVISO] IP da placa nao foi capturado; slot por IP nao sera criado agora.' -ForegroundColor Yellow
    }

    Invoke-ServerApi -Method Post -Path "/admin/devices/$deviceId/approve" -Token $token | Out-Null
    Invoke-ServerApi -Method Patch -Path "/admin/devices/$deviceId" -Body @{
        patient_name = $patientName
        room_label   = $roomLabel
    } -Token $token | Out-Null

    Write-Host "[OK] Dispositivo $deviceId aprovado e publicado no dashboard." -ForegroundColor Green
}

function Stop-BusyPorts {
    $ports = @(7890, 8000, 5173)
    $busy = @()
    $netstat = netstat -ano 2>$null
    foreach ($port in $ports) {
        $match = $netstat | Select-String ":$port\s" | Select-String 'LISTENING' | Select-Object -First 1
        if ($match) {
            $processId = ($match -split '\s+')[-1]
            $busy += [PSCustomObject]@{ Port = $port; PID = $processId }
        }
    }

    if ($busy.Count -eq 0) {
        return
    }

    Write-Host ''
    Write-Host 'Portas ja em uso:' -ForegroundColor Yellow
    foreach ($item in $busy) {
        Write-Host ("  Porta {0} - PID {1}" -f $item.Port, $item.PID) -ForegroundColor Yellow
    }

    if (-not (Ask-YesNo 'Encerrar esses processos para subir o supervisório' 'S')) {
        throw 'Inicializacao cancelada pelo usuario devido a portas ocupadas.'
    }

    foreach ($item in $busy) {
        Stop-Process -Id $item.PID -Force -ErrorAction SilentlyContinue
        Write-Host ("[OK] Processo {0} encerrado (porta {1})." -f $item.PID, $item.Port) -ForegroundColor Green
    }
}

function Ensure-ServerReady {
    if (-not (Test-Path (Join-Path $serverDir 'venv\Scripts\python.exe'))) {
        Write-Host 'Criando venv do servidor...'
        & python -m venv (Join-Path $serverDir 'venv')
    }

    Write-Host 'Instalando dependencias Python do servidor...'
    & (Join-Path $serverDir 'venv\Scripts\pip.exe') install -q -r (Join-Path $serverDir 'requirements.txt')
    if ($LASTEXITCODE -ne 0) {
        throw 'Falha ao instalar dependencias Python do servidor.'
    }
}

function Ensure-DashboardReady {
    Write-Host 'Instalando dependencias do dashboard...'
    Push-Location $dashDir
    try {
        cmd /c "npm install --prefer-offline"
        if ($LASTEXITCODE -ne 0) {
            throw 'Falha ao instalar dependencias npm do dashboard.'
        }
    }
    finally {
        Pop-Location
    }
}

function Start-Supervisorio($hostIp) {
    Write-Section 'Subindo supervisório VitalKare'

    Stop-BusyPorts
    Ensure-ServerReady
    Ensure-DashboardReady
    Update-DashboardEnv $hostIp

    Start-Process powershell -ArgumentList '-NoExit', '-Command', @"
Set-Location '$serverDir'
`$env:VK_TCP_HOST = '0.0.0.0'
`$env:VK_HTTP_HOST = '0.0.0.0'
& '$serverDir\venv\Scripts\python.exe' server.py
"@ -WindowStyle Normal

    Start-Process powershell -ArgumentList '-NoExit', '-Command', @"
Set-Location '$dashDir'
npm run dev
"@ -WindowStyle Normal

    Wait-HttpReady 'http://127.0.0.1:8000/api/devices'

    Write-Host ''
    Write-Host "[OK] Server TCP/HTTP iniciado usando o host de LAN $hostIp" -ForegroundColor Green
    Write-Host "[OK] Dashboard configurado para http://$hostIp`:8000" -ForegroundColor Green
    Write-Host "[INFO] Dashboard local: http://localhost:5173" -ForegroundColor Cyan
    Write-Host "[INFO] API docs: http://$hostIp`:8000/docs" -ForegroundColor Cyan
}

function Select-ComPort {
    $ports = Get-CimInstance Win32_SerialPort -ErrorAction SilentlyContinue | Sort-Object DeviceID
    Write-Host ''
    if (-not $ports) {
        throw 'Nenhuma porta serial encontrada.'
    }

    Write-Host 'Portas seriais encontradas:'
    foreach ($port in $ports) {
        Write-Host ("  {0} - {1}" -f $port.DeviceID, $port.Name)
    }

    while ($true) {
        $raw = Ask-Value 'Qual porta COM deseja usar' 'COM8'
        if ($raw) {
            if ($raw.ToUpper().StartsWith('COM')) { return $raw.ToUpper() }
            return ("COM{0}" -f $raw)
        }
    }
}

function Enter-IdfEnvironment($idfPath) {
    if ($script:IdfEnvironmentLoaded) {
        return
    }

    $exportScript = Join-Path $idfPath 'export.ps1'
    Push-Location $firmwareDir
    try {
        . $exportScript
    }
    finally {
        Pop-Location
    }
    $script:IdfEnvironmentLoaded = $true
}

function Invoke-DeviceProvision {
    param(
        [Parameter(Mandatory = $true)]$DeviceConfig,
        [Parameter(Mandatory = $true)][string]$IdfPath,
        [switch]$UseStaticIp,
        [string]$StaticIp,
        [string]$Gateway,
        [string]$Netmask
    )

    Enter-IdfEnvironment $IdfPath
    Push-Location $firmwareDir
    try {
        $commandArgs = @(
            $provisionScript,
            '--port', $DeviceConfig.ComPort,
            '--ssid', $DeviceConfig.Ssid,
            '--wifi-pass', $DeviceConfig.WifiPass,
            '--server-host', $DeviceConfig.HostIp,
            '--tcp-port', $DeviceConfig.TcpPort,
            '--device-id', $DeviceConfig.DeviceId,
            '--ward-id', $DeviceConfig.WardId,
            '--patient-id', $DeviceConfig.PatientId
        )

        if ($UseStaticIp) {
            $commandArgs += @(
                '--use-static-ip',
                '--static-ip', $StaticIp,
                '--gateway', $Gateway,
                '--netmask', $Netmask
            )
        }
        else {
            $commandArgs += '--clear-static-ip'
        }

        $commandArgs += '--non-interactive'

        & python @commandArgs
        if ($LASTEXITCODE -ne 0) {
            throw 'Falha ao gravar a NVS da placa.'
        }
    }
    finally {
        Pop-Location
    }
}

function Capture-DeviceNetworkInfo {
    param(
        [Parameter(Mandatory = $true)][string]$IdfPath,
        [Parameter(Mandatory = $true)][string]$ComPort,
        [int]$TimeoutSec = 60
    )

    Enter-IdfEnvironment $IdfPath
    Push-Location $firmwareDir
    try {
        $captureOutput = @(& python $captureScript --port $ComPort --timeout $TimeoutSec 2>&1)
        $captureOutput | ForEach-Object { "$_" }
        $exitCode = $LASTEXITCODE

        if ($exitCode -eq 1) {
            throw 'Falha ao abrir a porta serial para capturar o IP da placa.'
        }
        if ($exitCode -ne 0 -and $exitCode -ne 2) {
            throw "Captura da rede da placa falhou (codigo $exitCode)."
        }

        $network = [ordered]@{
            Mode     = $null
            Ip       = $null
            Gateway  = $null
            Netmask  = $null
            TimedOut = ($exitCode -eq 2)
        }

        foreach ($line in $captureOutput) {
            if ("$line" -match '^\s*modo:\s*(.+)$') {
                $network.Mode = $Matches[1].Trim().ToUpper()
                continue
            }
            if ("$line" -match '^\s*ip:\s*([0-9.]+)$') {
                $network.Ip = $Matches[1]
                continue
            }
            if ("$line" -match '^\s*gateway:\s*([0-9.]+)$') {
                $network.Gateway = $Matches[1]
                continue
            }
            if ("$line" -match '^\s*netmask:\s*([0-9.]+)$') {
                $network.Netmask = $Matches[1]
            }
        }

        return [PSCustomObject]$network
    }
    finally {
        Pop-Location
    }
}

function Provision-Device($idfPath, $hostIp, $hostNetwork) {
    Write-Section 'Provisionamento da placa'

    $boardName = Ask-Value 'Qual placa sera provisionada (ex: ESP32-S3 DevKit ou KIIRA V2)' 'ESP32-S3 DevKit'
    $comPort = Select-ComPort
    $ssid = Ask-Value 'SSID da rede Wi-Fi' $null -Required
    $wifiPass = Ask-Value 'Senha da rede Wi-Fi' $null -Required
    $deviceId = Ask-Value 'ID do dispositivo' 'leito01'
    $wardId = Ask-Value 'ID da ala' 'ala-a'
    $roomLabel = Ask-Value 'Rotulo do leito/quarto no dashboard' $deviceId
    $patientName = Ask-Value 'Nome do paciente (opcional)' ''
    $patientId = Ask-Value 'ID do paciente' 'PAC-000'
    $tcpPort = Ask-Value 'Porta TCP do servidor' '7890'
    $deviceConfig = [ordered]@{
        BoardName  = $boardName
        ComPort    = $comPort
        Ssid       = $ssid
        WifiPass   = $wifiPass
        DeviceId   = $deviceId
        WardId     = $wardId
        RoomLabel  = $roomLabel
        PatientName = $patientName
        PatientId  = $patientId
        TcpPort    = $tcpPort
        HostIp     = $hostIp
    }
    $deviceIp = $null
    $networkInfo = $null

    Write-Host ''
    Write-Host "[INFO] Gravando configuracao DHCP inicial na $boardName ($comPort)..." -ForegroundColor Cyan

    try {
        Invoke-DeviceProvision -DeviceConfig $deviceConfig -IdfPath $idfPath
    }
    catch {
        throw
    }

    Write-Host ''
    Write-Host '[OK] Provisionamento DHCP concluido.' -ForegroundColor Green
    Write-Host '[INFO] Aperte RESET na placa para ela conectar no SSID informado.' -ForegroundColor Yellow

    try {
        $networkInfo = Capture-DeviceNetworkInfo -IdfPath $idfPath -ComPort $comPort -TimeoutSec 60
        if ($networkInfo.Ip) {
            $deviceIp = $networkInfo.Ip
            Write-Host "[OK] A placa conectou via $($networkInfo.Mode) com IP $($networkInfo.Ip)." -ForegroundColor Green
        }
        elseif ($networkInfo.TimedOut) {
            Write-Host '[AVISO] Nao foi possivel capturar o IP automaticamente dentro do tempo limite.' -ForegroundColor Yellow
        }
    }
    catch {
        Write-Host "[AVISO] Nao foi possivel capturar a rede automaticamente: $($_.Exception.Message)" -ForegroundColor Yellow
    }

    if ($hostNetwork) {
        Write-Host "[INFO] Rede local detectada: interface=$($hostNetwork.InterfaceAlias) gateway=$($hostNetwork.Gateway) mascara=$($hostNetwork.Netmask)" -ForegroundColor Cyan
    }

    $shouldFixIp = $false
    if ($deviceIp) {
        $shouldFixIp = Ask-YesNo "Deseja fixar o IP $deviceIp na placa" 'N'
    }
    else {
        $shouldFixIp = Ask-YesNo 'Deseja configurar um IP fixo na placa agora' 'N'
    }

    if ($shouldFixIp) {
        $staticIp = $(if ($deviceIp) { $deviceIp } else { Ask-Value 'IP fixo da placa' '' -Required })
        $gatewayDefault = $(if ($networkInfo -and $networkInfo.Gateway) { $networkInfo.Gateway } elseif ($hostNetwork -and $hostNetwork.Gateway) { $hostNetwork.Gateway } else { $null })
        $netmaskDefault = $(if ($networkInfo -and $networkInfo.Netmask) { $networkInfo.Netmask } elseif ($hostNetwork -and $hostNetwork.Netmask) { $hostNetwork.Netmask } else { '255.255.255.0' })
        $gateway = Ask-Value 'Gateway da placa' $gatewayDefault -Required
        $netmask = Ask-Value 'Mascara da placa' $netmaskDefault -Required

        Write-Host ''
        Write-Host "[INFO] Regravando a placa para usar IP fixo $staticIp..." -ForegroundColor Cyan
        Invoke-DeviceProvision -DeviceConfig $deviceConfig -IdfPath $idfPath -UseStaticIp -StaticIp $staticIp -Gateway $gateway -Netmask $netmask

        Write-Host ''
        Write-Host '[OK] Configuracao de IP fixo gravada na NVS.' -ForegroundColor Green
        Write-Host '[INFO] Aperte RESET na placa novamente para ela subir com o IP fixo.' -ForegroundColor Yellow

        $deviceIp = $staticIp
        try {
            $fixedInfo = Capture-DeviceNetworkInfo -IdfPath $idfPath -ComPort $comPort -TimeoutSec 60
            if ($fixedInfo.Ip) {
                $deviceIp = $fixedInfo.Ip
                if ($fixedInfo.Ip -ne $staticIp) {
                    Write-Host "[AVISO] A placa voltou reportando IP $($fixedInfo.Ip), diferente do IP fixado $staticIp." -ForegroundColor Yellow
                }
                if ($fixedInfo.Mode -ne 'FIXO') {
                    Write-Host "[AVISO] A placa nao confirmou modo FIXO; verifique o reboot e a rede antes de publicar no dashboard." -ForegroundColor Yellow
                }
            }
            elseif ($fixedInfo.TimedOut) {
                Write-Host '[AVISO] Nao houve confirmacao automatica do IP fixo apos o segundo reboot.' -ForegroundColor Yellow
            }
        }
        catch {
            Write-Host "[AVISO] Nao foi possivel confirmar o IP fixo automaticamente: $($_.Exception.Message)" -ForegroundColor Yellow
        }
    }

    if (-not (Ask-YesNo 'Ja pode mandar a conexao/publicacao dessa placa para o dashboard' 'S')) {
        Write-Host '[INFO] Provisionamento encerrado sem publicar no dashboard.' -ForegroundColor Cyan
        return
    }

    if (-not $deviceIp -and (Ask-YesNo 'Deseja informar manualmente o IP atual da placa para registrar no backend' 'S')) {
        $deviceIp = Ask-Value 'IP atual da placa' '' -Required
    }

    try {
        Sync-DeviceToBackend `
            -deviceId $deviceId `
            -wardId $wardId `
            -roomLabel $roomLabel `
            -patientName $patientName `
            -patientId $patientId `
            -deviceIp $deviceIp `
            -boardName $boardName
    }
    catch {
        Write-Host "[AVISO] Falha ao registrar/aprovar o dispositivo no backend: $($_.Exception.Message)" -ForegroundColor Yellow
    }
}

Write-Section 'VitalKare - Inicializacao do Sistema'

if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
    throw 'Python nao encontrado no PATH.'
}

if (-not (Get-Command node -ErrorAction SilentlyContinue)) {
    throw 'Node.js nao encontrado no PATH.'
}

$idfPath = Get-IdfPath
if (-not $idfPath) {
    throw 'ESP-IDF nao encontrado em C:\esp.'
}

$hostIp = Select-HostIp
$hostNetwork = Get-HostNetworkProfile $hostIp
Write-Host ''
Write-Host "[OK] IP selecionado para o host VitalKare: $hostIp" -ForegroundColor Green
if ($hostNetwork) {
    Write-Host "[INFO] Gateway do host: $($hostNetwork.Gateway) | Mascara: $($hostNetwork.Netmask)" -ForegroundColor Cyan
}

Start-Supervisorio $hostIp

if (Ask-YesNo 'Deseja provisionar uma placa agora' 'S') {
    Provision-Device $idfPath $hostIp $hostNetwork
}

Write-Host ''
Write-Host 'Fluxo concluido.' -ForegroundColor Green
