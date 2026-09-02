# Cross-Agent Handoff

## Status

**Handoff state:** READY

The repository is ready for another agent to continue without access to the prior conversation. No implementation work was started during this handoff session.

## Current Objective

Develop Snowball Control as a standalone MK20 control-panel project for AI-assisted coding workflows. The MK20 remains a thin network client while development machines own coding tools, source work, and provider sessions. Preserve reliable product USB behavior while adding recoverable development access and, later, normalized host-side agent orchestration.

## Current Task

There is no active feature request recorded beyond preparing this cross-agent handoff. The last implementation milestone created the initial MK20 development tooling. The next roadmap task is to reboot-test and validate the documented product-COM plus TCP-ADB topology on real hardware before expanding the tooling.

## Current State

- `main` contains one implementation commit, `59cf07f` (`Initialize MK20 development tooling`).
- The tracked project provides MK20 discovery, health reporting, device information, file upload, network shell, and guarded factory restore through `hardware/mk20/dev-tools/mk20ctl.ps1`.
- `hardware/mk20/dev-tools/lunch.sh` is the device boot hook for Wi-Fi, TCP-only ADB, and a source-MAC firewall rule. It intentionally does not change the USB gadget.
- The device topology and recovery procedure are documented, but were not exercised against live hardware in this handoff session.
- No automated test suite or project build system is present. Fast syntax validation passes for both scripts.
- The large Tina T113 BSP exists locally at `hardware/mk20/Keyboard_T113_SourceCode_20260427_MK20/` and is intentionally ignored because it contains a large vendor tree, nested repositories, and generated artifacts.

## Completed Work

- Commit `59cf07f` initialized the standalone repository and added the project overview, MK20 architecture notes, development/recovery docs, configuration template, device boot hook, and Windows helper.
- `mk20ctl.ps1` implements `doctor`, `info`, `put`, `shell`, and `restore`.
- This handoff session inspected the working tree, staged and unstaged diffs, diff stat, recent history, branches, remotes, stashes, tracked files, documentation, and both scripts.
- CodexBar was installed at the user's request using Winget package `Finesssee.Win-CodexBar` version `0.54.0`. A user-level compatibility command at `C:\Users\wondo\.local\bin\codexbar.cmd` maps `codexbar --format json` to the Windows CLI and Codex provider.

## Remaining Work

1. Reboot-test the current COM plus TCP-ADB topology on the physical MK20 and confirm that the product application, product COM, Wi-Fi, TCP ADB, and firewall coexist across reboot.
2. Run all five `mk20ctl` commands against hardware, beginning with read-only `doctor` and `info`; use `restore` without `-Force` unless factory rollback is explicitly intended.
3. Add host-side tests for A1 frame construction/parsing, CRC handling, transport selection, upload chunking, error paths, and restore preflight behavior.
4. Close COM upload integrity gaps: validate response CRCs, verify final remote checksum/length, and define truncation behavior when replacing a longer destination with a shorter file.
5. Remove the unnecessary unconditional ADB dependency for COM-only operations if hardware validation confirms that change is appropriate.
6. Continue the milestones in `hardware/mk20/dev-tools/NORTH_STARS.md`; do not begin UI or multi-agent features before the development/recovery workflow is repeatable.

## Exact Next Action

After reading `AGENTS.md` and this file and verifying Git state, connect the development PC and MK20 to the documented environment, reboot the MK20, then run:

```powershell
.\hardware\mk20\dev-tools\mk20ctl.ps1 doctor
```

Capture whether COM discovery, TCP port 5555, ADB state, `KeyboardDevice`, `adbd`, and the port-5555 firewall rules all survive reboot. Do not modify code until the observed result is compared with `hardware/mk20/dev-tools/README.md`.

## Architecture and Important Decisions

- This repository is intentionally separate from Snowball Gateway. Do not vendor or modify Gateway code here.
- Keep two independent control planes: product traffic on USB CDC and development traffic on restricted TCP. Neither plane may reset or silently take ownership of the other.
- Tina Linux owns display, networking, and applications; GD32/QMK owns low-latency key/HID behavior. Their serial contract must eventually be versioned and independently testable.
- Provider sessions belong on the Windows/Snowball host behind normalized adapters. The T113 should render state and emit user actions rather than host coding agents itself.
- Device mutations should have preflight checks, checksums, logs, and tested inverse operations. The microSD boot hook is the recovery boundary; routine development should not require system-partition replacement.
- TCP ADB is unauthenticated on this Tina image. The current boundary is an iptables source-MAC allow rule followed by a drop rule on port 5555. Site credentials and the allowed MAC live only in ignored `dev-access.conf`.
- `mk20ctl put` prefers ADB in `Auto` mode and uses MD5 verification for ADB uploads. COM uploads use 96-byte Base64 `saveToFile` chunks with per-chunk acknowledgements but no completed-file checksum yet.
- `restore` is a dry run by default. `-Force` restores the factory ADB init script, removes the SD boot hook, selects serial USB, syncs, and reboots; treat it as destructive.

## Files Changed

| File | Purpose | State |
|---|---|---|
| `AGENTS.md` | Cross-agent workflow, quota guard, and handoff rules | Added as repository documentation in the handoff commit |
| `HANDOFF.md` | Complete continuation context and READY state | Added/updated in the handoff commit |

No application, firmware, script, protocol, schema, or public-behavior file was changed during handoff preparation.

## Tests and Verification

### Passed

- PowerShell parser check of `hardware/mk20/dev-tools/mk20ctl.ps1`: no syntax errors.
- `bash -n hardware/mk20/dev-tools/lunch.sh`: passed.
- `codexbar --format json`: passed after installation and returned Codex quota data.
- Before writing this file: no tracked unstaged diff, no staged diff, no stash entries, and `main` matched `origin/main` at `59cf07f`.

### Failed

- The first `codexbar --format json` failed because CodexBar was not installed. Resolved by installing Win-CodexBar 0.54.0 and adding the user-level compatibility command.
- Direct `codexbar-cli.exe --format json` failed because the Windows port requires the `usage` subcommand. The compatibility command supplies it.

### Not Run

- No live `doctor`, `info`, `put`, `shell`, or `restore` operation was run against the MK20.
- No reboot, USB enumeration, TCP, firewall, file-transfer, recovery, interrupted-transfer, or power-loss test was run.
- No Tina BSP build was attempted.
- No automated suite exists in the tracked repository; a filename search found no test/spec files outside the ignored BSP.

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
