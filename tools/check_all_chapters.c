/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Authentic-data acceptance tool. It never embeds or redistributes content. */
#include "jc_chapters.h"
#include "jc_audio.h"
#include "jc_runtime.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SCENE_TICKS 20000u
#define MAX_SCENE_SOUND_EVENTS 1024u

typedef struct sound_event {
    unsigned tick;
    unsigned frame;
    uint16_t sample_id;
} sound_event_t;

typedef struct scene_events {
    unsigned frames;
    unsigned sounds;
    unsigned known_silent_sounds;
    unsigned current_tick;
    bool invalid_sample;
    bool sound_overflow;
    uint16_t first_invalid_sample_id;
    unsigned first_invalid_tick;
    unsigned first_invalid_frame;
    sound_event_t sound[MAX_SCENE_SOUND_EVENTS];
} scene_events_t;

static bool record_event(void *userdata, const jc_script_event_t *event,
                         jc_script_error_t *error)
{
    scene_events_t *events = (scene_events_t *)userdata;

    (void)error;
    if (event->kind == JC_SCRIPT_EVENT_FRAME_READY)
        ++events->frames;
    if (event->domain == JC_SCRIPT_DOMAIN_TTM_VM &&
        event->kind == JC_SCRIPT_EVENT_INSTRUCTION &&
        event->opcode == 0xc051u) {
        uint16_t sample_id;

        if (event->arg_count < 1u) {
            if (!events->invalid_sample) {
                events->first_invalid_sample_id = UINT16_MAX;
                events->first_invalid_tick = events->current_tick;
                events->first_invalid_frame = events->frames;
            }
            events->invalid_sample = true;
            return true;
        }
        sample_id = event->args[0];
        if (sample_id >= JC_AUDIO_ORIGINAL_SAMPLE_COUNT) {
            if (!events->invalid_sample) {
                events->first_invalid_sample_id = sample_id;
                events->first_invalid_tick = events->current_tick;
                events->first_invalid_frame = events->frames;
            }
            events->invalid_sample = true;
        }
        if (sample_id == 11u || sample_id == 13u)
            ++events->known_silent_sounds;
        if (events->sounds >= MAX_SCENE_SOUND_EVENTS) {
            events->sound_overflow = true;
            return true;
        }
        events->sound[events->sounds].tick = events->current_tick;
        events->sound[events->sounds].frame = events->frames;
        events->sound[events->sounds].sample_id = sample_id;
        ++events->sounds;
    }
    return true;
}

static int fail_scene(const jc_chapter_t *chapter, const char *reason,
                      const jc_script_error_t *error)
{
    fprintf(stderr, "FAIL %s (%s:%u): %s",
            chapter->slug, chapter->ads_name, (unsigned)chapter->ads_tag,
            reason);
    if (error != NULL && error->message[0] != '\0')
        fprintf(stderr, ": %s", error->message);
    fputc('\n', stderr);
    return 1;
}

int main(int argc, char **argv)
{
    jc_content_t content;
    jc_runtime_config_t config;
    jc_runtime_t *runtime;
    jc_script_error_t error;
    char content_error[256];
    size_t chapter_index;
    unsigned total_ticks = 0u;
    unsigned total_frames = 0u;
    unsigned total_sounds = 0u;
    unsigned total_known_silent_sounds = 0u;
    scene_events_t events;
    FILE *sound_trace = NULL;

    if (argc != 2 && argc != 4) {
        fprintf(stderr,
                "usage: %s /path/to/RESOURCE.MAP "
                "[--sound-trace /path/to/events.csv]\n",
                argv[0]);
        return 2;
    }
    if (argc == 4) {
        if (strcmp(argv[2], "--sound-trace") != 0) {
            fprintf(stderr, "unknown option: %s\n", argv[2]);
            return 2;
        }
        sound_trace = fopen(argv[3], "wb");
        if (sound_trace == NULL) {
            fprintf(stderr, "could not open sound trace: %s\n", argv[3]);
            return 1;
        }
        fputs("slug,ads_name,ads_tag,seed,event,tick,frame,sample_id,status\n",
              sound_trace);
    }
    memset(&content, 0, sizeof(content));
    if (!jc_content_load(&content, argv[1], NULL, content_error,
                         sizeof(content_error))) {
        fprintf(stderr, "content load failed: %s\n", content_error);
        if (sound_trace != NULL)
            fclose(sound_trace);
        return 1;
    }
    runtime = (jc_runtime_t *)malloc(sizeof(*runtime));
    if (runtime == NULL) {
        jc_content_unload(&content);
        if (sound_trace != NULL)
            fclose(sound_trace);
        fputs("runtime allocation failed\n", stderr);
        return 1;
    }
    memset(&config, 0, sizeof(config));
    config.content = &content;
    config.width = 640u;
    config.height = 480u;
    config.transparent_source_index = -1;
    config.event_callback = record_event;
    config.event_userdata = &events;
    if (!jc_runtime_init(runtime, &config, &error)) {
        fprintf(stderr, "runtime init failed: %s\n", error.message);
        free(runtime);
        jc_content_unload(&content);
        if (sound_trace != NULL)
            fclose(sound_trace);
        return 1;
    }

    for (chapter_index = 0u; chapter_index < jc_chapter_count();
         ++chapter_index) {
        const jc_chapter_t *chapter = jc_chapter_at(chapter_index);
        char ads_name[64];
        unsigned tick;
        int written;

        memset(&events, 0, sizeof(events));
        written = snprintf(ads_name, sizeof(ads_name), "%s.ADS",
                           chapter->ads_name);
        if (written < 0 || (size_t)written >= sizeof(ads_name)) {
            jc_runtime_destroy(runtime);
            free(runtime);
            jc_content_unload(&content);
            return fail_scene(chapter, "ADS name is too long", NULL);
        }
        if (!jc_runtime_start_ads(runtime, ads_name, chapter->ads_tag,
                                  UINT32_C(0x4a430000) +
                                      (uint32_t)chapter_index,
                                  &error)) {
            jc_runtime_destroy(runtime);
            free(runtime);
            jc_content_unload(&content);
            return fail_scene(chapter, "could not start", &error);
        }
        for (tick = 0u; tick < MAX_SCENE_TICKS; ++tick) {
            events.current_tick = tick + 1u;
            jc_script_tick_result_t result = jc_runtime_tick(runtime, &error);
            if (result == JC_SCRIPT_TICK_ERROR) {
                jc_runtime_destroy(runtime);
                free(runtime);
                jc_content_unload(&content);
                if (sound_trace != NULL)
                    fclose(sound_trace);
                return fail_scene(chapter, "runtime error", &error);
            }
            if (result == JC_SCRIPT_TICK_FINISHED)
                break;
        }
        if (events.invalid_sample || events.sound_overflow) {
            if (events.invalid_sample) {
                fprintf(stderr,
                        "invalid PLAY_SAMPLE id=%s%u tick=%u frame=%u\n",
                        events.first_invalid_sample_id == UINT16_MAX ?
                            "missing-arg/" : "",
                        events.first_invalid_sample_id == UINT16_MAX ? 0u :
                            (unsigned)events.first_invalid_sample_id,
                        events.first_invalid_tick, events.first_invalid_frame);
            }
            jc_runtime_destroy(runtime);
            free(runtime);
            jc_content_unload(&content);
            if (sound_trace != NULL)
                fclose(sound_trace);
            return fail_scene(chapter,
                              events.sound_overflow ?
                                  "sound event capacity exceeded" :
                                  "invalid PLAY_SAMPLE event (missing ID or ID >= 25)",
                              NULL);
        }
        if (tick == MAX_SCENE_TICKS || events.frames == 0u) {
            jc_runtime_destroy(runtime);
            free(runtime);
            jc_content_unload(&content);
            if (sound_trace != NULL)
                fclose(sound_trace);
            return fail_scene(chapter,
                              tick == MAX_SCENE_TICKS ? "timed out" :
                                                       "rendered no frames",
                              NULL);
        }
        total_ticks += tick + 1u;
        total_frames += events.frames;
        total_sounds += events.sounds;
        total_known_silent_sounds += events.known_silent_sounds;
        if (sound_trace != NULL) {
            unsigned event_index;

            for (event_index = 0u; event_index < events.sounds;
                 ++event_index) {
                const sound_event_t *sound = &events.sound[event_index];
                if (fprintf(sound_trace,
                            "%s,%s,%u,0x%08x,%u,%u,%u,%u,%s\n",
                            chapter->slug, chapter->ads_name,
                            (unsigned)chapter->ads_tag,
                            (unsigned)(UINT32_C(0x4a430000) +
                                       (uint32_t)chapter_index),
                            event_index + 1u, sound->tick, sound->frame,
                            (unsigned)sound->sample_id,
                            sound->sample_id == 11u ||
                                sound->sample_id == 13u ?
                                "known_silent" : "playable") < 0) {
                    jc_runtime_destroy(runtime);
                    free(runtime);
                    jc_content_unload(&content);
                    fclose(sound_trace);
                    return fail_scene(chapter, "could not write sound trace",
                                      NULL);
                }
            }
        }
        printf("PASS %02u/%02u %-12s ticks=%u frames=%u sounds=%u "
               "known-silent=%u\n",
               (unsigned)(chapter_index + 1u),
               (unsigned)jc_chapter_count(), chapter->slug, tick + 1u,
               events.frames, events.sounds, events.known_silent_sounds);
    }

    jc_runtime_destroy(runtime);
    free(runtime);
    jc_content_unload(&content);
    if (sound_trace != NULL && fclose(sound_trace) != 0) {
        fputs("could not close sound trace\n", stderr);
        return 1;
    }
    printf("All %u chapters completed: ticks=%u frames=%u sound-events=%u "
           "known-silent-events=%u\n",
           (unsigned)jc_chapter_count(), total_ticks, total_frames,
           total_sounds, total_known_silent_sounds);
    return 0;
}
