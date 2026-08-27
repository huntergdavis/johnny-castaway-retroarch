/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "jc_palette.h"

#include <stdio.h>
#include <string.h>

#define JC_PALETTE_BODY_BYTES (16u + JC_PALETTE_COLORS * 3u)

static bool fail(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size > 0u)
        snprintf(error, error_size, "%s", message);
    return false;
}

bool jc_palette_decode(jc_palette_t *palette, const uint8_t *data, size_t size,
                       char *error, size_t error_size)
{
    size_t index;

    if (palette == NULL || data == NULL)
        return fail(error, error_size, "palette decoder received null input");
    if (size < JC_PALETTE_BODY_BYTES)
        return fail(error, error_size, "PAL resource is truncated");
    if (memcmp(data, "PAL:", 4u) != 0 || memcmp(data + 8u, "VGA:", 4u) != 0)
        return fail(error, error_size, "PAL resource has an invalid chunk tag");

    for (index = 0u; index < JC_PALETTE_COLORS; ++index) {
        const uint8_t *rgb = data + 16u + index * 3u;
        uint32_t red = (uint32_t)(rgb[0] & 0x3fu) << 2;
        uint32_t green = (uint32_t)(rgb[1] & 0x3fu) << 2;
        uint32_t blue = (uint32_t)(rgb[2] & 0x3fu) << 2;
        palette->xrgb[index] = (red << 16) | (green << 8) | blue;
    }
    if (error != NULL && error_size > 0u)
        error[0] = '\0';
    return true;
}
