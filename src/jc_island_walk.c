/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Persistent-island walk compositor. The clean-frame restore and tree
 * occlusion behavior are independently expressed from the PS1 port's
 * src/scene/island.c, src/ads/island_walk.c.inc, and src/walk/ code at revision
 * 25c5d84593ac20cbee354eaab7779ab7397d6bbe (GPL-3.0-or-later).
 */
#include "jc_island_walk.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JC_ISLAND_TREE_LEAVES_FRAME 12u
#define JC_ISLAND_TREE_TRUNK_FRAME 13u

static bool fail(jc_island_walk_t *transition, const char *message)
{
    if (transition != NULL) {
        snprintf(transition->error, sizeof(transition->error), "%s", message);
        transition->active = false;
    }
    return false;
}

static void clear_error(jc_island_walk_t *transition)
{
    transition->error[0] = '\0';
}

static bool surface_is_canvas(const jc_island_walk_t *transition,
                              const jc_surface_t *surface)
{
    return surface != NULL && surface->pixels != NULL &&
           surface->width == transition->width &&
           surface->height == transition->height &&
           surface->pitch >= surface->width;
}

static bool bmp_frame_surface(const jc_bmp_t *bmp, size_t frame,
                              jc_surface_t *surface)
{
    const jc_bmp_image_t *image;
    if (bmp == NULL || bmp->images == NULL || frame >= bmp->image_count)
        return false;
    image = &bmp->images[frame];
    if (image->pixels == NULL || image->width == 0u || image->height == 0u ||
        image->pixel_count < (size_t)image->width * image->height)
        return false;
    return jc_surface_init(surface, image->pixels, image->pixel_count,
                           image->width, image->height, image->width);
}

static bool add_coordinate(int left, int right, int *sum)
{
    if ((right > 0 && left > INT_MAX - right) ||
        (right < 0 && left < INT_MIN - right))
        return false;
    *sum = left + right;
    return true;
}

bool jc_island_walk_init(jc_island_walk_t *transition,
                         unsigned width, unsigned height,
                         int transparent_index)
{
    size_t pixel_count;

    if (transition == NULL)
        return false;
    memset(transition, 0, sizeof(*transition));
    if (width == 0u || height == 0u || width > (unsigned)INT_MAX ||
        height > (unsigned)INT_MAX || transparent_index < -1 ||
        transparent_index > 255 || (size_t)height > SIZE_MAX / width)
        return fail(transition, "island walk canvas arguments are invalid");
    pixel_count = (size_t)width * height;
    transition->clean_pixels = (uint8_t *)malloc(pixel_count);
    if (transition->clean_pixels == NULL)
        return fail(transition, "could not allocate the clean island canvas");
    if (!jc_surface_init(&transition->clean_island,
                         transition->clean_pixels, pixel_count,
                         width, height, width)) {
        free(transition->clean_pixels);
        memset(transition, 0, sizeof(*transition));
        return false;
    }
    transition->width = width;
    transition->height = height;
    transition->pixel_count = pixel_count;
    transition->transparent_index = transparent_index;
    transition->initialized = true;
    clear_error(transition);
    return true;
}

void jc_island_walk_destroy(jc_island_walk_t *transition)
{
    if (transition == NULL)
        return;
    free(transition->clean_pixels);
    memset(transition, 0, sizeof(*transition));
}

bool jc_island_walk_bind_sprites(jc_island_walk_t *transition,
                                 const jc_bmp_t *johnny_sprites,
                                 const jc_bmp_t *island_sprites)
{
    if (transition == NULL || !transition->initialized)
        return false;
    clear_error(transition);
    if (johnny_sprites == NULL || johnny_sprites->images == NULL ||
        johnny_sprites->image_count == 0u)
        return fail(transition, "JOHNWALK sprite table is empty");
    if (island_sprites != NULL &&
        (island_sprites->images == NULL ||
         island_sprites->image_count <= JC_ISLAND_TREE_TRUNK_FRAME))
        return fail(transition, "BACKGRND sprite table lacks tree cover frames");
    transition->johnny_sprites = johnny_sprites;
    transition->island_sprites = island_sprites;
    return true;
}

bool jc_island_walk_capture(jc_island_walk_t *transition,
                            const jc_surface_t *clean_island)
{
    unsigned row;
    if (transition == NULL || !transition->initialized)
        return false;
    clear_error(transition);
    if (!surface_is_canvas(transition, clean_island))
        return fail(transition, "clean island does not match the walk canvas");
    for (row = 0u; row < transition->height; ++row) {
        memcpy(transition->clean_pixels + (size_t)row * transition->width,
               clean_island->pixels + (size_t)row * clean_island->pitch,
               transition->width);
    }
    transition->has_clean_island = true;
    return true;
}

void jc_island_walk_cancel(jc_island_walk_t *transition)
{
    if (transition == NULL || !transition->initialized)
        return;
    memset(&transition->walker, 0, sizeof(transition->walker));
    memset(&transition->pose, 0, sizeof(transition->pose));
    transition->pose_ticks_remaining = 0u;
    transition->has_pose = false;
    transition->active = false;
    transition->arrived = false;
    clear_error(transition);
}

bool jc_island_walk_begin(jc_island_walk_t *transition,
                          uint8_t from_spot, uint8_t from_heading,
                          uint8_t to_spot, uint8_t to_heading,
                          int island_offset_x, int island_offset_y,
                          jc_rng_t *rng)
{
    if (transition == NULL || !transition->initialized)
        return false;
    jc_island_walk_cancel(transition);
    if (!transition->has_clean_island)
        return fail(transition, "no clean island has been captured");
    if (transition->johnny_sprites == NULL)
        return fail(transition, "JOHNWALK sprites are not bound");
    if (!jc_walk_init(&transition->walker, from_spot, from_heading,
                      to_spot, to_heading, rng))
        return fail(transition, "walk endpoints or random state are invalid");
    transition->island_offset_x = island_offset_x;
    transition->island_offset_y = island_offset_y;
    transition->active = true;
    return true;
}

static bool render_pose(const jc_island_walk_t *transition,
                        jc_surface_t *destination)
{
    jc_surface_t sprite;
    int x;
    int y;

    if (!transition->has_pose)
        return true;
    if (!bmp_frame_surface(transition->johnny_sprites,
                           transition->pose.sprite, &sprite) ||
        !add_coordinate(transition->pose.x, transition->island_offset_x, &x) ||
        !add_coordinate(transition->pose.y, transition->island_offset_y, &y))
        return false;
    jc_surface_blit(destination, x, y, &sprite,
                    transition->transparent_index,
                    transition->pose.flip_horizontal);

    if (transition->pose.behind_tree && transition->island_sprites != NULL) {
        if (!bmp_frame_surface(transition->island_sprites,
                               JC_ISLAND_TREE_TRUNK_FRAME, &sprite) ||
            !add_coordinate(442, transition->island_offset_x, &x) ||
            !add_coordinate(148, transition->island_offset_y, &y))
            return false;
        jc_surface_blit(destination, x, y, &sprite,
                        transition->transparent_index, false);
        if (!bmp_frame_surface(transition->island_sprites,
                               JC_ISLAND_TREE_LEAVES_FRAME, &sprite) ||
            !add_coordinate(365, transition->island_offset_x, &x) ||
            !add_coordinate(122, transition->island_offset_y, &y))
            return false;
        jc_surface_blit(destination, x, y, &sprite,
                        transition->transparent_index, false);
    }
    return true;
}

bool jc_island_walk_render(const jc_island_walk_t *transition,
                           jc_surface_t *destination)
{
    unsigned row;
    if (transition == NULL || !transition->initialized ||
        !transition->has_clean_island ||
        !surface_is_canvas(transition, destination))
        return false;
    jc_surface_reset_clip(destination);
    for (row = 0u; row < transition->height; ++row) {
        memcpy(destination->pixels + (size_t)row * destination->pitch,
               transition->clean_pixels + (size_t)row * transition->width,
               transition->width);
    }
    return render_pose(transition, destination);
}

jc_island_walk_result_t jc_island_walk_tick(
    jc_island_walk_t *transition, jc_surface_t *destination)
{
    if (transition == NULL || !transition->initialized)
        return JC_ISLAND_WALK_ERROR;
    if (!transition->active)
        return transition->arrived ? JC_ISLAND_WALK_ARRIVED :
                                     JC_ISLAND_WALK_IDLE;
    clear_error(transition);
    if (!surface_is_canvas(transition, destination)) {
        fail(transition, "walk destination does not match the canvas");
        return JC_ISLAND_WALK_ERROR;
    }
    if (transition->pose_ticks_remaining == 0u) {
        if (!jc_walk_next(&transition->walker, &transition->pose)) {
            fail(transition, "walker stopped before producing an arrival pose");
            return JC_ISLAND_WALK_ERROR;
        }
        transition->has_pose = true;
        transition->pose_ticks_remaining = transition->pose.delay_ticks;
        if (transition->pose_ticks_remaining == 0u) {
            fail(transition, "walker produced a zero-duration pose");
            return JC_ISLAND_WALK_ERROR;
        }
    }
    if (!jc_island_walk_render(transition, destination)) {
        fail(transition, "walk sprite or tree cover frame is invalid");
        return JC_ISLAND_WALK_ERROR;
    }
    --transition->pose_ticks_remaining;
    if (transition->walker.arrived && transition->pose_ticks_remaining == 0u) {
        transition->active = false;
        transition->arrived = true;
        return JC_ISLAND_WALK_ARRIVED;
    }
    return JC_ISLAND_WALK_ACTIVE;
}

const char *jc_island_walk_error(const jc_island_walk_t *transition)
{
    return transition != NULL ? transition->error :
                                "island walk object is null";
}
