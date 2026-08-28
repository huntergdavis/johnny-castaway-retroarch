/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef JC_AUDIO_H
#define JC_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "jc_wav.h"

#define JC_AUDIO_ORIGINAL_SAMPLE_COUNT 25u
#define JC_AUDIO_AMBIENCE_SAMPLE_ID 25u
#define JC_AUDIO_SAMPLE_COUNT 26u
#define JC_AUDIO_MAX_VOICES 8u
#define JC_AUDIO_OUTPUT_RATE 44100u
#define JC_AUDIO_OUTPUT_CHANNELS 2u
#define JC_AUDIO_VOLUME_MAX 100u
#define JC_AUDIO_INVALID_VOICE (-1)

typedef struct jc_audio_sample {
    const uint8_t *data;
    uint32_t length;
    uint8_t gain;
    bool loop;
} jc_audio_sample_t;

typedef struct jc_audio_voice {
    uint32_t position;
    uint8_t source_phase;
    uint8_t sample_id;
    bool active;
} jc_audio_voice_t;

/*
 * The mixer owns no sample memory. WAV buffers registered with it must remain
 * alive until they are unloaded or the mixer is no longer used.
 */
typedef struct jc_audio {
    jc_audio_sample_t samples[JC_AUDIO_SAMPLE_COUNT];
    jc_audio_voice_t voices[JC_AUDIO_MAX_VOICES];
    uint8_t volume;
    uint8_t next_voice;
    bool muted;
} jc_audio_t;

void jc_audio_init(jc_audio_t *audio);
void jc_audio_reset(jc_audio_t *audio);
void jc_audio_clear_samples(jc_audio_t *audio);

bool jc_audio_set_sample(jc_audio_t *audio, unsigned sample_id,
                         const jc_wav_pcm_t *pcm);
/* Extended registration used by ambience: gain is 0..100, loop wraps at EOF. */
bool jc_audio_set_sample_ex(jc_audio_t *audio, unsigned sample_id,
                            const jc_wav_pcm_t *pcm, bool loop,
                            unsigned gain);
jc_wav_status_t jc_audio_load_wav(jc_audio_t *audio, unsigned sample_id,
                                  const void *data, size_t size);
/* Parse and register a WAV with the same extended playback policy. */
jc_wav_status_t jc_audio_load_wav_ex(jc_audio_t *audio, unsigned sample_id,
                                     const void *data, size_t size, bool loop,
                                     unsigned gain);
bool jc_audio_has_sample(const jc_audio_t *audio, unsigned sample_id);
void jc_audio_unload_sample(jc_audio_t *audio, unsigned sample_id);

/* Returns the deterministic voice index, or JC_AUDIO_INVALID_VOICE. */
int jc_audio_trigger(jc_audio_t *audio, unsigned sample_id);
bool jc_audio_stop_voice(jc_audio_t *audio, unsigned voice_id);
size_t jc_audio_stop_sample(jc_audio_t *audio, unsigned sample_id);
void jc_audio_stop_all(jc_audio_t *audio);
size_t jc_audio_active_voice_count(const jc_audio_t *audio);

void jc_audio_set_volume(jc_audio_t *audio, unsigned volume);
unsigned jc_audio_get_volume(const jc_audio_t *audio);
void jc_audio_set_muted(jc_audio_t *audio, bool muted);
bool jc_audio_is_muted(const jc_audio_t *audio);

/*
 * Produce interleaved signed 16-bit stereo frames at 44100 Hz. The original
 * 11025 Hz samples are deterministically held for four output frames. Mixing
 * performs no allocation, I/O, locking, or device access.
 */
size_t jc_audio_mix(jc_audio_t *audio, int16_t *stereo, size_t frames);

/*
 * Only mutable playback/configuration state is serialized; sample pointers are
 * deliberately excluded. Register the same sample IDs before unserializing.
 */
size_t jc_audio_serialize_size(void);
bool jc_audio_serialize(const jc_audio_t *audio, void *data, size_t size);
bool jc_audio_unserialize(jc_audio_t *audio, const void *data, size_t size);

#endif
