#include "v2_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

V2_State g_v2_state;
V2_Theme g_v2_theme;
int g_mode_v2 = 1; // Default to V2 mode
uint32_t g_v2_tick_count = 0;

static const V2_Theme s_builtin_themes[] = {
    {
        .skin_id = "slate-dark",
        .skin_name = "Slate Dark",
        .font_korean = "D2Coding",
        .font_english = "D2Coding",
        .top_bg = 0x0863,
        .top_card = 0x10C5,
        .top_border = 0x1947,
        .top_text = 0xE75E,
        .top_dim = 0x9537,
        .top_accent = 0x05B6,
        .key_default_bg = 0x10C5,
        .key_default_border = 0x1947,
        .key_default_text = 0xE75E,
        .key_default_sub = 0x9537,
        .btn_line_visible = 1,
        .btn_line_color = 0x1947,
        .btn_fill_visible = 1,
        .btn_fill_color = 0x10C5,
        .title_line_visible = 0,
        .title_line_color = 0x3186,
        .title_fill_visible = 1,
        .title_fill_color = 0x1947,
        .title_font_color = 0x05B6,
        .btn_main_font_color = 0xE75E,
        .btn_sub_font_color = 0x9537,
        .auto_font_reverse = 1,
        .row_bg_enabled = 1,
        .row_bg_colors = {0x1107, 0x10C5, 0x08A4, 0x0863},
        .individual_button_bg = {[4] = 0x3884, [20] = 0x0964, [16] = 0x09AC},
        .btn_gradient_type = GRADIENT_VERTICAL,
        .btn_gradient_start = 0x1906,
        .btn_gradient_end = 0x0883,
        .debug_visible = 0,
        .key_filled_bg = 0x05B6,
        .key_filled_text = 0xFFFF,
        .key_focused_border = 0x05B6,
        .key_editing_border = 0xF4E1,
        .palette = {0xE75E, 0x05B6, 0x15D0, 0xF4E1, 0xF1E7, 0x9537}
    },
    {
        .skin_id = "matrix-emerald",
        .skin_name = "Matrix Emerald",
        .font_korean = "D2Coding",
        .font_english = "D2Coding",
        .top_bg = 0x0060,
        .top_card = 0x08E1,
        .top_border = 0x1407,
        .top_text = 0x4EF0,
        .top_dim = 0x4EF0,
        .top_accent = 0x262B,
        .key_default_bg = 0x08E1,
        .key_default_border = 0x1407,
        .key_default_text = 0x4EF0,
        .key_default_sub = 0x4EF0,
        .btn_line_visible = 1,
        .btn_line_color = 0x1407,
        .btn_fill_visible = 1,
        .btn_fill_color = 0x08E1,
        .title_line_visible = 0,
        .title_line_color = 0x1407,
        .title_fill_visible = 1,
        .title_fill_color = 0x01E2,
        .title_font_color = 0x4EF0,
        .btn_main_font_color = 0x4EF0,
        .btn_sub_font_color = 0x4EF0,
        .auto_font_reverse = 1,
        .row_bg_enabled = 1,
        .row_bg_colors = {0x0942, 0x08E1, 0x0881, 0x0841},
        .individual_button_bg = {[4] = 0x2861, [20] = 0x12C4},
        .btn_gradient_type = GRADIENT_VERTICAL,
        .btn_gradient_start = 0x09A2,
        .btn_gradient_end = 0x0080,
        .debug_visible = 0,
        .key_filled_bg = 0x262B,
        .key_filled_text = 0x0060,
        .key_focused_border = 0x4EF0,
        .key_editing_border = 0x8775,
        .palette = {0x4EF0, 0x262B, 0x8775, 0xA726, 0xEA28, 0x4EF0}
    },
    {
        .skin_id = "cyberpunk-neon",
        .skin_name = "Cyberpunk Neon",
        .font_korean = "D2Coding",
        .font_english = "D2Coding",
        .top_bg = 0x0843,
        .top_card = 0x1886,
        .top_border = 0x79DD,
        .top_text = 0xF1E7,
        .top_dim = 0xC5BF,
        .top_accent = 0x05B6,
        .key_default_bg = 0x1886,
        .key_default_border = 0x79DD,
        .key_default_text = 0xF1E7,
        .key_default_sub = 0xC5BF,
        .btn_line_visible = 1,
        .btn_line_color = 0x79DD,
        .btn_fill_visible = 1,
        .btn_fill_color = 0x1886,
        .title_line_visible = 0,
        .title_line_color = 0xF1E7,
        .title_fill_visible = 1,
        .title_fill_color = 0x28C8,
        .title_font_color = 0x05B6,
        .btn_main_font_color = 0xF1E7,
        .btn_sub_font_color = 0xC5BF,
        .auto_font_reverse = 1,
        .row_bg_enabled = 1,
        .row_bg_colors = {0x20C8, 0x1886, 0x1065, 0x0823},
        .individual_button_bg = {[4] = 0x58A3, [16] = 0x410C, [20] = 0x0A0E},
        .btn_gradient_type = GRADIENT_LT_TO_RB,
        .btn_gradient_start = 0x2908,
        .btn_gradient_end = 0x0862,
        .debug_visible = 0,
        .key_filled_bg = 0xF1E7,
        .key_filled_text = 0xFFFF,
        .key_focused_border = 0x05B6,
        .key_editing_border = 0xFE62,
        .palette = {0xFFDF, 0x05B6, 0x15D0, 0xFE62, 0xF1E7, 0xC5BF}
    },
    {
        .skin_id = "amber-crt",
        .skin_name = "Amber CRT",
        .font_korean = "D2Coding",
        .font_english = "D2Coding",
        .top_bg = 0x1060,
        .top_card = 0x20A0,
        .top_border = 0x79A1,
        .top_text = 0xF4E1,
        .top_dim = 0xFDF4,
        .top_accent = 0xFDF4,
        .key_default_bg = 0x20A0,
        .key_default_border = 0x79A1,
        .key_default_text = 0xF4E1,
        .key_default_sub = 0xFDF4,
        .btn_line_visible = 1,
        .btn_line_color = 0x79A1,
        .btn_fill_visible = 1,
        .btn_fill_color = 0x20A0,
        .title_line_visible = 0,
        .title_line_color = 0xB281,
        .title_fill_visible = 1,
        .title_fill_color = 0x28C0,
        .title_font_color = 0xFDF4,
        .btn_main_font_color = 0xF4E1,
        .btn_sub_font_color = 0xFDF4,
        .auto_font_reverse = 1,
        .row_bg_enabled = 1,
        .row_bg_colors = {0x2901, 0x20A0, 0x1860, 0x1020},
        .individual_button_bg = {[4] = 0x30E1, [20] = 0x31C2},
        .btn_gradient_type = GRADIENT_VERTICAL,
        .btn_gradient_start = 0x2921,
        .btn_gradient_end = 0x1040,
        .debug_visible = 0,
        .key_filled_bg = 0xF4E1,
        .key_filled_text = 0x1060,
        .key_focused_border = 0xFDF4,
        .key_editing_border = 0xFF51,
        .palette = {0xFF98, 0xFDF4, 0xF4E1, 0xDBA0, 0xB281, 0xFDF4}
    },
    {
        .skin_id = "high-contrast",
        .skin_name = "High Contrast",
        .font_korean = "D2Coding",
        .font_english = "D2Coding",
        .top_bg = 0x0000,
        .top_card = 0x1082,
        .top_border = 0xFFFF,
        .top_text = 0xFFFF,
        .top_dim = 0xA514,
        .top_accent = 0xFFFF,
        .key_default_bg = 0x0000,
        .key_default_border = 0xFFFF,
        .key_default_text = 0xFFFF,
        .key_default_sub = 0xA514,
        .btn_line_visible = 1,
        .btn_line_color = 0xFFFF,
        .btn_fill_visible = 1,
        .btn_fill_color = 0x0000,
        .title_line_visible = 1,
        .title_line_color = 0xFFFF,
        .title_fill_visible = 1,
        .title_fill_color = 0x18C3,
        .title_font_color = 0xFFFF,
        .btn_main_font_color = 0xFFFF,
        .btn_sub_font_color = 0xA514,
        .auto_font_reverse = 1,
        .row_bg_enabled = 0,
        .row_bg_colors = {0, 0, 0, 0},
        .individual_button_bg = {0},
        .btn_gradient_type = GRADIENT_NONE,
        .debug_visible = 0,
        .key_filled_bg = 0xFFFF,
        .key_filled_text = 0x0000,
        .key_focused_border = 0xFFFF,
        .key_editing_border = 0xFFFF,
        .palette = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x8C51}
    }
};

void v2_apply_theme(const char *skin_id) {
    if (!skin_id || !*skin_id) {
        g_v2_theme = s_builtin_themes[0];
        return;
    }
    for (size_t i = 0; i < sizeof(s_builtin_themes)/sizeof(s_builtin_themes[0]); i++) {
        if (strcmp(skin_id, s_builtin_themes[i].skin_id) == 0) {
            g_v2_theme = s_builtin_themes[i];
            return;
        }
    }
    g_v2_theme = s_builtin_themes[0];
}

void v2_init_defaults(void) {
    memset(&g_v2_state, 0, sizeof(g_v2_state));
    strncpy(g_v2_state.skin_id, "slate-dark", sizeof(g_v2_state.skin_id) - 1);
    v2_apply_theme("slate-dark");
    strncpy(g_v2_state.view_mode, "session", sizeof(g_v2_state.view_mode) - 1);
    strncpy(g_v2_state.top_title, "Snowball Control V2", sizeof(g_v2_state.top_title) - 1);
    strncpy(g_v2_state.top_subtitle, "Waiting for Host...", sizeof(g_v2_state.top_subtitle) - 1);
    strncpy(g_v2_state.top_body, "Connecting to Host middleware (port 7701)...\nRotate Left Knob to test inputs.", sizeof(g_v2_state.top_body) - 1);
    g_v2_state.volume = 75;
    g_v2_state.is_muted = 0;

    // Physical Matrix Default Setup
    for (int i = 1; i <= 20; i++) {
        g_v2_state.keys[i].id = (uint8_t)i;
        g_v2_state.keys[i].flags = KEY_FLAG_DISABLED;
    }
    strcpy(g_v2_state.keys[1].main, "Waiting for host");
}

static const char *skip_whitespace(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return p;
}

static const char *find_json_object_end(const char *pos) {
    if (!pos || *pos != '{') return NULL;
    int depth = 0;
    int in_string = 0;
    int escape = 0;
    for (const char *p = pos; *p; p++) {
        if (escape) {
            escape = 0;
            continue;
        }
        if (*p == '\\') {
            if (in_string) escape = 1;
            continue;
        }
        if (*p == '"') {
            in_string = !in_string;
            continue;
        }
        if (!in_string) {
            if (*p == '{') depth++;
            else if (*p == '}') {
                depth--;
                if (depth == 0) return p;
            }
        }
    }
    return NULL;
}

/* Consume the entire quoted value even when the display buffer is full. */
static const char *read_display_string(const char *pos, const char *end, char *out, int cap) {
    if (!pos || *pos != '"' || cap < 1) return NULL;
    pos++;
    int idx = 0;
    while (*pos && (!end || pos < end) && *pos != '"') {
        unsigned char value = (unsigned char)*pos++;
        if (value == '\\') {
            if (!*pos || (end && pos >= end)) return NULL;
            value = (unsigned char)*pos++;
            if (value == 'n') value = '\n';
            else if (value == 'r') value = '\r';
            else if (value == 't') value = '\t';
        }
        if (idx < cap - 1) out[idx++] = (char)value;
    }
    if (!*pos || (end && pos >= end)) return NULL;
    /* Do not leave an incomplete UTF-8 character at the byte limit. */
    if (idx) {
        int lead = idx - 1;
        while (lead > 0 && ((unsigned char)out[lead] & 0xc0) == 0x80) lead--;
        unsigned char ch = (unsigned char)out[lead];
        int width = ch >= 0xf0 ? 4 : ch >= 0xe0 ? 3 : ch >= 0xc0 ? 2 : 1;
        if (idx - lead < width) idx = lead;
    }
    out[idx] = '\0';
    return pos + 1;
}

static int parse_string_field_bounded(const char *json, const char *end, const char *key, char *out, int max_len) {
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    int nlen = strlen(needle);
    const char *pos = json;
    const char *found = NULL;

    while (pos && (!end || pos < end)) {
        pos = strstr(pos, needle);
        if (!pos || (end && pos >= end)) break;
        // Verify it is a key followed by ':'
        const char *after = skip_whitespace(pos + nlen);
        if (*after == ':') {
            found = after + 1;
            break;
        }
        pos += nlen;
    }
    if (!found) return 0;

    return read_display_string(skip_whitespace(found), end, out, max_len) != NULL;
}

static int parse_int_field_bounded(const char *json, const char *end, const char *key, int *out) {
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    int nlen = strlen(needle);
    const char *pos = json;
    const char *found = NULL;

    while (pos && (!end || pos < end)) {
        pos = strstr(pos, needle);
        if (!pos || (end && pos >= end)) break;
        const char *after = skip_whitespace(pos + nlen);
        if (*after == ':') {
            found = after + 1;
            break;
        }
        pos += nlen;
    }
    if (!found) return 0;

    pos = skip_whitespace(found);
    *out = atoi(pos);
    return 1;
}

static int parse_bool_field_bounded(const char *json, const char *end, const char *key, int *out) {
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    int nlen = strlen(needle);
    const char *pos = json;
    const char *found = NULL;

    while (pos && (!end || pos < end)) {
        pos = strstr(pos, needle);
        if (!pos || (end && pos >= end)) break;
        const char *after = skip_whitespace(pos + nlen);
        if (*after == ':') {
            found = after + 1;
            break;
        }
        pos += nlen;
    }
    if (!found) return 0;

    pos = skip_whitespace(found);
    if (strncmp(pos, "true", 4) == 0) {
        *out = 1;
        return 1;
    } else if (strncmp(pos, "false", 5) == 0) {
        *out = 0;
        return 1;
    }
    return 0;
}

static int parse_string_field(const char *json, const char *key, char *out, int max_len) {
    return parse_string_field_bounded(json, NULL, key, out, max_len);
}

static int parse_int_field(const char *json, const char *key, int *out) {
    return parse_int_field_bounded(json, NULL, key, out);
}

static int parse_bool_field(const char *json, const char *key, int *out) {
    return parse_bool_field_bounded(json, NULL, key, out);
}

int v2_parse_sync_packet(const char *json, int len) {
    if (!json || len < 10) return 0;
    if (!strstr(json, "\"type\":\"v2_sync\"") && !strstr(json, "\"type\": \"v2_sync\"")) {
        return 0;
    }

    parse_string_field(json, "viewMode", g_v2_state.view_mode, sizeof(g_v2_state.view_mode));
    if (parse_string_field(json, "skinId", g_v2_state.skin_id, sizeof(g_v2_state.skin_id))) {
        v2_apply_theme(g_v2_state.skin_id);
    }
    int opt_debug = 0;
    if (parse_bool_field(json, "debug", &opt_debug)) {
        g_v2_theme.debug_visible = (uint8_t)opt_debug;
    }
    int opt_line_vis = 0;
    if (parse_bool_field(json, "lineVisible", &opt_line_vis)) {
        g_v2_theme.btn_line_visible = (uint8_t)opt_line_vis;
    }
    int opt_fill_vis = 0;
    if (parse_bool_field(json, "fillVisible", &opt_fill_vis)) {
        g_v2_theme.btn_fill_visible = (uint8_t)opt_fill_vis;
    }
    int opt_auto_rev = 0;
    if (parse_bool_field(json, "autoFontReverse", &opt_auto_rev)) {
        g_v2_theme.auto_font_reverse = (uint8_t)opt_auto_rev;
    }
    parse_string_field(json, "topTitle", g_v2_state.top_title, sizeof(g_v2_state.top_title));
    parse_string_field(json, "topSubtitle", g_v2_state.top_subtitle, sizeof(g_v2_state.top_subtitle));
    parse_string_field(json, "topBody", g_v2_state.top_body, sizeof(g_v2_state.top_body));
    parse_int_field(json, "topScroll", &g_v2_state.top_scroll);
    parse_int_field(json, "topTotalLines", &g_v2_state.top_total_lines);
    parse_int_field(json, "volume", &g_v2_state.volume);
    parse_bool_field(json, "isMuted", &g_v2_state.is_muted);

    // Parse keys array: "keys":[ { ... }, ... ]
    const char *kpos = strstr(json, "\"keys\"");
    if (kpos) {
        kpos = strchr(kpos, '[');
        while (kpos && *kpos != ']') {
            kpos = strchr(kpos, '{');
            if (!kpos) break;
            const char *end_obj = find_json_object_end(kpos);
            if (!end_obj) break;

            int kid = 0, flags = 0, active_item = 0;
            char top[16] = "", main_lbl[24] = "", sub[24] = "";
            char items[8][24];
            uint8_t item_colors[8];
            int item_count = 0;
            memset(items, 0, sizeof(items));
            memset(item_colors, 0, sizeof(item_colors));

            parse_int_field_bounded(kpos, end_obj, "id", &kid);
            parse_string_field_bounded(kpos, end_obj, "top", top, sizeof(top));
            parse_string_field_bounded(kpos, end_obj, "main", main_lbl, sizeof(main_lbl));
            parse_string_field_bounded(kpos, end_obj, "sub", sub, sizeof(sub));
            parse_int_field_bounded(kpos, end_obj, "flags", &flags);
            parse_int_field_bounded(kpos, end_obj, "activeItem", &active_item);

            // Parse items array: "items":["a","b","c"]
            const char *itemspos = strstr(kpos, "\"items\"");
            if (itemspos && itemspos < end_obj) {
                const char *arr = strchr(itemspos, '[');
                if (arr && arr < end_obj) {
                    const char *p = arr + 1;
                    while (p < end_obj && *p != ']' && item_count < 8) {
                        p = skip_whitespace(p);
                        if (*p == '\"') {
                            const char *next = read_display_string(p, end_obj, items[item_count], sizeof(items[item_count]));
                            if (!next) break;
                            p = next;
                            item_count++;
                        } else {
                            p++;
                        }
                    }
                }
            }

            // Parse colors array: "colors":[0, 1, 2, ...]
            int color_count = 0;
            const char *colorspos = strstr(kpos, "\"colors\"");
            if (colorspos && colorspos < end_obj) {
                const char *arr = strchr(colorspos, '[');
                if (arr && arr < end_obj) {
                    const char *p = arr + 1;
                    while (p < end_obj && *p != ']' && color_count < 8) {
                        p = skip_whitespace(p);
                        if (*p >= '0' && *p <= '9') {
                            item_colors[color_count++] = (uint8_t)atoi(p);
                            while (p < end_obj && *p >= '0' && *p <= '9') p++;
                        } else {
                            p++;
                        }
                    }
                }
            }

            if (kid >= 1 && kid <= 20) {
                // Cleanly reset key before assigning new state to prevent stale data retention
                memset(&g_v2_state.keys[kid], 0, sizeof(V2_Key));
                g_v2_state.keys[kid].id = (uint8_t)kid;
                strncpy(g_v2_state.keys[kid].top, top, sizeof(g_v2_state.keys[kid].top) - 1);
                strncpy(g_v2_state.keys[kid].main, main_lbl, sizeof(g_v2_state.keys[kid].main) - 1);
                strncpy(g_v2_state.keys[kid].sub, sub, sizeof(g_v2_state.keys[kid].sub) - 1);
                g_v2_state.keys[kid].flags = (uint8_t)flags;
                int total_val = 0;
                if (parse_int_field_bounded(kpos, end_obj, "total", &total_val) && total_val > 0) {
                    item_count = total_val;
                }
                g_v2_state.keys[kid].item_count = (uint8_t)item_count;
                g_v2_state.keys[kid].active_item_idx = (uint8_t)active_item;
                for (int m = 0; m < item_count && m < 8; m++) {
                    strncpy(g_v2_state.keys[kid].items[m], items[m], sizeof(g_v2_state.keys[kid].items[m]) - 1);
                }
                for (int m = 0; m < 8; m++) {
                    g_v2_state.keys[kid].item_colors[m] = item_colors[m];
                }
            }

            kpos = end_obj + 1;
        }
    }

    return 1;
}
