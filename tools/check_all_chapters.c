/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Authentic-data acceptance tool. It never embeds or redistributes content. */
#include "jc_chapters.h"
#include "jc_runtime.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SCENE_TICKS 20000u

typedef struct scene_events {
    unsigned frames;
    unsigned sounds;
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
        event->opcode == 0xc051u)
        ++events->sounds;
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
    scene_events_t events;

    if (argc != 2) {
        fprintf(stderr, "usage: %s /path/to/RESOURCE.MAP\n", argv[0]);
        return 2;
    }
    memset(&content, 0, sizeof(content));
    if (!jc_content_load(&content, argv[1], NULL, content_error,
                         sizeof(content_error))) {
        fprintf(stderr, "content load failed: %s\n", content_error);
        return 1;
    }
    runtime = (jc_runtime_t *)malloc(sizeof(*runtime));
    if (runtime == NULL) {
        jc_content_unload(&content);
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
            jc_script_tick_result_t result = jc_runtime_tick(runtime, &error);
            if (result == JC_SCRIPT_TICK_ERROR) {
                jc_runtime_destroy(runtime);
                free(runtime);
                jc_content_unload(&content);
                return fail_scene(chapter, "runtime error", &error);
            }
            if (result == JC_SCRIPT_TICK_FINISHED)
                break;
        }
        if (tick == MAX_SCENE_TICKS || events.frames == 0u) {
            jc_runtime_destroy(runtime);
            free(runtime);
            jc_content_unload(&content);
            return fail_scene(chapter,
                              tick == MAX_SCENE_TICKS ? "timed out" :
                                                       "rendered no frames",
                              NULL);
        }
        total_ticks += tick + 1u;
        total_frames += events.frames;
        total_sounds += events.sounds;
        printf("PASS %02u/%02u %-12s ticks=%u frames=%u sounds=%u\n",
               (unsigned)(chapter_index + 1u),
               (unsigned)jc_chapter_count(), chapter->slug, tick + 1u,
               events.frames, events.sounds);
    }

    jc_runtime_destroy(runtime);
    free(runtime);
    jc_content_unload(&content);
    printf("All %u chapters completed: ticks=%u frames=%u sound-events=%u\n",
           (unsigned)jc_chapter_count(), total_ticks, total_frames,
           total_sounds);
    return 0;
}
