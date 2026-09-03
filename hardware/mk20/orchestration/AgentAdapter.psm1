<#
.SYNOPSIS
    AgentAdapter - Live coding agent adapter for MK20 control panel.
.DESCRIPTION
    Bridges live sessions from OpenAI Codex, Anthropic Claude Code, and Google Antigravity
    into the normalized SCHEMA.md event pipeline, routing tool executions and approval
    gates to physical MK20 hardware switches.
#>

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Import-Module (Resolve-Path "$PSScriptRoot\..\dev-tools\Mk20Protocol.psm1") -Force
. (Resolve-Path "$PSScriptRoot\Listen-Mk20Keys.ps1")

function Convert-ClaudeStreamEvent {
    param($Raw)

    $normalized = $null
    $type = $Raw.type

    switch ($type) {
        'system' {
            if ($Raw.subtype -eq 'init') {
                $normalized = @{
                    type = 'session.init'
                    data = @{
                        sessionId = $Raw.session_id
                        model = $Raw.model
                        tools = $Raw.tools
                        summary = "Initialized Claude Code session ($($Raw.model))"
                    }
                }
            }
        }
        'assistant' {
            $msg = $Raw.message
            if ($msg.content) {
                foreach ($block in $msg.content) {
                    if ($block.type -eq 'thinking') {
                        $normalized = @{
                            type = 'agent.state'
                            data = @{
                                state = 'thinking'
                                headline = if ($block.thinking) { ($block.thinking -split "`n")[0] } else { 'Thinking...' }
                            }
                        }
                    } elseif ($block.type -eq 'tool_use') {
                        $normalized = @{
                            type = 'tool.invocation'
                            data = @{
                                toolName = $block.name
                                toolId = $block.id
                                input = $block.input
                                summary = "Tool: $($block.name)"
                            }
                        }
                    } elseif ($block.type -eq 'text') {
                        $normalized = @{
                            type = 'agent.state'
                            data = @{
                                state = 'responding'
                                headline = if ($block.text) { ($block.text -split "`n")[0] } else { 'Responding' }
                            }
                        }
                    }
                }
            }
        }
        'user' {
            # In Claude stream, tool results are sent as user messages
            if ($Raw.message.content) {
                foreach ($block in $Raw.message.content) {
                    if ($block.type -eq 'tool_result') {
                        $normalized = @{
                            type = 'tool.result'
                            data = @{
                                toolId = $block.tool_use_id
                                success = (-not [bool]$block.is_error)
                                summary = if ($block.content) { ($block.content.ToString() -split "`n")[0] } else { 'Completed' }
                            }
                        }
                    }
                }
            }
        }
        'result' {
            $normalized = @{
                type = 'session.complete'
                data = @{
                    status = if ($Raw.is_error) { 'failed' } else { 'success' }
                    durationMs = $Raw.duration_ms
                    turnsTotal = $Raw.num_turns
                    costUsd = $Raw.total_cost_usd
                    summary = "Session completed in $($Raw.duration_ms)ms (Cost: `$$($Raw.total_cost_usd))"
                }
            }
        }
        'message_start' {
            $normalized = @{
                type = 'turn.start'
                data = @{
                    turnId = $Raw.message.id
                    role = $Raw.message.role
                    model = $Raw.message.model
                    summary = "Starting turn $($Raw.message.id)"
                }
            }
        }
        'tool_use' {
            $normalized = @{
                type = 'tool.invocation'
                data = @{
                    toolName = $Raw.name
                    toolId = $Raw.id
                    input = $Raw.input
                    summary = "Tool: $($Raw.name)"
                }
            }
        }
        'tool_result' {
            $normalized = @{
                type = 'tool.result'
                data = @{
                    toolId = $Raw.tool_use_id
                    success = (-not [bool]$Raw.is_error)
                    summary = if ($Raw.content) { ($Raw.content.ToString() -split "`n")[0] } else { 'Execution completed' }
                }
            }
        }
    }

    return $normalized
}

function Convert-CodexStreamEvent {
    param($Raw)

    $normalized = $null
    $type = $Raw.type

    switch ($type) {
        'turn.start' {
            $normalized = @{
                type = 'turn.start'
                data = @{
                    turnId = $Raw.id
                    prompt = $Raw.prompt
                }
            }
        }
        'tool_call' {
            $normalized = @{
                type = 'tool.invocation'
                data = @{
                    toolName = $Raw.name
                    summary = "Executing $($Raw.name) $($Raw.command)"
                }
            }
        }
        'tool_result' {
            $normalized = @{
                type = 'tool.result'
                data = @{
                    toolName = $Raw.name
                    success = ($Raw.exit_code -eq 0)
                    summary = if ($Raw.output) { ($Raw.output.ToString() -split "`n")[0] } else { 'Done' }
                }
            }
        }
        'approval_request' {
            $normalized = @{
                type = 'approval.requested'
                data = @{
                    approvalId = $Raw.id
                    command = $Raw.command
                    description = $Raw.description
                }
            }
        }
    }

    return $normalized
}

function Start-Mk20LiveSession {
    [CmdletBinding()]
    param(
        [ValidateSet('codex', 'claude', 'antigravity')]
        [string]$Provider = 'claude',

        [Parameter(Mandatory=$true)]
        [string]$Prompt,

        [string]$WorkingDir = (Get-Location).Path,
        [switch]$RequireHardwareApproval,
        [string]$PortName
    )

    $sessionId = [Guid]::NewGuid().ToString()
    Write-Host "`n=== Snowball Control: Live Agent Session ===" -ForegroundColor Cyan
    Write-Host "Session ID: $sessionId"
    Write-Host "Provider:   $Provider"
    Write-Host "Directory:  $WorkingDir"
    Write-Host "Objective:  $Prompt"
    Write-Host "Hardware:   $(if ($RequireHardwareApproval) { 'ENABLED (Key 1 = Approve, Key 2 = Reject)' } else { 'MONITOR ONLY' })`n" -ForegroundColor Yellow

    if (-not $PortName) {
        $port = Get-Mk20SerialPort
        if ($port) { $PortName = $port.DeviceID }
    }

    # Verify command executable exists
    $cmd = $null
    $cmdArgs = @()

    switch ($Provider) {
        'claude' {
            $cmd = "C:\Users\wondo\.local\bin\claude.exe"
            if (-not (Test-Path $cmd)) { throw "Claude Code executable not found at $cmd" }
            $cmdArgs = @('-p', '--verbose', '--output-format', 'stream-json', $Prompt)
        }
        'codex' {
            $cmd = "codex"
            $cmdArgs = @('exec', '--json', $Prompt)
        }
        'antigravity' {
            $cmd = "C:\Users\wondo\AppData\Local\agy\bin\agy.exe"
            $cmdArgs = @($Prompt)
        }
    }

    $timestamp = (Get-Date).ToString('HH:mm:ss.fff')
    Write-Host "[$timestamp] [session.init      ] Starting $Provider agent process..." -ForegroundColor Cyan

    $psi = [Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $cmd
    $escapedArgs = $cmdArgs | ForEach-Object { if ($_ -match '\s|"' -and $_ -notmatch '^".*"$') { "`"$($_ -replace '"', '\"')`"" } else { $_ } }
    $psi.Arguments = $escapedArgs -join ' '
    $psi.WorkingDirectory = $WorkingDir
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true

    $proc = [Diagnostics.Process]::Start($psi)

    try {
        while (-not $proc.HasExited -or -not $proc.StandardOutput.EndOfStream) {
            $line = $proc.StandardOutput.ReadLine()
            if ($line) {
                $now = (Get-Date).ToString('HH:mm:ss.fff')
                $parsed = $null
                try {
                    $parsed = $line | ConvertFrom-Json -ErrorAction SilentlyContinue
                } catch {}

                if ($parsed) {
                    $norm = if ($Provider -eq 'claude') { Convert-ClaudeStreamEvent $parsed } else { Convert-CodexStreamEvent $parsed }
                    if ($norm) {
                        $color = 'White'
                        switch ($norm.type) {
                            'turn.start'        { $color = 'Cyan' }
                            'agent.state'       { $color = 'Gray' }
                            'tool.invocation'   { $color = 'Yellow' }
                            'tool.result'       { $color = 'Green' }
                            'approval.requested'{ $color = 'Magenta' }
                        }
function Get-SafeProp {
    param($Obj, [string]$PropName)
    if (-not $Obj) { return $null }
    if ($Obj -is [System.Collections.IDictionary]) {
        if ($Obj.Contains($PropName)) { return $Obj[$PropName] }
        return $null
    }
    $p = $Obj.PSObject.Properties[$PropName]
    if ($p) { return $p.Value }
    return $null
}

                        $summaryText = Get-SafeProp $norm.data 'summary'
                        if (-not $summaryText) { $summaryText = Get-SafeProp $norm.data 'headline' }
                        if (-not $summaryText) { $summaryText = Get-SafeProp $norm.data 'toolName' }
                        if (-not $summaryText) { $summaryText = '' }
                        Write-Host "[$now] [$($norm.type.PadRight(18))] $summaryText" -ForegroundColor $color

                        # Check if modifying tool call requires hardware approval
                        $toolName = Get-SafeProp $norm.data 'toolName'

                        # Push update to physical MK20 HUD screen
                        try {
                            switch ($norm.type) {
                                'turn.start'  { & "$PSScriptRoot\Send-Mk20HudEvent.ps1" -Action thinking -Text "Starting turn: $summaryText" -ErrorAction SilentlyContinue }
                                'agent.state' { & "$PSScriptRoot\Send-Mk20HudEvent.ps1" -Action thinking -Text $summaryText -ErrorAction SilentlyContinue }
                                'tool.invocation' { & "$PSScriptRoot\Send-Mk20HudEvent.ps1" -Action tool -ToolName $toolName -ToolSummary $summaryText -ErrorAction SilentlyContinue }
                                'session.complete' {
                                    $costVal = Get-SafeProp $norm.data 'cost'
                                    if ($costVal) { & "$PSScriptRoot\Send-Mk20HudEvent.ps1" -Action cost -Cost $costVal -ErrorAction SilentlyContinue }
                                }
                            }
                        } catch {}

                        $isModifying = $norm.type -eq 'tool.invocation' -and ($toolName -match 'Bash|Write|Edit|execute_command|patch')
                        if ($isModifying -and $RequireHardwareApproval) {
                            try {
                                & "$PSScriptRoot\Send-Mk20HudEvent.ps1" -Action approval -ApprovalTitle "Execute $toolName?" -ApprovalDesc $summaryText -ErrorAction SilentlyContinue
                            } catch {}
                            Write-Host "`n>>> [HARDWARE APPROVAL REQUIRED FOR $toolName]" -ForegroundColor Magenta
                            Write-Host "    Press Key 1 (Approve) or Key 2 (Reject) on the physical MK20..." -ForegroundColor White
                            
                            $resolved = $null
                            if ($PortName) {
                                $serial = [IO.Ports.SerialPort]::new($PortName, 115200, 'None', 8, 'One')
                                $serial.ReadTimeout = 150
                                $serial.Open()
                                $serial.DiscardInBuffer()
                                $buf = [Collections.Generic.List[byte]]::new()
                                $deadline = [DateTime]::UtcNow.AddSeconds(15)

                                try {
                                    while ([DateTime]::UtcNow -lt $deadline -and -not $resolved) {
                                        if ($serial.BytesToRead -gt 0) {
                                            $chunk = [byte[]]::new($serial.BytesToRead)
                                            [void]$serial.Read($chunk, 0, $chunk.Length)
                                            $buf.AddRange($chunk)
                                            $arr = $buf.ToArray()
                                            for ($i = 0; $i -le ($arr.Length - 8); $i++) {
                                                if ($arr[$i] -eq 0xAA -and $arr[$i+1] -eq 0x55 -and $arr[$i+5] -eq 0x16) {
                                                    if ($arr[$i+6] -ne 0) {
                                                        $meta = Get-KeyMetadata -Row ([int]$arr[$i+7]) -Col ([int]$arr[$i+8]) -Pressed $true
                                                        $resolved = $meta
                                                        break
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

                            if ($resolved -and ($resolved.Action -eq 'REJECT' -or $resolved.KeyNumber -eq 2)) {
                                Write-Host ">>> REJECTED by physical Key 2! Aborting operation." -ForegroundColor Red
                                try { & "$PSScriptRoot\Send-Mk20HudEvent.ps1" -Action clear_approval -ErrorAction SilentlyContinue } catch {}
                                $proc.Kill()
                                break
                            } else {
                                Write-Host ">>> APPROVED by physical Key 1 (or timeout). Proceeding..." -ForegroundColor Green
                                try { & "$PSScriptRoot\Send-Mk20HudEvent.ps1" -Action clear_approval -ErrorAction SilentlyContinue } catch {}
                            }
                        }
                    }
                } else {
                    # Non-JSON text output
                    Write-Host "[$now] [agent.output      ] $line" -ForegroundColor DarkGray
                }
            }
        }
    }
    finally {
        if (-not $proc.HasExited) {
            $proc.Kill()
        }
        $proc.Dispose()
    }

    $nowEnd = (Get-Date).ToString('HH:mm:ss.fff')
    Write-Host "`n[$nowEnd] [session.complete  ] Live session finished." -ForegroundColor Cyan
}

Export-ModuleMember -Function @(
    'Convert-ClaudeStreamEvent',
    'Convert-CodexStreamEvent',
    'Start-Mk20LiveSession'
)
