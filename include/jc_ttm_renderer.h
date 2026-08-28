/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef JC_TTM_RENDERER_H
#define JC_TTM_RENDERER_H

#include "jc_bmp.h"
#include "jc_palette.h"
#include "jc_script_vm.h"
#include "jc_surface.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JC_TTM_RENDERER_MAX_WIDTH 640u
#define JC_TTM_RENDERER_MAX_HEIGHT 480u
#define JC_TTM_RENDERER_MAX_PIXELS \
    (JC_TTM_RENDERER_MAX_WIDTH * JC_TTM_RENDERER_MAX_HEIGHT)
#define JC_TTM_RENDERER_MAX_RESOURCE_BYTES (64u * 1024u * 1024u)
#define JC_TTM_RENDERER_MAX_BMP_PIXELS (64u * 1024u * 1024u)
#define JC_TTM_RENDERER_NAME_BYTES 255u
#define JC_TTM_RENDERER_TRANSPARENT 255u

/*
 * load() returns a resource body. The bytes must remain valid until the
 * matching release() call. A borrowed/static buffer may use a NULL release.
 */
typedef bool (*jc_ttm_renderer_resource_load_t)(
    void *userdata, const char *name, const uint8_t **data, size_t *size,
    char *error, size_t error_size);
typedef void (*jc_ttm_renderer_resource_release_t)(
    void *userdata, const uint8_t *data, size_t size);

typedef struct jc_ttm_renderer_resources {
    jc_ttm_renderer_resource_load_t load;
    jc_ttm_renderer_resource_release_t release;
    void *userdata;
} jc_ttm_renderer_resources_t;

typedef struct jc_ttm_renderer {
    unsigned width;
    unsigned height;
    size_t pixel_count;
    int transparent_source_index;
    int offset_x;
    int offset_y;
    uint8_t *pixel_storage;
    size_t pixel_storage_size;
    jc_surface_t background;
    jc_surface_t saved_zones;
    jc_surface_t saved_background;
    jc_surface_t scratch;
    jc_surface_t output;
    jc_surface_t thread_layers[JC_SCRIPT_MAX_THREADS];
    bool thread_active[JC_SCRIPT_MAX_THREADS];
    jc_bmp_t bmp_slots[JC_SCRIPT_MAX_TTM_SLOTS][JC_SCRIPT_MAX_BMP_SLOTS];
    size_t bmp_pixel_total;
    jc_palette_t palette;
    bool has_palette;
    bool has_saved_background;
    jc_ttm_renderer_resources_t resources;
    jc_script_event_callback_t downstream;
    void *downstream_userdata;
    bool initialized;
} jc_ttm_renderer_t;

bool jc_ttm_renderer_init(jc_ttm_renderer_t *renderer,
                          unsigned width, unsigned height,
                          int transparent_source_index,
                          const jc_ttm_renderer_resources_t *resources,
                          jc_script_event_callback_t downstream,
                          void *downstream_userdata,
                          jc_script_error_t *error);
void jc_ttm_renderer_destroy(jc_ttm_renderer_t *renderer);
bool jc_ttm_renderer_set_offset(jc_ttm_renderer_t *renderer,
                                int x, int y, jc_script_error_t *error);
bool jc_ttm_renderer_set_background(jc_ttm_renderer_t *renderer,
                                    const jc_surface_t *background,
                                    jc_script_error_t *error);
bool jc_ttm_renderer_compose(jc_ttm_renderer_t *renderer,
                             jc_script_error_t *error);

/* Directly usable as jc_script_event_callback_t with renderer as userdata. */
bool jc_ttm_renderer_event(void *userdata, const jc_script_event_t *event,
                           jc_script_error_t *error);

const jc_surface_t *jc_ttm_renderer_output(const jc_ttm_renderer_t *renderer);
const jc_palette_t *jc_ttm_renderer_palette(const jc_ttm_renderer_t *renderer);

#endif
