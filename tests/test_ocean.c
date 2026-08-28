/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "jc_ocean.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    jc_vag_info_t info;
    uint8_t *pcm;
    uint32_t hash = 2166136261u;
    size_t index;

    assert(jc_ocean_vag_size() == 126064u);
    assert(jc_ocean_probe(&info) == JC_VAG_OK);
    assert(info.sample_rate == 11025u);
    assert(info.sample_count == 220528u);
    assert(info.has_loop);
    assert(info.loop_start == 28u);
    assert(info.loop_end == 220528u);

    pcm = (uint8_t *)malloc(info.sample_count);
    assert(pcm != NULL);
    assert(jc_ocean_decode_u8(pcm, info.sample_count, &info) == JC_VAG_OK);
    for (index = 0u; index < info.sample_count; ++index) {
        hash ^= pcm[index];
        hash *= 16777619u;
    }
    assert(hash == 0xadfd87dcu);
    free(pcm);
    puts("embedded ocean tests passed");
    return 0;
}
