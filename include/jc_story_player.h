/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef JC_STORY_PLAYER_H
#define JC_STORY_PLAYER_H

#include "jc_director.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Nonblocking controller for the original mini-story plan. It contains no
 * renderer or platform code: the caller starts current->scene in jc_runtime,
 * then calls advance when that ADS scene finishes.
 */
typedef struct jc_story_player {
    jc_director_t director;
    jc_rng_t planner_rng;
    jc_story_run_t run;
    uint32_t plan_seed;
    size_t scene_index;
    bool ready;
} jc_story_player_t;

bool jc_story_player_start(jc_story_player_t *player, uint32_t plan_seed,
                           uint8_t story_day, int today_yday, uint8_t hour,
                           uint8_t month, uint8_t month_day);
bool jc_story_player_restore(jc_story_player_t *player, uint32_t plan_seed,
                             uint8_t story_day, size_t scene_index,
                             int today_yday, uint8_t hour, uint8_t month,
                             uint8_t month_day);
bool jc_story_player_advance(jc_story_player_t *player, int today_yday,
                             uint8_t hour, uint8_t month,
                             uint8_t month_day);

const jc_scene_play_t *jc_story_player_current(
    const jc_story_player_t *player);
uint32_t jc_story_player_runtime_seed(const jc_story_player_t *player);

#endif
