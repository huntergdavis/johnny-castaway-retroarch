/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "jc_compositor.h"

#include <limits.h>

/*
 * The ordered background/saved-zone/TTM/holiday model follows
 * jc_reborn/graphics.c grUpdateScreen().  Indexed transparency and horizontal
 * flip semantics follow Wilson Reborn's wilson-engine/src/surface.rs.
 */

static bool surface_valid(const jc_surface_t *surface)
{
    return surface != NULL && surface->pixels != NULL &&
           surface->width > 0u && surface->height > 0u &&
           surface->width <= (unsigned)INT_MAX &&
           surface->height <= (unsigned)INT_MAX &&
           surface->pitch >= surface->width &&
           surface->clip.x >= 0 && surface->clip.y >= 0 &&
           surface->clip.width >= 0 && surface->clip.height >= 0 &&
           surface->clip.x <= (int)surface->width &&
           surface->clip.y <= (int)surface->height &&
           surface->clip.width <= (int)surface->width - surface->clip.x &&
           surface->clip.height <= (int)surface->height - surface->clip.y;
}

static bool layer_valid(const jc_compositor_layer_t *layer)
{
    int width;
    int height;

    if (!surface_valid(layer->surface) || layer->transparent_index < -1 ||
        layer->transparent_index > 255)
        return false;
    width = (int)layer->surface->width;
    height = (int)layer->surface->height;
    return layer->x <= INT_MAX - (width - 1) &&
           layer->y <= INT_MAX - (height - 1);
}

bool jc_compositor_compose(jc_surface_t *destination,
                           const jc_surface_t *base,
                           const jc_compositor_layer_t *layers,
                           size_t layer_count)
{
    size_t index;

    if (!surface_valid(destination) || layer_count > JC_COMPOSITOR_MAX_LAYERS ||
        (layer_count != 0u && layers == NULL))
        return false;
    if (base != NULL) {
        if (!surface_valid(base) || base->width != destination->width ||
            base->height != destination->height)
            return false;
    }
    for (index = 0u; index < layer_count; ++index) {
        const jc_compositor_layer_t *layer = &layers[index];
        if (layer->visible && !layer_valid(layer))
            return false;
    }

    if (base != NULL && base != destination &&
        base->pixels != destination->pixels)
        jc_surface_blit(destination, 0, 0, base, -1, false);
    for (index = 0u; index < layer_count; ++index) {
        const jc_compositor_layer_t *layer = &layers[index];
        if (!layer->visible)
            continue;
        jc_surface_blit(destination, layer->x, layer->y, layer->surface,
                        layer->transparent_index, layer->flip_horizontal);
    }
    return true;
}
