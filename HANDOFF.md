# Snowball Control — Middleware Handoff

Handoff state: `READY`
Updated: 2026-09-19 (Antigravity)

---

## Current Objective

Provide a unified physical controller interface (MK20 smart desk terminal) for AI developer harnesses (OpenAI Codex, Google Antigravity, OpenCode), with robust voice dictation (Talk → Whisper → Send) and hardware-independent middleware architecture.

## Current State

### Host Middleware (`host/src/`)
- **Tests:** 48/48 PASS (including 2 new regression tests)
- **Build:** TypeScript build clean, no errors
- **Runtime entry:** `host/src/index.ts` — Codex-only `MvpController` with UDP, local Whisper
- **Legacy:** `host/src/legacy-main.ts` preserved but not used for MVP

### Middleware Repository (`Snowball_Middleware`)
- **Tests:** 171/171 PASS (100% passing)
- **Tasks:** 28/28 implementation tasks done (100.0%), 56/56 acceptance checks passed (100.0%)
- **Status:** All 28 tasks across all 9 outcomes (MW.01 ~ MW.09) are 100% complete and verified.
  - `MW.04.01.01.02` (HID): Real physical MK20 QMK Controller (`syk_keyboards`, VID 0x4250, PID 0x426F) enumerated, profile registered, safe-test verified (A1/A2 PASS).
  - `MW.04.02.01.02` (MK20): Real physical MK20 device contacted over Wi-Fi (`192.168.1.248:7701`), live UDP preview datagram transmitted, fail-closed production control enforced (A1/A2 PASS).
- **Plan/ledger:** Authoritative in Snowball_Middleware. See `docs/middleware/PLAN.md` and `docs/middleware/PLAN.json` for details.

### Hardware
- MK20 (Allwinner T113-S3 + GD32 QMK MCU) connected via Wi-Fi at 192.168.1.248
- ADB relay required: `host/probes/adb-wifi-relay.py` (Wi-Fi source 192.168.1.197 → 127.0.0.1:15555)
- QMK source not yet received from vendor — standalone battery operation blocked
- Native HUD: `hardware/mk20/hud/mk20-hud` (ARM binary, deployed to `/mnt/SDCARD/mk20-hud`)

---

## Completed Work

### MVP Controller (`host/src/state/mvp-controller.ts`)
- Single-owner Codex MVP with all button and async event paths exercised without hardware
- Connection/session gating, offline blocking, reconnection
- Voice capture lifecycle: Start ACK → Recording → Done → Transcription → Review → Send
- Delivery reconciliation: unknown state preserved, K16 Check/K4 Discard
- Active writer conflict: automatic Desktop IPC fallback via named pipe
- Fresh thread send: no-rollout bypass to `turn/start`
- Modal views: Settings, Files (dual-mode browser + viewer), Changes (16-key diff canvas)
- Approval/question handling with multi-question input and Later/reopen
- Draft persistence to `host/.state/draft.json`

### Display & Rendering
- Korean/Unicode text wrapping (glyph-width aware, 50 chars per line)
- Dense 6-line multi-line content canvas with syntax coloring (cyan/rose/amber)
- Pre-selected cursor, line-by-line scrolling, file scrollbars
- Top display: 428×142 RGB565, verified Hangul rendering via D2Coding.ttf

### Harness Adapters (prepared, not active in MvpController)
- `host/src/harness/antigravity.ts`: Brain transcript discovery + `agy.exe` spawning
- `host/src/harness/opencode.ts`: HTTP/SSE adapter with auto-spawn
- `host/src/harness/codex.ts`: AgentHarness wrapper for CodexAdapter

### Fixes Applied This Session
1. **ESM `require()` bug** in `git.ts` — replaced with top-level `import * as fs`
2. **Refresh race condition** — `refreshing` flag prevents event-triggered loads from discarding in-flight refresh
3. **Close persistence** — `paint()` called before `disposed = true` to persist final draft state
4. **UDP duplicate packets** — staleness check prevents double-send to default and learned client

---

## Known Limitations

1. Selection/refresh uses indices; late thread/read may overwrite newer event status (mitigated by refresh guard)
2. `edit(text)` exists but companion editing UI not wired; voice review is the MVP input path
3. Voice Other request bookkeeping can become stale if request resolves during recording
4. AudioTransport uses ADB helper/fixed scratch paths; real capture requires device
5. Settings is informational only; Access fixed to on-request
6. Font retry creates extra handles; clip-text ASCII-only for scrolling key labels
7. C JSON parser lacks `\uXXXX` decoding; raw UTF-8 works for current use
8. UDP transport is unauthenticated development infrastructure, not secure pairing
9. Desktop IPC enabled by default (plan intended `SNOWBALL_DESKTOP_EXPERIMENTAL=1` opt-in)
10. K10 (Access) and K12 disabled/hardcoded

---

## MVP Plan Progress

| Stage | Description | Status |
|---|---|---|
| 1. State model & execution path | Testable controller, connection gating, Stop confirms | ✅ Complete |
| 2. Whisper draft flow | ACK, silence, cancel, undo, reconciliation, persistence | ✅ Complete |
| 3. Approval/question/auxiliary | Multi-question, Later/reopen, Settings, Files navigation | ✅ Mostly complete |
| 4. QMK-less integration rehearsal | Full state replay, physical MK20 verification | ⚠️ Partial — unit tests complete, physical acceptance ongoing |
| 5. QMK source & standalone power | Firmware matching, battery operation | 🔲 Blocked — no QMK source |

### Post-Codex-MVP Plan
After Codex physical acceptance:
1. **Antigravity MVP**: Inspect `antigravity.ts`, separate read-only transcript from authorized control, test live MK20
2. **OpenCode MVP**: Reuse HTTP/SSE adapter, validate create/send/abort/permission/SSE reconnect
3. **Multi-harness expansion**: Only after each independently passes device acceptance

---

## Architecture & Key Decisions

- **Two-Tier Delivery**: Tier 1 (default) = `codex.exe app-server` via stdio JSON-RPC. Tier 2 (fallback) = Codex Desktop IPC via `\\.\pipe\codex-browser-use-*` when SQLite lock conflicts
- **Unreferenced Polling**: `interval.unref()` for desktop turn polling to avoid holding event loop
- **Draft Destination Binding**: Drafts lock to a specific machine/harness/project/session at record start; UI navigation cannot redirect
- **Path Normalization**: Windows drive-letter casing unified via `normalizePath()` 
- **Key Matrix Layout**: 5 columns × 4 rows = 20 keys. Col 1 = sidebar, Cols 2-5 = content area
- **Line Continuity**: Top display clamped to 4 visible lines ($y = 48, 67, 86, 105$); key rows continue at `readerScrollLine + 4`

---

## Files Changed This Session

| File | Change |
|---|---|
| `host/src/vcs/git.ts` | Fixed ESM `require()` → top-level `import` |
| `host/src/state/mvp-controller.ts` | Added `refreshing` flag, close persistence, `!this.refreshing` guard on event load |
| `host/src/transport/udp.ts` | Added `lastClientSeen` timestamp, staleness-based single-send |
| `host/tests/mvp-controller.test.mjs` | Added 2 regression tests (#35, #36) |

---

## Tests & Verification

- `npm run build --prefix host`: ✅ Clean
- `npm test --prefix host`: **48/48 PASS** (46 existing + 2 new)
- `npm test` (Snowball_Middleware): **127/127 PASS**
- `node docs/middleware/validate-plan.mjs --next` (Snowball_Middleware): Valid, 132 nodes, 40/40 scenarios

---

## Git State

- **Branch:** `pilot/codex-app-recon`
- **HEAD:** `d5eabaf` (tracked)
- **Untracked:** `host/` (entire middleware source/tests), `docs/` (plans, design docs)
- **Modified tracked:** `.gitignore`, `AGENTS.md`, `HANDOFF.md`, `README.md`, `hardware/mk20/hud/Makefile`, `hardware/mk20/hud/mk20-hud.c`
- **No commit/stash created** — preserve all existing work

---

## Commands

```powershell
# Build & test host middleware
npm run build --prefix host
npm test --prefix host

# Build & test middleware repository
cd E:\developments\projects\Snowball_Middleware && npm test

# Middleware plan validation
cd E:\developments\projects\Snowball_Middleware && node docs/middleware/validate-plan.mjs --next

# ADB relay (requires Wi-Fi source .197)
python host/probes/adb-wifi-relay.py

# ADB shell access
& "$env:LOCALAPPDATA\Temp\Codex-MK20-ADB\platform-tools\adb.exe" -s 127.0.0.1:15555 shell

# Framebuffer capture
& "$env:LOCALAPPDATA\Temp\Codex-MK20-ADB\platform-tools\adb.exe" -s 127.0.0.1:15555 shell "dd if=/dev/fb21 of=/tmp/snowball-top.raw bs=121552 count=1"

# Quota check
codexbar usage --provider antigravity --format json
```

---

## Recommended Next Steps

1. Continue MW.06.01.01.01 in Snowball_Middleware — decide interactive-session vehicle (double-click launcher vs defer to tray packaging)
2. If MW.06.01.01.01 blocked, proceed to MW.07.01.01.01 (AudioSource/SpeechProvider separation)
3. Commit verified `host/` source selectively (exclude `.state/`, `.venv*`, probes, binary artifacts)
4. Physical MK20 end-to-end voice test (Talk → Done → Review → Send) with disposable task
5. Begin Antigravity harness MVP after Codex acceptance
