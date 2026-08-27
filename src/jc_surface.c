/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "jc_surface.h"

#include <string.h>

static int maximum(int left, int right)
{
    return left > right ? left : right;
}

static int minimum(int left, int right)
{
    return left < right ? left : right;
}

bool jc_surface_init(jc_surface_t *surface, uint8_t *pixels, size_t size,
                     unsigned width, unsigned height, size_t pitch)
{
    if (surface == NULL || pixels == NULL || width == 0u || height == 0u)
        return false;
    if (pitch < width || height > size / pitch)
        return false;

    surface->pixels = pixels;
    surface->width = width;
    surface->height = height;
    surface->pitch = pitch;
    jc_surface_reset_clip(surface);
    return true;
}

void jc_surface_reset_clip(jc_surface_t *surface)
{
    surface->clip.x = 0;
    surface->clip.y = 0;
    surface->clip.width = (int)surface->width;
    surface->clip.height = (int)surface->height;
}

void jc_surface_set_clip(jc_surface_t *surface, int x, int y,
                         int width, int height)
{
    int left = maximum(0, x);
    int top = maximum(0, y);
    int right = minimum((int)surface->width, x + maximum(0, width));
    int bottom = minimum((int)surface->height, y + maximum(0, height));

    surface->clip.x = left;
    surface->clip.y = top;
    surface->clip.width = maximum(0, right - left);
    surface->clip.height = maximum(0, bottom - top);
}

void jc_surface_clear(jc_surface_t *surface, uint8_t color)
{
    unsigned row;
    for (row = 0u; row < surface->height; ++row)
        memset(surface->pixels + row * surface->pitch, color, surface->width);
}

void jc_surface_put_pixel(jc_surface_t *surface, int x, int y, uint8_t color)
{
    int right = surface->clip.x + surface->clip.width;
    int bottom = surface->clip.y + surface->clip.height;
    if (x < surface->clip.x || x >= right || y < surface->clip.y || y >= bottom)
        return;
    surface->pixels[(size_t)y * surface->pitch + (size_t)x] = color;
}

void jc_surface_fill_rect(jc_surface_t *surface, int x, int y,
                          int width, int height, uint8_t color)
{
    int left = maximum(x, surface->clip.x);
    int top = maximum(y, surface->clip.y);
    int right = minimum(x + maximum(0, width),
                        surface->clip.x + surface->clip.width);
    int bottom = minimum(y + maximum(0, height),
                        surface->clip.y + surface->clip.height);
    int row;

    if (right <= left || bottom <= top)
        return;
    for (row = top; row < bottom; ++row)
        memset(surface->pixels + (size_t)row * surface->pitch + (size_t)left,
               color, (size_t)(right - left));
}

void jc_surface_blit(jc_surface_t *destination, int destination_x,
                     int destination_y, const jc_surface_t *source,
                     int transparent_index, bool flip_horizontal)
{
    unsigned source_y;

    for (source_y = 0u; source_y < source->height; ++source_y) {
        unsigned source_x;
        int y = destination_y + (int)source_y;
        for (source_x = 0u; source_x < source->width; ++source_x) {
            unsigned read_x = flip_horizontal ? source->width - 1u - source_x : source_x;
            uint8_t color = source->pixels[(size_t)source_y * source->pitch + read_x];
            if (transparent_index < 0 || color != (uint8_t)transparent_index)
                jc_surface_put_pixel(destination, destination_x + (int)source_x,
                                     y, color);
        }
    }
}
