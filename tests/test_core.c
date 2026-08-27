/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "jc_core.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    jc_core_t first;
    jc_core_t second;
    unsigned char state[64];

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

    puts("jc_core tests passed");
    return 0;
}
