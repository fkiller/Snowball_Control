# Snowball Device Plugins Specification

This directory contains hardware device plugins for the Snowball Control ecosystem. Each plugin encapsulates the device-specific transport, key/knob mapping, display frame rendering, and theme/skin management for a specific physical or virtual device (e.g., MK20, MK10, StreamDeck).

---

## 1. Device Plugin Architecture

Every Device Plugin must follow the **One Device, Multiple Skins** model and implement the standard Device Skin Contract:

```text
Host Controller (MvpController / ContextManager)
       │
       ▼
┌────────────────────────────────────────────────────────┐
│ Device Plugin (e.g., plugins/device-mk20)              │
│                                                        │
│  ┌──────────────────────┐    ┌──────────────────────┐  │
│  │ DeviceSkinManager    │    │ Transport & Protocol │  │
│  │  - listSkins()       │    │  - UDP / USB CDC A1  │  │
│  │  - getActiveSkin()   │    │  - v2_sync datagrams │  │
│  │  - setActiveSkin()   │    │  - input decoding    │  │
│  │  - cycleSkin()       │    └──────────────────────┘  │
│  │  - registerSkin()    │                              │
│  │  - unregisterSkin()  │    ┌──────────────────────┐  │
│  └──────────┬───────────┘    │ UX Controller        │  │
│             │                │  - Key 2: Settings   │  │
│             ▼                │  - Key 18: Cycle     │  │
│  ┌──────────────────────┐    │  - Row 2: Selectors  │  │
│  │ DeviceSkin (Theme)   │    │  - Knob: Scroll/Pick │  │
│  │  - Top HUD colors    │    └──────────────────────┘  │
│  │  - Key LCD colors    │                              │
│  │  - 6-slot Palette    │                              │
│  └──────────────────────┘                              │
└────────────────────────────────────────────────────────┘
```

---

## 2. DeviceSkin Schema & Specification

A `DeviceSkin` defines the complete visual appearance for a device. Colors are authored as standard CSS Hex (`#RRGGBB` or `#RGB`) and converted to 16-bit RGB565 (`0x0000..0xFFFF`) for embedded hardware framebuffers.

### Theme Schema

```json
{
  "id": "theme-identifier",
  "name": "Human-Readable Name",
  "description": "Short description of the theme aesthetic",
  "version": "1.0.0",
  "isBuiltin": false,
  "theme": {
    "top": {
      "background": "#0F172A",
      "card": "#1E293B",
      "border": "#334155",
      "text": "#FFFFFF",
      "textDim": "#94A3B8",
      "accent": "#06B6D4",
      "status": {
        "idle": "#64748B",
        "active": "#06B6D4",
        "success": "#10B981",
        "warning": "#F59E0B",
        "error": "#F43F5E"
      }
    },
    "keys": {
      "defaultBg": "#1E293B",
      "defaultBorder": "#334155",
      "defaultText": "#FFFFFF",
      "defaultSubText": "#94A3B8",
      "filledBg": "#06B6D4",
      "filledText": "#FFFFFF",
      "focusedBorder": "#06B6D4",
      "focusedBg": "#1E293B",
      "editingBorder": "#F59E0B",
      "editingBg": "#1E293B",
      "disabledBg": "#0F172A",
      "disabledText": "#475569"
    },
    "palette": [
      "#FFFFFF",
      "#06B6D4",
      "#10B981",
      "#F59E0B",
      "#F43F5E",
      "#94A3B8"
    ]
  },
  "fonts": {
    "korean": "D2Coding",
    "english": "D2Coding",
    "sizeTitle": 10,
    "sizeMain": 14,
    "sizeSub": 10,
    "sizeDebug": 9
  },
  "buttonStyle": {
    "lineVisible": true,
    "lineColor": "#334155",
    "fillVisible": true,
    "fillColor": "#1E293B"
  },
  "titleStyle": {
    "lineVisible": true,
    "lineColor": "#334155",
    "fillVisible": true,
    "fillColor": "#1E293B",
    "fontColor": "#06B6D4"
  },
  "rowBgColors": ["#141E2E", "#121B2A", "#0D1522", "#090E17"],
  "individualButtonBg": {
    "4": "#3A1021",
    "16": "#083563",
    "20": "#082D21"
  },
  "gradient": {
    "type": "vertical",
    "startColor": "#192031",
    "endColor": "#081019"
  },
  "debug": {
    "visible": false
  },
  "fontColors": {
    "title": "#06B6D4",
    "main": "#E2E8F0",
    "sub": "#64748B",
    "autoReverse": true
  }
}
```

### Constraints & Validation Rules
1. **`id`**: Lowercase alphanumeric string matching `^[a-z0-9][a-z0-9-_]{1,31}$`.
2. **`name`**: Non-empty string $\le 40$ characters.
3. **`description`**: String $\le 128$ characters.
4. **`palette`**: Exactly 6 distinct valid color strings.
5. **Fail-Closed Protection**:
   - Built-in skins (`isBuiltin: true`) cannot be unregistered (`builtin_skin_protected`).
   - The currently active skin cannot be unregistered while active (`active_skin_in_use`).
   - Requesting a non-existent skin ID throws `skin_not_found`.

### 2.1 Button Physical Geometry & Title Layout Standard

Hardware key displays (e.g. MK20 128x128 LCDs) feature rounded physical keycaps:
1. **Rounded Canvas Clipping**: Outer button borders, focused/editing outlines, and background fills should clip to the physical corner radius (typically $r \approx 5 \sim 6\text{px}$) to prevent corner clipping against hardware bezels.
2. **Full-Width Top Title Layout**: Title headers span the entire top width of the key ($x \in [0, \text{Width}-1], y \in [0, \text{TitleHeight}-1]$), with top corners rounded to follow keycap curvature and bottom corners square.
3. **Title Line Convention**: Themes default to `titleStyle.lineVisible: false` with only background fill active for a modern borderless look. High-contrast accessibility themes may set `titleStyle.lineVisible: true` to provide a sharp dividing boundary.

---

## 3. Device Plugin Interface Contract

When authoring a new device plugin (e.g. `plugins/device-mk10`), the plugin module must provide:

### 3.1 Skin Management Methods
Every device transport/adapter must expose:
- `listSkins(): Array<{ id: string, name: string, description: string, isBuiltin: boolean, active: boolean }>`
- `getActiveSkin(): DeviceSkin`
- `setActiveSkin(id: string): { previousSkinId: string, activeSkin: DeviceSkin }`
- `cycleSkin(delta?: number): DeviceSkin`
- `registerSkin(skin: DeviceSkin | object): void`
- `unregisterSkin(id: string): void`

### 3.2 Event Emission
The plugin transport must emit an event whenever the active theme changes:
- Event: `'lab.skin'` or `'device.skin'`
- Payload: `{ skinId: string, skinName: string, skin: DeviceSkin }`

### 3.3 Wire Datagram Synchronization
Outgoing sync datagrams (e.g., `v2_sync` UDP or CDC A1 RPC frames) must carry:
- `skinId`: Bounded string ($\le 31$ bytes)
- `skinName`: Human-readable label ($\le 40$ bytes)
- (Optional rich datagrams): 16-bit RGB565 wire tokens via `skin.toWireTokens()`

---

## 4. Hardware UX Interaction Standard (MK20 Modal View Architecture)

Device plugins must adhere to the **MK20 Modal View Standard** (Col 0 Sidebar + Cols 1..4 16-Key Canvas Grid), maintaining complete UX consistency with `Files` and `Changes`:

1. **Modal Origin & Close (Key 17 - Top-Left)**:
   - `labelTop: "MODAL"` (or active index), `labelMain: "Settings"`, `labelSub: "Close"`, `isFilled: true`.
   - Pressing Key 17 unconditionally closes the modal and returns to Session view.
2. **Modal Sidebar Strip (Column 0: Keys 17, 18, 19, 20)**:
   - **Key 17**: Modal Exit button (`Close`).
   - **Key 18**: `SKIN / Cycle` — quick shortcut advancing to the next skin.
   - **Key 19**: `RESET / Default` — resets to `slate-dark`.
   - **Key 20**: `ACTIVE / Status` — displays current active theme and count.
3. **16-Key Interactive Canvas Grid (Cols 1..4: `[13, 9, 5, 1]`, `[14, 10, 6, 2]`, `[15, 11, 7, 3]`, `[16, 12, 8, 4]`)**:
   - Primary 4x4 tile workspace:
     - **Keys 13, 9, 5, 1, 14**: Built-in Themes 1..5 (`slate-dark`, `matrix-emerald`, `cyberpunk-neon`, `amber-crt`, `high-contrast`). The active theme tile is filled (`isFilled: true`).
     - **Key 10**: Reset Default tile.
     - **Keys 6, 2, 15, 11**: System, Voice (Whisper), and Access status tiles.
4. **Left Rotary Knob Interaction**:
   - **Turn**: Moves the highlighted cursor across the 4x4 canvas grid tiles (`isFocused: true`, cyan outline), dynamically previewing the item in the Top HUD display.
   - **Click (Press)**: Applies the currently focused theme or selects the tile.
5. **Direct Canvas Keypress**:
   - Pressing any key within the 16-key canvas directly selects and applies that theme immediately.

---

## 5. Existing Device Implementations

- [`plugins/device-mk20`](./device-mk20/README.md): Reference implementation for the MK20 20-key hardware terminal with 5 built-in themes (`slate-dark`, `matrix-emerald`, `cyberpunk-neon`, `amber-crt`, `high-contrast`).
