/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef JC_PATH_H
#define JC_PATH_H

#include "jc_rng.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JC_SPOT_COUNT 6u
#define JC_PATH_MAX_SPOTS JC_SPOT_COUNT

typedef struct jc_path {
    uint8_t spots[JC_PATH_MAX_SPOTS];
    size_t length;
} jc_path_t;

bool jc_path_calculate(uint8_t from_spot, uint8_t to_spot,
                       jc_rng_t *rng, jc_path_t *path);

#endif
