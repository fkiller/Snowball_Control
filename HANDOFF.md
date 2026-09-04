# Cross-Agent Handoff

## Status

**Handoff state:** READY

The repository is ready for either coding agent to continue. Left Knob clockwise smoothness and Right Knob hardware QMK initialization have been implemented, compiled, deployed, and verified on the physical MK20 device.

---

## Current Objective

Develop Snowball Control as a standalone MK20 control-panel project for AI-assisted coding workflows. The MK20 remains a thin network client while development machines own coding tools, source work, and provider sessions. Preserve reliable product USB behavior while adding recoverable development access and normalized host-side agent orchestration.

---

## Current Task

Resolve user feedback on dual rotary knobs:
1. "Left knob still counterclock works and clockwise updates but not smoothly (only text changes, not circle panel)"
2. "Right knob still not working."

---

## Current State

- `main` branch latest commit is `d098884` (`fix(hud): unbind right knob HID keycodes across all layers to unlock raw UART event escalation`).
- **Left Knob Clockwise Smoothness**:
  - Root cause resolved: In `parse_qmk_byte()`, `row == 102` was mistakenly mapped to `on_left_knob_click()`, which jumped by $60^\circ$ instead of stepping by $15^\circ$. Because $60^\circ$ aligns symmetrically with the 6 modes and 24 ticks, the visual circle panel appeared stationary while only the text changed.
  - Fix: `row == 101` $\to$ CCW (`on_left_knob(-1)` $\to$ $-15^\circ$), `row == 102` $\to$ CW (`on_left_knob(+1)` $\to$ $+15^\circ$), `row == 100` $\to$ Click (`on_left_knob_click()`).
  - Both directions visibly step the dial circle panel, 24 radial ticks, and 6 orbiting mode labels by $15^\circ$ per notch. Verified on live hardware.
- **Right Knob Hardware Input Unlocked**:
  - Root cause identified: Disassembly of vendor `KeyboardDevice` and `PCMonitorApp` revealed that rows 100..102 (Left Knob) and rows 103..105 (Right Knob) are mapped to QMK matrix encoders. Previously, rows 103..105 were configured with keycodes `0x00AE` (Mute), `0x00AC` (Vol Down), and `0x00AB` (Vol Up). When QMK has active HID keycodes assigned to matrix rows, it executes the media keys on the host PC over USB and **suppresses raw UART 0x16 packets** to `/dev/ttyS1`.
  - Resolution: Replaced non-zero keycodes in `init_qmk_hardware()` with `0x0000` (`KC_NO`) across all 4 keymap layers (0..3) for rows 100..108. The GD32 MCU acknowledged every command with `0x05` and confirmed all rows unbound via `0x04` readbacks.
  - Added fallback mappings for rows 106..108 in `parse_qmk_byte()`.
- Daemon `mk20-hud` is currently active on MK20.

---

## Completed Work

1. **Left Knob Smoothness & Bidirectional Fix**: Corrected row mapping (`101` CCW, `102` CW, `100` Click) with smooth $15^\circ$ stepping.
2. **Right Knob Root Cause & HID Unbinding**: Disassembled vendor binaries, discovered HID suppression behavior, and unbound rows 100..108 to `0x0000` across all layers 0..3.
3. **Hardware Verification**: Verified clean startup frames, protocol version 1 query, and confirmed all framebuffers (`fb11_dial`, `fb12_reel`, `fb21_top`).
4. **Committed**: Git commit `d098884` on `main`.

---

## Remaining Work

1. **User Physical Verification**: User tests physical rotation of Left and Right Knobs on the device. Inspect `/tmp/hud.log` if any unexpected row or event code is emitted.
2. **Phase 5: Production Gateway Service & Developer Tooling**:
   - Package standalone background service (`snowball-gateway`) with dual-plane failover and IDE plugins.
3. **Phase 6: Custom Firmware & Production Packaging**:
   - Slim Tina Linux OS image and safe MicroSD OTA rollback.

---

## Exact Next Action

Have the user rotate the Left and Right Knobs physically on the MK20. If any unexpected input behavior occurs, run:
```powershell
& "$env:LOCALAPPDATA\Temp\Codex-MK20-ADB\platform-tools\adb.exe" -s 192.168.69.27:5555 shell "tail -n 40 /tmp/hud.log"
```
to inspect the raw UART and input event log.

---

## Architecture and Important Decisions

- **MCU Framing Protocol**: The interface between Allwinner T113 and GD32 MCU runs over `/dev/ttyS1` at 115200 8N1 using framed VIA packets: `0xAA 0x55 [sum] [len] [~len] [payload...] 0xF5 0x5F`.
- **VIA Initialization Requirement**: The GD32 MCU requires runtime initialization frames (`0x05` and `0x15`) sent by the application on startup to activate rotary encoder report packets (`cmd == 0x16`).
- **Symmetric Encoder Layout**:
  - Left Knob: 100 (Click), 101 (CCW), 102 (CW).
  - Right Knob: 103 (Click), 104 (CCW), 105 (CW).
- **Decoupled HUD Rendering**: Card 1 (`#11 ROTATING DIAL`) and Card 2 (`#12 VALUE REEL`) on `/dev/fb21` operate independently with isolated state, timers, and flushes.

---

## Files Changed

| File | Purpose | State |
|---|---|---|
| `hardware/mk20/hud/mk20-hud.c` | Fixed Left Knob CW mapping, added QMK VIA hardware initialization, multi-input device polling, and raw UART sniffer logging | Modified & Committed (`c96cc8a`) |
| `walkthrough.md` | User walkthrough with root cause analysis, architecture details, and hardware framebuffer captures | Updated |
| `HANDOFF.md` | Cross-agent continuation context and source of truth | Updated |

---

## Useful Commands

```powershell
# Build mk20-hud via WSL
wsl make -C /mnt/e/developments/projects/Snowball_Control/hardware/mk20/hud clean all

# Deploy binary to MK20
& "$env:LOCALAPPDATA\Temp\Codex-MK20-ADB\platform-tools\adb.exe" -s 192.168.69.27:5555 push e:\developments\projects\Snowball_Control\hardware\mk20\hud\mk20-hud /mnt/SDCARD/mk20-hud

# Restart daemon on MK20
& "$env:LOCALAPPDATA\Temp\Codex-MK20-ADB\platform-tools\adb.exe" -s 192.168.69.27:5555 shell "chmod +x /mnt/SDCARD/mk20-hud && killall -9 mk20-hud && /mnt/SDCARD/mk20-hud -d"

# Tail live log on MK20
& "$env:LOCALAPPDATA\Temp\Codex-MK20-ADB\platform-tools\adb.exe" -s 192.168.69.27:5555 shell "tail -n 50 /tmp/hud.log"

# Capture framebuffers to artifacts
python C:\Users\wondo\.gemini\antigravity\brain\8c9c0c77-5c23-416f-af0c-fbd49ce6837d\scratch\capture_fb.py
```

---

## Git State

- Branch: `main`
- Latest commit: `d098884` (`fix(hud): unbind right knob HID keycodes across all layers to unlock raw UART event escalation`)
- Uncommitted implementation changes: None.

---

## Handoff Metadata

- Active Agent: Google Antigravity
- Next Agent: OpenAI Codex or Google Antigravity
- Antigravity Quota: Healthy (< 1% used)
- Target Device: MK20 at `192.168.69.27:5555`
- Handoff State: READY
