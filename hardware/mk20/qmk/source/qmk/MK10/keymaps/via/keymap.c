/* Copyright 2022 Binepad (@binpad) */
/* SPDX-License-Identifier: GPL-2.0-or-later */

#include QMK_KEYBOARD_H

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [0] = LAYOUT_ortho_6x3(
        KC_0,KC_1,KC_2,KC_3,KC_4,
        KC_A,KC_B,KC_C,KC_D,KC_E
    )
};
