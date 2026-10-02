#include "v2_render.h"
#include "v2_state.h"
#include "gfx_prims.h"
#include "unicode_text.h"
#include <stdio.h>
#include <string.h>

void v2_render_key_frame(uint16_t *fb, int key_idx, int pressed) {
    if (!fb || key_idx < 1 || key_idx > 20) return;
    V2_Key *k = &g_v2_state.keys[key_idx];

    // 1. If key is disabled or completely empty, draw completely dark blank (backlight off)
    if ((k->flags & KEY_FLAG_DISABLED) ||
        (!k->top[0] && !k->main[0] && !k->sub[0] && k->item_count == 0 && !(k->flags & (KEY_FLAG_FILLED | KEY_FLAG_EDITING | KEY_FLAG_FOCUSED)))) {
        draw_rect_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, COLOR_BLACK);
        return;
    }

    // 2. Dense Multi-Line Text Canvas (Code viewer & Git diff modal)
    if (k->flags & KEY_FLAG_LINES) {
        draw_rect_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, COLOR_BLACK);
        if (k->item_count == 0) return;
        int start_y = 7;
        int line_h = 19;
        for (int i = 0; i < k->item_count && i < 8; i++) {
            int y = start_y + i * line_h;
            uint16_t col = g_v2_theme.btn_main_font_color;
            switch (k->item_colors[i]) {
                case 1: col = g_v2_theme.top_accent; break;        // additions (+)
                case 2: col = COLOR_ROSE; break;                   // deletions (-)
                case 3: col = g_v2_theme.key_editing_border; break;// hunks (@@)
                case 4: col = g_v2_theme.top_dim; break;           // dim
                case 5: col = g_v2_theme.btn_sub_font_color; break;// context
                default: col = g_v2_theme.btn_main_font_color; break;
            }
            draw_string_16(fb, KEY_W, 4, y, k->items[i], col, 1);
        }
        return;
    }

    // Determine row (0..3)
    int row = v2_get_key_row(key_idx);

    // 3. Base Background Color Resolution
    uint16_t bg_color = g_v2_theme.btn_fill_color;
    if (g_v2_theme.row_bg_enabled && row >= 0 && row < 4 && g_v2_theme.row_bg_colors[row] != 0) {
        bg_color = g_v2_theme.row_bg_colors[row];
    }
    if (g_v2_theme.individual_button_bg[key_idx] != 0) {
        bg_color = g_v2_theme.individual_button_bg[key_idx];
    }
    if (k->flags & KEY_FLAG_FILLED) {
        bg_color = g_v2_theme.key_filled_bg;
    }
    if (pressed) {
        bg_color = g_v2_theme.top_accent;
    }

    // 4. Background Rendering (Fill visible & Gradation support, clipped to rounded rectangle)
    if (g_v2_theme.btn_fill_visible || (k->flags & KEY_FLAG_FILLED) || pressed) {
        if (!(k->flags & KEY_FLAG_FILLED) && !pressed &&
            g_v2_theme.individual_button_bg[key_idx] == 0 &&
            g_v2_theme.btn_gradient_type != GRADIENT_NONE) {
            draw_gradient_round_rect_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, KEY_CORNER_RADIUS,
                                       g_v2_theme.btn_gradient_start,
                                       g_v2_theme.btn_gradient_end,
                                       g_v2_theme.btn_gradient_type);
        } else {
            draw_round_rect_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, KEY_CORNER_RADIUS, bg_color);
        }
    } else {
        draw_round_rect_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, KEY_CORNER_RADIUS, COLOR_BLACK);
    }

    // 5. Multi-Item List Mode
    if ((k->flags & KEY_FLAG_LIST) && k->item_count > 0) {
        if (k->top[0]) {
            draw_title_bar_16(fb, KEY_W, KEY_W, KEY_TITLE_H, KEY_CORNER_RADIUS,
                              g_v2_theme.title_fill_color, g_v2_theme.title_line_color,
                              g_v2_theme.title_fill_visible, g_v2_theme.title_line_visible);
            uint16_t top_c = (k->flags & KEY_FLAG_FILLED) ? g_v2_theme.key_filled_text : g_v2_theme.title_font_color;
            uint16_t title_bg = g_v2_theme.title_fill_visible ? g_v2_theme.title_fill_color : bg_color;
            top_c = get_contrast_font_color(title_bg, top_c, g_v2_theme.auto_font_reverse);
            draw_string_centered_16(fb, KEY_W, 3, k->top, top_c, 1);
        }

        int start_y = 26;
        int line_h = 24;
        for (int i = 0; i < k->item_count && i < 4; i++) {
            int y = start_y + i * line_h;
            if (i == k->active_item_idx) {
                uint16_t pill_bg = (k->flags & KEY_FLAG_EDITING) ? COLOR_AMBER_BG : g_v2_theme.btn_fill_color;
                uint16_t pill_border = (k->flags & KEY_FLAG_EDITING) ? g_v2_theme.key_editing_border : g_v2_theme.key_focused_border;
                draw_rect_16(fb, KEY_W, 6, y - 2, KEY_W - 12, 21, pill_bg);
                draw_border_16(fb, KEY_W, 6, y - 2, KEY_W - 12, 21, 1, pill_border);
                uint16_t arrow_c = (k->flags & KEY_FLAG_EDITING) ? g_v2_theme.key_editing_border : g_v2_theme.top_accent;
                draw_string_16(fb, KEY_W, 10, y, ">", arrow_c, 1);
                uint16_t text_c = get_contrast_font_color(pill_bg, g_v2_theme.key_filled_text, g_v2_theme.auto_font_reverse);
                draw_string_16(fb, KEY_W, 22, y, k->items[i], text_c, 1);
            } else {
                uint16_t text_c = get_contrast_font_color(bg_color, g_v2_theme.btn_sub_font_color, g_v2_theme.auto_font_reverse);
                draw_string_16(fb, KEY_W, 22, y, k->items[i], text_c, 1);
            }
        }

        if (k->sub[0]) {
            uint16_t base_sub = (k->flags & KEY_FLAG_FILLED) ? g_v2_theme.key_filled_text : g_v2_theme.btn_sub_font_color;
            uint16_t sub_c = get_contrast_font_color(bg_color, base_sub, g_v2_theme.auto_font_reverse);
            draw_string_centered_16(fb, KEY_W, 102, k->sub, sub_c, 1);
        }
    } else {
        // 6. Button Title Style (Full-width rounded top banner, line visible, title font color, auto reverse)
        if (k->flags & KEY_FLAG_EDITING) {
            draw_title_bar_16(fb, KEY_W, KEY_W, KEY_TITLE_H, KEY_CORNER_RADIUS,
                              g_v2_theme.title_fill_color, g_v2_theme.key_editing_border,
                              g_v2_theme.title_fill_visible, g_v2_theme.title_line_visible);
            uint16_t title_bg = g_v2_theme.title_fill_visible ? g_v2_theme.title_fill_color : bg_color;
            uint16_t title_c = get_contrast_font_color(title_bg, g_v2_theme.key_editing_border, g_v2_theme.auto_font_reverse);
            draw_string_centered_16(fb, KEY_W, 3, "Editing", title_c, 1);
        } else if (k->top[0]) {
            draw_title_bar_16(fb, KEY_W, KEY_W, KEY_TITLE_H, KEY_CORNER_RADIUS,
                              g_v2_theme.title_fill_color, g_v2_theme.title_line_color,
                              g_v2_theme.title_fill_visible, g_v2_theme.title_line_visible);
            uint16_t base_title_font = (k->flags & KEY_FLAG_FILLED) ? g_v2_theme.key_filled_text : g_v2_theme.title_font_color;
            uint16_t title_bg = g_v2_theme.title_fill_visible ? g_v2_theme.title_fill_color : bg_color;
            uint16_t title_c = get_contrast_font_color(title_bg, base_title_font, g_v2_theme.auto_font_reverse);
            draw_string_centered_16(fb, KEY_W, 3, k->top, title_c, 1);
        }

        // 7. Main Label (Button Font Color + Automatic Font Color reversed by Background)
        if (k->main[0]) {
            int len = strlen(k->main);
            uint16_t base_main = (k->flags & KEY_FLAG_FILLED) ? g_v2_theme.key_filled_text : g_v2_theme.btn_main_font_color;
            uint16_t main_color = get_contrast_font_color(bg_color, base_main, g_v2_theme.auto_font_reverse);
            if (len <= 14) {
                int x = (KEY_W - len * 8) / 2;
                draw_string_16(fb, KEY_W, x, 52, k->main, main_color, 1);
            } else {
                int text_w = len * 8;
                int span = text_w + 32;
                int scroll_px = (g_v2_tick_count * 2) % span;
                int pos_x = 8 - scroll_px;
                draw_string_clipped_16(fb, KEY_W, pos_x, 52, k->main, main_color, 1, 8, 48, 112, 24);
                if (pos_x + span < 120) {
                    draw_string_clipped_16(fb, KEY_W, pos_x + span, 52, k->main, main_color, 1, 8, 48, 112, 24);
                }
            }
        }

        // 8. Sub Label / Debug Message (Button Bottom Message = Debug, Debug visible: Off for all)
        if (g_v2_theme.debug_visible) {
            char dbg_buf[24];
            snprintf(dbg_buf, sizeof(dbg_buf), "K%d [R%d:C%d]", key_idx, v2_get_key_row(key_idx), v2_get_key_col(key_idx));
            uint16_t dbg_color = get_contrast_font_color(bg_color, g_v2_theme.top_accent, g_v2_theme.auto_font_reverse);
            draw_string_centered_16(fb, KEY_W, 96, dbg_buf, dbg_color, 1);
        } else if (k->sub[0]) {
            int sub_len = strlen(k->sub);
            uint16_t base_sub = (k->flags & KEY_FLAG_FILLED) ? g_v2_theme.key_filled_text : g_v2_theme.btn_sub_font_color;
            uint16_t sub_color = get_contrast_font_color(bg_color, base_sub, g_v2_theme.auto_font_reverse);
            if (sub_len <= 14) {
                draw_string_centered_16(fb, KEY_W, 96, k->sub, sub_color, 1);
            } else {
                int sub_w = sub_len * 8;
                int sub_span = sub_w + 32;
                int sub_scroll_px = (g_v2_tick_count * 2) % sub_span;
                int sub_pos_x = 8 - sub_scroll_px;
                draw_string_clipped_16(fb, KEY_W, sub_pos_x, 96, k->sub, sub_color, 1, 8, 92, 112, 20);
                if (sub_pos_x + sub_span < 120) {
                    draw_string_clipped_16(fb, KEY_W, sub_pos_x + sub_span, 96, k->sub, sub_color, 1, 8, 92, 112, 20);
                }
            }
        }
    }

    // 9. Outer Button Border (Rounded border outline following button screen curvature)
    if (k->flags & KEY_FLAG_FILLED) {
        draw_round_border_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, KEY_CORNER_RADIUS, 1, g_v2_theme.top_accent);
    } else if (g_v2_theme.btn_line_visible) {
        draw_round_border_16(fb, KEY_W, 0, 0, KEY_W, KEY_H, KEY_CORNER_RADIUS, 1, g_v2_theme.btn_line_color);
    }

    // 10. Active Editing Indicator (Editing Inset Rounded Border)
    if (k->flags & KEY_FLAG_EDITING) {
        draw_round_border_16(fb, KEY_W, 2, 2, KEY_W - 4, KEY_H - 4,
                             KEY_CORNER_RADIUS > 2 ? KEY_CORNER_RADIUS - 2 : 2, 2,
                             g_v2_theme.key_editing_border);
    }

    // 11. Cursor Focused Outline (Focused Inset Rounded Border)
    if (k->flags & KEY_FLAG_FOCUSED) {
        draw_round_border_16(fb, KEY_W, 1, 1, KEY_W - 2, KEY_H - 2,
                             KEY_CORNER_RADIUS > 1 ? KEY_CORNER_RADIUS - 1 : 2, 1,
                             g_v2_theme.key_focused_border);
    }

    // 12. File scrollbar on right edge of Key 17 in Changes view
    if (strcmp(g_v2_state.view_mode, "changes") == 0 && k->id == 17 && k->item_count > 1) {
        int track_x = KEY_W - 6;
        int track_y = 12;
        int track_h = 104;
        draw_rect_16(fb, KEY_W, track_x, track_y, 3, track_h, g_v2_theme.btn_line_color);

        int total_f = k->item_count;
        int thumb_h = track_h * 3 / total_f;
        if (thumb_h < 8) thumb_h = 8;
        if (thumb_h > track_h) thumb_h = track_h;
        int max_idx = total_f > 1 ? total_f - 1 : 1;
        int cur_idx = k->active_item_idx;
        if (cur_idx < 0) cur_idx = 0;
        if (cur_idx > max_idx) cur_idx = max_idx;
        int thumb_y = track_y + (cur_idx * (track_h - thumb_h) / max_idx);
        draw_rect_16(fb, KEY_W, track_x, thumb_y, 3, thumb_h, g_v2_theme.top_accent);
    }
}

void v2_render_top_frame(uint16_t *fb) {
    if (!fb) return;

    // Clear background
    draw_rect_16(fb, TOP_W, 0, 0, TOP_W, TOP_H, g_v2_theme.top_bg);

    // 1. Header Bar (y: 0..24)
    draw_rect_16(fb, TOP_W, 0, 0, TOP_W, 24, g_v2_theme.top_card);
    draw_line_16(fb, TOP_W, 0, 24, TOP_W - 1, 24, g_v2_theme.top_border);

    // Left Title
    draw_string_16(fb, TOP_W, 12, 4, g_v2_state.top_title, g_v2_theme.top_text, 1);

    // Right Volume Pill
    char vol_str[24];
    if (g_v2_state.is_muted) {
        snprintf(vol_str, sizeof(vol_str), "[MUTED]");
        draw_string_16(fb, TOP_W, TOP_W - 75, 4, vol_str, COLOR_ROSE, 1);
    } else {
        snprintf(vol_str, sizeof(vol_str), "VOL %d%%", g_v2_state.volume);
        draw_string_16(fb, TOP_W, TOP_W - 75, 4, vol_str, g_v2_theme.top_accent, 1);
    }

    // 2. Subtitle Bar (y: 25..42)
    draw_string_16(fb, TOP_W, 12, 27, g_v2_state.top_subtitle, g_v2_theme.top_dim, 1);
    draw_line_16(fb, TOP_W, 0, 44, TOP_W - 1, 44, g_v2_theme.top_border);

    // 3. Body Text Area (y: 46..140)
    // Parse top_body into lines
    const char *p = g_v2_state.top_body;
    char lines[64][192];
    int line_count = 0;

    while (*p && line_count < 64) {
        int idx = 0, width = 0;
        while (*p && *p != '\n') {
            unsigned cp; int bytes = unicode_step(p, &cp);
            int advance = cp < 128 ? 8 : 16;
            if (width + advance <= TOP_W - 20 && idx + bytes < (int)sizeof lines[0]) {
                memcpy(lines[line_count] + idx, p, bytes);
                idx += bytes;
                width += advance;
            }
            p += bytes;
        }
        lines[line_count][idx] = '\0';
        line_count++;
        if (*p == '\n') p++;
    }

    if (line_count == 0) {
        strcpy(lines[0], "No content.");
        line_count = 1;
    }

    // Since the Host streams a sliding window starting at the current scroll position,
    // lines[0] is already the top visible line for this viewport.
    for (int i = 0; i < 4; i++) {
        if (i >= line_count) break;

        int y = 48 + i * 19;
        const char *line = lines[i];

        uint16_t color = g_v2_theme.top_text;
        if (strncmp(line, "[USER]", 6) == 0 || (line[0] == '>' && line[1] == ' ')) {
            color = g_v2_theme.top_accent;
        } else if (strncmp(line, "[AGENT]", 7) == 0) {
            color = g_v2_theme.top_text;
        } else if (strncmp(line, "[PROCESS]", 9) == 0 || strncmp(line, "Turn", 4) == 0 || strncmp(line, "Command", 7) == 0) {
            color = g_v2_theme.key_editing_border;
        } else if (strncmp(line, "Approval", 8) == 0) {
            color = COLOR_ROSE;
        } else if (line[0] == '+' && line[1] == ' ') {
            color = g_v2_theme.top_accent;
        } else if (line[0] == '-' && line[1] == ' ') {
            color = COLOR_ROSE;
        } else if (strncmp(line, "@@", 2) == 0) {
            color = g_v2_theme.key_editing_border;
        } else if (line[0] == '[' && (line[1] == '>' || line[2] == ']')) {
            color = g_v2_theme.top_accent;
        }

        draw_string_16(fb, TOP_W, 12, y, line, color, 1);
    }

    // 4. Scrollbar indicator on right edge
    int total_lines = g_v2_state.top_total_lines > 0 ? g_v2_state.top_total_lines : line_count;
    if (total_lines > 4) {
        int track_x = TOP_W - 8;
        int track_y = 48;
        int track_h = 76;
        draw_rect_16(fb, TOP_W, track_x, track_y, 4, track_h, g_v2_theme.top_border);

        int thumb_h = track_h * 4 / total_lines;
        if (thumb_h < 8) thumb_h = 8;
        if (thumb_h > track_h) thumb_h = track_h;
        int max_s = total_lines - 4;
        if (max_s < 1) max_s = 1;
        int s = g_v2_state.top_scroll;
        if (s < 0) s = 0;
        if (s > max_s) s = max_s;
        int thumb_y = track_y + (s * (track_h - thumb_h) / max_s);
        draw_rect_16(fb, TOP_W, track_x, thumb_y, 4, thumb_h, g_v2_theme.top_accent);
    }
}
