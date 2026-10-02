# QMK 非主机模式按键扫描修复 — 固件升级

## 问题说明

QMK 键盘在非主机模式下无法扫描按键。本固件已启用 `NO_USB_STARTUP_CHECK = yes` 配置，用于修复该问题。

## 固件升级

1. 下载 QMK Toolbox：  
   <https://qmk.fm/toolbox>

2. 按下键盘左上角按键后，给设备通电，使键盘进入 Bootloader/DFU 模式。

3. 下载固件（见下图）：

   ![固件下载](firmware-download.png)

4. 打开 QMK Toolbox，选择下载好的固件文件，点击 **Flash** 进行刷写，等待刷写完成。

5. 刷写完成后，拔下 USB 线并重新插入，重启设备。

## 注意事项

- 刷写前请确认固件与键盘型号匹配。
- 刷写过程中请勿断开 USB 连接。
- 若 QMK Toolbox 未识别到设备，可重新进入 Bootloader 模式或更换 USB 端口。