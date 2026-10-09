<p align="center">
  <img src="assets/banner.png" alt="Snowball Banner" width="100%">
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
  <a href="https://github.com/fkiller/Snowball_Middleware#one-shot-install"><img src="https://img.shields.io/badge/Install-Snowball-purple.svg" alt="Install Snowball"></a>
</p>

---

<a id="one-shot-install"></a>
## Install Snowball

**Install Snowball once per PC.** The default installation includes the Web UI, MK20 discovery/transport and speech runtime, M5Stack discovery/gateway, and all three Codex/Antigravity/OpenCode harness plugins. No device profile selection or connected USB device is needed.

Run in Windows PowerShell:

```powershell
& ([scriptblock]::Create((irm 'https://raw.githubusercontent.com/fkiller/Snowball_Middleware/main/install.ps1')))
```

The installer prepares Node/Git/Python, builds the companion components, checks the actual isolated harness workers, and starts a hidden per-user background application with a tray icon. The terminal returns after the owned runtime responds. The tray provides Web UI, status, Settings, Pause/Resume, Restart, Quit and login startup. Web UI stays on **http://127.0.0.1:8765/**. Default location: `%LOCALAPPDATA%\Snowball`; **Start-Snowball.ps1** restarts it without downloading dependencies. Missing native harness applications remain unavailable until installed and signed in through their vendors.

For updates, use the same `-InstallRoot` with `-Update` and omit `-Profile`. Old single-device installs gain both adapters while preserving device settings, pairing keys, controller state, speech caches, custom API port and login startup preference. An absent or ambiguous private LAN leaves automatic discovery waiting while the Web UI remains available; use `-Bind PRIVATE_PC_IP` when needed.

MK20 joins Wi-Fi independently: press K17 (Machines), choose a PC and pair/select it. Adding a PC requires no USB/ADB/SD changes. M5Stack's gateway is already installed; its existing security boundary still requires one USB enrollment per unknown PC and firmware 0.3.0 for multiple PCs. Run **Register-M5Stack.ps1** in the install root (optional `-Serial COMx`) to verify existing firmware and prepare USB enrollment using installed tools, without reinstalling, downloading or flashing. Add `-Flash` only for initial firmware installation/upgrade with a full private flash backup. macOS uses **Register-M5Stack.sh** with optional `--serial` / `--flash`. An already running tray is restarted; if stopped, start it with USB connected to complete enrollment. Advanced installer equivalents are `-PrepareM5Stack -NoFlash` and `-PrepareM5Stack`. Normal installation and `-Update` never interrogate or flash USB devices. Installer `-Serial` and `-NoFlash` require `-PrepareM5Stack`. Then disconnect USB and select the registered PC from M5Stack's Machine list. Discovery alone does not enroll an unknown PC.

Options: `-Update`, `-InstallRoot PATH`, `-Bind PRIVATE_PC_IP`, `-Port 8765`, `-NoStart`, `-NoShortcut`. `-Profile web|mk20|m5stack` remains an explicit development/diagnostic subset option, not the normal installation flow; it retains adapters already installed in that root. macOS source users with Node/Git/Python can run `npm run setup --` from a sibling checkout layout; explicit USB preparation uses `--prepare-m5stack [--no-flash]`. Linux suite workers are not yet supported. See the central specification for protocol, authentication and field-verification limits.

[Common architecture and repository ownership](https://github.com/fkiller/Snowball_Control/blob/main/docs/ARCHITECTURE.md)

[Control · MK20](https://github.com/fkiller/Snowball_Control) · [Middleware · Installer / Web UI](https://github.com/fkiller/Snowball_Middleware) · [Device · M5Stack](https://github.com/fkiller/Snowball_Device_M5Stack) · Harness: [Codex](https://github.com/fkiller/Snowball_Harness_Codex), [Antigravity](https://github.com/fkiller/Snowball_Harness_Antigravity), [OpenCode](https://github.com/fkiller/Snowball_Harness_OpenCode)

---

## 🌟 Overview

**Snowball is an inter-Harness local control interface.** It gives developers a common way to navigate and control Codex, AGY, and OpenCode through an MK20 desk terminal and local Web Supervisor. The user chooses the harness, project, and session; each harness keeps its native runtime, history, models, and permissions.

Our philosophy is to keep control in the user's hands and on their computer: make switching tools and following work immediate, discover capabilities from the installed environment, and show real results and limitations. The full [product philosophy](docs/ARCHITECTURE.md#product-philosophy-inter-harness-local-control) is maintained with the architecture.

**Preview:** Intended exclusively for personal workstations and trusted local networks. The initial scope covers OpenAI Codex, Google Antigravity (AGY), OpenCode, MK20 hardware, and local Web UI. Certain security boundaries (such as unencrypted UDP sync and development TCP ADB) are omitted as documented in [Current Architecture and Boundaries](docs/ARCHITECTURE.md).

**Snowball Control** provides the physical terminal runtime, custom QMK firmware, native Tina Linux HUD engine, device tools, and STT reference host for the **MK20 Desktop Terminal**.

Install the complete middleware through the common entry point above. This repository is the MK20 device component; its internal `host/npm start` is a legacy Codex-only reference runtime.

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
When no valid frame is received from the selected middleware, the terminal keeps **Machines (K17)** available for pairing, retry or switching. Selection displays the actual PC name with `Connecting to PC`; eight seconds without a valid frame displays `No PC response`. The recent-advertisement label `available` is separate from `connected`. Ordinary controls wait for the selected PC's accepted frame and scope. A saved selection reconnects when its paired PC becomes available and returns valid frames.

| Disconnected Machines cue (`/dev/fb17`) | Selected PC without a display response (`/dev/fb21`) |
| :---: | :---: |
| <img src="assets/screenshots/mk20_pairing_key17.png" width="128" alt="Actual disconnected Machines key"> | <img src="assets/screenshots/mk20_no_pc_response.png" width="428" alt="Actual selected PC response timeout"> |

These are actual runtime 0.2.1 framebuffers captured on 2026-10-08. The timeout capture documents an unresolved connection, not successful PC pairing/control. The [central specification](docs/ARCHITECTURE.md#change-impact-and-documentation) records cross-component verification and remaining field checks.

---

## ⌨️ MK20 Hardware Setup

- **Mandatory Modified QMK Firmware**: Due to a hang condition in factory QMK waiting for USB host enumeration on standalone power, flashing the modified QMK firmware is **strictly required**. See [Flashing Guide](hardware/mk20/qmk/README.md).
- **Full SD Image Backup**: Full disk-level microSD backup is strongly recommended prior to modification. Apply the Snowball image and restore the backup if any issue arises. Refer to [Deployment, Backup & Recovery](docs/ARCHITECTURE.md).
- **Wi-Fi Configuration**: Configure site Wi-Fi in `dev-access.conf` on the microSD root (git-ignored). See [Device Tools Guide](hardware/mk20/dev-tools/README.md).
- **Native HUD Daemon**: The HUD is a native C daemon running on Tina Linux ARMv7 (Allwinner T113), utilizing the 428×142 top display (`/dev/fb21`) and 128×128 key displays (`/dev/fb1`..`/dev/fb20`).
- **Local STT**: Supports CUDA or CPU inference. The default installation includes the MK20 Python dependencies and the runtime resolves/downloads the local model from hardware capabilities.

---

## M5Stack + FACES hardware demo

[![M5Stack + FACES](https://raw.githubusercontent.com/fkiller/Snowball_Device_M5Stack/main/assets/screenshots/m5stack_navigation_demo.gif)](https://github.com/fkiller/Snowball_Device_M5Stack/blob/main/assets/videos/m5stack_navigation_demo.mp4)

[Device firmware and setup](https://github.com/fkiller/Snowball_Device_M5Stack) · [Full MP4 on GitHub](https://github.com/fkiller/Snowball_Device_M5Stack/blob/main/assets/videos/m5stack_navigation_demo.mp4) · [X](https://x.com/fkiller/status/2106892916149158101?s=20)

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
│   │   ├── mk20_no_pc_response.png
│   │   ├── mk20_pairing_key17.png
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
