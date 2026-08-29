/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef JC_HOLIDAY_OVERLAY_H
#define JC_HOLIDAY_OVERLAY_H

#include "jc_bmp.h"
#include "jc_extras.h"
#include "jc_palette.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JC_HOLIDAY_OVERLAY_OPTION_COUNT 38u

typedef enum jc_holiday_overlay_mode {
    JC_HOLIDAY_OVERLAY_AUTO = 0,
    JC_HOLIDAY_OVERLAY_OFF,
    JC_HOLIDAY_OVERLAY_FORCED
} jc_holiday_overlay_mode_t;

typedef struct jc_holiday_overlay_selection {
    jc_holiday_overlay_mode_t mode;
    int holiday_id;
} jc_holiday_overlay_selection_t;

typedef struct jc_holiday_overlay_choice {
    const char *value;
    const char *label;
    jc_holiday_overlay_selection_t selection;
} jc_holiday_overlay_choice_t;

size_t jc_holiday_overlay_choice_count(void);
bool jc_holiday_overlay_choice_at(size_t index,
                                  jc_holiday_overlay_choice_t *choice);
bool jc_holiday_overlay_parse(const char *value,
                              jc_holiday_overlay_selection_t *selection);

const jc_holiday_extra_t *jc_holiday_overlay_resolve(
    const jc_holiday_overlay_selection_t *selection,
    int year, int month, int day);

/*
 * The embedded emblem set contains only PS1-port-generated frames 4..35.
 * Sierra's four original holiday sprites remain excluded.
 */
bool jc_holiday_overlay_has_emblem(const jc_holiday_extra_t *holiday);
bool jc_holiday_overlay_render_emblem(
    uint32_t *pixels, size_t width, size_t height, size_t stride,
    const jc_holiday_extra_t *holiday, const jc_palette_t *palette,
    size_t *drawn_pixels);

/*
 * Render one of the four original frames only from a caller-owned sheet,
 * normally decoded from the user's RESOURCE archive. No original bytes are
 * embedded by this module.
 */
bool jc_holiday_overlay_render_original(
    uint32_t *pixels, size_t width, size_t height, size_t stride,
    const jc_holiday_extra_t *holiday, const jc_palette_t *palette,
    jc_bmp_t *user_sheet, uint8_t transparent_index,
    size_t *drawn_pixels);

#endif
