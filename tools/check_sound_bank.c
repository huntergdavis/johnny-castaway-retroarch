/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Authentic-data acceptance tool. It never embeds or redistributes audio. */
#include "jc_audio.h"
#include "jc_sfx.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIX_BLOCK_FRAMES 1024u

static bool is_known_silent_id(unsigned sample_id)
{
    return sample_id == 11u || sample_id == 13u;
}

static int fail(const char *reason, unsigned sample_id)
{
    if (sample_id < JC_AUDIO_ORIGINAL_SAMPLE_COUNT)
        fprintf(stderr, "FAIL sound%u.wav: %s\n", sample_id, reason);
    else
        fprintf(stderr, "FAIL sound bank: %s\n", reason);
    return 1;
}

int main(int argc, char **argv)
{
    jc_sfx_t sfx;
    jc_audio_t audio;
    jc_sfx_report_t report;
    int16_t output[MIX_BLOCK_FRAMES * JC_AUDIO_OUTPUT_CHANNELS];
    uint32_t expected_ids = 0u;
    unsigned sample_id;

    if (argc != 2) {
        fprintf(stderr, "usage: %s /path/to/RESOURCE.MAP\n", argv[0]);
        return 2;
    }
    jc_sfx_init(&sfx);
    jc_audio_init(&audio);
    if (jc_sfx_load(&sfx, &audio, argv[1], NULL, &report) != JC_SFX_OK) {
        fprintf(stderr, "sound bank load failed: %s\n",
                jc_sfx_status_string(report.status));
        return 1;
    }
    for (sample_id = 0u; sample_id < JC_AUDIO_ORIGINAL_SAMPLE_COUNT;
         ++sample_id) {
        if (!is_known_silent_id(sample_id))
            expected_ids |= (uint32_t)1u << sample_id;
    }
    if (report.attempted_count != 23u || report.loaded_count != 23u ||
        report.missing_count != 2u || report.invalid_count != 0u ||
        report.loaded_ids != expected_ids ||
        report.missing_ids != (((uint32_t)1u << 11u) |
                               ((uint32_t)1u << 13u))) {
        fprintf(stderr,
                "FAIL sound bank report: attempted=%u loaded=%u missing=%u "
                "invalid=%u loaded-mask=%08x missing-mask=%08x\n",
                report.attempted_count, report.loaded_count,
                report.missing_count, report.invalid_count,
                (unsigned)report.loaded_ids, (unsigned)report.missing_ids);
        jc_sfx_unload(&sfx, &audio);
        return 1;
    }

    for (sample_id = 0u; sample_id < JC_AUDIO_ORIGINAL_SAMPLE_COUNT;
         ++sample_id) {
        size_t remaining;
        size_t source_samples;
        size_t signal_frames = 0u;
        unsigned peak = 0u;

        if (is_known_silent_id(sample_id)) {
            if (jc_audio_has_sample(&audio, sample_id) ||
                jc_audio_trigger(&audio, sample_id) != JC_AUDIO_INVALID_VOICE) {
                jc_sfx_unload(&sfx, &audio);
                return fail("known-absent original ID was playable", sample_id);
            }
            printf("PASS id=%02u known-silent original asset is absent\n",
                   sample_id);
            continue;
        }
        if (!jc_audio_has_sample(&audio, sample_id)) {
            jc_sfx_unload(&sfx, &audio);
            return fail("required user-supplied effect did not load", sample_id);
        }
        source_samples = audio.samples[sample_id].length;
        if (source_samples == 0u || source_samples > SIZE_MAX / 4u) {
            jc_sfx_unload(&sfx, &audio);
            return fail("decoded sample length is invalid", sample_id);
        }
        if (jc_audio_trigger(&audio, sample_id) == JC_AUDIO_INVALID_VOICE) {
            jc_sfx_unload(&sfx, &audio);
            return fail("effect could not be triggered", sample_id);
        }
        remaining = source_samples * 4u;
        while (remaining > 0u) {
            size_t frames = remaining < MIX_BLOCK_FRAMES ?
                remaining : MIX_BLOCK_FRAMES;
            size_t frame;

            memset(output, 0, sizeof(output));
            if (jc_audio_mix(&audio, output, frames) != frames) {
                jc_sfx_unload(&sfx, &audio);
                return fail("mixer returned a short block", sample_id);
            }
            for (frame = 0u; frame < frames; ++frame) {
                int value = output[frame * JC_AUDIO_OUTPUT_CHANNELS];
                unsigned magnitude =
                    value < 0 ? (unsigned)(-(int64_t)value) : (unsigned)value;
                if (output[frame * JC_AUDIO_OUTPUT_CHANNELS + 1u] != value) {
                    jc_sfx_unload(&sfx, &audio);
                    return fail("mono effect did not produce equal stereo", sample_id);
                }
                if (magnitude != 0u)
                    ++signal_frames;
                if (magnitude > peak)
                    peak = magnitude;
            }
            remaining -= frames;
        }
        if (signal_frames == 0u || peak == 0u) {
            jc_sfx_unload(&sfx, &audio);
            return fail("decoded effect produced only silence", sample_id);
        }
        if (jc_audio_active_voice_count(&audio) != 0u) {
            jc_sfx_unload(&sfx, &audio);
            return fail("voice remained active after the decoded effect", sample_id);
        }
        printf("PASS id=%02u source-samples=%u duration-ms=%u "
               "signal-frames=%u peak=%u\n",
               sample_id, (unsigned)source_samples,
               (unsigned)((source_samples * 1000u) / JC_WAV_SOURCE_RATE),
               (unsigned)signal_frames, peak);
    }

    jc_sfx_unload(&sfx, &audio);
    for (sample_id = 0u; sample_id < JC_AUDIO_ORIGINAL_SAMPLE_COUNT;
         ++sample_id) {
        if (jc_audio_has_sample(&audio, sample_id))
            return fail("sample remained registered after unload", sample_id);
    }
    puts("All 23 original effects loaded and produced signal; IDs 11/13 are known silent.");
    return 0;
}
