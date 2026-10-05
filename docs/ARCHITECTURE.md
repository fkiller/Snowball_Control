<p align="right">
  <strong>English</strong> | <a href="ARCHITECTURE.ko.md">한국어</a>
</p>

# Snowball Control & Middleware — Preview Architecture

This document serves as the **Single Source of Truth (SSOT)** governing the design, boundaries, and interfaces across the physical device and host middleware. The initial scope encompasses **OpenAI Codex, Google Antigravity (AGY), OpenCode, MK20 hardware, and local Web Supervisor**. This public Preview is intended strictly for personal workstations and trusted local networks; it does not provide security guarantees for multi-tenant or untrusted Internet environments.

---

## 1. Repositories and Concrete Execution Paths

| Subsystem | Location | Current Role & Responsibility |
| --- | --- | --- |
| **Keyboard MCU** | `Snowball_Control/hardware/mk20/qmk/` | QMK mechanical key matrix scan, USB HID, and UART escalation to Tina Linux |
| **Device Linux OS** | Vendor Tina T113-based Snowball SD Release | Framebuffers, ALSA, Wi-Fi, and ADB. Tina BSP SDK is not tracked in this repository |
| **Device HUD** | `Snowball_Control/hardware/mk20/hud/` | Native C low-latency HUD daemon running on `/mnt/SDCARD/mk20-hud` (UDP 7701) |
| **Device Audio Daemon** | `Snowball_Control/hardware/mk20/hud/` | Native C audio streaming daemon running on `/mnt/SDCARD/mk20-audio` (TCP 7702 SNAU protocol) |
| **Device Boot Tools** | `Snowball_Control/hardware/mk20/dev-tools/` | MicroSD bootstrap, Wi-Fi configuration, TCP ADB launcher, and HUD/audio startup |
| **MK20 Device Plugin** | `Snowball_Control/plugins/device-mk20/` | Isolated lab Preview transport and framebuffer encoding module |
| **Middleware & Web API** | [Snowball_Middleware](https://github.com/fkiller/Snowball_Middleware) `packages/core`, `packages/api`, `apps/supervisor` | Session/command journal, workspace grants, local REST/SSE API, and Web UI |
| **Integrated MK20 Runtime** | `Snowball_Middleware/scripts/start-all.mjs` | Unified entrypoint binding physical MK20, local STT, live harness discovery, and Web UI |
| **Native Harness Dispatch** | `Snowball_Middleware/scripts/harness-dispatch.mjs` | Native process spawns: Codex app-server, AGY stream-json, OpenCode run. Plugin sandboxing and host execution privileges are separate boundaries |
| **Installation & Catalog Scan** | `harness-runtime.mjs`, `harness-catalog-scanner.mjs` | Discovers PATH/explicit executables and local CLI caches. Emits empty catalog on discovery failure |
| **Session & Project Scan** | `harness-session-scanner.mjs`, `harness-db-scanner.py`, `harness-project-scanner.mjs` | Non-mutating observation of native indexes/DBs. Scanners never write to harness storage |
| **Desktop System Tray** | `Snowball_Middleware/apps/desktop/` | Electron system tray management shell. Does not automatically guarantee parity with integrated MK20 daemon |
| **Legacy Host** | `Snowball_Control/host/`, Middleware `reference/legacy-host/` | Reference implementation. Control's `npm start` is a legacy Codex-only runtime, not the full middleware launcher |
| **Legacy PowerShell Tools** | `hardware/mk20/orchestration/` | Diagnostic archive. Not used for production physical approvals or secure pairing |

> ⚠️ **Notice on Vendor Qt App**: The factory Qt application (`/data/KeyboardDevice`) is not the current product HUD. Descriptions of factory Qt screens in legacy manuals serve solely as vendor reference and must not be interpreted as the specification for the native C HUD.

```mermaid
flowchart LR
  QMK[QMK MCU] -->|UART Keys & Knobs| HUD[Tina C HUD]
  HUD <-->|LAN UDP Preview 7701| Daemon[Middleware start-all]
  Mic[MK20 ALSA MIC3] -->|TCP 7702 SNAU Stream| STT[Local Whisper]
  STT --> Daemon
  Daemon -->|TCP 7702 SNAU Stream| Spk[MK20 Speaker aplay]
  Daemon -->|OS Audio| HostSpk[Host PC/Mac Speaker]
  UI[Loopback Supervisor] <-->|HTTP & SSE| API[LocalApi & CommandJournal]
  API --> Daemon
  Sources[CLI, Cache, Session DBs] -->|Observation| Daemon
  Daemon -->|Native Processes| Harness[Codex, AGY, OpenCode]
```

The MK20 terminal handles screen rendering, physical keys, knobs, and dual-way voice audio streaming (TCP 7702 `SNAU` binary protocol), while the host PC retains absolute ownership of workspace filesystem grants and native agent process execution. Audio capture and playback stream directly over the local network with zero disk wear on MK20 flash storage. USB HID/CDC and LAN transport operate on decoupled paths; the Preview UDP protocol does not provide automated wired failover or cryptographic device pairing.

---

## 2. Preview Security Boundaries

- **Web Supervisor Loopback Binding**: Binds strictly to **`127.0.0.1:8765`** without authentication/PIN for personal loopback ergonomics. It must never be exposed to public network interfaces or unauthenticated reverse proxies.
- **LAN Communication**: MK20 UDP status packets, key events, and development TCP ADB operate over the local network. Preview UDP contains no cryptographic signature or replay protection; IP pinning is an operational sanity check, not a cryptographic security boundary.
- **Development ADB Shell**: The Tina Linux developer image omits ADB authentication keys. The MAC address filter in `lunch.sh` is an internal LAN guard and does not constitute cryptographic identity verification or access control.
- **Plugin Sandboxing**: `packages/plugin-host` validates entrypoint digests, manifest capabilities, and payload limits, isolating failures into child Node.js processes. This child process model is not an OS-level filesystem sandbox; untrusted arbitrary plugins must not be loaded without additional isolation.
- **Native Privilege Execution**: Tool approvals and command execution defer strictly to each harness's native policy engine. Observed stdout events or console timeouts are never converted into synthetic physical approvals.
- **Air-Gapped Operation**: Local Web UI, hardware control, and locally cached STT speech models operate completely offline. Cloud model inference and initial weight downloads require upstream vendor connectivity.

---

## 3. Installed Baseline Versions & Continuous Observation

The following baseline was validated on the primary review workstation (Windows 11) as of 2026-10-02:

| Component | Detected Version | Verification Evidence |
| --- | --- | --- |
| **Codex PATH CLI** | `0.160.0` | `codex --version`, native npm executable in system PATH |
| **Codex App-Server (Embedded)** | `0.159.2` | Running desktop app executable: `codex.exe --version` |
| **AGY CLI** | `1.2.13` | `agy --version` |
| **OpenCode Desktop** | `1.18.33` | Installed `app.asar/package.json` and PE ProductVersion |
| **OpenCode CLI** | Not Detected | CLI compatibility is not inferred from Desktop app version |
| **Node.js Requirement** | `>=22.12.0` | Defined in `package.json`. Validated on Node `24.19.0` |
| **Python** | `3.12.10` | Active Python virtual environment runtime |

The machine-readable reference specification is maintained in `Snowball_Middleware/config/harness-compatibility.json`. Models, effort variants, and active session lists are discovered dynamically from native CLI outputs rather than hardcoded tables. Upstream releases are monitored via [Codex Releases](https://github.com/openai/codex/releases), [AGY Releases](https://github.com/google-antigravity/antigravity-cli/releases), and [OpenCode Releases](https://github.com/anomalyco/opencode/releases).

---

## 4. MK20 Firmware, Recovery & SD Image

QMK firmware sources and Allwinner T113 BSP source code were provided directly by the hardware manufacturer via email (confirmed on 2026-10-02). The Snowball SD distribution built from these sources constitutes the official deployment and verification target. Factory firmware reverse-engineering is outside the project scope.

### Mandatory Modified QMK Firmware

Factory QMK firmware enters an indefinite wait loop on cold boot when USB host enumeration is absent, disabling matrix scanning and UART escalation on standalone power supplies. Applying the modified QMK build is **mandatory**:
- Build flags in `mk20_plus/rules.mk`: `NO_USB_STARTUP_CHECK = yes`, `NO_SUSPEND_POWER_DOWN = yes`.
- Official MK20 binary: `hardware/mk20/qmk/bin/syk_keyboards_mk20_plus_via.bin` (SHA-256: `28415537c79b7b08a0d735e337423633838d857dcae9e4968d4fbd103a2fff19`).
- Flashing procedure: Disconnect USB → hold top-left physical key while plugging in USB → flash via [QMK Toolbox](https://qmk.fm/toolbox). *Do not flash MK10 firmware onto MK20.*

### MicroSD Deployment, Backup & Rollback

1. Copy `dev-access.conf.example` to `dev-access.conf` on the microSD root and configure local 2.4 GHz Wi-Fi credentials and the workstation MAC address.
2. **Full Disk Backup**: Prior to deployment, create a full raw block-level image of the microSD card. Record the total capacity, date, and SHA-256 checksum outside the repository.
3. Deploy `lunch.sh`, `dev-access.conf`, custom HUD binary (`mk20-hud`), and fonts (`fonts/D2Coding.ttf`) to `/mnt/SDCARD/`.
4. In case of operational failure, write the backup raw image back to the card to restore the known state.

### HUD Engine, Boot Splash & Standby Mode

The MK20 hardware features a **428×142** header display (`/dev/fb21`) and 20 individual **128×128** key displays (`/dev/fb1`..`/dev/fb20`) operating in 16-bit RGB565:

- **Bootloader & Kernel Splash**:
  - U-Boot 160×160 BMP: `/mnt/SDCARD/bootlogo.bmp` (Source: `assets/bootlogo.bmp`)
  - Top header 428×142 RGB565 splash: `/mnt/SDCARD/mk20-plus.bin` (Source: `assets/mk20-plus.bin`)
- **Offline Standby Mode**:
  - If host middleware (`start-all.mjs`) is unreachable or UDP sync packets cease for > 15 seconds, the HUD automatically engages **Snowball Standby Mode**.
  - **Top Display (`/dev/fb21`)**: Left pane displays connection status alerts ("Host Disconnected", "Waiting for host middleware...", "Snowball Standby Mode"), while the right pane (x=286, y=7) renders the 128×128 RGB565 Snowball puppy icon.
  - **Key Matrix (`/dev/fb10` & others)**: Center Key 10 illuminates with the 128×128 Snowball puppy icon; all other 19 keys turn black (backlight off) to visually signal standby.
  - **Live Restoration**: When middleware sync resumes, all displays immediately switch to the active workspace session.

| Standby Top Display (`/dev/fb21`) | Standby Key 10 (`/dev/fb10`) | Connected Active Display (`/dev/fb21`) |
| :---: | :---: | :---: |
| <img src="../assets/screenshots/standby_top_display.png" width="300" alt="Standby Top Display"> | <img src="../assets/screenshots/standby_key10.png" width="128" alt="Standby Key 10"> | <img src="../assets/screenshots/online_top_display.png" width="300" alt="Connected Top Display"> |

- **Firmware Update Persistence**:
  - **Power Cycles & Normal Reboots**: `/mnt/SDCARD` is mounted on the non-volatile FAT32 `boot-resource` eMMC partition (`/dev/mmcblk0p1`). Boot logos, `mk20-hud`, and configs **100% persist** across reboots.
  - **Full Firmware Reflashing (OTA, LiveSuite, PhoenixCard)**: Writing a raw firmware image (`.img`) completely formats all partitions, resetting `bootlogo.bmp` and `mk20-plus.bin` to factory defaults. To preserve custom assets, either embed them directly into the Tina BSP SDK build tree (`Tina-t113-pro/.../boot-resource`) or maintain an auto-restore verification check in `lunch.sh`.

---

## 5. Live State Management & Transport

- **No Synthetic State**: If native project or session sources are empty, the UI displays an empty state. Fake projects, dummy models, or synthetic completions are strictly prohibited.
- **Draft & Execution Ownership**: MK20's `New task` represents a local uncommitted draft. Dispatch uses the explicit working directory (`cwd`) of the active session.
- **Dynamic Controls**: Model, effort variant, and permissions reflect discovered capabilities. Codex new turns default to `on-request`/`read-only`; existing threads inherit their native resume policy.
- **Abort & Cancellation**: Key 4 (Stop) sends an `AbortSignal` directly to the child process bound to the active session. It does not blindly terminate external or grandchild processes.
- **State Partitioning**: MK20 `ContextManager` maintains distinct scopes per harness, device, and project. Audio captures and dispatch operations bind immutably to their initiating session:
  - `K13`: Harness selector
  - `K9`: Project selector
  - `K5`: Session selector
  - `K1`: New task draft
  - `K12`: Speak on **MK20 onboard speaker** (Destination: `device`. Audio is streamed natively via TCP port 7702 `SNAU` binary protocol directly to `mk20-audio` $\rightarrow$ ALSA `aplay`, zero disk I/O, software + hardware gain scaling; press again to stop)
  - `K8`: Speak on **host PC / Mac speaker** (Destination: `host`. Label `Speak PC` on Windows/Linux, `Speak Mac` on macOS; in-memory PCM volume scaled to match Right Knob; press again to stop)
  - `K20`: Voice recording toggle (Initiates TCP port 7702 streaming capture directly to host; interrupts active TTS playback immediately to avoid acoustic echo and ALSA device conflicts)
  - `K16`: Transcribe & Send
  - `K4`: Cancel / Discard draft / Cut off active speech on both destinations (host player kill + stop signal to `mk20-audio`)
  - `Left Knob`: Scroll navigation and item commit
  - `Right Knob`: System volume (0..100) and click mute. Dynamically scales playback volume across both MK20 hardware speaker (via mixer + 16-bit PCM attenuation) and host speakers.
  - `Dual-Knob Chord`: Simultaneous press toggles HOST Mode (keystroke masking) and HID Mode (keystroke passthrough to PC).

### Speech Synthesis (TTS) 3-Tier Fallback Chain

Voice responses utilize a local-first, low-latency synthesis pipeline:
```mermaid
flowchart TD
  Text[Agent Turn Text] --> Split[Sentence Chunker]
  Split --> P1[Prefetch Chunk 0]
  P1 --> Play1[AudioPlayer Stream Play]
  Play1 -->|Concurrent Background Prefetch| P2[Synthesize Chunk 1..N]
  P2 --> Play2[Sequential Playback]
  P1 -->|Failure| OS[OS Native TTS - Windows SAPI / macOS say / Linux espeak]
```
1. **Tier 1 (Supertonic Optimized ONNX)**: Supertonic-3 resident synthesis.
   - **Throughput Profile**: Supertonic is an ONNX diffusion pipeline with multi-step NumPy loops. ONNX Runtime `CPUExecutionProvider` (AVX2 / AVX-512) achieves ultra-low latency (**~1.5s** per sentence, RTF 0.37) by eliminating the 200+ PCIe host-device `Memcpy` nodes that choke CUDA loops.
   - **Sentence Pipelined Streaming**: Rather than batching 300+ characters into a single blocking synthesize call, `LocalSupertonicProvider` (`host/src/audio/local-supertonic.ts`) chunks text into natural sentences, synthesizes chunk 0 immediately to start playback in $< 1.5\text{s}$, and prefetches chunk $1\dots N$ in the background while the previous sentence plays on speakers.
2. **Tier 2 (Supertonic Alternate EP)**: Dynamic Execution Provider evaluation (`CUDAExecutionProvider`, `DmlExecutionProvider`, `CoreMLExecutionProvider`) when requested.
3. **Tier 3 (OS Native TTS)**: Emergency offline platform fallback using native OS speech synthesizers (`PowerShell SAPI` on Windows, `say` on macOS, `espeak` on Linux) so speech delivery never fails.

#### Speech Text Sanitization (`host/src/audio/korean-transliterate.ts`)
Every utterance passes through `cleanTextForSpeech()` before synthesis (used by both `LocalSupertonicProvider` and middleware `start-all.mjs`):
1. **Markdown stripping**: code fences → "코드 블록 생략", inline code / links / headers / bullets / emphasis removed.
2. **Meaningless token removal** (`stripMeaninglessHashes`): UUIDs, `0x…` pointers, `sha256:/sha1:/md5:` digests, and 7–64 char hex strings that contain both digits and a–f letters (Git hashes; pure numbers and words like `beef` are kept). `커밋 854fc69를` → `커밋을` with particle re-agreement (`attachParticle`).
3. **English → Hangul pronunciation** (`transliterateEnglishToHangul`, only when the text is Korean): developer-term dictionary (`GitHub`→깃허브, `PowerShell`→파워셸, `MK20`→엠케이이십, `PC`→피씨 …), key names (`K12`→케이십이), and letter-by-letter reading of remaining 2–5 letter all-caps acronyms.
   - *Library survey*: `hangulize` (PyPI, last release 2012) and `g2pK`/`g2pkk` (depends on `eunjeon` → `distutils`, removed in Python 3.12) do not install on the bundled Python 3.12 runtime, so a zero-dependency dictionary + rule engine is used.

### Cross-Platform Hardware Acceleration Matrix (STT & TTS)

Snowball automatically probes and binds the optimal hardware execution backend per platform without requiring manual user reconfiguration:

| Platform & Hardware | Speech-to-Text (STT - Whisper) | Text-to-Speech (TTS - Supertonic) | Fallback Progression |
| :--- | :--- | :--- | :--- |
| **Windows + NVIDIA** | `faster-whisper` (CUDA / cuBLAS) | Supertonic (`CUDAExecutionProvider`) | CUDA $\rightarrow$ CPU $\rightarrow$ OS SAPI |
| **Windows + AMD / Intel** | `whisper.cpp` (Vulkan) / CPU | Supertonic (`DmlExecutionProvider` DirectML) | DirectML $\rightarrow$ CPU $\rightarrow$ OS SAPI |
| **macOS (Apple Silicon)** | `whisper.cpp` (Metal) / `mlx-whisper` | Supertonic (`CoreMLExecutionProvider`) | CoreML/Metal $\rightarrow$ CPU $\rightarrow$ OS `say` |
| **Linux + NVIDIA** | `faster-whisper` (CUDA) | Supertonic (`CUDAExecutionProvider`) | CUDA $\rightarrow$ CPU $\rightarrow$ OS `espeak` |
| **Linux + AMD / Intel** | `whisper.cpp` (Vulkan) | Supertonic (OpenVINO / CPU EP) | OpenVINO/CPU $\rightarrow$ OS `espeak` |
| **Universal Fallback** | `faster-whisper` / `whisper.cpp` (CPU) | Supertonic (`CPUExecutionProvider`) | OS Native Synthesizer |

#### Backend Roles:
- **DirectML (DirectX 12)**: Used in the TTS ONNX pipeline (`DmlExecutionProvider`) on Windows to accelerate AMD Radeon and Intel Arc/Iris GPUs without requiring NVIDIA CUDA.
- **MLX / Metal / CoreML**: Used on Apple Silicon macOS to leverage unified memory, the Apple Neural Engine (ANE), and Metal GPU for ultra-low latency STT (`whisper.cpp Metal` / `mlx-whisper`) and TTS (`CoreMLExecutionProvider`).
- **Vulkan (`ggml-vulkan`)**: Used in the STT pipeline (`whisper.cpp`) as a cross-platform compute backend for AMD and Intel GPUs on Windows and Linux, utilizing standard SPIR-V compute shaders.

### Multilingual Support Framework (i18n)

Multilingual support is structured around complete language packages:
$$\text{Language Package} = \text{UI Resources (Fonts \& Labels)} + \text{STT (Whisper)} + \text{TTS (Supertonic / OS Native)}$$

- **Living OS Auto-Discovery**: Automatically queries OS display culture (`(Get-Culture).Name` on Windows, `LANG` on POSIX) on startup. Korean OS (`ko-KR`) configures `ko` as the primary language and activates D2Coding Korean font rendering on the MK20 HUD.
- **Dynamic Configuration & Lifecycle**: Languages can be enabled, disabled, or set as primary via `/v1/settings` and `languageManager`. Adding a language ensures corresponding STT and TTS model weights exist locally.

### Native Audio Streaming Subsystem (`mk20-audio` & `SNAU` Protocol)

To eliminate high latency and flash wear from push-and-pull ADB commands, audio I/O on the MK20 operates via a dedicated native C daemon (`mk20-audio`) running on Tina Linux:

- **Daemon Architecture & Deployment**:
  - Source: `hardware/mk20/hud/mk20-audio.c`.
  - Statically linked ARMv7 binary (`arm-linux-gnueabihf-gcc -static -O2`) deployed to `/mnt/SDCARD/mk20-audio`.
  - Automatically launched at boot via `/mnt/SDCARD/lunch.sh` (`/mnt/SDCARD/mk20-audio &`), listening on TCP port **7702** (`INADDR_ANY`).
  - Cross-platform network model: Windows host initiates outbound connections to `192.168.1.248:7702`, traversing Windows Defender Firewall without administrator elevation prompts.

- **`SNAU` Binary Streaming Protocol**:
  Communication over TCP port 7702 uses a compact 16-byte binary header (`struct snau_header`):
  | Field | Type | Size | Description |
  | :--- | :--- | :--- | :--- |
  | `magic` | `char[4]` | 4B | Protocol magic: `'S'`, `'N'`, `'A'`, `'U'` (`0x55414E53` in LE) |
  | `mode` | `uint8_t` | 1B | `1` = PLAY (TTS), `2` = RECORD (STT), `3` = PING |
  | `channels` | `uint8_t` | 1B | `1` (mono) or `2` (stereo) |
  | `format` | `uint8_t` | 1B | Sample format: `16` (16-bit signed integer PCM, little-endian) |
  | `volume` | `uint8_t` | 1B | Software attenuation volume factor (`0`..`100`) |
  | `sample_rate` | `uint32_t` | 4B | Sample rate: `16000` (STT), `24000` (TTS Supertonic), or custom |
  | `data_len` | `uint32_t` | 4B | Payload byte count (`0` for continuous live streaming until socket close) |
  | `is_muted` | `uint8_t` | 1B | `1` if audio output should be muted, `0` otherwise |
  | `reserved` | `uint8_t[3]` | 3B | Zero-padding alignment bytes |

- **Microphone Capture (STT Streaming)**:
  - The host connects with `MODE_RECORD`. The daemon spawns ALSA `arecord -D hw:0,0 -r 16000 -f S16_LE -c 1 -t raw` bound to the onboard MIC3 channel.
  - Raw PCM samples are streamed directly over the TCP socket to Node.js `AudioTransport` (`host/src/audio/transport.ts`).
  - Upon user release or completion, the host closes the stream, prepends an in-memory 44-byte canonical WAV header (`monoPcmToWav`), and passes the buffer directly to the local Whisper worker. Zero temporary files are written to MK20 storage.

- **Speaker Playback (TTS Streaming)**:
  - The host connects with `MODE_PLAY`, transmitting the `SNAU` header with current knob volume and mute status.
  - The daemon sets the hardware ALSA mixer levels (`LINEOUT volume` 0..31 and `Headphone volume` 0..7 via `amixer cset`), scales PCM samples in 16-bit integer space, and pipes them directly into `aplay -D hw:0,0 -r <rate> -f S16_LE -c <ch> -t raw`.
  - Zero disk I/O on the device; speech plays back in real-time as chunks stream from the host.

- **Volume & Mute Integration**:
  - The MK20's physical `Right Knob` controls system volume (`0`..`100`) and click mute.
  - **MK20 Hardware Speaker**: Volume is transmitted in the `SNAU` header and applied simultaneously at the hardware mixer and software scaling stages in `mk20-audio`.
  - **Host PC / Mac Speaker**: Volume is scaled directly in-memory on the host's 16-bit PCM buffer before dispatching to the OS audio player (`PowerShell SoundPlayer` on Windows, `afplay` on macOS, `aplay` on Linux).

- **Hardware Concurrency & Interruption Management**:
  - The Allwinner T113 `audiocodec` (`hw:0,0`) cannot operate in full-duplex mode. Active speaker playback (`aplay`) locks the audio device and must terminate before microphone recording (`arecord`) can begin.
  - Pressing `Talk` (Key 20) or `Cancel` (Key 4) issues an immediate cutoff signal, killing active playback and resetting socket buffers before recording begins.

- **Legacy Fallback & Fault Tolerance**:
  - If TCP port 7702 is unreachable (e.g. older SD card image without `mk20-audio`), `AudioTransport` transparently falls back to the ADB pipeline (`arecord /tmp/snowball_rec.wav` $\rightarrow$ `adb pull`), guaranteeing backward compatibility and zero crash risk.

---

## 6. Installation & Verification

Place both repositories in the same parent directory:

```text
Snowball_Control/
Snowball_Middleware/
```

```bash
# 1. Build Control host library
cd Snowball_Control/host
npm ci && npm run build

# 2. Build Middleware and launch local Web Supervisor
cd ../../Snowball_Middleware
npm ci && npm run build
npm run start:local

# 3. Launch full integrated MK20 runtime
node scripts/start-all.mjs
```

### Verification Checklist

- **Control Hardware Plugin Tests**: `npm test --prefix plugins/device-mk20` (13 passed)
- **QMK Serial Contract**: `powershell -File hardware/mk20/contract/Test-QmkProtocol.ps1` (14 passed)
- **Control Host Unit Tests**: `npm test --prefix host` (66 passed, 2 optional STT skipped)
- **Middleware Comprehensive Tests**: `npm test` in `Snowball_Middleware` (241 passed, 5 optional skipped)
- **Live Framebuffer Capture**: `python scripts/dump_mk20_screens.py --adb <PATH> --device <IP:PORT> --output-dir <DIR>`

---

## 7. Pre-Release Verification & Remaining Scope

1. **Reproducible Build Documentation**: Maintain precise source hashes, toolchain versions, and compilation instructions for both QMK and Tina Linux SD images. Preserve all upstream GPLv2 licensing notices.
2. **Workstation CLI Baseline**: Document exact CLI versions on all development machines. OpenCode CLI path and capabilities must be explicitly verified when available.
3. **Approval & Abort Verification**: Native tool approval and turn cancellation bridges must be physically validated across all supported harness CLIs on live hardware.
4. **SD Image Deployment & Rollback Testing**: Verify full-image backup, custom Snowball SD card booting, HUD performance, and clean recovery using physical microSD media.
5. **Git History Hygiene**: Both repositories have undergone history sanitization. Contributors must clone fresh checkouts rather than force-pushing stale refs.
6. **Plugin Sandboxing Evolution**: Current Preview limits plugin execution to trusted internal adapters. Future extensions require kernel-level filesystem and process capability isolation.

---

## 📄 License

Snowball proprietary code is licensed under [Apache-2.0](file:///E:/developments/projects/Snowball_Control/LICENSE). Hardware QMK firmware code is governed by upstream GNU General Public License v2 (GPLv2). Vendor SDKs, proprietary BSPs, private credentials, and binary disk images are intentionally excluded from version control.
