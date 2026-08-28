/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Director logic follows jno6809/jc_reborn story.c (revision 524a580) and
 * antigerme/wilson-reborn crates/wilson-engine/src/story.rs (revision 2d302f5).
 * The latter clarified the original night cycle and re-drawn ambient-loop bound.
 */
#include "jc_director.h"

#include <string.h>

#include "jc_story_data.inc"

static void copy_scene(const struct TStoryScene *source,
                       jc_story_scene_t *destination)
{
    destination->ads_name = source->adsName;
    destination->ads_tag = (uint16_t)source->adsTagNo;
    destination->spot_start = (uint8_t)source->spotStart;
    destination->heading_start = (uint8_t)source->hdgStart;
    destination->spot_end = (uint8_t)source->spotEnd;
    destination->heading_end = (uint8_t)source->hdgEnd;
    destination->story_day = (uint8_t)source->dayNo;
    destination->flags = (uint8_t)source->flags;
}

static const struct TStoryScene *pick_scene(uint8_t day, uint8_t wanted,
                                             uint8_t unwanted, jc_rng_t *rng)
{
    unsigned matches[NUM_SCENES];
    unsigned count = 0u;
    unsigned index;
    for (index = 0u; index < NUM_SCENES; ++index) {
        const struct TStoryScene *scene = &storyScenes[index];
        unsigned flags = (unsigned)scene->flags;
        if ((flags & wanted) == wanted && (flags & unwanted) == 0u &&
            (scene->dayNo == 0 || scene->dayNo == (int)day))
            matches[count++] = index;
    }
    if (count == 0u)
        return NULL;
    return &storyScenes[matches[jc_rng_below(rng, count)]];
}

static bool valid_spot_heading(int spot, int heading)
{
    return spot >= SPOT_A && spot <= SPOT_F &&
           heading >= HDG_S && heading <= HDG_SE;
}

static bool valid_start(const struct TStoryScene *scene)
{
    return scene != NULL &&
           valid_spot_heading(scene->spotStart, scene->hdgStart);
}

static bool valid_end(const struct TStoryScene *scene)
{
    return scene != NULL &&
           valid_spot_heading(scene->spotEnd, scene->hdgEnd);
}

static bool same_scene(const struct TStoryScene *left,
                       const struct TStoryScene *right)
{
    return left != NULL && right != NULL &&
           left->adsTagNo == right->adsTagNo &&
           strcmp(left->adsName, right->adsName) == 0;
}

static jc_island_state_t island_for_scene(const struct TStoryScene *scene,
                                           uint8_t day, jc_holiday_t holiday,
                                           bool night, jc_rng_t *rng)
{
    jc_island_state_t island;
    memset(&island, 0, sizeof(island));
    island.night = night;
    island.low_tide = (scene->flags & LOWTIDE_OK) != 0 &&
                      jc_rng_below(rng, 2u) == 1u;
    if ((scene->flags & VARPOS_OK) != 0) {
        if (jc_rng_below(rng, 2u) == 1u) {
            island.x = -222 + (int)jc_rng_below(rng, 109u);
            island.y = -44 + (int)jc_rng_below(rng, 128u);
        } else if (jc_rng_below(rng, 2u) == 1u) {
            island.x = -114 + (int)jc_rng_below(rng, 134u);
            island.y = -14 + (int)jc_rng_below(rng, 99u);
        } else {
            island.x = -114 + (int)jc_rng_below(rng, 119u);
            island.y = -73 + (int)jc_rng_below(rng, 60u);
        }
    } else if ((scene->flags & LEFT_ISLAND) != 0) {
        island.x = -272;
    }
    island.raft_stage = (scene->flags & NORAFT) != 0 ? 0u :
                        jc_director_raft_for_day(day);
    island.holiday = (scene->flags & HOLIDAY_NOK) != 0 ?
                     JC_HOLIDAY_NONE : holiday;
    return island;
}

void jc_director_init(jc_director_t *director, uint8_t story_day,
                      int stored_yday)
{
    director->current_day = story_day < 1u || story_day > 11u ? 1u : story_day;
    director->stored_yday = stored_yday;
    director->daynight_mode = JC_DAYNIGHT_ORIGINAL;
    director->first_sequence = true;
}

bool jc_director_advance_day(jc_director_t *director, int today_yday)
{
    bool changed = false;
    if (today_yday != director->stored_yday) {
        director->stored_yday = today_yday;
        ++director->current_day;
        changed = true;
    }
    if (director->current_day < 1u || director->current_day > 11u) {
        director->current_day = 1u;
        changed = true;
    }
    return changed;
}

bool jc_director_is_night(uint8_t hour, jc_daynight_mode_t mode)
{
    if (mode == JC_DAYNIGHT_REAL_24H)
        return hour < 6u || hour >= 20u;
    hour %= 8u;
    return hour == 0u || hour == 7u;
}

uint8_t jc_director_raft_for_day(uint8_t story_day)
{
    if (story_day <= 2u)
        return 1u;
    if (story_day == 3u)
        return 2u;
    if (story_day == 4u)
        return 3u;
    if (story_day == 5u)
        return 4u;
    return 5u;
}

jc_holiday_t jc_director_holiday(uint8_t month, uint8_t month_day)
{
    unsigned mmdd = (unsigned)month * 100u + month_day;
    if (mmdd > 1028u && mmdd < 1101u)
        return JC_HOLIDAY_HALLOWEEN;
    if (mmdd > 314u && mmdd < 318u)
        return JC_HOLIDAY_ST_PATRICK;
    if (mmdd > 1222u && mmdd < 1226u)
        return JC_HOLIDAY_CHRISTMAS;
    if (mmdd < 102u || mmdd > 1228u)
        return JC_HOLIDAY_NEW_YEAR;
    return JC_HOLIDAY_NONE;
}

size_t jc_director_scene_count(void)
{
    return NUM_SCENES;
}

bool jc_director_scene(size_t index, jc_story_scene_t *scene)
{
    if (scene == NULL || index >= NUM_SCENES)
        return false;
    copy_scene(&storyScenes[index], scene);
    return true;
}

static void append_play(jc_story_run_t *run,
                        const struct TStoryScene *scene,
                        bool has_previous, uint8_t previous_spot,
                        uint8_t previous_heading, bool on_island)
{
    jc_scene_play_t *play = &run->scenes[run->scene_count++];
    memset(play, 0, sizeof(*play));
    copy_scene(scene, &play->scene);
    play->has_walk_from = has_previous;
    play->walk_from_spot = previous_spot;
    play->walk_from_heading = previous_heading;
    play->has_walk_to = on_island;
    play->walk_to_spot = (uint8_t)scene->spotStart;
    play->walk_to_heading = (uint8_t)scene->hdgStart;
    play->day_beat = scene->dayNo != 0;
    play->left_island = (scene->flags & LEFT_ISLAND) != 0;
}

bool jc_director_plan(jc_director_t *director, int today_yday, uint8_t hour,
                      uint8_t month, uint8_t month_day, jc_rng_t *rng,
                      jc_story_run_t *run)
{
    const struct TStoryScene *final_scene;
    bool has_previous = false;
    bool first_sequence;
    uint8_t previous_spot = 0u;
    uint8_t previous_heading = 0u;
    uint8_t final_unwanted;

    if (director == NULL || rng == NULL || run == NULL)
        return false;
    memset(run, 0, sizeof(*run));
    jc_director_advance_day(director, today_yday);
    first_sequence = director->first_sequence;
    final_unwanted = first_sequence ? FIRST : 0u;
    final_scene = pick_scene(director->current_day, FINAL,
                             final_unwanted, rng);
    if (final_scene != NULL && (final_scene->flags & FIRST) == 0u) {
        unsigned retry;
        for (retry = 0u; retry < 32u && !valid_start(final_scene); ++retry)
            final_scene = pick_scene(director->current_day, FINAL,
                                     final_unwanted, rng);
    }
    if (final_scene == NULL)
        return false;
    run->on_island = (final_scene->flags & ISLAND) != 0;
    if (run->on_island) {
        run->island = island_for_scene(
            final_scene, director->current_day,
            jc_director_holiday(month, month_day),
            jc_director_is_night(hour, director->daynight_mode), rng);
    } else {
        run->island.night = jc_director_is_night(
            hour, director->daynight_mode);
    }

    if ((final_scene->flags & FIRST) == 0) {
        uint8_t wanted = 0u;
        uint8_t unwanted = (uint8_t)(FINAL | (first_sequence ? FIRST : 0u));
        unsigned intermediate_count = 6u + jc_rng_below(rng, 14u);
        unsigned index = 0u;
        const struct TStoryScene *last_scene = NULL;
        if (run->island.low_tide)
            wanted |= LOWTIDE_OK;
        if (run->island.x != 0 || run->island.y != 0)
            wanted |= VARPOS_OK;
        while (run->scene_count + 1u < JC_DIRECTOR_MAX_SCENES &&
               index < intermediate_count) {
            const struct TStoryScene *scene = NULL;
            unsigned pick_try;
            for (pick_try = 0u; pick_try < 8u; ++pick_try) {
                scene = pick_scene(director->current_day, wanted,
                                   unwanted, rng);
                if (scene == NULL)
                    break;
                if (has_previous && !valid_start(scene))
                    continue;
                if (!valid_end(scene))
                    continue;
                if (!same_scene(scene, last_scene))
                    break;
            }
            if (scene == NULL)
                break;
            append_play(run, scene, has_previous, previous_spot,
                        previous_heading, true);
            has_previous = true;
            previous_spot = (uint8_t)scene->spotEnd;
            previous_heading = (uint8_t)scene->hdgEnd;
            last_scene = scene;
            unwanted |= FIRST;
            ++index;
        }
    }
    append_play(run, final_scene, has_previous, previous_spot,
                previous_heading, run->on_island);
    director->first_sequence = false;
    return true;
}
