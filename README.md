# Snowball Control

Snowball Control is a standalone MK20 control-panel project for AI-assisted
coding workflows. The MK20 acts as a thin network client; coding tools and
source work remain on development machines.

This repository is intentionally separate from the existing Snowball Gateway.
Gateway code is not modified or vendored here.

## Architecture & Specifications

- [`ARCHITECTURE.md`](ARCHITECTURE.md): System architecture, deliverables organization (Firmware vs. Gateway), zero-trust physical pairing, and dual-plane failover.
- [`hardware/mk20/contract/LINUX_QMK_CONTRACT.md`](hardware/mk20/contract/LINUX_QMK_CONTRACT.md): Subsystem boundaries, UART `/dev/ttyS1` framing, VIA protocol, and `KeyboardInfo` binary layout.
- [`hardware/mk20/orchestration/SCHEMA.md`](hardware/mk20/orchestration/SCHEMA.md): Normalized agent orchestration event model across Codex, Claude, and Gemini.
- [`hardware/mk20/dev-tools/NORTH_STARS.md`](hardware/mk20/dev-tools/NORTH_STARS.md): Architectural principles and progress across the four North Star milestones.

## Repository Contents

- `hardware/mk20/dev-tools/`: MK20 discovery, recovery, development-access, and host protocol tools (`mk20ctl.ps1`, `Mk20Protocol.psm1`, `Watch-Mk20Events.ps1`).
- `hardware/mk20/orchestration/`: Host-owned agent orchestration schema and synthetic session player (`SyntheticPlayer.ps1`).
- `hardware/mk20/contract/`: Linux/QMK contract specification and protocol unit tests (`Test-QmkProtocol.ps1`).

The supplied Tina T113 BSP is kept locally under `hardware/mk20/` for source analysis but is excluded from Git because it is a large vendor tree containing nested repositories and generated artifacts.

## Security

Device Wi-Fi credentials and the allowed development-PC MAC address belong in
`hardware/mk20/dev-tools/dev-access.conf`. That file and SD-card backups are
ignored. Copy `dev-access.conf.example` to create a local configuration.

See [the MK20 development-access guide](hardware/mk20/dev-tools/README.md) for
the current USB, TCP ADB, recovery, and rollback procedures.
