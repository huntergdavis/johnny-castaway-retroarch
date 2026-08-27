/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "jc_scr.h"
#include "jc_decompress.h"

#include <stdio.h>
#include <string.h>

static uint16_t read_le16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool fail(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size > 0u)
        snprintf(error, error_size, "%s", message);
    return false;
}

bool jc_scr_decode(const uint8_t *data, size_t size,
                   uint8_t *pixel_storage, size_t pixel_storage_size,
                   jc_surface_t *surface, char *error, size_t error_size)
{
    uint16_t width;
    uint16_t height;
    uint32_t packed_total;
    uint32_t unpacked_size;
    size_t pixel_count;
    size_t packed_pixel_count;
    size_t compressed_size;
    uint8_t method;
    size_t index;

    if (data == NULL || pixel_storage == NULL || surface == NULL)
        return fail(error, error_size, "SCR decoder received null input");
    if (size < 33u)
        return fail(error, error_size, "SCR resource is truncated");
    if (memcmp(data, "SCR:", 4u) != 0 || memcmp(data + 8u, "DIM:", 4u) != 0 ||
        memcmp(data + 20u, "BIN:", 4u) != 0)
        return fail(error, error_size, "SCR resource has an invalid chunk tag");

    width = read_le16(data + 16u);
    height = read_le16(data + 18u);
    if (width == 0u || height == 0u || (size_t)height > pixel_storage_size / width)
        return fail(error, error_size, "SCR dimensions exceed pixel storage");
    pixel_count = (size_t)width * height;
    packed_pixel_count = (pixel_count + 1u) / 2u;

    packed_total = read_le32(data + 24u);
    if (packed_total < 5u)
        return fail(error, error_size, "SCR packed block size is invalid");
    compressed_size = (size_t)packed_total - 5u;
    method = data[28u];
    unpacked_size = read_le32(data + 29u);
    if (unpacked_size != packed_pixel_count)
        return fail(error, error_size, "SCR unpacked size does not match its dimensions");
    if (compressed_size > size - 33u)
        return fail(error, error_size, "SCR packed block exceeds the resource body");

    if (!jc_decompress(method, data + 33u, compressed_size, pixel_storage,
                       packed_pixel_count, error, error_size))
        return false;
    for (index = packed_pixel_count; index > 0u; --index) {
        uint8_t packed = pixel_storage[index - 1u];
        size_t pixel = (index - 1u) * 2u;
        if (pixel + 1u < pixel_count)
            pixel_storage[pixel + 1u] = (uint8_t)(packed & 0x0fu);
        pixel_storage[pixel] = (uint8_t)(packed >> 4);
    }

    if (!jc_surface_init(surface, pixel_storage, pixel_storage_size,
                         width, height, width))
        return fail(error, error_size, "SCR surface initialization failed");
    if (error != NULL && error_size > 0u)
        error[0] = '\0';
    return true;
}
