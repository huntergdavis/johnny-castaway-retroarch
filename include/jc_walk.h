/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef JC_WALK_H
#define JC_WALK_H

#include "jc_path.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JC_HEADING_COUNT 8u

typedef struct jc_walk_frame {
    bool flip_horizontal;
    int x;
    int y;
    uint16_t sprite;
    uint16_t delay_ticks;
    bool behind_tree;
} jc_walk_frame_t;

typedef struct jc_walker {
    jc_path_t path;
    size_t path_index;
    int current_spot;
    int current_heading;
    int next_spot;
    int next_heading;
    int final_spot;
    int final_heading;
    int turn_increment;
    bool last_turn;
    bool arrived;
    bool behind_tree;
    size_t data_index;
} jc_walker_t;

bool jc_walk_init(jc_walker_t *walker, uint8_t from_spot,
                  uint8_t from_heading, uint8_t to_spot,
                  uint8_t to_heading, jc_rng_t *rng);
bool jc_walk_next(jc_walker_t *walker, jc_walk_frame_t *frame);

#endif
