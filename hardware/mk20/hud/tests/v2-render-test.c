#include <assert.h>
#include <string.h>
#include "../v2_state.h"
#include "../v2_render.h"
extern int g_host_offline;
int main(void) {
    uint16_t fb[128 * 128];
    v2_init_defaults();
    V2_State before = g_v2_state;
    v2_render_key_frame(fb, 17, 0);
    int lit = 0;
    for (unsigned i = 0; i < sizeof fb / sizeof fb[0]; i++) lit += fb[i] != 0;
    assert(lit > 100);
    assert(!memcmp(&before, &g_v2_state, sizeof before));
    v2_render_key_frame(fb, 16, 0);
    for (unsigned i = 0; i < sizeof fb / sizeof fb[0]; i++) assert(fb[i] == 0);
    g_host_offline = 0;
    v2_render_key_frame(fb, 17, 0);
    for (unsigned i = 0; i < sizeof fb / sizeof fb[0]; i++) assert(fb[i] == 0);
    return 0;
}
