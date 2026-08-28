/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Portable mixer independently written for libretro. Eight deterministic
 * round-robin voices follow the channel model in Hunter Davis's
 * sound_ps1.c (GPL-3.0, commit 1f97b08) from:
 * https://github.com/huntergdavis/jc_reborn
 * Sample storage, device APIs, and backend code are not copied.
 */
#include "jc_audio.h"

#include <limits.h>
#include <string.h>

#define JC_AUDIO_STATE_MAGIC 0x5541434au
#define JC_AUDIO_STATE_VERSION 1u
#define JC_AUDIO_STATE_HEADER_SIZE 12u
#define JC_AUDIO_STATE_VOICE_SIZE 8u
#define JC_AUDIO_STATE_SIZE \
    (JC_AUDIO_STATE_HEADER_SIZE + \
     JC_AUDIO_MAX_VOICES * JC_AUDIO_STATE_VOICE_SIZE)

static void write_u32le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static uint32_t read_u32le(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static void clear_voice(jc_audio_voice_t *voice)
{
    memset(voice, 0, sizeof(*voice));
}

void jc_audio_init(jc_audio_t *audio)
{
    if (audio == NULL)
        return;
    memset(audio, 0, sizeof(*audio));
    audio->volume = JC_AUDIO_VOLUME_MAX;
}

void jc_audio_stop_all(jc_audio_t *audio)
{
    unsigned voice_id;

    if (audio == NULL)
        return;
    for (voice_id = 0u; voice_id < JC_AUDIO_MAX_VOICES; ++voice_id)
        clear_voice(&audio->voices[voice_id]);
}

void jc_audio_reset(jc_audio_t *audio)
{
    if (audio == NULL)
        return;
    jc_audio_stop_all(audio);
    audio->next_voice = 0u;
}

void jc_audio_clear_samples(jc_audio_t *audio)
{
    if (audio == NULL)
        return;
    jc_audio_reset(audio);
    memset(audio->samples, 0, sizeof(audio->samples));
}

bool jc_audio_set_sample(jc_audio_t *audio, unsigned sample_id,
                         const jc_wav_pcm_t *pcm)
{
    return jc_audio_set_sample_ex(audio, sample_id, pcm, false,
                                  JC_AUDIO_VOLUME_MAX);
}

bool jc_audio_set_sample_ex(jc_audio_t *audio, unsigned sample_id,
                            const jc_wav_pcm_t *pcm, bool loop,
                            unsigned gain)
{
    if (audio == NULL || pcm == NULL || sample_id >= JC_AUDIO_SAMPLE_COUNT ||
        pcm->samples == NULL || pcm->sample_count == 0u ||
        pcm->sample_count > UINT32_MAX ||
        pcm->sample_rate != JC_WAV_SOURCE_RATE ||
        pcm->channels != JC_WAV_SOURCE_CHANNELS ||
        pcm->bits_per_sample != JC_WAV_SOURCE_BITS ||
        gain > JC_AUDIO_VOLUME_MAX)
        return false;

    jc_audio_stop_sample(audio, sample_id);
    audio->samples[sample_id].data = pcm->samples;
    audio->samples[sample_id].length = (uint32_t)pcm->sample_count;
    audio->samples[sample_id].gain = (uint8_t)gain;
    audio->samples[sample_id].loop = loop;
    return true;
}

jc_wav_status_t jc_audio_load_wav(jc_audio_t *audio, unsigned sample_id,
                                  const void *data, size_t size)
{
    return jc_audio_load_wav_ex(audio, sample_id, data, size, false,
                                JC_AUDIO_VOLUME_MAX);
}

jc_wav_status_t jc_audio_load_wav_ex(jc_audio_t *audio, unsigned sample_id,
                                     const void *data, size_t size, bool loop,
                                     unsigned gain)
{
    jc_wav_pcm_t pcm;
    jc_wav_status_t status;

    if (audio == NULL || sample_id >= JC_AUDIO_SAMPLE_COUNT ||
        gain > JC_AUDIO_VOLUME_MAX)
        return JC_WAV_ERR_INVALID_ARGUMENT;
    status = jc_wav_parse(data, size, &pcm);
    if (status != JC_WAV_OK)
        return status;
    if (!jc_audio_set_sample_ex(audio, sample_id, &pcm, loop, gain))
        return JC_WAV_ERR_UNSUPPORTED_FORMAT;
    return JC_WAV_OK;
}

bool jc_audio_has_sample(const jc_audio_t *audio, unsigned sample_id)
{
    return audio != NULL && sample_id < JC_AUDIO_SAMPLE_COUNT &&
           audio->samples[sample_id].data != NULL &&
           audio->samples[sample_id].length != 0u;
}

void jc_audio_unload_sample(jc_audio_t *audio, unsigned sample_id)
{
    if (audio == NULL || sample_id >= JC_AUDIO_SAMPLE_COUNT)
        return;
    jc_audio_stop_sample(audio, sample_id);
    memset(&audio->samples[sample_id], 0, sizeof(audio->samples[sample_id]));
}

int jc_audio_trigger(jc_audio_t *audio, unsigned sample_id)
{
    unsigned offset;
    unsigned selected;

    if (!jc_audio_has_sample(audio, sample_id))
        return JC_AUDIO_INVALID_VOICE;

    selected = audio->next_voice;
    for (offset = 0u; offset < JC_AUDIO_MAX_VOICES; ++offset) {
        unsigned candidate =
            (audio->next_voice + offset) % JC_AUDIO_MAX_VOICES;
        if (!audio->voices[candidate].active) {
            selected = candidate;
            break;
        }
    }

    audio->voices[selected].position = 0u;
    audio->voices[selected].source_phase = 0u;
    audio->voices[selected].sample_id = (uint8_t)sample_id;
    audio->voices[selected].active = true;
    audio->next_voice = (uint8_t)((selected + 1u) % JC_AUDIO_MAX_VOICES);
    return (int)selected;
}

bool jc_audio_stop_voice(jc_audio_t *audio, unsigned voice_id)
{
    if (audio == NULL || voice_id >= JC_AUDIO_MAX_VOICES ||
        !audio->voices[voice_id].active)
        return false;
    clear_voice(&audio->voices[voice_id]);
    return true;
}

size_t jc_audio_stop_sample(jc_audio_t *audio, unsigned sample_id)
{
    size_t stopped = 0u;
    unsigned voice_id;

    if (audio == NULL || sample_id >= JC_AUDIO_SAMPLE_COUNT)
        return 0u;
    for (voice_id = 0u; voice_id < JC_AUDIO_MAX_VOICES; ++voice_id) {
        jc_audio_voice_t *voice = &audio->voices[voice_id];
        if (voice->active && voice->sample_id == sample_id) {
            clear_voice(voice);
            ++stopped;
        }
    }
    return stopped;
}

size_t jc_audio_active_voice_count(const jc_audio_t *audio)
{
    size_t count = 0u;
    unsigned voice_id;

    if (audio == NULL)
        return 0u;
    for (voice_id = 0u; voice_id < JC_AUDIO_MAX_VOICES; ++voice_id) {
        if (audio->voices[voice_id].active)
            ++count;
    }
    return count;
}

void jc_audio_set_volume(jc_audio_t *audio, unsigned volume)
{
    if (audio == NULL)
        return;
    if (volume > JC_AUDIO_VOLUME_MAX)
        volume = JC_AUDIO_VOLUME_MAX;
    audio->volume = (uint8_t)volume;
}

unsigned jc_audio_get_volume(const jc_audio_t *audio)
{
    return audio != NULL ? audio->volume : 0u;
}

void jc_audio_set_muted(jc_audio_t *audio, bool muted)
{
    if (audio != NULL)
        audio->muted = muted;
}

bool jc_audio_is_muted(const jc_audio_t *audio)
{
    return audio != NULL && audio->muted;
}

static int16_t clamp_s16(int64_t value)
{
    if (value > INT16_MAX)
        return INT16_MAX;
    if (value < INT16_MIN)
        return INT16_MIN;
    return (int16_t)value;
}

size_t jc_audio_mix(jc_audio_t *audio, int16_t *stereo, size_t frames)
{
    size_t frame;

    if (audio == NULL || stereo == NULL || frames > SIZE_MAX / 2u)
        return 0u;

    for (frame = 0u; frame < frames; ++frame) {
        int32_t mixed = 0;
        int64_t scaled;
        int16_t output;
        unsigned voice_id;

        for (voice_id = 0u; voice_id < JC_AUDIO_MAX_VOICES; ++voice_id) {
            jc_audio_voice_t *voice = &audio->voices[voice_id];
            const jc_audio_sample_t *sample;

            if (!voice->active)
                continue;
            if (voice->sample_id >= JC_AUDIO_SAMPLE_COUNT ||
                voice->source_phase >= 4u) {
                clear_voice(voice);
                continue;
            }
            sample = &audio->samples[voice->sample_id];
            if (sample->data == NULL || voice->position >= sample->length) {
                clear_voice(voice);
                continue;
            }

            mixed += (((int32_t)sample->data[voice->position] - 128) * 256 *
                      (int32_t)sample->gain) /
                     (int32_t)JC_AUDIO_VOLUME_MAX;
            ++voice->source_phase;
            if (voice->source_phase == 4u) {
                voice->source_phase = 0u;
                ++voice->position;
                if (voice->position == sample->length) {
                    if (sample->loop)
                        voice->position = 0u;
                    else
                        clear_voice(voice);
                }
            }
        }

        scaled = audio->muted ? 0 :
            ((int64_t)mixed * (int64_t)audio->volume) /
                (int64_t)JC_AUDIO_VOLUME_MAX;
        output = clamp_s16(scaled);
        stereo[frame * 2u] = output;
        stereo[frame * 2u + 1u] = output;
    }
    return frames;
}

size_t jc_audio_serialize_size(void)
{
    return JC_AUDIO_STATE_SIZE;
}

bool jc_audio_serialize(const jc_audio_t *audio, void *data, size_t size)
{
    uint8_t *bytes = (uint8_t *)data;
    unsigned voice_id;

    if (audio == NULL || data == NULL || size < JC_AUDIO_STATE_SIZE)
        return false;
    if (audio->volume > JC_AUDIO_VOLUME_MAX ||
        audio->next_voice >= JC_AUDIO_MAX_VOICES)
        return false;
    memset(bytes, 0, JC_AUDIO_STATE_SIZE);
    write_u32le(bytes, JC_AUDIO_STATE_MAGIC);
    write_u32le(bytes + 4u, JC_AUDIO_STATE_VERSION);
    bytes[8] = audio->volume;
    bytes[9] = audio->muted ? 1u : 0u;
    bytes[10] = audio->next_voice;
    bytes[11] = JC_AUDIO_MAX_VOICES;

    for (voice_id = 0u; voice_id < JC_AUDIO_MAX_VOICES; ++voice_id) {
        const jc_audio_voice_t *voice = &audio->voices[voice_id];
        uint8_t *stored = bytes + JC_AUDIO_STATE_HEADER_SIZE +
            voice_id * JC_AUDIO_STATE_VOICE_SIZE;
        if (!voice->active)
            continue;
        if (voice->source_phase >= 4u ||
            voice->sample_id >= JC_AUDIO_SAMPLE_COUNT ||
            !jc_audio_has_sample(audio, voice->sample_id) ||
            voice->position >= audio->samples[voice->sample_id].length)
            return false;
        stored[0] = 1u;
        stored[1] = voice->source_phase;
        stored[2] = voice->sample_id;
        write_u32le(stored + 4u, voice->position);
    }
    return true;
}

bool jc_audio_unserialize(jc_audio_t *audio, const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    jc_audio_voice_t restored[JC_AUDIO_MAX_VOICES];
    unsigned voice_id;

    if (audio == NULL || data == NULL || size < JC_AUDIO_STATE_SIZE)
        return false;
    if (read_u32le(bytes) != JC_AUDIO_STATE_MAGIC ||
        read_u32le(bytes + 4u) != JC_AUDIO_STATE_VERSION ||
        bytes[8] > JC_AUDIO_VOLUME_MAX || bytes[9] > 1u ||
        bytes[10] >= JC_AUDIO_MAX_VOICES ||
        bytes[11] != JC_AUDIO_MAX_VOICES)
        return false;

    memset(restored, 0, sizeof(restored));
    for (voice_id = 0u; voice_id < JC_AUDIO_MAX_VOICES; ++voice_id) {
        const uint8_t *stored = bytes + JC_AUDIO_STATE_HEADER_SIZE +
            voice_id * JC_AUDIO_STATE_VOICE_SIZE;
        uint32_t position;
        unsigned sample_id;

        if (stored[0] > 1u || stored[3] != 0u)
            return false;
        if (stored[0] == 0u)
            continue;
        sample_id = stored[2];
        position = read_u32le(stored + 4u);
        if (stored[1] >= 4u || sample_id >= JC_AUDIO_SAMPLE_COUNT ||
            !jc_audio_has_sample(audio, sample_id) ||
            position >= audio->samples[sample_id].length)
            return false;
        restored[voice_id].active = true;
        restored[voice_id].source_phase = stored[1];
        restored[voice_id].sample_id = (uint8_t)sample_id;
        restored[voice_id].position = position;
    }

    memcpy(audio->voices, restored, sizeof(restored));
    audio->volume = bytes[8];
    audio->muted = bytes[9] != 0u;
    audio->next_voice = bytes[10];
    return true;
}
