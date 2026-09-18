# MK20 lab adapter — separate hardware repository

This package extracts MK20-specific key/knob translation, display rendering and
explicit UDP transport from the prototype boundary. It does not import middleware
core, harnesses, ADB, firmware tools or the old global ContextManager. No transport
starts on import. Run `npm test --prefix plugins/device-mk20` for pure/loopback tests.

Current firmware source (`hardware/mk20/hud/mk20-hud.c`) sends raw JSON key/knob
packets, frequently without sequence, and updates g_host_addr from received UDP.
There is no authenticated pairing handshake in that path. Therefore compatibility
is always lab_only/unpaired/control=false. IP pinning is an accident-prevention
measure, not authentication or replay protection. Legacy input is labelled
untrusted_lab and must never be admitted to CommandJournal as device authorization.

The lab transport requires an explicit labEnabled flag, exact bind address/peer,
and explicit start. It uses an ephemeral local port by default and never opens or
changes the existing 7701 daemon. Only explicit preview sends display data; incoming
packets cannot change its peer. It never mirrors a display packet to multiple peers.
Renderer enforces the 1400-byte cap and sanitizes legacy substring-parser delimiters.
This intentionally excludes unsupported rich-list rendering rather than claiming
complete display compatibility. Sequenced duplicate packets are dropped; absent
sequence remains unverifiable. No real MK20 was contacted during extraction.

Production needs a separately reviewed firmware/transport release and real device
acceptance. See PAIRING-CONTRACT.md. Do not set paired=true in a host-side config as
a substitute. Core DeviceRegistry cannot merge USB/LAN by display name/MAC/IP.
