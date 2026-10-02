#include "unicode_text.h"
#include "gfx_prims.h"
#include <stdio.h>

int main(void) {
    uint16_t pixels[TOP_W * TOP_H] = {0};
    if (!unicode_draw(pixels, TOP_W, 12, 20, "스노우볼 음성 초안", COLOR_WHITE, 1) ||
        !unicode_draw(pixels, TOP_W, 12, 50, "README 파일을 열고 model은 Codex", COLOR_WHITE, 1)) return 1;
    FILE *fp = fopen("/tmp/snowball-font.ppm", "wb");
    if (!fp) return 2;
    fprintf(fp, "P6\n%d %d\n255\n", TOP_W, TOP_H);
    int lit = 0;
    for (unsigned i = 0; i < TOP_W * TOP_H; i++) {
        uint16_t p = pixels[i];
        unsigned char rgb[3] = { (p >> 11) * 255 / 31, ((p >> 5) & 63) * 255 / 63, (p & 31) * 255 / 31 };
        fwrite(rgb, 1, 3, fp); if (p) lit++;
    }
    fclose(fp); printf("glyph pixels: %d\n", lit);
    return lit > 100 ? 0 : 3;
}
