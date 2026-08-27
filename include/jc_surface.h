/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef JC_SURFACE_H
#define JC_SURFACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct jc_rect {
    int x;
    int y;
    int width;
    int height;
} jc_rect_t;

typedef struct jc_surface {
    uint8_t *pixels;
    unsigned width;
    unsigned height;
    size_t pitch;
    jc_rect_t clip;
} jc_surface_t;

bool jc_surface_init(jc_surface_t *surface, uint8_t *pixels, size_t size,
                     unsigned width, unsigned height, size_t pitch);
void jc_surface_reset_clip(jc_surface_t *surface);
void jc_surface_set_clip(jc_surface_t *surface, int x, int y,
                         int width, int height);
void jc_surface_clear(jc_surface_t *surface, uint8_t color);
void jc_surface_put_pixel(jc_surface_t *surface, int x, int y, uint8_t color);
void jc_surface_fill_rect(jc_surface_t *surface, int x, int y,
                          int width, int height, uint8_t color);
void jc_surface_blit(jc_surface_t *destination, int destination_x,
                     int destination_y, const jc_surface_t *source,
                     int transparent_index, bool flip_horizontal);

#endif
