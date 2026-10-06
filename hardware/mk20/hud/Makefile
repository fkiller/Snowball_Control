CC ?= arm-linux-gnueabihf-gcc
CFLAGS := -O2 -fno-stack-protector -U_TIME_BITS -D_TIME_BITS=32 -U_FILE_OFFSET_BITS -D_FILE_OFFSET_BITS=32 -Wall -march=armv7-a -mtune=cortex-a7 -mfpu=neon -mfloat-abi=hard
LDFLAGS := -lm -ldl
FREETYPE_HEADERS ?= /usr/include/freetype2
CFLAGS += -I$(FREETYPE_HEADERS)

SRCS := mk20-hud.c v2_state.c v2_render.c unicode_text.c
OBJS := $(SRCS:.c=.o)

all: mk20-hud

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
	rm -f mk20-hud $(OBJS)
