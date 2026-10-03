# MK20 Architecture: Tina Linux & GD32/QMK Serial Contract

This specification establishes the architectural ownership, interfaces, and protocol contract between **Tina Linux (Allwinner T113)** and the **Key/HID MCU (GD32/STM32 running QMK)** as mandated by North Star 4.

---

## 1. Subsystem Ownership Boundary

| Subsystem | Owner | Hardware Interface | Driver / Daemon |
|---|---|---|---|
| **Mechanical Key Matrix (20 keys)** | GD32 / QMK | Direct GPIO matrix | QMK Matrix Scan |
| **Rotary Encoder / Dial** | GD32 & T113 ADC | GPIO / GPADC (`sunxi-gpadc0`) | `/dev/input/event1` |
| **Dynamic Keymaps & Macros** | GD32 / QMK | Internal Flash / EEPROM | VIA Protocol Engine |
| **Display Rendering (640x656 LCD)** | Tina Linux | Display Engine 2.0 / LCD bus | `/dev/fb0`, Qt `linuxfb` |
| **USB CDC Gadget (Host Link)** | Tina Linux | USB OTG (Gadget ConfigFS) | `/dev/ttyGS0` (`COM5` on PC) |
| **Wi-Fi Connectivity (802.11n)** | Tina Linux | USB Host / RTL8188GU | `wlan0`, `wpa_supplicant` |
| **Audio Playback / Buzzer** | Tina Linux | Sunxi Audio Codec | ALSA (`hw:0,0`) |
| **Network Development Shell** | Tina Linux | TCP Wi-Fi (port 5555) | `adbd` (MAC firewalled) |
| **Agent Session HUD & UI** | Tina Linux | Application Layer | `/data/KeyboardDevice` |

---

## 2. Linux <-> GD32 Serial Interface

- **Physical Bus**: Internal UART `/dev/ttyS1`
- **Baud Rate**: 115200 bps, 8 data bits, no parity, 1 stop bit (8N1)
- **Lock File**: `/tmp/lock/LCK..ttyS1`
- **Linux Handler**: `STM32CommunicationObject` (inside `/data/KeyboardDevice`)

---

## 3. Wire Protocol & Command Framing

The interface operates using framed VIA / QMK packets.

### 3.1 Packet Framing Structure

```text
+-------------------+------------------+-------------------+--------------------+-------------------+
| Start Byte (0xA5) | Command ID (1 B) | Payload Len (1 B) | Payload Data (N B) | Checksum (1 B)    |
+-------------------+------------------+-------------------+--------------------+-------------------+
```

- **Start Byte**: `0xA5`
- **Command ID**: 1 byte specifying the VIA / custom action.
- **Payload Length**: Length of data following.
- **Payload Data**: Raw bytes.
- **Checksum**: Byte sum of header + payload modulo 256.

### 3.2 Key State Escalation Event (`GD32 -> T113`)

When a key changes state, GD32 transmits a key state event:

```text
Signal: STM32CommunicationObject::keyStateChanged(layer, row, col, state, keyCode)
```

- `layer` (uint8): Active keymap layer (0..3).
- `row` (uint8): Physical switch matrix row (0..3).
- `col` (uint8): Physical switch matrix column (0..4).
- `state` (uint8): `1` for pressed, `0` for released.
- `keyCode` (uint8): Low byte of QMK keycode.

Upon receiving this signal:
1. `KeyboardDevice` updates its internal key state and UI graphic widgets.
2. If configured for host notification, `SerialPort::device_keyState_Changed` emits an A1 proactive escalation message over `/dev/ttyGS0` to the host.

### 3.3 Dynamic Keymap Configuration (`T113 -> GD32`)

Linux can reprogram the physical key definitions on GD32 at runtime using VIA commands:

- `dynamic_keymap_set_keycode(layer, row, col, keycode)`: Updates a single key binding.
- `dynamic_keymap_set_keycodes(QMap<QPair<int,int>, uint16>)`: Batch updates key bindings.
- `dynamic_keymap_macro_set_buffer(buffer)`: Writes key macro definitions to EEPROM.
- `dynamic_keymap_macro_reset()`: Clears all stored key macros.

### 3.4 Hardware Management Commands

- `request_bootloader_jump()`: Signals GD32 to enter DFU bootloader mode for firmware updates.
- `request_upload_key(layer, keycode)`: Requests current keycode binding from GD32.
- `request_mouse_control(buffer, length)`: Sends relative cursor movement / mouse clicks.

### 3.5 Dual-Knob Chord & Dynamic PC Key Masking (HOST ↔ HID Mode)

1. **Knob Button Escalation**:
   - Left knob button (pin `B5`, row `100`) and Right knob button (pin `B4`, row `103`) are unconditionally unbound to HID (`KC_NO = 0x0000`) across all layers by `mk20-hud`.
   - Press and release events are transmitted to Tina Linux as `0x16` key state events.
2. **Chord Detection**:
   - Simultaneous press of both knobs (`g_left_down && g_right_down`) triggers the `g_pc_keys_on` toggle in `mk20-hud`.
3. **Mode Operation**:
   - **HOST Mode (`g_pc_keys_on = 0`, default)**: All 20 matrix keys are written with `0x0000 (KC_NO)` via VIA command `0x05`. QMK sends key state events over UART to Tina Linux for HUD / Snowball control, but suppresses USB HID keystrokes to the PC.
   - **HID Mode (`g_pc_keys_on = 1`)**: All 20 matrix keys are restored to `g_saved_keymap[l][r][c]` (`KC_0`..`KC_J`). Keystrokes are sent to the PC as standard USB HID input while continuing UART escalation.
4. **Non-Host Standalone Boot Fix (`hardware/mk20/qmk/`)**:
   - QMK compiled with `NO_USB_STARTUP_CHECK = yes` and `NO_SUSPEND_POWER_DOWN = yes`.
   - Bypasses USB host enumeration wait loops on cold power-on without a PC USB connection. Matrix scanning and UART communication activate immediately.

---

## 4. Host-Level Key Matrix Representation

The MK20 matrix is a 5-column by 4-row layout indexed as follows:

```text
          [Header Display: 428 x 142]
+-------+-------+-------+-------+-------+
| (0,0) | (0,1) | (0,2) | (0,3) | (0,4) |  Row 0
+-------+-------+-------+-------+-------+
| (1,0) | (1,1) | (1,2) | (1,3) | (1,4) |  Row 1
+-------+-------+-------+-------+-------+
| (2,0) | (2,1) | (2,2) | (2,3) | (2,4) |  Row 2
+-------+-------+-------+-------+-------+
| (3,0) | (3,1) | (3,2) | (3,3) | (3,4) |  Row 3
+-------+-------+-------+-------+-------+
```

Stored configuration format in `/data/KeyboardInfo`:
- File size: 268 bytes.
- Header (bytes 0..3): Big-endian item count (`0x0000001A` = 26 records).
- Auxiliary Records (bytes 4..63): 6 records (10 bytes each) for rotary encoder and dial bindings.
- Mechanical Switch Records (bytes 64..263): 20 records (10 bytes each) indexing keys `(3,4)` down to `(0,0)`.
  - Record layout: `uint32_be row`, `uint32_be col`, `uint16_be flags`.
- Terminator (bytes 264..267): `0xFFFFFFFF` (`-1`).

---

## 5. Failure Modes and Recovery

1. **UART Timeout / Desynchronization**: If `STM32CommunicationObject` receives invalid framing or incomplete bytes, it invokes `clearState()` and triggers a timeout handler to flush buffers without hanging the Qt UI loop.
2. **GD32 Lockup**: T113 can cycle the USB / GD32 reset line via GPIO to restore key scanning without rebooting Linux.
3. **Firmware Rollback**: The GD32 DFU bootloader can be invoked programmatically via `request_bootloader_jump()` over `/dev/ttyS1`.
