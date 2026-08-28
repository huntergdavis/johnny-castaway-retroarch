/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "jc_core.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    jc_core_t first;
    jc_core_t second;
    jc_surface_t surface;
    jc_palette_t palette;
    uint8_t pixel = 1u;
    unsigned char state[64];
    size_t centered_pixel = ((JC_FRAME_HEIGHT - 1u) / 2u) * JC_FRAME_WIDTH +
                            (JC_FRAME_WIDTH - 1u) / 2u;

    jc_core_init(&first);
    jc_core_init(&second);
    assert(memcmp(jc_core_framebuffer(&first), jc_core_framebuffer(&second),
                  sizeof(first.framebuffer)) == 0);

    jc_core_step(&first);
    assert(memcmp(jc_core_framebuffer(&first), jc_core_framebuffer(&second),
                  sizeof(first.framebuffer)) != 0);
    assert(jc_core_serialize_size() <= sizeof(state));
    assert(jc_core_serialize(&first, state, sizeof(state)));
    assert(jc_core_unserialize(&second, state, sizeof(state)));
    assert(memcmp(jc_core_framebuffer(&first), jc_core_framebuffer(&second),
                  sizeof(first.framebuffer)) == 0);

    memset(&surface, 0, sizeof(surface));
    memset(&palette, 0, sizeof(palette));
    surface.pixels = &pixel;
    surface.width = 1u;
    surface.height = 1u;
    surface.pitch = 1u;
    palette.xrgb[1] = 0x00123456u;
    first.frame_number = 99u;
    first.phase = 17u;
    assert(jc_core_update_content_frame(&first, &surface, &palette));
    assert(first.frame_number == 99u && first.phase == 17u);
    assert(jc_core_framebuffer(&first)[centered_pixel] == 0x00123456u);

    puts("jc_core tests passed");
    return 0;
}
