<p align="center">
  <img src="assets/banner.png" alt="Snowball Control Banner" width="100%">
</p>

<h1 align="center">
  <img src="assets/icon.png" width="48" height="48" valign="middle" alt="Snowball Icon">
  Snowball Control — Preview
</h1>

<p align="center">
  <strong>Physical Terminal Firmware, Tina Linux HUD & Device Integration for AI Coding Agents</strong>
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

**Preview:** Intended exclusively for personal workstations and trusted local networks. The initial scope covers OpenAI Codex, Google Antigravity (AGY), OpenCode, MK20 hardware, and local Web UI. Certain security boundaries (such as unencrypted UDP sync and development TCP ADB) are omitted as documented in [Current Architecture and Boundaries](docs/ARCHITECTURE.md).

**Snowball Control** provides the physical terminal runtime, custom QMK firmware, native Tina Linux HUD engine, device tools, and STT reference host for the **MK20 Desktop Terminal**.

The complete middleware and Web Supervisor are hosted in the companion repository:  
👉 **[`Snowball_Middleware`](https://github.com/fkiller/Snowball_Middleware)** (place both repositories in the same parent directory). Control's internal `host/npm start` is a legacy Codex-only reference runtime.

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

## 📺 MK20 Standby Screen & Status

When host middleware is disconnected or not yet launched, the MK20 terminal automatically enters **Snowball Standby Mode**:
- **Top Display (`/dev/fb21`)**: Shows connection status alerts on the left with the 128×128 Snowball puppy icon on the right.
- **Key 10 (`/dev/fb10`)**: Illuminates with the 128×128 Snowball puppy icon while all other key displays are blanked (backlight off) to signal idle standby.
- **Auto Reconnect**: As soon as host middleware starts and UDP sync packets arrive, the display immediately switches to the live active session view.

| Standby Top Display (`/dev/fb21`) | Standby Key 10 (`/dev/fb10`) | Connected Active Display (`/dev/fb21`) |
| :---: | :---: | :---: |
| <img src="assets/screenshots/standby_top_display.png" width="300" alt="Standby Top Display"> | <img src="assets/screenshots/standby_key10.png" width="128" alt="Standby Key 10"> | <img src="assets/screenshots/online_top_display.png" width="300" alt="Connected Top Display"> |

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
├── assets/                     # Branding, bootloader logo, and screen captures
│   ├── banner.png
│   ├── icon.png
│   ├── bootlogo.bmp            # U-Boot splash logo (160x160 24-bit BMP)
│   ├── mk20-plus.bin           # Top screen boot resource (428x142 RGB565)
│   └── screenshots/            # Live hardware captures (standby & online)
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
