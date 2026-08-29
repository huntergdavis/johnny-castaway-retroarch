/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "jc_holiday_overlay.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIDTH 640u
#define HEIGHT 480u

int main(void)
{
    jc_holiday_overlay_selection_t selection;
    jc_holiday_overlay_choice_t choice;
    const jc_holiday_extra_t *holiday;
    uint32_t *pixels;
    size_t index;
    jc_palette_t palette;

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
    for (index = 0u; index < JC_PALETTE_COLORS; ++index)
        palette.xrgb[index] = 0x00100000u + (uint32_t)index;
    for (index = 2u; index < jc_holiday_overlay_choice_count(); ++index) {
        const jc_holiday_extra_t *forced;

        assert(jc_holiday_overlay_choice_at(index, &choice));
        assert(choice.selection.mode == JC_HOLIDAY_OVERLAY_FORCED);
        assert(jc_holiday_overlay_parse(choice.value, &selection));
        assert(selection.holiday_id == choice.selection.holiday_id);
        forced = jc_holiday_overlay_resolve(&selection, 2000, 1, 2);
        assert(forced != NULL && forced->id == selection.holiday_id);
        assert(strcmp(choice.label, forced->title) == 0);
    }

    holiday = jc_holiday_extra_by_id(36);
    assert(holiday != NULL && holiday->sprite_index == 14);
    assert(jc_holiday_overlay_has_emblem(holiday));
    memset(pixels, 0x5a, (size_t)WIDTH * HEIGHT * sizeof(*pixels));
    {
        size_t drawn = 0u;
        const uint32_t untouched = UINT32_C(0x5a5a5a5a);
        assert(jc_holiday_overlay_render_emblem(
            pixels, WIDTH, HEIGHT, WIDTH, holiday, &palette, &drawn));
        assert(drawn == 241u);
        assert(pixels[(size_t)283u * WIDTH + 404u] == untouched);
        assert(pixels[(size_t)284u * WIDTH + 403u] == untouched);
        assert(pixels[(size_t)316u * WIDTH + 404u] == untouched);
        assert(pixels[(size_t)284u * WIDTH + 436u] == untouched);
        assert(!jc_holiday_overlay_render_emblem(
            NULL, WIDTH, HEIGHT, WIDTH, holiday, &palette, NULL));
    }

    holiday = jc_holiday_extra_by_id(1);
    assert(holiday != NULL && !jc_holiday_overlay_has_emblem(holiday));
    assert(!jc_holiday_overlay_render_emblem(
        pixels, WIDTH, HEIGHT, WIDTH, holiday, &palette, NULL));

    {
        jc_bmp_t sheet;
        jc_bmp_image_t images[4];
        uint8_t storage[4] = {2u, 2u, 2u, 2u};
        size_t drawn = 0u;

        memset(&sheet, 0, sizeof(sheet));
        memset(images, 0, sizeof(images));
        for (index = 0u; index < 4u; ++index) {
            images[index].width = 1u;
            images[index].height = 1u;
            images[index].pixel_count = 1u;
            images[index].pixels = &storage[index];
        }
        sheet.image_count = 4u;
        sheet.images = images;
        memset(pixels, 0x5a, (size_t)WIDTH * HEIGHT * sizeof(*pixels));
        assert(jc_holiday_overlay_render_original(
            pixels, WIDTH, HEIGHT, WIDTH, holiday, &palette,
            &sheet, 0u, &drawn));
        assert(drawn == 1u);
        assert(pixels[(size_t)298u * WIDTH + 410u] == palette.xrgb[2]);
        assert(pixels[(size_t)298u * WIDTH + 409u] ==
               UINT32_C(0x5a5a5a5a));
        assert(!jc_holiday_overlay_render_original(
            pixels, WIDTH, HEIGHT, WIDTH, jc_holiday_extra_by_id(36),
            &palette, &sheet, 0u, NULL));
    }

    free(pixels);
    puts("holiday overlay tests passed");
    return 0;
}
