/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The interaction follows Hunter Davis's GPLv3 Johnny Castaway PS1 holiday
 * menu at revision 25c5d8459: date-driven selection, an explicit off state,
 * and manual holiday previews. This implementation is new portable C99 and
 * uses the translated catalog in jc_extras.c. It does not copy the PS1 GPU
 * menu code or the proprietary HOLIDAY.BMP sprite sheet.
 */
#include "jc_holiday_overlay.h"

#include <stdio.h>
#include <string.h>

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

bool jc_holiday_overlay_format(const jc_holiday_extra_t *holiday,
                               char *text, size_t text_size)
{
    int length;

    if (holiday == NULL || holiday->title == NULL ||
        holiday->date_label == NULL || text == NULL || text_size == 0u)
        return false;
    length = snprintf(text, text_size, "HOLIDAY: %s\n%s",
                      holiday->title, holiday->date_label);
    return length >= 0 && (size_t)length < text_size;
}

bool jc_holiday_overlay_render(uint32_t *pixels, size_t width, size_t height,
                               size_t stride,
                               const jc_holiday_extra_t *holiday,
                               jc_caption_render_result_t *result)
{
    return jc_holiday_overlay_render_anchored(
        pixels, width, height, stride, holiday, JC_CAPTION_ANCHOR_TOP, result);
}

bool jc_holiday_overlay_render_anchored(
    uint32_t *pixels, size_t width, size_t height, size_t stride,
    const jc_holiday_extra_t *holiday, jc_caption_anchor_t anchor,
    jc_caption_render_result_t *result)
{
    jc_caption_render_options_t options;
    char text[128];

    if (!jc_holiday_overlay_format(holiday, text, sizeof(text)))
        return false;
    jc_caption_render_options_init(&options);
    options.text_size = JC_CAPTION_TEXT_SMALL;
    options.background = JC_CAPTION_BACKGROUND_BAR;
    options.anchor = anchor;
    options.foreground_xrgb = 0x00ffffffu;
    options.background_xrgb = 0x00132945u;
    options.horizontal_margin = 16u;
    options.vertical_margin = 8u;
    options.padding = 4u;
    options.background_opacity = 255u;
    return jc_caption_render(pixels, width, height, stride, text,
                             strlen(text), &options, result);
}
