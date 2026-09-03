# Send-Mk20HudEvent.ps1 - Host HUD Controller for MK20 Screen
# Transmits real-time coding agent state updates to the MK20 native framebuffer engine over UDP 7701.

[CmdletBinding()]
param(
    [string]$DeviceIp = '192.168.69.27',
    [int]$Port = 7701,
    [ValidateSet('init', 'thinking', 'tool', 'cost', 'approval', 'clear_approval', 'key', 'dial')]
    [string]$Action = 'thinking',
    [string]$Provider = 'Claude Code 2.1',
    [string]$Model = 'deepseek-v4-pro',
    [string]$Text = '',
    [string]$ToolName = '',
    [string]$ToolSummary = '',
    [string]$Cost = '$0.000',
    [string]$Duration = '0.0s',
    [string]$ApprovalTitle = '',
    [string]$ApprovalDesc = '',
    [int]$Row = 0,
    [int]$Col = 0,
    [switch]$Pressed,
    [string]$DialAction = 'SCROLL RIGHT'
)

function Send-Mk20UdpPacket {
    param([string]$Message, [string]$Ip, [int]$TargetPort)
    $client = [Net.Sockets.UdpClient]::new()
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes($Message)
        [void]$client.Send($bytes, $bytes.Length, $Ip, $TargetPort)
    } finally {
        $client.Close()
        $client.Dispose()
    }
}

$payload = switch ($Action) {
    'init'           { "INIT:$Provider|$Model" }
    'thinking'       { "THINKING:$Text" }
    'tool'           { "TOOL:$ToolName|$ToolSummary" }
    'cost'           { "COST:$Cost" }
    'approval'       { "APPROVAL:$ApprovalTitle|$ApprovalDesc" }
    'clear_approval' { "CLEAR_APPROVAL:" }
    'key'            { "KEY:$Row|$Col|$(if ($Pressed) { 1 } else { 0 })" }
    'dial'           { "DIAL:$DialAction" }
}

if ($payload) {
    Send-Mk20UdpPacket -Message $payload -Ip $DeviceIp -TargetPort $Port
}
