/*
 * mk20-hud.c - Standalone MK20 Interactive UI Showcase (12 Interaction Patterns)
 * 
 * Hardware Architecture:
 * - 20 Individual Key LCDs: /dev/fb1 .. /dev/fb20 (128x128 16-bit RGB565, driver fb_gc9107)
 * - 1 Top Dial / HUD Display: /dev/fb21 (428x142 16-bit RGB565, driver fb_nv3007)
 * - GD32/QMK MCU Serial Bus: /dev/ttyS1 @ 115200 8N1
 * 
 * 12 Implemented Interaction Patterns:
 * 1. Toggle (Same Text, Different Backgrounds) -> Key 1
 * 2. Toggle (Different Text & Backgrounds)     -> Key 2
 * 7. Shift-style Momentary (Hold to activate) -> Key 3
 * 10. Pulse-style Effect (120 BPM Metronome)   -> Key 4
 * 3. Modes (Vertical uniform text list)       -> Key 5
 * 4. Modes (Vertical carousel, 2x center)     -> Key 6
 * 5. Modes (2x2 Icon Matrix)                  -> Key 7
 * 6. Modes (Horizontal 3-icon strip)          -> Key 8
 * 8. Real-time Number (Live CPU %)            -> Key 9
 * 9. Real-time Graph (Rolling CPU Sparkline)  -> Key 10
 * 11. Knob Dial Gauge in LCD                  -> Key 11 & Top Display
 * 12. Knob Pixel Text Horizontal Marquee      -> Key 12 & Top Display
 * Aux: Keys 13..20 (Counters, Themes, Mute, Status)
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
#include <math.h>

#define KEY_W 128
#define KEY_H 128
#define KEY_FB_BYTES (KEY_W * KEY_H * 2) // 32,768 bytes

#define TOP_W 428
#define TOP_H 142
#define TOP_FB_BYTES (TOP_W * TOP_H * 2) // 121,552 bytes

#define UDP_PORT 7701

// RGB565 Color Macro
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

// 8x16 Basic ASCII Font
static const uint8_t font8x16[96][16] = {
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
    ['|' - 32] = {0,0,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0,0,0,0,0},
};

// 12x12 Custom Icon Bitmaps
// WiFi, Bluetooth, USB, Ethernet, CPU, Memory, Heart, Dial
static const uint16_t icon12x12_wifi[12] = {
    0x000, 0x3FC, 0x402, 0x1F8, 0x204, 0x0F0, 0x108, 0x060, 0x000, 0x060, 0x060, 0x000
};

static const uint16_t icon12x12_ble[12] = {
    0x080, 0x180, 0x290, 0x490, 0x2A0, 0x1C0, 0x2A0, 0x490, 0x290, 0x180, 0x080, 0x000
};

static const uint16_t icon12x12_usb[12] = {
    0x080, 0x1C0, 0x2A0, 0x490, 0x2A0, 0x1C0, 0x080, 0x288, 0x484, 0x484, 0x38C, 0x000
};

static const uint16_t icon12x12_eth[12] = {
    0x7FE, 0x801, 0xBA7, 0xBA7, 0x801, 0x7FE, 0x080, 0x080, 0x1C0, 0x2A0, 0x490, 0x000
};

static const uint16_t icon12x12_cpu[12] = {
    0x294, 0x7FE, 0x402, 0x552, 0x552, 0x552, 0x552, 0x402, 0x7FE, 0x294, 0x000, 0x000
};

static const uint16_t icon12x12_ram[12] = {
    0x7FE, 0x801, 0xA05, 0xA05, 0x801, 0x7FE, 0x554, 0x000, 0x000, 0x000, 0x000, 0x000
};

static const uint16_t icon12x12_heart[12] = {
    0x000, 0x660, 0xFF0, 0xFF0, 0xFF0, 0xFF0, 0x7E0, 0x3C0, 0x180, 0x000, 0x000, 0x000
};

static const uint16_t icon12x12_dial[12] = {
    0x3C0, 0x420, 0x810, 0x8D0, 0x870, 0x830, 0x810, 0x420, 0x3C0, 0x000, 0x000, 0x000
};

// Hardware Framebuffer handles
static uint16_t *g_key_fbs[21];   // Index 1..20
static int g_key_fds[21];
static uint16_t *g_top_fb = NULL;  // Index 21
static int g_top_fd = -1;

// Pattern Interactive States
static int g_pat1_toggle = 0;             // Key 1: Toggle Same Text
static int g_pat2_toggle = 1;             // Key 2: Toggle Different Text
static int g_pat7_shift = 0;              // Key 3: Momentary Shift
static int g_pat10_metronome_bpm = 120;   // Key 4: Metronome Pulse
static int g_pat10_pulse_val = 0;         // 0..100 breathing phase
static int g_pat3_mode_idx = 0;           // Key 5: Modes Vertical Uniform (0..3)
static int g_pat4_mode_idx = 0;           // Key 6: Modes Vertical Carousel (0..3)
static int g_pat5_icon_idx = 0;           // Key 7: Modes 2x2 Icon Grid (0..3)
static int g_pat6_icon_idx = 0;           // Key 8: Modes Horizontal Strip (0..3)
static int g_pat8_cpu_pct = 32;           // Key 9: Real-time Number CPU %
static int g_pat9_cpu_history[60] = {0};  // Key 10: Real-time Sparkline Graph
static int g_pat11_knob_val = 45;         // Key 11: Knob Dial Gauge (0..100)
static int g_pat12_scroll_px = 0;         // Key 12: Horizontal Marquee Scroll
static int g_aux_counter = 0;             // Key 13: Aux Click Counter
static int g_aux_theme = 0;               // Key 14: Aux Theme Toggle

// General HUD State
typedef struct {
    char provider[32];
    char model[32];
    char status[32];
    char message[128];
    char dial_action[32];
    long long dial_until_ms;
} HudState;

static HudState g_state;

// Key mapping configuration for the 5x4 matrix
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

// Low-Level Drawing Primitives
static void draw_char_16(uint16_t *fb, int stride_pixels, int x, int y, char c, uint16_t color, int scale) {
    if (c < 32 || c > 126) c = ' ';
    const uint8_t *glyph = font8x16[c - 32];
    for (int row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        int px = x + col * scale + sx;
                        int py = y + row * scale + sy;
                        if (px >= 0 && px < stride_pixels && py >= 0 && py < KEY_H) {
                            fb[py * stride_pixels + px] = color;
                        }
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
        int py = y + dy;
        if (py < 0 || py >= KEY_H) continue;
        for (int dx = 0; dx < w; dx++) {
            int px = x + dx;
            if (px >= 0 && px < stride_pixels) {
                fb[py * stride_pixels + px] = color;
            }
        }
    }
}

static void draw_border_16(uint16_t *fb, int stride_pixels, int x, int y, int w, int h, int thick, uint16_t color) {
    draw_rect_16(fb, stride_pixels, x, y, w, thick, color);
    draw_rect_16(fb, stride_pixels, x, y + h - thick, w, thick, color);
    draw_rect_16(fb, stride_pixels, x, y, thick, h, color);
    draw_rect_16(fb, stride_pixels, x + w - thick, y, thick, h, color);
}

static void draw_icon12x12(uint16_t *fb, int stride_pixels, int x, int y, const uint16_t *icon, uint16_t color, int scale) {
    for (int r = 0; r < 12; r++) {
        uint16_t bits = icon[r];
        for (int c = 0; c < 12; c++) {
            if (bits & (0x800 >> c)) {
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        int px = x + c * scale + sx;
                        int py = y + r * scale + sy;
                        if (px >= 0 && px < stride_pixels && py >= 0 && py < KEY_H) {
                            fb[py * stride_pixels + px] = color;
                        }
                    }
                }
            }
        }
    }
}

// Flush Key Framebuffer over SPI bus immediately
static void flush_key(int key_idx) {
    if (key_idx >= 1 && key_idx <= 20 && g_key_fds[key_idx] >= 0 && g_key_fbs[key_idx]) {
        msync(g_key_fbs[key_idx], KEY_FB_BYTES, MS_ASYNC);
        lseek(g_key_fds[key_idx], 0, SEEK_SET);
        write(g_key_fds[key_idx], g_key_fbs[key_idx], KEY_FB_BYTES);
    }
}

static void flush_top(void) {
    if (g_top_fd >= 0 && g_top_fb) {
        msync(g_top_fb, TOP_FB_BYTES, MS_ASYNC);
        lseek(g_top_fd, 0, SEEK_SET);
        write(g_top_fd, g_top_fb, TOP_FB_BYTES);
    }
}

// =========================================================================
// PATTERN 1: Toggle (Same Text, Different Backgrounds) -> Key 1
// =========================================================================
static void render_pattern_1(void) {
    uint16_t *fb = g_key_fbs[1];
    if (!fb) return;

    uint16_t bg = g_pat1_toggle ? COLOR_EMERALD : COLOR_CARD;
    uint16_t border = g_pat1_toggle ? COLOR_WHITE : COLOR_EMERALD;
    uint16_t text_col = g_pat1_toggle ? COLOR_BLACK : COLOR_WHITE;
    uint16_t badge_col = g_pat1_toggle ? COLOR_BLACK : COLOR_EMERALD;

    draw_rect_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, bg);
    draw_border_16(fb, KEY_W, 2, 2, KEY_W - 4, KEY_H - 4, 3, border);

    draw_string_16(fb, KEY_W, 8, 8, "#01", badge_col, 1);
    draw_string_16(fb, KEY_W, KEY_W - 56, 8, "TOGGLE", badge_col, 1);

    // Same Text always: MUTE
    draw_string_16(fb, KEY_W, 32, 48, "MUTE", text_col, 2);

    // Subtitle indicator
    const char *stateStr = g_pat1_toggle ? "[ ACTIVE ]" : "[ INACTIVE ]";
    draw_string_16(fb, KEY_W, (KEY_W - strlen(stateStr)*8)/2, 86, stateStr, text_col, 1);

    flush_key(1);
}

// =========================================================================
// PATTERN 2: Toggle (Different Text, Different Backgrounds) -> Key 2
// =========================================================================
static void render_pattern_2(void) {
    uint16_t *fb = g_key_fbs[2];
    if (!fb) return;

    uint16_t bg = g_pat2_toggle ? COLOR_EMERALD_BG : COLOR_ROSE_BG;
    uint16_t border = g_pat2_toggle ? COLOR_EMERALD : COLOR_ROSE;
    uint16_t text_col = COLOR_WHITE;
    const char *label = g_pat2_toggle ? "MIC ON" : "MIC OFF";
    const char *sublabel = g_pat2_toggle ? "UNMUTED" : "MUTED";

    draw_rect_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, bg);
    draw_border_16(fb, KEY_W, 2, 2, KEY_W - 4, KEY_H - 4, 3, border);

    draw_string_16(fb, KEY_W, 8, 8, "#02", border, 1);
    draw_string_16(fb, KEY_W, KEY_W - 48, 8, "ON/OFF", border, 1);

    // Different Text
    draw_string_16(fb, KEY_W, (KEY_W - strlen(label)*16)/2, 48, label, text_col, 2);
    draw_string_16(fb, KEY_W, (KEY_W - strlen(sublabel)*8)/2, 86, sublabel, border, 1);

    flush_key(2);
}

// =========================================================================
// PATTERN 7: Shift-style (Momentary Push - Active only while pressed) -> Key 3
// =========================================================================
static void render_pattern_7(void) {
    uint16_t *fb = g_key_fbs[3];
    if (!fb) return;

    uint16_t bg = g_pat7_shift ? COLOR_PURPLE : COLOR_CARD;
    uint16_t border = g_pat7_shift ? COLOR_WHITE : COLOR_PURPLE;
    uint16_t text_col = g_pat7_shift ? COLOR_BLACK : COLOR_WHITE;

    draw_rect_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, bg);
    draw_border_16(fb, KEY_W, 2, 2, KEY_W - 4, KEY_H - 4, g_pat7_shift ? 4 : 2, border);

    draw_string_16(fb, KEY_W, 8, 8, "#03", g_pat7_shift ? COLOR_BLACK : COLOR_PURPLE, 1);
    draw_string_16(fb, KEY_W, KEY_W - 48, 8, "SHIFT", g_pat7_shift ? COLOR_BLACK : COLOR_PURPLE, 1);

    draw_string_16(fb, KEY_W, 24, 48, "TURBO", text_col, 2);
    const char *stateStr = g_pat7_shift ? "<HOLDING>" : "[PUSH/HOLD]";
    draw_string_16(fb, KEY_W, (KEY_W - strlen(stateStr)*8)/2, 86, stateStr, text_col, 1);

    flush_key(3);
}

// =========================================================================
// PATTERN 10: Pulse-style Effect (Metronome / Heartbeat Animation) -> Key 4
// =========================================================================
static void render_pattern_10(void) {
    uint16_t *fb = g_key_fbs[4];
    if (!fb) return;

    // Pulse brightness calculation
    float pulse = (sinf(g_pat10_pulse_val * 0.1f) + 1.0f) * 0.5f; // 0..1
    uint16_t bg = (pulse > 0.6f) ? COLOR_ROSE_BG : COLOR_CARD;
    uint16_t heart_col = (pulse > 0.6f) ? COLOR_WHITE : COLOR_ROSE;

    draw_rect_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, bg);
    draw_border_16(fb, KEY_W, 2, 2, KEY_W - 4, KEY_H - 4, (int)(1 + pulse * 4), heart_col);

    draw_string_16(fb, KEY_W, 8, 8, "#04", heart_col, 1);
    draw_string_16(fb, KEY_W, KEY_W - 48, 8, "PULSE", heart_col, 1);

    // Draw pulsating Heart Icon (scale 2 or 3)
    int scale = (pulse > 0.7f) ? 3 : 2;
    int hx = (KEY_W - 12 * scale) / 2;
    int hy = 40;
    draw_icon12x12(fb, KEY_W, hx, hy, icon12x12_heart, heart_col, scale);

    // BPM Label
    char bpmStr[16];
    snprintf(bpmStr, sizeof(bpmStr), "BPM %d", g_pat10_metronome_bpm);
    draw_string_16(fb, KEY_W, (KEY_W - strlen(bpmStr)*8)/2, 88, bpmStr, COLOR_WHITE, 1);

    flush_key(4);
}

// =========================================================================
// PATTERN 3: Modes (Vertical uniform multiline, Cursor highlight) -> Key 5
// =========================================================================
static void render_pattern_3(void) {
    uint16_t *fb = g_key_fbs[5];
    if (!fb) return;

    static const char *modes[4] = {"PROD", "STAG", "DEV ", "LOCL"};

    draw_rect_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, COLOR_CARD);
    draw_border_16(fb, KEY_W, 2, 2, KEY_W - 4, KEY_H - 4, 2, COLOR_CARD_BORDER);

    draw_string_16(fb, KEY_W, 8, 6, "#05", COLOR_CYAN, 1);
    draw_string_16(fb, KEY_W, KEY_W - 48, 6, "MODES", COLOR_CYAN, 1);

    for (int i = 0; i < 4; i++) {
        int y = 28 + i * 24;
        if (i == g_pat3_mode_idx) {
            // Highlighted Row
            draw_rect_16(fb, KEY_W, 6, y - 2, KEY_W - 12, 20, COLOR_CYAN_BG);
            draw_border_16(fb, KEY_W, 6, y - 2, KEY_W - 12, 20, 1, COLOR_CYAN);
            draw_string_16(fb, KEY_W, 12, y, ">", COLOR_CYAN, 1);
            draw_string_16(fb, KEY_W, 28, y, modes[i], COLOR_WHITE, 1);
            draw_string_16(fb, KEY_W, KEY_W - 28, y, "*", COLOR_CYAN, 1);
        } else {
            draw_string_16(fb, KEY_W, 28, y, modes[i], COLOR_TEXT_DIM, 1);
        }
    }

    flush_key(5);
}

// =========================================================================
// PATTERN 4: Modes (Vertical carousel, Enlarged Centered Font) -> Key 6
// =========================================================================
static void render_pattern_4(void) {
    uint16_t *fb = g_key_fbs[6];
    if (!fb) return;

    static const char *modes[4] = {"FAST", "BAL ", "DEEP", "ECO "};
    int cur = g_pat4_mode_idx;
    int prev = (cur + 3) % 4;
    int next = (cur + 1) % 4;

    draw_rect_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, COLOR_CARD);
    draw_border_16(fb, KEY_W, 2, 2, KEY_W - 4, KEY_H - 4, 2, COLOR_AMBER);

    draw_string_16(fb, KEY_W, 8, 6, "#06", COLOR_AMBER, 1);
    draw_string_16(fb, KEY_W, KEY_W - 48, 6, "WHEEL", COLOR_AMBER, 1);

    // Small Prev at top
    draw_string_16(fb, KEY_W, (KEY_W - strlen(modes[prev])*8)/2, 26, modes[prev], COLOR_TEXT_DIM, 1);

    // Large Centered Selected with highlighted card
    draw_rect_16(fb, KEY_W, 8, 48, KEY_W - 16, 36, COLOR_AMBER_BG);
    draw_border_16(fb, KEY_W, 8, 48, KEY_W - 16, 36, 2, COLOR_AMBER);
    draw_string_16(fb, KEY_W, (KEY_W - strlen(modes[cur])*16)/2, 53, modes[cur], COLOR_WHITE, 2);

    // Small Next at bottom
    draw_string_16(fb, KEY_W, (KEY_W - strlen(modes[next])*8)/2, 98, modes[next], COLOR_TEXT_DIM, 1);

    flush_key(6);
}

// =========================================================================
// PATTERN 5: Modes (2x2 Icon Matrix, Active Quadrant Highlight) -> Key 7
// =========================================================================
static void render_pattern_5(void) {
    uint16_t *fb = g_key_fbs[7];
    if (!fb) return;

    draw_rect_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, COLOR_CARD);
    draw_border_16(fb, KEY_W, 2, 2, KEY_W - 4, KEY_H - 4, 2, COLOR_CARD_BORDER);

    draw_string_16(fb, KEY_W, 8, 4, "#07", COLOR_INDIGO, 1);
    draw_string_16(fb, KEY_W, KEY_W - 48, 4, "2x2", COLOR_INDIGO, 1);

    // 4 Quadrants
    int qw = 54, qh = 46;
    int qx[4] = {8, 66, 8, 66};
    int qy[4] = {24, 24, 74, 74};
    const uint16_t *icons[4] = {icon12x12_wifi, icon12x12_ble, icon12x12_usb, icon12x12_eth};
    const char *names[4] = {"WIFI", "BLE", "USB", "ETH"};

    for (int i = 0; i < 4; i++) {
        int active = (i == g_pat5_icon_idx);
        uint16_t qbg = active ? COLOR_INDIGO_BG : COLOR_BG;
        uint16_t qborder = active ? COLOR_INDIGO : COLOR_CARD_BORDER;
        uint16_t qcol = active ? COLOR_WHITE : COLOR_TEXT_DIM;

        draw_rect_16(fb, KEY_W, qx[i], qy[i], qw, qh, qbg);
        draw_border_16(fb, KEY_W, qx[i], qy[i], qw, qh, active ? 2 : 1, qborder);

        draw_icon12x12(fb, KEY_W, qx[i] + (qw - 12)/2, qy[i] + 8, icons[i], qcol, 1);
        draw_string_16(fb, KEY_W, qx[i] + (qw - strlen(names[i])*8)/2, qy[i] + 26, names[i], qcol, 1);
    }

    flush_key(7);
}

// =========================================================================
// PATTERN 6: Modes (Horizontal 3-Item Strip: Prev, Large Center, Next) -> Key 8
// =========================================================================
static void render_pattern_6(void) {
    uint16_t *fb = g_key_fbs[8];
    if (!fb) return;

    static const uint16_t *icons[4] = {icon12x12_cpu, icon12x12_ram, icon12x12_wifi, icon12x12_eth};
    static const char *names[4] = {"CPU", "MEM", "WIFI", "LAN"};

    int cur = g_pat6_icon_idx;
    int prev = (cur + 3) % 4;
    int next = (cur + 1) % 4;

    draw_rect_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, COLOR_CARD);
    draw_border_16(fb, KEY_W, 2, 2, KEY_W - 4, KEY_H - 4, 2, COLOR_EMERALD);

    draw_string_16(fb, KEY_W, 8, 6, "#08", COLOR_EMERALD, 1);
    draw_string_16(fb, KEY_W, KEY_W - 56, 6, "H-CAR", COLOR_EMERALD, 1);

    // Left Small Prev Icon
    draw_icon12x12(fb, KEY_W, 14, 46, icons[prev], COLOR_TEXT_DIM, 1);

    // Center Large Selected Icon (scale 2) inside bright card
    draw_rect_16(fb, KEY_W, 44, 32, 40, 48, COLOR_EMERALD_BG);
    draw_border_16(fb, KEY_W, 44, 32, 40, 48, 2, COLOR_EMERALD);
    draw_icon12x12(fb, KEY_W, 52, 38, icons[cur], COLOR_WHITE, 2);

    // Right Small Next Icon
    draw_icon12x12(fb, KEY_W, KEY_W - 26, 46, icons[next], COLOR_TEXT_DIM, 1);

    // Name Label below
    draw_string_16(fb, KEY_W, (KEY_W - strlen(names[cur])*8)/2, 92, names[cur], COLOR_WHITE, 1);

    flush_key(8);
}

// =========================================================================
// PATTERN 8: Real-time Number (Live CPU % Telemetry) -> Key 9
// =========================================================================
static void render_pattern_8(void) {
    uint16_t *fb = g_key_fbs[9];
    if (!fb) return;

    draw_rect_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, COLOR_CARD);
    draw_border_16(fb, KEY_W, 2, 2, KEY_W - 4, KEY_H - 4, 2, COLOR_CYAN);

    draw_string_16(fb, KEY_W, 8, 8, "#09", COLOR_CYAN, 1);
    draw_string_16(fb, KEY_W, KEY_W - 48, 8, "CPU", COLOR_CYAN, 1);

    // Big Number: e.g. " 34% "
    char numStr[16];
    snprintf(numStr, sizeof(numStr), "%2d%%", g_pat8_cpu_pct);
    uint16_t numCol = (g_pat8_cpu_pct > 70) ? COLOR_ROSE : ((g_pat8_cpu_pct > 40) ? COLOR_AMBER : COLOR_EMERALD);
    draw_string_16(fb, KEY_W, (KEY_W - strlen(numStr)*16)/2, 44, numStr, numCol, 2);

    // Horizontal Level Gauge Bar
    int bar_w = KEY_W - 24;
    int fill_w = (bar_w * g_pat8_cpu_pct) / 100;
    draw_rect_16(fb, KEY_W, 12, 86, bar_w, 12, COLOR_BG);
    draw_border_16(fb, KEY_W, 12, 86, bar_w, 12, 1, COLOR_CARD_BORDER);
    draw_rect_16(fb, KEY_W, 14, 88, fill_w, 8, numCol);

    flush_key(9);
}

// =========================================================================
// PATTERN 9: Real-time Graph (Rolling 60-sample CPU Sparkline) -> Key 10
// =========================================================================
static void render_pattern_9(void) {
    uint16_t *fb = g_key_fbs[10];
    if (!fb) return;

    draw_rect_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, COLOR_CARD);
    draw_border_16(fb, KEY_W, 2, 2, KEY_W - 4, KEY_H - 4, 2, COLOR_AMBER);

    draw_string_16(fb, KEY_W, 8, 6, "#10", COLOR_AMBER, 1);
    draw_string_16(fb, KEY_W, KEY_W - 56, 6, "GRAPH", COLOR_AMBER, 1);

    // Sparkline Graph Frame (x: 8..118, y: 26..100 -> height 74)
    int gx = 10, gy = 26, gw = 108, gh = 70;
    draw_rect_16(fb, KEY_W, gx, gy, gw, gh, COLOR_BG);
    draw_border_16(fb, KEY_W, gx, gy, gw, gh, 1, COLOR_CARD_BORDER);

    // Horizontal grid lines
    for (int yline = gy + 15; yline < gy + gh; yline += 18) {
        for (int x = gx + 2; x < gx + gw - 2; x += 4) {
            fb[yline * KEY_W + x] = COLOR_DARK_GRAY;
        }
    }

    // Render Histogram Bars
    int bars = 30; // 30 bars, each 3px wide
    for (int b = 0; b < bars; b++) {
        int hist_idx = 60 - bars + b;
        int val = g_pat9_cpu_history[hist_idx];
        if (val < 0) val = 0;
        if (val > 100) val = 100;

        int bar_h = (val * (gh - 4)) / 100;
        int bx = gx + 2 + b * 3 + (b / 2);
        if (bx + 2 >= gx + gw) break;

        uint16_t bar_col = (val > 70) ? COLOR_ROSE : ((val > 40) ? COLOR_AMBER : COLOR_EMERALD);
        for (int h = 0; h < bar_h; h++) {
            int py = gy + gh - 2 - h;
            fb[py * KEY_W + bx] = bar_col;
            fb[py * KEY_W + bx + 1] = bar_col;
        }
    }

    // Sparkline Bottom Caption
    draw_string_16(fb, KEY_W, 12, 104, "HIST: 60s REALTIME", COLOR_GRAY, 1);

    flush_key(10);
}

// =========================================================================
// PATTERN 11: Knob Dial Gauge in LCD (| O [ VALUE ] O |) -> Key 11
// =========================================================================
static void render_pattern_11(void) {
    uint16_t *fb = g_key_fbs[11];
    if (!fb) return;

    draw_rect_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, COLOR_CARD);
    draw_border_16(fb, KEY_W, 2, 2, KEY_W - 4, KEY_H - 4, 2, COLOR_CYAN);

    draw_string_16(fb, KEY_W, 8, 8, "#11", COLOR_CYAN, 1);
    draw_string_16(fb, KEY_W, KEY_W - 48, 8, "DIAL", COLOR_CYAN, 1);

    // Rotary Gauge Icon
    draw_icon12x12(fb, KEY_W, (KEY_W - 24)/2, 28, icon12x12_dial, COLOR_CYAN, 2);

    // Value display: e.g. "45%"
    char valStr[16];
    snprintf(valStr, sizeof(valStr), "%3d%%", g_pat11_knob_val);
    draw_string_16(fb, KEY_W, (KEY_W - strlen(valStr)*8)/2, 62, valStr, COLOR_WHITE, 1);

    // Dial Effect Slider: | O [  ●  ] O |
    // Track: x: 14..114 (width 100)
    int tx = 14, ty = 84, tw = 100, th = 14;
    draw_rect_16(fb, KEY_W, tx, ty, tw, th, COLOR_BG);
    draw_border_16(fb, KEY_W, tx, ty, tw, th, 1, COLOR_CARD_BORDER);

    // Left and Right Detent Rings "O"
    draw_string_16(fb, KEY_W, tx - 10, ty - 1, "|", COLOR_CYAN, 1);
    draw_string_16(fb, KEY_W, tx + tw + 2, ty - 1, "|", COLOR_CYAN, 1);

    // Slider Knob position
    int kx = tx + 2 + ((tw - 16) * g_pat11_knob_val) / 100;
    draw_rect_16(fb, KEY_W, kx, ty + 2, 12, 10, COLOR_CYAN);
    draw_border_16(fb, KEY_W, kx, ty + 2, 12, 10, 1, COLOR_WHITE);

    // Subtitle
    draw_string_16(fb, KEY_W, 14, 106, "TURN ROTARY KNOB", COLOR_GRAY, 1);

    flush_key(11);
}

// =========================================================================
// PATTERN 12: Knob Pixel Text Horizontal Marquee Scroll -> Key 12
// =========================================================================
static void render_pattern_12(void) {
    uint16_t *fb = g_key_fbs[12];
    if (!fb) return;

    static const char *msg = ">>> ANTIGRAVITY MK20 REALTIME MULTI-DISPLAY INTERACTION ENGINE <<<";
    int msg_len_px = strlen(msg) * 8;

    draw_rect_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, COLOR_CARD);
    draw_border_16(fb, KEY_W, 2, 2, KEY_W - 4, KEY_H - 4, 2, COLOR_INDIGO);

    draw_string_16(fb, KEY_W, 8, 8, "#12", COLOR_INDIGO, 1);
    draw_string_16(fb, KEY_W, KEY_W - 56, 8, "SCROLL", COLOR_INDIGO, 1);

    // Marquee Viewport Window (x: 8..120, y: 44..76)
    int vx = 8, vy = 44, vw = KEY_W - 16, vh = 32;
    draw_rect_16(fb, KEY_W, vx, vy, vw, vh, COLOR_BG);
    draw_border_16(fb, KEY_W, vx, vy, vw, vh, 1, COLOR_INDIGO_BG);

    // Render Scrolled Text with clipping
    int text_x = vx + 4 - (g_pat12_scroll_px % msg_len_px);
    // Draw twice for seamless loop
    draw_string_16(fb, KEY_W, text_x, vy + 8, msg, COLOR_WHITE, 1);
    draw_string_16(fb, KEY_W, text_x + msg_len_px + 32, vy + 8, msg, COLOR_WHITE, 1);

    draw_string_16(fb, KEY_W, 14, 88, "PIXEL MARQUEE", COLOR_INDIGO, 1);
    draw_string_16(fb, KEY_W, 14, 106, "KNOB CONTROLS", COLOR_GRAY, 1);

    flush_key(12);
}

// =========================================================================
// Auxiliary Keys: 13..20
// =========================================================================
static void render_aux_key(int key_idx) {
    uint16_t *fb = g_key_fbs[key_idx];
    if (!fb) return;

    draw_rect_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, COLOR_CARD);
    draw_border_16(fb, KEY_W, 2, 2, KEY_W - 4, KEY_H - 4, 2, COLOR_CARD_BORDER);

    char numStr[16];
    snprintf(numStr, sizeof(numStr), "#%02d", key_idx);
    draw_string_16(fb, KEY_W, 8, 8, numStr, COLOR_GRAY, 1);

    switch (key_idx) {
        case 13: // Counter
            draw_string_16(fb, KEY_W, KEY_W - 56, 8, "COUNT", COLOR_CYAN, 1);
            char cntStr[16];
            snprintf(cntStr, sizeof(cntStr), "%d", g_aux_counter);
            draw_string_16(fb, KEY_W, (KEY_W - strlen(cntStr)*16)/2, 48, cntStr, COLOR_CYAN, 2);
            draw_string_16(fb, KEY_W, 14, 88, "[CLICK TO INC]", COLOR_GRAY, 1);
            break;
        case 14: // Audio Test
            draw_string_16(fb, KEY_W, KEY_W - 56, 8, "AUDIO", COLOR_EMERALD, 1);
            draw_string_16(fb, KEY_W, 24, 48, "BEEP", COLOR_EMERALD, 2);
            draw_string_16(fb, KEY_W, 14, 88, "[CLICK SOUND]", COLOR_GRAY, 1);
            break;
        case 15: // Reset All
            draw_string_16(fb, KEY_W, KEY_W - 56, 8, "RESET", COLOR_ROSE, 1);
            draw_string_16(fb, KEY_W, 20, 48, "RESET", COLOR_ROSE, 2);
            draw_string_16(fb, KEY_W, 14, 88, "[ALL DEFAULT]", COLOR_GRAY, 1);
            break;
        case 16: // Theme
            draw_string_16(fb, KEY_W, KEY_W - 56, 8, "THEME", COLOR_PURPLE, 1);
            draw_string_16(fb, KEY_W, 24, 48, g_aux_theme ? "LIGHT" : "DARK ", COLOR_PURPLE, 2);
            draw_string_16(fb, KEY_W, 14, 88, "[CLICK THEME]", COLOR_GRAY, 1);
            break;
        case 17: // Provider
            draw_string_16(fb, KEY_W, KEY_W - 48, 8, "PROV", COLOR_INDIGO, 1);
            draw_string_16(fb, KEY_W, 14, 48, "CLAUDE", COLOR_WHITE, 2);
            draw_string_16(fb, KEY_W, 14, 88, "OPUS 4.6", COLOR_INDIGO, 1);
            break;
        case 18: // Model
            draw_string_16(fb, KEY_W, KEY_W - 48, 8, "MODEL", COLOR_INDIGO, 1);
            draw_string_16(fb, KEY_W, 14, 48, "SONNET", COLOR_WHITE, 2);
            draw_string_16(fb, KEY_W, 14, 88, "AGENTIC", COLOR_INDIGO, 1);
            break;
        case 19: // Plan View
            draw_string_16(fb, KEY_W, KEY_W - 48, 8, "PLAN", COLOR_CYAN, 1);
            draw_string_16(fb, KEY_W, 24, 48, "PLAN", COLOR_WHITE, 2);
            draw_string_16(fb, KEY_W, 14, 88, "[ACTIVE]", COLOR_CYAN, 1);
            break;
        case 20: // Diff View
            draw_string_16(fb, KEY_W, KEY_W - 48, 8, "DIFF", COLOR_AMBER, 1);
            draw_string_16(fb, KEY_W, 24, 48, "DIFF", COLOR_WHITE, 2);
            draw_string_16(fb, KEY_W, 14, 88, "+204 -82", COLOR_EMERALD, 1);
            break;
    }

    flush_key(key_idx);
}

// Render All 20 Physical Keycaps
static void render_all_keys(void) {
    render_pattern_1();
    render_pattern_2();
    render_pattern_7();
    render_pattern_10();
    render_pattern_3();
    render_pattern_4();
    render_pattern_5();
    render_pattern_6();
    render_pattern_8();
    render_pattern_9();
    render_pattern_11();
    render_pattern_12();
    for (int k = 13; k <= 20; k++) {
        render_aux_key(k);
    }
}

// =========================================================================
// TOP DISPLAY (/dev/fb21): Pattern 11 (Dial Gauge) & Pattern 12 (Marquee)
// =========================================================================
static void render_top_display(void) {
    if (!g_top_fb) return;
    uint16_t *fb = g_top_fb;

    draw_rect_16(fb, TOP_W, 0, 0, TOP_W, TOP_H, COLOR_BG);
    draw_border_16(fb, TOP_W, 1, 1, TOP_W - 2, TOP_H - 2, 2, COLOR_CARD_BORDER);

    // Header Title
    draw_string_16(fb, TOP_W, 12, 8, "MK20 UI SHOWCASE", COLOR_CYAN, 1);
    draw_string_16(fb, TOP_W, 160, 8, "12 INTERACTIVE PATTERNS", COLOR_GRAY, 1);

    // Status Pill
    draw_rect_16(fb, TOP_W, TOP_W - 75, 6, 65, 18, COLOR_EMERALD);
    draw_string_16(fb, TOP_W, TOP_W - 70, 8, "ACTIVE", COLOR_BLACK, 1);

    draw_rect_16(fb, TOP_W, 8, 28, TOP_W - 16, 1, COLOR_CARD_BORDER);

    // PATTERN 11: Large Top Knob Dial Slider: | O [ ( VALUE ) ] O |
    int dx = 12, dy = 36, dw = 200, dh = 58;
    draw_rect_16(fb, TOP_W, dx, dy, dw, dh, COLOR_CARD);
    draw_border_16(fb, TOP_W, dx, dy, dw, dh, 1, COLOR_CYAN);

    draw_string_16(fb, TOP_W, dx + 6, dy + 6, "KNOB DIAL GAUGE", COLOR_CYAN, 1);
    char dialValStr[16];
    snprintf(dialValStr, sizeof(dialValStr), "%3d%%", g_pat11_knob_val);
    draw_string_16(fb, TOP_W, dx + dw - 44, dy + 6, dialValStr, COLOR_WHITE, 1);

    // Track
    int track_x = dx + 18, track_y = dy + 32, track_w = dw - 36, track_h = 14;
    draw_rect_16(fb, TOP_W, track_x, track_y, track_w, track_h, COLOR_BG);
    draw_border_16(fb, TOP_W, track_x, track_y, track_w, track_h, 1, COLOR_CARD_BORDER);

    // Knob Ends | O ... O |
    draw_string_16(fb, TOP_W, dx + 4, track_y - 1, "|O", COLOR_CYAN, 1);
    draw_string_16(fb, TOP_W, dx + dw - 18, track_y - 1, "O|", COLOR_CYAN, 1);

    // Active Slider Thumb
    int thumb_x = track_x + 2 + ((track_w - 20) * g_pat11_knob_val) / 100;
    draw_rect_16(fb, TOP_W, thumb_x, track_y + 2, 16, 10, COLOR_CYAN);
    draw_border_16(fb, TOP_W, thumb_x, track_y + 2, 16, 10, 1, COLOR_WHITE);

    // PATTERN 8 & 9 Preview Card: Live CPU
    int cx = 222, cy = 36, cw = 194, ch = 58;
    draw_rect_16(fb, TOP_W, cx, cy, cw, ch, COLOR_CARD);
    draw_border_16(fb, TOP_W, cx, cy, cw, ch, 1, COLOR_AMBER);

    draw_string_16(fb, TOP_W, cx + 6, cy + 6, "SYSTEM TELEMETRY", COLOR_AMBER, 1);
    char cpuMsg[32];
    snprintf(cpuMsg, sizeof(cpuMsg), "CPU LOAD: %2d%%", g_pat8_cpu_pct);
    draw_string_16(fb, TOP_W, cx + 6, cy + 24, cpuMsg, COLOR_WHITE, 1);
    draw_string_16(fb, TOP_W, cx + 6, cy + 40, "LATENCY: 0.01ms (SPI)", COLOR_EMERALD, 1);

    // PATTERN 12: Horizontal Marquee Marquee Bar (Bottom Strip)
    static const char *top_marquee = "ANTIGRAVITY MK20 DUAL-CORE STANDALONE HARDWARE TWIN -- PRESS KEYS #01 TO #12 TO TEST VISUAL PATTERNS -- ROTATE DIAL TO ADJUST GAUGES -- ";
    int top_marq_len = strlen(top_marquee) * 8;

    int mx = 12, my = 104, mw = TOP_W - 24, mh = 28;
    draw_rect_16(fb, TOP_W, mx, my, mw, mh, COLOR_CARD);
    draw_border_16(fb, TOP_W, mx, my, mw, mh, 1, COLOR_CARD_BORDER);

    int scroll_x = mx + 4 - (g_pat12_scroll_px % top_marq_len);
    draw_string_16(fb, TOP_W, scroll_x, my + 6, top_marquee, COLOR_WHITE, 1);
    draw_string_16(fb, TOP_W, scroll_x + top_marq_len + 32, my + 6, top_marquee, COLOR_WHITE, 1);

    flush_top();
}

// Live CPU % reader from /proc/stat
static int read_cpu_percent(void) {
    static unsigned long long prev_u = 0, prev_n = 0, prev_s = 0, prev_i = 0;
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return 28;

    char line[128];
    if (fgets(line, sizeof(line), fp)) {
        unsigned long long u, n, s, i;
        if (sscanf(line, "cpu %llu %llu %llu %llu", &u, &n, &s, &i) == 4) {
            unsigned long long total_diff = (u + n + s + i) - (prev_u + prev_n + prev_s + prev_i);
            unsigned long long idle_diff = i - prev_i;
            prev_u = u; prev_n = n; prev_s = s; prev_i = i;
            fclose(fp);
            if (total_diff > 0) {
                int pct = (int)((total_diff - idle_diff) * 100 / total_diff);
                if (pct < 0) pct = 0;
                if (pct > 100) pct = 100;
                return pct;
            }
        }
    }
    fclose(fp);
    return 28;
}

// Handle switch contact events
static void on_key_event(int row, int col, int pressed) {
    int key_idx = get_mapped_key_index(row, col);
    if (key_idx < 1 || key_idx > 20) return;

    if (key_idx == 1 && pressed) {
        // Pattern 1: Toggle Same Text
        g_pat1_toggle = !g_pat1_toggle;
        render_pattern_1();
    } else if (key_idx == 2 && pressed) {
        // Pattern 2: Toggle Different Text
        g_pat2_toggle = !g_pat2_toggle;
        render_pattern_2();
    } else if (key_idx == 3) {
        // Pattern 7: Shift-Style Momentary
        g_pat7_shift = pressed;
        render_pattern_7();
    } else if (key_idx == 4 && pressed) {
        // Pattern 10: Metronome BPM toggle
        g_pat10_metronome_bpm = (g_pat10_metronome_bpm == 120) ? 160 : ((g_pat10_metronome_bpm == 160) ? 80 : 120);
        render_pattern_10();
    } else if (key_idx == 5 && pressed) {
        // Pattern 3: Modes Vertical Uniform
        g_pat3_mode_idx = (g_pat3_mode_idx + 1) % 4;
        render_pattern_3();
    } else if (key_idx == 6 && pressed) {
        // Pattern 4: Modes Vertical Carousel
        g_pat4_mode_idx = (g_pat4_mode_idx + 1) % 4;
        render_pattern_4();
    } else if (key_idx == 7 && pressed) {
        // Pattern 5: Modes 2x2 Icon Grid
        g_pat5_icon_idx = (g_pat5_icon_idx + 1) % 4;
        render_pattern_5();
    } else if (key_idx == 8 && pressed) {
        // Pattern 6: Modes Horizontal Strip
        g_pat6_icon_idx = (g_pat6_icon_idx + 1) % 4;
        render_pattern_6();
    } else if (key_idx == 9 && pressed) {
        // Pattern 8: Force CPU re-poll
        g_pat8_cpu_pct = read_cpu_percent();
        render_pattern_8();
    } else if (key_idx == 10 && pressed) {
        // Pattern 9: Add spike to graph
        for (int i = 0; i < 59; i++) g_pat9_cpu_history[i] = g_pat9_cpu_history[i + 1];
        g_pat9_cpu_history[59] = 95;
        render_pattern_9();
    } else if (key_idx == 11 && pressed) {
        // Pattern 11: Increment Dial
        g_pat11_knob_val = (g_pat11_knob_val + 10) % 105;
        render_pattern_11();
        render_top_display();
    } else if (key_idx == 12 && pressed) {
        // Pattern 12: Jump Marquee Scroll
        g_pat12_scroll_px += 24;
        render_pattern_12();
        render_top_display();
    } else if (key_idx == 13 && pressed) {
        g_aux_counter++;
        render_aux_key(13);
    } else if (key_idx == 14 && pressed) {
        render_aux_key(14);
    } else if (key_idx == 15 && pressed) {
        // Reset all states
        g_pat1_toggle = 0;
        g_pat2_toggle = 1;
        g_pat3_mode_idx = 0;
        g_pat4_mode_idx = 0;
        g_pat5_icon_idx = 0;
        g_pat6_icon_idx = 0;
        g_pat11_knob_val = 50;
        render_all_keys();
        render_top_display();
    } else if (key_idx == 16 && pressed) {
        g_aux_theme = !g_aux_theme;
        render_aux_key(16);
    } else {
        render_aux_key(key_idx);
    }
}

// Handle rotary knob rotation
static void on_dial_turn(int direction) {
    // direction: +1 = Right / Clockwise, -1 = Left / Counter-Clockwise
    if (direction > 0) {
        g_pat11_knob_val += 5;
        if (g_pat11_knob_val > 100) g_pat11_knob_val = 100;
        g_pat12_scroll_px += 16;
        strncpy(g_state.dial_action, "KNOB -> RIGHT (+5%)", sizeof(g_state.dial_action) - 1);
    } else {
        g_pat11_knob_val -= 5;
        if (g_pat11_knob_val < 0) g_pat11_knob_val = 0;
        g_pat12_scroll_px -= 16;
        if (g_pat12_scroll_px < 0) g_pat12_scroll_px = 0;
        strncpy(g_state.dial_action, "KNOB <- LEFT (-5%)", sizeof(g_state.dial_action) - 1);
    }
    g_state.dial_until_ms = get_time_ms() + 1000;

    render_pattern_11();
    render_pattern_12();
    render_top_display();
}

// GD32 / QMK 8-State Byte Machine
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

                if (row == 100) {
                    on_dial_turn(-1); // Left
                } else if (row == 101) {
                    on_dial_turn(+1); // Right
                } else if (row == 102 || row == 104) {
                    // Push Click
                    strncpy(g_state.dial_action, "KNOB CLICK [PUSH]", sizeof(g_state.dial_action) - 1);
                    g_state.dial_until_ms = get_time_ms() + 1000;
                    render_top_display();
                } else if (row < 4 && col < 5) {
                    // Direct Instant Switch Contact
                    on_key_event(row, col, pressed != 0);
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
                on_key_event(r, c, p != 0);
            }
        }
    } else if (strncmp(buf, "DIAL:LEFT", 9) == 0) {
        on_dial_turn(-1);
    } else if (strncmp(buf, "DIAL:RIGHT", 10) == 0) {
        on_dial_turn(+1);
    } else if (strncmp(buf, "DIAL:", 5) == 0) {
        strncpy(g_state.dial_action, buf + 5, sizeof(g_state.dial_action) - 1);
        g_state.dial_until_ms = get_time_ms() + 1000;
        render_top_display();
    }
}

int main(int argc, char *argv[]) {
    printf("[MK20-HUD] Starting Standalone MK20 12-Pattern Showcase Engine...\n");

    strncpy(g_state.provider, "Claude Code 2.1", sizeof(g_state.provider) - 1);
    strncpy(g_state.model, "Sonnet 3.7", sizeof(g_state.model) - 1);
    strncpy(g_state.status, "ONLINE", sizeof(g_state.status) - 1);
    strncpy(g_state.message, "12 Interaction Patterns Active", sizeof(g_state.message) - 1);
    g_state.dial_until_ms = 0;

    // Initialize mock history
    for (int i = 0; i < 60; i++) {
        g_pat9_cpu_history[i] = 20 + (i % 35);
    }

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
    render_all_keys();
    render_top_display();

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

    // 5. Open UDP Socket
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

    // 6. Event Loop with Animation & Telemetry Timers
    struct pollfd pfd[2];
    pfd[0].fd = uart_fd;
    pfd[0].events = POLLIN;
    pfd[1].fd = sockfd;
    pfd[1].events = POLLIN;
    int pfd_count = (sockfd >= 0) ? 2 : 1;

    uint8_t uart_buf[128];
    char udp_buf[512];

    long long last_telemetry_ms = get_time_ms();
    long long last_anim_ms = get_time_ms();

    while (1) {
        int ret = poll(pfd, pfd_count, 33); // 30 FPS tick

        if (ret > 0) {
            // Hardware UART (GD32 MCU contact)
            if (pfd[0].revents & POLLIN) {
                int n = read(uart_fd, uart_buf, sizeof(uart_buf));
                if (n > 0) {
                    for (int i = 0; i < n; i++) {
                        parse_qmk_byte(uart_buf[i]);
                    }
                }
            }

            // UDP Packet
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

        long long now = get_time_ms();

        // 30 FPS Animation Tick (Pattern 10 Pulse & Pattern 12 Marquee)
        if (now - last_anim_ms >= 33) {
            last_anim_ms = now;

            // Pattern 10: Metronome pulse phase
            g_pat10_pulse_val = (g_pat10_pulse_val + 1) % 628;
            render_pattern_10();

            // Pattern 12: Auto-scroll pixel marquee
            g_pat12_scroll_px += 2;
            render_pattern_12();
            render_top_display();
        }

        // 1-Second Telemetry Tick (Pattern 8 CPU Number & Pattern 9 CPU Graph)
        if (now - last_telemetry_ms >= 1000) {
            last_telemetry_ms = now;

            g_pat8_cpu_pct = read_cpu_percent();
            for (int i = 0; i < 59; i++) {
                g_pat9_cpu_history[i] = g_pat9_cpu_history[i + 1];
            }
            g_pat9_cpu_history[59] = g_pat8_cpu_pct;

            render_pattern_8();
            render_pattern_9();
        }

        // Clear dial overlay if expired
        if (g_state.dial_until_ms > 0 && now >= g_state.dial_until_ms) {
            g_state.dial_until_ms = 0;
            g_state.dial_action[0] = '\0';
            render_top_display();
        }
    }

    return 0;
}
