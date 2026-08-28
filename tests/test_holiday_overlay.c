/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "jc_holiday_overlay.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIDTH 640u
#define HEIGHT 480u

static uint64_t hash_pixels(const uint32_t *pixels, size_t count)
{
    uint64_t hash = 1469598103934665603ull;
    size_t index;
    for (index = 0u; index < count; ++index) {
        hash ^= pixels[index];
        hash *= 1099511628211ull;
    }
    return hash;
}

int main(void)
{
    jc_holiday_overlay_selection_t selection;
    jc_holiday_overlay_choice_t choice;
    const jc_holiday_extra_t *holiday;
    uint32_t *pixels;
    uint64_t hashes[36];
    char text[128];
    size_t index;
    size_t other;

    assert(jc_holiday_overlay_choice_count() == 38u);
    assert(jc_holiday_overlay_choice_at(0u, &choice));
    assert(strcmp(choice.value, "auto") == 0);
    assert(choice.selection.mode == JC_HOLIDAY_OVERLAY_AUTO);
    assert(jc_holiday_overlay_choice_at(1u, &choice));
    assert(strcmp(choice.value, "off") == 0);
    assert(choice.selection.mode == JC_HOLIDAY_OVERLAY_OFF);
    assert(!jc_holiday_overlay_choice_at(38u, &choice));
    assert(!jc_holiday_overlay_choice_at(0u, NULL));

    assert(jc_holiday_overlay_parse("auto", &selection));
    holiday = jc_holiday_overlay_resolve(&selection, 2026, 12, 25);
    assert(holiday != NULL && holiday->id == 3);
    assert(jc_holiday_overlay_resolve(&selection, 2026, 8, 27) == NULL);

    assert(jc_holiday_overlay_parse("off", &selection));
    assert(jc_holiday_overlay_resolve(&selection, 2026, 12, 25) == NULL);
    assert(!jc_holiday_overlay_parse("not-a-holiday", &selection));
    assert(!jc_holiday_overlay_parse(NULL, &selection));

    pixels = (uint32_t *)calloc((size_t)WIDTH * HEIGHT, sizeof(*pixels));
    assert(pixels != NULL);
    for (index = 2u; index < jc_holiday_overlay_choice_count(); ++index) {
        jc_caption_render_result_t result;
        const jc_holiday_extra_t *forced;
        size_t pixel_index;
        bool lower_frame_changed = false;

        assert(jc_holiday_overlay_choice_at(index, &choice));
        assert(choice.selection.mode == JC_HOLIDAY_OVERLAY_FORCED);
        assert(jc_holiday_overlay_parse(choice.value, &selection));
        assert(selection.holiday_id == choice.selection.holiday_id);
        forced = jc_holiday_overlay_resolve(&selection, 2000, 1, 2);
        assert(forced != NULL && forced->id == selection.holiday_id);
        assert(strcmp(choice.label, forced->title) == 0);
        assert(jc_holiday_overlay_format(forced, text, sizeof(text)));
        assert(strstr(text, forced->title) != NULL);
        assert(strstr(text, forced->date_label) != NULL);

        memset(pixels, 0, (size_t)WIDTH * HEIGHT * sizeof(*pixels));
        assert(jc_holiday_overlay_render(pixels, WIDTH, HEIGHT, WIDTH,
                                         forced, &result));
        assert(result.line_count == 2u);
        assert(result.foreground_pixels > 0u);
        assert(result.y < 64u && result.height < 64u);
        assert(pixels[(size_t)result.y * WIDTH] != 0u);
        for (pixel_index = (size_t)HEIGHT / 2u * WIDTH;
             pixel_index < (size_t)WIDTH * HEIGHT; ++pixel_index) {
            if (pixels[pixel_index] != 0u)
                lower_frame_changed = true;
        }
        assert(!lower_frame_changed);
        hashes[index - 2u] = hash_pixels(pixels, (size_t)WIDTH * HEIGHT);
        for (other = 0u; other < index - 2u; ++other)
            assert(hashes[other] != hashes[index - 2u]);
    }

    holiday = jc_holiday_extra_by_id(8);
    assert(holiday != NULL);
    assert(jc_holiday_overlay_format(holiday, text, sizeof(text)));
    assert(strcmp(text, "HOLIDAY: Valentine's Day\nFEB 14") == 0);
    assert(!jc_holiday_overlay_format(holiday, text, 8u));
    assert(!jc_holiday_overlay_render(NULL, WIDTH, HEIGHT, WIDTH,
                                      holiday, NULL));

    memset(pixels, 0, (size_t)WIDTH * HEIGHT * sizeof(*pixels));
    {
        jc_caption_render_result_t result;
        assert(jc_holiday_overlay_render_anchored(
            pixels, WIDTH, HEIGHT, WIDTH, holiday,
            JC_CAPTION_ANCHOR_BOTTOM, &result));
        assert(result.y > HEIGHT / 2u);
        assert(result.y + result.height <= HEIGHT);
    }

    free(pixels);
    puts("holiday overlay tests passed");
    return 0;
}
