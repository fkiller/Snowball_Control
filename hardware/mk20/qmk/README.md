<p align="right">
  <strong>English</strong> | <a href="README.ko.md">한국어</a>
</p>

# MK20 Modified QMK Firmware Update

To resolve the factory firmware bug where key matrix scanning halts when cold booting without a USB host connection, **updating to the modified QMK firmware is mandatory**. The root cause, build requirements, binary checksums, vendor-provided sources, and deployment scope are documented in the [Architecture Specification](../../../docs/ARCHITECTURE.md).

The official target binary for the MK20 is `bin/syk_keyboards_mk20_plus_via.bin`. *Do not flash the sibling MK10 binary onto the MK20.* The QMK source code was provided directly by the hardware manufacturer via email, and the release notes confirm the application of `NO_USB_STARTUP_CHECK`. The configuration in `source/qmk/mk20_plus/rules.mk` defines `NO_USB_STARTUP_CHECK = yes` and `NO_SUSPEND_POWER_DOWN = yes`.

A full raw backup of your microSD card, keymaps, and recovery assets is strongly recommended before proceeding. After installing [QMK Toolbox](https://qmk.fm/toolbox):

1. Disconnect the MK20 USB cable from the workstation.
2. Hold down the top-left mechanical key while plugging in the USB cable.
3. Once the STM32duino DFU bootloader device is detected, select the MK20 firmware binary (`bin/syk_keyboards_mk20_plus_via.bin`).
4. Click **Flash** and do not disconnect the cable until completion.
5. Reconnect and verify key scanning and UART input even on standalone power without a PC USB host.

Physical firmware flashing on actual hardware should be performed with appropriate care. QMK-based firmware code is licensed under [upstream GNU General Public License v2 (GPLv2)](https://github.com/qmk/qmk_firmware/blob/master/LICENSE) and retains its original notices, separate from Snowball's Apache-2.0 codebase.
