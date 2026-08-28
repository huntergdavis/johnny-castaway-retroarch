/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "jc_director.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_table(void)
{
    static const char *names[11] = {
        "MARY.ADS", "JOHNNY.ADS", "SUZY.ADS", "MARY.ADS", "MARY.ADS",
        NULL, "MARY.ADS", "MARY.ADS", "SUZY.ADS", "JOHNNY.ADS", "JOHNNY.ADS"
    };
    static const unsigned tags[11] = {2, 2, 1, 3, 1, 0, 4, 5, 2, 6, 1};
    unsigned day;
    assert(jc_director_scene_count() == 63u);
    for (day = 1u; day <= 11u; ++day) {
        bool found = false;
        size_t index;
        for (index = 0u; index < jc_director_scene_count(); ++index) {
            jc_story_scene_t scene;
            assert(jc_director_scene(index, &scene));
            if (scene.story_day == day &&
                (scene.flags & JC_SCENE_FINAL) != 0u) {
                assert(names[day - 1u] != NULL);
                assert(strcmp(scene.ads_name, names[day - 1u]) == 0);
                assert(scene.ads_tag == tags[day - 1u]);
                found = true;
            }
        }
        assert(found == (names[day - 1u] != NULL));
    }
}

static void test_calendar(void)
{
    jc_director_t director;
    jc_director_init(&director, 11u, 100);
    assert(!jc_director_advance_day(&director, 100));
    assert(jc_director_advance_day(&director, 101));
    assert(director.current_day == 1u);
    assert(jc_director_is_night(23u, JC_DAYNIGHT_ORIGINAL));
    assert(!jc_director_is_night(22u, JC_DAYNIGHT_ORIGINAL));
    assert(jc_director_is_night(22u, JC_DAYNIGHT_REAL_24H));
    assert(jc_director_holiday(10u, 30u) == JC_HOLIDAY_HALLOWEEN);
    assert(jc_director_holiday(3u, 16u) == JC_HOLIDAY_ST_PATRICK);
    assert(jc_director_holiday(12u, 24u) == JC_HOLIDAY_CHRISTMAS);
    assert(jc_director_holiday(1u, 1u) == JC_HOLIDAY_NEW_YEAR);
}

static void test_plan(void)
{
    jc_director_t first;
    jc_director_t second;
    jc_story_run_t a;
    jc_story_run_t b;
    jc_rng_t rng_a;
    jc_rng_t rng_b;
    size_t index;
    jc_director_init(&first, 5u, 200);
    jc_director_init(&second, 5u, 200);
    jc_rng_init(&rng_a, 7u);
    jc_rng_init(&rng_b, 7u);
    assert(jc_director_plan(&first, 200, 12u, 6u, 14u, &rng_a, &a));
    assert(jc_director_plan(&second, 200, 12u, 6u, 14u, &rng_b, &b));
    assert(a.scene_count > 0u && a.scene_count <= JC_DIRECTOR_MAX_SCENES);
    assert(a.scene_count == b.scene_count);
    assert(a.island.holiday == JC_HOLIDAY_NONE);
    for (index = 0u; index < a.scene_count; ++index) {
        assert(strcmp(a.scenes[index].scene.ads_name,
                      b.scenes[index].scene.ads_name) == 0);
        assert(a.scenes[index].scene.ads_tag == b.scenes[index].scene.ads_tag);
        if (index + 1u < a.scene_count)
            assert((a.scenes[index].scene.flags & JC_SCENE_FINAL) == 0u);
    }
    assert((a.scenes[a.scene_count - 1u].scene.flags & JC_SCENE_FINAL) != 0u);
}

int main(void)
{
    test_table();
    test_calendar();
    test_plan();
    puts("director tests passed");
    return 0;
}
