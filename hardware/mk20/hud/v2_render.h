#ifndef V2_RENDER_H
#define V2_RENDER_H

#include <stdint.h>

void v2_render_key_frame(uint16_t *fb, int key_idx, int pressed);
void v2_render_top_frame(uint16_t *fb);

#endif // V2_RENDER_H
