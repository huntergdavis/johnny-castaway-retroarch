/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The sequence shape consumes jc_director's C99 translation of the original
 * Johnny Reborn/Wilson story loop. This state controller is new for libretro:
 * it replaces blocking platform loops with one explicit scene boundary at a
 * time and makes the plan reproducible from a seed/day/index save-state tuple.
 */
#include "jc_story_player.h"

#include <string.h>

static bool valid_calendar(uint8_t hour, uint8_t month, uint8_t month_day)
{
    return hour < 24u && month >= 1u && month <= 12u &&
           month_day >= 1u && month_day <= 31u;
}

static bool make_plan(jc_story_player_t *player, uint32_t plan_seed,
                      int today_yday, uint8_t hour, uint8_t month,
                      uint8_t month_day)
{
    jc_director_t director;
    jc_rng_t rng;
    jc_story_run_t run;

    if (player == NULL || !valid_calendar(hour, month, month_day))
        return false;
    director = player->director;
    jc_rng_init(&rng, plan_seed);
    if (!jc_director_plan(&director, today_yday, hour, month, month_day,
                          &rng, &run) ||
        run.scene_count == 0u || run.scene_count > JC_DIRECTOR_MAX_SCENES)
        return false;
    player->director = director;
    player->planner_rng = rng;
    player->run = run;
    player->plan_seed = plan_seed;
    player->scene_index = 0u;
    player->ready = true;
    return true;
}

bool jc_story_player_start(jc_story_player_t *player, uint32_t plan_seed,
                           uint8_t story_day, int today_yday, uint8_t hour,
                           uint8_t month, uint8_t month_day)
{
    if (player == NULL)
        return false;
    memset(player, 0, sizeof(*player));
    jc_director_init(&player->director, story_day, today_yday);
    return make_plan(player, plan_seed, today_yday, hour, month, month_day);
}

bool jc_story_player_restore(jc_story_player_t *player, uint32_t plan_seed,
                             uint8_t story_day, size_t scene_index,
                             int today_yday, uint8_t hour, uint8_t month,
                             uint8_t month_day)
{
    jc_story_player_t candidate;

    if (player == NULL || story_day < 1u || story_day > 11u)
        return false;
    if (!jc_story_player_start(&candidate, plan_seed, story_day, today_yday,
                               hour, month, month_day) ||
        scene_index >= candidate.run.scene_count)
        return false;
    candidate.scene_index = scene_index;
    *player = candidate;
    return true;
}

bool jc_story_player_advance(jc_story_player_t *player, int today_yday,
                             uint8_t hour, uint8_t month,
                             uint8_t month_day)
{
    uint32_t next_seed;

    if (player == NULL || !player->ready ||
        player->scene_index >= player->run.scene_count ||
        !valid_calendar(hour, month, month_day))
        return false;
    if (player->scene_index + 1u < player->run.scene_count) {
        ++player->scene_index;
        return true;
    }
    next_seed = jc_rng_next(&player->planner_rng);
    return make_plan(player, next_seed, today_yday, hour, month, month_day);
}

const jc_scene_play_t *jc_story_player_current(
    const jc_story_player_t *player)
{
    if (player == NULL || !player->ready ||
        player->scene_index >= player->run.scene_count)
        return NULL;
    return &player->run.scenes[player->scene_index];
}

uint32_t jc_story_player_runtime_seed(const jc_story_player_t *player)
{
    const jc_scene_play_t *play = jc_story_player_current(player);
    uint32_t seed;
    const unsigned char *name;

    if (play == NULL)
        return 0u;
    seed = player->plan_seed ^ UINT32_C(0xa5c31f27) ^
           (uint32_t)(player->scene_index + 1u) * UINT32_C(0x9e3779b9);
    seed ^= (uint32_t)play->scene.ads_tag << 16;
    name = (const unsigned char *)play->scene.ads_name;
    while (*name != '\0') {
        seed ^= *name++;
        seed *= UINT32_C(16777619);
    }
    return seed == 0u ? UINT32_C(1) : seed;
}
