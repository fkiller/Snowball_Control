TOOLCHAIN_DIR ?= $(abspath ../Keyboard_T113_SourceCode_20260427_MK20/Tina-t113-pro/prebuilt/gcc/linux-x86/arm/toolchain-sunxi-glibc/toolchain/bin)
CROSS_COMPILE ?= $(TOOLCHAIN_DIR)/arm-openwrt-linux-
CC := $(CROSS_COMPILE)gcc
CFLAGS := -O2 -Wall -march=armv7-a -mtune=cortex-a7 -mfpu=neon -mfloat-abi=hard
LDFLAGS := -lm -ldl
FREETYPE_HEADERS ?= .deps/freetype-2.6.1/include
FREETYPE_ARCHIVE ?= ../Keyboard_T113_SourceCode_20260427_MK20/Tina-t113-pro/dl/freetype-2.6.1.tar.gz
CFLAGS += -I$(FREETYPE_HEADERS)

SRCS := mk20-hud.c mk20-network.c v2_state.c v2_render.c unicode_text.c
OBJS := $(SRCS:.c=.o)

all: mk20-hud mk20-audio

mk20-audio: mk20-audio.c
	$(CC) $(CFLAGS) -o $@ $<

# Explicit lab target; not part of the normal HUD build or deployment.
pcm-stream: pcm-stream.c
	$(CC) $(CFLAGS) -o $@ $<

font-probe: font-probe.c unicode_text.c $(FREETYPE_HEADERS)/ft2build.h
	$(CC) $(CFLAGS) -o $@ font-probe.c unicode_text.c -ldl

mk20-hud: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

unicode_text.o: $(FREETYPE_HEADERS)/ft2build.h
$(FREETYPE_HEADERS)/ft2build.h:
	@test -f "$(FREETYPE_ARCHIVE)" || { echo 'Set FREETYPE_HEADERS to installed FreeType headers, or FREETYPE_ARCHIVE to a supplied source archive.'; exit 1; }
	mkdir -p .deps
	tar -xzf "$(FREETYPE_ARCHIVE)" -C .deps

clean:
	rm -f mk20-hud mk20-audio $(OBJS)
