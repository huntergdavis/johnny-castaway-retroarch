/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef JC_HOLIDAY_OVERLAY_H
#define JC_HOLIDAY_OVERLAY_H

#include "jc_caption_render.h"
#include "jc_extras.h"

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

bool jc_holiday_overlay_format(const jc_holiday_extra_t *holiday,
                               char *text, size_t text_size);

/*
 * Draw an asset-free holiday title/date preview over an XRGB8888 surface.
 * The existing embedded 5x7 caption font is used, so no Sierra/Dynamix
 * artwork or platform font is needed. Stride is measured in pixels.
 */
bool jc_holiday_overlay_render(uint32_t *pixels, size_t width, size_t height,
                               size_t stride,
                               const jc_holiday_extra_t *holiday,
                               jc_caption_render_result_t *result);

bool jc_holiday_overlay_render_anchored(
    uint32_t *pixels, size_t width, size_t height, size_t stride,
    const jc_holiday_extra_t *holiday, jc_caption_anchor_t anchor,
    jc_caption_render_result_t *result);

#endif
