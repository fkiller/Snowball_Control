#include "unicode_text.h"
#include "gfx_prims.h"
#include <dlfcn.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

int unicode_step(const char *s, unsigned *cp) {
    const unsigned char *p = (const unsigned char *)s;
    *cp = p[0]; if (!p[0] || p[0] < 128) return 1;
    int n = p[0] >= 0xf0 ? 4 : p[0] >= 0xe0 ? 3 : p[0] >= 0xc2 ? 2 : 0;
    if (!n) { *cp = '?'; return 1; }
    unsigned value = p[0] & (0x7f >> n);
    for (int i = 1; i < n; i++) {
        if (!p[i] || (p[i] & 0xc0) != 0x80) { *cp = '?'; return 1; }
        value = (value << 6) | (p[i] & 63);
    }
    if (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff) ||
        (n == 2 && value < 128) || (n == 3 && value < 2048) || (n == 4 && value < 65536)) { *cp = '?'; return 1; }
    *cp = value; return n;
}
int unicode_width(const char *s) {
    int width = 0; unsigned cp;
    while (*s) { s += unicode_step(s, &cp); width += cp < 128 ? 8 : 16; }
    return width;
}
static inline uint16_t blend_rgb565(uint16_t fg, uint16_t bg, uint8_t alpha) {
    uint32_t a = alpha;
    uint32_t fg_r = (fg >> 11) & 0x1F;
    uint32_t fg_g = (fg >> 5) & 0x3F;
    uint32_t fg_b = fg & 0x1F;
    uint32_t bg_r = (bg >> 11) & 0x1F;
    uint32_t bg_g = (bg >> 5) & 0x3F;
    uint32_t bg_b = bg & 0x1F;
    uint32_t r = (fg_r * a + bg_r * (255 - a)) / 255;
    uint32_t g = (fg_g * a + bg_g * (255 - a)) / 255;
    uint32_t b = (fg_b * a + bg_b * (255 - a)) / 255;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

int unicode_draw(uint16_t *fb, int stride, int x, int y, const char *s, uint16_t color, int scale) {
    static FT_Face face;
    static time_t retry_at;
    static FT_Error (*load)(FT_Face, FT_ULong, FT_Int32);
    if (!face && time(NULL) >= retry_at) {
        retry_at = time(NULL) + 5;
        void *lib = dlopen("libfreetype.so.6", RTLD_LAZY);
        if (!lib) { fprintf(stderr, "[Font] FreeType unavailable: %s\n", dlerror()); return 0; }
        FT_Error (*init)(FT_Library*) = dlsym(lib, "FT_Init_FreeType");
        FT_Error (*newface)(FT_Library, const char*, FT_Long, FT_Face*) = dlsym(lib, "FT_New_Face");
        FT_Error (*size)(FT_Face, FT_UInt, FT_UInt) = dlsym(lib, "FT_Set_Pixel_Sizes");
        load = dlsym(lib, "FT_Load_Char");
        FT_UInt (*indexof)(FT_Face, FT_ULong) = dlsym(lib, "FT_Get_Char_Index");
        FT_Library library;
        const char *font = getenv("SNOWBALL_FONT");
        const char *paths[] = {font, "/mnt/SDCARD/fonts/D2Coding.ttf", "/usr/share/fonts/D2Coding.ttf", NULL};
        if (!init || !newface || !size || !load || !indexof || init(&library)) { fprintf(stderr, "[Font] FreeType initialization failed\n"); return 0; }
        for (int i = 0; i < 3 && !face; i++) {
            if (paths[i] && !newface(library, paths[i], 0, &face)) {
                if (!indexof(face, 0xD55C)) { fprintf(stderr, "[Font] Font has no Hangul: %s\n", paths[i]); face = NULL; }
                else fprintf(stderr, "[Font] Korean font ready: %s\n", paths[i]);
            }
        }
        if (!face) { fprintf(stderr, "[Font] Install D2Coding.ttf in /mnt/SDCARD/fonts or set SNOWBALL_FONT\n"); return 0; }
        if (size(face, 0, 16)) { face = NULL; return 0; }
    }
    if (!face) return 0;
    int height = stride == TOP_W ? TOP_H : KEY_H;
    int baseline = 13; // Optimized for D2Coding at 16px
    while (*s) {
        unsigned cp; s += unicode_step(s, &cp);
        if (!load(face, cp, FT_LOAD_RENDER)) {
            FT_GlyphSlot glyph = face->glyph;
            FT_Bitmap *b = &glyph->bitmap;
            for (unsigned row = 0; row < b->rows; row++) {
                for (unsigned col = 0; col < b->width; col++) {
                    unsigned char alpha = b->buffer[row * b->pitch + col];
                    if (alpha < 12) continue;
                    for (int dy = 0; dy < scale; dy++) {
                        for (int dx = 0; dx < scale; dx++) {
                            int px = x + (glyph->bitmap_left + (int)col) * scale + dx;
                            int py = y + (baseline - glyph->bitmap_top + (int)row) * scale + dy;
                            if (px >= 0 && px < stride && py >= 0 && py < height) {
                                if (alpha >= 240) {
                                    fb[py * stride + px] = color;
                                } else {
                                    fb[py * stride + px] = blend_rgb565(color, fb[py * stride + px], alpha);
                                }
                            }
                        }
                    }
                }
            }
        }
        x += (cp < 128 ? 8 : 16) * scale;
    }
    return 1;
}
