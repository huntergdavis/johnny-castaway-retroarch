/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The opt-in real-data test's sound-ID payload lengths are factual inventory
 * values verified by antigerme/wilson-reborn in
 * crates/wilson-dgds/src/exe_sound.rs (GPL-3.0, commit 2d302f5):
 * https://github.com/antigerme/wilson-reborn
 */
#include "jc_audio.h"
#include "jc_wav.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put_u16le(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void put_u32le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static size_t make_wav(uint8_t *wav, size_t capacity,
                       const uint8_t *samples, size_t sample_count,
                       int add_odd_junk)
{
    size_t position = 12u;

    assert(capacity >= 80u + sample_count);
    memcpy(wav, "RIFF", 4u);
    memcpy(wav + 8u, "WAVE", 4u);
    if (add_odd_junk) {
        memcpy(wav + position, "JUNK", 4u);
        put_u32le(wav + position + 4u, 3u);
        wav[position + 8u] = 1u;
        wav[position + 9u] = 2u;
        wav[position + 10u] = 3u;
        wav[position + 11u] = 0u;
        position += 12u;
    }
    memcpy(wav + position, "fmt ", 4u);
    put_u32le(wav + position + 4u, 16u);
    put_u16le(wav + position + 8u, 1u);
    put_u16le(wav + position + 10u, 1u);
    put_u32le(wav + position + 12u, 11025u);
    put_u32le(wav + position + 16u, 11025u);
    put_u16le(wav + position + 20u, 1u);
    put_u16le(wav + position + 22u, 8u);
    position += 24u;
    memcpy(wav + position, "data", 4u);
    put_u32le(wav + position + 4u, (uint32_t)sample_count);
    memcpy(wav + position + 8u, samples, sample_count);
    position += 8u + sample_count;
    if ((sample_count & 1u) != 0u)
        wav[position++] = 0u;
    put_u32le(wav + 4u, (uint32_t)(position - 8u));
    return position;
}

static void test_wav_parser(void)
{
    static const uint8_t samples[] = { 0u, 128u, 255u };
    uint8_t wav[128];
    uint8_t bad[128];
    jc_wav_pcm_t pcm;
    size_t size = make_wav(wav, sizeof(wav), samples, sizeof(samples), 1);
    size_t truncated;

    assert(jc_wav_parse(wav, size, &pcm) == JC_WAV_OK);
    assert(pcm.sample_count == sizeof(samples));
    assert(memcmp(pcm.samples, samples, sizeof(samples)) == 0);
    assert(pcm.sample_rate == 11025u && pcm.channels == 1u &&
           pcm.bits_per_sample == 8u);
    assert(strcmp(jc_wav_status_string(JC_WAV_OK), "ok") == 0);
    assert(jc_wav_parse(NULL, 0u, &pcm) == JC_WAV_ERR_INVALID_ARGUMENT);
    assert(jc_wav_parse(wav, 11u, &pcm) == JC_WAV_ERR_TRUNCATED);
    for (truncated = 0u; truncated < size; ++truncated)
        assert(jc_wav_parse(wav, truncated, &pcm) != JC_WAV_OK);

    memcpy(bad, wav, size);
    memcpy(bad, "NOPE", 4u);
    assert(jc_wav_parse(bad, size, &pcm) == JC_WAV_ERR_NOT_WAVE);

    memcpy(bad, wav, size);
    put_u32le(bad + 4u, (uint32_t)size);
    assert(jc_wav_parse(bad, size, &pcm) == JC_WAV_ERR_TRUNCATED);

    memcpy(bad, wav, size);
    /* fmt starts after the 12-byte RIFF header and 12-byte JUNK chunk. */
    put_u16le(bad + 12u + 12u + 10u, 2u);
    assert(jc_wav_parse(bad, size, &pcm) ==
           JC_WAV_ERR_UNSUPPORTED_FORMAT);

    memcpy(bad, wav, size);
    put_u32le(bad + 12u + 4u, UINT32_MAX);
    assert(jc_wav_parse(bad, size, &pcm) == JC_WAV_ERR_TRUNCATED);

    memcpy(bad, wav, size);
    memcpy(bad + 24u, "NOPE", 4u);
    assert(jc_wav_parse(bad, size, &pcm) == JC_WAV_ERR_MISSING_FMT);

    memcpy(bad, wav, size);
    memcpy(bad + 48u, "NOPE", 4u);
    assert(jc_wav_parse(bad, size, &pcm) == JC_WAV_ERR_MISSING_DATA);

    size = make_wav(wav, sizeof(wav), samples, 0u, 0);
    assert(jc_wav_parse(wav, size, &pcm) == JC_WAV_ERR_EMPTY_DATA);
}

static void assert_frame(const int16_t *frames, size_t frame, int16_t value)
{
    assert(frames[frame * 2u] == value);
    assert(frames[frame * 2u + 1u] == value);
}

static void test_resampling_and_batch_determinism(void)
{
    static const uint8_t samples[] = { 128u, 255u, 0u };
    uint8_t wav[128];
    int16_t whole[24];
    int16_t split[24];
    jc_audio_t first;
    jc_audio_t second;
    size_t size = make_wav(wav, sizeof(wav), samples, sizeof(samples), 0);
    size_t frame;

    jc_audio_init(&first);
    jc_audio_init(&second);
    assert(jc_audio_load_wav(&first, 0u, wav, size) == JC_WAV_OK);
    assert(jc_audio_load_wav(&second, 0u, wav, size) == JC_WAV_OK);
    assert(jc_audio_trigger(&first, 0u) == 0);
    assert(jc_audio_trigger(&second, 0u) == 0);
    assert(jc_audio_mix(&first, whole, 12u) == 12u);
    assert(jc_audio_mix(&second, split, 2u) == 2u);
    assert(jc_audio_mix(&second, split + 4u, 5u) == 5u);
    assert(jc_audio_mix(&second, split + 14u, 5u) == 5u);
    assert(memcmp(whole, split, sizeof(whole)) == 0);
    for (frame = 0u; frame < 4u; ++frame)
        assert_frame(whole, frame, 0);
    for (; frame < 8u; ++frame)
        assert_frame(whole, frame, 32512);
    for (; frame < 12u; ++frame)
        assert_frame(whole, frame, INT16_MIN);
    assert(jc_audio_active_voice_count(&first) == 0u);
}

static void test_voices_volume_mute_and_stop(void)
{
    static const uint8_t high[] = { 255u, 0u, 128u, 64u };
    uint8_t wav[128];
    int16_t output[16];
    jc_audio_t audio;
    size_t size = make_wav(wav, sizeof(wav), high, sizeof(high), 0);
    unsigned voice;

    jc_audio_init(&audio);
    assert(jc_audio_load_wav(&audio, 1u, wav, size) == JC_WAV_OK);
    assert(jc_audio_load_wav(&audio, 2u, wav, size) == JC_WAV_OK);
    assert(jc_audio_trigger(&audio, 1u) == 0);
    assert(jc_audio_trigger(&audio, 2u) == 1);
    jc_audio_mix(&audio, output, 1u);
    assert_frame(output, 0u, INT16_MAX);
    jc_audio_mix(&audio, output, 3u);
    jc_audio_mix(&audio, output, 1u);
    assert_frame(output, 0u, INT16_MIN);

    jc_audio_stop_all(&audio);
    jc_audio_set_volume(&audio, 50u);
    assert(jc_audio_trigger(&audio, 1u) == 2);
    jc_audio_mix(&audio, output, 1u);
    assert_frame(output, 0u, 16256);

    jc_audio_stop_all(&audio);
    jc_audio_set_muted(&audio, true);
    assert(jc_audio_trigger(&audio, 1u) == 3);
    jc_audio_mix(&audio, output, 4u);
    assert_frame(output, 0u, 0);
    assert_frame(output, 3u, 0);
    jc_audio_set_muted(&audio, false);
    jc_audio_mix(&audio, output, 1u);
    assert_frame(output, 0u, -16384);

    jc_audio_reset(&audio);
    assert(jc_audio_get_volume(&audio) == 50u);
    assert(jc_audio_has_sample(&audio, 1u));
    for (voice = 0u; voice < JC_AUDIO_MAX_VOICES; ++voice)
        assert(jc_audio_trigger(&audio, 1u) == (int)voice);
    assert(jc_audio_active_voice_count(&audio) == JC_AUDIO_MAX_VOICES);
    assert(jc_audio_trigger(&audio, 1u) == 0);
    assert(jc_audio_stop_voice(&audio, 0u));
    assert(!jc_audio_stop_voice(&audio, 0u));
    assert(jc_audio_stop_sample(&audio, 1u) == JC_AUDIO_MAX_VOICES - 1u);
    jc_audio_unload_sample(&audio, 1u);
    assert(!jc_audio_has_sample(&audio, 1u));
    assert(jc_audio_trigger(&audio, 1u) == JC_AUDIO_INVALID_VOICE);

    jc_audio_set_volume(&audio, 999u);
    assert(jc_audio_get_volume(&audio) == JC_AUDIO_VOLUME_MAX);
    assert(jc_audio_mix(NULL, output, 1u) == 0u);
    assert(jc_audio_mix(&audio, NULL, 1u) == 0u);
}

static void test_serialization(void)
{
    static const uint8_t samples[] = { 128u, 255u, 0u, 64u };
    uint8_t wav[128];
    uint8_t state[128];
    uint8_t invalid[128];
    int16_t expected[32];
    int16_t restored[32];
    jc_audio_t first;
    jc_audio_t second;
    size_t size = make_wav(wav, sizeof(wav), samples, sizeof(samples), 0);
    size_t state_size = jc_audio_serialize_size();

    assert(state_size <= sizeof(state));
    jc_audio_init(&first);
    jc_audio_init(&second);
    assert(jc_audio_load_wav(&first, 0u, wav, size) == JC_WAV_OK);
    assert(jc_audio_load_wav(&second, 0u, wav, size) == JC_WAV_OK);
    jc_audio_set_volume(&first, 73u);
    assert(jc_audio_trigger(&first, 0u) == 0);
    jc_audio_mix(&first, expected, 3u);
    assert(jc_audio_serialize(&first, state, state_size));
    assert(jc_audio_unserialize(&second, state, state_size));
    jc_audio_mix(&first, expected, 16u);
    jc_audio_mix(&second, restored, 16u);
    assert(memcmp(expected, restored, sizeof(expected)) == 0);
    assert(jc_audio_get_volume(&second) == 73u);

    memcpy(invalid, state, state_size);
    invalid[0] ^= 0xffu;
    assert(!jc_audio_unserialize(&second, invalid, state_size));

    memcpy(invalid, state, state_size);
    invalid[12u + 2u] = 24u;
    assert(!jc_audio_unserialize(&second, invalid, state_size));

    jc_audio_init(&second);
    assert(!jc_audio_unserialize(&second, state, state_size));
}

static void test_real_wavs_if_requested(void)
{
    static const uint32_t expected_lengths[JC_AUDIO_SAMPLE_COUNT] = {
        10262u, 11072u, 1488u, 7392u, 4992u, 2816u, 15744u, 14976u,
        2304u, 3040u, 20224u, 0u, 5438u, 0u, 11328u, 2838u, 7604u,
        4253u, 13943u, 3288u, 7215u, 4838u, 1292u, 1515u, 9672u
    };
    const char *directory = getenv("JC_AUDIO_DATA_DIR");
    unsigned loaded = 0u;
    unsigned sample_id;

    if (directory == NULL || directory[0] == '\0')
        return;

    for (sample_id = 0u; sample_id < JC_AUDIO_SAMPLE_COUNT; ++sample_id) {
        char path[1024];
        long length;
        uint8_t *bytes;
        jc_wav_pcm_t pcm;
        FILE *file;

        if (expected_lengths[sample_id] == 0u)
            continue;
        assert(snprintf(path, sizeof(path), "%s/sound%u.wav", directory,
                        sample_id) > 0);
        file = fopen(path, "rb");
        assert(file != NULL);
        assert(fseek(file, 0L, SEEK_END) == 0);
        length = ftell(file);
        assert(length > 0L);
        assert(fseek(file, 0L, SEEK_SET) == 0);
        bytes = (uint8_t *)malloc((size_t)length);
        assert(bytes != NULL);
        assert(fread(bytes, 1u, (size_t)length, file) == (size_t)length);
        assert(fclose(file) == 0);
        assert(jc_wav_parse(bytes, (size_t)length, &pcm) == JC_WAV_OK);
        assert(pcm.sample_count == expected_lengths[sample_id]);
        free(bytes);
        ++loaded;
    }
    assert(loaded == 23u);
    puts("validated 23 external original-format WAVs");
}

int main(void)
{
    test_wav_parser();
    test_resampling_and_batch_determinism();
    test_voices_volume_mute_and_stop();
    test_serialization();
    test_real_wavs_if_requested();
    puts("audio tests passed");
    return 0;
}
