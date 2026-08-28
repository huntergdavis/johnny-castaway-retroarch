/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef JC_RNG_H
#define JC_RNG_H

#include <stdint.h>

typedef struct jc_rng {
    uint64_t state;
} jc_rng_t;

void jc_rng_init(jc_rng_t *rng, uint64_t seed);
uint32_t jc_rng_next(jc_rng_t *rng);
uint32_t jc_rng_below(jc_rng_t *rng, uint32_t upper_bound);

#endif
