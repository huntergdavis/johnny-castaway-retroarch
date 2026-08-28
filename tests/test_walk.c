/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "jc_walk.h"

#include <assert.h>
#include <stdio.h>

static bool run_walk(uint8_t from, uint8_t to, uint64_t seed)
{
    jc_rng_t rng;
    jc_walker_t walker;
    jc_walk_frame_t frame;
    unsigned count = 0u;
    bool behind_tree = false;
    jc_rng_init(&rng, seed);
    assert(jc_walk_init(&walker, from, 0u, to, 4u, &rng));
    while (jc_walk_next(&walker, &frame)) {
        assert(++count < 5000u);
        assert(frame.x >= -1);
        assert(frame.sprite < 64u);
        behind_tree = behind_tree || frame.behind_tree;
        if (walker.arrived)
            assert(frame.delay_ticks == 80u);
        else
            assert(frame.delay_ticks == 6u);
    }
    assert(count > 0u);
    assert(walker.arrived);
    assert(walker.current_spot == to);
    assert(walker.current_heading == 4);
    return behind_tree;
}

int main(void)
{
    uint8_t from;
    uint8_t to;
    bool found_behind_tree = false;
    for (from = 0u; from < JC_SPOT_COUNT; ++from) {
        for (to = 0u; to < JC_SPOT_COUNT; ++to)
            (void)run_walk(from, to, 1u);
    }
    for (from = 0u; from < 64u; ++from)
        found_behind_tree = run_walk(3u, 4u, from) || found_behind_tree;
    assert(found_behind_tree);
    puts("walk tests passed");
    return 0;
}
