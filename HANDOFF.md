# Cross-Agent Handoff

## Status

**Handoff state:** READY (Mandatory Quota Guard Triggered: Google Antigravity -> OpenAI Codex)

Google Antigravity reached the mandatory quota guard threshold (>90% used, ~6% remaining on 5-hour limit). All changes have been cleanly validated, tested (54/54 tests passing), committed, and documented below. OpenAI Codex can immediately continue without needing conversational history.

---

## Current Objective

Implement an end-to-end Text-to-Speech (TTS) pipeline matching the existing Speech-to-Text (STT) architecture:
1. Model selection (Kokoro-82M ONNX ~80MB single model across all platforms).
2. Hardware Execution Provider fallback hierarchy (CoreML on macOS; CUDA -> DirectML -> CPU on Windows; CUDA -> CPU on Linux; OS native SAPI/macOS `say` emergency fallback).
3. Host Middleware implementation (resident stdio JSON-RPC daemon, audio player, context state, mvp controller).
4. Web UI Simulator (`docs/simulator/index.html`) & Device Plugin (`plugins/device-mk20`).
5. **Auto-TTS Toggle UX Behavior**:
   - Speak button acts as a stateful toggle.
   - When toggled **ON**: Immediately speaks the latest LLM turn response in the current session.
   - While remaining ON: Every subsequent completed turn automatically speaks the new LLM turn response.
   - When toggled **OFF**: Immediately aborts active speech and disables auto-speaking.
   - Interruption safety: When user presses Talk (Key 20) or Stop (Key 4), active speech immediately aborts to prevent mic pickup while preserving auto-TTS toggle ON state for the upcoming turn.

---

## Recent Issue Analysis & Resolutions (User Feedback 2026-10-01)

### 1. "무슨 모델로 연결된거야? TTS 퀄리티 완전 구린데" (Terrible robotic quality)
- **Root Cause**: The user did not hear Kokoro-82M. Kokoro-82M worker crashed on Windows during stdio JSON-RPC with `'utf-8' codec can't encode character '\udc81' in position 13: surrogates not allowed` due to Python's default Windows ACP encoding on `sys.stdin` and unhandled surrogate characters in the thread text.
- Because Kokoro crashed, `LocalKokoroProvider` automatically fell back to **Windows PowerShell `System.Speech` (SAPI5 default voice: Microsoft David Desktop)**. Microsoft David is an ancient Windows XP/7-era 2000s robotic voice.
- **Resolution**:
  - `host/src/audio/tts_worker.py`: Added `sys.stdin.reconfigure(encoding="utf-8", errors="replace")`.
  - Added `sanitize_tts_text()` to strip dangling surrogates, normalize non-breaking hyphens (`\u2011` -> `-`), non-breaking spaces, and typographic curly quotes.
  - `host/src/audio/local-kokoro.ts`: Spawn Python with `-X utf8` and `PYTHONIOENCODING=utf-8`, `PYTHONUTF8=1`.

### 2. "한글 세션인데 영문만 발음해." (Only pronounces English words)
- **Root Cause**:
  1. `LocalKokoroProvider.synthesize()` was defaulting to `lang: "en-us"`. In Kokoro's tokenizer, English mode filters out Hangul phonemes, producing phonemes only for English terms (`USB`, `Wi-Fi`).
  2. When Kokoro crashed and fell back to Windows SAPI (Microsoft David Desktop), PowerShell `-Command` encoding mangled Korean characters and Microsoft David has no Korean phoneme engine, reading out only English words and ignoring Korean.
- **Resolution**:
  - `tts_worker.py`: Added `has_korean(text)` which detects Hangul syllables/jamo (`0xAC00..0xD7A3`, `0x1100..0x11FF`, `0x3130..0x318F`) and automatically sets `lang = "ko"`.
  - `local-kokoro.ts`: Automatically sets `lang = "ko"` when text contains Korean.
  - Verified live: Korean synthesis with Kokoro-82M produces natural, high-quality 24kHz audio (`engine: 'kokoro-onnx'`).

### 3. "플레이는 MK20에서 되야지. 지금은 PC에서 들림." (Audio plays on PC, should play on MK20)
- **Status & Architecture Context**:
  - Currently, `AudioPlayer` (`host/src/audio/player.ts`) plays audio through the host PC's audio subsystem (PowerShell `SoundPlayer` on Windows, `afplay` on macOS, `aplay` on Linux).
  - The MK20 hardware features an Allwinner T113 Sunxi Audio Codec with an ALSA sink (`hw:0,0`).
  - However, the current network link between Host and MK20 (UDP 7701) only handles framebuffer display syncing and key/knob input packets. An audio streaming protocol (e.g. UDP PCM streaming daemon `pcm-stream` or ALSA network sink) is not yet deployed to the MK20 HUD firmware.
  - The user acknowledged this can be modified later.

---

## Current State

- Branch: `implement_tts_pipeline`
- Latest commit: `b70a646 fix(tts): add UTF-8 stream sanitization, Korean auto-detection, and turn response fallback`
- All 54 host unit/integration tests passing cleanly (`54/54 PASS`).
- All 13 MK20 device plugin tests passing cleanly (`13/13 PASS`).
- Hardware assessment script (`scripts/assess_tts_backend.py`) operational and verified against live hardware (`AMD Threadripper 24C/48T + NVIDIA CUDA Execution Provider` detected, simulation fallback verified).
- Runtime check script (`scripts/ensure_tts_runtime.py`) operational with `uv` installer support (`"ok": true`).
- Live Model Verified: `kokoro-v1.0.onnx` (80MB) and `voices-v1.0.bin` (26.9MB) downloaded to `%APPDATA%\Snowball\models\tts` and verified.
- Live Korean Synthesis Verified: `engine: 'kokoro-onnx'`, 24kHz WAV generated in 4.8s.
- Host Middleware daemon is currently running in the background and connected to MK20 via UDP 7701.

---

## Exact Next Action for OpenAI Codex

1. Verify that the running host middleware produces high quality Korean Kokoro audio when the user presses **Speak (Key 12)** on MK20.
2. If the user requests MK20 on-device playback:
   - Check `hardware/mk20/contract/LINUX_QMK_CONTRACT.md` (ALSA `hw:0,0`).
   - Check `hardware/mk20/hud/Makefile` (`pcm-stream` target).
   - Implement audio streaming from Host to MK20 ALSA sink over UDP or network socket.
3. Test suite verification commands:
   ```powershell
   npm test --prefix host
   npm test --prefix plugins/device-mk20
   ```

---

## Files Changed

| File | Purpose | State |
|---|---|---|
| `host/src/audio/local-kokoro.ts` | Added `-X utf8`, UTF-8 env, text sanitization, Korean auto-detection | Committed (`b70a646`) |
| `host/src/audio/tts_worker.py` | Added stdin UTF-8 reconfigure, text sanitization, Korean auto-detection | Committed (`b70a646`) |
| `host/src/state/mvp-controller.ts` | Turn fallback to find latest completed agent turn when last turn failed | Committed (`b70a646`) |
| `host/tests/tts.test.mjs` | Added unit test for turn fallback when latest turn is failed | Committed (`b70a646`) |
| `HANDOFF.md` | Complete cross-agent handoff context and root-cause analysis | Updated |

---

## Constraints

- **Single Unified Model**: Kokoro-82M ONNX (~80MB) across all platforms.
- **Toggle Contract**: Speak button (Key 12) is stateful toggle.
- **Interruption Guarantee**: Talk (Key 20) and Stop (Key 4) immediately cancel active TTS playback.
- **Zero Residue**: Model stored in `%APPDATA%\Snowball\models\tts`.
