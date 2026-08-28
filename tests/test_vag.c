/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "jc_vag.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void put_u32be(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static void make_vag(uint8_t *vag, size_t blocks)
{
    memset(vag, 0, JC_VAG_HEADER_SIZE + blocks * JC_VAG_BLOCK_SIZE);
    memcpy(vag, "VAGp", 4u);
    put_u32be(vag + 4u, 0x20u);
    put_u32be(vag + 12u, (uint32_t)(blocks * JC_VAG_BLOCK_SIZE));
    put_u32be(vag + 16u, 11025u);
}

static void test_nibbles_and_metadata(void)
{
    uint8_t vag[JC_VAG_HEADER_SIZE + 2u * JC_VAG_BLOCK_SIZE];
    uint8_t output[2u * JC_VAG_SAMPLES_PER_BLOCK];
    jc_vag_info_t info;
    size_t i;

    make_vag(vag, 2u);
    vag[JC_VAG_HEADER_SIZE + 1u] = 6u;
    vag[JC_VAG_HEADER_SIZE + 2u] = 0x7fu;
    vag[JC_VAG_HEADER_SIZE + JC_VAG_BLOCK_SIZE] = 0u;
    vag[JC_VAG_HEADER_SIZE + JC_VAG_BLOCK_SIZE + 1u] = 3u;

    assert(jc_vag_probe(vag, sizeof(vag), &info) == JC_VAG_OK);
    assert(info.sample_rate == 11025u);
    assert(info.sample_count == sizeof(output));
    assert(info.has_loop);
    assert(info.loop_start == 0u);
    assert(info.loop_end == sizeof(output));
    assert(jc_vag_decode_u8(vag, sizeof(vag), output, sizeof(output),
                            &info) == JC_VAG_OK);
    assert(output[0] == 112u); /* low nibble f = -1 -> -4096 */
    assert(output[1] == 240u); /* high nibble 7 = +7 -> +28672 */
    for (i = 2u; i < sizeof(output); ++i)
        assert(output[i] == 128u);
}

static void test_predictor_and_clipping(void)
{
    uint8_t vag[JC_VAG_HEADER_SIZE + JC_VAG_BLOCK_SIZE];
    uint8_t output[JC_VAG_SAMPLES_PER_BLOCK];
    jc_vag_info_t info;

    make_vag(vag, 1u);
    vag[JC_VAG_HEADER_SIZE] = 0x10u; /* predictor 1, shift 0 */
    vag[JC_VAG_HEADER_SIZE + 2u] = 0x77u;
    assert(jc_vag_decode_u8(vag, sizeof(vag), output, sizeof(output),
                            &info) == JC_VAG_OK);
    assert(output[0] == 240u);
    assert(output[1] == 255u);
    assert(output[2] == 247u); /* 32767 * 60 / 64 decays to 30719 */
}

static void test_failures(void)
{
    uint8_t vag[JC_VAG_HEADER_SIZE + JC_VAG_BLOCK_SIZE];
    uint8_t output[JC_VAG_SAMPLES_PER_BLOCK];
    jc_vag_info_t info;

    make_vag(vag, 1u);
    assert(jc_vag_probe(NULL, sizeof(vag), &info) ==
           JC_VAG_ERR_INVALID_ARGUMENT);
    assert(jc_vag_probe(vag, JC_VAG_HEADER_SIZE - 1u, &info) ==
           JC_VAG_ERR_TRUNCATED);
    vag[0] = 'X';
    assert(jc_vag_probe(vag, sizeof(vag), &info) == JC_VAG_ERR_BAD_MAGIC);
    vag[0] = 'V';
    put_u32be(vag + 12u, 15u);
    assert(jc_vag_probe(vag, sizeof(vag), &info) == JC_VAG_ERR_BAD_SIZE);
    put_u32be(vag + 12u, JC_VAG_BLOCK_SIZE);
    put_u32be(vag + 16u, 22050u);
    assert(jc_vag_probe(vag, sizeof(vag), &info) ==
           JC_VAG_ERR_UNSUPPORTED_RATE);
    put_u32be(vag + 16u, 11025u);
    vag[JC_VAG_HEADER_SIZE] = 0x50u;
    assert(jc_vag_probe(vag, sizeof(vag), &info) == JC_VAG_ERR_BAD_BLOCK);
    vag[JC_VAG_HEADER_SIZE] = 13u;
    assert(jc_vag_probe(vag, sizeof(vag), &info) == JC_VAG_ERR_BAD_BLOCK);
    vag[JC_VAG_HEADER_SIZE] = 0u;
    vag[JC_VAG_HEADER_SIZE + 1u] = 4u;
    assert(jc_vag_probe(vag, sizeof(vag), &info) == JC_VAG_ERR_BAD_BLOCK);
    vag[JC_VAG_HEADER_SIZE + 1u] = 0u;
    assert(jc_vag_decode_u8(vag, sizeof(vag), output, sizeof(output) - 1u,
                            &info) == JC_VAG_ERR_OUTPUT_TOO_SMALL);
}

int main(void)
{
    test_nibbles_and_metadata();
    test_predictor_and_clipping();
    test_failures();
    puts("vag tests passed");
    return 0;
}
