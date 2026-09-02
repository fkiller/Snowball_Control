# MK20 / Snowball North Stars

## 1. Two independent control planes

Keep product traffic on USB CDC and development traffic on authenticated or
strictly firewalled TCP. Neither plane may reset or silently take ownership of
the other.

Milestones:

1. Reboot-test the current COM + TCP ADB topology.
2. Build `mk20ctl doctor` to verify USB, Wi-Fi, ADB, process, and firewall state.
3. Add explicit `dev-mode` and `restore` commands with guarded rollback.
4. Evaluate a deliberate `serial,adb` composite gadget only after endpoint and
   host-driver tests.

## 2. Reproducible and recoverable device operations

Every device mutation should have a preflight check, checksum, log entry, and
tested inverse operation. SD-card boot hooks are the recovery boundary; system
partition replacement is not required for normal development.

Milestones:

1. Turn the proven A1 protocol into a host library.
2. Support chunked Base64 `saveToFile` with response and checksum validation.
3. Capture a read-only device inventory and configuration snapshot.
4. Add a power-loss and interrupted-transfer recovery test.

## 3. Host-owned agent orchestration

The T113 should render state and emit user actions; the Windows/Snowball host
should own Codex, Claude, and Gemini sessions. Provider-specific APIs remain
behind normalized adapters.

Milestones:

1. Normalize machine, project, session, turn, activity, approval, and result.
2. Probe Codex app-server, Claude Agent SDK, and Gemini ACP event shapes.
3. Stream a synthetic session to the MK20 without invoking a live provider.
4. Add approval, cancel, retry, and resume actions from physical keys.

## 4. Explicit Linux/QMK ownership

Treat Tina Linux as the display/network/application owner and GD32/QMK as the
low-latency key/HID owner. Version and test the serial contract between them so
either side can be upgraded independently.

Milestones:

1. Document message framing, version negotiation, and failure behavior.
2. Record key, encoder, display, audio, Wi-Fi, and update responsibilities.
3. Build protocol fixtures and replay tests on the host.
4. Add compatibility gates before deploying either firmware.

## Smallest next implementation

Create a host-side `mk20ctl` with five commands:

- `doctor`: discover COM and TCP ADB, then report one health summary.
- `info`: call A1 `getInfo` and collect read-only Linux identity.
- `put`: chunk, Base64-encode, upload, and verify a file.
- `shell`: open the network ADB root shell.
- `restore`: validate backups and restore factory USB behavior.

This converts the successful recovery session into a repeatable development
workflow before UI or multi-agent features are added.
