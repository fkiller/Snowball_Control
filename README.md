<p align="center">
  <img src="assets/banner.png" alt="Snowball Control Banner" width="100%">
</p>

<h1 align="center">
  <img src="assets/icon.png" width="48" height="48" valign="middle" alt="Snowball Icon">
  Snowball Control — Preview
</h1>

<p align="center">
  <strong>The Physical Terminal for Snowball's inter-Harness Local Control</strong>
</p>

<p align="center">
  <a href="README.md">English</a> | <a href="README.ko.md">한국어</a>
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-Apache_2.0-blue.svg" alt="License"></a>
  <img src="https://img.shields.io/badge/Node.js-%3E%3D22.12-green.svg" alt="Node.js">
  <img src="https://img.shields.io/badge/Platforms-Tina%20Linux%20%7C%20QMK-orange.svg" alt="Platforms">
  <a href="https://github.com/fkiller/Snowball_Middleware"><img src="https://img.shields.io/badge/Companion-Snowball%20Middleware-purple.svg" alt="Companion Repo"></a>
</p>

---

## 🌟 Overview

**Snowball is an inter-Harness local control interface.** It gives developers a common way to navigate and control Codex, AGY, and OpenCode through an MK20 desk terminal and local Web Supervisor. The user chooses the harness, project, and session; each harness keeps its native runtime, history, models, and permissions.

Our philosophy is to keep control in the user's hands and on their computer: make switching tools and following work immediate, discover capabilities from the installed environment, and show real results and limitations. The full [product philosophy](docs/ARCHITECTURE.md#product-philosophy-inter-harness-local-control) is maintained with the architecture.

**Preview:** Intended exclusively for personal workstations and trusted local networks. The initial scope covers OpenAI Codex, Google Antigravity (AGY), OpenCode, MK20 hardware, and local Web UI. Certain security boundaries (such as unencrypted UDP sync and development TCP ADB) are omitted as documented in [Current Architecture and Boundaries](docs/ARCHITECTURE.md).

**Snowball Control** provides the physical terminal runtime, custom QMK firmware, native Tina Linux HUD engine, device tools, and STT reference host for the **MK20 Desktop Terminal**.

The complete middleware and Web Supervisor are hosted in the companion repository:  
👉 **[`Snowball_Middleware`](https://github.com/fkiller/Snowball_Middleware)** (place both repositories in the same parent directory). Control's internal `host/npm start` is a legacy Codex-only reference runtime.

---

## 🎥 Hardware Demos & Visual Showcase

### 1. Live Breadcrumb Navigation & Voice Session
Demonstration of the MK20 running its dynamic breadcrumb hierarchy (`Device > Harness > Project > Session`), built-in mic speech prompt capture, modal view with top-left title/close button, and rotary knob selection:

<p align="center">
  <img src="assets/screenshots/mk20_navigation_demo.gif" width="480" alt="MK20 Live Navigation Demo"><br>
  <em>Physical hardware demonstration of breadcrumb hierarchy and modal navigation.</em><br>
  <a href="assets/videos/mk20_navigation_demo.mp4">▶️ Watch Full High-Res MP4</a> &nbsp;|&nbsp; <a href="https://x.com/fkiller/status/2099345561627382015">🔗 Original Post on X</a>
</p>

### 2. UI Elements & Key Matrix Testing
Physical testing of the 20 mechanical key switches with individual 128×128 LCD keycaps, dual rotary encoders, dynamic contrast themes, and low-latency frame drawing:

<p align="center">
  <img src="assets/screenshots/mk20_ui_elements_test.gif" width="400" alt="MK20 UI Elements Test"><br>
  <em>Physical testing of key matrix displays, rotary dials, and dynamic themes.</em><br>
  <a href="assets/videos/mk20_ui_elements_test.mp4">▶️ Watch Full High-Res MP4</a> &nbsp;|&nbsp; <a href="https://x.com/fkiller/status/2095861881281917119">🔗 Original Post on X</a>
</p>

### 3. Web Supervisor Dashboard
The local-loopback control plane (`http://127.0.0.1:8765/`) visualizing detected workspaces, session journals, and MK20 hardware status in real-time:

<p align="center">
  <img src="assets/screenshots/web_supervisor_dashboard_en.png" width="100%" alt="Snowball Web Supervisor Dashboard">
</p>

### 4. MK20 Offline Standby Screen & Status
When host middleware is disconnected or not yet launched, the MK20 terminal automatically enters **Snowball Standby Mode**:
- **Top Display (`/dev/fb21`)**: Shows connection status alerts on the left with the 128×128 Snowball puppy icon on the right.
- **Key 10 (`/dev/fb10`)**: Illuminates with the 128×128 Snowball puppy icon while all other key displays are blanked (backlight off) to signal idle standby.
- **Auto Reconnect**: As soon as host middleware starts and UDP sync packets arrive, the display immediately switches to the live active session view.

| Standby Top Display (`/dev/fb21`) | Standby Key 10 (`/dev/fb10`) | Connected Active Display (`/dev/fb21`) |
| :---: | :---: | :---: |
| <img src="assets/screenshots/standby_top_display.png" width="300" alt="Standby Top Display"> | <img src="assets/screenshots/standby_key10.png" width="128" alt="Standby Key 10"> | <img src="assets/screenshots/online_top_display.png" width="300" alt="Connected Top Display"> |

---

## 🚀 Getting Started

Ensure **Node.js >= 22.12** and **Python >= 3.10** are installed. Build Control's shared STT host before building the middleware:

```bash
# 1. Build Control host library
cd Snowball_Control/host
npm ci
npm run build

# 2. Build and launch Middleware Web Supervisor
cd ../../Snowball_Middleware
npm ci
npm run build
npm run start:local
```

Access the Web UI at **`http://127.0.0.1:8765/`** (no login/PIN required on loopback). To launch the full MK20 integration Preview, run `node scripts/start-all.mjs` in `Snowball_Middleware`. This requires real CLI installation/login for each harness, device network configuration, and a local STT model. Discovered models/efforts are dynamically detected without fabricated defaults.

---

## ⌨️ MK20 Hardware Setup

- **Mandatory Modified QMK Firmware**: Due to a hang condition in factory QMK waiting for USB host enumeration on standalone power, flashing the modified QMK firmware is **strictly required**. See [Flashing Guide](hardware/mk20/qmk/README.md).
- **Full SD Image Backup**: Full disk-level microSD backup is strongly recommended prior to modification. Apply the Snowball image and restore the backup if any issue arises. Refer to [Deployment, Backup & Recovery](docs/ARCHITECTURE.md).
- **Wi-Fi Configuration**: Configure site Wi-Fi in `dev-access.conf` on the microSD root (git-ignored). See [Device Tools Guide](hardware/mk20/dev-tools/README.md).
- **Native HUD Daemon**: The HUD is a native C daemon running on Tina Linux ARMv7 (Allwinner T113), utilizing the 428×142 top display (`/dev/fb21`) and 128×128 key displays (`/dev/fb1`..`/dev/fb20`).
- **Local STT**: Supports CUDA or CPU inference. Python dependencies and model weights must be installed separately.

---

## 🧪 Verification & Test Suite

Run the full verification suite across all submodules:

```bash
# Pure hardware plugin contract tests (13 tests)
npm test --prefix plugins/device-mk20

# Linux <-> GD32/QMK serial contract tests (14 tests)
powershell -File hardware/mk20/contract/Test-QmkProtocol.ps1

# Host reference unit tests (52 tests)
npm test --prefix host
```

---

## 📁 Repository Structure

```text
Snowball_Control/
├── assets/                     # Branding, bootloader logo, videos, and screen captures
│   ├── banner.png
│   ├── icon.png
│   ├── bootlogo.bmp            # U-Boot splash logo (160x160 24-bit BMP)
│   ├── mk20-plus.bin           # Top screen boot resource (428x142 RGB565)
│   ├── screenshots/            # Live hardware captures, GIFs & Web UI dashboard
│   │   ├── mk20_navigation_demo.gif
│   │   ├── mk20_ui_elements_test.gif
│   │   ├── web_supervisor_dashboard_en.png
│   │   ├── web_supervisor_dashboard_ko.png
│   │   ├── standby_top_display.png
│   │   ├── standby_key10.png
│   │   └── online_top_display.png
│   └── videos/                 # Full high-res MP4 video recordings
│       ├── mk20_navigation_demo.mp4
│       └── mk20_ui_elements_test.mp4
├── docs/                       # Single Source of Truth architecture specification
│   ├── ARCHITECTURE.md         # English canonical architecture specification (default)
│   └── ARCHITECTURE.ko.md      # Korean architecture specification
├── hardware/mk20/
│   ├── contract/               # Tina Linux <-> QMK hardware serial contract
│   ├── dev-tools/              # MK20 microSD bootstrap, lunch.sh & mk20ctl
│   ├── hud/                    # Native C low-latency HUD engine for Tina Linux
│   └── qmk/                    # Modified QMK firmware source & binaries
├── host/                       # Reference host runtime & STT adapter
├── plugins/device-mk20/        # Isolated lab hardware transport & skin engine
├── LICENSE                     # Apache-2.0 License
├── README.md                   # English project documentation (default)
└── README.ko.md                # Korean project documentation
```

---

## 📄 License & Publication Limits

Snowball custom codebase is licensed under [Apache-2.0](LICENSE). QMK firmware sources and Allwinner T113 BSP code were provided directly by the hardware manufacturer via email. QMK-based code is governed by upstream GPLv2 and vendor notices. Vendor SDKs/BSPs, private network credentials, and full SD card backups are excluded from this repository. Complete architectural specifications, build records, and verification criteria are maintained in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).
