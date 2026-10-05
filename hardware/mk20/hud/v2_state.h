#ifndef V2_STATE_H
#define V2_STATE_H

#include <stdint.h>

#define KEY_FLAG_FILLED   (1 << 0)
#define KEY_FLAG_EDITING  (1 << 1)
#define KEY_FLAG_FOCUSED  (1 << 2)
#define KEY_FLAG_DISABLED (1 << 3)
#define KEY_FLAG_LIST     (1 << 4)
#define KEY_FLAG_LINES    (1 << 5)

typedef struct {
    uint8_t id;          // 1..20
    char top[16];
    char main[24];
    char sub[24];
    uint8_t flags;
    uint8_t item_count;
    uint8_t active_item_idx;
    char items[8][24];
    uint8_t item_colors[8];
} V2_Key;

#include "gfx_prims.h"

typedef struct {
    char skin_id[32];
    char skin_name[40];
    char font_korean[32];
    char font_english[32];

    // Top display colors
    uint16_t top_bg;
    uint16_t top_card;
    uint16_t top_border;
    uint16_t top_text;
    uint16_t top_dim;
    uint16_t top_accent;

    // Legacy compatible alias fields
    uint16_t key_default_bg;
    uint16_t key_default_border;
    uint16_t key_default_text;
    uint16_t key_default_sub;

    // Button Style
    uint8_t  btn_line_visible;       // 1 = draw border, 0 = no border
    uint16_t btn_line_color;         // border color
    uint8_t  btn_fill_visible;       // 1 = fill bg, 0 = don't fill
    uint16_t btn_fill_color;         // default fill color

    // Button Title Style
    uint8_t  title_line_visible;     // 1 = draw title bottom line or pill border
    uint16_t title_line_color;       // title line color
    uint8_t  title_fill_visible;     // 1 = draw title pill bg
    uint16_t title_fill_color;       // title pill bg color
    uint16_t title_font_color;       // title font color

    // Button Font Colors
    uint16_t btn_main_font_color;
    uint16_t btn_sub_font_color;
    uint8_t  auto_font_reverse;      // 1 = reverse font by bg luminance

    // Button Row Background Color Group (Row 0, 1, 2, 3)
    uint8_t  row_bg_enabled;
    uint16_t row_bg_colors[4];

    // Individual Button Background Color (1..20)
    uint16_t individual_button_bg[21];

    // Gradation support for button background
    V2_GradientType btn_gradient_type;
    uint16_t btn_gradient_start;
    uint16_t btn_gradient_end;

    // Button Bottom Message = Debug (0 = Off for all, 1 = Show K[id] [R:C])
    uint8_t  debug_visible;

    // Dynamic states
    uint16_t key_filled_bg;
    uint16_t key_filled_text;
    uint16_t key_focused_border;
    uint16_t key_editing_border;
    uint16_t palette[6];
} V2_Theme;

static inline int v2_get_key_row(int key_idx) {
    if (key_idx < 1 || key_idx > 20) return 0;
    return (key_idx - 1) % 4;
}

static inline int v2_get_key_col(int key_idx) {
    if (key_idx < 1 || key_idx > 20) return 0;
    return 4 - ((key_idx - 1) / 4);
}

typedef struct {
    char view_mode[16];  // "session", "workspace", "settings", "question"
    char skin_id[32];
    char top_title[64];
    char top_subtitle[64];
    char top_body[16384]; // multi-line text
    int top_scroll;
    int top_total_lines;
    int volume;
    int is_muted;
    V2_Key keys[21];     // 1..20
} V2_State;

extern V2_State g_v2_state;
extern V2_Theme g_v2_theme;
extern int g_mode_v2;
extern uint32_t g_v2_tick_count;
extern int g_host_offline;
extern char g_v2_controller_id[21];
extern char g_v2_run_id[33];
extern uint32_t g_v2_sequence;

void v2_init_defaults(void);
void v2_apply_theme(const char *skin_id);
int v2_parse_sync_packet(const char *json_str, int len);

#endif // V2_STATE_H
