# Cross-Agent Handoff

## Status

**Handoff state:** READY

The repository is completely prepared for either coding agent (Google Antigravity / OpenAI Codex) to continue seamlessly. The full Text-to-Speech (TTS) pipeline matching the STT architecture has been fully implemented, integrated across Host middleware, Web UI simulator, and device plugins, and verified with 100% test suite pass rate (53/53 tests in host, 13/13 tests in plugins).

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

## Current Task

Completed full TTS pipeline implementation, verified all unit and integration test suites, updated Web UI simulator and device plugin, and verified cross-platform hardware assessment and runtime management scripts.

---

## Current State

- Branch: `implement_tts_pipeline`
- All 53 host unit/integration tests passing cleanly (`53/53 PASS`).
- All 13 MK20 device plugin tests passing cleanly (`13/13 PASS`).
- Hardware assessment script (`scripts/assess_tts_backend.py`) operational and verified against live hardware (`AMD Threadripper 24C/48T + NVIDIA CUDA Execution Provider` detected, simulation fallback verified).
- Runtime check script (`scripts/ensure_tts_runtime.py`) operational with `uv` installer support (`"ok": true`).
- Live Model Verified: `kokoro-v1.0.onnx` (80MB) and `voices-v1.0.bin` (26.9MB) downloaded to `%APPDATA%\Snowball\models\tts` and verified.
- Live Synthesis Verified: Tested end-to-end synthesis via both `tts_worker.py` CLI and `LocalKokoroProvider` from Node with 24kHz audio generation verified.
- Speak button (Key 12 in physical MK20 / Host; Key 17 in Web Simulator) implements identical toggle behavior with auto-speak on turn completion.

---

## Completed Work

1. **Config & Hardware Assessment**:
   - `config/tts.json`: Standardized Kokoro-82M ONNX configuration matching `config/stt.json`.
   - `scripts/assess_tts_backend.py`: Hardware & EP probe detecting Apple Silicon CoreML, Windows CUDA/DirectML, Linux CUDA, CPU threads, and fallback simulation mode (`--simulate`).
   - `scripts/ensure_tts_runtime.py`: Python runtime installer and model manager (`--check`, `--install`, `--download-models`).
2. **Host Middleware Audio Layer**:
   - `host/src/audio/tts-provider.ts`: Interface definitions (`NativeTtsProvider`, `TtsSynthesizeOptions`, `TtsSynthesizeResult`).
   - `host/src/audio/player.ts`: Cross-platform low-latency audio player (`AudioPlayer`) using child process streaming (PowerShell `SoundPlayer`, macOS `afplay`, Linux `aplay`) with instant PID kill cancellation.
   - `host/src/audio/tts_worker.py`: Resident stdio JSON-RPC daemon with zero-delay IPC pipe, Kokoro-82M ONNX synthesis, and audio streaming.
   - `host/src/audio/local-kokoro.ts`: Host daemon wrapper for Kokoro-82M ONNX worker with automatic OS native fallback (PowerShell `System.Speech` / macOS `say`) for zero-crash safety.
3. **State Management & Controller**:
   - `host/src/state/context.ts`: Added `public autoTts = false;`, enabled Key 12 in `getDeviceState()` across session and question viewmodes (`Top: AUTO TTS`, `Main: Speak`, `Sub: Auto ON / Speaking / Off`), enabled Key 12 styling.
   - `host/src/state/mvp-controller.ts`:
     - Injected `readonly tts: NativeTtsProvider = new LocalKokoroProvider()`.
     - Added `toggleAutoTts()`, `speakText()`, `stopSpeaking()`.
     - Wired Key 12 to `toggleAutoTts()`.
     - Wired `turn/completed` event: automatically speaks newly arrived agent response when `autoTts === true`.
     - Wired `startVoice()` (Key 20) and `stopTask()` (Key 4) to call `stopSpeaking()` immediately so microphone does not pick up speaker audio.
     - Wired `close()` to terminate TTS worker child process.
4. **Web UI Simulator**:
   - `docs/simulator/index.html`: Wired Speak button (Key 17) to `toggleAutoTts`, `speakTurn`, `stopSpeech`, and connected to `send()` turn arrival and `record()` / `stop()` audio cancellation.
5. **Testing & Verification**:
   - `host/tests/tts.test.mjs`: Complete unit test suite verifying toggle ON/OFF, turn auto-speech sequence, and talk interruption.
   - Updated `host/tests/mvp-controller.test.mjs` and `host/tests/mvp-state.test.mjs` to maintain 100% test suite pass rate.
   - Fixed venv python resolution in `host/src/audio/local-whisper.ts` and `host/tests/whisper-worker.test.mjs`.

---

## Remaining Work

1. **Physical MK20 Hardware Deployment**:
   - Validate live UDP datagrams on physical MK20 device when Key 12 is pressed.
2. **Merge Branch**:
   - Merge `implement_tts_pipeline` into `main` after user review.

---

## Exact Next Action

Run the verification commands on the worktree to ensure everything builds and passes:
```powershell
npm test --prefix host
npm test --prefix plugins/device-mk20
python scripts/assess_tts_backend.py
```
If physical MK20 is connected via network/USB, launch host daemon:
```powershell
npm start --prefix host
```

---

## Architecture and Important Decisions

- **Single Unified Model vs Multi-Model**: Kokoro-82M is small (~80MB), high quality, and runs anywhere ONNX Runtime runs. We avoided multiple model downloads (Piper/Edge-TTS) and instead adopted Execution Provider hardware acceleration:
  - macOS: CoreML (`CoreMLExecutionProvider`) -> CPU
  - Windows: CUDA (`CUDAExecutionProvider`) -> DirectML (`DmlExecutionProvider`) -> CPU
  - Linux: CUDA -> CPU
  - Emergency Native Fallback: Windows PowerShell `System.Speech` / macOS `say` for zero-crash safety.
- **Key 12 Hardware Alignment**: In physical MK20 hardware, Row 3 is `[K20 (Talk), K16 (Send), K12 (Speak), K8 (Later/Undo), K4 (Stop)]`. Key 12 was previously disabled as "outside MVP". Enabling Key 12 aligns physical hardware with Speak functionality.
- **Microphone Interruption Contract**: When voice recording begins (`startVoice()`), active TTS playback is aborted immediately (`this.context.isSpeaking = false; void this.tts.stop();`), while preserving `this.context.autoTts = true` so the resulting LLM response will be spoken automatically upon turn completion.

---

## Files Changed

| File | Purpose | State |
|---|---|---|
| `config/tts.json` | TTS configuration matching `config/stt.json` | Created |
| `scripts/assess_tts_backend.py` | Hardware EP assessment & diagnostic tool | Created |
| `scripts/ensure_tts_runtime.py` | Python runtime dependency check & model downloader | Created |
| `host/src/audio/tts-provider.ts` | TTS provider type definitions | Created |
| `host/src/audio/player.ts` | Cross-platform audio player with instant PID kill | Created |
| `host/src/audio/tts_worker.py` | Resident Kokoro-82M ONNX stdio JSON-RPC daemon | Created |
| `host/src/audio/local-kokoro.ts` | Host Kokoro provider with OS SAPI/say emergency fallback | Created |
| `host/src/audio/local-whisper.ts` | Enhanced venv python resolution | Modified |
| `host/src/state/context.ts` | Added `autoTts`, Key 12 display visual | Modified |
| `host/src/state/mvp-controller.ts` | Wired Key 12 toggle, auto-speech on turn, talk interruption | Modified |
| `docs/simulator/index.html` | Updated Key 17 Speak toggle and auto-TTS | Modified |
| `host/tests/tts.test.mjs` | Unit test suite for TTS toggle and auto-speech | Created |
| `host/tests/mvp-state.test.mjs` | Updated Key 12 assertion for Speak enabled | Modified |
| `host/tests/whisper-worker.test.mjs` | Enhanced venv resolution for whisper test | Modified |
| `host/package.json` | Added `tts_worker.py` copy build step | Modified |
| `HANDOFF.md` | Cross-agent continuation context and source of truth | Updated |

---

## Tests and Verification

- `npm run build --prefix host`: Clean build (code 0).
- `node --test host/tests/tts.test.mjs`: 4/4 PASS.
- `node --test host/tests/mvp-controller.test.mjs`: 16/16 PASS.
- `node --test host/tests/mvp-state.test.mjs`: 8/8 PASS.
- `node --test host/tests/integration.test.mjs`: 1/1 PASS.
- `node --test host/tests/whisper-worker.test.mjs`: 1/1 PASS.
- `npm test --prefix host`: 53/53 PASS.
- `npm test --prefix plugins/device-mk20`: 13/13 PASS.
- `python scripts/assess_tts_backend.py`: Verified live output (CUDA detected on Windows AMD64 + Threadripper 24C/48T).

---

## Constraints

- Zero Residue: All models downloaded to `<userDataDir>/models/tts/` for clean uninstallation.
- Toggle Contract: Speak button is strictly a stateful toggle.
- Non-blocking Audio: Audio player and TTS worker must never block event loop or fail to terminate cleanly on abort.
