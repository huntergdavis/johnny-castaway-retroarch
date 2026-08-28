/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "jc_chapters.h"
#include "jc_story_player.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void assert_same(const jc_story_player_t *left,
                        const jc_story_player_t *right)
{
    const jc_scene_play_t *a = jc_story_player_current(left);
    const jc_scene_play_t *b = jc_story_player_current(right);

    assert(a != NULL && b != NULL);
    assert(left->director.current_day == right->director.current_day);
    assert(left->plan_seed == right->plan_seed);
    assert(left->scene_index == right->scene_index);
    assert(left->run.scene_count == right->run.scene_count);
    assert(strcmp(a->scene.ads_name, b->scene.ads_name) == 0);
    assert(a->scene.ads_tag == b->scene.ads_tag);
    assert(a->has_walk_from == b->has_walk_from);
    assert(a->walk_from_spot == b->walk_from_spot);
    assert(a->walk_from_heading == b->walk_from_heading);
    assert(jc_story_player_runtime_seed(left) ==
           jc_story_player_runtime_seed(right));
    assert(jc_chapter_for_ads(a->scene.ads_name, a->scene.ads_tag) != NULL);
}

static void test_deterministic_sequence(void)
{
    jc_story_player_t first;
    jc_story_player_t second;
    unsigned index;
    uint32_t initial_plan;
    bool rolled = false;

    assert(jc_story_player_start(&first, 12345u, 5u, 200, 12u, 6u, 14u));
    assert(jc_story_player_start(&second, 12345u, 5u, 200, 12u, 6u, 14u));
    initial_plan = first.plan_seed;
    for (index = 0u; index < 200u; ++index) {
        assert_same(&first, &second);
        assert(jc_story_player_runtime_seed(&first) != 0u);
        assert(jc_story_player_advance(&first, 200, 12u, 6u, 14u));
        assert(jc_story_player_advance(&second, 200, 12u, 6u, 14u));
        if (first.plan_seed != initial_plan)
            rolled = true;
    }
    assert(rolled);
}

static void test_restore(void)
{
    jc_story_player_t source;
    jc_story_player_t restored;
    size_t index;

    assert(jc_story_player_start(&source, 77u, 3u, 99, 22u, 10u, 30u));
    for (index = 0u; index < 3u &&
                         index + 1u < source.run.scene_count; ++index)
        assert(jc_story_player_advance(&source, 99, 22u, 10u, 30u));
    assert(jc_story_player_restore(
        &restored, source.plan_seed, source.director.current_day,
        source.scene_index, 99, 22u, 10u, 30u));
    assert_same(&source, &restored);
    assert(source.planner_rng.state == restored.planner_rng.state);
    assert(!jc_story_player_restore(
        &restored, source.plan_seed, source.director.current_day,
        source.run.scene_count, 99, 22u, 10u, 30u));
}

static void test_day_rollover(void)
{
    jc_story_player_t player;
    size_t count;

    assert(jc_story_player_start(&player, 9u, 1u, 100, 12u, 1u, 2u));
    count = player.run.scene_count;
    while (count-- > 0u)
        assert(jc_story_player_advance(&player, 101, 12u, 1u, 3u));
    assert(player.director.current_day == 2u);
}

static void test_invalid_input(void)
{
    jc_story_player_t player;

    assert(!jc_story_player_start(NULL, 1u, 1u, 0, 12u, 1u, 1u));
    assert(!jc_story_player_start(&player, 1u, 1u, 0, 24u, 1u, 1u));
    assert(!jc_story_player_start(&player, 1u, 1u, 0, 12u, 0u, 1u));
    assert(jc_story_player_current(&player) == NULL);
    assert(jc_story_player_runtime_seed(&player) == 0u);
}

int main(void)
{
    test_deterministic_sequence();
    test_restore();
    test_day_rollover();
    test_invalid_input();
    puts("story player tests passed");
    return 0;
}
