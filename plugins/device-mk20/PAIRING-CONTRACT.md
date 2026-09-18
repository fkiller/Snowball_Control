# Required authenticated firmware contract — NOT implemented on the device

MW.04.02.01.02 is blocked for production acceptance. Legacy UDP/ADB cannot satisfy it.
This document is a versioned compatibility requirement, not a new wire protocol
silently applied to existing firmware.

1. Firmware must implement a standard authenticated encrypted transport using a
   reviewed library (e.g. mutually authenticated TLS), a persistent device key and
   explicit protocol/capability negotiation. The device, not its IP, proves identity.
2. Enrollment starts only after explicit local user intent and physical device
   confirmation. Bind the device and host key fingerprints, roles, protocol version
   and fresh session challenge to that confirmation. Comparing a short code must
   use a reviewed authenticated key-exchange design, not a homemade hash/password
   protocol. No production key-exchange choice is approved by this document alone.
3. States: unpaired → awaiting_physical_confirmation → paired; cancellation,
   expiration, wrong identity/version or attempt limits return unpaired. A grant
   is single-use and cannot survive timeout/restart as an implicit approval.
4. Control frames require authenticated session identity, bounded monotonic sequence,
   operation/capability scope and current lease. Stale/replayed frames are rejected.
   Host/device restart revalidates the key and never replays buffered physical input.
5. Revoke/forget invalidates credentials and active handles on both peers; IP change
   cannot re-enroll a revoked key. Replacement firmware/key requires explicit repair.
6. USB and LAN observations merge only after independent proof that both reference
   the same persistent device key. Serial/name/VID/PID/IP agreement is insufficient.

Acceptance on actual MK20: success, cancel, timeout, wrong key/code, replay,
firmware mismatch, reboot, IP change, revoke and USB/LAN same-key verification.
Until that firmware exists and is exercised, requireProductionControl always fails;
the lab parser/display fixture cannot pass the real pairing acceptance gate.
