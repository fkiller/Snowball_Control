<#
.SYNOPSIS
    SyntheticPlayer - Streams normalized agent orchestration events to the MK20.
.DESCRIPTION
    Replays realistic mock AI agent sessions (session, turn, state, tool, approval,
    and completion) against the MK20 control panel without calling live LLM APIs.
    Demonstrates host-owned orchestration architecture from NORTH_STARS.md.
#>

[CmdletBinding()]
param(
    [ValidateSet('codex', 'claude', 'gemini')]
    [string]$Provider = 'claude',

    [string]$Objective = 'Harden A1 protocol and validate live hardware',
    [int]$StepDelayMs = 1200,
    [switch]$InteractiveApproval,
    [switch]$HardwareApproval
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Import-Module (Resolve-Path "$PSScriptRoot\..\dev-tools\Mk20Protocol.psm1") -Force
. (Resolve-Path "$PSScriptRoot\Listen-Mk20Keys.ps1")

function Emit-Event {
    param(
        [string]$Type,
        [hashtable]$Data,
        [string]$Color = 'White'
    )

    $event = [pscustomobject]@{
        version = "1.0"
        eventId = "evt-" + (Get-Date -Format 'yyyyMMdd-HHmmss-fff')
        timestamp = (Get-Date).ToUniversalTime().ToString('o')
        type = $Type
        provider = $Provider
        data = $Data
    }

    $desc = $null
    if ($Data.ContainsKey('headline')) { $desc = $Data['headline'] }
    elseif ($Data.ContainsKey('summary')) { $desc = $Data['summary'] }
    elseif ($Data.ContainsKey('title')) { $desc = $Data['title'] }
    elseif ($Data.ContainsKey('objective')) { $desc = $Data['objective'] }
    elseif ($Data.ContainsKey('userPrompt')) { $desc = $Data['userPrompt'] }
    elseif ($Data.ContainsKey('action')) { $desc = "Resolved: $($Data['action']) (via $($Data['resolvedBy']))" }
    else { $desc = $Type }

    $timeStr = (Get-Date).ToString('HH:mm:ss.fff')
    Write-Host "[$timeStr] " -NoNewline -ForegroundColor DarkGray
    Write-Host "[$($Type.PadRight(18))] " -NoNewline -ForegroundColor Cyan
    Write-Host "$desc" -ForegroundColor $Color
}

Write-Host "=== MK20 Synthetic Agent Session Player ===" -ForegroundColor Cyan
Write-Host "Provider:  $Provider"
Write-Host "Objective: $Objective`n" -ForegroundColor Yellow

$comPort = Get-Mk20SerialPort
$hasHardware = $null -ne $comPort
if ($hasHardware) {
    Write-Host "[HUD] Connected to MK20 on $($comPort.DeviceID)" -ForegroundColor Green
} else {
    Write-Host "[HUD] Running in simulation mode (no MK20 COM detected)" -ForegroundColor DarkYellow
}

# 1. session.init
Emit-Event -Type 'session.init' -Data @{
    provider = $Provider
    model = if ($Provider -eq 'claude') { 'claude-3-7-sonnet' } elseif ($Provider -eq 'codex') { 'gpt-5-codex' } else { 'gemini-2.5-pro' }
    workspace = 'Snowball_Control'
    objective = $Objective
} -Color Cyan
Start-Sleep -Milliseconds $StepDelayMs

# 2. turn.start
Emit-Event -Type 'turn.start' -Data @{
    turnIndex = 1
    userPrompt = "Verify dual-plane recovery topology and advance North Star milestones."
} -Color Magenta
Start-Sleep -Milliseconds $StepDelayMs

# 3. agent.state (Thinking)
Emit-Event -Type 'agent.state' -Data @{
    state = 'thinking'
    headline = 'Reasoning about A1 frame CRC handling and recovery invariants'
} -Color Gray
Start-Sleep -Milliseconds $StepDelayMs

# 4. tool.invocation (view_file)
Emit-Event -Type 'tool.invocation' -Data @{
    toolName = 'view_file'
    summary = 'Inspecting hardware/mk20/dev-tools/NORTH_STARS.md'
} -Color Yellow
Start-Sleep -Milliseconds ($StepDelayMs / 2)

# 5. tool.result
Emit-Event -Type 'tool.result' -Data @{
    toolName = 'view_file'
    success = $true
    durationMs = 85
    summary = 'Read 68 lines of architectural guidance'
} -Color Green
Start-Sleep -Milliseconds ($StepDelayMs / 2)

# 6. agent.state (Planning)
Emit-Event -Type 'agent.state' -Data @{
    state = 'planning'
    headline = 'Constructing unit test suite for length & payload CRC validation'
} -Color Gray
Start-Sleep -Milliseconds $StepDelayMs

# 7. tool.invocation (run_command)
Emit-Event -Type 'tool.invocation' -Data @{
    toolName = 'run_command'
    summary = 'Executing Test-Mk20Protocol.ps1'
} -Color Yellow
Start-Sleep -Milliseconds ($StepDelayMs / 2)

# 8. tool.result
Emit-Event -Type 'tool.result' -Data @{
    toolName = 'run_command'
    success = $true
    durationMs = 940
    summary = '19 passed, 0 failed across all CRC vectors and stream tests'
} -Color Green
Start-Sleep -Milliseconds ($StepDelayMs / 2)

# 9. approval.requested
$apprEvent = Emit-Event -Type 'approval.requested' -Data @{
    approvalId = 'appr-401'
    severity = 'medium'
    title = 'Execute live device snapshot and verify hardware?'
    description = 'Captures read-only system, network, and theme files over ADB.'
    options = @(
        @{ key = 0; action = 'approve'; label = 'Approve (Key 0)' },
        @{ key = 1; action = 'reject'; label = 'Reject (Key 1)' }
    )
} -Color Yellow

$resolvedBy = 'synthetic_auto_agent'
if ($HardwareApproval) {
    Write-Host "`n>>> [HARDWARE APPROVAL ACTIVE]" -ForegroundColor Magenta
    Write-Host "    Press Key 1 (Approve) or Key 2 (Reject) on your physical MK20..." -ForegroundColor White
    $resolvedKey = $null
    $port = Get-Mk20SerialPort
    if ($port) {
        $serial = [IO.Ports.SerialPort]::new($port.DeviceID, 115200, 'None', 8, 'One')
        $serial.ReadTimeout = 150
        $serial.Open()
        $serial.DiscardInBuffer()
        $buf = [Collections.Generic.List[byte]]::new()
        $deadline = [DateTime]::UtcNow.AddSeconds(25)

        try {
            while ([DateTime]::UtcNow -lt $deadline -and -not $resolvedKey) {
                if ($serial.BytesToRead -gt 0) {
                    $chunk = [byte[]]::new($serial.BytesToRead)
                    [void]$serial.Read($chunk, 0, $chunk.Length)
                    $buf.AddRange($chunk)

                    $arr = $buf.ToArray()
                    for ($i = 0; $i -le ($arr.Length - 8); $i++) {
                        if ($arr[$i] -eq 0xAA -and $arr[$i+1] -eq 0x55) {
                            $len = $arr[$i+3]
                            if (($i + 7 + $len) -le $arr.Length -and $arr[$i+5] -eq 0x16) {
                                $p = ($arr[$i+6] -ne 0)
                                if ($p) {
                                    $meta = Get-KeyMetadata -Row ([int]$arr[$i+7]) -Col ([int]$arr[$i+8]) -Pressed $p
                                    $resolvedKey = $meta
                                    break
                                }
                            }
                        }
                    }
                }
                Start-Sleep -Milliseconds 20
            }
        }
        finally {
            if ($serial.IsOpen) { $serial.Close() }
            $serial.Dispose()
        }
    }

    if ($resolvedKey) {
        $action = if ($resolvedKey.Action -eq 'REJECT' -or $resolvedKey.KeyNumber -eq 2) { 'reject' } else { 'approve' }
        $resolvedBy = "mk20_key_$($resolvedKey.KeyNumber)"
        Write-Host ">>> Physical switch contact registered: Key $($resolvedKey.KeyNumber) -> Action: $action" -ForegroundColor Green
    } else {
        Write-Host ">>> Hardware wait timed out; falling back to auto-approval" -ForegroundColor Yellow
        $action = 'approve'
        $resolvedBy = 'timeout_fallback'
    }
} elseif ($InteractiveApproval) {
    Write-Host "`n>>> Press [Y] to approve or [N] to reject: " -NoNewline -ForegroundColor Yellow
    $key = [Console]::ReadKey($true).KeyChar
    $action = if ($key -eq 'y' -or $key -eq 'Y') { 'approve' } else { 'reject' }
    $resolvedBy = 'user_keystroke'
} else {
    Write-Host ">>> (Auto-approving in synthetic playback mode)" -ForegroundColor DarkGray
    $action = 'approve'
    $resolvedBy = 'synthetic_auto_agent'
}

# 10. approval.resolved
Emit-Event -Type 'approval.resolved' -Data @{
    approvalId = 'appr-401'
    action = $action
    resolvedBy = $resolvedBy
} -Color Cyan
Start-Sleep -Milliseconds $StepDelayMs

# 11. session.complete
Emit-Event -Type 'session.complete' -Data @{
    status = 'success'
    turnsTotal = 1
    toolsRun = 2
    summary = 'Synthetic session completed: all invariants preserved.'
} -Color Green

Write-Host "`n=== Session Playback Complete ===" -ForegroundColor Cyan
