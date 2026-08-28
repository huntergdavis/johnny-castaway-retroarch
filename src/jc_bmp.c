/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "jc_bmp.h"

#include "jc_decompress.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Format sequencing and packed-block semantics follow Wilson Reborn's
 * crates/wilson-dgds/src/{bmp,chunk,pixels}.rs.  The high-nibble-first pixel
 * order is independently confirmed by jc_reborn/graphics.c grLoadBmp().
 * This implementation adds fixed resource limits and checked arithmetic for
 * use with untrusted frontend content.
 */

typedef struct jc_bmp_reader {
    const uint8_t *data;
    size_t size;
    size_t offset;
} jc_bmp_reader_t;

static bool fail(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size > 0u)
        snprintf(error, error_size, "%s", message);
    return false;
}

static bool take(jc_bmp_reader_t *reader, size_t count,
                 const uint8_t **result)
{
    if (count > reader->size - reader->offset)
        return false;
    *result = reader->data + reader->offset;
    reader->offset += count;
    return true;
}

static bool read_u8(jc_bmp_reader_t *reader, uint8_t *result)
{
    const uint8_t *data;
    if (!take(reader, 1u, &data))
        return false;
    *result = data[0];
    return true;
}

static bool read_u16(jc_bmp_reader_t *reader, uint16_t *result)
{
    const uint8_t *data;
    if (!take(reader, 2u, &data))
        return false;
    *result = (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
    return true;
}

static bool read_u32(jc_bmp_reader_t *reader, uint32_t *result)
{
    const uint8_t *data;
    if (!take(reader, 4u, &data))
        return false;
    *result = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
              ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
    return true;
}

static bool expect_tag(jc_bmp_reader_t *reader, const char tag[4])
{
    const uint8_t *data;
    return take(reader, 4u, &data) && memcmp(data, tag, 4u) == 0;
}

static bool add_size(size_t left, size_t right, size_t *result)
{
    if (right > SIZE_MAX - left)
        return false;
    *result = left + right;
    return true;
}

static bool multiply_size(size_t left, size_t right, size_t *result)
{
    if (left != 0u && right > SIZE_MAX / left)
        return false;
    *result = left * right;
    return true;
}

void jc_bmp_free(jc_bmp_t *bmp)
{
    if (bmp == NULL)
        return;
    free(bmp->pixel_storage);
    free(bmp->images);
    memset(bmp, 0, sizeof(*bmp));
}

bool jc_bmp_parse(jc_bmp_t *bmp, const uint8_t *data, size_t size,
                  char *error, size_t error_size)
{
    jc_bmp_t parsed;
    jc_bmp_reader_t reader;
    uint32_t info_size;
    uint16_t image_count;
    uint32_t packed_total;
    uint32_t unpacked_size;
    uint8_t compression_method;
    const uint8_t *compressed_data;
    uint8_t *packed_pixels = NULL;
    size_t required_packed_size = 0u;
    size_t packed_offset = 0u;
    size_t pixel_offset = 0u;
    size_t index;

    if (bmp == NULL || data == NULL)
        return fail(error, error_size, "BMP parser received null input");
    memset(&parsed, 0, sizeof(parsed));
    memset(bmp, 0, sizeof(*bmp));
    reader.data = data;
    reader.size = size;
    reader.offset = 0u;

    if (!expect_tag(&reader, "BMP:") ||
        !read_u16(&reader, &parsed.sheet_width) ||
        !read_u16(&reader, &parsed.sheet_height))
        return fail(error, error_size, "BMP resource header is truncated or invalid");
    if (parsed.sheet_width == 0u || parsed.sheet_height == 0u)
        return fail(error, error_size, "BMP sheet dimensions must be nonzero");
    if (!expect_tag(&reader, "INF:") ||
        !read_u32(&reader, &info_size) ||
        !read_u16(&reader, &image_count))
        return fail(error, error_size, "BMP INF chunk is truncated or invalid");
    (void)info_size; /* Original resources do not use this field as a boundary. */
    if (image_count == 0u || image_count > JC_BMP_MAX_IMAGES)
        return fail(error, error_size, "BMP image count is outside supported limits");

    parsed.image_count = image_count;
    parsed.images = (jc_bmp_image_t *)calloc(parsed.image_count,
                                               sizeof(*parsed.images));
    if (parsed.images == NULL)
        return fail(error, error_size, "could not allocate BMP image metadata");

    for (index = 0u; index < parsed.image_count; ++index) {
        if (!read_u16(&reader, &parsed.images[index].width)) {
            jc_bmp_free(&parsed);
            return fail(error, error_size, "BMP width table is truncated");
        }
    }
    for (index = 0u; index < parsed.image_count; ++index) {
        size_t pixel_count;
        size_t packed_count;
        if (!read_u16(&reader, &parsed.images[index].height)) {
            jc_bmp_free(&parsed);
            return fail(error, error_size, "BMP height table is truncated");
        }
        if (parsed.images[index].width == 0u ||
            parsed.images[index].height == 0u ||
            !multiply_size(parsed.images[index].width,
                           parsed.images[index].height, &pixel_count) ||
            pixel_count > JC_BMP_MAX_PIXELS - parsed.pixel_storage_size) {
            jc_bmp_free(&parsed);
            return fail(error, error_size, "BMP frame dimensions exceed supported limits");
        }
        parsed.images[index].pixel_count = pixel_count;
        parsed.pixel_storage_size += pixel_count;

        /* Each frame begins on a byte boundary in the DGDS sprite stream. */
        packed_count = pixel_count / 2u + pixel_count % 2u;
        if (!add_size(required_packed_size, packed_count,
                      &required_packed_size) ||
            required_packed_size > JC_BMP_MAX_PACKED_BYTES) {
            jc_bmp_free(&parsed);
            return fail(error, error_size, "BMP packed pixels exceed supported limits");
        }
    }

    if (!expect_tag(&reader, "BIN:") ||
        !read_u32(&reader, &packed_total) || packed_total < 5u ||
        !read_u8(&reader, &compression_method) ||
        !read_u32(&reader, &unpacked_size)) {
        jc_bmp_free(&parsed);
        return fail(error, error_size, "BMP BIN packed block is truncated or invalid");
    }
    if (unpacked_size < required_packed_size ||
        unpacked_size > JC_BMP_MAX_PACKED_BYTES) {
        jc_bmp_free(&parsed);
        return fail(error, error_size, "BMP unpacked pixel size is inconsistent with its frames");
    }
    if (!take(&reader, (size_t)packed_total - 5u, &compressed_data)) {
        jc_bmp_free(&parsed);
        return fail(error, error_size, "BMP compressed pixel block exceeds the resource body");
    }

    packed_pixels = (uint8_t *)malloc(unpacked_size);
    parsed.pixel_storage = (uint8_t *)malloc(parsed.pixel_storage_size);
    if (packed_pixels == NULL || parsed.pixel_storage == NULL) {
        free(packed_pixels);
        jc_bmp_free(&parsed);
        return fail(error, error_size, "could not allocate BMP pixel storage");
    }
    if (!jc_decompress(compression_method, compressed_data,
                       (size_t)packed_total - 5u, packed_pixels, unpacked_size,
                       error, error_size)) {
        free(packed_pixels);
        jc_bmp_free(&parsed);
        return false;
    }

    for (index = 0u; index < parsed.image_count; ++index) {
        size_t pixel;
        size_t packed_count = parsed.images[index].pixel_count / 2u +
                              parsed.images[index].pixel_count % 2u;
        parsed.images[index].pixels = parsed.pixel_storage + pixel_offset;
        for (pixel = 0u; pixel < parsed.images[index].pixel_count; ++pixel) {
            uint8_t packed = packed_pixels[packed_offset + pixel / 2u];
            parsed.images[index].pixels[pixel] =
                (pixel & 1u) == 0u ? (uint8_t)(packed >> 4) :
                                      (uint8_t)(packed & 0x0fu);
        }
        packed_offset += packed_count;
        pixel_offset += parsed.images[index].pixel_count;
    }
    free(packed_pixels);

    *bmp = parsed;
    if (error != NULL && error_size > 0u)
        error[0] = '\0';
    return true;
}

bool jc_bmp_image_surface(jc_bmp_t *bmp, size_t image_index,
                          jc_surface_t *surface)
{
    jc_bmp_image_t *image;
    if (bmp == NULL || surface == NULL || image_index >= bmp->image_count)
        return false;
    image = &bmp->images[image_index];
    return jc_surface_init(surface, image->pixels, image->pixel_count,
                           image->width, image->height, image->width);
}
