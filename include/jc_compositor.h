/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef JC_COMPOSITOR_H
#define JC_COMPOSITOR_H

#include "jc_surface.h"

#include <stdbool.h>
#include <stddef.h>

#define JC_COMPOSITOR_MAX_LAYERS 256u
#define JC_COMPOSITOR_TRANSPARENT 255

typedef struct jc_compositor_layer {
    const jc_surface_t *surface;
    int x;
    int y;
    int transparent_index;
    bool flip_horizontal;
    bool visible;
} jc_compositor_layer_t;

/*
 * Copy base into destination, then draw visible layers in array order.  Every
 * write respects destination's current clip rectangle.  Passing NULL as base
 * leaves the destination's existing pixels in place.
 */
bool jc_compositor_compose(jc_surface_t *destination,
                           const jc_surface_t *base,
                           const jc_compositor_layer_t *layers,
                           size_t layer_count);

#endif
