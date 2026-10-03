# Historical handoff

현재 실행 경로, 검증 절차와 미확인 항목은 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)에서 관리합니다. 과거 handoff의 테스트 숫자나 완료 선언은 현재 지원 보장이 아닙니다.

---

## Current Objective & Status: TTS Pipeline & Main Integration

**Handoff state:** WORKING / READY FOR TESTING

### Key TTS Architectural Implementation
1. **Model selection**: Kokoro-82M ONNX single model stored in `%APPDATA%\Snowball\models\tts`.
2. **Execution Provider**: Full NVIDIA CUDA 12 Execution Provider acceleration enabled (`onnxruntime-gpu 1.19.2`).
3. **No OS Fallback**: Windows SAPI and macOS say fallbacks have been completely eliminated. Kokoro-82M is the sole TTS engine.
4. **Auto-TTS Toggle UX**:
   - Speak button (Key 12) acts as a stateful toggle.
   - When toggled **ON**: Immediately speaks the concluding response of the current session's latest turn.
   - While remaining ON: Every subsequent completed turn automatically speaks the concluding LLM response.
   - When toggled **OFF**: Immediately aborts active speech and disables auto-speaking.
   - Interruption safety: When user presses Talk (Key 20) or Stop (Key 4), active speech immediately aborts to prevent microphone pickup.
