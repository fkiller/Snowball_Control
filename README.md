# Snowball Control

Snowball Control is a standalone MK20 control-panel project for AI-assisted
coding workflows. The MK20 acts as a thin network client; coding tools and
source work remain on development machines.

This repository is intentionally separate from the existing Snowball Gateway.
Gateway code is not modified or vendored here.

## Repository contents

- `hardware/mk20/dev-tools/`: MK20 discovery, recovery, and development-access
  tools.
- `hardware/mk20/dev-tools/NORTH_STARS.md`: current architecture principles and
  incremental implementation direction.

The supplied Tina T113 BSP is kept locally under `hardware/mk20/` for source
analysis but is excluded from Git because it is a large vendor tree containing
nested repositories and generated artifacts.

## Security

Device Wi-Fi credentials and the allowed development-PC MAC address belong in
`hardware/mk20/dev-tools/dev-access.conf`. That file and SD-card backups are
ignored. Copy `dev-access.conf.example` to create a local configuration.

See [the MK20 development-access guide](hardware/mk20/dev-tools/README.md) for
the current USB, TCP ADB, recovery, and rollback procedures.
