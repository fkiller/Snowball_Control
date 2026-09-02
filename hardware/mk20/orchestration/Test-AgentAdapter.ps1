<#
.SYNOPSIS
    Test-AgentAdapter - Automated unit tests for live agent event normalization.
.DESCRIPTION
    Validates conversion of streaming JSON events from Anthropic Claude Code
    and OpenAI Codex into the normalized SCHEMA.md event pipeline.
#>

[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Import-Module (Resolve-Path "$PSScriptRoot\AgentAdapter.psm1") -Force

$passCount = 0
$failCount = 0

function Assert-Equal {
    param($Actual, $Expected, [string]$TestName)
    if ($Actual -eq $Expected) {
        Write-Host "  [PASS] $TestName" -ForegroundColor Green
        $script:passCount++
    } else {
        Write-Host "  [FAIL] $TestName (Expected: '$Expected', Got: '$Actual')" -ForegroundColor Red
        $script:failCount++
    }
}

Write-Host "=== Running AgentAdapter Event Normalization Tests ===" -ForegroundColor Cyan

# Test 1: Claude message_start -> turn.start
$claudeMsgStart = @{
    type = 'message_start'
    message = @{
        id = 'msg_101'
        role = 'assistant'
        model = 'claude-3-7-sonnet'
    }
}
$norm1 = Convert-ClaudeStreamEvent $claudeMsgStart
Assert-Equal $norm1.type 'turn.start' 'Claude message_start maps to turn.start'
Assert-Equal $norm1.data.turnId 'msg_101' 'Claude turnId extracted correctly'
Assert-Equal $norm1.data.model 'claude-3-7-sonnet' 'Claude model extracted correctly'

# Test 2: Claude tool_use -> tool.invocation
$claudeToolUse = @{
    type = 'tool_use'
    id = 'tool_202'
    name = 'Bash'
    input = @{ command = 'git status' }
}
$norm2 = Convert-ClaudeStreamEvent $claudeToolUse
Assert-Equal $norm2.type 'tool.invocation' 'Claude tool_use maps to tool.invocation'
Assert-Equal $norm2.data.toolName 'Bash' 'Claude tool name extracted'
Assert-Equal $norm2.data.toolId 'tool_202' 'Claude tool ID extracted'

# Test 3: Claude tool_result -> tool.result
$claudeToolRes = @{
    type = 'tool_result'
    tool_use_id = 'tool_202'
    is_error = $false
    content = "On branch main`nYour branch is up to date"
}
$norm3 = Convert-ClaudeStreamEvent $claudeToolRes
Assert-Equal $norm3.type 'tool.result' 'Claude tool_result maps to tool.result'
Assert-Equal $norm3.data.success $true 'Claude tool success extracted'
Assert-Equal $norm3.data.summary 'On branch main' 'Claude single-line summary generated'

# Test 4: Codex turn.start
$codexTurnStart = @{
    type = 'turn.start'
    id = 'turn_999'
    prompt = 'Refactor database models'
}
$norm4 = Convert-CodexStreamEvent $codexTurnStart
Assert-Equal $norm4.type 'turn.start' 'Codex turn.start maps to turn.start'
Assert-Equal $norm4.data.turnId 'turn_999' 'Codex turnId extracted'

# Test 5: Codex tool_call -> tool.invocation
$codexToolCall = @{
    type = 'tool_call'
    name = 'execute_command'
    command = 'pytest tests/'
}
$norm5 = Convert-CodexStreamEvent $codexToolCall
Assert-Equal $norm5.type 'tool.invocation' 'Codex tool_call maps to tool.invocation'
Assert-Equal $norm5.data.toolName 'execute_command' 'Codex tool name extracted'

# Test 6: Codex approval_request -> approval.requested
$codexApproval = @{
    type = 'approval_request'
    id = 'appr_777'
    command = 'rm -rf /tmp/test'
    description = 'Delete temp folder'
}
$norm6 = Convert-CodexStreamEvent $codexApproval
Assert-Equal $norm6.type 'approval.requested' 'Codex approval maps to approval.requested'
Assert-Equal $norm6.data.approvalId 'appr_777' 'Codex approvalId extracted'

Write-Host "`n=== Test Summary: $passCount passed, $failCount failed ===" -ForegroundColor $(if ($failCount -eq 0) { 'Green' } else { 'Red' })
if ($failCount -gt 0) { throw "AgentAdapter test suite failed with $failCount errors" }
