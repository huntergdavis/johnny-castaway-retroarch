/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "jc_island_walk.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIDTH 640u
#define HEIGHT 480u
#define PIXELS ((size_t)WIDTH * HEIGHT)

typedef struct test_sprites {
    jc_bmp_t johnny;
    jc_bmp_image_t johnny_images[64];
    uint8_t *johnny_pixels;
    jc_bmp_t island;
    jc_bmp_image_t island_images[14];
    uint8_t transparent_pixel;
    uint8_t *trunk_pixels;
    uint8_t *leaves_pixels;
} test_sprites_t;

static void set_image(jc_bmp_image_t *image, uint16_t width, uint16_t height,
                      uint8_t *pixels)
{
    image->width = width;
    image->height = height;
    image->pixel_count = (size_t)width * height;
    image->pixels = pixels;
}

static void init_sprites(test_sprites_t *sprites, bool full_canvas_johnny)
{
    size_t index;
    size_t johnny_size = full_canvas_johnny ? PIXELS : 1u;
    memset(sprites, 0, sizeof(*sprites));
    sprites->johnny_pixels = (uint8_t *)malloc(johnny_size);
    sprites->trunk_pixels = (uint8_t *)malloc(198u * 332u);
    sprites->leaves_pixels = (uint8_t *)malloc(275u * 358u);
    assert(sprites->johnny_pixels != NULL && sprites->trunk_pixels != NULL &&
           sprites->leaves_pixels != NULL);
    memset(sprites->johnny_pixels, 9, johnny_size);
    memset(sprites->trunk_pixels, 6, 198u * 332u);
    memset(sprites->leaves_pixels, 7, 275u * 358u);
    sprites->johnny.images = sprites->johnny_images;
    sprites->johnny.image_count = 64u;
    for (index = 0u; index < sprites->johnny.image_count; ++index) {
        set_image(&sprites->johnny_images[index],
                  full_canvas_johnny ? WIDTH : 1u,
                  full_canvas_johnny ? HEIGHT : 1u,
                  sprites->johnny_pixels);
    }

    sprites->transparent_pixel = 0u;
    sprites->island.images = sprites->island_images;
    sprites->island.image_count = 14u;
    for (index = 0u; index < sprites->island.image_count; ++index)
        set_image(&sprites->island_images[index], 1u, 1u,
                  &sprites->transparent_pixel);
    set_image(&sprites->island_images[12], 275u, 358u,
              sprites->leaves_pixels);
    set_image(&sprites->island_images[13], 198u, 332u,
              sprites->trunk_pixels);
}

static void free_sprites(test_sprites_t *sprites)
{
    free(sprites->johnny_pixels);
    free(sprites->trunk_pixels);
    free(sprites->leaves_pixels);
    memset(sprites, 0, sizeof(*sprites));
}

static void test_cadence_restore_and_arrival(void)
{
    jc_island_walk_t transition;
    test_sprites_t sprites;
    jc_rng_t rng;
    jc_surface_t clean;
    jc_surface_t output;
    uint8_t *clean_pixels = (uint8_t *)malloc(PIXELS);
    uint8_t *output_pixels = (uint8_t *)malloc(PIXELS);
    jc_walk_frame_t first_pose;
    unsigned repeat;
    unsigned final_pose_ticks = 0u;
    unsigned total_ticks = 0u;
    jc_island_walk_result_t result;

    assert(clean_pixels != NULL && output_pixels != NULL);
    memset(clean_pixels, 1, PIXELS);
    memset(output_pixels, 2, PIXELS);
    assert(jc_surface_init(&clean, clean_pixels, PIXELS, WIDTH, HEIGHT, WIDTH));
    assert(jc_surface_init(&output, output_pixels, PIXELS,
                           WIDTH, HEIGHT, WIDTH));
    init_sprites(&sprites, false);
    assert(jc_island_walk_init(&transition, WIDTH, HEIGHT, 0));
    assert(jc_island_walk_bind_sprites(&transition, &sprites.johnny, NULL));
    assert(jc_island_walk_capture(&transition, &clean));
    jc_rng_init(&rng, 7u);
    assert(jc_island_walk_begin(&transition, 0u, 0u, 1u, 4u,
                                0, 0, &rng));

    assert(jc_island_walk_tick(&transition, &output) ==
           JC_ISLAND_WALK_ACTIVE);
    first_pose = transition.pose;
    assert(transition.pose_ticks_remaining == 5u);
    for (repeat = 1u; repeat < 6u; ++repeat) {
        output.pixels[0] = 99u;
        assert(jc_island_walk_tick(&transition, &output) ==
               JC_ISLAND_WALK_ACTIVE);
        assert(memcmp(&transition.pose, &first_pose,
                      sizeof(first_pose)) == 0);
        assert(output.pixels[0] == 1u);
    }
    assert(transition.pose_ticks_remaining == 0u);
    assert(jc_island_walk_tick(&transition, &output) ==
           JC_ISLAND_WALK_ACTIVE);
    assert(transition.pose_ticks_remaining == 5u);

    do {
        result = jc_island_walk_tick(&transition, &output);
        assert(result != JC_ISLAND_WALK_ERROR);
        if (transition.walker.arrived)
            ++final_pose_ticks;
        assert(++total_ticks < 50000u);
    } while (result != JC_ISLAND_WALK_ARRIVED);
    assert(final_pose_ticks == 80u);
    assert(transition.arrived && !transition.active);
    assert(jc_island_walk_tick(&transition, &output) ==
           JC_ISLAND_WALK_ARRIVED);
    assert(jc_island_walk_render(&transition, &output));

    jc_island_walk_cancel(&transition);
    assert(jc_island_walk_tick(&transition, &output) == JC_ISLAND_WALK_IDLE);
    jc_island_walk_destroy(&transition);
    free_sprites(&sprites);
    free(clean_pixels);
    free(output_pixels);
}

static void test_tree_cover_order(void)
{
    jc_island_walk_t transition;
    test_sprites_t sprites;
    jc_rng_t rng;
    jc_surface_t clean;
    jc_surface_t output;
    uint8_t *clean_pixels = (uint8_t *)malloc(PIXELS);
    uint8_t *output_pixels = (uint8_t *)malloc(PIXELS);
    unsigned ticks = 0u;
    bool saw_behind_tree = false;

    assert(clean_pixels != NULL && output_pixels != NULL);
    memset(clean_pixels, 1, PIXELS);
    assert(jc_surface_init(&clean, clean_pixels, PIXELS, WIDTH, HEIGHT, WIDTH));
    assert(jc_surface_init(&output, output_pixels, PIXELS,
                           WIDTH, HEIGHT, WIDTH));
    init_sprites(&sprites, true);
    assert(jc_island_walk_init(&transition, WIDTH, HEIGHT, 0));
    assert(jc_island_walk_bind_sprites(&transition, &sprites.johnny,
                                       &sprites.island));
    assert(jc_island_walk_capture(&transition, &clean));
    /* Seed 0's first D->E route roll is 66, selecting the direct tree edge. */
    jc_rng_init(&rng, 0u);
    assert(jc_island_walk_begin(&transition, 3u, 0u, 4u, 4u,
                                0, 0, &rng));
    while (jc_island_walk_tick(&transition, &output) ==
           JC_ISLAND_WALK_ACTIVE) {
        assert(++ticks < 50000u);
        if (transition.pose.behind_tree) {
            /* Leaves are drawn after both Johnny and the trunk. */
            assert(output.pixels[479u * WIDTH + 639u] == 7u);
            saw_behind_tree = true;
            break;
        }
    }
    assert(saw_behind_tree);
    jc_island_walk_destroy(&transition);
    free_sprites(&sprites);
    free(clean_pixels);
    free(output_pixels);
}

static void test_invalid_contracts(void)
{
    jc_island_walk_t transition;
    test_sprites_t sprites;
    jc_rng_t rng;
    jc_surface_t clean;
    jc_surface_t output;
    uint8_t clean_pixels[16];
    uint8_t output_pixels[16];

    memset(clean_pixels, 1, sizeof(clean_pixels));
    assert(!jc_island_walk_init(NULL, 4u, 4u, 0));
    assert(!jc_island_walk_init(&transition, 0u, 4u, 0));
    assert(jc_island_walk_init(&transition, 4u, 4u, 0));
    assert(!jc_island_walk_bind_sprites(&transition, NULL, NULL));
    init_sprites(&sprites, false);
    assert(jc_island_walk_bind_sprites(&transition, &sprites.johnny, NULL));
    jc_rng_init(&rng, 1u);
    assert(!jc_island_walk_begin(&transition, 0u, 0u, 1u, 0u,
                                 0, 0, &rng));
    assert(jc_surface_init(&clean, clean_pixels, sizeof(clean_pixels),
                           4u, 4u, 4u));
    assert(jc_surface_init(&output, output_pixels, sizeof(output_pixels),
                           4u, 4u, 4u));
    assert(jc_island_walk_capture(&transition, &clean));
    assert(!jc_island_walk_begin(&transition, 9u, 0u, 1u, 0u,
                                 0, 0, &rng));
    assert(jc_island_walk_begin(&transition, 0u, 0u, 1u, 0u,
                                0, 0, &rng));
    sprites.johnny.image_count = 1u;
    while (jc_island_walk_tick(&transition, &output) ==
           JC_ISLAND_WALK_ACTIVE) {
        /* The first nonzero sprite index must fail safely. */
    }
    assert(jc_island_walk_error(&transition)[0] != '\0');
    jc_island_walk_destroy(&transition);
    free_sprites(&sprites);
}

int main(void)
{
    test_cadence_restore_and_arrival();
    test_tree_cover_order();
    test_invalid_contracts();
    puts("island walk tests passed");
    return 0;
}
