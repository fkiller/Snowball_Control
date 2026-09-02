[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('doctor', 'info', 'put', 'shell', 'restore')]
    [string]$Command = 'doctor',

    [string]$Device = '192.168.69.27:5555',
    [string]$Source,
    [string]$Destination,
    [ValidateSet('Auto', 'Adb', 'Com')]
    [string]$Transport = 'Auto',
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-Adb {
    $candidates = @(
        $env:MK20_ADB,
        (Join-Path $env:LOCALAPPDATA 'Temp\Codex-MK20-ADB\platform-tools\adb.exe'),
        (Get-Command adb.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -First 1)
    ) | Where-Object { $_ }

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    throw 'adb.exe was not found. Set MK20_ADB or install Android SDK Platform-Tools.'
}

function Invoke-Adb {
    param(
        [Parameter(Mandatory)] [string[]]$Arguments,
        [switch]$AllowFailure
    )

    $prevPref = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & $script:Adb @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $prevPref
    }
    if (-not $AllowFailure -and $exitCode -ne 0) {
        throw "adb failed ($exitCode): $($output -join [Environment]::NewLine)"
    }
    return $output
}

function Connect-Mk20 {
    [void](Invoke-Adb -Arguments @('connect', $Device) -AllowFailure)
    $state = (Invoke-Adb -Arguments @('-s', $Device, 'get-state') -AllowFailure | Out-String).Trim()
    return $state -eq 'device'
}

function Get-Mk20ComPort {
    $port = Get-CimInstance Win32_SerialPort -ErrorAction SilentlyContinue |
        Where-Object PNPDeviceID -Match 'VID_1D6B&PID_0104' |
        Select-Object -First 1
    return $port
}

function Test-TcpPort {
    param([string]$HostName, [int]$Port, [int]$TimeoutMs = 1200)

    $client = [Net.Sockets.TcpClient]::new()
    try {
        $task = $client.ConnectAsync($HostName, $Port)
        return $task.Wait($TimeoutMs) -and $client.Connected
    }
    catch {
        return $false
    }
    finally {
        $client.Dispose()
    }
}

function Initialize-Crc32 {
    if ('Mk20Crc32' -as [type]) { return }

    Add-Type -TypeDefinition @'
using System;
public static class Mk20Crc32 {
    public static uint Calculate(byte[] data) {
        uint crc = 0xffffffffu;
        foreach (byte value in data) {
            crc ^= value;
            for (int bit = 0; bit < 8; bit++)
                crc = (crc & 1) != 0 ? (crc >> 1) ^ 0xedb88320u : crc >> 1;
        }
        return crc ^ 0xffffffffu;
    }
}
'@
}

function ConvertTo-UInt32Bytes {
    param([uint32]$Value)
    return [BitConverter]::GetBytes($Value)
}

function New-A1Frame {
    param([uint32]$Id, [string]$Json)

    Initialize-Crc32
    $payload = [Text.Encoding]::UTF8.GetBytes($Json)
    $size = ConvertTo-UInt32Bytes ([uint32]$payload.Length)
    $stream = [IO.MemoryStream]::new()
    foreach ($part in @(
        [byte[]](0xA1, 0xA5, 0x5A, 0x5E),
        (ConvertTo-UInt32Bytes $Id),
        (ConvertTo-UInt32Bytes 101),
        $size,
        (ConvertTo-UInt32Bytes ([Mk20Crc32]::Calculate($size))),
        $payload,
        (ConvertTo-UInt32Bytes ([Mk20Crc32]::Calculate($payload)))
    )) {
        $stream.Write($part, 0, $part.Length)
    }
    return $stream.ToArray()
}

function Find-A1Payload {
    param([byte[]]$Buffer, [uint32]$ExpectedId)

    for ($offset = 0; $offset -le $Buffer.Length - 24; $offset++) {
        if ($Buffer[$offset] -ne 0xA1 -or $Buffer[$offset + 1] -ne 0xA5 -or
            $Buffer[$offset + 2] -ne 0x5A -or $Buffer[$offset + 3] -ne 0x5E) {
            continue
        }

        $id = [BitConverter]::ToUInt32($Buffer, $offset + 4)
        $length = [BitConverter]::ToUInt32($Buffer, $offset + 12)
        if ($id -ne $ExpectedId -or $length -gt 1MB) { continue }
        if ($offset + 24 + $length -gt $Buffer.Length) { continue }

        return [Text.Encoding]::UTF8.GetString($Buffer, $offset + 20, [int]$length)
    }
    return $null
}

function Invoke-A1 {
    param([string]$PortName, [hashtable]$Request, [int]$TimeoutMs = 5000)

    $id = [uint32](Get-Random -Minimum 10000 -Maximum 2000000000)
    $json = $Request | ConvertTo-Json -Compress -Depth 8
    $frame = New-A1Frame -Id $id -Json $json
    $serial = [IO.Ports.SerialPort]::new($PortName, 115200, 'None', 8, 'One')
    $serial.ReadTimeout = 200
    $serial.WriteTimeout = 1500
    $bytes = [Collections.Generic.List[byte]]::new()

    try {
        $serial.Open()
        $serial.DiscardInBuffer()
        $serial.Write($frame, 0, $frame.Length)
        $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
        do {
            if ($serial.BytesToRead -gt 0) {
                $chunk = [byte[]]::new($serial.BytesToRead)
                [void]$serial.Read($chunk, 0, $chunk.Length)
                $bytes.AddRange($chunk)
                $payload = Find-A1Payload -Buffer $bytes.ToArray() -ExpectedId $id
                if ($payload) { return $payload | ConvertFrom-Json }
            }
            Start-Sleep -Milliseconds 40
        } while ([DateTime]::UtcNow -lt $deadline)
    }
    finally {
        if ($serial.IsOpen) { $serial.Close() }
        $serial.Dispose()
    }

    throw "No A1 response received from $PortName."
}

function Invoke-A1OnPort {
    param(
        [IO.Ports.SerialPort]$Serial,
        [hashtable]$Request,
        [int]$TimeoutMs = 5000
    )

    $id = [uint32](Get-Random -Minimum 10000 -Maximum 2000000000)
    $json = $Request | ConvertTo-Json -Compress -Depth 8
    $frame = New-A1Frame -Id $id -Json $json
    $bytes = [Collections.Generic.List[byte]]::new()

    try {
        $Serial.Write($frame, 0, $frame.Length)
    }
    catch {
        throw "A1 write failed on $($Serial.PortName): $($_.Exception.Message)"
    }

    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    do {
        if ($Serial.BytesToRead -gt 0) {
            $chunk = [byte[]]::new($Serial.BytesToRead)
            [void]$Serial.Read($chunk, 0, $chunk.Length)
            $bytes.AddRange($chunk)
            $payload = Find-A1Payload -Buffer $bytes.ToArray() -ExpectedId $id
            if ($payload) { return $payload | ConvertFrom-Json }
        }
        Start-Sleep -Milliseconds 25
    } while ([DateTime]::UtcNow -lt $deadline)

    throw "No A1 response received from $($Serial.PortName) for request id $id."
}

function Show-Doctor {
    $hostName = $Device.Split(':')[0]
    $port = [int]$Device.Split(':')[1]

    $com = $null
    try { $com = Get-Mk20ComPort } catch { Write-Warning "COM discovery failed: $($_.Exception.Message)" }

    $wifi = $null
    $wifiIp = $null
    try {
        $wifi = Get-NetConnectionProfile -InterfaceAlias 'Wi-Fi' -ErrorAction SilentlyContinue
        $wifiIp = Get-NetIPAddress -InterfaceAlias 'Wi-Fi' -AddressFamily IPv4 -ErrorAction SilentlyContinue |
            Select-Object -ExpandProperty IPAddress -First 1
    } catch { Write-Warning "Wi-Fi check failed: $($_.Exception.Message)" }

    $tcp = $false
    try { $tcp = Test-TcpPort -HostName $hostName -Port $port } catch { Write-Warning "TCP check failed: $($_.Exception.Message)" }

    $adbReady = $false
    try { $adbReady = Connect-Mk20 } catch { Write-Warning "ADB check failed: $($_.Exception.Message)" }

    [pscustomobject]@{
        WifiProfile = $wifi.Name
        WifiIPv4 = $wifiIp
        ProductCom = if ($com) { $com.DeviceID } else { $null }
        ProductUsb = if ($com) { '1D6B:0104' } else { $null }
        TcpAdbEndpoint = $Device
        TcpAdbOpen = $tcp
        AdbState = if ($adbReady) { 'device' } else { 'unavailable' }
    } | Format-List

    if ($adbReady) {
        try {
            Invoke-Adb -Arguments @('-s', $Device, 'shell',
                'id; echo KERNEL=$(uname -r); echo MODEL=$(cat /etc/openwrt_release | grep DISTRIB_TARGET); ' +
                'echo PIDS=$(pidof KeyboardDevice) $(pidof adbd); ' +
                'iptables -L INPUT -n --line-numbers | grep 5555')
        } catch { Write-Warning "Remote diagnostics failed: $($_.Exception.Message)" }
    }
}

function Show-Info {
    $com = Get-Mk20ComPort
    if ($com) {
        try {
            $reply = Invoke-A1 -PortName $com.DeviceID -Request @{ method = 'getInfo' }
            $reply | ConvertTo-Json -Depth 10
        }
        catch {
            Write-Warning "COM getInfo failed: $($_.Exception.Message)"
        }
    }
    else {
        Write-Warning 'Product COM interface was not found.'
    }

    $adbReady = $false
    try { $adbReady = Connect-Mk20 } catch {}

    if ($adbReady) {
        try {
            Invoke-Adb -Arguments @('-s', $Device, 'shell',
                'id; uname -a; cat /etc/openwrt_release; echo ===FILESYSTEMS===; df -h; ' +
                'echo ===NETWORK===; ip addr show wlan0; echo ===PROCESSES===; ' +
                'ps w | grep -E "[K]eyboardDevice|[a]dbd|[x]iaozhi"')
        } catch { Write-Warning "ADB shell failed: $($_.Exception.Message)" }
    }
    else {
        Write-Warning "Network ADB is unavailable at $Device."
    }
}

function Put-File {
    if (-not $Source -or -not $Destination) {
        throw 'put requires -Source <local file> and -Destination <device path>.'
    }
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
        throw "Source file does not exist: $Source"
    }
    $sourcePath = (Resolve-Path -LiteralPath $Source).Path
    $adbReady = $false
    if ($Transport -ne 'Com') { $adbReady = Connect-Mk20 }
    if ($Transport -eq 'Adb' -or ($Transport -eq 'Auto' -and $adbReady)) {
        if (-not $adbReady) { throw "Network ADB is unavailable at $Device." }
        Invoke-Adb -Arguments @('-s', $Device, 'push', $sourcePath, $Destination)
        $localMd5 = (Get-FileHash -LiteralPath $sourcePath -Algorithm MD5).Hash.ToLowerInvariant()
        $remoteLine = (Invoke-Adb -Arguments @('-s', $Device, 'shell', "md5sum '$Destination'") | Out-String).Trim()
        $remoteMd5 = ($remoteLine -split '\s+')[0].ToLowerInvariant()
        if ($localMd5 -ne $remoteMd5) {
            throw "Checksum mismatch: local=$localMd5 remote=$remoteMd5"
        }
        Write-Output "Uploaded and verified over ADB: $Destination ($localMd5)"
        return
    }

    $com = Get-Mk20ComPort
    if (-not $com) { throw 'Product COM interface was not found.' }
    $raw = [IO.File]::ReadAllBytes($sourcePath)
    $chunkSize = 96
    $chunkCount = [Math]::Ceiling($raw.Length / [double]$chunkSize)
    $completed = 0
    $serial = [IO.Ports.SerialPort]::new($com.DeviceID, 115200, 'None', 8, 'One')
    $serial.ReadTimeout = 200
    $serial.WriteTimeout = 1500
    try {
        $serial.Open()
        $serial.DiscardInBuffer()
        for ($offset = 0; $offset -lt $raw.Length; $offset += $chunkSize) {
            $length = [Math]::Min($chunkSize, $raw.Length - $offset)
            $chunk = [byte[]]::new($length)
            [Array]::Copy($raw, $offset, $chunk, 0, $length)
            $reply = Invoke-A1OnPort -Serial $serial -TimeoutMs 3500 -Request @{
                method = 'saveToFile'
                parameters = @{
                    filePath = $Destination
                    seek = $offset
                    data = [Convert]::ToBase64String($chunk)
                }
            }
            if (-not $reply.success) {
                throw "COM upload failed at offset $offset`: $($reply.errorString)"
            }
            $completed++
            if (($completed % 8) -eq 0 -or $completed -eq $chunkCount) {
                Write-Progress -Activity "Uploading $Destination over $($com.DeviceID)" `
                    -Status "$completed / $chunkCount chunks acknowledged" `
                    -PercentComplete (($completed / $chunkCount) * 100)
            }
        }
    }
    finally {
        Write-Progress -Activity "Uploading $Destination over $($com.DeviceID)" -Completed
        if ($serial.IsOpen) { $serial.Close() }
        $serial.Dispose()
    }
    Write-Output "Uploaded over COM with acknowledged chunks: $Destination ($completed/$chunkCount)"
}

function Open-Shell {
    if (-not (Connect-Mk20)) {
        throw "Network ADB is unavailable at $Device."
    }
    & $script:Adb -s $Device shell
    if ($LASTEXITCODE -ne 0) { throw "adb shell exited with $LASTEXITCODE" }
}

function Restore-Factory {
    if (-not (Connect-Mk20)) {
        throw "Network ADB is unavailable at $Device."
    }

    $check = Invoke-Adb -Arguments @('-s', $Device, 'shell',
        'test -s /mnt/SDCARD/adbd-configfs.init.factory && ' +
        'md5sum /mnt/SDCARD/adbd-configfs.init.factory && ' +
        'test -x /data/setusbconfig && echo READY')
    $check
    if (($check | Out-String) -notmatch 'READY') {
        throw 'Factory backup or serial USB helper is missing; restore was not attempted.'
    }

    if (-not $Force) {
        Write-Output 'Dry run only. Re-run with -Force to restore factory adbd, remove lunch.sh, select serial USB, and reboot.'
        return
    }

    Invoke-Adb -Arguments @('-s', $Device, 'shell',
        'cp /mnt/SDCARD/adbd-configfs.init.factory /etc/init.d/adbd && ' +
        'chmod 755 /etc/init.d/adbd && rm -f /mnt/SDCARD/lunch.sh && ' +
        '/data/setusbconfig serial && sync && reboot') -AllowFailure
    Write-Output 'Factory restore was issued; the network ADB connection will close during reboot.'
}

$script:Adb = Resolve-Adb

switch ($Command) {
    'doctor' { Show-Doctor }
    'info' { Show-Info }
    'put' { Put-File }
    'shell' { Open-Shell }
    'restore' { Restore-Factory }
}
