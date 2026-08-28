/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Independently written bounded RIFF parser. Format facts and word-aligned
 * chunk walking were checked against antigerme/wilson-reborn's
 * crates/wilson-dgds/src/exe_sound.rs (GPL-3.0, commit 2d302f5):
 * https://github.com/antigerme/wilson-reborn
 * No upstream implementation code is copied here.
 */
#include "jc_wav.h"

#include <stdbool.h>
#include <string.h>

static uint16_t read_u16le(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t read_u32le(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

jc_wav_status_t jc_wav_parse(const void *data, size_t size,
                             jc_wav_pcm_t *pcm)
{
    const uint8_t *bytes = (const uint8_t *)data;
    const uint8_t *sample_data = NULL;
    size_t sample_count = 0u;
    size_t riff_end;
    size_t position;
    uint32_t sample_rate = 0u;
    uint32_t byte_rate = 0u;
    uint16_t audio_format = 0u;
    uint16_t channels = 0u;
    uint16_t block_align = 0u;
    uint16_t bits_per_sample = 0u;
    bool have_fmt = false;
    bool have_data = false;

    if (pcm == NULL || data == NULL)
        return JC_WAV_ERR_INVALID_ARGUMENT;
    memset(pcm, 0, sizeof(*pcm));

    if (size < 12u)
        return JC_WAV_ERR_TRUNCATED;
    if (memcmp(bytes, "RIFF", 4u) != 0 ||
        memcmp(bytes + 8u, "WAVE", 4u) != 0)
        return JC_WAV_ERR_NOT_WAVE;

    if (read_u32le(bytes + 4u) < 4u)
        return JC_WAV_ERR_MALFORMED;
    riff_end = (size_t)read_u32le(bytes + 4u) + 8u;
    if (riff_end < 12u)
        return JC_WAV_ERR_MALFORMED;
    if (riff_end > size)
        return JC_WAV_ERR_TRUNCATED;

    position = 12u;
    while (position < riff_end) {
        uint32_t raw_chunk_size;
        size_t chunk_size;
        size_t chunk_data;
        size_t next;

        if (riff_end - position < 8u)
            return JC_WAV_ERR_MALFORMED;
        raw_chunk_size = read_u32le(bytes + position + 4u);
        chunk_size = (size_t)raw_chunk_size;
        chunk_data = position + 8u;
        if (chunk_size > riff_end - chunk_data)
            return JC_WAV_ERR_TRUNCATED;
        next = chunk_data + chunk_size;

        if (memcmp(bytes + position, "fmt ", 4u) == 0 && !have_fmt) {
            if (chunk_size < 16u)
                return JC_WAV_ERR_MALFORMED;
            audio_format = read_u16le(bytes + chunk_data);
            channels = read_u16le(bytes + chunk_data + 2u);
            sample_rate = read_u32le(bytes + chunk_data + 4u);
            byte_rate = read_u32le(bytes + chunk_data + 8u);
            block_align = read_u16le(bytes + chunk_data + 12u);
            bits_per_sample = read_u16le(bytes + chunk_data + 14u);
            have_fmt = true;
        } else if (memcmp(bytes + position, "data", 4u) == 0 && !have_data) {
            sample_data = bytes + chunk_data;
            sample_count = chunk_size;
            have_data = true;
        }

        if ((raw_chunk_size & 1u) != 0u) {
            if (next == riff_end)
                return JC_WAV_ERR_TRUNCATED;
            ++next;
        }
        position = next;
    }

    if (!have_fmt)
        return JC_WAV_ERR_MISSING_FMT;
    if (!have_data)
        return JC_WAV_ERR_MISSING_DATA;
    if (audio_format != 1u || channels != JC_WAV_SOURCE_CHANNELS ||
        sample_rate != JC_WAV_SOURCE_RATE ||
        bits_per_sample != JC_WAV_SOURCE_BITS || block_align != 1u ||
        byte_rate != JC_WAV_SOURCE_RATE)
        return JC_WAV_ERR_UNSUPPORTED_FORMAT;
    if (sample_count == 0u)
        return JC_WAV_ERR_EMPTY_DATA;

    pcm->samples = sample_data;
    pcm->sample_count = sample_count;
    pcm->sample_rate = sample_rate;
    pcm->channels = channels;
    pcm->bits_per_sample = bits_per_sample;
    return JC_WAV_OK;
}

const char *jc_wav_status_string(jc_wav_status_t status)
{
    switch (status) {
    case JC_WAV_OK:
        return "ok";
    case JC_WAV_ERR_INVALID_ARGUMENT:
        return "invalid argument";
    case JC_WAV_ERR_TRUNCATED:
        return "truncated RIFF/WAVE data";
    case JC_WAV_ERR_NOT_WAVE:
        return "not a RIFF/WAVE file";
    case JC_WAV_ERR_MALFORMED:
        return "malformed RIFF/WAVE data";
    case JC_WAV_ERR_MISSING_FMT:
        return "missing fmt chunk";
    case JC_WAV_ERR_MISSING_DATA:
        return "missing data chunk";
    case JC_WAV_ERR_UNSUPPORTED_FORMAT:
        return "unsupported WAV format";
    case JC_WAV_ERR_EMPTY_DATA:
        return "empty WAV data chunk";
    default:
        return "unknown WAV error";
    }
}
