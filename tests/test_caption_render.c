/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "jc_caption_render.h"
#include "jc_captions.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SCENE_WIDTH 640u
#define SCENE_HEIGHT 480u

static uint32_t scene_a[SCENE_WIDTH * SCENE_HEIGHT];
static uint32_t scene_b[SCENE_WIDTH * SCENE_HEIGHT];

static void fill(uint32_t *pixels, size_t count, uint32_t color)
{
    size_t index;
    for (index = 0u; index < count; ++index)
        pixels[index] = color;
}

static uint64_t hash_pixels(const uint32_t *pixels, size_t count)
{
    uint64_t hash = 1469598103934665603ull;
    size_t index;
    for (index = 0u; index < count; ++index) {
        hash ^= pixels[index];
        hash *= 1099511628211ull;
    }
    return hash;
}

static void transparent_a_pixels(void)
{
    uint32_t pixels[20u * 12u];
    jc_caption_render_options_t options;
    jc_caption_render_result_t result;
    const uint32_t background = 0x00112233u;
    const uint32_t foreground = 0x00abcdefu;

    fill(pixels, 20u * 12u, background);
    jc_caption_render_options_init(&options);
    options.text_size = JC_CAPTION_TEXT_SMALL;
    options.background = JC_CAPTION_BACKGROUND_NONE;
    options.anchor = JC_CAPTION_ANCHOR_TOP;
    options.horizontal_margin = 0u;
    options.vertical_margin = 0u;
    options.foreground_xrgb = foreground;
    assert(jc_caption_render(pixels, 20u, 12u, 20u, "A", 1u,
                             &options, &result));
    assert(result.line_count == 1u && result.consumed_bytes == 1u);
    assert(result.x == 7u && result.y == 0u);
    assert(result.width == 5u && result.height == 7u);
    assert(result.foreground_pixels == 18u);
    assert(!result.truncated && !result.clipped);
    assert(pixels[0u * 20u + 7u] == background);
    assert(pixels[0u * 20u + 8u] == foreground);
    assert(pixels[0u * 20u + 9u] == foreground);
    assert(pixels[0u * 20u + 10u] == foreground);
    assert(pixels[0u * 20u + 11u] == background);
    assert(pixels[3u * 20u + 7u] == foreground);
    assert(pixels[3u * 20u + 11u] == foreground);
    assert(pixels[7u * 20u + 9u] == background);
}

static void opacity_box_pixels(void)
{
    uint32_t pixels[20u * 20u];
    jc_caption_render_options_t options;
    jc_caption_render_result_t result;

    fill(pixels, 20u * 20u, 0x00ffffffu);
    jc_caption_render_options_init(&options);
    options.text_size = JC_CAPTION_TEXT_SMALL;
    options.background = JC_CAPTION_BACKGROUND_BOX;
    options.anchor = JC_CAPTION_ANCHOR_TOP;
    options.horizontal_margin = 0u;
    options.vertical_margin = 0u;
    options.padding = 2u;
    options.foreground_xrgb = 0x00ffffffu;
    options.background_xrgb = 0x00000000u;
    options.background_opacity = 128u;
    assert(jc_caption_render(pixels, 20u, 20u, 20u, "A", 1u,
                             &options, &result));
    assert(result.x == 5u && result.y == 0u);
    assert(result.width == 9u && result.height == 11u);
    assert(pixels[0u * 20u + 5u] == 0x007f7f7fu);
    assert(pixels[0u * 20u + 13u] == 0x007f7f7fu);
    assert(pixels[0u * 20u + 4u] == 0x00ffffffu);
    assert(pixels[2u * 20u + 8u] == 0x00ffffffu);
    assert(pixels[2u * 20u + 7u] == 0x007f7f7fu);
}

static void bottom_bar_and_sizes(void)
{
    uint32_t pixels[20u * 20u];
    uint32_t large_pixels[40u * 32u];
    jc_caption_render_options_t options;
    jc_caption_render_result_t result;

    fill(pixels, 20u * 20u, 0x00336699u);
    jc_caption_render_options_init(&options);
    options.text_size = JC_CAPTION_TEXT_MEDIUM;
    options.background = JC_CAPTION_BACKGROUND_BAR;
    options.anchor = JC_CAPTION_ANCHOR_BOTTOM;
    options.horizontal_margin = 0u;
    options.vertical_margin = 1u;
    options.padding = 1u;
    options.background_opacity = 255u;
    assert(jc_caption_render(pixels, 20u, 20u, 20u, "I", 1u,
                             &options, &result));
    assert(result.x == 0u && result.y == 3u);
    assert(result.width == 20u && result.height == 16u);
    assert(pixels[2u * 20u] == 0x00336699u);
    assert(pixels[3u * 20u] == 0x00000000u);
    assert(pixels[18u * 20u + 19u] == 0x00000000u);
    assert(pixels[19u * 20u] == 0x00336699u);

    options.text_size = JC_CAPTION_TEXT_LARGE;
    options.background = JC_CAPTION_BACKGROUND_NONE;
    options.anchor = JC_CAPTION_ANCHOR_TOP;
    options.vertical_margin = 0u;
    assert(!jc_caption_render(pixels, 8u, 8u, 20u, "I", 1u,
                              &options, &result));

    fill(large_pixels, 40u * 32u, 0u);
    options.anchor = JC_CAPTION_ANCHOR_CENTER;
    assert(jc_caption_render(large_pixels, 40u, 32u, 40u, "I", 1u,
                             &options, &result));
    assert(result.x == 12u && result.y == 5u);
    assert(result.width == 15u && result.height == 21u);
    assert(result.foreground_pixels > 0u);
}

static void wrapping_and_newlines(void)
{
    uint32_t pixels[35u * 32u];
    jc_caption_render_options_t options;
    jc_caption_render_result_t result;
    static const char word_wrap[] = "AAAA BBBB";
    static const char hard_wrap[] = "AAAAAA";
    static const char blank_line[] = "A\n\nB";

    fill(pixels, 35u * 32u, 0u);
    jc_caption_render_options_init(&options);
    options.text_size = JC_CAPTION_TEXT_SMALL;
    options.background = JC_CAPTION_BACKGROUND_NONE;
    options.anchor = JC_CAPTION_ANCHOR_TOP;
    options.horizontal_margin = 0u;
    options.vertical_margin = 0u;
    options.max_text_width = 29u;
    assert(jc_caption_render(pixels, 35u, 32u, 35u,
                             word_wrap, sizeof(word_wrap) - 1u,
                             &options, &result));
    assert(result.line_count == 2u && result.consumed_bytes == 9u);
    assert(result.width == 23u && result.height == 15u);
    assert(!result.truncated && !result.clipped);

    fill(pixels, 35u * 32u, 0u);
    assert(jc_caption_render(pixels, 35u, 32u, 35u,
                             hard_wrap, sizeof(hard_wrap) - 1u,
                             &options, &result));
    assert(result.line_count == 2u && result.consumed_bytes == 6u);
    assert(result.width == 29u && result.height == 15u);

    fill(pixels, 35u * 32u, 0u);
    assert(jc_caption_render(pixels, 35u, 32u, 35u,
                             blank_line, sizeof(blank_line) - 1u,
                             &options, &result));
    assert(result.line_count == 3u && result.consumed_bytes == 4u);
    assert(result.height == 23u);
}

static void bounded_input_replacement_and_truncation(void)
{
    static uint32_t pixels[64u * 256u];
    jc_caption_render_options_t options;
    jc_caption_render_result_t result;
    const char bounded[4] = {'A', 'B', 'C', 'X'};
    const char unsupported[1] = {(char)0xff};
    char many_lines[80];
    size_t index;

    jc_caption_render_options_init(&options);
    options.text_size = JC_CAPTION_TEXT_SMALL;
    options.background = JC_CAPTION_BACKGROUND_NONE;
    options.anchor = JC_CAPTION_ANCHOR_TOP;
    options.horizontal_margin = 0u;
    options.vertical_margin = 0u;
    fill(pixels, 64u * 256u, 0u);
    assert(jc_caption_render(pixels, 64u, 256u, 64u, bounded, 3u,
                             &options, &result));
    assert(result.consumed_bytes == 3u && result.line_count == 1u);
    assert(result.width == 17u);

    fill(pixels, 64u * 256u, 0u);
    assert(jc_caption_render(pixels, 64u, 256u, 64u, unsupported, 1u,
                             &options, &result));
    assert(result.replacement_count == 1u);
    assert(result.foreground_pixels > 0u);
    assert(!jc_caption_glyph_supported(0xffu));

    for (index = 0u; index < sizeof(many_lines); index += 2u) {
        many_lines[index] = 'A';
        many_lines[index + 1u] = '\n';
    }
    fill(pixels, 64u * 256u, 0u);
    assert(jc_caption_render(pixels, 64u, 256u, 64u,
                             many_lines, sizeof(many_lines),
                             &options, &result));
    assert(result.line_count == JC_CAPTION_RENDER_MAX_LINES);
    assert(result.consumed_bytes == 64u);
    assert(result.truncated && !result.clipped);
}

static void clipping_stride_and_guards(void)
{
    uint32_t pixels[10u * 8u];
    jc_caption_render_options_t options;
    jc_caption_render_result_t result;
    size_t row;

    fill(pixels, 10u * 8u, 0x00555555u);
    jc_caption_render_options_init(&options);
    options.text_size = JC_CAPTION_TEXT_SMALL;
    options.background = JC_CAPTION_BACKGROUND_NONE;
    options.anchor = JC_CAPTION_ANCHOR_TOP;
    options.horizontal_margin = 0u;
    options.vertical_margin = 0u;
    assert(jc_caption_render(pixels, 8u, 8u, 10u, "A", 1u,
                             &options, &result));
    for (row = 0u; row < 8u; ++row) {
        assert(pixels[row * 10u + 8u] == 0x00555555u);
        assert(pixels[row * 10u + 9u] == 0x00555555u);
    }

    fill(pixels, 10u * 8u, 0u);
    options.anchor = JC_CAPTION_ANCHOR_BOTTOM;
    assert(jc_caption_render(pixels, 8u, 4u, 10u, "A", 1u,
                             &options, &result));
    assert(result.clipped && result.y == 0u && result.height == 4u);

    assert(!jc_caption_render(NULL, 8u, 8u, 8u, "A", 1u,
                              &options, &result));
    assert(!jc_caption_render(pixels, 8u, 8u, 7u, "A", 1u,
                              &options, &result));
    assert(!jc_caption_render(pixels, 8u, 8u, 8u, NULL, 1u,
                              &options, &result));
    options.text_size = (jc_caption_text_size_t)99;
    assert(!jc_caption_render(pixels, 8u, 8u, 8u, "A", 1u,
                              &options, &result));
}

static void complete_catalog_and_determinism(void)
{
    jc_caption_render_options_t options;
    size_t caption_index;
    uint64_t first_hash;
    uint64_t second_hash;

    jc_caption_render_options_init(&options);
    for (caption_index = 0u; caption_index < jc_caption_count();
         ++caption_index) {
        const jc_caption_entry_t *entry = jc_caption_at(caption_index);
        const unsigned char *p = (const unsigned char *)entry->text;
        jc_caption_render_result_t result;
        while (*p != '\0') {
            assert(jc_caption_glyph_supported(*p));
            ++p;
        }
        fill(scene_a, SCENE_WIDTH * SCENE_HEIGHT, 0x00406080u);
        assert(jc_caption_render(scene_a, SCENE_WIDTH, SCENE_HEIGHT,
                                 SCENE_WIDTH, entry->text,
                                 strlen(entry->text), &options, &result));
        assert(!result.truncated && !result.clipped);
        assert(result.replacement_count == 0u);
        if (entry->text[0] != '\0')
            assert(result.line_count >= 1u && result.foreground_pixels > 0u);
    }

    fill(scene_a, SCENE_WIDTH * SCENE_HEIGHT, 0x00123456u);
    fill(scene_b, SCENE_WIDTH * SCENE_HEIGHT, 0x00123456u);
    assert(jc_caption_render(scene_a, SCENE_WIDTH, SCENE_HEIGHT,
                             SCENE_WIDTH, jc_caption_lookup("scene32")->text,
                             strlen(jc_caption_lookup("scene32")->text),
                             NULL, NULL));
    assert(jc_caption_render(scene_b, SCENE_WIDTH, SCENE_HEIGHT,
                             SCENE_WIDTH, jc_caption_lookup("scene32")->text,
                             strlen(jc_caption_lookup("scene32")->text),
                             NULL, NULL));
    first_hash = hash_pixels(scene_a, SCENE_WIDTH * SCENE_HEIGHT);
    second_hash = hash_pixels(scene_b, SCENE_WIDTH * SCENE_HEIGHT);
    assert(first_hash == second_hash);
    assert(memcmp(scene_a, scene_b, sizeof(scene_a)) == 0);
}

static uint32_t stress_next(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static void guarded_surface_stress(void)
{
    enum { GUARD = 4, MAX_STRIDE = 36, MAX_HEIGHT = 24 };
    uint32_t storage[GUARD + MAX_STRIDE * MAX_HEIGHT + GUARD];
    static const unsigned char alphabet[] = {
        'A', 'b', ' ', '9', '.', ',', '!', '?', '-', '\n', '\r', '\t', 0xffu
    };
    char text[64];
    uint32_t random = 0x4a435253u;
    unsigned trial;

    for (trial = 0u; trial < 500u; ++trial) {
        jc_caption_render_options_t options;
        jc_caption_render_result_t result;
        size_t width = stress_next(&random) % 32u + 1u;
        size_t height = stress_next(&random) % MAX_HEIGHT + 1u;
        size_t stride = width + stress_next(&random) % 5u;
        size_t text_length = stress_next(&random) % sizeof(text);
        size_t index;
        size_t row;

        assert(stride <= MAX_STRIDE);
        fill(storage, sizeof(storage) / sizeof(storage[0]), 0x00c0ffeeu);
        for (index = 0u; index < text_length; ++index) {
            text[index] = (char)alphabet[stress_next(&random) %
                (sizeof(alphabet) / sizeof(alphabet[0]))];
        }
        jc_caption_render_options_init(&options);
        options.text_size = (jc_caption_text_size_t)(stress_next(&random) % 3u);
        options.background = (jc_caption_background_t)(stress_next(&random) % 3u);
        options.anchor = (jc_caption_anchor_t)(stress_next(&random) % 3u);
        options.horizontal_margin = (uint16_t)(stress_next(&random) % 10u);
        options.vertical_margin = (uint16_t)(stress_next(&random) % 10u);
        options.padding = (uint16_t)(stress_next(&random) % 8u);
        options.max_text_width = (uint16_t)(stress_next(&random) % 40u);
        options.background_opacity = (uint8_t)stress_next(&random);
        (void)jc_caption_render(storage + GUARD, width, height, stride,
                                text, text_length, &options, &result);
        for (index = 0u; index < GUARD; ++index) {
            assert(storage[index] == 0x00c0ffeeu);
            assert(storage[GUARD + stride * height + index] == 0x00c0ffeeu);
        }
        for (row = 0u; row < height; ++row) {
            for (index = width; index < stride; ++index) {
                assert(storage[GUARD + row * stride + index] == 0x00c0ffeeu);
            }
        }
    }
}

int main(void)
{
    transparent_a_pixels();
    opacity_box_pixels();
    bottom_bar_and_sizes();
    wrapping_and_newlines();
    bounded_input_replacement_and_truncation();
    clipping_stride_and_guards();
    complete_catalog_and_determinism();
    guarded_surface_stress();
    puts("caption renderer tests passed");
    return 0;
}
