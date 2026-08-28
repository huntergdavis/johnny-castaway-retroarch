/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "jc_bmp.h"
#include "jc_compositor.h"

#include <stdbool.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,           \
                    #condition);                                                \
            return false;                                                       \
        }                                                                       \
    } while (0)

typedef struct bytes {
    uint8_t data[512];
    size_t size;
} bytes_t;

static void append_u8(bytes_t *bytes, uint8_t value)
{
    bytes->data[bytes->size++] = value;
}

static void append_u16(bytes_t *bytes, uint16_t value)
{
    append_u8(bytes, (uint8_t)value);
    append_u8(bytes, (uint8_t)(value >> 8));
}

static void append_u32(bytes_t *bytes, uint32_t value)
{
    append_u8(bytes, (uint8_t)value);
    append_u8(bytes, (uint8_t)(value >> 8));
    append_u8(bytes, (uint8_t)(value >> 16));
    append_u8(bytes, (uint8_t)(value >> 24));
}

static void append_data(bytes_t *bytes, const void *data, size_t size)
{
    memcpy(bytes->data + bytes->size, data, size);
    bytes->size += size;
}

static bytes_t two_image_bmp(uint8_t method, const uint8_t *encoded,
                             size_t encoded_size, uint32_t unpacked_size)
{
    bytes_t bytes = {{0}, 0u};
    append_data(&bytes, "BMP:", 4u);
    append_u16(&bytes, 3u);
    append_u16(&bytes, 3u);
    append_data(&bytes, "INF:", 4u);
    append_u32(&bytes, 0u);
    append_u16(&bytes, 2u);
    append_u16(&bytes, 2u);
    append_u16(&bytes, 3u);
    append_u16(&bytes, 2u);
    append_u16(&bytes, 1u);
    append_data(&bytes, "BIN:", 4u);
    append_u32(&bytes, (uint32_t)encoded_size + 5u);
    append_u8(&bytes, method);
    append_u32(&bytes, unpacked_size);
    append_data(&bytes, encoded, encoded_size);
    return bytes;
}

static bool test_stored_parse_and_views(void)
{
    const uint8_t packed[] = {0x12u, 0x34u, 0xABu, 0xC0u};
    bytes_t bytes = two_image_bmp(0u, packed, sizeof(packed), sizeof(packed));
    const uint8_t first[] = {1u, 2u, 3u, 4u};
    const uint8_t second[] = {10u, 11u, 12u};
    jc_bmp_t bmp = {0};
    jc_surface_t surface;
    char error[128] = "not cleared";

    CHECK(jc_bmp_parse(&bmp, bytes.data, bytes.size, error, sizeof(error)));
    CHECK(error[0] == '\0');
    CHECK(bmp.sheet_width == 3u && bmp.sheet_height == 3u);
    CHECK(bmp.image_count == 2u && bmp.pixel_storage_size == 7u);
    CHECK(bmp.images[0].width == 2u && bmp.images[0].height == 2u);
    CHECK(memcmp(bmp.images[0].pixels, first, sizeof(first)) == 0);
    CHECK(bmp.images[1].width == 3u && bmp.images[1].height == 1u);
    CHECK(memcmp(bmp.images[1].pixels, second, sizeof(second)) == 0);
    CHECK(jc_bmp_image_surface(&bmp, 1u, &surface));
    CHECK(surface.width == 3u && surface.height == 1u && surface.pitch == 3u);
    CHECK(!jc_bmp_image_surface(&bmp, 2u, &surface));

    jc_bmp_free(&bmp);
    CHECK(bmp.images == NULL && bmp.pixel_storage == NULL && bmp.image_count == 0u);
    jc_bmp_free(&bmp);
    return true;
}

static bool test_rle_parse(void)
{
    /* Literal four packed bytes: 12 34 AB C0. */
    const uint8_t encoded[] = {0x04u, 0x12u, 0x34u, 0xABu, 0xC0u};
    bytes_t bytes = two_image_bmp(1u, encoded, sizeof(encoded), 4u);
    jc_bmp_t bmp = {0};
    char error[128];

    CHECK(jc_bmp_parse(&bmp, bytes.data, bytes.size, error, sizeof(error)));
    CHECK(bmp.images[1].pixels[0] == 10u);
    CHECK(bmp.images[1].pixels[1] == 11u);
    CHECK(bmp.images[1].pixels[2] == 12u);
    jc_bmp_free(&bmp);
    return true;
}

static bool parse_must_fail(bytes_t bytes, const char *error_fragment)
{
    jc_bmp_t bmp = {0};
    char error[160] = "";
    bool parsed = jc_bmp_parse(&bmp, bytes.data, bytes.size, error, sizeof(error));
    CHECK(!parsed);
    CHECK(strstr(error, error_fragment) != NULL);
    CHECK(bmp.images == NULL && bmp.pixel_storage == NULL);
    return true;
}

static bool test_parser_rejects_malformed_resources(void)
{
    const uint8_t packed[] = {0x12u, 0x34u, 0xABu, 0xC0u};
    bytes_t valid = two_image_bmp(0u, packed, sizeof(packed), sizeof(packed));
    bytes_t bad;
    size_t truncated_size;

    for (truncated_size = 0u; truncated_size < valid.size; ++truncated_size) {
        jc_bmp_t truncated = {0};
        char error[128];
        CHECK(!jc_bmp_parse(&truncated, valid.data, truncated_size,
                            error, sizeof(error)));
        CHECK(truncated.images == NULL && truncated.pixel_storage == NULL);
    }

    bad = valid;
    bad.data[0] = 'X';
    CHECK(parse_must_fail(bad, "header"));

    bad = valid;
    bad.size--;
    CHECK(parse_must_fail(bad, "exceeds"));

    bad = valid;
    /* packed total is immediately after BIN:, at byte offset 30. */
    bad.data[30] = 4u;
    bad.data[31] = bad.data[32] = bad.data[33] = 0u;
    CHECK(parse_must_fail(bad, "packed block"));

    bad = valid;
    /* Unpacked size starts at byte offset 35.  Three bytes cannot hold frames. */
    bad.data[35] = 3u;
    bad.data[36] = bad.data[37] = bad.data[38] = 0u;
    CHECK(parse_must_fail(bad, "inconsistent"));

    bad = valid;
    /* First width at byte 18: make a 65535x65535 frame. */
    bad.data[18] = bad.data[19] = 0xffu;
    /* First height at byte 22. */
    bad.data[22] = bad.data[23] = 0xffu;
    CHECK(parse_must_fail(bad, "dimensions"));

    bad = valid;
    /* Image count at byte 16: 257 exceeds the fixed metadata bound. */
    bad.data[16] = 1u;
    bad.data[17] = 1u;
    CHECK(parse_must_fail(bad, "count"));

    CHECK(!jc_bmp_parse(NULL, valid.data, valid.size, NULL, 0u));
    return true;
}

static bool test_transparent_flipped_clipped_blit(void)
{
    uint8_t destination_pixels[3u * 7u];
    uint8_t source_pixels[] = {1u, 255u, 2u, 3u, 4u, 5u};
    jc_surface_t destination;
    jc_surface_t source;
    size_t row;

    memset(destination_pixels, 0xEE, sizeof(destination_pixels));
    CHECK(jc_surface_init(&destination, destination_pixels,
                          sizeof(destination_pixels), 5u, 3u, 7u));
    CHECK(jc_surface_init(&source, source_pixels, sizeof(source_pixels),
                          3u, 2u, 3u));
    for (row = 0u; row < destination.height; ++row)
        memset(destination.pixels + row * destination.pitch, 0u,
               destination.width);
    jc_surface_set_clip(&destination, 1, 0, 3, 3);

    jc_surface_blit(&destination, 0, 1, &source, 255, true);
    CHECK(destination_pixels[0] == 0u && destination_pixels[1] == 0u);
    CHECK(destination_pixels[1u * 7u + 1u] == 0u); /* flipped transparent */
    CHECK(destination_pixels[1u * 7u + 2u] == 1u);
    CHECK(destination_pixels[2u * 7u + 1u] == 4u);
    CHECK(destination_pixels[2u * 7u + 2u] == 3u);
    CHECK(destination_pixels[5] == 0xEEu && destination_pixels[6] == 0xEEu);
    CHECK(destination_pixels[12] == 0xEEu && destination_pixels[13] == 0xEEu);
    return true;
}

static bool test_ordered_layer_composition(void)
{
    uint8_t output_pixels[8] = {0};
    uint8_t base_pixels[8] = {9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u};
    uint8_t first_pixels[4] = {1u, 255u, 2u, 3u};
    uint8_t top_pixels[2] = {7u, 255u};
    uint8_t hidden_pixels[1] = {8u};
    jc_surface_t output;
    jc_surface_t base;
    jc_surface_t first;
    jc_surface_t top;
    jc_surface_t hidden;
    jc_compositor_layer_t layers[3];
    const uint8_t expected[8] = {9u, 1u, 9u, 0u, 9u, 7u, 3u, 0u};

    CHECK(jc_surface_init(&output, output_pixels, sizeof(output_pixels),
                          4u, 2u, 4u));
    CHECK(jc_surface_init(&base, base_pixels, sizeof(base_pixels), 4u, 2u, 4u));
    CHECK(jc_surface_init(&first, first_pixels, sizeof(first_pixels), 2u, 2u, 2u));
    CHECK(jc_surface_init(&top, top_pixels, sizeof(top_pixels), 2u, 1u, 2u));
    CHECK(jc_surface_init(&hidden, hidden_pixels, sizeof(hidden_pixels), 1u, 1u, 1u));
    jc_surface_set_clip(&output, 0, 0, 3, 2);

    layers[0] = (jc_compositor_layer_t){&first, 1, 0, 255, false, true};
    /* Flip [7,T] to [T,7] at x=0, overwriting first's bottom-left pixel. */
    layers[1] = (jc_compositor_layer_t){&top, 0, 1, 255, true, true};
    layers[2] = (jc_compositor_layer_t){&hidden, 2, 0, -1, false, false};
    CHECK(jc_compositor_compose(&output, &base, layers, 3u));
    CHECK(memcmp(output_pixels, expected, sizeof(expected)) == 0);

    CHECK(jc_compositor_compose(&output, NULL, NULL, 0u));
    CHECK(!jc_compositor_compose(NULL, &base, layers, 3u));
    CHECK(!jc_compositor_compose(&output, &first, NULL, 0u));
    layers[0].transparent_index = 256;
    CHECK(!jc_compositor_compose(&output, NULL, layers, 1u));
    layers[0].transparent_index = 255;
    layers[0].x = INT_MAX;
    CHECK(!jc_compositor_compose(&output, NULL, layers, 1u));
    CHECK(!jc_compositor_compose(&output, NULL, layers,
                                 JC_COMPOSITOR_MAX_LAYERS + 1u));
    return true;
}

int main(void)
{
    unsigned passed = 0u;

    if (!test_stored_parse_and_views())
        return 1;
    ++passed;
    if (!test_rle_parse())
        return 1;
    ++passed;
    if (!test_parser_rejects_malformed_resources())
        return 1;
    ++passed;
    if (!test_transparent_flipped_clipped_blit())
        return 1;
    ++passed;
    if (!test_ordered_layer_composition())
        return 1;
    ++passed;

    printf("BMP/compositor tests passed: %u\n", passed);
    return 0;
}
