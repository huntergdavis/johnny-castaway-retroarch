/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "jc_fade.h"

#include <limits.h>
#include <string.h>

/*
 * Transition order, shapes, and 50 Hz step counts are derived from grFadeOut
 * in Hunter Davis's Johnny-Castaway-PS1 tree:
 *
 *   repository: git@github.com:huntergdavis/Johnny-Castaway-PS1.git
 *   revision:   25c5d84593ac20cbee354eaab7779ab7397d6bbe
 *   file:       src/host/graphics.c
 *
 * The oracle paints palette index 5 over a 640x480 SDL surface.  This module
 * expresses the same five cumulative masks at arbitrary dimensions and lets
 * the caller choose the XRGB8888 mask color.  IN is the complement of the
 * corresponding OUT mask; the oracle itself only implements OUT.
 */

static bool direction_valid(jc_fade_direction_t direction)
{
    return direction < JC_FADE_DIRECTION_COUNT;
}

static bool style_valid(jc_fade_style_t style)
{
    return style < JC_FADE_STYLE_COUNT;
}

static bool state_valid(const jc_fade_t *fade)
{
    if (fade == NULL)
        return false;
    if (!fade->active)
        return true;
    return direction_valid(fade->direction) && style_valid(fade->style) &&
           fade->step_count > 0u &&
           fade->step_count <= JC_FADE_MAX_STEPS && fade->step > 0u &&
           fade->step <= fade->step_count;
}

static uint64_t integer_square_root(uint64_t value)
{
    uint64_t result = 0u;
    uint64_t bit = UINT64_C(1) << 62;

    while (bit > value)
        bit >>= 2;
    while (bit != 0u) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return result;
}

static uint64_t divide_round_up_product(uint64_t value, uint32_t numerator,
                                        uint32_t denominator)
{
    uint64_t quotient = value / denominator;
    uint64_t remainder = value % denominator;
    uint64_t partial = remainder * numerator;

    return quotient * numerator + partial / denominator +
           (partial % denominator != 0u ? 1u : 0u);
}

static uint64_t distance_from_center_twice(unsigned coordinate,
                                           unsigned extent)
{
    uint64_t pixel_center = (uint64_t)coordinate * 2u + 1u;
    uint64_t center = extent;

    return pixel_center >= center ? pixel_center - center
                                  : center - pixel_center;
}

static uint64_t iris_radius_squared(unsigned width, unsigned height,
                                    uint32_t step, uint32_t step_count)
{
    uint64_t maximum_dx = width > 0u ? (uint64_t)width - 1u : 0u;
    uint64_t maximum_dy = height > 0u ? (uint64_t)height - 1u : 0u;
    uint64_t maximum_squared = maximum_dx * maximum_dx +
                               maximum_dy * maximum_dy;
    uint64_t maximum_radius = integer_square_root(maximum_squared);
    uint64_t radius;

    if (maximum_radius * maximum_radius < maximum_squared)
        ++maximum_radius;
    radius = divide_round_up_product(maximum_radius, step, step_count);
    return radius * radius;
}

static bool shape_contains(const jc_fade_t *fade, unsigned x, unsigned y,
                           unsigned width, unsigned height,
                           uint64_t radius_squared)
{
    uint64_t dx;
    uint64_t extent;
    uint64_t coverage;

    switch (fade->style) {
    case JC_FADE_IRIS: {
        uint64_t dy = distance_from_center_twice(y, height);

        dx = distance_from_center_twice(x, width);
        return dx * dx + dy * dy <= radius_squared;
    }
    case JC_FADE_BOX:
        dx = distance_from_center_twice(x, width);
        extent = divide_round_up_product(width, fade->step,
                                         fade->step_count);
        if (dx >= extent)
            return false;
        dx = distance_from_center_twice(y, height);
        extent = divide_round_up_product(height, fade->step,
                                         fade->step_count);
        return dx < extent;
    case JC_FADE_WIPE_RIGHT_TO_LEFT:
        coverage = divide_round_up_product(width, fade->step,
                                           fade->step_count);
        return (uint64_t)x >= (uint64_t)width - coverage;
    case JC_FADE_WIPE_LEFT_TO_RIGHT:
        coverage = divide_round_up_product(width, fade->step,
                                           fade->step_count);
        return (uint64_t)x < coverage;
    case JC_FADE_SPLIT:
        dx = distance_from_center_twice(x, width);
        extent = divide_round_up_product(width, fade->step,
                                         fade->step_count);
        return dx < extent;
    default:
        return false;
    }
}

void jc_fade_init(jc_fade_t *fade)
{
    if (fade != NULL)
        memset(fade, 0, sizeof(*fade));
}

jc_fade_style_t jc_fade_style_for_sequence(uint32_t sequence_index)
{
    return (jc_fade_style_t)(sequence_index % (uint32_t)JC_FADE_STYLE_COUNT);
}

uint32_t jc_fade_default_steps(jc_fade_style_t style)
{
    if (!style_valid(style))
        return 0u;
    return style == JC_FADE_IRIS || style == JC_FADE_BOX ? 20u : 16u;
}

bool jc_fade_begin_steps(jc_fade_t *fade, jc_fade_direction_t direction,
                         jc_fade_style_t style, uint32_t step_count)
{
    if (fade == NULL || !direction_valid(direction) || !style_valid(style) ||
        step_count == 0u || step_count > JC_FADE_MAX_STEPS)
        return false;
    fade->step = 1u;
    fade->step_count = step_count;
    fade->direction = direction;
    fade->style = style;
    fade->active = true;
    return true;
}

bool jc_fade_begin(jc_fade_t *fade, jc_fade_direction_t direction,
                   jc_fade_style_t style)
{
    uint32_t steps = jc_fade_default_steps(style);

    return steps != 0u && jc_fade_begin_steps(fade, direction, style, steps);
}

bool jc_fade_is_active(const jc_fade_t *fade)
{
    return state_valid(fade) && fade->active;
}

bool jc_fade_apply(const jc_fade_t *fade, uint32_t *pixels,
                   unsigned width, unsigned height, size_t pitch,
                   uint32_t mask_color)
{
    unsigned y;
    uint64_t radius_squared = 0u;

    if (!state_valid(fade) || pixels == NULL || width == 0u || height == 0u ||
        width > (unsigned)INT_MAX || height > (unsigned)INT_MAX ||
        pitch < width || (size_t)height > SIZE_MAX / pitch)
        return false;
    if (!fade->active)
        return true;
    if (fade->style == JC_FADE_IRIS)
        radius_squared = iris_radius_squared(width, height, fade->step,
                                             fade->step_count);

    for (y = 0u; y < height; ++y) {
        uint32_t *row = pixels + (size_t)y * pitch;
        unsigned x;

        for (x = 0u; x < width; ++x) {
            bool inside = shape_contains(fade, x, y, width, height,
                                         radius_squared);
            bool masked = fade->direction == JC_FADE_OUT ? inside : !inside;

            if (masked)
                row[x] = mask_color;
        }
    }
    return true;
}

void jc_fade_advance(jc_fade_t *fade)
{
    if (!state_valid(fade) || !fade->active)
        return;
    if (fade->step == fade->step_count)
        fade->active = false;
    else
        ++fade->step;
}
