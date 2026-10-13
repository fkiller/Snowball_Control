<p align="right">
  <strong>English</strong> | <a href="README.ko.md">한국어</a>
</p>

# MK20 Modified QMK Firmware Update

To resolve the factory firmware bug where key matrix scanning halts when cold booting without a USB host connection, **updating to the modified QMK firmware is mandatory**. The root cause, build requirements, binary checksums, vendor-provided sources, and deployment scope are documented in the [Architecture Specification](../../../docs/ARCHITECTURE.md).

The official target binary for the MK20 is `bin/syk_keyboards_mk20_plus_via.bin`. *Do not flash the sibling MK10 binary onto the MK20.* The QMK source code was provided directly by the hardware manufacturer via email, and the release notes confirm the application of `NO_USB_STARTUP_CHECK`. The configuration in `source/qmk/mk20_plus/rules.mk` defines `NO_USB_STARTUP_CHECK = yes` and `NO_SUSPEND_POWER_DOWN = yes`.

Use the middleware tray **Device setup… → MK20** for normal updates. Keep USB connected, confirm the target MK20 and its USB connection in the **Keyboard firmware · QMK** section, then choose **Enter keyboard firmware check mode** to request DFU through the manufacturer’s internal UART command without flashing. Review the new DFU entry; a matching included image needs no update. If the button is unavailable: Power MK20 off, then hold the leftmost square display key in the top row (not a round knob) while powering on, with USB connected to the PC. Reconnecting USB alone may not power-cycle a separately powered keyboard. Confirm the physical model and selected attachment, then choose Update. Writing requires an actual readback backup of the complete affected application/settings region and SHA-256 verification. Bytes outside the patched binary, including EEPROM, are preserved; complete readback, automatic reset and keyboard arrival on the same USB topology determine completion. The bootloader is never changed. Unsupported drivers or DFU readback block writing and are explained in the same window. QMK Toolbox and registration scripts are not needed in the normal journey. SD runtime preparation in the same window first backs up the complete card image.

Physical firmware flashing on actual hardware should be performed with appropriate care. QMK-based firmware code is licensed under [upstream GNU General Public License v2 (GPLv2)](https://github.com/qmk/qmk_firmware/blob/master/LICENSE) and retains its original notices, separate from Snowball's Apache-2.0 codebase.
