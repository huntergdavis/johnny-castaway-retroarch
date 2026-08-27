/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "jc_decompress.h"
#include "jc_scr.h"
#include "jc_surface.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct bit_writer {
    unsigned char bytes[2048];
    size_t bit_position;
} bit_writer_t;

static void write_bits(bit_writer_t *writer, unsigned value, unsigned count)
{
    unsigned index;
    for (index = 0u; index < count; ++index) {
        if ((value & (1u << index)) != 0u)
            writer->bytes[writer->bit_position / 8u] |=
                (unsigned char)(1u << (writer->bit_position % 8u));
        ++writer->bit_position;
    }
}

static void write_le16(unsigned char *data, unsigned value)
{
    data[0] = (unsigned char)value;
    data[1] = (unsigned char)(value >> 8);
}

static void write_le32(unsigned char *data, unsigned long value)
{
    data[0] = (unsigned char)value;
    data[1] = (unsigned char)(value >> 8);
    data[2] = (unsigned char)(value >> 16);
    data[3] = (unsigned char)(value >> 24);
}

static void test_surface(void)
{
    unsigned char destination_pixels[20];
    unsigned char source_pixels[4] = {1, 0, 2, 3};
    jc_surface_t destination;
    jc_surface_t source;

    assert(jc_surface_init(&destination, destination_pixels,
                           sizeof(destination_pixels), 5, 4, 5));
    assert(jc_surface_init(&source, source_pixels, sizeof(source_pixels), 2, 2, 2));
    jc_surface_clear(&destination, 9);
    jc_surface_set_clip(&destination, 1, 1, 3, 2);
    jc_surface_fill_rect(&destination, -2, -2, 5, 5, 4);
    assert(destination_pixels[0] == 9);
    assert(destination_pixels[6] == 4);
    jc_surface_blit(&destination, 2, 1, &source, 0, false);
    assert(destination_pixels[7] == 1);
    assert(destination_pixels[8] == 9);
    assert(destination_pixels[12] == 2);
    assert(destination_pixels[13] == 3);
    jc_surface_blit(&destination, 1, 1, &source, 0, true);
    assert(destination_pixels[6] == 4);
    assert(destination_pixels[7] == 1);
}

static void test_decompress(void)
{
    const unsigned char rle[] = {2, 10, 11, 0x83, 7};
    unsigned char output[1024];
    unsigned char expected[1024];
    bit_writer_t writer;
    char error[128];
    size_t index;

    assert(jc_decompress(JC_COMPRESSION_RLE, rle, sizeof(rle), output, 5,
                         error, sizeof(error)));
    assert(memcmp(output, (unsigned char[]){10, 11, 7, 7, 7}, 5) == 0);
    assert(!jc_decompress(JC_COMPRESSION_RLE, rle, 2, output, 5,
                          error, sizeof(error)));

    memset(&writer, 0, sizeof(writer));
    write_bits(&writer, 'A', 9);
    write_bits(&writer, 'B', 9);
    write_bits(&writer, 257, 9);
    write_bits(&writer, 259, 9);
    assert(jc_decompress(JC_COMPRESSION_LZW, writer.bytes,
                         (writer.bit_position + 7u) / 8u, output, 7,
                         error, sizeof(error)));
    assert(memcmp(output, "ABABABA", 7) == 0);

    memset(&writer, 0, sizeof(writer));
    for (index = 0u; index < sizeof(expected); ++index) {
        unsigned width = index <= 255u ? 9u : (index <= 767u ? 10u : 11u);
        expected[index] = (unsigned char)index;
        write_bits(&writer, expected[index], width);
    }
    assert(jc_decompress(JC_COMPRESSION_LZW, writer.bytes,
                         (writer.bit_position + 7u) / 8u, output,
                         sizeof(output), error, sizeof(error)));
    assert(memcmp(output, expected, sizeof(output)) == 0);
}

static void test_scr(void)
{
    unsigned char resource[35] = {0};
    unsigned char pixels[4];
    jc_surface_t surface;
    char error[128];

    memcpy(resource, "SCR:", 4);
    memcpy(resource + 8, "DIM:", 4);
    write_le32(resource + 12, 4);
    write_le16(resource + 16, 2);
    write_le16(resource + 18, 2);
    memcpy(resource + 20, "BIN:", 4);
    write_le32(resource + 24, 7);
    resource[28] = JC_COMPRESSION_NONE;
    write_le32(resource + 29, 2);
    resource[33] = 0x12;
    resource[34] = 0x34;

    assert(jc_scr_decode(resource, sizeof(resource), pixels, sizeof(pixels),
                         &surface, error, sizeof(error)));
    assert(surface.width == 2 && surface.height == 2);
    assert(memcmp(pixels, (unsigned char[]){1, 2, 3, 4}, 4) == 0);
    resource[0] = 'X';
    assert(!jc_scr_decode(resource, sizeof(resource), pixels, sizeof(pixels),
                          &surface, error, sizeof(error)));
}

int main(void)
{
    test_surface();
    test_decompress();
    test_scr();
    puts("graphics/decompression tests passed");
    return 0;
}
