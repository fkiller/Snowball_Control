#ifndef SNOWBALL_UNICODE_TEXT_H
#define SNOWBALL_UNICODE_TEXT_H
#include <stdint.h>
int unicode_step(const char *text, unsigned *codepoint);
int unicode_width(const char *text);
int unicode_draw(uint16_t *fb, int stride, int x, int y, const char *text, uint16_t color, int scale);
#endif
