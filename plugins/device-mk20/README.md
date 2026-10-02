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
Renderer enforces 1400 bytes for ordinary previews and at most 4096 bytes for
lab-only rich workspace/changes/list previews. Rich datagram acceptance by the
physical firmware has not been verified. Text is sanitized for the legacy
substring parser; numeric color and index fields are bounded. Sequenced duplicate packets are dropped; absent
sequence remains unverifiable. Extraction used loopback tests. On 2026-09-24,
a Wi-Fi-source-bound physical run at `192.168.1.248:7701` received four HUD
ping replies and a captured `/dev/fb21` screen showed the test title and body.
This verifies the lab preview and return path for that device, but does not
verify authenticated pairing, physical key input or command control. The exact
capture and commands are recorded in the middleware repository at
`docs/middleware/evidence/MW.04.02.01.02/wifi-recovery-20260924.md`.

Production needs a separately reviewed firmware/transport release and real device
acceptance. See PAIRING-CONTRACT.md. Do not set paired=true in a host-side config as
a substitute. Core DeviceRegistry cannot merge USB/LAN by display name/MAC/IP.

## Device Skin & Theme Architecture

The Device Plugin defines extensible `DeviceSkin` themes allowing any MK20 terminal to host **Multiple Skins per Device**.

### Built-in Skins
1. **`slate-dark`** (Default): Modern slate and deep navy with cyan (`#06b6d4`) and emerald (`#10b981`) accents.
2. **`matrix-emerald`**: Phosphor green cyber terminal aesthetic (`#22c55e` accent, `#15803d` borders).
3. **`cyberpunk-neon`**: Synthwave neon palette with hot magenta (`#f43f5e`), electric cyan (`#06b6d4`), and violet cards.
4. **`amber-crt`**: Warm vintage monochrome amber phosphor CRT monitor (`#f59e0b`, `#fbbf24`).
5. **`high-contrast`**: Pure black & white (`#000000` / `#ffffff`) for extreme readability and accessibility.

### Modal View Integration (MK20 Modal Standard)
Skins can be switched directly on the physical hardware terminal using the unified Modal View standard (Col 0 Sidebar + Cols 1..4 Canvas Grid):
- **Enter Settings**: Press **Key 2** (`SYSTEM / Settings`) from Session mode.
- **Top-Left Close (Key 17)**: `labelTop: "MODAL"`, `labelMain: "Settings"`, `labelSub: "Close"`, `isFilled: true`. Closes Settings modal and returns to session.
- **Sidebar Strip (Column 0)**:
  - **Key 18** (`SKIN / Cycle`): Fast shortcut cycling to the next skin.
  - **Key 19** (`RESET / Default`): Fast reset to `slate-dark`.
  - **Key 20** (`ACTIVE / <Theme>`): Shows active theme name and total theme count.
- **16-Key Canvas Grid (Cols 1..4: `[13, 9, 5, 1]`, `[14, 10, 6, 2]`, `[15, 11, 7, 3]`, `[16, 12, 8, 4]`)**:
  - **Keys 13, 9, 5, 1, 14**: 5 built-in skins (`SlateDark`, `Emerald`, `Cyberpunk`, `AmberCRT`, `HiContrast`). The currently active skin tile is illuminated (`isFilled: true`).
  - **Key 10**: Reset Default tile.
  - **Keys 6, 2, 15, 11**: System info tiles (Voice STT, Access level, MK20 Device, Version).
  - Pressing any canvas key directly selects and applies that theme immediately.
- **Left Rotary Knob**:
  - Rotating moves the focused cursor across the 16-key canvas (`isFocused: true`, cyan outline), previewing details in the Top HUD.
  - Clicking (pressing) the knob commits and applies the focused theme.
- **Top HUD Display**: Shows `SETTINGS | <Theme>`, active item index, description, and full color palette previews.

### Design Elements Specification

Each `DeviceSkin` supports comprehensive visual styling properties:

| Design Element | Field | Type / Options | Description |
| :--- | :--- | :--- | :--- |
| **Fonts (KO/EN)** | `fonts.korean`, `fonts.english` | String (`'D2Coding'`) | Open-source SIL OFL font optimized for 128x128 small displays, preventing anti-aliasing blur. |
| **Font Sizes** | `fonts.sizeTitle`, `sizeMain`, `sizeSub`, `sizeDebug` | Integer (10, 14, 10, 9) | Legible type scale for button title pills, main labels, sub-labels, and debug messages. |
| **Button Style** | `buttonStyle` | `{ lineVisible, lineColor, fillVisible, fillColor }` | Controls button border outline and background box fill visibility and RGB colors. |
| **Title Style** | `titleStyle` | `{ lineVisible, lineColor, fillVisible, fillColor, fontColor }` | Styling for the top title pill banner of each button. |
| **Row Backgrounds** | `rowBgColors` | Array of 4 hex colors (`[row0, row1, row2, row3]`) | Group color hierarchy separating physical key matrix rows (0: Top, 3: Bottom). |
| **Individual Button Bg** | `individualButtonBg` | Map of key IDs to hex (e.g. `{ 4: '#3a1021', 20: '#082d21' }`) | Explicit per-key overrides (e.g., K4 Stop, K16 Send, K20 Talk). |
| **Gradient Background** | `gradient` | `{ type, startColor, endColor }` | Background gradient interpolation (`none`, `vertical`, `horizontal`, `LTtoRB`, `RTtoLB`). |
| **Debug Message** | `debug.visible` | Boolean (`false` by default) | When enabled, displays physical coordinate `K%d [R%d:C%d]` on the bottom row. Off for all themes by default. |
| **Font Colors** | `fontColors` | `{ title, main, sub, autoReverse }` | Per-layer typography colors. |
| **Auto Font Reversal** | `fontColors.autoReverse` | Boolean (`true`) | Computes ITU-R BT.601 perceived luminance ($Y = 0.299R + 0.587G + 0.114B$) to dynamically invert font color for maximum contrast and legibility. |

### Button Physical Geometry & Title Layout

The MK20 128x128 key LCD displays physically sit behind rounded square keycaps. The embedded HUD engine renders the button canvas to match the physical bezel:

1. **Rounded Rectangle Geometry (`KEY_CORNER_RADIUS = 6`)**:
   - The button screen is physically a rounded rectangle ($128 \times 128$).
   - Outer button borders (`draw_round_border_16`), focused cursor outlines, active editing indicator borders, and background fills (`draw_round_rect_16`, `draw_gradient_round_rect_16`) conform to a 6-pixel radius curvature using subpixel integer circle math: `(2r - 2x - 1)^2 + (2r - 2y - 1)^2 <= 4r^2`.
   - The 4 corner pixels of row 0 and row 127 are cleanly clipped to black (`0x0000`), matching the hardware bezel.

2. **Full-Width Top Title Layout (`KEY_TITLE_H = 22`)**:
   - The button Title layout spans the entire top area ($x \in [0, 127], y \in [0, 21]$), replacing pill-style badges with a solid top header bar.
   - Rendered via `draw_title_bar_16`: top-left and top-right corners follow the outer 6px corner curvature, while bottom corners are square to meet the button body flush.

3. **Title Line Convention**:
   - Seamless Background Skins: `slate-dark`, `matrix-emerald`, `cyberpunk-neon`, and `amber-crt` configure `titleStyle.lineVisible: false` with `fillVisible: true`, rendering a clean, flush title header without harsh divider lines.
   - High Contrast Skin: `high-contrast` configures `titleStyle.lineVisible: true`, drawing a crisp 1-pixel dividing line (`0xFFFF`) at row 21 for maximum monochromatic accessibility and clear section boundaries.


