/* Host-only regression: replay real QMK contacts without opening hardware. */
#define main hud_device_main
#include "mk20-hud.c"
#undef main
#include <assert.h>

static void contact(unsigned char row, unsigned char down) {
    unsigned char data[] = {0x16, down, row, row, 0, 0};
    unsigned char sum = 0;
    for (unsigned i = 0; i < sizeof data; i++) sum += data[i];
    unsigned char prefix[] = {0xaa, 0x55, sum, 6, 0xf9};
    for (unsigned i = 0; i < sizeof prefix; i++) parse_qmk_byte(prefix[i]);
    for (unsigned i = 0; i < sizeof data; i++) parse_qmk_byte(data[i]);
    parse_qmk_byte(0xf5); parse_qmk_byte(0x5f);
}

int main(void) {
    g_mode_v2 = 1;
    contact(100, 1); assert(!g_knob_toggle_pending);
    contact(103, 1); assert(g_knob_toggle_pending);
    /* Whole press and release may be drained before the event-loop tick. */
    contact(100, 0); contact(103, 0); assert(g_knob_toggle_pending);
    g_knob_toggle_pending = 0;
    contact(103, 1); contact(100, 1); assert(g_knob_toggle_pending);
    g_knob_toggle_pending = 0;
    contact(100, 1); contact(103, 1); assert(!g_knob_toggle_pending);
    contact(100, 0); contact(100, 1); assert(!g_knob_toggle_pending);
    contact(100, 0); contact(103, 0);
    contact(100, 1); contact(100, 0); assert(!g_knob_toggle_pending);
    puts("PASS: short chord, reverse order, duplicate contacts, both-release rearm, single click");
    return 0;
}
