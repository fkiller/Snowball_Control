<p align="center">
  <img src="assets/banner.png" alt="Snowball Control Banner" width="100%">
</p>

<h1 align="center">
  <img src="assets/icon.png" width="48" height="48" valign="middle" alt="Snowball Icon">
  Snowball Control
</h1>

<p align="center">
  <strong>Physical Hardware Control Panel & Native Desktop Middleware for AI Coding Agents</strong>
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-Apache_2.0-blue.svg" alt="License"></a>
  <img src="https://img.shields.io/badge/Node.js-%3E%3D18.0.0-green.svg" alt="Node.js">
  <img src="https://img.shields.io/badge/Python-%3E%3D3.10-yellow.svg" alt="Python">
  <img src="https://img.shields.io/badge/Hardware-MK20%20(Tina%20T113%20%2B%20GD32)-purple.svg" alt="Hardware MK20">
  <img src="https://img.shields.io/badge/Tests-100%25%20Passing-brightgreen.svg" alt="Tests">
</p>

---

## 🌟 Overview

**Snowball Control** gives developers physical, tactile control over autonomous AI coding agents running on their local workstations. By bridging the **MK20 hardware terminal** with a low-latency native desktop host daemon, Snowball turns agent oversight into a physical desk experience: inspect running sessions, review diffs, grant file/command approvals, dictate prompts, switch models, and adjust reasoning effort levels—all without switching desktop windows or breaking focus.

Snowball Control natively coordinates with leading AI coding harnesses:
- **OpenCode** (Local HTTP/SSE streaming, model/effort discovery, tool permissions)
- **Google Antigravity (AGY)** (Local CLI integration, session dispatch)
- **OpenAI Codex** (Desktop IPC, session orchestration, rollout reconcile)

---

## ⚡ Non-Negotiable Core Principles

1. **Zero Simulation (시뮬레이션 전면 금지)**
   - No mock delays, fake approvals, or simulated responses.
   - Every key press, knob turn, voice transcription, and session switch interacts directly with live local processes, real hardware registers, and native agent CLIs.
2. **Living Source of Truth (살아있는 원천 기반 동적 발견)**
   - Model lists, reasoning efforts (Variants), sessions, and workspaces are **never** hardcoded.
   - The daemon dynamically interrogates native tools (`opencode models`, `~/.cache/opencode/models.json`, `agy.exe models`, Codex rollout indices) to guarantee 100% fidelity with the local machine environment.
3. **Local-First & Security Boundary**
   - The local host supervisor and device transport operate strictly on local loopback and trusted LAN.
   - Zero mandatory cloud dependencies: full physical control and voice input work seamlessly even without an external internet connection.
4. **Dual-Mode Operation & Dual-Knob Chord Switching**
   - **Host AI Control Mode**: Keypad matrices map to agent actions (approve, reject, diff scroll, session switch).
   - **Native PC Typing Mode**: Keypad restores standard mechanical HID keystrokes for typing.
   - **Instant Hardware Toggle**: Pressing both rotary push-encoders simultaneously toggles between modes instantly with on-screen HUD feedback.
5. **Standalone Wireless Boot**
   - Equipped with custom QMK firmware (`NO_USB_STARTUP_CHECK = yes`, `NO_SUSPEND_POWER_DOWN = yes`), the MK20 boots and operates independently over Wi-Fi/UDP without requiring an active USB host connection.

---

## 🖥️ Hardware: The MK20 Desk Terminal

The MK20 is a dedicated dual-processor desktop control surface:

```text
┌────────────────────────────────────────────────────────┐
│               TOP DISPLAY (400 x 120)                  │
│       Session Info / Diff Preview / Agent Status       │
└────────────────────────────────────────────────────────┘
┌───────┐ ┌────────────────────────────────────┐ ┌───────┐
│ Left  │ │        20 LCD MECHANICAL KEYS      │ │ Right │
│ Rotary│ │       (4 x 5 Matrix, 128x128 each) │ │ Rotary│
│ Knob  │ │  Dynamic Action Labels & Icons     │ │ Knob  │
└───────┘ └────────────────────────────────────┘ └───────┘
```

- **Application Processor**: Allwinner Tina T113 (Dual-Core ARM Cortex-A7) running a custom lightweight Linux C HUD daemon (`mk20-hud`).
- **Keyboard Controller**: GD32F303 (ARM Cortex-M4) running QMK firmware with VIA support, communicating via high-speed internal UART (`/dev/ttyS1`).
- **Displays**: 640x656 total physical LCD resolution partitioned into a 400x120 Top status bar and 20 individual 128x128 per-key button displays.
- **Audio Subsystem**: Onboard microphone and speaker with native ALSA capture and local low-latency `faster-whisper` GPU/CPU transcription.

---

## 🏗️ Architecture

For the complete architectural specification, protocol definitions, and hardware register maps, see the single authoritative source:
👉 **[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)**

```text
 ┌──────────────────────┐                     ┌────────────────────────┐
 │   MK20 Desk Terminal │                     │     Host Workstation   │
 │                      │   Wi-Fi / UDP       │                        │
 │  Tina Linux T113     │ <─────────────────> │  Host Middleware       │
 │   - mk20-hud daemon  │                     │   - Node.js Daemon     │
 │   - ALSA voice rec   │   USB HID / DFU     │   - faster-whisper STT │
 │                      │ ------------------> │                        │
 │  GD32 QMK Controller │                     │  Local AI Harnesses    │
 │   - 4x5 Key Matrix   │                     │   - OpenCode           │
 │   - Dual Encoders    │                     │   - Antigravity (AGY)  │
 └──────────────────────┘                     │   - OpenAI Codex       │
                                              └────────────────────────┘
```

---

## 📦 MK20 3-Tier Deliverables & Deployment Guide

MK20 hardware operation relies on three complementary software deliverables:

```text
┌────────────────────────────────────────────────────────────────────────┐
│ Tier 1: QMK Keyboard Firmware (GD32F303 / STM32F103)                   │
│         Precompiled: hardware/mk20/qmk/bin/syk_keyboards_mk20_plus_via.bin│
│         Source:      hardware/mk20/qmk/source/                         │
├────────────────────────────────────────────────────────────────────────┤
│ Tier 2: Tina Linux OS & Recovery Firmware (Allwinner T113)             │
│         Recovery Hook: hardware/mk20/dev-tools/lunch.sh               │
│         Config:        hardware/mk20/dev-tools/dev-access.conf         │
├────────────────────────────────────────────────────────────────────────┤
│ Tier 3: Middleware-Client HUD Daemon (mk20-hud)                        │
│         Native C Source: hardware/mk20/hud/                            │
│         Target Runtime:  /mnt/SDCARD/mk20-hud                          │
└────────────────────────────────────────────────────────────────────────┘
```

### 1. Tier 1: QMK Keyboard Firmware
The custom QMK firmware provides **Standalone Wireless Boot** (`NO_USB_STARTUP_CHECK = yes`, `NO_SUSPEND_POWER_DOWN = yes`) and **Dual-Knob Chord Switching** (pressing Left + Right knobs toggles Host AI Control `KC_NO` vs standard mechanical PC typing).

**Flashing via STM32duino DFU**:
1. Unplug the MK20 USB cable from the PC.
2. Hold down the **top-left key (Row 0, Col 0 - `KC_0`)**.
3. Plug the USB cable back into the PC while holding the key, then release after 1-2 seconds.
4. Flash using [QMK Toolbox](https://qmk.fm/toolbox) or CLI:
   ```bash
   dfu-util -a 2 -d 1EAF:0003 -R -D hardware/mk20/qmk/bin/syk_keyboards_mk20_plus_via.bin
   ```
For complete details, see [hardware/mk20/qmk/README.md](hardware/mk20/qmk/README.md).

### 2. Tier 2: Tina Linux OS & MicroSD Recovery Hook
The Allwinner T113 dual-core ARM Cortex-A7 runs customized Tina Linux with framebuffer (`/dev/fb0`), ALSA sound (`hw:0,0`), and Wi-Fi.

**MicroSD Zero-Risk Deployment**:
1. Format a MicroSD card as FAT32.
2. Copy `hardware/mk20/dev-tools/dev-access.conf.example` to `dev-access.conf` and set your local 2.4 GHz Wi-Fi SSID, password, and PC MAC address.
3. Copy `hardware/mk20/dev-tools/lunch.sh` and `dev-access.conf` to the root of the MicroSD card.
4. Insert into the MK20. On boot, `lunch.sh` connects to Wi-Fi, activates TCP ADB (`:5555`), applies firewall filtering, and auto-starts the HUD daemon. Removing the card reverts the device cleanly to the factory image.
For full procedures, see [hardware/mk20/dev-tools/README.md](hardware/mk20/dev-tools/README.md).

### 3. Tier 3: Middleware-Client Device HUD Daemon (`mk20-hud`)
The native C daemon running on Tina Linux renders the 640x656 LCD (400x120 Top HUD + 20x 128x128 keys with subpixel rounded corners), communicates with the host over UDP port 7701, and escalates QMK UART `/dev/ttyS1` key events.

**Building & Deploying via ADB**:
```bash
# Cross-compile on Linux/WSL (if modifying HUD source)
cd hardware/mk20/hud && make

# Connect to MK20 via Wi-Fi ADB and deploy
adb connect 192.168.1.248:5555
adb -s 192.168.1.248:5555 push hardware/mk20/hud/mk20-hud /mnt/SDCARD/mk20-hud
adb -s 192.168.1.248:5555 push hardware/mk20/hud/voice_rec.sh /mnt/SDCARD/voice_rec.sh
adb -s 192.168.1.248:5555 shell chmod +x /mnt/SDCARD/mk20-hud /mnt/SDCARD/voice_rec.sh
adb -s 192.168.1.248:5555 shell "/mnt/SDCARD/mk20-hud > /tmp/hud.log 2>&1 &"
```

---

## 💻 Snowball-Middleware Multi-Platform Setup Guide

The host middleware runs natively on **Windows**, **macOS**, and **Linux** workstations.

### 🪟 Windows (x64)
- **Prerequisites**: Node.js >= 18, Python >= 3.10, (Recommended) NVIDIA GPU + CUDA 12.x.
```powershell
# 1. Clone repository & install host dependencies
cd host
npm install
npm run build

# 2. (Optional) Set up local Whisper voice worker with CUDA acceleration
python -m venv .venv-whisper
.\.venv-whisper\Scripts\pip install faster-whisper nvidia-cublas-cu12

# 3. Start the host middleware daemon
npm start
```
*Auto-discovered harnesses:* OpenCode (`opencode.cmd`), Antigravity (`%LOCALAPPDATA%\agy\bin\agy.exe`), OpenAI Codex (`%LOCALAPPDATA%\OpenAI\Codex\bin\codex.exe`).

### 🍎 macOS (Apple Silicon & Intel)
- **Prerequisites**: macOS 12+, Node.js >= 18, Python >= 3.10, Homebrew.
- **Hardware Acceleration**: Automatic Apple Silicon Metal GPU acceleration for low-latency Whisper STT.
```bash
# 1. Install prerequisites via Homebrew
brew install node python@3.11 android-platform-tools

# 2. Clone repository & install dependencies
cd host
npm install
npm run build

# 3. (Optional) Set up local Whisper voice worker
python3 -m venv .venv-whisper
./.venv-whisper/bin/pip install faster-whisper

# 4. Start the host middleware daemon
npm start
```
*Auto-discovered harnesses:* OpenCode (`opencode`), Antigravity (`~/Library/Application Support/Antigravity`, `~/.local/bin/agy`), OpenAI Codex (`/Applications/Codex.app`, `codex`).

### 🐧 Linux (Ubuntu / Debian / Fedora / Arch)
- **Prerequisites**: Linux Kernel 5.4+, Node.js >= 18, Python >= 3.10, `adb`.
- **Hardware Acceleration**: NVIDIA CUDA (`libcublas.so`), Vulkan, or CPU int8 fallback.
```bash
# 1. Install prerequisites (Ubuntu/Debian)
sudo apt update && sudo apt install -y nodejs npm python3 python3-venv adb git

# 2. Clone repository & install dependencies
cd host
npm install
npm run build

# 3. (Optional) Set up local Whisper voice worker
python3 -m venv .venv-whisper
./.venv-whisper/bin/pip install faster-whisper

# 4. Start the host middleware daemon
npm start
```
*Auto-discovered harnesses:* OpenCode (`opencode`), Antigravity (`~/.config/Antigravity`, `~/.local/bin/agy`), OpenAI Codex (`~/.local/bin/codex`).

---

## 🧪 Verification & Test Suite

Run the full automated test suite (76 total tests) across host middleware, device plugins, and QMK serial contracts:

```bash
# 1. Run host test suite (49 unit & integration tests)
cd host
npm test

# 2. Run device plugin & skin engine tests (13 tests)
cd ../plugins/device-mk20
node tests/lab.test.mjs
node tests/skin.test.mjs

# 3. Run QMK protocol serial contract tests (14 tests)
powershell -File hardware/mk20/contract/Test-QmkProtocol.ps1
```

---

## 📁 Repository Structure

```text
Snowball_Control/
├── assets/                     # Official brand artwork, icon, and banner
│   ├── banner.png
│   └── icon.png
├── docs/
│   ├── ARCHITECTURE.md         # Single Source of Truth architecture specification
│   └── NORTH_STAR.md           # Product vision and milestone tracking
├── hardware/
│   └── mk20/
│       ├── contract/           # Linux/QMK UART serial contract specifications & tests
│       ├── dev-tools/          # Device provisioning, recovery, and ADB scripts
│       ├── hud/                # Tina Linux native C HUD daemon (mk20-hud) source
│       ├── orchestration/      # Session orchestration schema & pairing manager
│       └── qmk/                # QMK firmware binaries, guide, and source code
├── host/                       # Host middleware daemon, harness adapters & Whisper STT
├── plugins/                    # Device & harness sandboxed plugins
├── scripts/                    # Screen capture, diagnostic & test automation scripts
├── LICENSE                     # Apache-2.0 License
└── README.md                   # Project overview and quick start guide
```

---

## 📄 License

This project is licensed under the Apache License 2.0 - see the [LICENSE](LICENSE) file for details.
