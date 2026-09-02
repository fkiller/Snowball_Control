# Snowball Control: Normalized Agent Event Schema

## Architectural Invariant: Host-Owned Agent Orchestration

The MK20 (Allwinner T113) acts strictly as a visual display and physical key input client (HUD).
The Windows / Snowball host owns:
- Provider sessions (Codex app-server, Claude Agent SDK, Gemini ACP).
- Workspace directories and git repositories.
- Execution tools and command runners.

Communication between the host orchestration engine and the MK20 HUD is conducted via normalized JSON events streamed over TCP / USB CDC.

---

## 1. Core Event Model

Every event conforms to the standard envelope:

```json
{
  "version": "1.0",
  "eventId": "evt-20260902-183000-001",
  "timestamp": "2026-09-02T18:30:00.123Z",
  "type": "<event-type>",
  "sessionId": "sess-a1b2c3d4",
  "data": { ... }
}
```

---

## 2. Event Types & Payloads

### 2.1 `session.init`
Emitted when a coding task session begins.

```json
{
  "type": "session.init",
  "data": {
    "provider": "codex | claude | gemini",
    "model": "gpt-5-codex | claude-3-7-sonnet | gemini-2.5-pro",
    "workspace": "Snowball_Control",
    "branch": "main",
    "objective": "Implement agent orchestration HUD"
  }
}
```

### 2.2 `turn.start`
Emitted at the beginning of an agent turn.

```json
{
  "type": "turn.start",
  "data": {
    "turnIndex": 1,
    "userPrompt": "Build the host-owned agent orchestration layer"
  }
}
```

### 2.3 `agent.state`
Emitted when the agent's internal cognitive state transitions.

```json
{
  "type": "agent.state",
  "data": {
    "state": "thinking | planning | tool_executing | waiting_approval | idle",
    "headline": "Analyzing repository structure...",
    "subtext": "Reading hardware/mk20/dev-tools/NORTH_STARS.md"
  }
}
```

### 2.4 `tool.invocation`
Emitted before a tool or command executes.

```json
{
  "type": "tool.invocation",
  "data": {
    "toolName": "run_command",
    "summary": "Building test binary",
    "details": "cargo test --workspace"
  }
}
```

### 2.5 `tool.result`
Emitted after a tool execution finishes.

```json
{
  "type": "tool.result",
  "data": {
    "toolName": "run_command",
    "success": true,
    "durationMs": 1420,
    "summary": "19 passed, 0 failed"
  }
}
```

### 2.6 `approval.requested`
Emitted when a critical action requires physical human confirmation before proceeding.

```json
{
  "type": "approval.requested",
  "data": {
    "approvalId": "appr-101",
    "severity": "low | medium | high | destructive",
    "title": "Execute Command?",
    "description": "git push origin main --force",
    "options": [
      { "keyIndex": 0, "action": "approve", "label": "Approve", "color": "#00FF00" },
      { "keyIndex": 1, "action": "reject", "label": "Reject", "color": "#FF0000" },
      { "keyIndex": 2, "action": "inspect", "label": "View Diff", "color": "#FFFF00" }
    ]
  }
}
```

### 2.7 `approval.resolved`
Emitted when the physical key press or host UI resolves the approval request.

```json
{
  "type": "approval.resolved",
  "data": {
    "approvalId": "appr-101",
    "action": "approve",
    "resolvedBy": "mk20_key_0"
  }
}
```

### 2.8 `session.complete`
Emitted when the session ends.

```json
{
  "type": "session.complete",
  "data": {
    "status": "success | cancelled | error",
    "turnsTotal": 4,
    "toolsRun": 12,
    "summary": "All tasks completed successfully."
  }
}
```

---

## 3. Physical Key Layout on MK20 (5x4 Grid)

The 20 keys are addressed by `(row, col)` with coordinates corresponding to the `getInfo` A1 response:

```text
[Header Display: 428 x 142 (Status, Session, Turn, Model)]
+-------------------------------------------------------------+
| Col 0       | Col 1       | Col 2       | Col 3       | Col 4
Row 0: | Approve     | Reject      | Retry       | Pause/Cont  | Cancel
Row 1: | Provider    | Model       | Plan        | Diff        | Shell
Row 2: | Tool Log    | Errors      | Test Run    | Git Status  | Git Push
Row 3: | Hotkey 1    | Hotkey 2    | Hotkey 3    | Mute/Vol    | Brightness
+-------------------------------------------------------------+
```

When a key is pressed, GD32/QMK sends the key event over UART to Tina Linux, which can forward the action to the host orchestrator.
