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

- `main` branch latest commit is `c96cc8a` (`fix(hud): resolve Left Knob clockwise smoothness and unlock Right Knob input`).
- **Left Knob Clockwise Smoothness**:
  - Root cause resolved: In `parse_qmk_byte()`, `row == 102` was mistakenly mapped to `on_left_knob_click()`, which jumped by $60^\circ$ instead of stepping by $15^\circ$. Because $60^\circ$ aligns symmetrically with the 6 modes and 24 ticks, the visual circle panel appeared stationary while only the text changed.
  - Fix: `row == 101` $\to$ CCW (`on_left_knob(-1)` $\to$ $-15^\circ$), `row == 102` $\to$ CW (`on_left_knob(+1)` $\to$ $+15^\circ$), `row == 100` $\to$ Click (`on_left_knob_click()`).
  - Both directions visibly step the dial circle panel, 24 radial ticks, and 6 orbiting mode labels by $15^\circ$ per notch.
- **Right Knob Hardware Input Unlocked**:
  - Disassembled vendor `KeyboardDevice` and SDK `package/PCMonitorApp/src/serial.c`: The GD32 MCU runs QMK/VIA and requires runtime dynamic keymap and encoder binding over `/dev/ttyS1` (`id_dynamic_keymap_set_keycode = 0x05`, `id_dynamic_keymap_set_encoder = 0x15`).
  - Implemented `init_qmk_hardware()` in `mk20-hud.c`: Programs key matrix `0..3, 0..4`, Left knob rows 100..102, Right knob rows 103..105 (`0x00AE`, `0x00AC`, `0x00AB`), and VIA encoders 0 and 1.
  - The GD32 MCU acknowledged every initialization command with `0x05` and `0xFF` response frames over UART.
  - Multi-source polling active for `/dev/ttyS1`, `/dev/input/event0` (`sunxi-keyboard`), `/dev/input/event1` (`sunxi-gpadc0`), `/dev/input/event2` (`sunxi-ir`), `/dev/input/event3` (`audiocodec`), and UDP port 7701.
  - Raw UART logging active (`[UART RX ... B]`) in `/tmp/hud.log`.
- Daemon `mk20-hud` is currently active on MK20 (`PID 1668`).

---

## Completed Work

1. **Left Knob Smoothness & Bidirectional Fix**: Corrected row mapping (`101` CCW, `102` CW, `100` Click) with smooth $15^\circ$ stepping.
2. **QMK Initialization & Right Knob Binding**: Implemented VIA packet initialization sequence in `mk20-hud.c` for keycodes and rotary encoders. Verified live GD32 MCU acknowledgements.
3. **Multi-Device Polling & Sniffing**: Main loop polls `/dev/ttyS1`, `/dev/input/event0..3`, and UDP 7701.
4. **Hardware Verification**: Captured and verified `fb11_dial.bmp`, `fb12_reel.bmp`, and `fb21_top.bmp`.
5. **Committed**: Git commit `c96cc8a` on `main`.

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
- Latest commit: `c96cc8a` (`fix(hud): resolve Left Knob clockwise smoothness and unlock Right Knob input`)
- Uncommitted implementation changes: None.

---

## Handoff Metadata

- Active Agent: Google Antigravity
- Next Agent: OpenAI Codex or Google Antigravity
- Antigravity Quota: Healthy (< 1% used)
- Target Device: MK20 at `192.168.69.27:5555`
- Handoff State: READY
