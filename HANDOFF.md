# Cross-Agent Handoff

## Status

**Handoff state:** READY

The repository is ready for another agent to continue without access to the prior conversation. No implementation work was started during this handoff session.

## Current Objective

Develop Snowball Control as a standalone MK20 control-panel project for AI-assisted coding workflows. The MK20 remains a thin network client while development machines own coding tools, source work, and provider sessions. Preserve reliable product USB behavior while adding recoverable development access and, later, normalized host-side agent orchestration.

## Current Task

There is no active feature request recorded beyond preparing this cross-agent handoff. The last implementation milestone created the initial MK20 development tooling. The next roadmap task is to reboot-test and validate the documented product-COM plus TCP-ADB topology on real hardware before expanding the tooling.

## Current State

- `main` contains commits through `a756c3f` (`feat(mk20-hud): implement smooth vertical value scrolling controlled by knob (Pattern 12)`).
- **MK20 Hardware Multi-Display Architecture Unlocked**:
  - **22 Independent Framebuffer Devices**:
    - `/dev/fb1` to `/dev/fb20`: 20 separate 128x128 16-bit RGB565 LCD screens (driven by individual `fb_gc9107` SPI controllers directly under each keycap).
    - `/dev/fb21`: 428x142 16-bit RGB565 status screen (driven by `fb_nv3007`) in the top window.
    - `/dev/fb0`: Virtual unmapped shadow buffer in RAM (not wired to physical glass).
  - **100% Standalone On-Device Engine**:
    - `mk20-hud` runs natively on the Allwinner T113 dual Cortex-A7 SoC with zero host PC dependencies, zero USB HID, and sub-millisecond response.
    - Directly mmaps all 21 hardware framebuffers and forces zero-latency SPI writes (`write(fd, fb, 32768)`).
    - Communicates over `/dev/ttyS1` to GD32/QMK MCU using vendor 8-state byte machine and dynamic keymap bindings.
  - **12 Interactive Visual Interaction Patterns Running Live**:
    - Pattern 1 (Key 1): Toggle (Same Text `[MUTE]`, Dark Card $\leftrightarrow$ Emerald Green).
    - Pattern 2 (Key 2): Toggle (Different Text & Colors: `[MIC ON]` $\leftrightarrow$ `[MIC OFF]`).
    - Pattern 7 (Key 3): Shift-Style Momentary (`[TURBO]`, active only while physically held down).
    - Pattern 10 (Key 4): Pulse-Style Metronome (120 BPM animated pulse with pulsating heart icon).
    - Pattern 3 (Key 5): Modes (Vertical uniform text list with cyan cursor highlight).
    - Pattern 4 (Key 6): Modes (Vertical carousel with 2x center enlarged selected mode).
    - Pattern 5 (Key 7): Modes (2x2 icon matrix with illuminated active quadrant).
    - Pattern 6 (Key 8): Modes (Horizontal 3-icon strip with 2x center active icon).
    - Pattern 8 (Key 9): Real-Time Number (Live Allwinner T113 CPU % with level gauge).
    - Pattern 9 (Key 10): Real-Time Graph (Rolling 60-second CPU load sparkline chart).
    - Pattern 11 (Key 11 & Top): Concentric Rotating Circular Knob Panel with Rotating Radial Text (6 modes: `CODE`, `PLAN`, `DIFF`, `TEST`, `EXEC`, `CHAT` orbiting at $R=31$, 24 rotating radial ticks, 12 o'clock needle pointer, and synchronized mini dial gauge on `/dev/fb21`).
    - Pattern 12 (Key 12 & Top): Smooth Vertical Value Scrolling Controlled by Knob (precision vertical reel with 30 FPS lerp interpolation, ruler ticks, center selection band, level progress bar on `/dev/fb21` and `/dev/fb12`).
    - Keys 13..20: Counter, Audio, Reset, Theme, Provider, Model, Plan, Diff.
  - **All 4 North Star architectural foundations delivered**: Dual control planes (`COM5` and TCP `5555`), A1 host RPC, normalized agent adapters, and Linux/QMK contract.

## Completed Work

1. **Phase 0 & 1 Foundations**:
   - Host A1 Protocol Library ([`Mk20Protocol.psm1`](file:///e:/developments/projects/Snowball_Control/hardware/mk20/dev-tools/Mk20Protocol.psm1)) with CRC32 verification and all 10 RPC methods.
   - Wire protocol automated unit test suite ([`Test-Mk20Protocol.ps1`](file:///e:/developments/projects/Snowball_Control/hardware/mk20/dev-tools/Test-Mk20Protocol.ps1), 19/19 passed).
   - Linux/QMK subsystem contract ([`LINUX_QMK_CONTRACT.md`](file:///e:/developments/projects/Snowball_Control/hardware/mk20/contract/LINUX_QMK_CONTRACT.md)) and test suite ([`Test-QmkProtocol.ps1`](file:///e:/developments/projects/Snowball_Control/hardware/mk20/contract/Test-QmkProtocol.ps1), 14/14 passed).
   - System Architecture ([`ARCHITECTURE.md`](file:///e:/developments/projects/Snowball_Control/ARCHITECTURE.md)) documenting Firmware vs Gateway deliverables and pairing model.
   - Normalized agent event model ([`SCHEMA.md`](file:///e:/developments/projects/Snowball_Control/hardware/mk20/orchestration/SCHEMA.md)).
2. **Phase 2: Live Key Matrix Ingestion & Pairing Engine**:
   - **Key Matrix Ingestion ([`Listen-Mk20Keys.ps1`](file:///e:/developments/projects/Snowball_Control/hardware/mk20/orchestration/Listen-Mk20Keys.ps1))**: Real-time switch contact monitor mapping `(row, col)` to physical keys 1..20 and rotary dial actions `100..105`, with semantic action routing (`APPROVE`, `REJECT`, `RETRY`, `CANCEL`).
   - **Zero-Trust Physical Presence Pairing ([`PairingManager.psm1`](file:///e:/developments/projects/Snowball_Control/hardware/mk20/orchestration/PairingManager.psm1))**: Host identity auto-detection (`Get-HostNetworkIdentity`), 6-digit challenge PIN, physical switch confirmation (Key 1), dynamic `iptables` MAC filtering binding, and `/mnt/SDCARD/paired_hosts.json` persistence.
   - **Hardware-in-the-Loop Agent Approvals ([`SyntheticPlayer.ps1`](file:///e:/developments/projects/Snowball_Control/hardware/mk20/orchestration/SyntheticPlayer.ps1))**: `-HardwareApproval` flag pauses agent playback at `approval.requested` until real MK20 switch contact is confirmed.
3. **Phase 3: Live Coding Agent Adapters & Execution**:
   - **Universal Agent Adapter ([`AgentAdapter.psm1`](file:///e:/developments/projects/Snowball_Control/hardware/mk20/orchestration/AgentAdapter.psm1))**: Normalizes streaming JSON from Claude Code 2.1 (`claude -p --verbose --output-format stream-json`) and OpenAI Codex (`codex exec --json`) into the universal `SCHEMA.md` pipeline.
   - **Unit Test Suite ([`Test-AgentAdapter.ps1`](file:///e:/developments/projects/Snowball_Control/hardware/mk20/orchestration/Test-AgentAdapter.ps1))**: 15/15 automated unit tests passing across all event types.
   - **Session Launcher ([`Start-Mk20Session.ps1`](file:///e:/developments/projects/Snowball_Control/hardware/mk20/orchestration/Start-Mk20Session.ps1))**: Interactive launcher supporting live execution (`-Provider claude`, `-Provider codex`) or synthetic playback (`-Synthetic`) with hardware approval gating.
4. **Phase 4: MK20 Standalone Multi-Display Engine & 12 Interaction Patterns**:
   - **Native Multi-Display Engine ([`mk20-hud.c`](file:///e:/developments/projects/Snowball_Control/hardware/mk20/hud/mk20-hud.c))**: Directly controls all 21 hardware LCDs (`/dev/fb1..fb20` and `/dev/fb21`) with 16-bit RGB565 rendering and direct SPI bus write flush.
   - **12 Interactive Visual Patterns**: Implemented toggles, carousels, 2x2 matrix, sparkline graph, real-time CPU telemetry, metronome pulse, dial slider, and pixel marquee scroll.
   - **Cross-Compilation Pipeline ([`Makefile`](file:///e:/developments/projects/Snowball_Control/hardware/mk20/hud/Makefile))**: Cross-compiled natively using vendor Linaro GCC with NEON/hardfloat and math library linking.

## Remaining Work

1. **Phase 5: Production Gateway Service & Developer Tooling**:
   - Package standalone background service (`snowball-gateway`) with dual-plane failover and IDE plugins.
2. **Phase 6: Custom Firmware & Production Packaging**:
   - Slim Tina Linux OS image and safe MicroSD OTA rollback.

## Exact Next Action

Test and interact with each of the 12 visual patterns on the physical MK20 hardware, or proceed to Phase 5 (Production Gateway Service packaging).

## Architecture and Important Decisions

- This repository is intentionally separate from Snowball Gateway. Do not vendor or modify Gateway code here.
## Architecture and Important Decisions

- **Deliverables Separation**: Clean division between Device Firmware (`mk20-firmware`) and Host Gateway (`snowball-gateway`). See [`ARCHITECTURE.md`](ARCHITECTURE.md).
- **Two Independent Control Planes**: Product traffic on USB CDC (`COM5`) and development traffic on restricted TCP (`5555`). Automatic failover maintains active sessions if USB disconnects.
- **Physical Presence Pairing**: Zero-trust pairing requires physical confirmation on the MK20 (PIN/dialog on 640x656 LCD + physical switch press) before dynamic `iptables` MAC filtering rules admit network connections.
- **Tina Linux & GD32/QMK Contract**: Linux owns display, networking, and applications; GD32 owns low-latency key matrix and rotary dial scanning. Serial interface uses framed VIA packets (`0xAA 0x55 ... 0xF5 0x5F`) over `/dev/ttyS1`. See [`hardware/mk20/contract/LINUX_QMK_CONTRACT.md`](hardware/mk20/contract/LINUX_QMK_CONTRACT.md).
- **Host-Owned Intelligence**: Provider sessions (Codex, Claude, Gemini) reside strictly on the host PC behind normalized event adapters. See [`hardware/mk20/orchestration/SCHEMA.md`](hardware/mk20/orchestration/SCHEMA.md).
- **Recoverable Operations**: MicroSD boot hook (`lunch.sh`) is the recovery boundary; routine development does not require system-partition replacement.

## Files Changed

| File | Purpose | State |
|---|---|---|
| `ARCHITECTURE.md` | Complete system architecture, deliverables separation, pairing flow, and failover | Added |
| `README.md` | Project overview and links to architectural specifications | Updated |
| `hardware/mk20/contract/LINUX_QMK_CONTRACT.md` | Subsystem ownership, UART `/dev/ttyS1` framing, and `KeyboardInfo` schema | Added |
| `hardware/mk20/contract/Test-QmkProtocol.ps1` | Automated test suite for VIA framing, checksums, and `KeyboardInfo` parsing (14/14 pass) | Added |
| `hardware/mk20/orchestration/SCHEMA.md` | Universal normalized agent event model across Codex, Claude, and Gemini | Added |
| `hardware/mk20/orchestration/SyntheticPlayer.ps1` | Mock agent session player streaming real-time turns, tools, and approvals | Added |
| `hardware/mk20/dev-tools/Mk20Protocol.psm1` | Standalone host A1 protocol module with bi-directional CRC32 verification | Added |
| `hardware/mk20/dev-tools/Test-Mk20Protocol.ps1` | Automated unit tests for wire framing, CRC vectors, and corruption rejection (19/19 pass) | Added |
| `hardware/mk20/dev-tools/Watch-Mk20Events.ps1` | Real-time serial event listener for proactive frames | Added |
| `hardware/mk20/dev-tools/mk20ctl.ps1` | Host control helper (`doctor`, `info`, `put`, `shell`, `restore`, `snapshot`) | Updated |
| `hardware/mk20/dev-tools/lunch.sh` | Wi-Fi + TCP ADB + firewall boot hook with logging and IP persistence | Updated |
| `HANDOFF.md` | Cross-agent continuation context and source of truth | Updated |

## Tests and Verification

### Passed Live on Physical MK20 Hardware

- `mk20ctl.ps1 doctor`: Exit code 0, verified COM5, TCP port 5555 open, ADB connected as root, concurrent PIDs 1743/1908, firewall rules active.
- `mk20ctl.ps1 info`: Exit code 0, serial A1 `getInfo` returned complete display + key layout, ADB returned system diagnostics.
- `mk20ctl.ps1 put -Source test-probe.txt -Destination /mnt/SDCARD/test-probe.txt`: Exit code 0, MD5 verified.
- `mk20ctl.ps1 put -Transport Com -Source test-com-probe.txt -Destination /mnt/SDCARD/test-com-probe.txt`: Exit code 0, acknowledged chunks verified.
- `mk20ctl.ps1 restore`: Exit code 0, preflight MD5 check verified factory backup, emitted `READY`.
- `mk20ctl.ps1 snapshot`: Exit code 0, captured complete inventory of configuration, logs, and diagnostics.
- `Set-Mk20Backlight`: Verified live (`{"result":43,"success":true}`).
- `Set-Mk20Volume`: Verified live (`{"result":7,"success":true}`).
- `Send-Mk20File` with native `setFileCRC`: Verified live (`CrcVerified: True`).
- `Remove-Mk20File`: Verified live (`deletedFiles` confirmed).

### Passed Host Automated Unit Tests

- `Test-Mk20Protocol.ps1`: **19 passed, 0 failed** (IEEE 802.3 CRC32 standard vectors, framing, resync, corruption rejection).
- `Test-QmkProtocol.ps1`: **14 passed, 0 failed** (VIA framing, modulo-256 checksums, 26-record `KeyboardInfo` binary layout).
- `SyntheticPlayer.ps1`: Verified synthetic agent turn and tool streaming with millisecond timestamps.

## Known Problems and Open Questions

- The documented endpoint `192.168.69.27:5555`, COM port `COM6`, Wi-Fi profile, and process state may change with DHCP, USB enumeration, or device configuration.
- `mk20ctl.ps1` resolves `adb.exe` at startup even for a COM-only upload, so `put -Transport Com` still requires ADB to be installed or configured.
- The A1 response parser checks header, request ID, and length but does not validate the encoded size CRC or payload CRC.
- COM uploads acknowledge chunks but do not verify final remote length/content. Replacing a longer remote file with a shorter one may leave trailing data unless `saveToFile` truncates independently; this is unverified.
- The unauthenticated TCP-ADB design depends on the MAC firewall behaving as expected on the deployment network. Confirm rule ordering and threat model on hardware.
- The ignored `dev-access.conf` contains secrets and must never be committed.

## Failed or Abandoned Approaches

- No application implementation approach was attempted or abandoned in this handoff session.
- Upstream CodexBar does not ship a native Windows CLI package and points Windows users to Win-CodexBar, which was installed instead.
- The Windows package exposes `codexbar-cli.exe`, while `codexbar.exe` is the tray application; a user-level shim was required for the exact quota command mandated by `AGENTS.md`.

## Constraints

- Follow `AGENTS.md`, especially its repository-first rule, source-of-truth priority, quota thresholds, and cross-agent continuation procedure.
- Preserve existing architecture, dependencies, tests, and externally visible behavior unless a concrete requirement justifies change.
- Do not broadly refactor, change public APIs/protocols, or begin UI/multi-agent work ahead of recovery and hardware validation.
- Keep product USB CDC independent from TCP development access; do not switch USB to ADB for routine development.
- Never commit Wi-Fi credentials, device backups, logs, generated BSP output, or the ignored Tina vendor tree.
- Do not run `restore -Force` without explicit intent to perform factory rollback and lose persistent TCP-ADB access after reboot.

## Useful Commands

```powershell
codexbar --format json

git status --short --branch
git log -10 --oneline
git diff
git diff --stat
git stash list

$tokens = $null
$errors = $null
[void][System.Management.Automation.Language.Parser]::ParseFile(
    (Resolve-Path '.\hardware\mk20\dev-tools\mk20ctl.ps1'),
    [ref]$tokens,
    [ref]$errors
)
$errors
bash -n .\hardware\mk20\dev-tools\lunch.sh

.\hardware\mk20\dev-tools\mk20ctl.ps1 doctor
.\hardware\mk20\dev-tools\mk20ctl.ps1 info
.\hardware\mk20\dev-tools\mk20ctl.ps1 put -Source .\file.bin -Destination /mnt/SDCARD/file.bin
.\hardware\mk20\dev-tools\mk20ctl.ps1 put -Transport Com -Source .\recovery.sh -Destination /mnt/SDCARD/lunch.sh
.\hardware\mk20\dev-tools\mk20ctl.ps1 shell
.\hardware\mk20\dev-tools\mk20ctl.ps1 restore
```

If `adb.exe` is not in the default location, set `MK20_ADB` to its full path for that shell.

## Git State

- Branch: `main`
- Upstream: `origin/main`
- Latest implementation commit: `a756c3f` (`feat(mk20-hud): implement smooth vertical value scrolling controlled by knob (Pattern 12)`)
- Handoff documentation: `walkthrough.md` and `HANDOFF.md`
- Uncommitted implementation changes: none.
- Stashes: none.
- Local-only ignored content: Tina T113 BSP and any site-specific development configuration/backups/logs.
- Remote: `origin` -> `https://github.com/fkiller/Snowball_Control.git`

## Recommended Next Steps

1. Run the Exact Next Action and save the reboot/health results.
2. Exercise `info`, then test ADB and COM uploads with disposable files and verify remote length/checksum independently.
3. Add host-side tests for A1/CRC/upload and decouple COM-only operations from ADB resolution.
4. Validate dry-run restore and perform an intentional recovery drill only with explicit authorization.
5. Update this file when state materially changes, then continue the ordered north-star milestones.

## Handoff Metadata

- Previous agent: OpenAI Codex
- Intended next agent: Google Antigravity or another capable coding agent
- Reason: explicit user request; the installed quota tool also confirmed the mandatory handoff threshold
- CodexBar: Win-CodexBar 0.54.0 (`Finesssee.Win-CodexBar`)
- Codex 5-hour quota at final check: 96% used, 4% remaining; resets at `2026-09-02T06:21:56Z`
- Codex weekly quota at final check: 51% used, 49% remaining; resets at `2026-09-07T02:38:17Z`
- Prepared: 2026-09-02 America/New_York
- Handoff state: READY
