/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "jc_path.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void validate(const jc_path_t *path, uint8_t from, uint8_t to)
{
    size_t first;
    size_t second;
    assert(path->length > 0u && path->length <= JC_PATH_MAX_SPOTS);
    assert(path->spots[0] == from);
    assert(path->spots[path->length - 1u] == to);
    for (first = 0u; first < path->length; ++first) {
        assert(path->spots[first] < JC_SPOT_COUNT);
        for (second = first + 1u; second < path->length; ++second)
            assert(path->spots[first] != path->spots[second]);
    }
}

int main(void)
{
    jc_rng_t rng;
    uint8_t from;
    uint8_t to;
    jc_rng_init(&rng, 20260618u);
    for (from = 0u; from < JC_SPOT_COUNT; ++from) {
        for (to = 0u; to < JC_SPOT_COUNT; ++to) {
            unsigned trial;
            for (trial = 0u; trial < 300u; ++trial) {
                jc_path_t path;
                size_t hop;
                assert(jc_path_calculate(from, to, &rng, &path));
                validate(&path, from, to);
                for (hop = 1u; hop < path.length; ++hop)
                    assert(!(path.spots[hop - 1u] == 4u && path.spots[hop] == 2u));
            }
        }
    }
    {
        jc_rng_t a;
        jc_rng_t b;
        jc_path_t first;
        jc_path_t second;
        jc_rng_init(&a, 7u);
        jc_rng_init(&b, 7u);
        assert(jc_path_calculate(0u, 3u, &a, &first));
        assert(jc_path_calculate(0u, 3u, &b, &second));
        assert(first.length == second.length);
        assert(memcmp(first.spots, second.spots, first.length) == 0);
    }
    puts("path tests passed");
    return 0;
}
