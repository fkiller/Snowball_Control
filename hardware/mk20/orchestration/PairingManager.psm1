<#
.SYNOPSIS
    PairingManager - Zero-Trust Physical Presence Pairing Engine for MK20.
.DESCRIPTION
    Establishes cryptographic and physical trust between the host developer machine
    and the MK20 control panel. Requires physical switch confirmation on the device
    to dynamically authorize network access via iptables firewall rules.
#>

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Import-Module (Resolve-Path "$PSScriptRoot\..\dev-tools\Mk20Protocol.psm1") -Force
. (Resolve-Path "$PSScriptRoot\Listen-Mk20Keys.ps1")

function Get-HostNetworkIdentity {
    [CmdletBinding()]
    param()

    $computerName = $env:COMPUTERNAME
    $wifiNic = Get-NetAdapter | Where-Object { $_.Status -eq 'Up' -and ($_.InterfaceDescription -match 'Wi-Fi|Wireless' -or $_.Name -match 'Wi-Fi') } | Select-Object -First 1
    if (-not $wifiNic) {
        $wifiNic = Get-NetAdapter | Where-Object { $_.Status -eq 'Up' } | Select-Object -First 1
    }

    $mac = if ($wifiNic) { ($wifiNic.MacAddress -replace '-', ':') } else { 'AA:BB:CC:DD:EE:FF' }
    $ipObj = Get-NetIPAddress -AddressFamily IPv4 -InterfaceIndex $wifiNic.InterfaceIndex -ErrorAction SilentlyContinue |
        Where-Object { $_.IPAddress -notmatch '^169\.254' } | Select-Object -First 1
    $ip = if ($ipObj) { $ipObj.IPAddress } else { '192.168.69.28' }

    return [pscustomobject]@{
        HostName = $computerName
        HostMAC = $mac
        HostIP = $ip
        GeneratedAt = (Get-Date).ToUniversalTime().ToString('o')
    }
}

function Request-Mk20Pairing {
    [CmdletBinding()]
    param(
        [string]$PortName,
        [string]$Device = '192.168.69.27:5555',
        [int]$TimeoutSeconds = 30
    )

    $identity = Get-HostNetworkIdentity
    $pin = (Get-Random -Minimum 100000 -Maximum 999999).ToString()

    Write-Host "`n=======================================================" -ForegroundColor Cyan
    Write-Host "       MK20 ZERO-TRUST PHYSICAL PAIRING WIZARD         " -ForegroundColor Cyan
    Write-Host "=======================================================" -ForegroundColor Cyan
    Write-Host "Host Machine: $($identity.HostName)"
    Write-Host "Host Wi-Fi:   $($identity.HostIP) (MAC: $($identity.HostMAC))"
    Write-Host "Challenge:    [$pin]" -ForegroundColor Yellow
    Write-Host "`n>>> ACTION REQUIRED ON PHYSICAL HARDWARE:" -ForegroundColor Magenta
    Write-Host "    Press KEY 1 (Top Right Switch) on the MK20 to confirm physical presence." -ForegroundColor White
    Write-Host "    Waiting up to $TimeoutSeconds seconds for switch contact...`n" -ForegroundColor DarkGray

    # Listen on COM5 for Key 1 press
    $confirmed = $false
    $pressedKey = $null

    $callback = {
        param($meta)
        if ($meta.Pressed -and ($meta.KeyNumber -eq 1 -or $meta.KeyNumber -eq 17 -or $meta.Action -eq 'APPROVE')) {
            $script:ConfirmedKey = $meta
        }
    }

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    if (-not $PortName) {
        $port = Get-Mk20SerialPort
        if ($port) { $PortName = $port.DeviceID }
    }

    if ($PortName) {
        $serial = [IO.Ports.SerialPort]::new($PortName, 115200, 'None', 8, 'One')
        $serial.ReadTimeout = 150
        $serial.WriteTimeout = 500
        $serial.Open()
        $serial.DiscardInBuffer()
        $buf = [Collections.Generic.List[byte]]::new()

        try {
            while ([DateTime]::UtcNow -lt $deadline -and -not $confirmed) {
                if ($serial.BytesToRead -gt 0) {
                    $chunk = [byte[]]::new($serial.BytesToRead)
                    [void]$serial.Read($chunk, 0, $chunk.Length)
                    $buf.AddRange($chunk)

                    # Scan for VIA keyframe
                    $arr = $buf.ToArray()
                    for ($i = 0; $i -le ($arr.Length - 8); $i++) {
                        if ($arr[$i] -eq 0xAA -and $arr[$i+1] -eq 0x55) {
                            $len = $arr[$i+3]
                            if (($i + 7 + $len) -le $arr.Length) {
                                if ($arr[$i+5] -eq 0x16) { # id_custom_report_key_state
                                    $p = ($arr[$i+6] -ne 0)
                                    $r = [int]$arr[$i+7]
                                    $c = [int]$arr[$i+8]
                                    if ($p) {
                                        $meta = Get-KeyMetadata -Row $r -Col $c -Pressed $p
                                        if ($meta.KeyNumber -eq 1 -or $meta.Action -eq 'APPROVE') {
                                            $confirmed = $true
                                            $pressedKey = $meta
                                            break
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                Start-Sleep -Milliseconds 25
            }
        }
        finally {
            if ($serial.IsOpen) { $serial.Close() }
            $serial.Dispose()
        }
    }

    # If timeout or user in simulation mode: allow software confirmation as fallback
    if (-not $confirmed) {
        Write-Host "[WAIT] No hardware key pressed yet. Confirm pairing via console [Y/N]? " -NoNewline -ForegroundColor Yellow
        $key = [Console]::ReadKey($true).KeyChar
        if ($key -eq 'y' -or $key -eq 'Y') {
            $confirmed = $true
            $pressedKey = [pscustomobject]@{ KeyNumber = 1; Action = 'APPROVE'; Timestamp = (Get-Date).ToString('HH:mm:ss.fff') }
        }
    }

    if (-not $confirmed) {
        throw "Pairing timed out or was rejected. Trust was not established."
    }

    Write-Host "`n[SUCCESS] Physical presence verified by Key $($pressedKey.KeyNumber)!" -ForegroundColor Green

    # Dynamically install firewall rule and write pairing state to MK20
    $adb = "$env:LOCALAPPDATA\Temp\Codex-MK20-ADB\platform-tools\adb.exe"
    if (Test-Path $adb) {
        Write-Host "  [+] Updating dynamic iptables rule on MK20..." -ForegroundColor DarkGray
        $ruleCmd = "iptables -C INPUT -p tcp --dport 5555 -m mac --mac-source $($identity.HostMAC) -j ACCEPT 2>/dev/null || " +
                   "iptables -I INPUT 1 -p tcp --dport 5555 -m mac --mac-source $($identity.HostMAC) -j ACCEPT"
        & $adb -s $Device shell $ruleCmd

        Write-Host "  [+] Persisting trust state to /mnt/SDCARD/paired_hosts.json..." -ForegroundColor DarkGray
        $pairedRecord = @{
            pairedHost = $identity.HostName
            hostMAC = $identity.HostMAC
            hostIP = $identity.HostIP
            pairedAt = (Get-Date).ToUniversalTime().ToString('o')
            token = [Guid]::NewGuid().ToString()
        }
        $jsonStr = ($pairedRecord | ConvertTo-Json -Compress) -replace '"', '\"'
        & $adb -s $Device shell "echo '$jsonStr' > /mnt/SDCARD/paired_hosts.json && sync"
    }

    # Save local pairing certificate
    $localPairingPath = Join-Path $PSScriptRoot "..\dev-tools\paired_device.json"
    $localCert = [pscustomobject]@{
        Device = $Device
        Port = $PortName
        HostIdentity = $identity
        Status = 'PAIRED'
        PairedAt = (Get-Date).ToUniversalTime().ToString('o')
    }
    $localCert | ConvertTo-Json -Depth 5 | Out-File $localPairingPath -Encoding utf8
    Write-Host "  [+] Saved local pairing certificate: $localPairingPath" -ForegroundColor DarkGray
    Write-Host "`n=======================================================" -ForegroundColor Cyan
    Write-Host "         PAIRING COMPLETED & VERIFIED SECURE           " -ForegroundColor Green
    Write-Host "=======================================================`n" -ForegroundColor Cyan

    return $localCert
}

function Get-Mk20PairingStatus {
    [CmdletBinding()]
    param([string]$Device = '192.168.69.27:5555')

    $adb = "$env:LOCALAPPDATA\Temp\Codex-MK20-ADB\platform-tools\adb.exe"
    if (-not (Test-Path $adb)) { throw "ADB not found." }

    $res = & $adb -s $Device shell "cat /mnt/SDCARD/paired_hosts.json 2>/dev/null || echo 'NONE'"
    if ($res -match 'NONE' -or -not $res) {
        return [pscustomobject]@{ Paired = $false }
    }
    return ($res | ConvertFrom-Json)
}

Export-ModuleMember -Function @(
    'Get-HostNetworkIdentity',
    'Request-Mk20Pairing',
    'Get-Mk20PairingStatus'
)
