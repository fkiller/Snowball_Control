<#
.SYNOPSIS
    Start-Mk20Session - Launch or attach an AI coding session connected to the MK20.
.DESCRIPTION
    Launches an OpenAI Codex, Anthropic Claude Code, or synthetic agent session,
    streaming telemetry to the MK20 control panel and routing human-in-the-loop
    approvals to the physical mechanical switches (Key 1 = Approve, Key 2 = Reject).
#>

[CmdletBinding(DefaultParameterSetName='Live')]
param(
    [Parameter(Position=0, Mandatory=$false)]
    [ValidateSet('claude', 'codex', 'antigravity')]
    [string]$Provider = 'claude',

    [Parameter(Position=1, Mandatory=$false)]
    [string]$Prompt,

    [Parameter(ParameterSetName='Live')]
    [string]$WorkingDir = (Get-Location).Path,

    [Parameter(ParameterSetName='Live')]
    [switch]$RequireHardwareApproval,

    [Parameter(ParameterSetName='Synthetic')]
    [switch]$Synthetic,

    [string]$PortName
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Import-Module (Resolve-Path "$PSScriptRoot\AgentAdapter.psm1") -Force

if ($Synthetic -or -not $Prompt) {
    if (-not $Prompt) {
        $Prompt = "Verify live MK20 HUD connection and hardware switch contact"
    }
    Write-Host "`n>>> Running Synthetic Session Player on MK20..." -ForegroundColor Cyan
    & "$PSScriptRoot\SyntheticPlayer.ps1" -Provider $Provider -Objective $Prompt -HardwareApproval:$RequireHardwareApproval
    return
}

Start-Mk20LiveSession -Provider $Provider -Prompt $Prompt -WorkingDir $WorkingDir -RequireHardwareApproval:$RequireHardwareApproval -PortName $PortName
