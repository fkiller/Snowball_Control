CC := /mnt/e/developments/projects/Snowball_Control/hardware/mk20/Keyboard_T113_SourceCode_20260427_MK20/Tina-t113-pro/prebuilt/gcc/linux-x86/arm/toolchain-sunxi-glibc/toolchain/bin/arm-openwrt-linux-gcc
CFLAGS := -O2 -Wall -march=armv7-a -mtune=cortex-a7 -mfpu=neon -mfloat-abi=hard
LDFLAGS := 

all: mk20-hud

mk20-hud: mk20-hud.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f mk20-hud
