#include <assert.h>
#include <string.h>
#include "../v2_state.h"
int g_host_offline = 0;
int main(void) {
    v2_init_defaults();
    for (int i = 1; i <= 20; i++) assert(g_v2_state.keys[i].flags & KEY_FLAG_DISABLED);
    const char *packet = "{\"type\":\"v2_sync\",\"keys\":[{\"id\":1,\"main\":\"가나다라마바사아\",\"items\":[\"abcdefghijklmnopqrstuvwxyz0123456789\",\"Second\"]}]}";
    assert(v2_parse_sync_packet(packet, strlen(packet)));
    assert(strlen(g_v2_state.keys[1].main) == 21);
    assert(strcmp(g_v2_state.keys[1].main, "가나다라마바사") == 0);
    assert(g_v2_state.keys[1].item_count == 2);
    assert(strcmp(g_v2_state.keys[1].items[0], "abcdefghijklmnopqrstuvw") == 0);
    assert(strcmp(g_v2_state.keys[1].items[1], "Second") == 0);
    const char *quoted = "{\"type\":\"v2_sync\",\"keys\":[{\"id\":2,\"items\":[\"abcdefghijk\\\"mnopqrstuvwxyz0123456789\",\"Next\"]}]}";
    assert(v2_parse_sync_packet(quoted, strlen(quoted)));
    assert(g_v2_state.keys[2].item_count == 2);
    assert(strcmp(g_v2_state.keys[2].items[1], "Next") == 0);
    return 0;
}
