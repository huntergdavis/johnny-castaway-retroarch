/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Portable PS1 SPU ADPCM decoder. The block interpretation follows Sony's
 * VAG layout as documented by PSX-SPX and is independently implemented here.
 * The exact high-predictor/low-shift and low-nibble-first layout was verified
 * against Hunter Davis's GPL-3.0 scripts/wav2vag.py at PS1 revision
 * 25c5d84593ac20cbee354eaab7779ab7397d6bbe.
 */
#include "jc_vag.h"

#include <limits.h>
#include <string.h>

#define JC_VAG_MAX_DATA_SIZE (64u * 1024u * 1024u)
#define JC_VAG_SOURCE_RATE 11025u

static const int predictor_positive[5] = { 0, 60, 115, 98, 122 };
static const int predictor_negative[5] = { 0, 0, -52, -55, -60 };

static uint32_t read_u32be(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           (uint32_t)bytes[3];
}

static jc_vag_status_t inspect(const uint8_t *bytes, size_t size,
                               jc_vag_info_t *info)
{
    uint32_t data_size;
    size_t block_count;
    size_t block_index;
    bool saw_loop_start = false;
    bool saw_loop_end = false;
    jc_vag_info_t parsed;

    if (bytes == NULL || info == NULL)
        return JC_VAG_ERR_INVALID_ARGUMENT;
    if (size < JC_VAG_HEADER_SIZE)
        return JC_VAG_ERR_TRUNCATED;
    if (memcmp(bytes, "VAGp", 4u) != 0)
        return JC_VAG_ERR_BAD_MAGIC;

    data_size = read_u32be(bytes + 12u);
    if (data_size == 0u || data_size > JC_VAG_MAX_DATA_SIZE ||
        data_size % JC_VAG_BLOCK_SIZE != 0u ||
        (size_t)data_size > size - JC_VAG_HEADER_SIZE)
        return JC_VAG_ERR_BAD_SIZE;
    if (read_u32be(bytes + 16u) != JC_VAG_SOURCE_RATE)
        return JC_VAG_ERR_UNSUPPORTED_RATE;

    block_count = (size_t)data_size / JC_VAG_BLOCK_SIZE;
    if (block_count > SIZE_MAX / JC_VAG_SAMPLES_PER_BLOCK)
        return JC_VAG_ERR_BAD_SIZE;

    memset(&parsed, 0, sizeof(parsed));
    parsed.sample_rate = JC_VAG_SOURCE_RATE;
    parsed.sample_count = block_count * JC_VAG_SAMPLES_PER_BLOCK;
    for (block_index = 0u; block_index < block_count; ++block_index) {
        const uint8_t *block = bytes + JC_VAG_HEADER_SIZE +
                               block_index * JC_VAG_BLOCK_SIZE;
        unsigned predictor = block[0] >> 4;
        unsigned shift = block[0] & 15u;
        unsigned flags = block[1];

        if (predictor >= 5u || shift > 12u || (flags & ~7u) != 0u)
            return JC_VAG_ERR_BAD_BLOCK;
        if ((flags & 4u) != 0u) {
            if (saw_loop_start)
                return JC_VAG_ERR_BAD_BLOCK;
            parsed.loop_start = block_index * JC_VAG_SAMPLES_PER_BLOCK;
            saw_loop_start = true;
        }
        if ((flags & 3u) == 3u) {
            if (!saw_loop_start || saw_loop_end)
                return JC_VAG_ERR_BAD_BLOCK;
            parsed.loop_end = (block_index + 1u) *
                              JC_VAG_SAMPLES_PER_BLOCK;
            saw_loop_end = true;
        }
    }
    if (saw_loop_start != saw_loop_end)
        return JC_VAG_ERR_BAD_BLOCK;
    parsed.has_loop = saw_loop_start;
    *info = parsed;
    return JC_VAG_OK;
}

jc_vag_status_t jc_vag_probe(const void *data, size_t size,
                             jc_vag_info_t *info)
{
    return inspect((const uint8_t *)data, size, info);
}

static int32_t floor_div64(int32_t value)
{
    if (value >= 0)
        return value / 64;
    return -(((-value) + 63) / 64);
}

static int16_t clamp_s16(int32_t value)
{
    if (value > INT16_MAX)
        return INT16_MAX;
    if (value < INT16_MIN)
        return INT16_MIN;
    return (int16_t)value;
}

jc_vag_status_t jc_vag_decode_u8(const void *data, size_t size,
                                 uint8_t *output, size_t output_size,
                                 jc_vag_info_t *info)
{
    const uint8_t *bytes = (const uint8_t *)data;
    jc_vag_info_t parsed;
    jc_vag_status_t status;
    int32_t previous = 0;
    int32_t previous2 = 0;
    size_t block_count;
    size_t block_index;
    size_t output_index = 0u;

    if (output == NULL || info == NULL)
        return JC_VAG_ERR_INVALID_ARGUMENT;
    status = inspect(bytes, size, &parsed);
    if (status != JC_VAG_OK)
        return status;
    if (output_size < parsed.sample_count)
        return JC_VAG_ERR_OUTPUT_TOO_SMALL;

    block_count = parsed.sample_count / JC_VAG_SAMPLES_PER_BLOCK;
    for (block_index = 0u; block_index < block_count; ++block_index) {
        const uint8_t *block = bytes + JC_VAG_HEADER_SIZE +
                               block_index * JC_VAG_BLOCK_SIZE;
        unsigned predictor = block[0] >> 4;
        unsigned shift = block[0] & 15u;
        size_t sample_index;

        for (sample_index = 0u;
             sample_index < JC_VAG_SAMPLES_PER_BLOCK;
             ++sample_index) {
            uint8_t packed = block[2u + sample_index / 2u];
            unsigned raw = (sample_index & 1u) != 0u ? packed >> 4 :
                                                       packed & 15u;
            int32_t nibble = raw >= 8u ? (int32_t)raw - 16 : (int32_t)raw;
            int32_t delta = (nibble * 4096) / (int32_t)(1u << shift);
            int32_t prediction = floor_div64(
                previous * predictor_positive[predictor] +
                previous2 * predictor_negative[predictor] + 32);
            int16_t sample = clamp_s16(delta + prediction);

            output[output_index++] = (uint8_t)(((int32_t)sample + 32768) >> 8);
            previous2 = previous;
            previous = sample;
        }
    }
    *info = parsed;
    return JC_VAG_OK;
}

const char *jc_vag_status_string(jc_vag_status_t status)
{
    switch (status) {
    case JC_VAG_OK:
        return "ok";
    case JC_VAG_ERR_INVALID_ARGUMENT:
        return "invalid argument";
    case JC_VAG_ERR_TRUNCATED:
        return "truncated VAG header";
    case JC_VAG_ERR_BAD_MAGIC:
        return "invalid VAG magic";
    case JC_VAG_ERR_BAD_SIZE:
        return "invalid VAG data size";
    case JC_VAG_ERR_UNSUPPORTED_RATE:
        return "unsupported VAG sample rate";
    case JC_VAG_ERR_BAD_BLOCK:
        return "invalid VAG ADPCM block";
    case JC_VAG_ERR_OUTPUT_TOO_SMALL:
        return "VAG output buffer too small";
    default:
        return "unknown VAG error";
    }
}
