# STM32duino Bootloader

STM32F103 USB DFU 引导程序，使用 [generic-none_bootloader.bin](https://github.com/rogerclarkmelbourne/STM32duino-bootloader/blob/master/bootloader_only_binaries/generic-none_bootloader.bin)。

## 烧录

写入 Flash 起始地址 `0x08000000`：

```bash
st-flash write generic-none_bootloader.bin 0x8000000
```

## 使用

复位后通过 USB DFU 上传固件（`dfu-util` 或 Arduino IDE）。

用户程序起始地址：`0x08002000`。

## 链接

https://github.com/rogerclarkmelbourne/STM32duino-bootloader