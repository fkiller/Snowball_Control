<#
.SYNOPSIS
    Legacy pairing status diagnostics. Pairing creation is retired.
.DESCRIPTION
    This module does not establish cryptographic or physical trust.
#>

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Import-Module (Resolve-Path "$PSScriptRoot\..\dev-tools\Mk20Protocol.psm1") -Force

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
    $ip = if ($ipObj) { $ipObj.IPAddress } else { '192.168.1.100' }

    return [pscustomobject]@{
        HostName = $computerName
        HostMAC = $mac
        HostIP = $ip
        GeneratedAt = (Get-Date).ToUniversalTime().ToString('o')
    }
}

function Request-Mk20Pairing {
    [CmdletBinding()]
    param([string]$PortName, [string]$Device, [int]$TimeoutSeconds = 30)
    throw 'Legacy pairing is unsupported: this module cannot prove native physical presence. Use the reviewed device transport; Preview LAN access is not authenticated pairing.'
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
