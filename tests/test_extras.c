/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "jc_captions.h"
#include "jc_chapters.h"
#include "jc_extras.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_captions(void)
{
    jc_captions_t state;
    const jc_caption_entry_t *entry;
    size_t index;

    assert(jc_caption_count() == 79u);
    assert(jc_caption_at(79u) == NULL);
    for (index = 0u; index < jc_caption_count(); ++index) {
        entry = jc_caption_at(index);
        assert(entry != NULL && entry->id != NULL && entry->text != NULL);
        assert(jc_caption_lookup(entry->id) == entry);
    }
    assert(jc_caption_lookup("missing") == NULL);
    entry = jc_caption_for_ads("FISHING.ADS", 3u);
    assert(entry != NULL && strcmp(entry->id, "scene20") == 0);
    entry = jc_caption_for_ads("visitor", 5u);
    assert(entry != NULL && strcmp(entry->id, "visitorboat") == 0);
    assert(jc_caption_for_ads("FISHING", 99u) == NULL);

    jc_captions_init(&state);
    assert(jc_captions_current_text(&state) == NULL);
    assert(!jc_captions_show(&state, "intro", 2u));
    jc_captions_set_enabled(&state, true);
    assert(jc_captions_show(&state, "intro", 2u));
    assert(strstr(jc_captions_current_text(&state), "Johnny Castaway") != NULL);
    jc_captions_tick(&state);
    assert(state.remaining_ticks == 1u);
    jc_captions_tick(&state);
    assert(jc_captions_current_text(&state) == NULL);
    assert(jc_captions_show_ads(&state, "BUILDING.ADS", 7u, 0u));
    assert(state.remaining_ticks == JC_CAPTION_DEFAULT_TICKS);
    assert(strcmp(state.current->id, "scene62") == 0);
    jc_captions_set_enabled(&state, false);
    assert(jc_captions_current_text(&state) == NULL);
}

static void test_chapters(void)
{
    size_t index;
    assert(jc_chapter_count() == 63u);
    assert(jc_chapter_at(63u) == NULL);
    assert(strcmp(jc_chapter_family_name(0u), "FISHING") == 0);
    assert(jc_chapter_family_name(9u) == NULL);
    for (index = 0u; index < jc_chapter_count(); ++index) {
        const jc_chapter_t *chapter = jc_chapter_at(index);
        const jc_caption_entry_t *caption;
        assert(chapter != NULL && chapter->ps1_validated);
        assert(chapter->slug != NULL && chapter->title != NULL);
        assert(chapter->ads_name != NULL && chapter->ads_tag != 0u);
        assert(chapter->ps1_preview_name != NULL);
        assert(chapter->ps1_frame_count != 0u);
        assert(jc_chapter_lookup(chapter->slug) == chapter);
        assert(jc_chapter_for_ads(chapter->ads_name, chapter->ads_tag) == chapter);
        caption = jc_caption_for_ads(chapter->ads_name, chapter->ads_tag);
        assert(caption != NULL);
        assert(strcmp(caption->id, chapter->caption_id) == 0);
    }
    assert(strcmp(jc_chapter_lookup("activity12")->ps1_preview_name,
                  "SXAC12.SCR") == 0);
    assert(jc_chapter_for_ads("STAND.ADS", 15u) ==
           jc_chapter_lookup("stand15"));
    assert(jc_chapter_lookup("missing") == NULL);
}

static void test_holidays(void)
{
    const jc_holiday_extra_t *holiday;
    int easter_month;
    int easter_day;
    size_t first;
    size_t second;

    assert(jc_holiday_extra_count() == 36u);
    assert(jc_holiday_extra_at(36u) == NULL);
    for (first = 0u; first < jc_holiday_extra_count(); ++first) {
        const jc_holiday_extra_t *row = jc_holiday_extra_at(first);
        assert(row != NULL && row->title != NULL && row->id > 0);
        assert(jc_holiday_extra_by_id(row->id) == row);
        for (second = first + 1u; second < jc_holiday_extra_count(); ++second)
            assert(row->id != jc_holiday_extra_at(second)->id);
    }
    assert(jc_holiday_day_of_week(2026, 8, 27) == 4);
    assert(jc_holiday_day_of_week(2026, 2, 29) == -1);
    jc_holiday_easter_sunday(2026, &easter_month, &easter_day);
    assert(easter_month == 4 && easter_day == 5);
    holiday = jc_holiday_extra_for_date(2026, 4, 5, false);
    assert(holiday != NULL && holiday->id == 15);
    holiday = jc_holiday_extra_for_date(2026, 2, 17, false);
    assert(holiday != NULL && holiday->id == 11);
    holiday = jc_holiday_extra_for_date(2026, 11, 3, false);
    assert(holiday != NULL && holiday->id == 33);
    holiday = jc_holiday_extra_for_date(2026, 11, 26, false);
    assert(holiday != NULL && holiday->id == 35);
    holiday = jc_holiday_extra_for_date(2026, 10, 31, true);
    assert(holiday != NULL && holiday->id == 1 && holiday->original_four);
    assert(jc_holiday_extra_for_date(2026, 2, 14, true) == NULL);
    assert(jc_holiday_extra_for_date(2026, 2, 29, false) == NULL);
}

static void test_ambience(void)
{
    const jc_ambience_asset_t *asset = jc_ambience_ocean_asset();
    jc_ambience_config_t config;
    assert(asset != NULL && strcmp(asset->id, "ocean") == 0);
    assert(strcmp(asset->license_name, "CC0 1.0 / public domain") == 0);
    assert(strcmp(asset->sha256,
                  "b9eeae5a7f42545ad7fe99701c248c07e8b4c0ad0ab17bb86420f36ea97259c2") == 0);
    assert(asset->sample_rate == 11025u && asset->channels == 1u);
    assert(asset->duration_milliseconds == 20000u && asset->seamless_loop);
    jc_ambience_config_init(&config);
    assert(config.enabled && config.volume == 56u);
    jc_ambience_config_set_volume(&config, 250u);
    assert(config.volume == 100u);
    jc_ambience_config_set_enabled(&config, false);
    assert(!config.enabled);
}

int main(void)
{
    test_captions();
    test_chapters();
    test_holidays();
    test_ambience();
    puts("extras tests passed");
    return 0;
}
