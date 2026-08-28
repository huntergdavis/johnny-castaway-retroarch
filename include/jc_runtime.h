/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef JC_RUNTIME_H
#define JC_RUNTIME_H

#include "jc_content.h"
#include "jc_script_vm.h"
#include "jc_ttm_renderer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct retro_vfs_interface;

#define JC_RUNTIME_MAX_RESOURCE_BYTES (64u * 1024u * 1024u)
#define JC_RUNTIME_MAX_DECODED_SCRIPT_BYTES (16u * 1024u * 1024u)
#define JC_RUNTIME_MAX_TOTAL_SCRIPT_BYTES (64u * 1024u * 1024u)
#define JC_RUNTIME_NAME_BYTES 255u

typedef struct jc_runtime_config {
    /* content and vfs must remain valid until jc_runtime_destroy(). */
    const jc_content_t *content;
    const struct retro_vfs_interface *vfs;
    unsigned width;
    unsigned height;
    int transparent_source_index;
    jc_script_event_callback_t event_callback;
    void *event_userdata;
} jc_runtime_config_t;

typedef struct jc_runtime {
    const jc_content_t *content;
    const struct retro_vfs_interface *vfs;
    unsigned width;
    unsigned height;
    int transparent_source_index;
    jc_script_event_callback_t event_callback;
    void *event_userdata;
    jc_ads_t ads;
    uint8_t *ads_bytecode;
    jc_ttm_t ttms[JC_SCRIPT_MAX_TTM_SLOTS];
    uint8_t *ttm_bytecode[JC_SCRIPT_MAX_TTM_SLOTS];
    bool ttm_loaded[JC_SCRIPT_MAX_TTM_SLOTS];
    size_t total_script_bytes;
    jc_script_vm_t vm;
    jc_ttm_renderer_t renderer;
    bool initialized;
    bool scene_loaded;
    bool scene_finished;
} jc_runtime_t;

/* jc_runtime_t is intentionally bounded but large; use static or heap storage. */
bool jc_runtime_init(jc_runtime_t *runtime,
                     const jc_runtime_config_t *config,
                     jc_script_error_t *error);
void jc_runtime_destroy(jc_runtime_t *runtime);

/* Clears the active scene and renderer while retaining the content binding. */
bool jc_runtime_reset(jc_runtime_t *runtime, jc_script_error_t *error);

/* Loads one ADS and every TTM declared by its exact resource id/name pair. */
bool jc_runtime_start_ads(jc_runtime_t *runtime, const char *ads_name,
                          uint16_t start_tag, uint32_t seed,
                          jc_script_error_t *error);

/* Advances exactly one 50 Hz VM tick and never sleeps or blocks on timing. */
jc_script_tick_result_t jc_runtime_tick(jc_runtime_t *runtime,
                                        jc_script_error_t *error);

bool jc_runtime_is_active(const jc_runtime_t *runtime);
const jc_surface_t *jc_runtime_output(const jc_runtime_t *runtime);
const jc_palette_t *jc_runtime_palette(const jc_runtime_t *runtime);

#endif
