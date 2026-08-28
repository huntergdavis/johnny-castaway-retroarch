/* SPDX-License-Identifier: GPL-3.0-or-later */
/* C99 translation of antigerme/wilson-reborn rng.rs at revision 2d302f5. */
#include "jc_rng.h"

void jc_rng_init(jc_rng_t *rng, uint64_t seed)
{
    uint64_t mixed = seed + UINT64_C(0x9e3779b97f4a7c15);
    mixed = (mixed ^ (mixed >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    mixed = (mixed ^ (mixed >> 27)) * UINT64_C(0x94d049bb133111eb);
    mixed ^= mixed >> 31;
    rng->state = mixed | UINT64_C(1);
}

uint32_t jc_rng_next(jc_rng_t *rng)
{
    uint64_t value = rng->state;
    value ^= value << 13;
    value ^= value >> 7;
    value ^= value << 17;
    rng->state = value;
    return (uint32_t)(value >> 32);
}

uint32_t jc_rng_below(jc_rng_t *rng, uint32_t upper_bound)
{
    return upper_bound == 0u ? 0u : jc_rng_next(rng) % upper_bound;
}
