/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef JC_FADE_H
#define JC_FADE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Nonblocking scene-boundary fades for an XRGB8888 framebuffer.
 *
 * A caller begins one leg, applies its mask to each presented frame, then
 * advances it once.  A complete scene change is normally an OUT leg, a scene
 * swap while the frame is black, and an IN leg using the same style.
 */

#define JC_FADE_MAX_STEPS 10000u

typedef enum jc_fade_direction {
    JC_FADE_OUT = 0,
    JC_FADE_IN,
    JC_FADE_DIRECTION_COUNT
} jc_fade_direction_t;

typedef enum jc_fade_style {
    JC_FADE_IRIS = 0,
    JC_FADE_BOX,
    JC_FADE_WIPE_RIGHT_TO_LEFT,
    JC_FADE_WIPE_LEFT_TO_RIGHT,
    JC_FADE_SPLIT,
    JC_FADE_STYLE_COUNT
} jc_fade_style_t;

typedef struct jc_fade {
    uint32_t step;
    uint32_t step_count;
    jc_fade_direction_t direction;
    jc_fade_style_t style;
    bool active;
} jc_fade_t;

void jc_fade_init(jc_fade_t *fade);

/* The PS1-derived sequence rotates through all five styles in this order. */
jc_fade_style_t jc_fade_style_for_sequence(uint32_t sequence_index);

/* Returns 20 steps for iris/box and 16 for each linear/split wipe. */
uint32_t jc_fade_default_steps(jc_fade_style_t style);

bool jc_fade_begin(jc_fade_t *fade, jc_fade_direction_t direction,
                   jc_fade_style_t style);
bool jc_fade_begin_steps(jc_fade_t *fade, jc_fade_direction_t direction,
                         jc_fade_style_t style, uint32_t step_count);
bool jc_fade_is_active(const jc_fade_t *fade);

/*
 * Apply the current transition mask in place.  pitch is measured in pixels.
 * The mask color is normally 0x00000000 (black).  Padding after width is never
 * touched.  An initialized, inactive fade is a successful no-op.
 */
bool jc_fade_apply(const jc_fade_t *fade, uint32_t *pixels,
                   unsigned width, unsigned height, size_t pitch,
                   uint32_t mask_color);

/* Consume the current displayed step.  The fade becomes inactive at its end. */
void jc_fade_advance(jc_fade_t *fade);

#endif
