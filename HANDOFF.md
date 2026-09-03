# Cross-Agent Handoff

## Status

**Handoff state:** READY

The repository is ready for another agent to continue without access to the prior conversation. No implementation work was started during this handoff session.

## Current Objective

Develop Snowball Control as a standalone MK20 control-panel project for AI-assisted coding workflows. The MK20 remains a thin network client while development machines own coding tools, source work, and provider sessions. Preserve reliable product USB behavior while adding recoverable development access and, later, normalized host-side agent orchestration.

## Current Task

There is no active feature request recorded beyond preparing this cross-agent handoff. The last implementation milestone created the initial MK20 development tooling. The next roadmap task is to reboot-test and validate the documented product-COM plus TCP-ADB topology on real hardware before expanding the tooling.

## Current State

- `main` contains commits through `195dfe3` (`Implement North Star 4`).
- **All 4 North Star architectural foundations delivered**:
  - **North Star 1 (Dual Control Planes)**: Physical COM (`COM5`) and TCP ADB (`192.168.69.27:5555`) verified live and running concurrently with MAC-restricted firewall rules.
  - **North Star 2 (A1 Host Library & Recovery)**: `Mk20Protocol.psm1` (all 10 RPC methods, bi-directional CRC32 validation), `Test-Mk20Protocol.ps1` (19/19 unit tests passing), `mk20ctl snapshot` (capturing complete device inventory), and live hardware verification of backlight, volume, and `setFileCRC`.
  - **North Star 3 (Host-Owned Agent Orchestration)**: `SCHEMA.md` normalized JSON event model (Codex, Claude, Gemini) and `SyntheticPlayer.ps1` session player with real-time HUD event streaming.
  - **North Star 4 (Linux/QMK Ownership & Contract)**: `LINUX_QMK_CONTRACT.md` documenting UART `/dev/ttyS1` boundary, `Test-QmkProtocol.ps1` (14/14 unit tests passing) validating VIA framing, checksums, and `KeyboardInfo` 26-record binary matrix layout, and `Watch-Mk20Events.ps1` real-time serial listener.

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
   - **Live Execution Verified**: Tested live with Claude Code 2.1; verified real-time stream normalization, token cost reporting, and clean shutdown.
4. **Phase 4: MK20 640x656 LCD Screen Rendering & Form-Factor Engine**:
   - **Native Framebuffer HUD Engine ([`mk20-hud.c`](file:///e:/developments/projects/Snowball_Control/hardware/mk20/hud/mk20-hud.c))**: Directly mmaps `/dev/fb0` (640x656 @ 32bpp, 2560 bytes/row) on Allwinner T113. Redesigned to precisely match the MK20 hardware form factor:
     - **Top Display Window**: 428x142 px (`x: 106..534, y: 0..142`) for telemetry, model badge, rolling thought ticker, and rotary dial overlay. Hidden bezel corners (`x < 106` and `x > 534`) left blank.
     - **20 Dynamic Screen Keys**: 5 cols x 4 rows of 128x128 px individual LCD cells directly underneath each transparent mechanical keycap.
     - **Tactile-Visual Button Press Response**: Dual-input monitoring via `/dev/ttyS1` UART and UDP `KEY:` messages. When a key is pressed, the LCD cell underneath immediately illuminates with bright colored glow, inverted text, and 3px border.
   - **Cross-Compilation Pipeline ([`Makefile`](file:///e:/developments/projects/Snowball_Control/hardware/mk20/hud/Makefile))**: Uses vendor Linaro GCC 6.4.1 toolchain via WSL.
   - **Host HUD Controller ([`Send-Mk20HudEvent.ps1`](file:///e:/developments/projects/Snowball_Control/hardware/mk20/orchestration/Send-Mk20HudEvent.ps1))**: Added support for `-Action key` (with `-Pressed`) and `-Action dial`.
   - **Live Screen Capture Verified**: Validated by dumping physical framebuffer `/dev/fb0` directly from the hardware for both normal state and live keypress depression state.

## Remaining Work

1. **Phase 5: Production Gateway Service & Developer Tooling**:
   - Package standalone background service (`snowball-gateway`) with dual-plane failover and IDE plugins.
2. **Phase 6: Custom Firmware & Production Packaging**:
   - Slim Tina Linux OS image and safe MicroSD OTA rollback.

## Exact Next Action

Build Phase 5: Production Gateway Service packaging the background daemon (`snowball-gateway`) with dual-plane failover (`COM5` and TCP `5555`), heartbeat monitoring, and automatic reconnection.

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
- Latest implementation commit: `59cf07fea913ef8a7eb6bdc5d6e74563724b63cf` (`Initialize MK20 development tooling`)
- Handoff documentation: the commit containing this file; local `main` is ahead of `origin/main` until pushed.
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
