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
#include <linux/input.h>
#include <math.h>
#include <signal.h>
#include <errno.h>

#include "v2_state.h"
#include "v2_render.h"

#undef KEY_W
#undef KEY_H
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
const uint8_t font8x16[96][16] = {
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
static int g_pat10_metronome_bpm = 120;   // Key 4: Metronome Pulse BPM (80/120/160)
static float g_pat10_phase = 0.0f;        // Key 4: Phase accumulator (0..2*PI)
static int g_pat3_mode_idx = 0;           // Key 5: Modes Vertical Uniform (0..3)
static int g_pat4_mode_idx = 0;           // Key 6: Modes Vertical Carousel (0..3)
static int g_pat5_icon_idx = 0;           // Key 7: Modes 2x2 Icon Grid (0..3)
static int g_pat6_icon_idx = 0;           // Key 8: Modes Horizontal Strip (0..3)
static int g_pat6_animating = 0;          // Key 8: Horizontal scroll animation active
static float g_pat6_anim_offset = 0.0f;   // Key 8: Pixel offset (0.0 .. 40.0)
static long long g_pat6_anim_start_ms = 0;
static int g_pat8_cpu_pct = 32;           // Key 9: Real-time Number CPU %
static int g_pat9_cpu_history[60] = {0};  // Key 10: Real-time Sparkline Graph
static int g_pat11_knob_val = 45;         // Key 11: Knob Dial Gauge (0..100)
static float g_pat11_dial_angle = 0.0f;   // Key 11: Circular Dial Panel rotation angle (radians)
static const char *g_pat11_modes[6] = {"CODE", "PLAN", "DIFF", "TEST", "EXEC", "CHAT"};
static int g_pat12_target_val = 50;        // Key 12: Smooth Vertical Value (0..100)
static float g_pat12_current_val = 50.0f;  // Key 12: Smoothly interpolated value position
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
    char scroll_action[32];
    long long scroll_until_ms;
} HudState;

static HudState g_state;
static uint8_t g_key_pressed[21] = {0};
static volatile int g_running = 1;
static uint32_t g_dirty_keys = 0;
static int g_dirty_top = 0;
static long long g_last_top_flush_ms = 0;

static void handle_signal(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        g_running = 0;
    }
}

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

static inline int get_fb_height(int stride_pixels) {
    return (stride_pixels == TOP_W) ? TOP_H : KEY_H;
}

static struct sockaddr_in g_host_addr;
static int g_has_host_addr = 0;
static long long g_last_host_sync_ms = 0;
int g_host_offline = 0;
static int g_pc_keys_on = 0;
static int g_map_reply_layer = -1, g_map_reply_row, g_map_reply_col;
static uint16_t g_map_reply_code;
static uint16_t g_saved_keymap[4][4][5];
static int g_saved_keymap_valid = 0;
static int g_left_down = 0, g_right_down = 0, g_knob_chord = 0;
static int g_knob_toggle_pending = 0;
static void apply_pc_key_mode(int fd);
static int g_sockfd = -1;

// Low-Level Drawing Primitives
void draw_char_16(uint16_t *fb, int stride_pixels, int x, int y, char c, uint16_t color, int scale) {
    if (c < 32 || c > 126) c = ' ';
    const uint8_t *glyph = font8x16[c - 32];
    int max_h = get_fb_height(stride_pixels);
    for (int row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        int px = x + col * scale + sx;
                        int py = y + row * scale + sy;
                        if (px >= 0 && px < stride_pixels && py >= 0 && py < max_h) {
                            fb[py * stride_pixels + px] = color;
                        }
                    }
                }
            }
        }
    }
}

#include "unicode_text.h"
void draw_string_16(uint16_t *fb, int stride_pixels, int x, int y, const char *str, uint16_t color, int scale) {
    for (const unsigned char *p = (const unsigned char *)str; *p; p++) {
        if (*p >= 128) { if (unicode_draw(fb, stride_pixels, x, y, str, color, scale)) return; break; }
    }
    int cur_x = x;
    while (*str) {
        unsigned cp;
        str += unicode_step(str, &cp);
        // A missing font must be visible, not silently rendered as blank UTF-8 bytes.
        draw_char_16(fb, stride_pixels, cur_x, y, cp < 128 ? (char)cp : '?', color, scale);
        cur_x += (cp < 128 ? 8 : 16) * scale;
    }
}

void draw_string_clipped_16(uint16_t *fb, int stride_pixels, int x, int y, const char *str, uint16_t color, int scale, int clip_x, int clip_y, int clip_w, int clip_h) {
    for (const unsigned char *p = (const unsigned char *)str; *p; p++) {
        if (*p >= 128 && unicode_draw_clipped(fb, stride_pixels, x, y, str, color, scale, clip_x, clip_y, clip_w, clip_h)) return;
    }
    int cur_x = x;
    int max_h = get_fb_height(stride_pixels);
    while (*str) {
        unsigned codepoint;
        str += unicode_step(str, &codepoint);
        char c = codepoint < 128 ? (char)codepoint : '?';
        if (c < 32 || c > 126) c = ' ';
        const uint8_t *glyph = font8x16[c - 32];
        for (int row = 0; row < 16; row++) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < 8; col++) {
                if (bits & (0x80 >> col)) {
                    for (int sy = 0; sy < scale; sy++) {
                        for (int sx = 0; sx < scale; sx++) {
                            int px = cur_x + col * scale + sx;
                            int py = y + row * scale + sy;
                            if (px >= clip_x && px < (clip_x + clip_w) &&
                                py >= clip_y && py < (clip_y + clip_h) &&
                                px >= 0 && px < stride_pixels && py >= 0 && py < max_h) {
                                fb[py * stride_pixels + px] = color;
                            }
                        }
                    }
                }
            }
        }
        cur_x += (codepoint < 128 ? 8 : 16) * scale;
    }
}

void draw_string_centered_16(uint16_t *fb, int stride_pixels, int y, const char *str, uint16_t color, int scale) {
    int x = (KEY_W - unicode_width(str) * scale) / 2;
    if (x < 2) x = 2;
    draw_string_16(fb, stride_pixels, x, y, str, color, scale);
}

static void draw_badge_right_16(uint16_t *fb, int stride_pixels, int y, const char *str, uint16_t color) {
    int len = strlen(str);
    int x = KEY_W - 6 - len * 8;
    if (x < 0) x = 0;
    draw_string_16(fb, stride_pixels, x, y, str, color, 1);
}

void draw_rect_16(uint16_t *fb, int stride_pixels, int x, int y, int w, int h, uint16_t color) {
    int max_h = get_fb_height(stride_pixels);
    for (int dy = 0; dy < h; dy++) {
        int py = y + dy;
        if (py < 0 || py >= max_h) continue;
        for (int dx = 0; dx < w; dx++) {
            int px = x + dx;
            if (px >= 0 && px < stride_pixels) {
                fb[py * stride_pixels + px] = color;
            }
        }
    }
}

void draw_gradient_rect_16(uint16_t *fb, int stride_pixels, int x, int y, int w, int h,
                            uint16_t c_start, uint16_t c_end, V2_GradientType type) {
    if (!fb || w <= 0 || h <= 0) return;
    if (type == GRADIENT_NONE || c_start == c_end) {
        draw_rect_16(fb, stride_pixels, x, y, w, h, c_start);
        return;
    }
    int max_h = get_fb_height(stride_pixels);
    int max_diag = (w - 1) + (h - 1);
    if (max_diag <= 0) max_diag = 1;

    for (int dy = 0; dy < h; dy++) {
        int py = y + dy;
        if (py < 0 || py >= max_h) continue;

        if (type == GRADIENT_VERTICAL) {
            uint16_t col = interpolate_rgb565(c_start, c_end, dy, h > 1 ? h - 1 : 1);
            for (int dx = 0; dx < w; dx++) {
                int px = x + dx;
                if (px >= 0 && px < stride_pixels) {
                    fb[py * stride_pixels + px] = col;
                }
            }
            continue;
        }

        for (int dx = 0; dx < w; dx++) {
            int px = x + dx;
            if (px < 0 || px >= stride_pixels) continue;
            uint16_t col;
            switch (type) {
                case GRADIENT_HORIZONTAL:
                    col = interpolate_rgb565(c_start, c_end, dx, w > 1 ? w - 1 : 1);
                    break;
                case GRADIENT_LT_TO_RB:
                    col = interpolate_rgb565(c_start, c_end, dx + dy, max_diag);
                    break;
                case GRADIENT_RT_TO_LB:
                    col = interpolate_rgb565(c_start, c_end, (w - 1 - dx) + dy, max_diag);
                    break;
                default:
                    col = c_start;
                    break;
            }
            fb[py * stride_pixels + px] = col;
        }
    }
}

void draw_border_16(uint16_t *fb, int stride_pixels, int x, int y, int w, int h, int thick, uint16_t color) {
    draw_rect_16(fb, stride_pixels, x, y, w, thick, color);
    draw_rect_16(fb, stride_pixels, x, y + h - thick, w, thick, color);
    draw_rect_16(fb, stride_pixels, x, y, thick, h, color);
    draw_rect_16(fb, stride_pixels, x + w - thick, y, thick, h, color);
}

void draw_round_rect_16(uint16_t *fb, int stride_pixels, int x, int y, int w, int h, int r, uint16_t color) {
    if (!fb || w <= 0 || h <= 0) return;
    int max_h = get_fb_height(stride_pixels);
    int r2_x4 = 4 * r * r;

    for (int dy = 0; dy < h; dy++) {
        int py = y + dy;
        if (py < 0 || py >= max_h) continue;

        int row_offset = py * stride_pixels;
        for (int dx = 0; dx < w; dx++) {
            int px = x + dx;
            if (px < 0 || px >= stride_pixels) continue;

            int inside = 1;
            if (dx < r && dy < r) {
                int kx = 2 * r - 2 * dx - 1;
                int ky = 2 * r - 2 * dy - 1;
                inside = (kx * kx + ky * ky) <= r2_x4;
            } else if (dx >= w - r && dy < r) {
                int kx = 2 * (dx - (w - r)) + 1;
                int ky = 2 * r - 2 * dy - 1;
                inside = (kx * kx + ky * ky) <= r2_x4;
            } else if (dx < r && dy >= h - r) {
                int kx = 2 * r - 2 * dx - 1;
                int ky = 2 * (dy - (h - r)) + 1;
                inside = (kx * kx + ky * ky) <= r2_x4;
            } else if (dx >= w - r && dy >= h - r) {
                int kx = 2 * (dx - (w - r)) + 1;
                int ky = 2 * (dy - (h - r)) + 1;
                inside = (kx * kx + ky * ky) <= r2_x4;
            }

            if (inside) {
                fb[row_offset + px] = color;
            } else {
                fb[row_offset + px] = COLOR_BLACK;
            }
        }
    }
}

void draw_gradient_round_rect_16(uint16_t *fb, int stride_pixels, int x, int y, int w, int h, int r,
                                uint16_t c_start, uint16_t c_end, V2_GradientType type) {
    if (!fb || w <= 0 || h <= 0) return;
    if (type == GRADIENT_NONE || c_start == c_end) {
        draw_round_rect_16(fb, stride_pixels, x, y, w, h, r, c_start);
        return;
    }
    int max_h = get_fb_height(stride_pixels);
    int max_diag = (w - 1) + (h - 1);
    if (max_diag <= 0) max_diag = 1;
    int r2_x4 = 4 * r * r;

    for (int dy = 0; dy < h; dy++) {
        int py = y + dy;
        if (py < 0 || py >= max_h) continue;

        int row_offset = py * stride_pixels;
        uint16_t vert_col = (type == GRADIENT_VERTICAL)
            ? interpolate_rgb565(c_start, c_end, dy, h > 1 ? h - 1 : 1)
            : 0;

        for (int dx = 0; dx < w; dx++) {
            int px = x + dx;
            if (px < 0 || px >= stride_pixels) continue;

            int inside = 1;
            if (dx < r && dy < r) {
                int kx = 2 * r - 2 * dx - 1;
                int ky = 2 * r - 2 * dy - 1;
                inside = (kx * kx + ky * ky) <= r2_x4;
            } else if (dx >= w - r && dy < r) {
                int kx = 2 * (dx - (w - r)) + 1;
                int ky = 2 * r - 2 * dy - 1;
                inside = (kx * kx + ky * ky) <= r2_x4;
            } else if (dx < r && dy >= h - r) {
                int kx = 2 * r - 2 * dx - 1;
                int ky = 2 * (dy - (h - r)) + 1;
                inside = (kx * kx + ky * ky) <= r2_x4;
            } else if (dx >= w - r && dy >= h - r) {
                int kx = 2 * (dx - (w - r)) + 1;
                int ky = 2 * (dy - (h - r)) + 1;
                inside = (kx * kx + ky * ky) <= r2_x4;
            }

            if (!inside) {
                fb[row_offset + px] = COLOR_BLACK;
                continue;
            }

            uint16_t col;
            switch (type) {
                case GRADIENT_VERTICAL:
                    col = vert_col;
                    break;
                case GRADIENT_HORIZONTAL:
                    col = interpolate_rgb565(c_start, c_end, dx, w > 1 ? w - 1 : 1);
                    break;
                case GRADIENT_LT_TO_RB:
                    col = interpolate_rgb565(c_start, c_end, dx + dy, max_diag);
                    break;
                case GRADIENT_RT_TO_LB:
                    col = interpolate_rgb565(c_start, c_end, (w - 1 - dx) + dy, max_diag);
                    break;
                default:
                    col = c_start;
                    break;
            }
            fb[row_offset + px] = col;
        }
    }
}

void draw_round_border_16(uint16_t *fb, int stride_pixels, int x, int y, int w, int h, int r, int thick, uint16_t color) {
    if (!fb || w <= 0 || h <= 0 || thick <= 0) return;
    int max_h = get_fb_height(stride_pixels);
    int r2_x4 = 4 * r * r;
    int inner_r = (r > thick) ? (r - thick) : 0;
    int inner_r2_x4 = 4 * inner_r * inner_r;

    for (int dy = 0; dy < h; dy++) {
        int py = y + dy;
        if (py < 0 || py >= max_h) continue;

        int row_offset = py * stride_pixels;
        for (int dx = 0; dx < w; dx++) {
            int px = x + dx;
            if (px < 0 || px >= stride_pixels) continue;

            int inside_outer = 1;
            if (dx < r && dy < r) {
                int kx = 2 * r - 2 * dx - 1;
                int ky = 2 * r - 2 * dy - 1;
                inside_outer = (kx * kx + ky * ky) <= r2_x4;
            } else if (dx >= w - r && dy < r) {
                int kx = 2 * (dx - (w - r)) + 1;
                int ky = 2 * r - 2 * dy - 1;
                inside_outer = (kx * kx + ky * ky) <= r2_x4;
            } else if (dx < r && dy >= h - r) {
                int kx = 2 * r - 2 * dx - 1;
                int ky = 2 * (dy - (h - r)) + 1;
                inside_outer = (kx * kx + ky * ky) <= r2_x4;
            } else if (dx >= w - r && dy >= h - r) {
                int kx = 2 * (dx - (w - r)) + 1;
                int ky = 2 * (dy - (h - r)) + 1;
                inside_outer = (kx * kx + ky * ky) <= r2_x4;
            }

            if (!inside_outer) continue;

            int in_border = 0;
            if (dx < thick || dx >= w - thick || dy < thick || dy >= h - thick) {
                in_border = 1;
            } else if (inner_r > 0) {
                int idx = dx - thick;
                int idy = dy - thick;
                int iw = w - 2 * thick;
                int ih = h - 2 * thick;
                if (idx < inner_r && idy < inner_r) {
                    int kx = 2 * inner_r - 2 * idx - 1;
                    int ky = 2 * inner_r - 2 * idy - 1;
                    if ((kx * kx + ky * ky) > inner_r2_x4) in_border = 1;
                } else if (idx >= iw - inner_r && idy < inner_r) {
                    int kx = 2 * (idx - (iw - inner_r)) + 1;
                    int ky = 2 * inner_r - 2 * idy - 1;
                    if ((kx * kx + ky * ky) > inner_r2_x4) in_border = 1;
                } else if (idx < inner_r && idy >= ih - inner_r) {
                    int kx = 2 * inner_r - 2 * idx - 1;
                    int ky = 2 * (idy - (ih - inner_r)) + 1;
                    if ((kx * kx + ky * ky) > inner_r2_x4) in_border = 1;
                } else if (idx >= iw - inner_r && idy >= ih - inner_r) {
                    int kx = 2 * (idx - (iw - inner_r)) + 1;
                    int ky = 2 * (idy - (ih - inner_r)) + 1;
                    if ((kx * kx + ky * ky) > inner_r2_x4) in_border = 1;
                }
            }

            if (in_border) {
                fb[row_offset + px] = color;
            }
        }
    }
}

void draw_title_bar_16(uint16_t *fb, int stride_pixels, int w, int title_h, int r,
                       uint16_t fill_color, uint16_t line_color, int has_fill, int has_line) {
    if (!fb || w <= 0 || title_h <= 0) return;
    int max_h = get_fb_height(stride_pixels);
    int r2_x4 = 4 * r * r;

    for (int dy = 0; dy < title_h; dy++) {
        if (dy >= max_h) break;
        int row_offset = dy * stride_pixels;
        for (int dx = 0; dx < w; dx++) {
            if (dx >= stride_pixels) break;

            int inside = 1;
            if (dx < r && dy < r) {
                int kx = 2 * r - 2 * dx - 1;
                int ky = 2 * r - 2 * dy - 1;
                inside = (kx * kx + ky * ky) <= r2_x4;
            } else if (dx >= w - r && dy < r) {
                int kx = 2 * (dx - (w - r)) + 1;
                int ky = 2 * r - 2 * dy - 1;
                inside = (kx * kx + ky * ky) <= r2_x4;
            }

            if (!inside) {
                fb[row_offset + dx] = COLOR_BLACK;
            } else if (has_fill) {
                fb[row_offset + dx] = fill_color;
            }
        }
    }

    if (has_line && title_h > 0 && title_h <= max_h) {
        int line_y = title_h - 1;
        int row_offset = line_y * stride_pixels;
        for (int dx = 0; dx < w; dx++) {
            if (dx < stride_pixels) {
                fb[row_offset + dx] = line_color;
            }
        }
    }
}

static void draw_icon12x12(uint16_t *fb, int stride_pixels, int x, int y, const uint16_t *icon, uint16_t color, int scale) {
    int max_h = get_fb_height(stride_pixels);
    for (int r = 0; r < 12; r++) {
        uint16_t bits = icon[r];
        for (int c = 0; c < 12; c++) {
            if (bits & (0x800 >> c)) {
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        int px = x + c * scale + sx;
                        int py = y + r * scale + sy;
                        if (px >= 0 && px < stride_pixels && py >= 0 && py < max_h) {
                            fb[py * stride_pixels + px] = color;
                        }
                    }
                }
            }
        }
    }
}

static void draw_icon12x12_clipped(uint16_t *fb, int stride_pixels, int x, int y, const uint16_t *icon, uint16_t color, int scale, int clip_x, int clip_y, int clip_w, int clip_h) {
    int max_h = get_fb_height(stride_pixels);
    for (int r = 0; r < 12; r++) {
        uint16_t bits = icon[r];
        for (int c = 0; c < 12; c++) {
            if (bits & (0x800 >> c)) {
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        int px = x + c * scale + sx;
                        int py = y + r * scale + sy;
                        if (px >= clip_x && px < (clip_x + clip_w) &&
                            py >= clip_y && py < (clip_y + clip_h) &&
                            px >= 0 && px < stride_pixels && py >= 0 && py < max_h) {
                            fb[py * stride_pixels + px] = color;
                        }
                    }
                }
            }
        }
    }
}

void draw_line_16(uint16_t *fb, int stride_pixels, int x0, int y0, int x1, int y1, uint16_t color) {
    int max_h = get_fb_height(stride_pixels);
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;

    while (1) {
        if (x0 >= 0 && x0 < stride_pixels && y0 >= 0 && y0 < max_h) {
            fb[y0 * stride_pixels + x0] = color;
        }
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void draw_circle_16(uint16_t *fb, int stride_pixels, int cx, int cy, int r, uint16_t color) {
    int max_h = get_fb_height(stride_pixels);
    int x = 0, y = r;
    int d = 3 - 2 * r;

    while (y >= x) {
        int pts[8][2] = {
            {cx + x, cy + y}, {cx - x, cy + y}, {cx + x, cy - y}, {cx - x, cy - y},
            {cx + y, cy + x}, {cx - y, cy + x}, {cx + y, cy - x}, {cx - y, cy - x}
        };
        for (int i = 0; i < 8; i++) {
            int px = pts[i][0], py = pts[i][1];
            if (px >= 0 && px < stride_pixels && py >= 0 && py < max_h) {
                fb[py * stride_pixels + px] = color;
            }
        }
        if (d < 0) {
            d += 4 * x + 6;
        } else {
            d += 4 * (x - y) + 10;
            y--;
        }
        x++;
    }
}

static void draw_circle_thick_16(uint16_t *fb, int stride_pixels, int cx, int cy, int r, int thick, uint16_t color) {
    for (int t = 0; t < thick; t++) {
        draw_circle_16(fb, stride_pixels, cx, cy, r - t, color);
    }
}

static void draw_filled_circle_16(uint16_t *fb, int stride_pixels, int cx, int cy, int r, uint16_t color) {
    int max_h = get_fb_height(stride_pixels);
    int r2 = r * r;
    for (int dy = -r; dy <= r; dy++) {
        int py = cy + dy;
        if (py < 0 || py >= max_h) continue;
        int dx_max = (int)sqrtf((float)(r2 - dy * dy));
        int x_start = cx - dx_max;
        int x_end = cx + dx_max;
        if (x_start < 0) x_start = 0;
        if (x_end >= stride_pixels) x_end = stride_pixels - 1;
        for (int px = x_start; px <= x_end; px++) {
            fb[py * stride_pixels + px] = color;
        }
    }
}

static void draw_triangle_down_16(uint16_t *fb, int stride_pixels, int tip_x, int tip_y, int base_w, int h, uint16_t color) {
    int max_h = get_fb_height(stride_pixels);
    for (int dy = 0; dy <= h; dy++) {
        int y = tip_y - h + dy;
        int half_w = (base_w * (h - dy)) / (2 * h);
        int x0 = tip_x - half_w;
        int x1 = tip_x + half_w;
        for (int x = x0; x <= x1; x++) {
            if (x >= 0 && x < stride_pixels && y >= 0 && y < max_h) {
                fb[y * stride_pixels + x] = color;
            }
        }
    }
}

static void draw_char_rotated_16(uint16_t *fb, int stride_pixels, int cx, int cy, char c, float angle_rad, uint16_t color) {
    if (c < 32 || c > 126) c = ' ';
    const uint8_t *glyph = font8x16[c - 32];
    int max_h = get_fb_height(stride_pixels);
    float cos_a = cosf(angle_rad);
    float sin_a = sinf(angle_rad);

    static const float sub_dx[4] = { 0.0f, 0.5f, 0.0f, 0.5f };
    static const float sub_dy[4] = { 0.0f, 0.0f, 0.5f, 0.5f };

    for (int gy = 0; gy < 16; gy++) {
        uint8_t bits = glyph[gy];
        if (!bits) continue;
        for (int gx = 0; gx < 8; gx++) {
            if (bits & (0x80 >> gx)) {
                for (int s = 0; s < 4; s++) {
                    float lx = (float)(gx - 4) + sub_dx[s];
                    float ly = (float)(gy - 8) + sub_dy[s];
                    int px = cx + (int)roundf(lx * cos_a - ly * sin_a);
                    int py = cy + (int)roundf(lx * sin_a + ly * cos_a);
                    if (px >= 0 && px < stride_pixels && py >= 0 && py < max_h) {
                        fb[py * stride_pixels + px] = color;
                    }
                }
            }
        }
    }
}

static void draw_string_rotated_16(uint16_t *fb, int stride_pixels, int cx, int cy, float radius, float angle_center, const char *str, uint16_t color) {
    int len = strlen(str);
    float tangent_angle = angle_center + (float)(M_PI / 2.0);
    float cos_t = cosf(tangent_angle);
    float sin_t = sinf(tangent_angle);

    float mid_x = cx + radius * cosf(angle_center);
    float mid_y = cy + radius * sinf(angle_center);

    float char_pitch = 8.0f;
    for (int i = 0; i < len; i++) {
        float offset = (i - (len - 1) * 0.5f) * char_pitch;
        int ch_x = (int)roundf(mid_x + offset * cos_t);
        int ch_y = (int)roundf(mid_y + offset * sin_t);
        draw_char_rotated_16(fb, stride_pixels, ch_x, ch_y, str[i], tangent_angle, color);
    }
}

static int get_active_dial_mode(void) {
    float best_diff = 1000.0f;
    int best_idx = 0;
    float target = -(float)(M_PI / 2.0); // Top pointer at -90 degrees

    for (int i = 0; i < 6; i++) {
        float angle = target - i * (float)(M_PI / 3.0) + g_pat11_dial_angle;
        float diff = fmodf(angle - target + 3.0f * (float)M_PI, 2.0f * (float)M_PI) - (float)M_PI;
        if (fabsf(diff) < best_diff) {
            best_diff = fabsf(diff);
            best_idx = i;
        }
    }
    return best_idx;
}

// Draw a precision vertical value scrolling reel with smooth fractional offset and ruler ticks
static void draw_vertical_value_reel(uint16_t *fb, int stride_pixels,
                                    int vx, int vy, int vw, int vh,
                                    float current_val, int target_val,
                                    uint16_t border_col, uint16_t highlight_col) {
    int max_h = get_fb_height(stride_pixels);
    if (vy + vh > max_h) vh = max_h - vy;

    // 1. Viewport Background & Frame
    draw_rect_16(fb, stride_pixels, vx, vy, vw, vh, COLOR_BG);
    draw_border_16(fb, stride_pixels, vx, vy, vw, vh, 1, border_col);

    int cy = vy + vh / 2;
    int item_pitch = 20;
    int band_h = 20;

    // 2. Active Center Selection Band
    draw_rect_16(fb, stride_pixels, vx + 2, cy - band_h / 2, vw - 4, band_h, COLOR_INDIGO_BG);
    draw_border_16(fb, stride_pixels, vx + 2, cy - band_h / 2, vw - 4, band_h, 1, highlight_col);

    // Pointer Needles flanking the center
    draw_string_16(fb, stride_pixels, vx + 4, cy - 8, ">", highlight_col, 1);
    draw_string_16(fb, stride_pixels, vx + vw - 12, cy - 8, "<", highlight_col, 1);

    // 3. Visible Range of Integer Values
    int min_k = (int)floorf(current_val) - 4;
    int max_k = (int)ceilf(current_val) + 4;

    for (int k = min_k; k <= max_k; k++) {
        if (k < 0 || k > 100) continue;

        // When current_val increases, values scroll UP
        float delta = (float)k - current_val;
        int y_pos = cy + (int)roundf(delta * (float)item_pitch) - 8;

        // Quick culling
        if (y_pos + 16 < vy || y_pos > vy + vh) continue;

        float dist = fabsf(delta);
        uint16_t num_color;
        if (dist < 0.5f) {
            num_color = COLOR_WHITE;
        } else if (dist < 1.5f) {
            num_color = highlight_col;
        } else {
            num_color = COLOR_TEXT_DIM;
        }

        char num_str[8];
        snprintf(num_str, sizeof(num_str), "%02d", k);
        int num_len_px = strlen(num_str) * 8;
        int num_x = vx + (vw - num_len_px) / 2;

        draw_string_clipped_16(fb, stride_pixels, num_x, y_pos, num_str, num_color, 1,
                              vx + 2, vy + 2, vw - 4, vh - 4);

        // Ruler Ticks on Left & Right
        int tick_y = cy + (int)roundf(delta * (float)item_pitch);
        if (tick_y >= vy + 2 && tick_y <= vy + vh - 2) {
            int is_major = (k % 5 == 0);
            int tick_len = is_major ? 6 : 3;
            uint16_t tick_c = is_major ? highlight_col : COLOR_DARK_GRAY;

            // Left tick
            draw_line_16(fb, stride_pixels, vx + 14, tick_y, vx + 14 + tick_len, tick_y, tick_c);
            // Right tick
            draw_line_16(fb, stride_pixels, vx + vw - 14 - tick_len, tick_y, vx + vw - 14, tick_y, tick_c);
        }
    }
}

// Flush Key Framebuffer over SPI bus immediately
static void flush_key(int key_idx) {
    if (key_idx >= 1 && key_idx <= 20 && g_key_fds[key_idx] >= 0 && g_key_fbs[key_idx]) {
        msync(g_key_fbs[key_idx], KEY_FB_BYTES, MS_ASYNC);
        off_t pos = lseek(g_key_fds[key_idx], 0, SEEK_SET);
        if (pos == (off_t)-1) return;
        size_t total = 0;
        uint8_t *ptr = (uint8_t *)g_key_fbs[key_idx];
        while (total < KEY_FB_BYTES) {
            ssize_t n = write(g_key_fds[key_idx], ptr + total, KEY_FB_BYTES - total);
            if (n > 0) {
                total += n;
            } else if (n < 0 && (errno == EINTR || errno == EAGAIN)) {
                continue;
            } else {
                break;
            }
        }
    }
}

static void flush_top(void) {
    if (g_top_fd >= 0 && g_top_fb) {
        msync(g_top_fb, TOP_FB_BYTES, MS_ASYNC);
        off_t pos = lseek(g_top_fd, 0, SEEK_SET);
        if (pos == (off_t)-1) return;
        size_t total = 0;
        uint8_t *ptr = (uint8_t *)g_top_fb;
        while (total < TOP_FB_BYTES) {
            ssize_t n = write(g_top_fd, ptr + total, TOP_FB_BYTES - total);
            if (n > 0) {
                total += n;
            } else if (n < 0 && (errno == EINTR || errno == EAGAIN)) {
                continue;
            } else {
                break;
            }
        }
    }
}

// =========================================================================
// PATTERN 1: Toggle (Same Text, Different Backgrounds) -> Key 1
// =========================================================================
static void render_pattern_1(void) {
    uint16_t *fb = g_key_fbs[1];
    if (!fb) return;

    int is_pressed = g_key_pressed[1];
    uint16_t bg = g_pat1_toggle ? COLOR_EMERALD : COLOR_CARD;
    uint16_t border = is_pressed ? COLOR_WHITE : (g_pat1_toggle ? COLOR_WHITE : COLOR_EMERALD);
    uint16_t text_col = g_pat1_toggle ? COLOR_BLACK : COLOR_WHITE;
    uint16_t badge_col = g_pat1_toggle ? COLOR_BLACK : COLOR_EMERALD;
    int border_thick = is_pressed ? 3 : (g_pat1_toggle ? 3 : 2);

    draw_rect_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, bg);
    draw_border_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, border_thick, border);

    draw_string_16(fb, KEY_W, 6, 6, "#01", badge_col, 1);
    draw_badge_right_16(fb, KEY_W, 6, "TOGGLE", badge_col);

    // Same Text always: MUTE
    draw_string_16(fb, KEY_W, 32, 44, "MUTE", text_col, 2);

    // Subtitle indicator
    const char *stateStr = g_pat1_toggle ? "[ ACTIVE ]" : "[INACTIVE]";
    draw_string_centered_16(fb, KEY_W, 86, stateStr, text_col, 1);

    flush_key(1);
}

// =========================================================================
// PATTERN 2: Toggle (Different Text, Different Backgrounds) -> Key 2
// =========================================================================
static void render_pattern_2(void) {
    uint16_t *fb = g_key_fbs[2];
    if (!fb) return;

    int is_pressed = g_key_pressed[2];
    uint16_t bg = g_pat2_toggle ? COLOR_EMERALD_BG : COLOR_ROSE_BG;
    uint16_t border = is_pressed ? COLOR_WHITE : (g_pat2_toggle ? COLOR_EMERALD : COLOR_ROSE);
    uint16_t text_col = COLOR_WHITE;
    const char *label = g_pat2_toggle ? "MIC ON" : "MIC OFF";
    const char *sublabel = g_pat2_toggle ? "[UNMUTED]" : "[ MUTED ]";
    int border_thick = is_pressed ? 3 : 2;

    draw_rect_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, bg);
    draw_border_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, border_thick, border);

    draw_string_16(fb, KEY_W, 6, 6, "#02", border, 1);
    draw_badge_right_16(fb, KEY_W, 6, "ON/OFF", border);

    // Different Text
    draw_string_centered_16(fb, KEY_W, 44, label, text_col, 2);
    draw_string_centered_16(fb, KEY_W, 86, sublabel, border, 1);

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
    uint16_t badge_col = g_pat7_shift ? COLOR_BLACK : COLOR_PURPLE;

    draw_rect_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, bg);
    draw_border_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, g_pat7_shift ? 3 : 2, border);

    draw_string_16(fb, KEY_W, 6, 6, "#03", badge_col, 1);
    draw_badge_right_16(fb, KEY_W, 6, "SHIFT", badge_col);

    draw_string_centered_16(fb, KEY_W, 44, "TURBO", text_col, 2);
    const char *stateStr = g_pat7_shift ? "<HOLDING>" : "[PUSH/HOLD]";
    draw_string_centered_16(fb, KEY_W, 86, stateStr, text_col, 1);

    flush_key(3);
}

// =========================================================================
// PATTERN 10: Pulse-style Effect (Metronome / Heartbeat Animation) -> Key 4
// =========================================================================
static void render_pattern_10(void) {
    uint16_t *fb = g_key_fbs[4];
    if (!fb) return;

    int is_pressed = g_key_pressed[4];
    float s = sinf(g_pat10_phase);
    float pulse = (s > 0.0f) ? (s * s) : 0.0f; // Sharp beat at exact BPM
    uint16_t bg = (pulse > 0.45f) ? COLOR_ROSE_BG : COLOR_CARD;
    uint16_t heart_col = is_pressed ? COLOR_WHITE : ((pulse > 0.45f) ? COLOR_WHITE : COLOR_ROSE);
    int border_thick = is_pressed ? 3 : (int)(2 + pulse * 2.0f);

    draw_rect_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, bg);
    draw_border_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, border_thick, heart_col);

    draw_string_16(fb, KEY_W, 6, 6, "#04", heart_col, 1);
    draw_badge_right_16(fb, KEY_W, 6, "PULSE", heart_col);

    int scale = (pulse > 0.6f) ? 3 : 2;
    int hx = (KEY_W - 12 * scale) / 2;
    int hy = 36;
    draw_icon12x12(fb, KEY_W, hx, hy, icon12x12_heart, heart_col, scale);

    char bpmStr[16];
    snprintf(bpmStr, sizeof(bpmStr), "BPM %d", g_pat10_metronome_bpm);
    draw_string_centered_16(fb, KEY_W, 86, bpmStr, COLOR_WHITE, 1);

    flush_key(4);
}

// =========================================================================
// PATTERN 3: Modes (Vertical uniform multiline, Cursor highlight) -> Key 5
// =========================================================================
static void render_pattern_3(void) {
    uint16_t *fb = g_key_fbs[5];
    if (!fb) return;

    static const char *modes[4] = {"PROD", "STAG", "DEV ", "LOCL"};
    int is_pressed = g_key_pressed[5];

    draw_rect_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, COLOR_CARD);
    draw_border_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, is_pressed ? 3 : 2, is_pressed ? COLOR_WHITE : COLOR_CYAN);

    draw_string_16(fb, KEY_W, 6, 6, "#05", COLOR_CYAN, 1);
    draw_badge_right_16(fb, KEY_W, 6, "MODES", COLOR_CYAN);

    for (int i = 0; i < 4; i++) {
        int y = 24 + i * 21;
        if (i == g_pat3_mode_idx) {
            draw_rect_16(fb, KEY_W, 4, y - 2, KEY_W - 8, 19, COLOR_CYAN_BG);
            draw_border_16(fb, KEY_W, 4, y - 2, KEY_W - 8, 19, 1, COLOR_CYAN);
            draw_string_16(fb, KEY_W, 8, y, ">", COLOR_CYAN, 1);
            draw_string_16(fb, KEY_W, 22, y, modes[i], COLOR_WHITE, 1);
            draw_string_16(fb, KEY_W, KEY_W - 16, y, "*", COLOR_CYAN, 1);
        } else {
            draw_string_16(fb, KEY_W, 22, y, modes[i], COLOR_TEXT_DIM, 1);
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
    int is_pressed = g_key_pressed[6];

    draw_rect_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, COLOR_CARD);
    draw_border_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, is_pressed ? 3 : 2, is_pressed ? COLOR_WHITE : COLOR_AMBER);

    draw_string_16(fb, KEY_W, 6, 6, "#06", COLOR_AMBER, 1);
    draw_badge_right_16(fb, KEY_W, 6, "WHEEL", COLOR_AMBER);

    // Small Prev at top
    draw_string_centered_16(fb, KEY_W, 24, modes[prev], COLOR_TEXT_DIM, 1);

    // Large Centered Selected with highlighted card
    draw_rect_16(fb, KEY_W, 6, 44, KEY_W - 12, 34, COLOR_AMBER_BG);
    draw_border_16(fb, KEY_W, 6, 44, KEY_W - 12, 34, 2, COLOR_AMBER);
    draw_string_centered_16(fb, KEY_W, 46, modes[cur], COLOR_WHITE, 2);

    // Small Next at bottom
    draw_string_centered_16(fb, KEY_W, 86, modes[next], COLOR_TEXT_DIM, 1);

    flush_key(6);
}

// =========================================================================
// PATTERN 5: Modes (2x2 Icon Matrix, Active Quadrant Highlight) -> Key 7
// =========================================================================
static void render_pattern_5(void) {
    uint16_t *fb = g_key_fbs[7];
    if (!fb) return;

    int is_pressed = g_key_pressed[7];

    draw_rect_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, COLOR_CARD);
    draw_border_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, is_pressed ? 3 : 2, is_pressed ? COLOR_WHITE : COLOR_INDIGO);

    draw_string_16(fb, KEY_W, 6, 6, "#07", COLOR_INDIGO, 1);
    draw_badge_right_16(fb, KEY_W, 6, "2x2", COLOR_INDIGO);

    // 4 Quadrants
    int qw = 54, qh = 42;
    int qx[4] = {6, 68, 6, 68};
    int qy[4] = {24, 24, 70, 70};
    const uint16_t *icons[4] = {icon12x12_wifi, icon12x12_ble, icon12x12_usb, icon12x12_eth};
    const char *names[4] = {"WIFI", "BLE", "USB", "ETH"};

    for (int i = 0; i < 4; i++) {
        int active = (i == g_pat5_icon_idx);
        uint16_t qbg = active ? COLOR_INDIGO_BG : COLOR_BG;
        uint16_t qborder = active ? COLOR_INDIGO : COLOR_CARD_BORDER;
        uint16_t qcol = active ? COLOR_WHITE : COLOR_TEXT_DIM;

        draw_rect_16(fb, KEY_W, qx[i], qy[i], qw, qh, qbg);
        draw_border_16(fb, KEY_W, qx[i], qy[i], qw, qh, active ? 2 : 1, qborder);

        draw_icon12x12(fb, KEY_W, qx[i] + (qw - 12)/2, qy[i] + 5, icons[i], qcol, 1);
        int name_x = qx[i] + (qw - strlen(names[i])*8)/2;
        draw_string_16(fb, KEY_W, name_x, qy[i] + 21, names[i], qcol, 1);
    }

    flush_key(7);
}

// =========================================================================
// PATTERN 6: Modes (Horizontal Strip: Right-Scrolling Animation, Higher Z-Index Center) -> Key 8
// =========================================================================
static void render_pattern_6(void) {
    uint16_t *fb = g_key_fbs[8];
    if (!fb) return;

    static const uint16_t *icons[4] = {icon12x12_cpu, icon12x12_ram, icon12x12_wifi, icon12x12_eth};
    static const char *names[4] = {"CPU", "MEM", "WIFI", "LAN"};

    int cur = g_pat6_icon_idx;
    int left = (cur + 3) % 4;
    int right = (cur + 1) % 4;
    int far_left = (cur + 2) % 4;
    int is_pressed = g_key_pressed[8];
    float dx = g_pat6_anim_offset; // 0.0 .. 40.0

    draw_rect_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, COLOR_CARD);
    draw_border_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, is_pressed ? 3 : 2, is_pressed ? COLOR_WHITE : COLOR_EMERALD);

    draw_string_16(fb, KEY_W, 6, 6, "#08", COLOR_EMERALD, 1);
    draw_badge_right_16(fb, KEY_W, 6, "SCROLL", COLOR_EMERALD);

    int iy = 38; // 24x24 icon vertically centered in y: 24..78

    // 1. Left Zone Layer (x: 4..44, width 40, height 54)
    draw_rect_16(fb, KEY_W, 4, 24, 40, 54, COLOR_BG);
    draw_border_16(fb, KEY_W, 4, 24, 40, 54, 1, COLOR_CARD_BORDER);
    // Two icons in left zone scrolling left-to-right:
    int far_left_x = -28 + (int)roundf(dx);
    int left_x = 12 + (int)roundf(dx);
    draw_icon12x12_clipped(fb, KEY_W, far_left_x, iy, icons[far_left], COLOR_DARK_GRAY, 2, 5, 25, 38, 52);
    draw_icon12x12_clipped(fb, KEY_W, left_x, iy, icons[left], COLOR_TEXT_DIM, 2, 5, 25, 38, 52);

    // 2. Right Zone Layer (x: 84..124, width 40, height 54)
    draw_rect_16(fb, KEY_W, 84, 24, 40, 54, COLOR_BG);
    draw_border_16(fb, KEY_W, 84, 24, 40, 54, 1, COLOR_CARD_BORDER);
    // Two icons in right zone scrolling left-to-right (no overflow outside button boundary):
    int cur_to_right_x = 52 + (int)roundf(dx);
    int right_x = 92 + (int)roundf(dx);
    draw_icon12x12_clipped(fb, KEY_W, cur_to_right_x, iy, icons[cur], COLOR_TEXT_DIM, 2, 85, 25, 38, 52);
    draw_icon12x12_clipped(fb, KEY_W, right_x, iy, icons[right], COLOR_DARK_GRAY, 2, 85, 25, 38, 52);

    // 3. Center Highlight Box Layer (HIGHER Z-INDEX: drawn OVER left & right zones)
    draw_rect_16(fb, KEY_W, 44, 22, 40, 58, COLOR_EMERALD_BG);
    draw_border_16(fb, KEY_W, 44, 22, 40, 58, 2, COLOR_EMERALD);
    // Center icons clipped to center box interior [46, 24, 36, 54]:
    draw_icon12x12_clipped(fb, KEY_W, left_x, iy, icons[left], COLOR_WHITE, 2, 46, 24, 36, 54);
    draw_icon12x12_clipped(fb, KEY_W, cur_to_right_x, iy, icons[cur], COLOR_WHITE, 2, 46, 24, 36, 54);

    // Label below (centered, no overflow)
    int display_idx = (dx > 20.0f) ? left : cur;
    draw_string_centered_16(fb, KEY_W, 88, names[display_idx], COLOR_WHITE, 1);
    draw_string_centered_16(fb, KEY_W, 106, "RIGHT SCROLL", COLOR_GRAY, 1);

    flush_key(8);
}

// =========================================================================
// PATTERN 8: Real-time Number (Live CPU % Telemetry) -> Key 9
// =========================================================================
static void render_pattern_8(void) {
    uint16_t *fb = g_key_fbs[9];
    if (!fb) return;

    int is_pressed = g_key_pressed[9];

    draw_rect_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, COLOR_CARD);
    draw_border_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, is_pressed ? 3 : 2, is_pressed ? COLOR_WHITE : COLOR_CYAN);

    draw_string_16(fb, KEY_W, 6, 6, "#09", COLOR_CYAN, 1);
    draw_badge_right_16(fb, KEY_W, 6, "CPU", COLOR_CYAN);

    // Big Number: e.g. " 34% "
    char numStr[16];
    snprintf(numStr, sizeof(numStr), "%2d%%", g_pat8_cpu_pct);
    uint16_t numCol = (g_pat8_cpu_pct > 70) ? COLOR_ROSE : ((g_pat8_cpu_pct > 40) ? COLOR_AMBER : COLOR_EMERALD);
    draw_string_centered_16(fb, KEY_W, 40, numStr, numCol, 2);

    // Horizontal Level Gauge Bar
    int bar_w = KEY_W - 20;
    int fill_w = (bar_w * g_pat8_cpu_pct) / 100;
    draw_rect_16(fb, KEY_W, 10, 84, bar_w, 10, COLOR_BG);
    draw_border_16(fb, KEY_W, 10, 84, bar_w, 10, 1, COLOR_CARD_BORDER);
    draw_rect_16(fb, KEY_W, 11, 85, fill_w, 8, numCol);

    flush_key(9);
}

// =========================================================================
// PATTERN 9: Real-time Graph (Rolling 60-sample CPU Sparkline) -> Key 10
// =========================================================================
static void render_pattern_9(void) {
    uint16_t *fb = g_key_fbs[10];
    if (!fb) return;

    int is_pressed = g_key_pressed[10];

    draw_rect_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, COLOR_CARD);
    draw_border_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, is_pressed ? 3 : 2, is_pressed ? COLOR_WHITE : COLOR_AMBER);

    draw_string_16(fb, KEY_W, 6, 6, "#10", COLOR_AMBER, 1);
    draw_badge_right_16(fb, KEY_W, 6, "GRAPH", COLOR_AMBER);

    // Sparkline Graph Frame (x: 6..121, y: 24..84 -> height 60)
    int gx = 6, gy = 24, gw = KEY_W - 12, gh = 60;
    draw_rect_16(fb, KEY_W, gx, gy, gw, gh, COLOR_BG);
    draw_border_16(fb, KEY_W, gx, gy, gw, gh, 1, COLOR_CARD_BORDER);

    // Horizontal grid lines
    for (int yline = gy + 15; yline < gy + gh; yline += 15) {
        for (int x = gx + 2; x < gx + gw - 2; x += 4) {
            fb[yline * KEY_W + x] = COLOR_DARK_GRAY;
        }
    }

    // Render Histogram Bars
    int bars = 28;
    for (int b = 0; b < bars; b++) {
        int hist_idx = 60 - bars + b;
        int val = g_pat9_cpu_history[hist_idx];
        if (val < 0) val = 0;
        if (val > 100) val = 100;

        int bar_h = (val * (gh - 4)) / 100;
        int bx = gx + 2 + b * 4;
        if (bx + 2 >= gx + gw) break;

        uint16_t bar_col = (val > 70) ? COLOR_ROSE : ((val > 40) ? COLOR_AMBER : COLOR_EMERALD);
        for (int h = 0; h < bar_h; h++) {
            int py = gy + gh - 2 - h;
            fb[py * KEY_W + bx] = bar_col;
            fb[py * KEY_W + bx + 1] = bar_col;
            fb[py * KEY_W + bx + 2] = bar_col;
        }
    }

    // Bottom Caption (centered, NO overflow!)
    draw_string_centered_16(fb, KEY_W, 92, "HIST: 60s", COLOR_GRAY, 1);

    flush_key(10);
}

// =========================================================================
// PATTERN 11: Rotating Circular Panel with Rotating Text (Concentric with Knob) -> Key 11
// =========================================================================
static void render_pattern_11(void) {
    uint16_t *fb = g_key_fbs[11];
    if (!fb) return;

    int is_pressed = g_key_pressed[11];
    int active_idx = get_active_dial_mode();

    draw_rect_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, COLOR_CARD);
    draw_border_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, is_pressed ? 3 : 2, is_pressed ? COLOR_WHITE : COLOR_CYAN);

    draw_string_16(fb, KEY_W, 6, 6, "#11", COLOR_CYAN, 1);
    draw_badge_right_16(fb, KEY_W, 6, "DIAL", COLOR_CYAN);

    int cx = 64, cy = 64;
    int r_bezel = 48;
    float target_top = -(float)(M_PI / 2.0);

    // 1. Outer Bezel Ring (Circle around the dial)
    draw_circle_thick_16(fb, KEY_W, cx, cy, r_bezel, 2, COLOR_CYAN_BG);

    // 2. Rotating Radial Ticks (24 ticks, every 15 degrees)
    for (int t = 0; t < 24; t++) {
        float tick_angle = target_top - t * (float)(M_PI / 12.0) + g_pat11_dial_angle;
        float cos_t = cosf(tick_angle);
        float sin_t = sinf(tick_angle);
        int is_major = (t % 4 == 0); // 6 major ticks aligned with modes
        int tick_len = is_major ? 7 : 3;
        uint16_t tick_col = is_major ? COLOR_CYAN : COLOR_DARK_GRAY;

        int x0 = cx + (int)roundf((r_bezel - tick_len) * cos_t);
        int y0 = cy + (int)roundf((r_bezel - tick_len) * sin_t);
        int x1 = cx + (int)roundf((r_bezel - 1) * cos_t);
        int y1 = cy + (int)roundf((r_bezel - 1) * sin_t);
        draw_line_16(fb, KEY_W, x0, y0, x1, y1, tick_col);
    }

    // 3. Rotating Mode Texts along Circular Orbit
    for (int i = 0; i < 6; i++) {
        float angle = target_top - i * (float)(M_PI / 3.0) + g_pat11_dial_angle;
        int is_active = (i == active_idx);
        uint16_t text_col = is_active ? COLOR_WHITE : COLOR_TEXT_DIM;
        draw_string_rotated_16(fb, KEY_W, cx, cy, 31.0f, angle, g_pat11_modes[i], text_col);
    }

    // 4. Central Knob Hub (concentric with the knob)
    draw_filled_circle_16(fb, KEY_W, cx, cy, 19, is_pressed ? COLOR_CYAN_BG : COLOR_BG);
    draw_circle_thick_16(fb, KEY_W, cx, cy, 19, 2, is_pressed ? COLOR_WHITE : COLOR_CYAN);

    // Active Mode Text centered inside Hub
    draw_string_centered_16(fb, KEY_W, cy - 8, g_pat11_modes[active_idx], is_pressed ? COLOR_WHITE : COLOR_CYAN, 1);

    // 5. Fixed Top Indicator Pointer (Triangle pointing down at 12 o'clock)
    draw_triangle_down_16(fb, KEY_W, cx, 14, 8, 6, COLOR_WHITE);
    draw_line_16(fb, KEY_W, cx, 8, cx, 14, COLOR_CYAN);

    flush_key(11);
}

// =========================================================================
// PATTERN 12: Smooth Vertical Value Scrolling Controlled by Knob -> Key 12
// =========================================================================
static void render_pattern_12(void) {
    uint16_t *fb = g_key_fbs[12];
    if (!fb) return;

    int is_pressed = g_key_pressed[12];

    draw_rect_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, COLOR_CARD);
    draw_border_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, is_pressed ? 3 : 2, is_pressed ? COLOR_WHITE : COLOR_INDIGO);

    draw_string_16(fb, KEY_W, 6, 6, "#12", COLOR_INDIGO, 1);
    draw_badge_right_16(fb, KEY_W, 6, "SCROLL", COLOR_INDIGO);

    // Vertical Value Reel Viewport
    draw_vertical_value_reel(fb, KEY_W, 16, 24, 96, 72,
                             g_pat12_current_val, g_pat12_target_val,
                             COLOR_INDIGO_BG, COLOR_INDIGO);

    // Bottom Status & Numeric Readout
    char valStr[16];
    snprintf(valStr, sizeof(valStr), "[ %3d%% ]", (int)roundf(g_pat12_current_val));
    draw_string_centered_16(fb, KEY_W, 100, valStr, is_pressed ? COLOR_WHITE : COLOR_INDIGO, 1);
    draw_string_centered_16(fb, KEY_W, 114, "SMOOTH REEL", COLOR_GRAY, 1);

    flush_key(12);
}

// =========================================================================
// Auxiliary Keys: 13..20
// =========================================================================
static void render_aux_key(int key_idx) {
    if (key_idx < 13 || key_idx > 20) return;
    uint16_t *fb = g_key_fbs[key_idx];
    if (!fb) return;

    int is_pressed = g_key_pressed[key_idx];
    draw_rect_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, COLOR_CARD);
    draw_border_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, is_pressed ? 3 : 2, is_pressed ? COLOR_WHITE : COLOR_CARD_BORDER);

    char numStr[16];
    snprintf(numStr, sizeof(numStr), "#%02d", key_idx);
    draw_string_16(fb, KEY_W, 6, 6, numStr, COLOR_GRAY, 1);

    switch (key_idx) {
        case 13: // Counter
            draw_badge_right_16(fb, KEY_W, 6, "COUNT", COLOR_CYAN);
            char cntStr[16];
            snprintf(cntStr, sizeof(cntStr), "%d", g_aux_counter);
            draw_string_centered_16(fb, KEY_W, 44, cntStr, COLOR_CYAN, 2);
            draw_string_centered_16(fb, KEY_W, 86, "+1 COUNT", COLOR_GRAY, 1);
            break;
        case 14: // Audio Test
            draw_badge_right_16(fb, KEY_W, 6, "AUDIO", COLOR_EMERALD);
            draw_string_centered_16(fb, KEY_W, 44, "BEEP", COLOR_EMERALD, 2);
            draw_string_centered_16(fb, KEY_W, 86, "BEEP TEST", COLOR_GRAY, 1);
            break;
        case 15: // Reset All
            draw_badge_right_16(fb, KEY_W, 6, "RESET", COLOR_ROSE);
            draw_string_centered_16(fb, KEY_W, 44, "RESET", COLOR_ROSE, 2);
            draw_string_centered_16(fb, KEY_W, 86, "RESET ALL", COLOR_GRAY, 1);
            break;
        case 16: // Theme
            draw_badge_right_16(fb, KEY_W, 6, "THEME", COLOR_PURPLE);
            draw_string_centered_16(fb, KEY_W, 44, g_aux_theme ? "LIGHT" : "DARK ", COLOR_PURPLE, 2);
            draw_string_centered_16(fb, KEY_W, 86, "TOGGLE THEME", COLOR_GRAY, 1);
            break;
        case 17: // Provider
            draw_badge_right_16(fb, KEY_W, 6, "PROV", COLOR_INDIGO);
            draw_string_centered_16(fb, KEY_W, 44, "CLAUDE", COLOR_WHITE, 2);
            draw_string_centered_16(fb, KEY_W, 86, "OPUS 4.6", COLOR_INDIGO, 1);
            break;
        case 18: // Model
            draw_badge_right_16(fb, KEY_W, 6, "MODEL", COLOR_INDIGO);
            draw_string_centered_16(fb, KEY_W, 44, "SONNET", COLOR_WHITE, 2);
            draw_string_centered_16(fb, KEY_W, 86, "AGENTIC", COLOR_INDIGO, 1);
            break;
        case 19: // Plan View
            draw_badge_right_16(fb, KEY_W, 6, "PLAN", COLOR_CYAN);
            draw_string_centered_16(fb, KEY_W, 44, "PLAN", COLOR_WHITE, 2);
            draw_string_centered_16(fb, KEY_W, 86, "[ACTIVE]", COLOR_CYAN, 1);
            break;
        case 20: // Diff View
            draw_badge_right_16(fb, KEY_W, 6, "DIFF", COLOR_AMBER);
            draw_string_centered_16(fb, KEY_W, 44, "DIFF", COLOR_WHITE, 2);
            draw_string_centered_16(fb, KEY_W, 86, "+204 -82", COLOR_EMERALD, 1);
            break;
    }

    flush_key(key_idx);
}

// Render an individual physical keycap by index (1..20)
static void render_key_by_index(int key_idx) {
    if (g_mode_v2) {
        v2_render_key_frame(g_key_fbs[key_idx], key_idx, g_key_pressed[key_idx]);
        flush_key(key_idx);
        return;
    }
    switch (key_idx) {
        case 1: render_pattern_1(); break;
        case 2: render_pattern_2(); break;
        case 3: render_pattern_7(); break;
        case 4: render_pattern_10(); break;
        case 5: render_pattern_3(); break;
        case 6: render_pattern_4(); break;
        case 7: render_pattern_5(); break;
        case 8: render_pattern_6(); break;
        case 9: render_pattern_8(); break;
        case 10: render_pattern_9(); break;
        case 11: render_pattern_11(); break;
        case 12: render_pattern_12(); break;
        default:
            if (key_idx >= 13 && key_idx <= 20) {
                render_aux_key(key_idx);
            }
            break;
    }
}

// Render All 20 Physical Keycaps
static void render_all_keys(void) {
    for (int k = 1; k <= 20; k++) {
        render_key_by_index(k);
    }
}

// =========================================================================
// TOP DISPLAY (/dev/fb21): Pattern 11 (Dial Gauge) & Pattern 12 (Marquee)
// =========================================================================
static void render_top_display(void) {
    if (!g_top_fb) return;
    if (g_mode_v2) {
        v2_render_top_frame(g_top_fb);
        flush_top();
        return;
    }
    uint16_t *fb = g_top_fb;

    draw_rect_16(fb, TOP_W, 0, 0, TOP_W, TOP_H, COLOR_BG);
    draw_border_16(fb, TOP_W, 0, 0, TOP_W, TOP_H, 2, COLOR_CARD_BORDER);

    // 1. Top Header Bar (y=0..26)
    draw_string_16(fb, TOP_W, 10, 6, "MK20 UI SHOWCASE", COLOR_CYAN, 1);

    char cpuStr[24];
    snprintf(cpuStr, sizeof(cpuStr), "CPU: %2d%%", g_pat8_cpu_pct);
    draw_string_16(fb, TOP_W, 146, 6, cpuStr, COLOR_AMBER, 1);
    draw_string_16(fb, TOP_W, 222, 6, "LAT: 0.01ms", COLOR_EMERALD, 1);

    // Status Pill
    draw_rect_16(fb, TOP_W, TOP_W - 75, 4, 65, 18, COLOR_EMERALD);
    draw_string_16(fb, TOP_W, TOP_W - 70, 6, "ACTIVE", COLOR_BLACK, 1);

    draw_rect_16(fb, TOP_W, 8, 25, TOP_W - 16, 1, COLOR_CARD_BORDER);

    // 2. Main Content: Dual Feature Cards (y=29..136, height=107)

    // CARD 1: PATTERN 11: Rotating Circular Knob Dial Gauge (Left)
    int dx = 8, dy = 29, dw = 202, dh = 107;
    draw_rect_16(fb, TOP_W, dx, dy, dw, dh, COLOR_CARD);
    draw_border_16(fb, TOP_W, dx, dy, dw, dh, 1, COLOR_CYAN);

    // Header inside Card 1
    draw_string_16(fb, TOP_W, dx + 6, dy + 6, "#11 ROTATING DIAL", COLOR_CYAN, 1);
    char degStr[16];
    float deg = g_pat11_dial_angle * 180.0f / (float)M_PI;
    if (deg < 0.0f) deg += 360.0f;
    snprintf(degStr, sizeof(degStr), "%3.0f*", deg);
    draw_string_16(fb, TOP_W, dx + dw - 40, dy + 6, degStr, COLOR_WHITE, 1);

    // Mini Circular Rotating Dial in Card 1
    int tcx = dx + 38, tcy = dy + 56;
    int tr = 24;
    draw_circle_thick_16(fb, TOP_W, tcx, tcy, tr, 1, COLOR_CYAN_BG);
    draw_filled_circle_16(fb, TOP_W, tcx, tcy, 9, COLOR_BG);
    draw_circle_thick_16(fb, TOP_W, tcx, tcy, 9, 1, COLOR_CYAN);

    for (int t = 0; t < 12; t++) {
        float tang = -(float)(M_PI / 2.0) - t * (float)(M_PI / 6.0) + g_pat11_dial_angle;
        float c_t = cosf(tang), s_t = sinf(tang);
        int is_m = (t % 2 == 0);
        int tlen = is_m ? 5 : 2;
        uint16_t tcol = is_m ? COLOR_CYAN : COLOR_DARK_GRAY;
        int tx0 = tcx + (int)roundf((tr - tlen) * c_t);
        int ty0 = tcy + (int)roundf((tr - tlen) * s_t);
        int tx1 = tcx + (int)roundf((tr - 1) * c_t);
        int ty1 = tcy + (int)roundf((tr - 1) * s_t);
        draw_line_16(fb, TOP_W, tx0, ty0, tx1, ty1, tcol);
    }
    draw_triangle_down_16(fb, TOP_W, tcx, tcy - tr - 2, 5, 4, COLOR_WHITE);

    // Right of Dial: Mode Selection Box & Subtitle
    int active_mode = get_active_dial_mode();
    draw_rect_16(fb, TOP_W, dx + 72, dy + 32, 120, 22, COLOR_CYAN_BG);
    draw_border_16(fb, TOP_W, dx + 72, dy + 32, 120, 22, 1, COLOR_CYAN);
    char modeBadge[32];
    snprintf(modeBadge, sizeof(modeBadge), "> [ %s ] <", g_pat11_modes[active_mode]);
    draw_string_16(fb, TOP_W, dx + 82, dy + 36, modeBadge, COLOR_WHITE, 1);

    draw_string_16(fb, TOP_W, dx + 72, dy + 62, "LEFT KNOB", COLOR_GRAY, 1);
    if (g_state.dial_until_ms > get_time_ms()) {
        draw_string_clipped_16(fb, TOP_W, dx + 72, dy + 82, g_state.dial_action, COLOR_AMBER, 1, dx + 72, dy + 80, 124, 16);
    } else {
        draw_string_16(fb, TOP_W, dx + 72, dy + 82, "DIAL SELECTION", COLOR_TEXT_DIM, 1);
    }

    // CARD 2: PATTERN 12: Smooth Vertical Value Reel (Right)
    int cx = 218, cy = 29, cw = 202, dh2 = 107;
    draw_rect_16(fb, TOP_W, cx, cy, cw, dh2, COLOR_CARD);
    draw_border_16(fb, TOP_W, cx, cy, cw, dh2, 1, COLOR_INDIGO);

    // Header inside Card 2
    draw_string_16(fb, TOP_W, cx + 6, cy + 6, "#12 VALUE REEL", COLOR_INDIGO, 1);
    char valStr[16];
    snprintf(valStr, sizeof(valStr), "%3d%%", (int)roundf(g_pat12_current_val));
    draw_string_16(fb, TOP_W, cx + cw - 42, cy + 6, valStr, COLOR_WHITE, 1);

    // Vertical Value Reel Viewport in Card 2
    draw_vertical_value_reel(fb, TOP_W, cx + 6, cy + 24, 102, 76,
                             g_pat12_current_val, g_pat12_target_val,
                             COLOR_INDIGO_BG, COLOR_INDIGO);

    // Right Side of Reel in Card 2: Numeric Readout & Dynamic Progress Gauge
    int rx = cx + 114;
    draw_string_16(fb, TOP_W, rx, cy + 20, "RIGHT KNOB", COLOR_GRAY, 1);
    draw_string_16(fb, TOP_W, rx, cy + 36, valStr, COLOR_WHITE, 2);

    // Mini Level Bar
    int bar_x = rx, bar_y = cy + 72, bar_w = 76, bar_h = 7;
    draw_rect_16(fb, TOP_W, bar_x, bar_y, bar_w, bar_h, COLOR_BG);
    draw_border_16(fb, TOP_W, bar_x, bar_y, bar_w, bar_h, 1, COLOR_CARD_BORDER);
    int fill_w = (int)roundf((g_pat12_current_val / 100.0f) * (bar_w - 2));
    if (fill_w > bar_w - 2) fill_w = bar_w - 2;
    if (fill_w > 0) {
        draw_rect_16(fb, TOP_W, bar_x + 1, bar_y + 1, fill_w, bar_h - 2, COLOR_INDIGO);
    }

    if (g_state.scroll_until_ms > get_time_ms()) {
        draw_string_clipped_16(fb, TOP_W, rx, cy + 86, g_state.scroll_action, COLOR_AMBER, 1, rx, cy + 84, 80, 16);
    } else {
        draw_string_16(fb, TOP_W, rx, cy + 86, "SMOOTH LERP", COLOR_GRAY, 1);
    }

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

// Forward declarations for knob handlers
static void on_left_knob(int direction);
static void on_left_knob_click(void);
static void on_right_knob(int direction);
static void on_right_knob_click(void);

// Handle switch contact events
static void on_key_event(int row, int col, int pressed) {
    int key_idx = get_mapped_key_index(row, col);
    if (key_idx < 1 || key_idx > 20) return;

    g_key_pressed[key_idx] = pressed;

    if (g_mode_v2) {
        if (g_sockfd >= 0 && g_has_host_addr) {
            char pkt[128];
            int n = snprintf(pkt, sizeof(pkt), "{\"type\":\"key\",\"keyId\":%d,\"isDown\":%s}\n",
                             key_idx, pressed ? "true" : "false");
            sendto(g_sockfd, pkt, n, 0, (struct sockaddr *)&g_host_addr, sizeof(g_host_addr));
        }
        g_dirty_keys |= (1 << key_idx);
        return;
    }

    switch (key_idx) {
        case 1:
            if (pressed) g_pat1_toggle = !g_pat1_toggle;
            break;
        case 2:
            if (pressed) g_pat2_toggle = !g_pat2_toggle;
            break;
        case 3:
            g_pat7_shift = pressed;
            break;
        case 4:
            if (pressed) {
                g_pat10_metronome_bpm = (g_pat10_metronome_bpm == 120) ? 160 : ((g_pat10_metronome_bpm == 160) ? 80 : 120);
            }
            break;
        case 5:
            if (pressed) g_pat3_mode_idx = (g_pat3_mode_idx + 1) % 4;
            break;
        case 6:
            if (pressed) g_pat4_mode_idx = (g_pat4_mode_idx + 1) % 4;
            break;
        case 7:
            if (pressed) g_pat5_icon_idx = (g_pat5_icon_idx + 1) % 4;
            break;
        case 8:
            if (pressed) {
                if (g_pat6_animating) {
                    g_pat6_icon_idx = (g_pat6_icon_idx + 3) % 4;
                }
                g_pat6_animating = 1;
                g_pat6_anim_start_ms = get_time_ms();
                g_pat6_anim_offset = 0.0f;
            }
            break;
        case 9:
            if (pressed) g_pat8_cpu_pct = read_cpu_percent();
            break;
        case 10:
            if (pressed) {
                for (int i = 0; i < 59; i++) g_pat9_cpu_history[i] = g_pat9_cpu_history[i + 1];
                g_pat9_cpu_history[59] = 95;
            }
            break;
        case 11:
            if (pressed) {
                on_left_knob(+1);
            }
            break;
        case 12:
            if (pressed) {
                on_right_knob(+1);
            }
            break;
        case 13:
            if (pressed) g_aux_counter++;
            break;
        case 14:
            break;
        case 15:
            if (pressed) {
                g_pat1_toggle = 0;
                g_pat2_toggle = 1;
                g_pat3_mode_idx = 0;
                g_pat4_mode_idx = 0;
                g_pat5_icon_idx = 0;
                g_pat6_icon_idx = 0;
                g_pat6_animating = 0;
                g_pat6_anim_offset = 0.0f;
                g_pat10_metronome_bpm = 120;
                g_pat10_phase = 0.0f;
                g_pat11_knob_val = 50;
                g_pat11_dial_angle = 0.0f;
                g_pat12_target_val = 50;
                g_pat12_current_val = 50.0f;
                g_state.dial_action[0] = '\0';
                g_state.dial_until_ms = 0;
                g_state.scroll_action[0] = '\0';
                g_state.scroll_until_ms = 0;
                g_dirty_keys = 0x1FFFFE;
                g_dirty_top = 1;
                return;
            }
            break;
        case 16:
            if (pressed) g_aux_theme = !g_aux_theme;
            break;
        case 17:
        case 18:
        case 19:
        case 20:
            break;
    }
    g_dirty_keys |= (1 << key_idx);
}

// Handle Left Rotary Knob (controls Card 1 & Key 11)
static void on_left_knob(int direction) {
    if (g_mode_v2) {
        if (g_sockfd >= 0 && g_has_host_addr) {
            char pkt[128];
            int n = snprintf(pkt, sizeof(pkt), "{\"type\":\"knob_left\",\"delta\":%d}\n", direction);
            sendto(g_sockfd, pkt, n, 0, (struct sockaddr *)&g_host_addr, sizeof(g_host_addr));
        }
        return;
    }

    // direction: +1 = CW, -1 = CCW
    float step = (float)(M_PI / 12.0f); // 15 degrees per notch
    if (direction > 0) {
        g_pat11_dial_angle += step;
        while (g_pat11_dial_angle >= (float)(2.0 * M_PI)) g_pat11_dial_angle -= (float)(2.0 * M_PI);
        g_pat11_knob_val += 5;
        if (g_pat11_knob_val > 100) g_pat11_knob_val = 100;
    } else {
        g_pat11_dial_angle -= step;
        while (g_pat11_dial_angle < 0.0f) g_pat11_dial_angle += (float)(2.0 * M_PI);
        g_pat11_knob_val -= 5;
        if (g_pat11_knob_val < 0) g_pat11_knob_val = 0;
    }
    int mode = get_active_dial_mode();
    snprintf(g_state.dial_action, sizeof(g_state.dial_action), "DIAL %s -> [%s]",
             (direction > 0) ? "CW" : "CCW", g_pat11_modes[mode]);
    g_state.dial_until_ms = get_time_ms() + 1000;

    g_dirty_keys |= (1 << 11);
    g_dirty_top = 1;
}

static void on_left_knob_click(void) {
    if (g_mode_v2) {
        if (g_sockfd >= 0 && g_has_host_addr) {
            char pkt[128];
            int n = snprintf(pkt, sizeof(pkt), "{\"type\":\"knob_left\",\"isClick\":true}\n");
            sendto(g_sockfd, pkt, n, 0, (struct sockaddr *)&g_host_addr, sizeof(g_host_addr));
        }
        return;
    }

    g_pat11_dial_angle += (float)(M_PI / 3.0f); // Advance one mode (60 deg)
    while (g_pat11_dial_angle >= (float)(2.0 * M_PI)) g_pat11_dial_angle -= (float)(2.0 * M_PI);
    int mode = get_active_dial_mode();
    snprintf(g_state.dial_action, sizeof(g_state.dial_action), "KNOB CLICK [%s]", g_pat11_modes[mode]);
    g_state.dial_until_ms = get_time_ms() + 1000;

    g_dirty_keys |= (1 << 11);
    g_dirty_top = 1;
}

// Handle Right Rotary Knob (controls Card 2 & Key 12)
static void on_right_knob(int direction) {
    if (g_mode_v2) {
        if (g_sockfd >= 0 && g_has_host_addr) {
            char pkt[128];
            int n = snprintf(pkt, sizeof(pkt), "{\"type\":\"knob_right\",\"delta\":%d}\n", direction);
            sendto(g_sockfd, pkt, n, 0, (struct sockaddr *)&g_host_addr, sizeof(g_host_addr));
        }
        g_v2_state.volume += direction * 5;
        if (g_v2_state.volume < 0) g_v2_state.volume = 0;
        if (g_v2_state.volume > 100) g_v2_state.volume = 100;
        g_dirty_top = 1;
        return;
    }

    // direction: +1 = CW (Up), -1 = CCW (Down)
    if (direction > 0) {
        g_pat12_target_val += 2;
        if (g_pat12_target_val > 100) g_pat12_target_val = 100;
        snprintf(g_state.scroll_action, sizeof(g_state.scroll_action), "REEL UP -> [%d]", g_pat12_target_val);
    } else {
        g_pat12_target_val -= 2;
        if (g_pat12_target_val < 0) g_pat12_target_val = 0;
        snprintf(g_state.scroll_action, sizeof(g_state.scroll_action), "REEL DN <- [%d]", g_pat12_target_val);
    }
    g_state.scroll_until_ms = get_time_ms() + 1000;

    g_dirty_keys |= (1 << 12);
    g_dirty_top = 1;
}

static void on_right_knob_click(void) {
    if (g_mode_v2) {
        if (g_sockfd >= 0 && g_has_host_addr) {
            char pkt[128];
            int n = snprintf(pkt, sizeof(pkt), "{\"type\":\"knob_right\",\"isClick\":true}\n");
            sendto(g_sockfd, pkt, n, 0, (struct sockaddr *)&g_host_addr, sizeof(g_host_addr));
        }
        g_v2_state.is_muted = !g_v2_state.is_muted;
        g_dirty_top = 1;
        return;
    }

    g_pat12_target_val = (g_pat12_target_val == 50) ? 100 : ((g_pat12_target_val == 100) ? 0 : 50);
    snprintf(g_state.scroll_action, sizeof(g_state.scroll_action), "REEL RESET [%d]", g_pat12_target_val);
    g_state.scroll_until_ms = get_time_ms() + 1000;

    g_dirty_keys |= (1 << 12);
    g_dirty_top = 1;
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

            if (sum == s_checksum) {
                if (s_dataLen >= 6 && s_dataBuf[0] == 0x04) {
                    g_map_reply_layer = s_dataBuf[1];
                    g_map_reply_row = s_dataBuf[2]; g_map_reply_col = s_dataBuf[3];
                    g_map_reply_code = ((uint16_t)s_dataBuf[4] << 8) | s_dataBuf[5];
                } else if (s_dataLen >= 4 && s_dataBuf[0] == 0x16) {
                    uint8_t pressed = s_dataBuf[1];
                    uint8_t row = s_dataBuf[2];
                    uint8_t col = s_dataBuf[3];

                    printf("[QMK RX] row=%u col=%u pressed=%u\n", row, col, pressed);
                    fflush(stdout);

                    // Left Rotary Knob (rows 100, 101, 102)
                    if (row == 100) {
                        if (col == 100 || col == 0) {
                            if (pressed) {
                                g_left_down = 1;
                                if (g_right_down && !g_knob_chord) { g_knob_chord = 1; g_knob_toggle_pending = 1; }
                            } else {
                                if (g_left_down && !g_knob_chord) on_left_knob_click();
                                g_left_down = 0;
                                if (!g_right_down) g_knob_chord = 0;
                            }
                        } else if (col == 1) {
                            on_left_knob(+1);     // Left Knob CW
                        } else {
                            on_left_knob(-1);     // Left Knob CCW
                        }
                    } else if (row == 101) {
                        on_left_knob(-1);         // Left Knob CCW (smooth 15 deg step)
                    } else if (row == 102) {
                        on_left_knob(+1);         // Left Knob CW (smooth 15 deg step)
                    }
                    // Right Rotary Knob (rows 103, 104, 105, plus 106..108 fallback)
                    else if (row == 103 || row == 106) {
                        if (col == 103 || col == 106 || col == 0) {
                            if (pressed) {
                                g_right_down = 1;
                                if (g_left_down && !g_knob_chord) { g_knob_chord = 1; g_knob_toggle_pending = 1; }
                            } else {
                                if (g_right_down && !g_knob_chord) on_right_knob_click();
                                g_right_down = 0;
                                if (!g_left_down) g_knob_chord = 0;
                            }
                        } else if (col == 1) {
                            on_right_knob(+1);     // Right Knob CW
                        } else {
                            on_right_knob(-1);     // Right Knob CCW
                        }
                    } else if (row == 104 || row == 107) {
                        on_right_knob(-1);         // Right Knob CCW
                    } else if (row == 105 || row == 108) {
                        on_right_knob(+1);         // Right Knob CW
                    }
                    // Direct Instant Switch Contact (Keys 1..20)
                    else if (row < 4 && col < 5) {
                        on_key_event(row, col, pressed != 0);
                    }
                } else {
                    printf("[QMK RX OTHER] cmd=0x%02X len=%u: ", s_dataBuf[0], s_dataLen);
                    for (int i = 0; i < s_dataLen; i++) printf("%02X ", s_dataBuf[i]);
                    printf("\n");
                    fflush(stdout);
                }
            } else {
                printf("[QMK CHECKSUM ERR] sum=0x%02X expected=0x%02X\n", sum, s_checksum);
                fflush(stdout);
            }
        }
        s_parse_state = STATE_HEADER1;
        break;
    default:
        s_parse_state = STATE_HEADER1;
        break;
    }
}

static void handle_udp_packet(const char *buf, int len, const struct sockaddr_in *sender) {
    if (v2_parse_sync_packet(buf, len)) {
        if (sender) {
            g_host_addr = *sender;
            g_has_host_addr = 1;
        }
        g_last_host_sync_ms = get_time_ms();
        g_host_offline = 0;
        g_dirty_keys = 0x1FFFFE;
        g_dirty_top = 1;
        return;
    } else if (strstr(buf, "\"type\":\"v2_sync\"") || strstr(buf, "\"type\": \"v2_sync\"")) {
        fprintf(stderr, "[MK20-HUD] Failed to parse v2_sync packet (len=%d)\n", len);
    }
    if (strncmp(buf, "KEY:", 4) == 0) {
        int r, c, p;
        if (sscanf(buf + 4, "%d|%d|%d", &r, &c, &p) == 3) {
            if (r >= 0 && r < 4 && c >= 0 && c < 5) {
                on_key_event(r, c, p != 0);
            }
        }
    } else if (strncmp(buf, "DIAL:LEFT_CW", 12) == 0) {
        on_left_knob(+1);
    } else if (strncmp(buf, "DIAL:LEFT_CCW", 13) == 0) {
        on_left_knob(-1);
    } else if (strncmp(buf, "DIAL:LEFT_CLICK", 15) == 0) {
        on_left_knob_click();
    } else if (strncmp(buf, "DIAL:RIGHT_CW", 13) == 0) {
        on_right_knob(+1);
    } else if (strncmp(buf, "DIAL:RIGHT_CCW", 14) == 0) {
        on_right_knob(-1);
    } else if (strncmp(buf, "DIAL:RIGHT_CLICK", 16) == 0) {
        on_right_knob_click();
    } else if (strncmp(buf, "DIAL:LEFT", 9) == 0) {
        on_left_knob(-1);
    } else if (strncmp(buf, "DIAL:RIGHT", 10) == 0) {
        on_left_knob(+1);
    } else if (strncmp(buf, "DIAL:", 5) == 0) {
        strncpy(g_state.dial_action, buf + 5, sizeof(g_state.dial_action) - 1);
        g_state.dial_until_ms = get_time_ms() + 1000;
        g_dirty_top = 1;
    }
}

// QMK / VIA Hardware Initialization Helpers
static void send_qmk_frame(int fd, const uint8_t *data, uint8_t len) {
    if (fd < 0) return;
    uint8_t frame[64];
    uint8_t sum = 0;
    int idx = 0;

    frame[idx++] = 0xAA;
    frame[idx++] = 0x55;
    frame[idx++] = 0x00; // placeholder for sum
    frame[idx++] = len;
    frame[idx++] = (uint8_t)(0xFF - len);

    for (int i = 0; i < len; i++) {
        frame[idx++] = data[i];
        sum += data[i];
    }

    frame[2] = sum;

    frame[idx++] = 0xF5;
    frame[idx++] = 0x5F;

    write(fd, frame, idx);
    usleep(1000); // 1ms delay between VIA packets
}

static void send_qmk_set_keycode(int fd, uint8_t layer, uint8_t row, uint8_t col, uint16_t keycode) {
    uint8_t data[6];
    data[0] = 0x05; // id_dynamic_keymap_set_keycode
    data[1] = layer;
    data[2] = row;
    data[3] = col;
    data[4] = (uint8_t)(keycode >> 8);
    data[5] = (uint8_t)(keycode & 0xFF);
    send_qmk_frame(fd, data, sizeof(data));
}

static void send_qmk_get_keycode(int fd, uint8_t layer, uint8_t row, uint8_t col) {
    // Firmware returns the same payload length; reserve bytes 4/5 for the keycode.
    uint8_t data[6] = {0};
    data[0] = 0x04; // id_dynamic_keymap_get_keycode
    data[1] = layer;
    data[2] = row;
    data[3] = col;
    send_qmk_frame(fd, data, sizeof(data));
}

static void apply_pc_key_mode(int fd) {
    if (!g_saved_keymap_valid) return;
    for (int l = 0; l < 4; l++) for (int r = 0; r < 4; r++) for (int c = 0; c < 5; c++)
        send_qmk_set_keycode(fd, l, r, c, g_pc_keys_on ? g_saved_keymap[l][r][c] : 0);
    printf("[MK20-HUD] PC keyboard %s\n", g_pc_keys_on ? "ON" : "OFF"); fflush(stdout);
}

static void init_qmk_hardware(int uart_fd) {
    if (uart_fd < 0) return;
    printf("[MK20-HUD] Initializing QMK key matrix and rotary encoders across all 4 layers...\n");

    // Preserve the device's current mapping once before disabling USB keys.
    // Never invent a keyboard mapping or overwrite a prior backup with KC_NO.
    uint16_t saved[4][4][5];
    const char *backup = "/mnt/SDCARD/snowball-keymap.bin";
    FILE *fp = fopen(backup, "rb");
    int valid = 0;
    if (fp) {
        char magic[4];
        valid = fread(magic, 1, 4, fp) == 4 && !memcmp(magic, "SKM1", 4) &&
            fread(saved, 1, sizeof saved, fp) == sizeof saved && fgetc(fp) == EOF;
        fclose(fp);
        if (!valid) { fprintf(stderr, "Invalid keymap backup; matrix unchanged.\n"); return; }
    } else {
        valid = 1;
        for (int l = 0; l < 4 && valid; l++) for (int r = 0; r < 4 && valid; r++) for (int c = 0; c < 5; c++) {
            g_map_reply_layer = -1;
            send_qmk_get_keycode(uart_fd, l, r, c);
            long long until = get_time_ms() + 250;
            while (get_time_ms() < until && g_map_reply_layer < 0) {
                struct pollfd p = { .fd = uart_fd, .events = POLLIN };
                if (poll(&p, 1, 20) > 0) {
                    uint8_t bytes[128]; int n = read(uart_fd, bytes, sizeof bytes);
                    for (int i = 0; i < n; i++) parse_qmk_byte(bytes[i]);
                }
            }
            if (g_map_reply_layer != l || g_map_reply_row != r || g_map_reply_col != c) { valid = 0; break; }
            saved[l][r][c] = g_map_reply_code;
        }
        if (!valid) {
            fprintf(stderr, "[MK20-HUD] Keymap readback failed; falling back to factory default layout.\n");
            static const uint16_t factory_layer0[4][5] = {
                { 0x0027, 0x001E, 0x001F, 0x0020, 0x0021 }, // Row 0: 0, 1, 2, 3, 4
                { 0x0022, 0x0023, 0x0024, 0x0025, 0x0026 }, // Row 1: 5, 6, 7, 8, 9
                { 0x0004, 0x0005, 0x0006, 0x0007, 0x0008 }, // Row 2: A, B, C, D, E
                { 0x0009, 0x000A, 0x000B, 0x000C, 0x000D }  // Row 3: F, G, H, I, J
            };
            memset(saved, 0, sizeof saved);
            memcpy(saved[0], factory_layer0, sizeof factory_layer0);
            valid = 1;
        }
        fp = fopen("/mnt/SDCARD/snowball-keymap.bin.tmp", "wb");
        if (fp) {
            int written = fwrite("SKM1", 1, 4, fp) == 4 && fwrite(saved, 1, sizeof saved, fp) == sizeof saved;
            int closed = fclose(fp);
            if (written && !closed) rename("/mnt/SDCARD/snowball-keymap.bin.tmp", backup);
        }
    }
    // --pc-keys-on restores that snapshot; default mode suppresses PC typing.
    memcpy(g_saved_keymap, saved, sizeof saved); g_saved_keymap_valid = 1;
    for (int l = 0; l < 4; l++) {
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 5; c++) {
                send_qmk_set_keycode(uart_fd, l, r, c, g_pc_keys_on ? saved[l][r][c] : 0x0000);
            }
        }
    }

    // 2. Both Rotary Knobs (Left: rows 100..102, Right: rows 103..105, Extra: 106..108)
    // Keycode 0x0000 (KC_NO) instructs QMK not to consume events for USB HID,
    // thereby escalating raw id_custom_report_key_state (0x16) packets over UART /dev/ttyS1.
    for (int l = 0; l < 4; l++) {
        for (int row = 100; row <= 108; row++) {
            send_qmk_set_keycode(uart_fd, l, row, row, 0x0000);
        }
    }

    // 3. Query protocol version and read back encoder keycodes
    uint8_t ver_cmd[1] = { 0x01 }; // id_get_protocol_version
    send_qmk_frame(uart_fd, ver_cmd, sizeof(ver_cmd));

    for (int row = 100; row <= 105; row++) {
        send_qmk_get_keycode(uart_fd, 0, row, row);
    }

    printf("[MK20-HUD] QMK hardware initialized successfully (rows 100..108 unbound to HID on layers 0..3).\n");
    printf("[MK20-HUD] PC matrix keys %s; saved mapping: %s\n", g_pc_keys_on ? "ON" : "OFF", backup);
    fflush(stdout);
}

int main(int argc, char *argv[]) {
    for (int a = 1; a < argc; a++) if (!strcmp(argv[a], "--pc-keys-on")) g_pc_keys_on = 1;
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    printf("[MK20-HUD] Starting Standalone MK20 UI Engine...\n");

    for (int a = 1; a < argc; a++) {
        if (strcmp(argv[a], "--showcase") == 0) {
            g_mode_v2 = 0;
        } else if (strcmp(argv[a], "--v2") == 0) {
            g_mode_v2 = 1;
        }
    }

    if (g_mode_v2) {
        printf("[MK20-HUD] Mode: Product Design V2\n");
        v2_init_defaults();
    } else {
        printf("[MK20-HUD] Mode: 12-Pattern Showcase\n");
    }

    strncpy(g_state.provider, "Claude Code 2.1", sizeof(g_state.provider) - 1);
    strncpy(g_state.model, "Sonnet 3.7", sizeof(g_state.model) - 1);
    strncpy(g_state.status, "ONLINE", sizeof(g_state.status) - 1);
    strncpy(g_state.message, "12 Interaction Patterns Active", sizeof(g_state.message) - 1);
    g_state.dial_until_ms = 0;

    // Initialize mock history
    for (int i = 0; i < 60; i++) {
        g_pat9_cpu_history[i] = 20 + (i % 35);
    }

    for (int a = 1; a < argc; a++) {
        if (strcmp(argv[a], "-d") == 0) {
            if (daemon(1, 1) < 0) {
                perror("daemon() failed");
            }
            int log_fd = open("/tmp/hud.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (log_fd >= 0) {
                dup2(log_fd, STDOUT_FILENO);
                dup2(log_fd, STDERR_FILENO);
                close(log_fd);
            }
            break;
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

        // Initialize dynamic keymap and rotary encoders on GD32 MCU
        init_qmk_hardware(uart_fd);
    }

    // 5. Open Input Event Devices (/dev/input/event0 .. event3)
    int ev_fds[4] = {-1, -1, -1, -1};
    int ev_pfd_idx[4] = {-1, -1, -1, -1};
    for (int e = 0; e < 4; e++) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/input/event%d", e);
        ev_fds[e] = open(path, O_RDONLY | O_NONBLOCK);
        if (ev_fds[e] >= 0) {
            char name[64] = "unknown";
            ioctl(ev_fds[e], EVIOCGNAME(sizeof(name)), name);
            printf("[MK20-HUD] Listening on %s: '%s' (fd %d)\n", path, name, ev_fds[e]);
        }
    }

    // 6. Open UDP Socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd >= 0) {
        g_sockfd = sockfd;
        int flags = fcntl(sockfd, F_GETFL, 0);
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
        struct sockaddr_in servaddr;
        memset(&servaddr, 0, sizeof(servaddr));
        servaddr.sin_family = AF_INET;
        servaddr.sin_addr.s_addr = INADDR_ANY;
        servaddr.sin_port = htons(UDP_PORT);
        bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr));
    }

    // 7. Event Loop with Animation & Telemetry Timers
    struct pollfd pfd[8];
    int pfd_count = 0;

    pfd[pfd_count].fd = uart_fd;
    pfd[pfd_count].events = POLLIN;
    int uart_pfd_idx = pfd_count++;

    int udp_pfd_idx = -1;
    if (sockfd >= 0) {
        pfd[pfd_count].fd = sockfd;
        pfd[pfd_count].events = POLLIN;
        udp_pfd_idx = pfd_count++;
    }

    for (int e = 0; e < 4; e++) {
        if (ev_fds[e] >= 0) {
            pfd[pfd_count].fd = ev_fds[e];
            pfd[pfd_count].events = POLLIN;
            ev_pfd_idx[e] = pfd_count++;
        }
    }

    uint8_t uart_buf[128];
    char udp_buf[32768];

    long long last_telemetry_ms = get_time_ms();
    long long last_anim_ms = get_time_ms();
    long long last_pulse_ms = get_time_ms();

    while (g_running) {
        int ret = poll(pfd, pfd_count, 16); // ~60 Hz poll tick

        if (ret > 0) {
            // Hardware UART (GD32 MCU contact) - drain entire FIFO
            if (uart_pfd_idx >= 0 && (pfd[uart_pfd_idx].revents & POLLIN)) {
                int n;
                while ((n = read(uart_fd, uart_buf, sizeof(uart_buf))) > 0) {
                    printf("[UART RX %d B] ", n);
                    for (int i = 0; i < n; i++) printf("%02X ", uart_buf[i]);
                    printf("\n");
                    fflush(stdout);
                    for (int i = 0; i < n; i++) {
                        parse_qmk_byte(uart_buf[i]);
                    }
                }
            }

            // Input Event Devices
            for (int e = 0; e < 4; e++) {
                if (ev_pfd_idx[e] >= 0 && (pfd[ev_pfd_idx[e]].revents & POLLIN)) {
                    struct input_event evs[16];
                    int n = read(ev_fds[e], evs, sizeof(evs));
                    if (n > 0) {
                        int count = n / (int)sizeof(struct input_event);
                        for (int i = 0; i < count; i++) {
                            if (evs[i].type == 0) continue; // Skip EV_SYN
                            printf("[INPUT EV%d] type=%u code=%u val=%d\n", e, evs[i].type, evs[i].code, evs[i].value);
                            fflush(stdout);
                            if (evs[i].type == EV_REL) {
                                if (evs[i].value > 0) on_right_knob(+1);
                                else if (evs[i].value < 0) on_right_knob(-1);
                            } else if (evs[i].type == EV_KEY && evs[i].value == 1) {
                                if (evs[i].code == 28 || evs[i].code == 352) { // ENTER / OK
                                    on_right_knob_click();
                                } else if (evs[i].code == 115 || evs[i].code == 103) { // Vol Up / Up
                                    on_right_knob(+1);
                                } else if (evs[i].code == 114 || evs[i].code == 108) { // Vol Down / Down
                                    on_right_knob(-1);
                                }
                            }
                        }
                    }
                }
            }

            // UDP Packet - drain socket
            if (udp_pfd_idx >= 0 && (pfd[udp_pfd_idx].revents & POLLIN)) {
                struct sockaddr_in cliaddr;
                socklen_t len = sizeof(cliaddr);
                int n;
                while ((n = recvfrom(sockfd, udp_buf, sizeof(udp_buf) - 1, 0, (struct sockaddr *)&cliaddr, &len)) > 0) {
                    udp_buf[n] = '\0';
                    handle_udp_packet(udp_buf, n, &cliaddr);
                }
            }
        }

        long long now = get_time_ms();
        if (g_knob_toggle_pending) {
            g_knob_toggle_pending = 0; // Retain a chord even if press/release arrive in one UART batch.
            if (g_saved_keymap_valid) {
                g_pc_keys_on = !g_pc_keys_on; apply_pc_key_mode(uart_fd);
                snprintf(g_v2_state.top_subtitle, sizeof g_v2_state.top_subtitle, "PC keyboard %s", g_pc_keys_on ? "ON" : "OFF");
                g_dirty_top = 1;
            }
        }

        if (g_mode_v2) {
            if (now - g_last_host_sync_ms > 15000 && !g_host_offline) {
                g_host_offline = 1;
                snprintf(g_v2_state.top_title, sizeof g_v2_state.top_title, "Host disconnected");
                snprintf(g_v2_state.top_subtitle, sizeof g_v2_state.top_subtitle, "Displayed session state is stale");
                snprintf(g_v2_state.top_body, sizeof g_v2_state.top_body, "Start Snowball middleware on PC.\nDevice buttons need the host.\nWindows dictation PoC is separate.\nNo active recording is confirmed.");
                for (int k = 1; k <= 20; k++) g_v2_state.keys[k].flags |= KEY_FLAG_DISABLED;
                g_dirty_keys = 0x1FFFFE; g_dirty_top = 1;
            }
            static long long last_host_ping = 0;
            if (g_has_host_addr && now - last_host_ping > 1000) {
                last_host_ping = now;
                sendto(g_sockfd, "{\"type\":\"ping\"}", 15, 0, (struct sockaddr *)&g_host_addr, sizeof g_host_addr);
            }
            static long long last_v2_anim_ms = 0;
            if (now - last_v2_anim_ms >= 50) {
                last_v2_anim_ms = now;
                g_v2_tick_count++;
                for (int k = 1; k <= 20; k++) {
                    if (g_v2_state.keys[k].main[0] && strlen(g_v2_state.keys[k].main) > 14) {
                        g_dirty_keys |= (1 << k);
                    }
                    if (g_v2_state.keys[k].sub[0] && strlen(g_v2_state.keys[k].sub) > 14) {
                        g_dirty_keys |= (1 << k);
                    }
                }
            }
        }

        if (!g_mode_v2) {
            // 1. Dynamic Tempo Metronome Pulse (~33ms tick, 30 FPS) -> Key 4
            if (now - last_pulse_ms >= 33) {
                long long dt_ms = now - last_pulse_ms;
                last_pulse_ms = now;
                float dt_sec = (float)dt_ms / 1000.0f;
                float bps = (float)g_pat10_metronome_bpm / 60.0f;
                g_pat10_phase += 2.0f * (float)M_PI * bps * dt_sec;
                while (g_pat10_phase >= 2.0f * (float)M_PI) g_pat10_phase -= 2.0f * (float)M_PI;
                g_dirty_keys |= (1 << 4);
            }

            // 2. Pattern 6 Horizontal Scroll Animation Tick (~16ms) -> Key 8
            if (g_pat6_animating) {
                long long elapsed = now - g_pat6_anim_start_ms;
                float dur = 220.0f; // 220ms ease-out
                if (elapsed >= (long long)dur) {
                    g_pat6_animating = 0;
                    g_pat6_anim_offset = 0.0f;
                    g_pat6_icon_idx = (g_pat6_icon_idx + 3) % 4;
                } else {
                    float t = (float)elapsed / dur;
                    float p = 1.0f - powf(1.0f - t, 3.0f); // Cubic ease-out
                    g_pat6_anim_offset = p * 40.0f;
                }
                g_dirty_keys |= (1 << 8);
            }

            // 3. 30 FPS Physics Lerp (every 33ms) -> Key 12 & Top Display
            if (now - last_anim_ms >= 33) {
                last_anim_ms = now;

                float diff = (float)g_pat12_target_val - g_pat12_current_val;
                if (fabsf(diff) > 0.01f) {
                    g_pat12_current_val += diff * 0.30f;
                    g_dirty_keys |= (1 << 12);
                    g_dirty_top = 1;
                } else if (g_pat12_current_val != (float)g_pat12_target_val) {
                    g_pat12_current_val = (float)g_pat12_target_val;
                    g_dirty_keys |= (1 << 12);
                    g_dirty_top = 1;
                }
            }

            // 4. 1-Second Telemetry Tick (Pattern 8 CPU Number & Pattern 9 CPU Graph)
            if (now - last_telemetry_ms >= 1000) {
                last_telemetry_ms = now;

                g_pat8_cpu_pct = read_cpu_percent();
                for (int i = 0; i < 59; i++) {
                    g_pat9_cpu_history[i] = g_pat9_cpu_history[i + 1];
                }
                g_pat9_cpu_history[59] = g_pat8_cpu_pct;

                g_dirty_keys |= (1 << 9) | (1 << 10);
                g_dirty_top = 1;
            }

            // 5. Clear overlays if expired
            if (g_state.dial_until_ms > 0 && now >= g_state.dial_until_ms) {
                g_state.dial_until_ms = 0;
                g_state.dial_action[0] = '\0';
                g_dirty_top = 1;
            }
            if (g_state.scroll_until_ms > 0 && now >= g_state.scroll_until_ms) {
                g_state.scroll_until_ms = 0;
                g_state.scroll_action[0] = '\0';
                g_dirty_top = 1;
            }
        }

        // Batch flush all dirty keys
        if (g_dirty_keys) {
            for (int k = 1; k <= 20; k++) {
                if (g_dirty_keys & (1 << k)) {
                    render_key_by_index(k);
                }
            }
            g_dirty_keys = 0;
        }

        // Rate-limit Top Display flush (<= 20 FPS, interval >= 50ms)
        if (g_dirty_top && (now - g_last_top_flush_ms >= 50)) {
            g_last_top_flush_ms = now;
            g_dirty_top = 0;
            render_top_display();
        }
    }

    printf("[MK20-HUD] Shutting down cleanly...\n");
    if (uart_fd >= 0) close(uart_fd);
    if (sockfd >= 0) close(sockfd);
    if (g_top_fb) munmap(g_top_fb, TOP_FB_BYTES);
    if (g_top_fd >= 0) close(g_top_fd);
    for (int k = 1; k <= 20; k++) {
        if (g_key_fbs[k]) munmap(g_key_fbs[k], KEY_FB_BYTES);
        if (g_key_fds[k] >= 0) close(g_key_fds[k]);
    }

    return 0;
}
