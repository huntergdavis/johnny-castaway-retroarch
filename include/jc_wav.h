/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef JC_WAV_H
#define JC_WAV_H

#include <stddef.h>
#include <stdint.h>

#define JC_WAV_SOURCE_RATE 11025u
#define JC_WAV_SOURCE_CHANNELS 1u
#define JC_WAV_SOURCE_BITS 8u

typedef enum jc_wav_status {
    JC_WAV_OK = 0,
    JC_WAV_ERR_INVALID_ARGUMENT,
    JC_WAV_ERR_TRUNCATED,
    JC_WAV_ERR_NOT_WAVE,
    JC_WAV_ERR_MALFORMED,
    JC_WAV_ERR_MISSING_FMT,
    JC_WAV_ERR_MISSING_DATA,
    JC_WAV_ERR_UNSUPPORTED_FORMAT,
    JC_WAV_ERR_EMPTY_DATA
} jc_wav_status_t;

/*
 * A non-owning view of the original Johnny Castaway PCM data. The source WAV
 * storage must remain alive for as long as this view is used.
 */
typedef struct jc_wav_pcm {
    const uint8_t *samples;
    size_t sample_count;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
} jc_wav_pcm_t;

/*
 * Parse a bounded RIFF/WAVE buffer and accept only the original sound format:
 * unsigned 8-bit, mono PCM at 11025 Hz. No allocation is performed.
 */
jc_wav_status_t jc_wav_parse(const void *data, size_t size,
                             jc_wav_pcm_t *pcm);
const char *jc_wav_status_string(jc_wav_status_t status);

#endif
