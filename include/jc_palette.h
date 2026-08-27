/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef JC_PALETTE_H
#define JC_PALETTE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JC_PALETTE_COLORS 256u

typedef struct jc_palette {
    uint32_t xrgb[JC_PALETTE_COLORS];
} jc_palette_t;

bool jc_palette_decode(jc_palette_t *palette, const uint8_t *data, size_t size,
                       char *error, size_t error_size);

#endif
