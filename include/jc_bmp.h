/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef JC_BMP_H
#define JC_BMP_H

#include "jc_surface.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The original interpreter reserved 120 frames per BMP slot; the PS1 port
 * widened that to 255.  Keep parsing bounded while accepting either layout.
 */
#define JC_BMP_MAX_IMAGES 256u
#define JC_BMP_MAX_PIXELS (64u * 1024u * 1024u)
#define JC_BMP_MAX_PACKED_BYTES (64u * 1024u * 1024u)

typedef struct jc_bmp_image {
    uint16_t width;
    uint16_t height;
    size_t pixel_count;
    uint8_t *pixels;
} jc_bmp_image_t;

typedef struct jc_bmp {
    uint16_t sheet_width;
    uint16_t sheet_height;
    size_t image_count;
    jc_bmp_image_t *images;
    uint8_t *pixel_storage;
    size_t pixel_storage_size;
} jc_bmp_t;

/*
 * Parse one complete DGDS BMP resource body.  The output must not currently
 * own storage; call jc_bmp_free() before parsing into it again.
 */
bool jc_bmp_parse(jc_bmp_t *bmp, const uint8_t *data, size_t size,
                  char *error, size_t error_size);
void jc_bmp_free(jc_bmp_t *bmp);

/* Create a non-owning indexed surface view of one decoded sprite frame. */
bool jc_bmp_image_surface(jc_bmp_t *bmp, size_t image_index,
                          jc_surface_t *surface);

#endif
