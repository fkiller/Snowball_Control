/*
 * mk20-hud.c - Native Framebuffer HUD Engine for MK20 LCD Keyboard & Knob
 * 
 * Specifically designed for the MK20 hardware form-factor:
 * - 640x656 LCD panel @ 32bpp BGRA
 * - Top Display Window: 428x142 px (x: 106..534, y: 0..142)
 * - Bezel & Knob Housing: x: 0..105 and 535..639 at y: 0..142 (Hidden)
 * - 20 Transparent Keycaps: 5 cols x 4 rows, exactly 128x128 px each (y: 144..655)
 * - Instant visual button press response (depress, glow, highlight)
 * - Dual input: /dev/ttyS1 QMK UART + UDP 7701 network socket
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <linux/fb.h>

#define SCREEN_W 640
#define SCREEN_H 656
#define BYTES_PER_PIXEL 4
#define STRIDE (SCREEN_W * BYTES_PER_PIXEL)
#define UDP_PORT 7701

// Top Display Window Geometry
#define TOP_WIN_X 106
#define TOP_WIN_Y 0
#define TOP_WIN_W 428
#define TOP_WIN_H 142

// Key Matrix Geometry
#define KEY_GRID_Y 144
#define KEY_SIZE 128
#define KEY_COLS 5
#define KEY_ROWS 4

// Color Definitions (ARGB32)
#define COLOR_BLACK       0xFF000000
#define COLOR_BG          0xFF080D1A  // Deep obsidian navy
#define COLOR_CARD        0xFF121B2A  // Card surface
#define COLOR_CARD_BORDER 0xFF1E293B  // Card outline
#define COLOR_WHITE       0xFFFFFFFF
#define COLOR_GRAY        0xFF94A3B8
#define COLOR_TEXT_DIM    0xFF475569
#define COLOR_EMERALD     0xFF10B981  // Key 1 / Approve
#define COLOR_EMERALD_BG  0xFF064E3B
#define COLOR_EMERALD_LGT 0xFF34D399
#define COLOR_ROSE        0xFFF43F5E  // Key 2 / Reject
#define COLOR_ROSE_BG     0xFF881337
#define COLOR_ROSE_LGT    0xFFFB7185
#define COLOR_AMBER       0xFFF59E0B  // Key 3 / Retry / Thinking
#define COLOR_AMBER_BG    0xFF78350F
#define COLOR_INDIGO      0xFF6366F1  // Key 4 / Cancel / Mode
#define COLOR_INDIGO_BG   0xFF312E81
#define COLOR_CYAN        0xFF06B6D4  // Model / Provider
#define COLOR_CYAN_BG     0xFF164E63

// Embedded 8x16 Basic ASCII Bitmap Font Table
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
    ['-' - 32] = {0,0,0,0,0,0x7e,0x7e,0,0,0,0,0,0,0,0,0},
    ['.' - 32] = {0,0,0,0,0,0,0,0,0,0,0x18,0x18,0,0,0,0},
    ['/' - 32] = {0,0,0x06,0x0c,0x18,0x30,0x60,0xc0,0x80,0,0,0,0,0,0,0},
    ['0' - 32] = {0,0,0x3c,0x66,0xc3,0xc3,0xc3,0xc3,0xc3,0x66,0x3c,0,0,0,0,0},
    ['1' - 32] = {0,0,0x18,0x38,0x78,0x18,0x18,0x18,0x18,0x18,0x7e,0,0,0,0,0},
    ['2' - 32] = {0,0,0x7e,0xc3,0x03,0x06,0x0c,0x18,0x30,0x60,0xff,0,0,0,0,0},
    ['3' - 32] = {0,0,0x7e,0xc3,0x03,0x1e,0x03,0x03,0x03,0xc3,0x7e,0,0,0,0,0},
    ['4' - 32] = {0,0,0x06,0x0e,0x1e,0x36,0x66,0xc6,0xff,0x06,0x06,0,0,0,0,0},
    ['5' - 32] = {0,0,0xff,0xc0,0xc0,0xfe,0x03,0x03,0x03,0xc3,0x7e,0,0,0,0,0},
    ['6' - 32] = {0,0,0x3e,0x60,0xc0,0xfc,0xc6,0xc3,0xc3,0x66,0x3c,0,0,0,0,0},
    ['7' - 32] = {0,0,0xff,0x03,0x06,0x0c,0x18,0x30,0x60,0x60,0x60,0,0,0,0,0},
    ['8' - 32] = {0,0,0x3c,0x66,0xc3,0x66,0x3c,0x66,0xc3,0x66,0x3c,0,0,0,0,0},
    ['9' - 32] = {0,0,0x3c,0x66,0xc3,0xc3,0x67,0x3f,0x03,0x06,0x7c,0,0,0,0,0},
    [':' - 32] = {0,0,0,0x18,0x18,0,0,0,0,0x18,0x18,0,0,0,0,0},
    [';' - 32] = {0,0,0,0x18,0x18,0,0,0,0,0x18,0x18,0x08,0x10,0,0,0},
    ['<' - 32] = {0,0,0x0c,0x18,0x30,0x60,0x30,0x18,0x0c,0,0,0,0,0,0,0},
    ['=' - 32] = {0,0,0,0x7e,0x7e,0,0x7e,0x7e,0,0,0,0,0,0,0,0},
    ['>' - 32] = {0,0,0x30,0x18,0x0c,0x06,0x0c,0x18,0x30,0,0,0,0,0,0,0},
    ['?' - 32] = {0,0,0x3c,0x66,0x06,0x0c,0x18,0x18,0,0x18,0x18,0,0,0,0,0},
    ['@' - 32] = {0,0,0x3c,0x66,0xc3,0xdf,0xdb,0xdb,0xc0,0x60,0x3e,0,0,0,0,0},
    ['A' - 32] = {0,0,0x18,0x3c,0x66,0xc3,0xc3,0xff,0xc3,0xc3,0xc3,0,0,0,0,0},
    ['B' - 32] = {0,0,0xfc,0x66,0x66,0x7c,0x66,0x66,0x66,0x66,0xfc,0,0,0,0,0},
    ['C' - 32] = {0,0,0x3c,0x66,0xc3,0xc0,0xc0,0xc0,0xc3,0x66,0x3c,0,0,0,0,0},
    ['D' - 32] = {0,0,0xf8,0x6c,0x66,0x66,0x66,0x66,0x66,0x6c,0xf8,0,0,0,0,0},
    ['E' - 32] = {0,0,0xff,0x60,0x60,0x7c,0x60,0x60,0x60,0x60,0xff,0,0,0,0,0},
    ['F' - 32] = {0,0,0xff,0x60,0x60,0x7c,0x60,0x60,0x60,0x60,0x60,0,0,0,0,0},
    ['G' - 32] = {0,0,0x3c,0x66,0xc3,0xc0,0xc0,0xcf,0xc3,0x66,0x3b,0,0,0,0,0},
    ['H' - 32] = {0,0,0xc3,0xc3,0xc3,0xff,0xc3,0xc3,0xc3,0xc3,0xc3,0,0,0,0,0},
    ['I' - 32] = {0,0,0x7e,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x7e,0,0,0,0,0},
    ['J' - 32] = {0,0,0x1f,0x06,0x06,0x06,0x06,0x06,0xc6,0xcc,0x78,0,0,0,0,0},
    ['K' - 32] = {0,0,0xe3,0x66,0x6c,0x78,0x70,0x78,0x6c,0x66,0xe3,0,0,0,0,0},
    ['L' - 32] = {0,0,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0xff,0,0,0,0,0},
    ['M' - 32] = {0,0,0xc3,0xe7,0xff,0xdb,0xc3,0xc3,0xc3,0xc3,0xc3,0,0,0,0,0},
    ['N' - 32] = {0,0,0xc3,0xe3,0xf3,0xdb,0xcf,0xc7,0xc3,0xc3,0xc3,0,0,0,0,0},
    ['O' - 32] = {0,0,0x3c,0x66,0xc3,0xc3,0xc3,0xc3,0xc3,0x66,0x3c,0,0,0,0,0},
    ['P' - 32] = {0,0,0xfc,0x66,0x66,0x66,0x7c,0x60,0x60,0x60,0x60,0,0,0,0,0},
    ['Q' - 32] = {0,0,0x3c,0x66,0xc3,0xc3,0xc3,0xc3,0xcb,0x66,0x3d,0x03,0,0,0,0},
    ['R' - 32] = {0,0,0xfc,0x66,0x66,0x66,0x7c,0x6c,0x66,0xc3,0xc3,0,0,0,0,0},
    ['S' - 32] = {0,0,0x3e,0x63,0xc0,0x7c,0x06,0x03,0x03,0xc6,0x7c,0,0,0,0,0},
    ['T' - 32] = {0,0,0xff,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0,0,0,0,0},
    ['U' - 32] = {0,0,0xc3,0xc3,0xc3,0xc3,0xc3,0xc3,0xc3,0x66,0x3c,0,0,0,0,0},
    ['V' - 32] = {0,0,0xc3,0xc3,0xc3,0xc3,0xc3,0x66,0x66,0x3c,0x18,0,0,0,0,0},
    ['W' - 32] = {0,0,0xc3,0xc3,0xc3,0xc3,0xdb,0xff,0xe7,0xc3,0xc3,0,0,0,0,0},
    ['X' - 32] = {0,0,0xc3,0x66,0x3c,0x18,0x18,0x3c,0x66,0xc3,0xc3,0,0,0,0,0},
    ['Y' - 32] = {0,0,0xc3,0xc3,0x66,0x3c,0x18,0x18,0x18,0x18,0x18,0,0,0,0,0},
    ['Z' - 32] = {0,0,0xff,0x03,0x06,0x0c,0x18,0x30,0x60,0xc0,0xff,0,0,0,0,0},
    ['a' - 32] = {0,0,0,0,0x78,0x0c,0x7c,0xcc,0xcc,0xcc,0x76,0,0,0,0,0},
    ['b' - 32] = {0,0,0xe0,0x60,0x60,0x7c,0x66,0x66,0x66,0x66,0x7c,0,0,0,0,0},
    ['c' - 32] = {0,0,0,0,0x3c,0x66,0xc0,0xc0,0xc0,0x66,0x3c,0,0,0,0,0},
    ['d' - 32] = {0,0,0x1c,0x0c,0x0c,0x7c,0xcc,0xcc,0xcc,0xcc,0x76,0,0,0,0,0},
    ['e' - 32] = {0,0,0,0,0x3c,0x66,0xff,0xc0,0xc0,0x66,0x3c,0,0,0,0,0},
    ['f' - 32] = {0,0,0x1c,0x30,0x30,0x7c,0x30,0x30,0x30,0x30,0x78,0,0,0,0,0},
    ['g' - 32] = {0,0,0,0,0x76,0xcc,0xcc,0xcc,0x7c,0x0c,0x78,0,0,0,0,0},
    ['h' - 32] = {0,0,0xe0,0x60,0x60,0x6c,0x76,0x66,0x66,0x66,0x66,0,0,0,0,0},
    ['i' - 32] = {0,0,0x18,0x18,0,0x38,0x18,0x18,0x18,0x18,0x3c,0,0,0,0,0},
    ['j' - 32] = {0,0,0x0c,0x0c,0,0x1c,0x0c,0x0c,0x0c,0x0c,0x0c,0xcc,0x78,0,0,0},
    ['k' - 32] = {0,0,0xe0,0x60,0x60,0x66,0x6c,0x78,0x6c,0x66,0xe3,0,0,0,0,0},
    ['l' - 32] = {0,0,0x38,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3c,0,0,0,0,0},
    ['m' - 32] = {0,0,0,0,0xe6,0xff,0xdb,0xdb,0xdb,0xdb,0xdb,0,0,0,0,0},
    ['n' - 32] = {0,0,0,0,0xdc,0x66,0x66,0x66,0x66,0x66,0x66,0,0,0,0,0},
    ['o' - 32] = {0,0,0,0,0x3c,0x66,0xc3,0xc3,0xc3,0x66,0x3c,0,0,0,0,0},
    ['p' - 32] = {0,0,0,0,0xdc,0x66,0x66,0x66,0x7c,0x60,0xf0,0,0,0,0,0},
    ['q' - 32] = {0,0,0,0,0x76,0xcc,0xcc,0xcc,0x7c,0x0c,0x1e,0,0,0,0,0},
    ['r' - 32] = {0,0,0,0,0x5c,0x76,0x60,0x60,0x60,0x60,0xf0,0,0,0,0,0},
    ['s' - 32] = {0,0,0,0,0x7c,0xc6,0xe0,0x3c,0x06,0x86,0x7c,0,0,0,0,0},
    ['t' - 32] = {0,0,0x10,0x30,0x7c,0x30,0x30,0x30,0x30,0x36,0x1c,0,0,0,0,0},
    ['u' - 32] = {0,0,0,0,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0x76,0,0,0,0,0},
    ['v' - 32] = {0,0,0,0,0xc3,0xc3,0x66,0x66,0x3c,0x18,0,0,0,0,0,0},
    ['w' - 32] = {0,0,0,0,0xc3,0xc3,0xdb,0xdb,0xff,0x66,0,0,0,0,0,0},
    ['x' - 32] = {0,0,0,0,0xc3,0x66,0x3c,0x18,0x3c,0x66,0xc3,0,0,0,0,0},
    ['y' - 32] = {0,0,0,0,0xcc,0xcc,0xcc,0xcc,0x7c,0x0c,0x78,0,0,0,0,0},
    ['z' - 32] = {0,0,0,0,0xfe,0x0c,0x18,0x30,0x60,0xc0,0xfe,0,0,0,0,0},
    ['[' - 32] = {0,0,0x3c,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x3c,0,0,0,0,0},
    [']' - 32] = {0,0,0x3c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x3c,0,0,0,0,0},
    ['_' - 32] = {0,0,0,0,0,0,0,0,0,0,0,0,0xff,0,0,0}
};

// Per-Key Definition Structure
struct KeyDef {
    int keyNum;             // 1..20
    const char *badge;      // "01", "02", etc.
    const char *title;      // "APPROVE", "REJECT", "PLAN", etc.
    const char *subtitle;   // "[KEY 1]", "CLAUDE", "PRO"
    uint32_t baseColor;     // Highlight color
    uint32_t activeBgColor; // Pressed background
};

// Global Agent State
struct HudState {
    char provider[32];
    char model[48];
    char status[32];
    char thinking[128];
    char tool_name[32];
    char tool_summary[64];
    char cost[32];
    char duration[32];
    int approval_active;
    char approval_title[64];
    char dial_action[32];
    uint32_t dial_until_ms;
};

// Matrix of Keys (4 rows x 5 cols)
// Matches vendor map_table:
// Row 0: Key 17 (col 0), Key 13 (col 1), Key 9 (col 2), Key 5 (col 3), Key 1 (col 4)
// Row 1: Key 18 (col 0), Key 14 (col 1), Key 10(col 2), Key 6 (col 3), Key 2 (col 4)
// Row 2: Key 19 (col 0), Key 15 (col 1), Key 11(col 2), Key 7 (col 3), Key 3 (col 4)
// Row 3: Key 20 (col 0), Key 16 (col 1), Key 12(col 2), Key 8 (col 3), Key 4 (col 4)
static const struct KeyDef g_keyMatrix[KEY_ROWS][KEY_COLS] = {
    { // Row 0 (y: 144)
        {17, "17", "PROVIDER", "CLAUDE",   COLOR_CYAN,    COLOR_CYAN_BG},
        {13, "13", "STATUS",   "ONLINE",   COLOR_GRAY,    COLOR_CARD},
        {9,  "09", "AUTO",     "EXEC",     COLOR_INDIGO,  COLOR_INDIGO_BG},
        {5,  "05", "SPEED",    "NORMAL",   COLOR_GRAY,    COLOR_CARD},
        {1,  "01", "APPROVE",  "[CONFIRM]",COLOR_EMERALD, COLOR_EMERALD_BG}
    },
    { // Row 1 (y: 272)
        {18, "18", "MODEL",    "SONNET",   COLOR_CYAN,    COLOR_CYAN_BG},
        {14, "14", "TERMINAL", "STREAM",   COLOR_GRAY,    COLOR_CARD},
        {10, "10", "MANUAL",   "GATE",     COLOR_INDIGO,  COLOR_INDIGO_BG},
        {6,  "06", "BRANCH",   "MAIN",     COLOR_GRAY,    COLOR_CARD},
        {2,  "02", "REJECT",   "[DECLINE]",COLOR_ROSE,    COLOR_ROSE_BG}
    },
    { // Row 2 (y: 400)
        {19, "19", "PLAN",     "VIEW",     COLOR_AMBER,   COLOR_AMBER_BG},
        {15, "15", "PAUSE",    "AGENT",    COLOR_AMBER,   COLOR_AMBER_BG},
        {11, "11", "AUDIO",    "ON",       COLOR_GRAY,    COLOR_CARD},
        {7,  "07", "DIFF",     "CHECK",    COLOR_GRAY,    COLOR_CARD},
        {3,  "03", "RETRY",    "[REVISE]", COLOR_AMBER,   COLOR_AMBER_BG}
    },
    { // Row 3 (y: 528)
        {20, "20", "DIFF",     "CHANGED",  COLOR_AMBER,   COLOR_AMBER_BG},
        {16, "16", "RESUME",   "TURN",     COLOR_EMERALD, COLOR_EMERALD_BG},
        {12, "12", "MUTE",     "SOUND",    COLOR_GRAY,    COLOR_CARD},
        {8,  "08", "CLEAN",    "WORKSPACE",COLOR_ROSE,    COLOR_ROSE_BG},
        {4,  "04", "CANCEL",   "[ABORT]",  COLOR_INDIGO,  COLOR_INDIGO_BG}
    }
};

static uint32_t *g_fb = NULL;
static uint32_t g_backbuffer[SCREEN_W * SCREEN_H];
static struct HudState g_state;
static uint8_t g_keyPressed[KEY_ROWS][KEY_COLS] = {0};

static inline void set_pixel(int x, int y, uint32_t color) {
    if (x >= 0 && x < SCREEN_W && y >= 0 && y < SCREEN_H) {
        g_backbuffer[y * SCREEN_W + x] = color;
    }
}

static void fill_rect(int rx, int ry, int rw, int rh, uint32_t color) {
    for (int y = ry; y < ry + rh; y++) {
        for (int x = rx; x < rx + rw; x++) {
            set_pixel(x, y, color);
        }
    }
}

static void draw_rect_outline(int rx, int ry, int rw, int rh, uint32_t color) {
    for (int x = rx; x < rx + rw; x++) {
        set_pixel(x, ry, color);
        set_pixel(x, ry + rh - 1, color);
    }
    for (int y = ry; y < ry + rh; y++) {
        set_pixel(rx, y, color);
        set_pixel(rx + rw - 1, y, color);
    }
}

static void draw_char(int x, int y, char c, uint32_t color, int scale) {
    if (c < 32 || c > 126) c = '?';
    const uint8_t *glyph = font8x16_basic[c - 32];

    for (int row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                if (scale == 1) {
                    set_pixel(x + col, y + row, color);
                } else {
                    for (int dy = 0; dy < scale; dy++) {
                        for (int dx = 0; dx < scale; dx++) {
                            set_pixel(x + col * scale + dx, y + row * scale + dy, color);
                        }
                    }
                }
            }
        }
    }
}

static void draw_text(int x, int y, const char *str, uint32_t color, int scale) {
    int cur_x = x;
    int cur_y = y;
    while (*str) {
        if (*str == '\n') {
            cur_x = x;
            cur_y += 18 * scale;
        } else {
            draw_char(cur_x, cur_y, *str, color, scale);
            cur_x += 8 * scale + (scale > 1 ? 1 : 0);
        }
        str++;
    }
}

// Current millisecond timestamp
static uint32_t get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

// Render complete HUD adhering to MK20 physical form-factor
static void render_hud(void) {
    // 1. Clear full framebuffer
    fill_rect(0, 0, SCREEN_W, SCREEN_H, COLOR_BLACK);

    // 2. Render Top Display Window (x: 106..534, y: 0..142)
    // Corners (0..105 and 535..639) remain black/hidden behind housing.
    fill_rect(TOP_WIN_X + 2, TOP_WIN_Y + 4, TOP_WIN_W - 4, TOP_WIN_H - 8, COLOR_CARD);
    draw_rect_outline(TOP_WIN_X + 2, TOP_WIN_Y + 4, TOP_WIN_W - 4, TOP_WIN_H - 8, COLOR_CARD_BORDER);

    // Top Header Line: Online status, Title, Cost
    fill_rect(TOP_WIN_X + 14, TOP_WIN_Y + 16, 8, 8, COLOR_EMERALD);
    draw_text(TOP_WIN_X + 28, TOP_WIN_Y + 14, "SNOWBALL CONTROL", COLOR_WHITE, 1);
    
    char costStr[32];
    snprintf(costStr, sizeof(costStr), "Cost: %s", g_state.cost);
    draw_text(TOP_WIN_X + 280, TOP_WIN_Y + 14, costStr, COLOR_EMERALD, 1);

    // Model & Provider Line
    char modelBanner[64];
    snprintf(modelBanner, sizeof(modelBanner), "MODEL: %s", g_state.model);
    draw_text(TOP_WIN_X + 14, TOP_WIN_Y + 38, modelBanner, COLOR_CYAN, 1);

    // Rotary Dial Notification Overlay OR Reasoning Ticker
    uint32_t now = get_time_ms();
    if (g_state.dial_until_ms > now) {
        fill_rect(TOP_WIN_X + 14, TOP_WIN_Y + 62, TOP_WIN_W - 28, 26, COLOR_INDIGO_BG);
        draw_rect_outline(TOP_WIN_X + 14, TOP_WIN_Y + 62, TOP_WIN_W - 28, 26, COLOR_INDIGO);
        char dialStr[64];
        snprintf(dialStr, sizeof(dialStr), "KNOB ACTIVE: %s", g_state.dial_action);
        draw_text(TOP_WIN_X + 24, TOP_WIN_Y + 68, dialStr, COLOR_WHITE, 1);
    } else {
        // Real-time Reasoning Ticker
        char thinkSnippet[52];
        strncpy(thinkSnippet, g_state.thinking, sizeof(thinkSnippet) - 1);
        thinkSnippet[sizeof(thinkSnippet) - 1] = '\0';
        draw_text(TOP_WIN_X + 14, TOP_WIN_Y + 64, thinkSnippet, COLOR_AMBER, 1);
    }

    // Active Tool / Status Line
    char toolLine[64];
    snprintf(toolLine, sizeof(toolLine), "TOOL: %s -> %s", g_state.tool_name, g_state.tool_summary);
    toolLine[50] = '\0';
    draw_text(TOP_WIN_X + 14, TOP_WIN_Y + 92, toolLine, COLOR_GRAY, 1);

    // Bottom info line
    draw_text(TOP_WIN_X + 14, TOP_WIN_Y + 116, "MINIME-PC-AMD | COM5 | CDC", COLOR_TEXT_DIM, 1);
    draw_text(TOP_WIN_X + 310, TOP_WIN_Y + 116, "CRC32 OK", COLOR_EMERALD, 1);

    // 3. Render 20 Individual Transparent Keycaps (128x128 each)
    for (int r = 0; r < KEY_ROWS; r++) {
        for (int c = 0; c < KEY_COLS; c++) {
            const struct KeyDef *kd = &g_keyMatrix[r][c];
            int kx = c * KEY_SIZE;
            int ky = KEY_GRID_Y + r * KEY_SIZE;
            int isPressed = g_keyPressed[r][c];
            int isApprove = (kd->keyNum == 1);
            int isReject = (kd->keyNum == 2);

            // Inset box: 120x120 within 128x128 cell (4px padding)
            int bx = kx + 4;
            int by = ky + 4;
            int bw = KEY_SIZE - 8;
            int bh = KEY_SIZE - 8;

            uint32_t bgColor = COLOR_CARD;
            uint32_t borderColor = COLOR_CARD_BORDER;
            uint32_t textColor = COLOR_GRAY;
            uint32_t accentColor = kd->baseColor;

            // Approval Modal Alert State: Key 1 & 2 pulse brightly
            if (g_state.approval_active) {
                if (isApprove) {
                    bgColor = COLOR_EMERALD_BG;
                    borderColor = COLOR_EMERALD;
                    accentColor = COLOR_EMERALD_LGT;
                } else if (isReject) {
                    bgColor = COLOR_ROSE_BG;
                    borderColor = COLOR_ROSE;
                    accentColor = COLOR_ROSE_LGT;
                }
            }

            // Tactile-Visual Press Response: Immediate depression & intense glow
            if (isPressed) {
                bgColor = kd->activeBgColor;
                borderColor = kd->baseColor;
                textColor = COLOR_WHITE;
                // Depressed inner shadow effect
                fill_rect(bx, by, bw, bh, bgColor);
                // Thick 3px illuminated border
                draw_rect_outline(bx, by, bw, bh, borderColor);
                draw_rect_outline(bx + 1, by + 1, bw - 2, bh - 2, borderColor);
                draw_rect_outline(bx + 2, by + 2, bw - 4, bh - 4, borderColor);
            } else {
                fill_rect(bx, by, bw, bh, bgColor);
                draw_rect_outline(bx, by, bw, bh, borderColor);
            }

            // Draw Key Number Badge (Top-Left corner)
            draw_text(bx + 8, by + 8, kd->badge, isPressed ? COLOR_WHITE : COLOR_TEXT_DIM, 1);

            // Draw Primary Action / Title (Center)
            int titleLen = strlen(kd->title);
            int tx = bx + (bw - titleLen * 8) / 2;
            int ty = by + 46;
            draw_text(tx, ty, kd->title, isPressed ? COLOR_WHITE : accentColor, 1);

            // Draw Subtitle / Key Action (Bottom)
            int subLen = strlen(kd->subtitle);
            int sx = bx + (bw - subLen * 8) / 2;
            int sy = by + 88;
            draw_text(sx, sy, kd->subtitle, isPressed ? COLOR_WHITE : textColor, 1);
        }
    }

    // 4. Push backbuffer to physical framebuffer /dev/fb0
    if (g_fb) {
        memcpy(g_fb, g_backbuffer, sizeof(g_backbuffer));
    }
}

// Parse incoming state packet
static void parse_packet(char *buf) {
    if (strncmp(buf, "INIT:", 5) == 0) {
        char *p = buf + 5;
        char *sep = strchr(p, '|');
        if (sep) {
            *sep = '\0';
            strncpy(g_state.provider, p, sizeof(g_state.provider) - 1);
            strncpy(g_state.model, sep + 1, sizeof(g_state.model) - 1);
        }
    } else if (strncmp(buf, "THINKING:", 9) == 0) {
        strncpy(g_state.thinking, buf + 9, sizeof(g_state.thinking) - 1);
    } else if (strncmp(buf, "TOOL:", 5) == 0) {
        char *p = buf + 5;
        char *sep = strchr(p, '|');
        if (sep) {
            *sep = '\0';
            strncpy(g_state.tool_name, p, sizeof(g_state.tool_name) - 1);
            strncpy(g_state.tool_summary, sep + 1, sizeof(g_state.tool_summary) - 1);
        }
    } else if (strncmp(buf, "COST:", 5) == 0) {
        strncpy(g_state.cost, buf + 5, sizeof(g_state.cost) - 1);
    } else if (strncmp(buf, "APPROVAL:", 9) == 0) {
        g_state.approval_active = 1;
        strncpy(g_state.approval_title, buf + 9, sizeof(g_state.approval_title) - 1);
    } else if (strncmp(buf, "CLEAR_APPROVAL:", 15) == 0) {
        g_state.approval_active = 0;
    } else if (strncmp(buf, "KEY:", 4) == 0) {
        // Format: KEY:<row>|<col>|<pressed>
        int r = 0, c = 0, pr = 0;
        if (sscanf(buf + 4, "%d|%d|%d", &r, &c, &pr) == 3) {
            if (r >= 0 && r < KEY_ROWS && c >= 0 && c < KEY_COLS) {
                g_keyPressed[r][c] = (pr != 0);
            }
        }
    } else if (strncmp(buf, "DIAL:", 5) == 0) {
        strncpy(g_state.dial_action, buf + 5, sizeof(g_state.dial_action) - 1);
        g_state.dial_until_ms = get_time_ms() + 750; // Show for 750ms
    }
}

// Ingest direct serial bytes from GD32/QMK MCU over /dev/ttyS1 if available
static void check_qmk_serial(int uart_fd) {
    if (uart_fd < 0) return;

    static uint8_t s_buf[64];
    static int s_pos = 0;

    int n = read(uart_fd, s_buf + s_pos, sizeof(s_buf) - s_pos);
    if (n > 0) {
        s_pos += n;
        for (int i = 0; i <= s_pos - 9; i++) {
            // Check for QMK packet: 0xAA 0x55 [sum] [len] [~len] 0x16 [pressed] [row] [col]
            if (s_buf[i] == 0xAA && s_buf[i+1] == 0x55 && s_buf[i+5] == 0x16) {
                uint8_t pressed = s_buf[i+6];
                uint8_t row = s_buf[i+7];
                uint8_t col = s_buf[i+8];

                if (row >= 100) {
                    // Dial event
                    const char *action = (row == 100) ? "SCROLL LEFT" : ((row == 101) ? "SCROLL RIGHT" : "CLICK PUSH");
                    strncpy(g_state.dial_action, action, sizeof(g_state.dial_action) - 1);
                    g_state.dial_until_ms = get_time_ms() + 750;
                    render_hud();
                } else if (row < KEY_ROWS && col < KEY_COLS) {
                    // Switch contact event! Instant visual response
                    g_keyPressed[row][col] = (pressed != 0);
                    render_hud();
                }

                // Consume packet
                memmove(s_buf, s_buf + i + 9, s_pos - (i + 9));
                s_pos -= (i + 9);
                break;
            }
        }
        if (s_pos >= sizeof(s_buf)) s_pos = 0;
    }
}

int main(int argc, char *argv[]) {
    printf("[MK20-HUD] Initializing MK20 Form-Factor Framebuffer Engine...\n");

    // Initialize default state
    strncpy(g_state.provider, "Claude Code 2.1", sizeof(g_state.provider) - 1);
    strncpy(g_state.model, "deepseek-v4-pro", sizeof(g_state.model) - 1);
    strncpy(g_state.status, "ONLINE", sizeof(g_state.status) - 1);
    strncpy(g_state.thinking, "Ready. 20 Screen Keys active.", sizeof(g_state.thinking) - 1);
    strncpy(g_state.tool_name, "Ready", sizeof(g_state.tool_name) - 1);
    strncpy(g_state.tool_summary, "Listening on /dev/ttyS1 & UDP 7701", sizeof(g_state.tool_summary) - 1);
    strncpy(g_state.cost, "$0.124", sizeof(g_state.cost) - 1);
    strncpy(g_state.duration, "0.0s", sizeof(g_state.duration) - 1);
    g_state.approval_active = 0;
    g_state.dial_until_ms = 0;

    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        if (daemon(1, 0) < 0) {
            perror("daemon() failed");
        }
    }

    // Open Framebuffer /dev/fb0
    int fbfd = open("/dev/fb0", O_RDWR);
    if (fbfd < 0) {
        perror("Failed to open /dev/fb0");
        return 1;
    }

    struct fb_var_screeninfo vinfo;
    if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo)) {
        perror("ioctl FBIOGET_VSCREENINFO failed");
        close(fbfd);
        return 1;
    }

    size_t screensize = SCREEN_W * SCREEN_H * BYTES_PER_PIXEL;
    g_fb = (uint32_t *)mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
    if (g_fb == MAP_FAILED) {
        perror("mmap /dev/fb0 failed");
        close(fbfd);
        return 1;
    }

    // Open QMK UART /dev/ttyS1 non-blocking if accessible
    int uart_fd = open("/dev/ttyS1", O_RDONLY | O_NOCTTY | O_NONBLOCK);
    if (uart_fd >= 0) {
        struct termios tty;
        if (tcgetattr(uart_fd, &tty) == 0) {
            cfsetispeed(&tty, B115200);
            tty.c_cflag |= (CLOCAL | CREAD);
            tty.c_cflag &= ~PARENB;
            tty.c_cflag &= ~CSTOPB;
            tty.c_cflag &= ~CSIZE;
            tty.c_cflag |= CS8;
            tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
            tty.c_iflag &= ~(IXON | IXOFF | IXANY);
            tty.c_oflag &= ~OPOST;
            tcsetattr(uart_fd, TCSANOW, &tty);
        }
        printf("[MK20-HUD] Connected to QMK MCU UART /dev/ttyS1 for direct switch contact.\n");
    } else {
        printf("[MK20-HUD] /dev/ttyS1 busy or unavailable. Using UDP key forwarding.\n");
    }

    // Render initial screen
    render_hud();
    printf("[MK20-HUD] Display initialized at %dx%d @ 32bpp.\n", SCREEN_W, SCREEN_H);

    // Setup UDP listener on port 7701
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Failed to create UDP socket");
        return 1;
    }

    // Set non-blocking socket so we can poll both UDP and UART
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(UDP_PORT);

    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind UDP port 7701 failed");
        close(sockfd);
        return 1;
    }

    printf("[MK20-HUD] Listening for HUD events on UDP 0.0.0.0:%d...\n", UDP_PORT);

    char buffer[1024];
    while (1) {
        // 1. Check UDP packets
        int n = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0, NULL, NULL);
        if (n > 0) {
            buffer[n] = '\0';
            parse_packet(buffer);
            render_hud();
        }

        // 2. Check direct switch contacts from QMK UART
        if (uart_fd >= 0) {
            check_qmk_serial(uart_fd);
        }

        // Fast 15ms sleep loop for smooth responsiveness
        usleep(15000);
    }

    if (uart_fd >= 0) close(uart_fd);
    munmap(g_fb, screensize);
    close(fbfd);
    close(sockfd);
    return 0;
}
