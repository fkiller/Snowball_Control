# Native Codex dictation — Windows / MK20 PoC

Codex owns recognition and inserts text into this separate editable draft. The PC microphone route was user-verified on September 7. MK20 live PCM transport works; MK20-to-Codex transcription still needs virtual-microphone acceptance. See [verification](../../../docs/MK20_NATIVE_AUDIO_VERIFICATION.md).

## Build and run

Requires Windows, .NET 9 Desktop Runtime, signed-in Codex desktop with global dictation. Build needs .NET 9 SDK and pinned NAudio 2.2.1. From repository root:

```powershell
& host/poc/native-dictation/Build-Poc.ps1 -OutputDirectory host/poc/native-dictation/artifacts/validated
& host/poc/native-dictation/artifacts/validated/Snowball.Dictation.Poc.exe
```

Close the corresponding old PoC before rebuilding its output directory. Build creates a local speech fixture; this is test synthesis, not a transcription fallback.

Codex Settings → Voice → toggle dictation: **Ctrl+Alt+Shift+F9**. For PC mode select your physical microphone and use 받아쓰기 시작 / 중지 · 결과 받기. Keep focus in the draft and inspect the native overlay. For MK20 mode install official [VB-CABLE](https://vb-audio.com/Cable/), select **CABLE Output** in Codex, then test 시험 음성 전사 before MK20 녹음 or MK20 실시간 시작. The app plays exclusively to CABLE Input; it never falls back to speakers. Installation requires normal user administrator approval. No Windows audio defaults are changed.

MK20 laboratory prerequisites: ADB at `%LOCALAPPDATA%\Temp\Codex-MK20-ADB\platform-tools\adb.exe`, connected device `192.168.69.27:5555`, helper `/mnt/SDCARD/snowball-pcm-poc`. Build helper with `wsl make -C /mnt/e/developments/projects/Snowball_Control/hardware/mk20/hud pcm-stream`; deploy that helper explicitly without replacing the HUD. Capture uses MIC3 of three-channel PCM16/16kHz and stops within 60 seconds. Live transport uses a nonce and loopback ADB forward; it is not production secure pairing.

## Diagnostics

```powershell
dotnet host/poc/native-dictation/artifacts/validated/Snowball.Dictation.Poc.dll --devices devices.json
dotnet host/poc/native-dictation/artifacts/validated/Snowball.Dictation.Poc.dll --probe-cable cable.json
dotnet host/poc/native-dictation/artifacts/validated/Snowball.Dictation.Poc.dll --probe-live live.json
dotnet host/poc/native-dictation/artifacts/validated/Snowball.Dictation.Poc.dll --probe-mk20 capture.json
```

The capture probes record briefly; cable probe uses a synthetic tone only. Check JSON and exit code. A passing transport or loopback report does not establish transcription. Reports contain metrics, not recorded audio. Live audio stays in memory; bounded mode uses temporary owned files. No credentials are read and no agent Send operation exists.

## Recovery and acceptance

Native shortcut state is requested, not acknowledged. Do not switch apps during dictation. If uncertain, stop in the native overlay before 다시 준비. Reset preserves the draft. Inspect and edit the result before explicit copy. TextChanged detects draft changes, not authenticated transcript events. Do not retry an uncertain toggle blindly.

User-verified September 7 PC transcript: `테스트 시작 하나 둘 셋 넷. English test. One two three four. 하나 둘 셋 넷 한글 테스트.` One observed Stop-to-draft-change interval was 1.7 seconds, not an accuracy/latency benchmark. MK20 spoken quality, fixture-to-Codex, expanded UI, repeated runs and error recovery remain separate acceptance gates. September 8 build: zero warnings/errors; virtual cable remains absent.
