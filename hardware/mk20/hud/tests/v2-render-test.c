#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "../v2_state.h"
#include "../v2_render.h"
#include "../unicode_text.h"
extern int g_host_offline;
int main(int argc, char **argv) {
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
    // Embedded Hangul works without FreeType or any SD font; pixels are binary.
    memset(fb, 0, sizeof fb);
    assert(unicode_draw_clipped(fb, 128, -4, -3, "한글힣ㄱᄀ", 0xffff, 2, 4, 5, 40, 20));
    lit = 0;
    for (int y = 0; y < 128; y++) for (int x = 0; x < 128; x++) {
        uint16_t pixel = fb[y * 128 + x];
        assert(pixel == 0 || pixel == 0xffff);
        if (x < 4 || x >= 44 || y < 5 || y >= 25) assert(pixel == 0);
        lit += pixel != 0;
    }
    assert(lit > 40);
    for (int cp = 33; cp <= 126; cp++) {
        memset(fb, 0, sizeof fb);
        char text[] = {(char)cp, 0};
        assert(unicode_draw(fb, 128, 0, 0, text, 0xffff, 1));
        lit = 0;
        for (int i = 0; i < 128 * 128; i++) lit += fb[i] != 0;
        assert(lit > 0); // Every printable code character must actually have ink.
    }
    g_host_offline = 0;
    const char *styled = "{\"type\":\"v2_sync\",\"topBody\":\"한글 제목\\n코드 ** 원문\\n  인용\\n\",\"topStyles\":\"hcqr\",\"keys\":[]}";
    assert(v2_parse_sync_packet(styled, strlen(styled)));
    assert(!strcmp(g_v2_state.top_styles, "hcqr"));
    uint16_t top[TOP_W * TOP_H];
    v2_render_top_frame(top);
    assert(top[46 * TOP_W + 10] == g_v2_theme.top_card);
    assert(top[65 * TOP_W + 12] == g_v2_theme.top_accent);
    assert(top[92 * TOP_W + 12] == g_v2_theme.top_border);
    const char *crumb="{\"type\":\"v2_sync\",\"crumbProject\":\"한글 프로젝트\",\"crumbSession\":\"An exceptionally long session title that must fit a single row\",\"crumbHarness\":\"Codex\",\"topBody\":\"1\\n2\\n3\\n4\\n5\\n6\",\"keys\":[]}";
    assert(v2_parse_sync_packet(crumb,strlen(crumb)));
    assert(!strcmp(g_v2_state.crumb_project,"한글 프로젝트"));
    v2_render_top_frame(top);
    uint16_t header[TOP_W*24];memcpy(header,top,sizeof header);
    g_v2_now_ms=1000;g_v2_volume_until_ms=4000;v2_render_top_frame(top);
    assert(memcmp(header,top,sizeof header)); // Only actual-change interval reserves volume width.
    g_v2_now_ms=4001;v2_render_top_frame(top);assert(!memcmp(header,top,sizeof header));
    g_v2_volume_until_ms=0;
    const char *plain = "{\"type\":\"v2_sync\",\"topBody\":\"plain\",\"keys\":[]}";
    assert(v2_parse_sync_packet(plain, strlen(plain)));
    assert(!g_v2_state.top_styles[0]);
    assert(!g_v2_state.crumb_project[0]);
    const char *code = "{\"type\":\"v2_sync\",\"topBody\":\"{\\\"keys\\\": [], \\\"topStyles\\\":\\\"r\\\"}; C:\\\\code\",\"topStyles\":\"c\",\"keys\":[{\"id\":1,\"main\":\"actual\",\"flags\":0}]}";
    assert(v2_parse_sync_packet(code, strlen(code)));
    assert(!strcmp(g_v2_state.top_body, "{\"keys\": [], \"topStyles\":\"r\"}; C:\\code"));
    assert(!strcmp(g_v2_state.top_styles, "c"));
    assert(!strcmp(g_v2_state.keys[1].main, "actual"));
    // Optional software-renderer artifact from a real encoded host packet.
    if (argc == 3) {
        FILE *input = fopen(argv[1], "rb"); assert(input);
        char packet[8192]; size_t n = fread(packet, 1, sizeof(packet) - 1, input);
        fclose(input); packet[n] = 0;
        assert(v2_parse_sync_packet(packet, n));
        v2_render_top_frame(top);
        FILE *output = fopen(argv[2], "wb"); assert(output);
        fprintf(output, "P6\n%d %d\n255\n", TOP_W, TOP_H);
        for (int i = 0; i < TOP_W * TOP_H; i++) {
            unsigned char rgb[] = { ((top[i] >> 11) & 31) * 255 / 31, ((top[i] >> 5) & 63) * 255 / 63, (top[i] & 31) * 255 / 31 };
            fwrite(rgb, 1, 3, output);
        }
        fclose(output);
    }
    assert(v2_parse_sync_packet(plain,strlen(plain)));
    g_host_offline = 0;
    memset(&g_v2_state.keys[17],0,sizeof g_v2_state.keys[17]);
    v2_render_key_frame(fb, 17, 0);
    for (unsigned i = 0; i < sizeof fb / sizeof fb[0]; i++) assert(fb[i] == 0);
    return 0;
}
