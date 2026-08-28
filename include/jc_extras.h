/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef JC_EXTRAS_H
#define JC_EXTRAS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum jc_holiday_rule_kind {
    JC_HOLIDAY_RULE_FIXED = 0,
    JC_HOLIDAY_RULE_NTH_WEEKDAY,
    JC_HOLIDAY_RULE_EASTER_OFFSET,
    JC_HOLIDAY_RULE_WINTER_SOLSTICE,
    JC_HOLIDAY_RULE_SUMMER_SOLSTICE,
    JC_HOLIDAY_RULE_VERNAL_EQUINOX,
    JC_HOLIDAY_RULE_AUTUMNAL_EQUINOX,
    JC_HOLIDAY_RULE_ELECTION_DAY
} jc_holiday_rule_kind_t;

typedef struct jc_holiday_extra {
    int id;
    const char *title;
    const char *short_name;
    const char *date_label;
    jc_holiday_rule_kind_t kind;
    int8_t month;
    int8_t day;
    int8_t nth;
    int8_t weekday;
    int8_t easter_offset;
    int8_t sprite_index;
    bool original_four;
} jc_holiday_extra_t;

typedef struct jc_ambience_asset {
    const char *id;
    const char *display_name;
    const char *ps1_filename;
    const char *source_url;
    const char *license_name;
    const char *sha256;
    uint32_t sample_rate;
    uint32_t duration_milliseconds;
    uint8_t channels;
    bool seamless_loop;
} jc_ambience_asset_t;

typedef struct jc_ambience_config {
    uint8_t volume;
    bool enabled;
} jc_ambience_config_t;

size_t jc_holiday_extra_count(void);
const jc_holiday_extra_t *jc_holiday_extra_at(size_t index);
const jc_holiday_extra_t *jc_holiday_extra_by_id(int id);
const jc_holiday_extra_t *jc_holiday_extra_for_date(int year, int month,
                                                    int day,
                                                    bool original_four_only);
int jc_holiday_day_of_week(int year, int month, int day);
void jc_holiday_easter_sunday(int year, int *month, int *day);

const jc_ambience_asset_t *jc_ambience_ocean_asset(void);
void jc_ambience_config_init(jc_ambience_config_t *config);
void jc_ambience_config_set_enabled(jc_ambience_config_t *config,
                                    bool enabled);
void jc_ambience_config_set_volume(jc_ambience_config_t *config,
                                   unsigned volume);

#endif
