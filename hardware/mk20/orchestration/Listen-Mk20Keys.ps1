<#
.SYNOPSIS
    Listen-Mk20Keys - Live physical switch matrix monitor and action dispatcher for MK20.
.DESCRIPTION
    Listens on the MK20 USB CDC serial interface (COM5) for mechanical keypresses
    and rotary encoder actions, mapping switch coordinates to physical key numbers
    (1..20) and semantic agent actions (Approve, Reject, Retry, Cancel).
#>

[CmdletBinding()]
param(
    [string]$PortName,
    [int]$TimeoutSeconds = 0, # 0 = run until Ctrl+C
    [scriptblock]$OnKeyEvent
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Import-Module (Resolve-Path "$PSScriptRoot\..\dev-tools\Mk20Protocol.psm1") -Force

# Matrix Lookup Table: maps (row, col) -> physical key number (1..20)
# Matches vendor PCMonitorApp map_table
$script:MatrixMap = @{
    '0,4' = 1;  '1,4' = 2;  '2,4' = 3;  '3,4' = 4
    '0,3' = 5;  '1,3' = 6;  '2,3' = 7;  '3,3' = 8
    '0,2' = 9;  '1,2' = 10; '2,2' = 11; '3,2' = 12
    '0,1' = 13; '1,1' = 14; '2,1' = 15; '3,1' = 16
    '0,0' = 17; '1,0' = 18; '2,0' = 19; '3,0' = 20
}

# Semantic Agent Action Mapping
$script:ActionMap = @{
    1  = 'APPROVE'          # Key 1: Confirm command / tool execution
    2  = 'REJECT'           # Key 2: Decline proposal
    3  = 'RETRY'            # Key 3: Retry turn / request alternative
    4  = 'CANCEL'           # Key 4: Emergency stop current agent turn
    5  = 'SWITCH_PROVIDER'  # Key 5: Toggle Codex / Claude / Gemini
    6  = 'SWITCH_MODEL'     # Key 6: Toggle Flash / Pro / Opus
    7  = 'VIEW_PLAN'        # Key 7: Open implementation plan
    8  = 'VIEW_DIFF'        # Key 8: Open git diff
    17 = 'APPROVE'          # Key 17: Alternative primary approve
}

function Get-KeyMetadata {
    param([int]$Row, [int]$Col, [bool]$Pressed)
    $keyNum = $null
    $action = 'UNKNOWN'
    $isDial = $false

    if ($Row -ge 100) {
        $isDial = $true
        $keyNum = $Row
        switch ($Row) {
            100 { $action = 'DIAL_LEFT' }
            101 { $action = 'DIAL_RIGHT' }
            102 { $action = 'DIAL_PUSH' }
            103 { $action = 'DIAL_LONG_PUSH' }
            104 { $action = 'DIAL_ROTATE_FAST_L' }
            105 { $action = 'DIAL_ROTATE_FAST_R' }
            default { $action = "DIAL_$Row" }
        }
    } else {
        $coordKey = "$Row,$Col"
        if ($script:MatrixMap.ContainsKey($coordKey)) {
            $keyNum = $script:MatrixMap[$coordKey]
            if ($script:ActionMap.ContainsKey($keyNum)) {
                $action = $script:ActionMap[$keyNum]
            }
        }
    }

    return [pscustomobject]@{
        KeyNumber = $keyNum
        Row = $Row
        Col = $Col
        Pressed = $Pressed
        IsDial = $isDial
        Action = $action
        Timestamp = (Get-Date).ToString('HH:mm:ss.fff')
    }
}

function Process-KeySignal {
    param([int]$Row, [int]$Col, [bool]$Pressed, [scriptblock]$Callback)
    $meta = Get-KeyMetadata -Row $Row -Col $Col -Pressed $Pressed

    $stateText = if ($Pressed) { 'DOWN' } else { 'UP  ' }
    $color = if ($Pressed) { 'Green' } else { 'DarkGray' }
    if ($meta.Action -eq 'REJECT' -and $Pressed) { $color = 'Red' }
    elseif ($meta.Action -eq 'CANCEL' -and $Pressed) { $color = 'Magenta' }
    elseif ($meta.IsDial) { $color = 'Cyan' }

    Write-Host "[$($meta.Timestamp)] " -NoNewline -ForegroundColor DarkGray
    if ($meta.IsDial) {
        Write-Host "[DIAL] Row $($meta.Row) [$stateText] -> $($meta.Action)" -ForegroundColor $color
    } else {
        Write-Host "[KEY $($meta.KeyNumber.ToString().PadLeft(2))] (Row $($meta.Row), Col $($meta.Col)) [$stateText] -> Action: $($meta.Action)" -ForegroundColor $color
    }

    # Forward to MK20 HUD screen for instant visual feedback
    try {
        if ($meta.IsDial) {
            & "$PSScriptRoot\Send-Mk20HudEvent.ps1" -Action dial -DialAction $meta.Action -ErrorAction SilentlyContinue
        } else {
            & "$PSScriptRoot\Send-Mk20HudEvent.ps1" -Action key -Row $Row -Col $Col -Pressed:$Pressed -ErrorAction SilentlyContinue
        }
    } catch {}

    if ($Callback) {
        & $Callback $meta
    }
    return $meta
}

function Start-Mk20KeyListener {
    [CmdletBinding()]
    param(
        [string]$Port,
        [int]$TimeoutSec = 0,
        [scriptblock]$Callback
    )

    if (-not $Port) {
        $detected = Get-Mk20SerialPort
        if (-not $detected) { throw "MK20 COM port not found." }
        $Port = $detected.DeviceID
    }

    Write-Host "=== MK20 Live Key Matrix Monitor ===" -ForegroundColor Cyan
    Write-Host "Port: $Port (115200 8N1)"
    if ($TimeoutSec -gt 0) {
        Write-Host "Timeout: $TimeoutSec seconds" -ForegroundColor DarkGray
    } else {
        Write-Host "Listening continuously (Press Ctrl+C to stop)..." -ForegroundColor Yellow
    }
    Write-Host "Press any physical mechanical key or turn the dial on the MK20:`n" -ForegroundColor DarkCyan

    $serial = [IO.Ports.SerialPort]::new($Port, 115200, 'None', 8, 'One')
    $serial.ReadTimeout = 200
    $serial.WriteTimeout = 1000
    $serial.Open()
    $serial.DiscardInBuffer()

    $buffer = [Collections.Generic.List[byte]]::new()
    $deadline = if ($TimeoutSec -gt 0) { [DateTime]::UtcNow.AddSeconds($TimeoutSec) } else { [DateTime]::MaxValue }

    try {
        while ([DateTime]::UtcNow -lt $deadline) {
            if ($serial.BytesToRead -gt 0) {
                $chunk = [byte[]]::new($serial.BytesToRead)
                [void]$serial.Read($chunk, 0, $chunk.Length)
                $buffer.AddRange($chunk)

                # 1. Check for VIA Protocol Key Frame (0xAA 0x55 ... 0xF5 0x5F)
                $bufArr = $buffer.ToArray()
                $i = 0
                while ($i -le ($bufArr.Length - 8)) {
                    if ($bufArr[$i] -eq 0xAA -and $bufArr[$i+1] -eq 0x55) {
                        $len = $bufArr[$i+3]
                        if (($i + 7 + $len) -le $bufArr.Length) {
                            $cmd = $bufArr[$i+5]
                            # id_custom_report_key_state = 0x16
                            if ($cmd -eq 0x16 -and $len -ge 4) {
                                $pressed = ($bufArr[$i+6] -ne 0)
                                $row = [int]$bufArr[$i+7]
                                $col = [int]$bufArr[$i+8]
                                [void](Process-KeySignal -Row $row -Col $col -Pressed $pressed -Callback $Callback)
                                $removeCount = $i + 7 + $len
                                $buffer.RemoveRange(0, $removeCount)
                                $bufArr = $buffer.ToArray()
                                $i = 0
                                continue
                            }
                        }
                    }
                    $i++
                }

                # 2. Check for A1 Protocol JSON Frame (Magic: 0xA1 0xA5 0x5A 0x5E)
                $frame = Parse-Mk20A1Frame -Buffer $buffer.ToArray()
                while ($frame) {
                    $json = $frame.Payload
                    if ($json.method -eq 'device_keyState_Changed' -or $json.ack_method -eq 'device_keyState_Changed') {
                        $params = $json.parameters
                        $pressed = [bool]$params.pressed
                        $row = [int]$params.row
                        $col = [int]$params.col
                        [void](Process-KeySignal -Row $row -Col $col -Pressed $pressed -Callback $Callback)
                    }
                    $buffer.RemoveRange(0, $frame.BytesConsumed)
                    $frame = Parse-Mk20A1Frame -Buffer $buffer.ToArray()
                }

                # Trim stale buffer if oversized
                if ($buffer.Count -gt 4096) {
                    $buffer.RemoveRange(0, 2048)
                }
            }
            Start-Sleep -Milliseconds 15
        }
    }
    finally {
        if ($serial.IsOpen) { $serial.Close() }
        $serial.Dispose()
    }
}

Start-Mk20KeyListener -Port $PortName -TimeoutSec $TimeoutSeconds -Callback $OnKeyEvent
