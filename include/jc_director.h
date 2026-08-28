/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef JC_DIRECTOR_H
#define JC_DIRECTOR_H

#include "jc_rng.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JC_DIRECTOR_MAX_SCENES 20u

enum jc_scene_flag {
    JC_SCENE_FINAL = 0x01,
    JC_SCENE_FIRST = 0x02,
    JC_SCENE_ISLAND = 0x04,
    JC_SCENE_LEFT_ISLAND = 0x08,
    JC_SCENE_VARIABLE_POSITION = 0x10,
    JC_SCENE_LOW_TIDE = 0x20,
    JC_SCENE_NO_RAFT = 0x40,
    JC_SCENE_NO_HOLIDAY = 0x80
};

typedef enum jc_holiday {
    JC_HOLIDAY_NONE = 0,
    JC_HOLIDAY_HALLOWEEN,
    JC_HOLIDAY_ST_PATRICK,
    JC_HOLIDAY_CHRISTMAS,
    JC_HOLIDAY_NEW_YEAR
} jc_holiday_t;

typedef enum jc_daynight_mode {
    JC_DAYNIGHT_ORIGINAL = 0,
    JC_DAYNIGHT_REAL_24H
} jc_daynight_mode_t;

typedef struct jc_story_scene {
    const char *ads_name;
    uint16_t ads_tag;
    uint8_t spot_start;
    uint8_t heading_start;
    uint8_t spot_end;
    uint8_t heading_end;
    uint8_t story_day;
    uint8_t flags;
} jc_story_scene_t;

typedef struct jc_island_state {
    bool low_tide;
    bool night;
    uint8_t raft_stage;
    jc_holiday_t holiday;
    int x;
    int y;
} jc_island_state_t;

typedef struct jc_scene_play {
    jc_story_scene_t scene;
    bool has_walk_from;
    uint8_t walk_from_spot;
    uint8_t walk_from_heading;
    bool has_walk_to;
    uint8_t walk_to_spot;
    uint8_t walk_to_heading;
    bool day_beat;
    bool left_island;
} jc_scene_play_t;

typedef struct jc_story_run {
    bool on_island;
    jc_island_state_t island;
    size_t scene_count;
    jc_scene_play_t scenes[JC_DIRECTOR_MAX_SCENES];
} jc_story_run_t;

typedef struct jc_director {
    uint8_t current_day;
    int stored_yday;
    jc_daynight_mode_t daynight_mode;
} jc_director_t;

void jc_director_init(jc_director_t *director, uint8_t story_day,
                      int stored_yday);
bool jc_director_advance_day(jc_director_t *director, int today_yday);
bool jc_director_plan(jc_director_t *director, int today_yday, uint8_t hour,
                      uint8_t month, uint8_t month_day, jc_rng_t *rng,
                      jc_story_run_t *run);
bool jc_director_is_night(uint8_t hour, jc_daynight_mode_t mode);
uint8_t jc_director_raft_for_day(uint8_t story_day);
jc_holiday_t jc_director_holiday(uint8_t month, uint8_t month_day);
size_t jc_director_scene_count(void);
bool jc_director_scene(size_t index, jc_story_scene_t *scene);

#endif
