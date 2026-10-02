# Cross-Agent Handoff

## Status

**Handoff state:** READY

The repository is ready for either coding agent to continue. The **Device Skin & Theme Architecture**, **Extended Design Elements**, **Button Rounded Rectangle Geometry**, and **Full-Width Top Title Layout** have been fully designed, implemented, deployed, and physically verified across the hardware native C HUD daemon, device plugin, and host controller.

---

## Current Objective

1. **Device Skin Definition**: Define Device Skin inside Device Plugin allowing UI Theme switching per device.
2. **One Device, Multiple Skins Pattern**: Multi-skin registry on each device plugin instance.
3. **Hardware UX Consistency**: Modal View Architecture Standard (Col 0 Sidebar + Cols 1..4 Canvas Grid) on MK20 terminal.
4. **Rich Design Elements**:
   - Fonts (Korean / English): Open-source high-legibility font for small displays (`D2Coding`, SIL OFL).
   - Button Style: `lineVisible`, `lineColor`, `fillVisible`, `fillColor`.
   - Button Title Style: `lineVisible`, `lineColor`, `fillVisible`, `fillColor`, `fontColor`.
   - Button Row background Color Group (Rows 0..3).
   - Individual Button background Color overrides (e.g. K4 Stop, K16 Send, K20 Talk).
   - Gradation support for button background color (`none`, `vertical`, `horizontal`, `LTtoRB`, `RTtoLB`).
   - Button Bottom Message = Debug, Debug visible (`false` / Off for all themes by default).
   - Button Font colors (title, main, sub).
   - Automatic Font Color reversed by background (ITU-R BT.601 perceived luminance contrast calculation).
   - Tastefully blended into all 5 built-in themes.
5. **Physical Button Rounded Geometry & Full-Width Title Banner**:
   - Physical button LCD ($128 \times 128$) is a rounded rectangle ($r = 10\text{px}$).
   - Outer button outlines, active editing indicator borders, focused cursor outlines, and background fills conform to this 10px curvature.
   - Title layout spans the entire top width ($x \in [0, 127], y \in [0, 21]$), with top corners rounded to follow keycap curvature ($r = 10\text{px}$).
   - Title line is disabled (`titleStyle.lineVisible: false`) across modern skins (`slate-dark`, `matrix-emerald`, `cyberpunk-neon`, `amber-crt`) leaving a seamless background fill, while retained (`titleStyle.lineVisible: true`) on `high-contrast` for crisp monochrome separation.

---

## Completed Work

1. **Open-Source Font Selection (`D2Coding`)**:
   - Selected **D2Coding** (SIL Open Font License by Naver), already pre-installed on the device at `/usr/share/fonts/D2Coding.ttf` (4.18 MB) and `/mnt/SDCARD/fonts/D2Coding.ttf`.
   - Specifically engineered for terminal code displays with pixel-clear readability at 9~14px for English and Korean glyphs on 128x128 key LCD displays.

2. **Native Embedded C Engine (`hardware/mk20/hud/`)**:
   - [`gfx_prims.h`](file:///hardware/mk20/hud/gfx_prims.h):
     - Added `#define KEY_CORNER_RADIUS 10` and `#define KEY_TITLE_H 22`.
     - `V2_GradientType` enum (`GRADIENT_NONE`, `GRADIENT_VERTICAL`, `GRADIENT_HORIZONTAL`, `GRADIENT_LT_TO_RB`, `GRADIENT_RT_TO_LB`).
     - Inline `interpolate_rgb565(c0, c1, t, max_t)`.
     - Inline `get_rgb565_luminance(c)` and `get_contrast_font_color(bg, default_fg, auto_reverse)`.
     - Declared `draw_round_rect_16`, `draw_round_border_16`, `draw_gradient_round_rect_16`, `draw_title_bar_16`.
   - [`mk20-hud.c`](file:///hardware/mk20/hud/mk20-hud.c):
     - Implemented `draw_round_rect_16(...)`, `draw_round_border_16(...)`, `draw_gradient_round_rect_16(...)`, and `draw_title_bar_16(...)` with pure integer subpixel circle arithmetic: `(2r - 2x - 1)^2 + (2r - 2y - 1)^2 <= 4r^2`.
   - [`v2_state.h`](file:///hardware/mk20/hud/v2_state.h):
     - Extended `V2_Theme` with button style, title style, typography colors, row background groups, individual button background overrides (`individual_button_bg[21]`), gradient parameters, and `debug_visible`.
     - Added physical matrix coordinate helpers: `v2_get_key_row(key_idx)` and `v2_get_key_col(key_idx)`.
   - [`v2_state.c`](file:///hardware/mk20/hud/v2_state.c):
     - Populated all 5 built-in themes with exact 16-bit RGB565 configurations matching the theme design elements.
     - Configured `title_line_visible = 0` for `slate-dark`, `matrix-emerald`, `cyberpunk-neon`, `amber-crt`, and `title_line_visible = 1` for `high-contrast`.
     - Updated `v2_parse_sync_packet()` to support runtime JSON overrides (`debug`, `lineVisible`, `fillVisible`, `autoFontReverse`).
   - [`v2_render.c`](file:///hardware/mk20/hud/v2_render.c):
     - Full rounded key rendering pipeline:
       - Background: `draw_gradient_round_rect_16` or `draw_round_rect_16` with $r = 10\text{px}$.
       - Title banner: `draw_title_bar_16` covering full top width ($x \in [0, 127], y \in [0, 21]$) with top rounded corners ($r = 10\text{px}$) and bottom square corners flush with body.
       - Title divider line conditionally drawn at $y=21$ only when `title_line_visible` is enabled (`high-contrast`).
       - Outer button border: `draw_round_border_16` following rounded perimeter.
       - Active editing and focused outlines: Inset rounded borders following corner curvature.
       - Auto contrast inverted labels and optional debug coordinates.
   - Built with WSL OpenWrt ARM GCC (`73,952 bytes`), pushed via ADB to `/mnt/SDCARD/mk20-hud`, running as daemon (PID 2524).

3. **Device Plugin Architecture (`plugins/device-mk20/`)**:
   - [`plugins/device-mk20/src/skin.mjs`](file:///plugins/device-mk20/src/skin.mjs):
     - Extended `DeviceSkin` with `fonts`, `buttonStyle`, `titleStyle`, `rowBgColors`, `individualButtonBg`, `gradient`, `debug`, and `fontColors`.
     - Configured `lineVisible: false` for `slate-dark`, `matrix-emerald`, `cyberpunk-neon`, `amber-crt`, and `lineVisible: true` for `high-contrast`.
     - Added `getLuminance(color)`, `getContrastFontColor(bg, defaultFg, autoReverse)`.
     - Added `getButtonBackground(keyId)` and `getReadableFontColor(bg, defaultFg)`.
     - Extended `toWireTokens()` and `toJSON()`.
     - Populated `BUILTIN_MK20_SKINS` across all 5 built-in themes.
   - [`plugins/device-mk20/tests/skin.test.mjs`](file:///plugins/device-mk20/tests/skin.test.mjs):
     - Unit tests for luminance calculation, contrast auto reversal, design element schema completeness, wire tokens, and button background lookups.
     - **13/13 PASS** (`npm test --prefix plugins/device-mk20`).

4. **Host Controller & Integration**:
   - `host/tests/`: **49/49 PASS** (`node --test host/tests`).
   - Full combined test suite: **62/62 PASS** across all modules.
   - Mirrored all plugin files and hardware files to `E:\developments\projects\Snowball_Control\`.

5. **Secondary Font Contrast & Color Enhancements**:
   - Fixed low visibility in `cyberpunk-neon` where Machine (Key 17) and Harness (Key 13) unselected items and bottom messages clashed against hot pink / saturated filled backgrounds.
   - Rewrote dynamic contrast algorithm (`get_contrast_font_color` in `gfx_prims.h` and `getContrastFontColor` in `skin.mjs`): checks luminance delta $\Delta L = |L_{bg} - L_{fg}|$. If $\Delta L \ge 75$, theme font color is preserved; if $\Delta L < 75$, inverts to pure white ($L_{bg} < 135$) or black ($L_{bg} \ge 135$).
   - Updated `v2_render.c` multi-item list mode so `k->sub` evaluates filled background text override (`key_filled_text` vs `btn_sub_font_color`) before contrast calculation.
   - Overhauled secondary font styles across all themes with luminous tones:
     - `cyberpunk-neon`: `#c4b5fd` (0xC5BF, Luminous Neon Lilac, $L = 195$)
     - `matrix-emerald`: `#4ade80` (0x4EF0, Phosphor Light Green, $L = 188$)
     - `slate-dark`: `#94a3b8` (0x9537, Cool Slate Mist, $L = 160$)
     - `amber-crt`: `#fbbf24` (0xFDF4, Phosphor Amber, $L = 191$)

6. **Physical Hardware Framebuffer Verification**:
   - Top HUD `/dev/fb21`: Dynamic palette updates confirmed via raw dump inspection.
   - Key LCD `/dev/fb1`:
     - Row 0 / Row 127: 4 pixels black corner cutout (`00 00 ...`), border line, 4 pixels black cutout.
     - Row 1: 2 pixels black cutout, 2 border pixels, full-width title fill, 2 border pixels, 2 black cutout.
     - Row 20-21: Clean title background fill without harsh line for `amber-crt` / `slate-dark`.
     - Row 21 under `high-contrast`: Full white dividing line `ff ff ff ff ...` across the entire row.
     - Row 126-127: Rounded bottom corners verified with symmetric black bezel cutout.
   - Multi-Item LCDs `/dev/fb13` (Harness) and `/dev/fb17` (Machine):
     - Row 54 (unselected item) and Row 106 (`k->sub` label): Verified via hexdump that text colors render as crisp white (`ff ff` / 0xFFFF) on filled backgrounds and luminous lilac (`c4b5fd` / 0xC5BF) on dark card backgrounds, completely eliminating the previous low-contrast color collision.

---

## Files Changed

| File | Purpose |
|---|---|
| `hardware/mk20/hud/gfx_prims.h` | Corner radius macro (10px), title height macro, rounded drawing declarations, dynamic contrast delta calculation ($\Delta L \ge 75$) |
| `hardware/mk20/hud/mk20-hud.c` | Subpixel integer circle routines (`draw_round_rect_16`, `draw_round_border_16`, `draw_gradient_round_rect_16`, `draw_title_bar_16`) |
| `hardware/mk20/hud/v2_state.h` | Extended `V2_Theme` struct, key coordinate macros |
| `hardware/mk20/hud/v2_state.c` | Theme palettes (`title_line_visible` set to 0 for dark themes, 1 for high contrast; secondary font colors set to luminous tones) |
| `hardware/mk20/hud/v2_render.c` | Key rendering pipeline with rounded background, full-width title bar, rounded borders, and multi-item list contrast reversal |
| `plugins/device-mk20/src/skin.mjs` | Extended `DeviceSkin`, `BUILTIN_MK20_SKINS` updated with luminous secondary colors and $\Delta L \ge 75$ contrast logic |
| `plugins/device-mk20/tests/skin.test.mjs` | Extended unit tests for design elements, luminance, and contrast |
| `plugins/device-mk20/README.md` | Documented design elements specification and physical button geometry |
| `plugins/README.md` | Standard Device Plugin specification updated with design elements schema and button geometry standard |
| `ARCHITECTURE.md` | System architecture updated with Device Plugin and Modal View standards |
| `HANDOFF.md` | Source of truth and cross-agent documentation |

---

## Tests and Verification

- `npm test --prefix plugins/device-mk20`: **13/13 PASS**
- `node --test host/tests`: **49/49 PASS**
- Total test suite: **62/62 PASS**
- Physical framebuffer verification:
  - Top HUD `/dev/fb21`: Palette updates verified via ADB raw dumps.
  - Key LCD `/dev/fb1`: Rounded corner bezel cutouts and full-width top title verified via byte-level hexdump.
  - Key LCD `/dev/fb13`, `/dev/fb17`: Multi-item list contrast and luminous secondary text verified on physical device.

---

## Git State

- **Branch:** `device_skin_plugin`
- **Tracked modified files:**
  - `hardware/mk20/hud/gfx_prims.h`
  - `hardware/mk20/hud/mk20-hud.c`
  - `hardware/mk20/hud/v2_render.c`
  - `hardware/mk20/hud/v2_state.c`
  - `hardware/mk20/hud/v2_state.h`
  - `plugins/README.md`
  - `plugins/device-mk20/README.md`
  - `plugins/device-mk20/src/skin.mjs`
  - `plugins/device-mk20/tests/skin.test.mjs`
  - `HANDOFF.md`

---

## Exact Next Action

All requested tasks are complete, compiled with OpenWrt GCC, deployed to MK20 physical hardware, and verified via framebuffer byte inspection. The system is operating normally with 100% test coverage (62/62).
