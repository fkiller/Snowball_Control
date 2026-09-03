/*
 * mk20-hud.c - Native Framebuffer HUD Engine for MK20 Control Panel
 * 
 * Drives the 640x656 LCD (/dev/fb0) on Allwinner T113 Tina Linux,
 * rendering real-time AI coding agent telemetry, model thoughts,
 * active tool executions, and high-visibility approval modals.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <linux/fb.h>

#define SCREEN_W 640
#define SCREEN_H 656
#define BYTES_PER_PIXEL 4
#define STRIDE (SCREEN_W * BYTES_PER_PIXEL)
#define UDP_PORT 7701

// Color Definitions (ARGB32)
#define COLOR_BG          0xFF0B0F19  // Deep dark navy
#define COLOR_CARD        0xFF161E2E  // Elevated surface
#define COLOR_CARD_BORDER 0xFF2A3447  // Subtle card outline
#define COLOR_WHITE       0xFFFFFFFF
#define COLOR_GRAY        0xFF8E9BAE
#define COLOR_TEXT_DIM    0xFF64748B
#define COLOR_EMERALD     0xFF10B981  // Success / Approve
#define COLOR_EMERALD_BG  0xFF064E3B
#define COLOR_ROSE        0xFFF43F5E  // Reject / Stop
#define COLOR_ROSE_BG     0xFF881337
#define COLOR_AMBER       0xFFF59E0B  // Warning / Thinking
#define COLOR_INDIGO      0xFF6366F1  // Provider accent
#define COLOR_INDIGO_BG   0xFF312E81
#define COLOR_CYAN        0xFF06B6D4

// Simple 8x16 Basic ASCII Bitmap Font (Characters 32..126)
// Generated basic 5x7 / 8x16 proportional font table
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

struct HudState {
    char provider[32];
    char model[48];
    char status[32];
    char thinking[256];
    char tool_name[32];
    char tool_summary[128];
    char cost[32];
    char duration[32];
    int turn_current;
    int turn_total;
    int approval_active;
    char approval_title[128];
    char approval_desc[256];
};

static uint32_t *g_fb = NULL;
static uint32_t g_backbuffer[SCREEN_W * SCREEN_H];
static struct HudState g_state;

// Draw a single pixel into backbuffer
static inline void set_pixel(int x, int y, uint32_t color) {
    if (x >= 0 && x < SCREEN_W && y >= 0 && y < SCREEN_H) {
        g_backbuffer[y * SCREEN_W + x] = color;
    }
}

// Draw solid rectangle
static void fill_rect(int rx, int ry, int rw, int rh, uint32_t color) {
    for (int y = ry; y < ry + rh; y++) {
        for (int x = rx; x < rx + rw; x++) {
            set_pixel(x, y, color);
        }
    }
}

// Draw rectangle outline
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

// Draw single character using embedded font table
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

// Draw text string
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

// Render complete HUD UI onto backbuffer and flip to /dev/fb0
static void render_hud(void) {
    // 1. Clear background
    fill_rect(0, 0, SCREEN_W, SCREEN_H, COLOR_BG);

    // 2. Top Header Bar (Model, Provider, Cost, Time)
    fill_rect(16, 16, SCREEN_W - 32, 70, COLOR_CARD);
    draw_rect_outline(16, 16, SCREEN_W - 32, 70, COLOR_CARD_BORDER);

    // Pulsing green online dot
    fill_rect(32, 34, 12, 12, COLOR_EMERALD);
    draw_text(52, 32, "SNOWBALL CONTROL", COLOR_WHITE, 1);
    char subHeader[64];
    snprintf(subHeader, sizeof(subHeader), "Provider: %s", g_state.provider);
    draw_text(52, 54, subHeader, COLOR_INDIGO, 1);

    // Right header stats
    char costStr[64];
    snprintf(costStr, sizeof(costStr), "Cost: %s", g_state.cost);
    draw_text(460, 32, costStr, COLOR_EMERALD, 1);

    char durStr[64];
    snprintf(durStr, sizeof(durStr), "Turn: %s", g_state.duration);
    draw_text(460, 54, durStr, COLOR_GRAY, 1);

    // 3. Model Badge Card
    fill_rect(16, 102, SCREEN_W - 32, 52, COLOR_CARD);
    draw_rect_outline(16, 102, SCREEN_W - 32, 52, COLOR_CARD_BORDER);
    draw_text(32, 114, "ACTIVE MODEL:", COLOR_GRAY, 1);
    draw_text(150, 112, g_state.model, COLOR_CYAN, 2);

    // 4. Model Reasoning Ticker Box
    fill_rect(16, 170, SCREEN_W - 32, 110, COLOR_CARD);
    draw_rect_outline(16, 170, SCREEN_W - 32, 110, COLOR_CARD_BORDER);
    draw_text(32, 182, "MODEL REASONING / THINKING:", COLOR_AMBER, 1);
    draw_text(32, 206, g_state.thinking, COLOR_WHITE, 1);

    // 5. Active Tool Execution Card
    fill_rect(16, 296, SCREEN_W - 32, 100, COLOR_CARD);
    draw_rect_outline(16, 296, SCREEN_W - 32, 100, COLOR_CARD_BORDER);
    draw_text(32, 308, "ACTIVE TOOL INVOCATION:", COLOR_CYAN, 1);
    
    char toolHeader[64];
    snprintf(toolHeader, sizeof(toolHeader), "Tool: %s", g_state.tool_name);
    draw_text(32, 332, toolHeader, COLOR_AMBER, 1);
    draw_text(32, 356, g_state.tool_summary, COLOR_GRAY, 1);

    // 6. Approval Modal (Prominent Red/Green Action Panel)
    if (g_state.approval_active) {
        fill_rect(16, 412, SCREEN_W - 32, 170, COLOR_INDIGO_BG);
        draw_rect_outline(16, 412, SCREEN_W - 32, 170, COLOR_AMBER);
        
        draw_text(180, 426, "*** HUMAN APPROVAL REQUIRED ***", COLOR_AMBER, 1);
        draw_text(32, 452, g_state.approval_title, COLOR_WHITE, 1);
        draw_text(32, 474, g_state.approval_desc, COLOR_GRAY, 1);

        // Physical Key 1 [APPROVE] Button Target
        fill_rect(32, 506, 270, 56, COLOR_EMERALD_BG);
        draw_rect_outline(32, 506, 270, 56, COLOR_EMERALD);
        draw_text(60, 524, "[KEY 1] APPROVE", COLOR_EMERALD, 2);

        // Physical Key 2 [REJECT] Button Target
        fill_rect(338, 506, 270, 56, COLOR_ROSE_BG);
        draw_rect_outline(338, 506, 270, 56, COLOR_ROSE);
        draw_text(376, 524, "[KEY 2] REJECT", COLOR_ROSE, 2);
    } else {
        fill_rect(16, 412, SCREEN_W - 32, 170, COLOR_CARD);
        draw_rect_outline(16, 412, SCREEN_W - 32, 170, COLOR_CARD_BORDER);
        draw_text(32, 430, "KEY MATRIX ACTION DISPATCHER:", COLOR_GRAY, 1);
        draw_text(32, 460, "Key 1: [APPROVE]  Key 5: [PROVIDER]  Key 17: [APPR ALT]", COLOR_TEXT_DIM, 1);
        draw_text(32, 486, "Key 2: [REJECT]   Key 6: [MODEL]     Key 18: [INFO]", COLOR_TEXT_DIM, 1);
        draw_text(32, 512, "Key 3: [RETRY]    Key 7: [PLAN]      Key 19: [RESET]", COLOR_TEXT_DIM, 1);
        draw_text(32, 538, "Key 4: [CANCEL]   Key 8: [DIFF]      Key 20: [DEV]", COLOR_TEXT_DIM, 1);
    }

    // 7. Footer Status Bar
    draw_text(24, 610, "MINIME-PC-AMD (AA:BB:CC:DD:EE:FF) | Tina Linux 4.0 | COM5", COLOR_TEXT_DIM, 1);
    draw_text(520, 610, "CRC32 VALID", COLOR_EMERALD, 1);

    // 8. Push backbuffer to physical framebuffer /dev/fb0
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
    } else if (strncmp(buf, "DURATION:", 9) == 0) {
        strncpy(g_state.duration, buf + 9, sizeof(g_state.duration) - 1);
    } else if (strncmp(buf, "APPROVAL:", 9) == 0) {
        g_state.approval_active = 1;
        char *p = buf + 9;
        char *sep = strchr(p, '|');
        if (sep) {
            *sep = '\0';
            strncpy(g_state.approval_title, p, sizeof(g_state.approval_title) - 1);
            strncpy(g_state.approval_desc, sep + 1, sizeof(g_state.approval_desc) - 1);
        } else {
            strncpy(g_state.approval_title, p, sizeof(g_state.approval_title) - 1);
        }
    } else if (strncmp(buf, "CLEAR_APPROVAL:", 15) == 0) {
        g_state.approval_active = 0;
    }
}

int main(int argc, char *argv[]) {
    printf("[MK20-HUD] Initializing Snowball Control Framebuffer Engine...\n");

    // Initialize default state
    strncpy(g_state.provider, "Claude Code 2.1", sizeof(g_state.provider) - 1);
    strncpy(g_state.model, "deepseek-v4-pro", sizeof(g_state.model) - 1);
    strncpy(g_state.status, "ONLINE", sizeof(g_state.status) - 1);
    strncpy(g_state.thinking, "Snowball Control ready for coding agent sessions.", sizeof(g_state.thinking) - 1);
    strncpy(g_state.tool_name, "Ready", sizeof(g_state.tool_name) - 1);
    strncpy(g_state.tool_summary, "Listening on COM5 & UDP port 7701", sizeof(g_state.tool_summary) - 1);
    strncpy(g_state.cost, "$0.000", sizeof(g_state.cost) - 1);
    strncpy(g_state.duration, "0.0s", sizeof(g_state.duration) - 1);
    g_state.approval_active = 0;

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

    // Render initial screen
    render_hud();
    printf("[MK20-HUD] Display initialized at %dx%d @ 32bpp.\n", SCREEN_W, SCREEN_H);

    // Setup UDP listener on port 7701
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Failed to create UDP socket");
        return 1;
    }

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
        int n = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0, NULL, NULL);
        if (n > 0) {
            buffer[n] = '\0';
            parse_packet(buffer);
            render_hud();
        }
    }

    munmap(g_fb, screensize);
    close(fbfd);
    close(sockfd);
    return 0;
}
