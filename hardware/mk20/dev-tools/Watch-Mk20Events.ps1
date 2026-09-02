<#
.SYNOPSIS
    Watch-Mk20Events - Listens on MK20 COM port for incoming A1 frames and proactive device events.
#>

[CmdletBinding()]
param(
    [string]$PortName,
    [int]$TimeoutSeconds = 10
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Import-Module (Resolve-Path "$PSScriptRoot\Mk20Protocol.psm1") -Force

if (-not $PortName) {
    $port = Get-Mk20SerialPort
    if (-not $port) { throw "MK20 COM port not found." }
    $PortName = $port.DeviceID
}

Write-Host "Listening for A1 events on $PortName for $TimeoutSeconds seconds..." -ForegroundColor Cyan

$serial = [IO.Ports.SerialPort]::new($PortName, 115200, 'None', 8, 'One')
$serial.ReadTimeout = 200
$serial.WriteTimeout = 1000
$serial.Open()
$serial.DiscardInBuffer()

$buffer = [Collections.Generic.List[byte]]::new()
$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)

try {
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($serial.BytesToRead -gt 0) {
            $chunk = [byte[]]::new($serial.BytesToRead)
            [void]$serial.Read($chunk, 0, $chunk.Length)
            $buffer.AddRange($chunk)

            $bufArray = $buffer.ToArray()
            $frame = Parse-Mk20A1Frame -Buffer $bufArray
            while ($frame) {
                $ts = (Get-Date).ToString('HH:mm:ss.fff')
                Write-Host "[$ts] [EVENT ID $($frame.Id)] [Type $($frame.MessageType)]" -ForegroundColor Green
                Write-Host ($frame.PayloadJson) -ForegroundColor Yellow

                # Remove parsed bytes from buffer
                $buffer.RemoveRange(0, $frame.BytesConsumed)
                $bufArray = $buffer.ToArray()
                $frame = Parse-Mk20A1Frame -Buffer $bufArray
            }
        }
        Start-Sleep -Milliseconds 20
    }
    Write-Host "Listen period completed." -ForegroundColor Cyan
}
finally {
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
}
