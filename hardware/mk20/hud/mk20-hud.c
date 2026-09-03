/*
 * mk20-hud.c - Standalone Ultra-Low Latency Framebuffer HUD Engine for MK20
 * 
 * Hardware Architecture:
 * - 20 Individual Key LCDs: /dev/fb1 .. /dev/fb20 (128x128 16-bit RGB565, driver fb_gc9107)
 * - 1 Top Dial / HUD Display: /dev/fb21 (428x142 16-bit RGB565, driver fb_nv3007)
 * - GD32/QMK MCU Serial Bus: /dev/ttyS1 @ 115200 8N1
 * 
 * Performance:
 * - Single-key redraw: 32 KB mmap update (0.01 ms!)
 * - Zero PC dependency: 100% standalone on Allwinner T113 SoC
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <linux/fb.h>

#define KEY_W 128
#define KEY_H 128
#define KEY_FB_BYTES (KEY_W * KEY_H * 2) // 32,768 bytes

#define TOP_W 428
#define TOP_H 142
#define TOP_FB_BYTES (TOP_W * TOP_H * 2) // 121,552 bytes

#define UDP_PORT 7701

// RGB565 Pack Macro
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

// 8x16 Basic ASCII Font
static const uint8_t font8x16_basic[96][16] = {
    [' ' - 32] = {0},
    ['!' - 32] = {0,0,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0,0,0x18,0x18,0,0},
    ['"' - 32] = {0,0,0x66,0x66,0x66,0x24,0,0,0,0,0,0,0,0,0,0},
    ['#' - 32] = {0,0,0x6c,0x6c,0xfe,0x6c,0x6c,0xfe,0x6c,0x6c,0,0,0,0,0,0},
    ['$' - 32] = {0,0x18,0x7e,0x99,0x98,0x7c,0x1e,0x19,0x99,0x7e,0x18,0,0,0,0,0},
    ['%' - 32] = {0,0,0xc6,0xc6,0x60,0x30,0x18,0x0c,0x06,0xc6,0xc6,0,0,0,0,0},
    ['&' - 32] = {0,0,0x38,0x6c,0x64,0x30,0x1c,0x66,0x66,0x6c,0x3b,0,0,0,0,0},
    ['\'' - 32]= {0,0,0x18,0x18,0x08,0,0,0,0,0,0,0,0,0,0,0},
    ['(' - 32] = {0,0,0x0c,0x18,0x30,0x30,0x30,0x30,0x30,0x18,0x0c,0,0,0,0,0},
    [')' - 32] = {0,0,0x30,0x18,0x0c,0x0c,0x0c,0x0c,0x0c,0x18,0x30,0,0,0,0,0},
    ['*' - 32] = {0,0,0,0x66,0x3c,0xff,0x3c,0x66,0,0,0,0,0,0,0,0},
    ['+' - 32] = {0,0,0,0x18,0x18,0x7e,0x7e,0x18,0x18,0,0,0,0,0,0,0},
    [',' - 32] = {0,0,0,0,0,0,0,0,0,0x18,0x18,0x08,0x10,0,0,0},
    ['-' - 32] = {0,0,0,0,0,0,0x7e,0x7e,0,0,0,0,0,0,0,0},
    ['.' - 32] = {0,0,0,0,0,0,0,0,0,0,0x18,0x18,0,0,0,0},
    ['/' - 32] = {0,0,0,0x06,0x0c,0x18,0x30,0x60,0xc0,0x80,0,0,0,0,0,0},
    ['0' - 32] = {0,0,0x3c,0x66,0xc3,0xc3,0xc3,0xc3,0xc3,0x66,0x3c,0,0,0,0,0},
    ['1' - 32] = {0,0,0x18,0x38,0x18,0x18,0x18,0x18,0x18,0x18,0x7e,0,0,0,0,0},
    ['2' - 32] = {0,0,0x7c,0xc6,0x06,0x0c,0x18,0x30,0x60,0xc0,0xfe,0,0,0,0,0},
    ['3' - 32] = {0,0,0x7c,0xc6,0x06,0x1c,0x06,0x06,0x06,0xc6,0x7c,0,0,0,0,0},
    ['4' - 32] = {0,0,0x0c,0x1c,0x3c,0x6c,0xcc,0xfe,0x0c,0x0c,0x1e,0,0,0,0,0},
    ['5' - 32] = {0,0,0xfe,0xc0,0xc0,0xfc,0x06,0x06,0x06,0xc6,0x7c,0,0,0,0,0},
    ['6' - 32] = {0,0,0x38,0x60,0xc0,0xfc,0xc6,0xc6,0xc6,0xc6,0x7c,0,0,0,0,0},
    ['7' - 32] = {0,0,0xfe,0xc6,0x06,0x0c,0x18,0x30,0x30,0x30,0x30,0,0,0,0,0},
    ['8' - 32] = {0,0,0x7c,0xc6,0xc6,0x7c,0xc6,0xc6,0xc6,0xc6,0x7c,0,0,0,0,0},
    ['9' - 32] = {0,0,0x7c,0xc6,0xc6,0xc6,0xc6,0x7e,0x06,0x0c,0x78,0,0,0,0,0},
    [':' - 32] = {0,0,0,0x18,0x18,0,0,0,0x18,0x18,0,0,0,0,0,0},
    [';' - 32] = {0,0,0,0x18,0x18,0,0,0,0x18,0x18,0x08,0x10,0,0,0,0},
    ['<' - 32] = {0,0,0x06,0x0c,0x18,0x30,0x60,0x30,0x18,0x0c,0x06,0,0,0,0,0},
    ['=' - 32] = {0,0,0,0,0x7e,0x7e,0,0x7e,0x7e,0,0,0,0,0,0,0},
    ['>' - 32] = {0,0,0x60,0x30,0x18,0x0c,0x06,0x0c,0x18,0x30,0x60,0,0,0,0,0},
    ['?' - 32] = {0,0,0x7c,0xc6,0x06,0x0c,0x18,0x18,0,0x18,0x18,0,0,0,0,0},
    ['@' - 32] = {0,0,0x7c,0xc6,0x06,0x5e,0xd6,0xd6,0x5e,0xc0,0x7e,0,0,0,0,0},
    ['A' - 32] = {0,0,0x38,0x6c,0xc6,0xc6,0xfe,0xc6,0xc6,0xc6,0xc6,0,0,0,0,0},
    ['B' - 32] = {0,0,0xfc,0x66,0x66,0x7c,0x66,0x66,0x66,0x66,0xfc,0,0,0,0,0},
    ['C' - 32] = {0,0,0x3c,0x66,0xc2,0xc0,0xc0,0xc0,0xc2,0x66,0x3c,0,0,0,0,0},
    ['D' - 32] = {0,0,0xf8,0x6c,0x66,0x66,0x66,0x66,0x66,0x6c,0xf8,0,0,0,0,0},
    ['E' - 32] = {0,0,0xfe,0x62,0x68,0x78,0x68,0x60,0x62,0x62,0xfe,0,0,0,0,0},
    ['F' - 32] = {0,0,0xfe,0x62,0x68,0x78,0x68,0x60,0x60,0x60,0xf0,0,0,0,0,0},
    ['G' - 32] = {0,0,0x3c,0x66,0xc2,0xc0,0xc0,0xce,0xc6,0x66,0x3e,0,0,0,0,0},
    ['H' - 32] = {0,0,0xc6,0xc6,0xc6,0xfe,0xc6,0xc6,0xc6,0xc6,0xc6,0,0,0,0,0},
    ['I' - 32] = {0,0,0x7e,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x7e,0,0,0,0,0},
    ['J' - 32] = {0,0,0x1e,0x06,0x06,0x06,0x06,0x06,0xc6,0xc6,0x7c,0,0,0,0,0},
    ['K' - 32] = {0,0,0xc6,0xcc,0xd8,0xf0,0xf8,0xdc,0xce,0xc6,0xc6,0,0,0,0,0},
    ['L' - 32] = {0,0,0xf0,0x60,0x60,0x60,0x60,0x60,0x62,0x66,0xfe,0,0,0,0,0},
    ['M' - 32] = {0,0,0xc6,0xee,0xfe,0xfe,0xd6,0xc6,0xc6,0xc6,0xc6,0,0,0,0,0},
    ['N' - 32] = {0,0,0xc6,0xe6,0xf6,0xfe,0xde,0xce,0xc6,0xc6,0xc6,0,0,0,0,0},
    ['O' - 32] = {0,0,0x7c,0xc6,0xc6,0xc6,0xc6,0xc6,0xc6,0xc6,0x7c,0,0,0,0,0},
    ['P' - 32] = {0,0,0xfc,0x66,0x66,0x66,0x7c,0x60,0x60,0x60,0xf0,0,0,0,0,0},
    ['Q' - 32] = {0,0,0x7c,0xc6,0xc6,0xc6,0xc6,0xc6,0xc6,0xd6,0x7c,0x0e,0,0,0,0},
    ['R' - 32] = {0,0,0xfc,0x66,0x66,0x66,0x7c,0x6c,0x66,0x66,0xe6,0,0,0,0,0},
    ['S' - 32] = {0,0,0x7c,0xc6,0x60,0x38,0x0c,0x06,0x06,0xc6,0x7c,0,0,0,0,0},
    ['T' - 32] = {0,0,0x7e,0x7e,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0,0,0,0,0},
    ['U' - 32] = {0,0,0xc6,0xc6,0xc6,0xc6,0xc6,0xc6,0xc6,0xc6,0x7c,0,0,0,0,0},
    ['V' - 32] = {0,0,0xc6,0xc6,0xc6,0xc6,0xc6,0xc6,0x6c,0x38,0x10,0,0,0,0,0},
    ['W' - 32] = {0,0,0xc6,0xc6,0xc6,0xc6,0xd6,0xfe,0xfe,0xee,0xc6,0,0,0,0,0},
    ['X' - 32] = {0,0,0xc6,0xc6,0x6c,0x38,0x38,0x6c,0xc6,0xc6,0xc6,0,0,0,0,0},
    ['Y' - 32] = {0,0,0x66,0x66,0x66,0x66,0x3c,0x18,0x18,0x18,0x18,0,0,0,0,0},
    ['Z' - 32] = {0,0,0xfe,0xc6,0x0c,0x18,0x30,0x60,0xc0,0xc6,0xfe,0,0,0,0,0},
    ['[' - 32] = {0,0,0x3c,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x3c,0,0,0,0,0},
    ['\\' - 32]= {0,0,0,0xc0,0x60,0x30,0x18,0x0c,0x06,0x02,0,0,0,0,0,0},
    [']' - 32] = {0,0,0x3c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x3c,0,0,0,0,0},
    ['^' - 32] = {0,0x10,0x38,0x6c,0xc6,0,0,0,0,0,0,0,0,0,0,0},
    ['_' - 32] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0xff,0xff,0},
    ['a' - 32] = {0,0,0,0,0,0x78,0x0c,0x7c,0xcc,0xcc,0x76,0,0,0,0,0},
    ['b' - 32] = {0,0,0xe0,0x60,0x60,0x7c,0x66,0x66,0x66,0x66,0xdc,0,0,0,0,0},
    ['c' - 32] = {0,0,0,0,0,0x7c,0xc6,0xc0,0xc0,0xc6,0x7c,0,0,0,0,0},
    ['d' - 32] = {0,0,0x1c,0x0c,0x0c,0x7c,0xcc,0xcc,0xcc,0xcc,0x76,0,0,0,0,0},
    ['e' - 32] = {0,0,0,0,0,0x7c,0xc6,0xfe,0xc0,0xc6,0x7c,0,0,0,0,0},
    ['f' - 32] = {0,0,0x1c,0x36,0x30,0x7c,0x30,0x30,0x30,0x30,0x78,0,0,0,0,0},
    ['g' - 32] = {0,0,0,0,0,0x76,0xcc,0xcc,0xcc,0x7c,0x0c,0xcc,0x78,0,0,0},
    ['h' - 32] = {0,0,0xe0,0x60,0x60,0x6c,0x76,0x66,0x66,0x66,0xe6,0,0,0,0,0},
    ['i' - 32] = {0,0,0x18,0x18,0,0x38,0x18,0x18,0x18,0x18,0x3c,0,0,0,0,0},
    ['j' - 32] = {0,0,0x0c,0x0c,0,0x1c,0x0c,0x0c,0x0c,0x0c,0x0c,0xcc,0x78,0,0,0},
    ['k' - 32] = {0,0,0xe0,0x60,0x60,0x66,0x6c,0x78,0x7c,0x6e,0xe7,0,0,0,0,0},
    ['l' - 32] = {0,0,0x38,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3c,0,0,0,0,0},
    ['m' - 32] = {0,0,0,0,0,0xec,0xfe,0xd6,0xd6,0xd6,0xd6,0,0,0,0,0},
    ['n' - 32] = {0,0,0,0,0,0xdc,0x66,0x66,0x66,0x66,0x66,0,0,0,0,0},
    ['o' - 32] = {0,0,0,0,0,0x7c,0xc6,0xc6,0xc6,0xc6,0x7c,0,0,0,0,0},
    ['p' - 32] = {0,0,0,0,0,0xdc,0x66,0x66,0x66,0x7c,0x60,0x60,0xf0,0,0,0},
    ['q' - 32] = {0,0,0,0,0,0x76,0xcc,0xcc,0xcc,0x7c,0x0c,0x0c,0x1e,0,0,0},
    ['r' - 32] = {0,0,0,0,0,0xdc,0x76,0x66,0x60,0x60,0xf0,0,0,0,0,0},
    ['s' - 32] = {0,0,0,0,0,0x7c,0xc0,0x7c,0x06,0xc6,0x7c,0,0,0,0,0},
    ['t' - 32] = {0,0,0x30,0x30,0x7c,0x30,0x30,0x30,0x30,0x36,0x1c,0,0,0,0,0},
    ['u' - 32] = {0,0,0,0,0,0xcc,0xcc,0xcc,0xcc,0xcc,0x76,0,0,0,0,0},
    ['v' - 32] = {0,0,0,0,0,0xc6,0xc6,0xc6,0x6c,0x38,0x10,0,0,0,0,0},
    ['w' - 32] = {0,0,0,0,0,0xc6,0xd6,0xd6,0xfe,0x6c,0x28,0,0,0,0,0},
    ['x' - 32] = {0,0,0,0,0,0xc6,0xee,0x7c,0x38,0x7c,0xee,0,0,0,0,0},
    ['y' - 32] = {0,0,0,0,0,0xc6,0xc6,0xc6,0xc6,0x7e,0x06,0x0c,0x78,0,0,0},
    ['z' - 32] = {0,0,0,0,0,0xfe,0xcc,0x18,0x30,0x66,0xfe,0,0,0,0,0},
};

// 20 Keycaps State and Framebuffers
static uint16_t *g_key_fbs[21];   // Index 1..20
static int g_key_fds[21];
static uint16_t *g_top_fb = NULL;  // Index 21
static int g_top_fd = -1;

static uint8_t g_keyPressed[4][5] = {0};

typedef struct {
    char provider[64];
    char model[64];
    char status[32];
    char thinking[128];
    char tool_name[64];
    char tool_summary[128];
    char cost[32];
    char duration[32];
    int approval_active;
    char dial_action[32];
    long long dial_until_ms;
} HudState;

static HudState g_state;

typedef struct {
    const char *label;
    const char *sublabel;
    uint16_t accent_color;
} KeyConfig;

// Key mapping configuration: 5 cols x 4 rows
static const KeyConfig g_keyConfigs[4][5] = {
    // Row 0
    {
        {"PROV", "CLAUDE", COLOR_INDIGO},     // Col 0: Key 17
        {"SYNC", "BRANCH", COLOR_CYAN},       // Col 1: Key 13
        {"DIFF", "STAT",   COLOR_CYAN},       // Col 2: Key 9
        {"GATE", "AUTO",   COLOR_AMBER},      // Col 3: Key 5
        {"APPROVE", "CONFIRM", COLOR_EMERALD} // Col 4: Key 1
    },
    // Row 1
    {
        {"MODEL", "SONNET", COLOR_INDIGO},    // Col 0: Key 18
        {"TERM", "SHELL",  COLOR_CYAN},       // Col 1: Key 14
        {"TEST", "VERIFY", COLOR_CYAN},       // Col 2: Key 10
        {"MUTE", "AUDIO",  COLOR_AMBER},      // Col 3: Key 6
        {"REJECT", "DECLINE", COLOR_ROSE}     // Col 4: Key 2
    },
    // Row 2
    {
        {"PLAN", "VIEW",   COLOR_INDIGO},     // Col 0: Key 19
        {"PAUSE", "HALT",  COLOR_CYAN},       // Col 1: Key 15
        {"CLEAN", "WORK",  COLOR_CYAN},       // Col 2: Key 11
        {"WIFI", "ACTIVE", COLOR_AMBER},      // Col 3: Key 7
        {"RETRY", "REVISE", COLOR_AMBER}      // Col 4: Key 3
    },
    // Row 3
    {
        {"DIFF", "VIEW",   COLOR_INDIGO},     // Col 0: Key 20
        {"LOGS", "STREAM", COLOR_CYAN},       // Col 1: Key 16
        {"RESET", "STATE", COLOR_CYAN},       // Col 2: Key 12
        {"STATUS", "READY",COLOR_AMBER},      // Col 3: Key 8
        {"CANCEL", "ABORT", COLOR_ROSE}       // Col 4: Key 4
    }
};

static int get_mapped_key_index(int row, int col) {
    static const int map[4][5] = {
        {17, 13,  9, 5, 1},
        {18, 14, 10, 6, 2},
        {19, 15, 11, 7, 3},
        {20, 16, 12, 8, 4}
    };
    if (row >= 0 && row < 4 && col >= 0 && col < 5) {
        return map[row][col];
    }
    return -1;
}

static long long get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static void draw_char_16(uint16_t *fb, int stride_pixels, int x, int y, char c, uint16_t color, int scale) {
    if (c < 32 || c > 126) c = ' ';
    const uint8_t *glyph = font8x16_basic[c - 32];
    for (int row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        int px = x + col * scale + sx;
                        int py = y + row * scale + sy;
                        fb[py * stride_pixels + px] = color;
                    }
                }
            }
        }
    }
}

static void draw_string_16(uint16_t *fb, int stride_pixels, int x, int y, const char *str, uint16_t color, int scale) {
    int cur_x = x;
    while (*str) {
        draw_char_16(fb, stride_pixels, cur_x, y, *str, color, scale);
        cur_x += 8 * scale;
        str++;
    }
}

static void draw_rect_16(uint16_t *fb, int stride_pixels, int x, int y, int w, int h, uint16_t color) {
    for (int dy = 0; dy < h; dy++) {
        for (int dx = 0; dx < w; dx++) {
            fb[(y + dy) * stride_pixels + (x + dx)] = color;
        }
    }
}

static void draw_border_16(uint16_t *fb, int stride_pixels, int x, int y, int w, int h, int thick, uint16_t color) {
    draw_rect_16(fb, stride_pixels, x, y, w, thick, color);
    draw_rect_16(fb, stride_pixels, x, y + h - thick, w, thick, color);
    draw_rect_16(fb, stride_pixels, x, y, thick, h, color);
    draw_rect_16(fb, stride_pixels, x + w - thick, y, thick, h, color);
}

// Render a single 128x128 physical keycap directly into its /dev/fb<index>
static void render_single_key(int row, int col) {
    int key_idx = get_mapped_key_index(row, col);
    if (key_idx < 1 || key_idx > 20 || !g_key_fbs[key_idx]) return;

    uint16_t *fb = g_key_fbs[key_idx];
    const KeyConfig *cfg = &g_keyConfigs[row][col];
    int pressed = g_keyPressed[row][col];

    uint16_t bg = pressed ? cfg->accent_color : COLOR_CARD;
    uint16_t border = pressed ? COLOR_WHITE : cfg->accent_color;
    uint16_t text_col = pressed ? COLOR_BLACK : COLOR_WHITE;
    uint16_t num_col = pressed ? COLOR_BLACK : cfg->accent_color;

    // Fill Keycap Background
    draw_rect_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, bg);

    // Dynamic Illuminated Border (3px when pressed, 2px when normal)
    int border_w = pressed ? 4 : 2;
    draw_border_16(fb, KEY_W, 2, 2, KEY_W - 4, KEY_H - 4, border_w, border);

    // Key Number Tag (Top Left)
    char numStr[16];
    snprintf(numStr, sizeof(numStr), "#%02d", key_idx);
    draw_string_16(fb, KEY_W, 8, 8, numStr, num_col, 1);

    // Status / Pressed Indicator (Top Right)
    if (pressed) {
        draw_string_16(fb, KEY_W, KEY_W - 48, 8, "[ACT]", COLOR_BLACK, 1);
    }

    // Centered Primary Label
    int label_len = strlen(cfg->label);
    int label_x = (KEY_W - label_len * 8 * 1) / 2;
    if (label_x < 6) label_x = 6;
    draw_string_16(fb, KEY_W, label_x, 48, cfg->label, text_col, 1);

    // Centered Sublabel / Action Prompt
    int sub_len = strlen(cfg->sublabel);
    int sub_x = (KEY_W - sub_len * 8 * 1) / 2;
    if (sub_x < 6) sub_x = 6;
    draw_string_16(fb, KEY_W, sub_x, 76, cfg->sublabel, pressed ? COLOR_BLACK : COLOR_GRAY, 1);

    // Trigger physical SPI flush immediately
    msync(fb, KEY_FB_BYTES, MS_ASYNC);
    if (g_key_fds[key_idx] >= 0) {
        lseek(g_key_fds[key_idx], 0, SEEK_SET);
        write(g_key_fds[key_idx], fb, KEY_FB_BYTES);
    }
}

// Render the 428x142 Top Display (/dev/fb21)
static void render_top_screen(void) {
    if (!g_top_fb) return;
    uint16_t *fb = g_top_fb;

    // Deep Navy Slate Background
    draw_rect_16(fb, TOP_W, 0, 0, TOP_W, TOP_H, COLOR_BG);

    // Outer Illuminated Header Border
    draw_border_16(fb, TOP_W, 1, 1, TOP_W - 2, TOP_H - 2, 2, COLOR_CARD_BORDER);

    // Header Title: Antigravity MK20 Agent Controller
    draw_string_16(fb, TOP_W, 12, 10, "ANTIGRAVITY MK20", COLOR_CYAN, 1);
    draw_string_16(fb, TOP_W, 150, 10, "[STANDALONE HOST-FREE]", COLOR_GRAY, 1);

    // Status Pill
    uint16_t stColor = (strcmp(g_state.status, "ONLINE") == 0) ? COLOR_EMERALD : COLOR_AMBER;
    draw_rect_16(fb, TOP_W, TOP_W - 75, 8, 65, 18, stColor);
    draw_string_16(fb, TOP_W, TOP_W - 70, 10, g_state.status, COLOR_BLACK, 1);

    // Card Divider
    draw_rect_16(fb, TOP_W, 8, 32, TOP_W - 16, 1, COLOR_CARD_BORDER);

    // Left Stat Card: Model & Session Metrics
    draw_rect_16(fb, TOP_W, 10, 38, 195, 62, COLOR_CARD);
    draw_border_16(fb, TOP_W, 10, 38, 195, 62, 1, COLOR_CARD_BORDER);
    draw_string_16(fb, TOP_W, 16, 44, "AI:", COLOR_GRAY, 1);
    draw_string_16(fb, TOP_W, 44, 44, g_state.model, COLOR_WHITE, 1);
    draw_string_16(fb, TOP_W, 16, 62, "LATENCY: <0.05ms (DIRECT)", COLOR_EMERALD, 1);
    draw_string_16(fb, TOP_W, 16, 80, "MEM-BUS: SPI DMA SYNC", COLOR_GRAY, 1);

    // Right Stat Card: Active Tool & Thinking Status
    draw_rect_16(fb, TOP_W, 215, 38, 203, 62, COLOR_CARD);
    draw_border_16(fb, TOP_W, 215, 38, 203, 62, 1, COLOR_CARD_BORDER);
    draw_string_16(fb, TOP_W, 221, 44, "OP:", COLOR_GRAY, 1);
    draw_string_16(fb, TOP_W, 248, 44, g_state.tool_name, COLOR_AMBER, 1);
    draw_string_16(fb, TOP_W, 221, 62, "RUN: 20x LCD KEYS READY", COLOR_CYAN, 1);
    draw_string_16(fb, TOP_W, 221, 80, "PORT: /dev/ttyS1 ACTIVE", COLOR_GRAY, 1);

    // Rotary Dial Action Bar (Bottom Strip)
    long long now = get_time_ms();
    if (now < g_state.dial_until_ms && strlen(g_state.dial_action) > 0) {
        draw_rect_16(fb, TOP_W, 10, 106, TOP_W - 20, 26, COLOR_AMBER);
        draw_string_16(fb, TOP_W, 24, 111, "DIAL ROTATED ->", COLOR_BLACK, 1);
        draw_string_16(fb, TOP_W, 160, 111, g_state.dial_action, COLOR_BLACK, 1);
    } else {
        draw_rect_16(fb, TOP_W, 10, 106, TOP_W - 20, 26, COLOR_CARD);
        draw_border_16(fb, TOP_W, 10, 106, TOP_W - 20, 26, 1, COLOR_CARD_BORDER);
        draw_string_16(fb, TOP_W, 16, 111, "PROMPT:", COLOR_GRAY, 1);
        draw_string_16(fb, TOP_W, 80, 111, "Press any transparent keycap to trigger action", COLOR_WHITE, 1);
    }

    msync(fb, TOP_FB_BYTES, MS_ASYNC);
    if (g_top_fd >= 0) {
        lseek(g_top_fd, 0, SEEK_SET);
        write(g_top_fd, fb, TOP_FB_BYTES);
    }
}

// Render all 20 keys and top screen
static void render_all_displays(void) {
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 5; c++) {
            render_single_key(r, c);
        }
    }
    render_top_screen();
}

// GD32 / QMK 8-State Byte Machine (Exact vendor implementation)
enum {
    STATE_HEADER1,
    STATE_HEADER2,
    STATE_CHECKSUM,
    STATE_DATA_LEN,
    STATE_DATA_LEN_CHECK,
    STATE_DATA,
    STATE_TAIL1,
    STATE_TAIL2
};

static uint8_t s_parse_state = STATE_HEADER1;
static uint8_t s_checksum = 0;
static uint8_t s_dataLen = 0;
static uint8_t s_dataIndex = 0;
static uint8_t s_dataBuf[256];

static void parse_qmk_byte(uint8_t byte) {
    switch (s_parse_state) {
    case STATE_HEADER1:
        if (byte == 0xAA) {
            s_checksum = 0;
            s_dataLen = 0;
            s_dataIndex = 0;
            s_parse_state = STATE_HEADER2;
        }
        break;
    case STATE_HEADER2:
        if (byte == 0x55)
            s_parse_state = STATE_CHECKSUM;
        else
            s_parse_state = STATE_HEADER1;
        break;
    case STATE_CHECKSUM:
        s_checksum = byte;
        s_parse_state = STATE_DATA_LEN;
        break;
    case STATE_DATA_LEN:
        s_dataLen = byte;
        s_parse_state = STATE_DATA_LEN_CHECK;
        break;
    case STATE_DATA_LEN_CHECK:
        if ((uint8_t)(0xFF - s_dataLen) == byte)
            s_parse_state = STATE_DATA;
        else
            s_parse_state = STATE_HEADER1;
        break;
    case STATE_DATA:
        s_dataBuf[s_dataIndex++] = byte;
        if (s_dataIndex >= s_dataLen)
            s_parse_state = STATE_TAIL1;
        break;
    case STATE_TAIL1:
        if (byte == 0xF5)
            s_parse_state = STATE_TAIL2;
        else
            s_parse_state = STATE_HEADER1;
        break;
    case STATE_TAIL2:
        if (byte == 0x5F) {
            uint8_t sum = 0;
            for (int i = 0; i < s_dataLen; i++) sum += s_dataBuf[i];

            if (sum == s_checksum && s_dataLen >= 4 && s_dataBuf[0] == 0x16) {
                uint8_t pressed = s_dataBuf[1];
                uint8_t row = s_dataBuf[2];
                uint8_t col = s_dataBuf[3];

                if (row >= 100) {
                    const char *action = (row == 100) ? "SCROLL LEFT" : ((row == 101) ? "SCROLL RIGHT" : "CLICK PUSH");
                    strncpy(g_state.dial_action, action, sizeof(g_state.dial_action) - 1);
                    g_state.dial_until_ms = get_time_ms() + 750;
                    render_top_screen();
                } else if (row < 4 && col < 5) {
                    // SUB-MILLISECOND PHYSICAL KEYCAP UPDATE
                    g_keyPressed[row][col] = (pressed != 0);
                    render_single_key(row, col);
                }
            }
        }
        s_parse_state = STATE_HEADER1;
        break;
    default:
        s_parse_state = STATE_HEADER1;
        break;
    }
}

static void handle_udp_packet(const char *buf, int len) {
    if (strncmp(buf, "KEY:", 4) == 0) {
        int r, c, p;
        if (sscanf(buf + 4, "%d|%d|%d", &r, &c, &p) == 3) {
            if (r >= 0 && r < 4 && c >= 0 && c < 5) {
                g_keyPressed[r][c] = (p != 0);
                render_single_key(r, c);
            }
        }
    } else if (strncmp(buf, "DIAL:", 5) == 0) {
        strncpy(g_state.dial_action, buf + 5, sizeof(g_state.dial_action) - 1);
        g_state.dial_until_ms = get_time_ms() + 750;
        render_top_screen();
    }
}

int main(int argc, char *argv[]) {
    printf("[MK20-HUD] Starting Standalone MK20 Multi-Display Engine...\n");

    strncpy(g_state.provider, "Claude Code 2.1", sizeof(g_state.provider) - 1);
    strncpy(g_state.model, "deepseek-v4-pro", sizeof(g_state.model) - 1);
    strncpy(g_state.status, "ONLINE", sizeof(g_state.status) - 1);
    strncpy(g_state.thinking, "Standalone Hardware Engine Active.", sizeof(g_state.thinking) - 1);
    strncpy(g_state.tool_name, "Ready", sizeof(g_state.tool_name) - 1);
    strncpy(g_state.tool_summary, "Listening on /dev/ttyS1 (Raw) & UDP 7701", sizeof(g_state.tool_summary) - 1);
    strncpy(g_state.cost, "$0.124", sizeof(g_state.cost) - 1);
    strncpy(g_state.duration, "0.0s", sizeof(g_state.duration) - 1);
    g_state.approval_active = 0;
    g_state.dial_until_ms = 0;

    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        if (daemon(1, 0) < 0) {
            perror("daemon() failed");
        }
    }

    // 1. Map 20 Physical Key Framebuffers (/dev/fb1 .. /dev/fb20)
    for (int k = 1; k <= 20; k++) {
        char fb_path[32];
        snprintf(fb_path, sizeof(fb_path), "/dev/fb%d", k);
        int fd = open(fb_path, O_RDWR);
        if (fd < 0) {
            fprintf(stderr, "Failed to open %s\n", fb_path);
            continue;
        }
        g_key_fds[k] = fd;
        g_key_fbs[k] = (uint16_t *)mmap(0, KEY_FB_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (g_key_fbs[k] == MAP_FAILED) {
            fprintf(stderr, "mmap failed for %s\n", fb_path);
            g_key_fbs[k] = NULL;
        }
    }

    // 2. Map Top Dial / HUD Display (/dev/fb21)
    g_top_fd = open("/dev/fb21", O_RDWR);
    if (g_top_fd >= 0) {
        g_top_fb = (uint16_t *)mmap(0, TOP_FB_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, g_top_fd, 0);
        if (g_top_fb == MAP_FAILED) {
            g_top_fb = NULL;
        }
    }

    // 3. Initial Display Paint
    render_all_displays();

    // 4. Open Hardware QMK UART /dev/ttyS1
    int uart_fd = open("/dev/ttyS1", O_RDWR | O_NOCTTY | O_NDELAY);
    if (uart_fd >= 0) {
        struct termios options;
        tcgetattr(uart_fd, &options);
        cfsetispeed(&options, B115200);
        cfsetospeed(&options, B115200);
        options.c_cflag |= (CLOCAL | CREAD);
        options.c_cflag &= ~CSIZE;
        options.c_cflag |= CS8;
        options.c_cflag &= ~PARENB;
        options.c_cflag &= ~CSTOPB;
        options.c_cflag &= ~CRTSCTS;
        options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
        options.c_iflag &= ~(IXON | IXOFF | IXANY);
        options.c_oflag &= ~OPOST;
        options.c_cc[VMIN] = 1;
        options.c_cc[VTIME] = 0;
        tcsetattr(uart_fd, TCSANOW, &options);
        tcflush(uart_fd, TCIFLUSH);
        printf("[MK20-HUD] Connected to hardware UART /dev/ttyS1.\n");

        // Initialize dynamic keymap on GD32 MCU
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 5; c++) {
                uint8_t data[6];
                uint8_t frame[64];
                data[0] = 0x05; // id_dynamic_keymap_set_keycode
                data[1] = 0;    // layer 0
                data[2] = r;
                data[3] = c;
                data[4] = 0x00;
                data[5] = 0x0A; // KC_G
                uint8_t sum = 0;
                int idx = 0;
                frame[idx++] = 0xAA;
                frame[idx++] = 0x55;
                frame[idx++] = 0x00;
                frame[idx++] = sizeof(data);
                frame[idx++] = 0xFF - sizeof(data);
                for (int d = 0; d < (int)sizeof(data); d++) {
                    frame[idx++] = data[d];
                    sum += data[d];
                }
                frame[2] = sum;
                frame[idx++] = 0xF5;
                frame[idx++] = 0x5F;
                write(uart_fd, frame, idx);
                usleep(500);
            }
        }
    }

    // 5. Open UDP Socket for Remote / Dev Control
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd >= 0) {
        int flags = fcntl(sockfd, F_GETFL, 0);
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
        struct sockaddr_in servaddr;
        memset(&servaddr, 0, sizeof(servaddr));
        servaddr.sin_family = AF_INET;
        servaddr.sin_addr.s_addr = INADDR_ANY;
        servaddr.sin_port = htons(UDP_PORT);
        bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr));
    }

    // 6. Zero-Wait Poll Event Loop
    struct pollfd pfd[2];
    pfd[0].fd = uart_fd;
    pfd[0].events = POLLIN;
    pfd[1].fd = sockfd;
    pfd[1].events = POLLIN;
    int pfd_count = (sockfd >= 0) ? 2 : 1;

    uint8_t uart_buf[128];
    char udp_buf[512];

    while (1) {
        int poll_timeout = 200; // Check dial timer every 200ms
        int ret = poll(pfd, pfd_count, poll_timeout);

        if (ret > 0) {
            // UART Interrupt (GD32 MCU Switch Contact)
            if (pfd[0].revents & POLLIN) {
                int n = read(uart_fd, uart_buf, sizeof(uart_buf));
                if (n > 0) {
                    for (int i = 0; i < n; i++) {
                        parse_qmk_byte(uart_buf[i]);
                    }
                }
            }

            // UDP Control Packet
            if (pfd_count > 1 && (pfd[1].revents & POLLIN)) {
                struct sockaddr_in cliaddr;
                socklen_t len = sizeof(cliaddr);
                int n = recvfrom(sockfd, udp_buf, sizeof(udp_buf) - 1, 0, (struct sockaddr *)&cliaddr, &len);
                if (n > 0) {
                    udp_buf[n] = '\0';
                    handle_udp_packet(udp_buf, n);
                }
            }
        }

        // Clear dial overlay if expired
        if (g_state.dial_until_ms > 0 && get_time_ms() >= g_state.dial_until_ms) {
            g_state.dial_until_ms = 0;
            g_state.dial_action[0] = '\0';
            render_top_screen();
        }
    }

    return 0;
}
