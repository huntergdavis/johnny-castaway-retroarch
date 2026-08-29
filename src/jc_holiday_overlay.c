/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The interaction follows Hunter Davis's GPLv3 Johnny Castaway PS1 holiday
 * menu at revision 25c5d8459: date-driven selection, an explicit off state,
 * and manual holiday previews. This implementation is new portable C99 and
 * uses the translated catalog in jc_extras.c. It does not copy the PS1 GPU
 * menu code or Sierra's four proprietary holiday sprites. The 32 new emblem
 * frames are mechanically imported from the PS1 port's GPL art pipeline.
 */
#include "jc_holiday_overlay.h"

#include <string.h>

#define JC_HOLIDAY_EMBLEM_FIRST_SPRITE 4
#define JC_HOLIDAY_EMBLEM_LAST_SPRITE 35
#define JC_HOLIDAY_EMBLEM_WIDTH 32u
#define JC_HOLIDAY_EMBLEM_HEIGHT 32u
#define JC_HOLIDAY_EMBLEM_PACKED_ROW_BYTES 16u
#define JC_HOLIDAY_EMBLEM_PACKED_FRAME_BYTES 512u
#define JC_HOLIDAY_EMBLEM_X 404u
#define JC_HOLIDAY_EMBLEM_Y 284u

#include "jc_holiday_emblems.inc"

static const char *const holiday_values[] = {
    "new_year",
    "elvis_birthday",
    "mlk_day",
    "groundhog_day",
    "valentines_day",
    "super_bowl",
    "presidents_day",
    "mardi_gras",
    "pi_day",
    "st_patricks_day",
    "spring",
    "april_fools_day",
    "four_twenty",
    "easter",
    "earth_day",
    "star_wars_day",
    "cinco_de_mayo",
    "mothers_day",
    "memorial_day",
    "fathers_day",
    "summer",
    "pride_day",
    "independence_day",
    "moon_landing_day",
    "watermelon_day",
    "left_handers_day",
    "hawaii_statehood_day",
    "labor_day",
    "pirate_day",
    "autumn",
    "indigenous_peoples_day",
    "halloween",
    "election_day",
    "veterans_day",
    "thanksgiving",
    "christmas"
};

size_t jc_holiday_overlay_choice_count(void)
{
    return JC_HOLIDAY_OVERLAY_OPTION_COUNT;
}

bool jc_holiday_overlay_choice_at(size_t index,
                                  jc_holiday_overlay_choice_t *choice)
{
    const jc_holiday_extra_t *holiday;

    if (choice == NULL || index >= jc_holiday_overlay_choice_count())
        return false;
    if (index == 0u) {
        choice->value = "auto";
        choice->label = "Automatic - Local Date";
        choice->selection.mode = JC_HOLIDAY_OVERLAY_AUTO;
        choice->selection.holiday_id = 0;
        return true;
    }
    if (index == 1u) {
        choice->value = "off";
        choice->label = "Off";
        choice->selection.mode = JC_HOLIDAY_OVERLAY_OFF;
        choice->selection.holiday_id = 0;
        return true;
    }

    holiday = jc_holiday_extra_at(index - 2u);
    if (holiday == NULL)
        return false;
    choice->value = holiday_values[index - 2u];
    choice->label = holiday->title;
    choice->selection.mode = JC_HOLIDAY_OVERLAY_FORCED;
    choice->selection.holiday_id = holiday->id;
    return true;
}

bool jc_holiday_overlay_parse(const char *value,
                              jc_holiday_overlay_selection_t *selection)
{
    size_t index;

    if (value == NULL || selection == NULL)
        return false;
    for (index = 0u; index < jc_holiday_overlay_choice_count(); ++index) {
        jc_holiday_overlay_choice_t choice;
        if (jc_holiday_overlay_choice_at(index, &choice) &&
            strcmp(value, choice.value) == 0) {
            *selection = choice.selection;
            return true;
        }
    }
    return false;
}

const jc_holiday_extra_t *jc_holiday_overlay_resolve(
    const jc_holiday_overlay_selection_t *selection,
    int year, int month, int day)
{
    if (selection == NULL || selection->mode == JC_HOLIDAY_OVERLAY_OFF)
        return NULL;
    if (selection->mode == JC_HOLIDAY_OVERLAY_AUTO)
        return jc_holiday_extra_for_date(year, month, day, false);
    if (selection->mode == JC_HOLIDAY_OVERLAY_FORCED)
        return jc_holiday_extra_by_id(selection->holiday_id);
    return NULL;
}

bool jc_holiday_overlay_has_emblem(const jc_holiday_extra_t *holiday)
{
    return holiday != NULL &&
           holiday->sprite_index >= JC_HOLIDAY_EMBLEM_FIRST_SPRITE &&
           holiday->sprite_index <= JC_HOLIDAY_EMBLEM_LAST_SPRITE;
}

bool jc_holiday_overlay_render_emblem(
    uint32_t *pixels, size_t width, size_t height, size_t stride,
    const jc_holiday_extra_t *holiday, const jc_palette_t *palette,
    size_t *drawn_pixels)
{
    const unsigned char *frame;
    size_t drawn = 0u;
    size_t y;

    if (drawn_pixels != NULL)
        *drawn_pixels = 0u;
    if (pixels == NULL || palette == NULL ||
        !jc_holiday_overlay_has_emblem(holiday) ||
        stride < width || JC_HOLIDAY_EMBLEM_X >= width ||
        JC_HOLIDAY_EMBLEM_Y >= height)
        return false;
    frame = jc_holiday_emblem_packed +
            (size_t)(holiday->sprite_index - JC_HOLIDAY_EMBLEM_FIRST_SPRITE) *
            JC_HOLIDAY_EMBLEM_PACKED_FRAME_BYTES;
    for (y = 0u; y < JC_HOLIDAY_EMBLEM_HEIGHT &&
                 JC_HOLIDAY_EMBLEM_Y + y < height; ++y) {
        size_t x;
        uint32_t *destination = pixels +
            (JC_HOLIDAY_EMBLEM_Y + y) * stride + JC_HOLIDAY_EMBLEM_X;
        const unsigned char *source = frame +
            y * JC_HOLIDAY_EMBLEM_PACKED_ROW_BYTES;
        for (x = 0u; x < JC_HOLIDAY_EMBLEM_WIDTH &&
                     JC_HOLIDAY_EMBLEM_X + x < width; ++x) {
            unsigned char packed = source[x / 2u];
            unsigned char index = (x & 1u) == 0u ?
                (unsigned char)(packed >> 4) :
                (unsigned char)(packed & 0x0fu);
            if (index != 0u) {
                destination[x] = palette->xrgb[index];
                ++drawn;
            }
        }
    }
    if (drawn_pixels != NULL)
        *drawn_pixels = drawn;
    return drawn > 0u;
}

static bool original_position(int holiday_id, size_t *x, size_t *y)
{
    if (x == NULL || y == NULL)
        return false;
    switch (holiday_id) {
    case 1: /* Halloween */
        *x = 410u;
        *y = 298u;
        return true;
    case 2: /* St. Patrick's Day */
        *x = 333u;
        *y = 286u;
        return true;
    case 3: /* Christmas */
        *x = 404u;
        *y = 267u;
        return true;
    case 4: /* New Year's Day */
        *x = 361u;
        *y = 155u;
        return true;
    default:
        return false;
    }
}

bool jc_holiday_overlay_render_original(
    uint32_t *pixels, size_t width, size_t height, size_t stride,
    const jc_holiday_extra_t *holiday, const jc_palette_t *palette,
    jc_bmp_t *user_sheet, uint8_t transparent_index,
    size_t *drawn_pixels)
{
    jc_surface_t sprite;
    size_t origin_x;
    size_t origin_y;
    size_t drawn = 0u;
    size_t y;

    if (drawn_pixels != NULL)
        *drawn_pixels = 0u;
    if (pixels == NULL || palette == NULL || holiday == NULL ||
        !holiday->original_four || holiday->sprite_index < 0 ||
        user_sheet == NULL || stride < width ||
        !original_position(holiday->id, &origin_x, &origin_y) ||
        !jc_bmp_image_surface(user_sheet, (size_t)holiday->sprite_index,
                              &sprite) ||
        origin_x >= width || origin_y >= height)
        return false;
    for (y = 0u; y < sprite.height && origin_y + y < height; ++y) {
        size_t x;
        const uint8_t *source = sprite.pixels + y * sprite.pitch;
        uint32_t *destination = pixels +
            (origin_y + y) * stride + origin_x;
        for (x = 0u; x < sprite.width && origin_x + x < width; ++x) {
            uint8_t index = source[x];
            if (index != transparent_index) {
                destination[x] = palette->xrgb[index];
                ++drawn;
            }
        }
    }
    if (drawn_pixels != NULL)
        *drawn_pixels = drawn;
    return drawn > 0u;
}
