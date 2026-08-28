/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef JC_ISLAND_WALK_H
#define JC_ISLAND_WALK_H

#include "jc_bmp.h"
#include "jc_walk.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JC_ISLAND_WALK_ERROR_BYTES 160u

typedef enum jc_island_walk_result {
    JC_ISLAND_WALK_IDLE = 0,
    JC_ISLAND_WALK_ACTIVE,
    JC_ISLAND_WALK_ARRIVED,
    JC_ISLAND_WALK_ERROR
} jc_island_walk_result_t;

/*
 * A transition envelope for the persistent island and Johnny's walk between
 * two ADS scenes. The caller owns the decoded JOHNWALK.BMP and optional
 * BACKGRND.BMP for the lifetime of the binding. This object owns only its
 * clean indexed-island snapshot.
 *
 * Rendering order follows the GPL-3.0-or-later PS1 port at revision
 * 25c5d84593ac20cbee354eaab7779ab7397d6bbe:
 *   clean island -> Johnny -> trunk (frame 13) -> leaves (frame 12).
 * The tree cover is used only on the original spot 3 <-> 4 path. No resource
 * bytes or proprietary artwork are embedded in this module.
 */
typedef struct jc_island_walk {
    unsigned width;
    unsigned height;
    size_t pixel_count;
    int transparent_index;
    uint8_t *clean_pixels;
    jc_surface_t clean_island;
    const jc_bmp_t *johnny_sprites;
    const jc_bmp_t *island_sprites;
    jc_walker_t walker;
    jc_walk_frame_t pose;
    uint32_t pose_ticks_remaining;
    int island_offset_x;
    int island_offset_y;
    bool initialized;
    bool has_clean_island;
    bool has_pose;
    bool active;
    bool arrived;
    char error[JC_ISLAND_WALK_ERROR_BYTES];
} jc_island_walk_t;

bool jc_island_walk_init(jc_island_walk_t *transition,
                         unsigned width, unsigned height,
                         int transparent_index);
void jc_island_walk_destroy(jc_island_walk_t *transition);

/* BMPs remain caller-owned. Passing NULL for island_sprites disables the
 * behind-tree cover while keeping the walk functional. */
bool jc_island_walk_bind_sprites(jc_island_walk_t *transition,
                                 const jc_bmp_t *johnny_sprites,
                                 const jc_bmp_t *island_sprites);

/* Capture a clean, full-canvas island. Row padding and clip state are ignored. */
bool jc_island_walk_capture(jc_island_walk_t *transition,
                            const jc_surface_t *clean_island);

bool jc_island_walk_begin(jc_island_walk_t *transition,
                          uint8_t from_spot, uint8_t from_heading,
                          uint8_t to_spot, uint8_t to_heading,
                          int island_offset_x, int island_offset_y,
                          jc_rng_t *rng);

/* Redraw without advancing the six/eighty-tick walker cadence. */
bool jc_island_walk_render(const jc_island_walk_t *transition,
                           jc_surface_t *destination);

/* Advance one 50 Hz tick and produce a complete trail-free indexed frame. */
jc_island_walk_result_t jc_island_walk_tick(
    jc_island_walk_t *transition, jc_surface_t *destination);

/* Drop the active pose but retain the clean island and sprite binding. */
void jc_island_walk_cancel(jc_island_walk_t *transition);
const char *jc_island_walk_error(const jc_island_walk_t *transition);

#endif
