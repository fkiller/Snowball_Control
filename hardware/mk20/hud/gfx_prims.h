#ifndef GFX_PRIMS_H
#define GFX_PRIMS_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define KEY_W 128
#define KEY_H 128
#define TOP_W 428
#define TOP_H 142

#define RGB565(r, g, b) (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) >> 3) & 0x1F))

#define COLOR_BLACK       0x0000
#define COLOR_BG          RGB565(8, 13, 26)
#define COLOR_CARD        RGB565(18, 27, 42)
#define COLOR_CARD_BORDER RGB565(30, 41, 59)
#define COLOR_WHITE       0xFFFF
#define COLOR_GRAY        RGB565(148, 163, 184)
#define COLOR_TEXT_DIM    RGB565(71, 85, 105)
#define COLOR_EMERALD     RGB565(16, 185, 129)
#define COLOR_EMERALD_BG  RGB565(6, 78, 59)
#define COLOR_ROSE        RGB565(244, 63, 94)
#define COLOR_ROSE_BG     RGB565(136, 19, 55)
#define COLOR_AMBER       RGB565(245, 158, 11)
#define COLOR_AMBER_BG    RGB565(120, 53, 15)
#define COLOR_INDIGO      RGB565(99, 102, 241)
#define COLOR_INDIGO_BG   RGB565(49, 46, 129)
#define COLOR_CYAN        RGB565(6, 182, 212)
#define COLOR_CYAN_BG     RGB565(22, 78, 99)
#define COLOR_PURPLE      RGB565(168, 85, 247)
#define COLOR_DARK_GRAY   RGB565(38, 48, 64)

extern const uint8_t font8x16[96][16];

typedef enum {
    GRADIENT_NONE = 0,
    GRADIENT_VERTICAL = 1,
    GRADIENT_HORIZONTAL = 2,
    GRADIENT_LT_TO_RB = 3,
    GRADIENT_RT_TO_LB = 4
} V2_GradientType;

static inline uint16_t interpolate_rgb565(uint16_t c0, uint16_t c1, int t, int max_t) {
    if (max_t <= 0 || t <= 0) return c0;
    if (t >= max_t) return c1;
    int r0 = (c0 >> 11) & 0x1F, g0 = (c0 >> 5) & 0x3F, b0 = c0 & 0x1F;
    int r1 = (c1 >> 11) & 0x1F, g1 = (c1 >> 5) & 0x3F, b1 = c1 & 0x1F;
    int r = r0 + ((r1 - r0) * t) / max_t;
    int g = g0 + ((g1 - g0) * t) / max_t;
    int b = b0 + ((b1 - b0) * t) / max_t;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static inline uint8_t get_rgb565_luminance(uint16_t c) {
    int r = ((c >> 11) & 0x1F) * 255 / 31;
    int g = ((c >> 5) & 0x3F) * 255 / 63;
    int b = (c & 0x1F) * 255 / 31;
    return (uint8_t)((299 * r + 587 * g + 114 * b) / 1000);
}

static inline uint16_t get_contrast_font_color(uint16_t bg, uint16_t default_fg, int auto_reverse) {
    if (!auto_reverse) return default_fg;
    uint8_t lum_bg = get_rgb565_luminance(bg);
    uint8_t lum_fg = get_rgb565_luminance(default_fg);
    int diff = (int)lum_bg - (int)lum_fg;
    if (diff < 0) diff = -diff;

    // Preserve theme color if contrast difference is already high enough (>= 75)
    if (diff >= 75) {
        return default_fg;
    }

    // Insufficient contrast (< 75): invert based on background brightness
    if (lum_bg >= 135) {
        return COLOR_BLACK;
    } else {
        return COLOR_WHITE;
    }
}

#define KEY_CORNER_RADIUS 10
#define KEY_TITLE_H       22

void draw_char_16(uint16_t *fb, int stride_pixels, int x, int y, char c, uint16_t color, int scale);
void draw_string_16(uint16_t *fb, int stride_pixels, int x, int y, const char *str, uint16_t color, int scale);
void draw_string_centered_16(uint16_t *fb, int stride_pixels, int y, const char *str, uint16_t color, int scale);
void draw_rect_16(uint16_t *fb, int stride_pixels, int x, int y, int w, int h, uint16_t color);
void draw_gradient_rect_16(uint16_t *fb, int stride_pixels, int x, int y, int w, int h, uint16_t c_start, uint16_t c_end, V2_GradientType type);
void draw_border_16(uint16_t *fb, int stride_pixels, int x, int y, int w, int h, int thick, uint16_t color);
void draw_round_rect_16(uint16_t *fb, int stride_pixels, int x, int y, int w, int h, int r, uint16_t color);
void draw_round_border_16(uint16_t *fb, int stride_pixels, int x, int y, int w, int h, int r, int thick, uint16_t color);
void draw_gradient_round_rect_16(uint16_t *fb, int stride_pixels, int x, int y, int w, int h, int r, uint16_t c_start, uint16_t c_end, V2_GradientType type);
void draw_title_bar_16(uint16_t *fb, int stride_pixels, int w, int title_h, int r, uint16_t fill_color, uint16_t line_color, int has_fill, int has_line);
void draw_line_16(uint16_t *fb, int stride_pixels, int x0, int y0, int x1, int y1, uint16_t color);
void draw_string_clipped_16(uint16_t *fb, int stride_pixels, int x, int y, const char *str, uint16_t color, int scale, int clip_x, int clip_y, int clip_w, int clip_h);

#endif // GFX_PRIMS_H
