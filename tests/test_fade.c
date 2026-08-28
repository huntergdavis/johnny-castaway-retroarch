/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "jc_fade.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_WIDTH 640u
#define TEST_HEIGHT 480u
#define TEST_PIXELS ((size_t)TEST_WIDTH * TEST_HEIGHT)

static uint32_t pixels[TEST_PIXELS];
static uint32_t inverse[TEST_PIXELS];

static size_t count_color(const uint32_t *frame, size_t count,
                          uint32_t color)
{
    size_t found = 0u;
    size_t index;

    for (index = 0u; index < count; ++index) {
        if (frame[index] == color)
            ++found;
    }
    return found;
}

static void test_defaults_and_lifecycle(void)
{
    jc_fade_t fade;
    uint32_t index;

    jc_fade_init(&fade);
    assert(!jc_fade_is_active(&fade));
    assert(jc_fade_default_steps(JC_FADE_IRIS) == 20u);
    assert(jc_fade_default_steps(JC_FADE_BOX) == 20u);
    assert(jc_fade_default_steps(JC_FADE_WIPE_RIGHT_TO_LEFT) == 16u);
    assert(jc_fade_default_steps(JC_FADE_WIPE_LEFT_TO_RIGHT) == 16u);
    assert(jc_fade_default_steps(JC_FADE_SPLIT) == 16u);
    assert(jc_fade_default_steps(JC_FADE_STYLE_COUNT) == 0u);
    for (index = 0u; index < 12u; ++index)
        assert(jc_fade_style_for_sequence(index) ==
               (jc_fade_style_t)(index % 5u));

    assert(!jc_fade_begin(NULL, JC_FADE_OUT, JC_FADE_IRIS));
    assert(!jc_fade_begin(&fade, JC_FADE_DIRECTION_COUNT, JC_FADE_IRIS));
    assert(!jc_fade_begin(&fade, JC_FADE_OUT, JC_FADE_STYLE_COUNT));
    assert(!jc_fade_begin_steps(&fade, JC_FADE_OUT, JC_FADE_IRIS, 0u));
    assert(!jc_fade_begin_steps(&fade, JC_FADE_OUT, JC_FADE_IRIS,
                                JC_FADE_MAX_STEPS + 1u));
    assert(jc_fade_begin_steps(&fade, JC_FADE_OUT, JC_FADE_BOX, 4u));
    assert(jc_fade_is_active(&fade) && fade.step == 1u);
    jc_fade_advance(&fade);
    jc_fade_advance(&fade);
    jc_fade_advance(&fade);
    assert(jc_fade_is_active(&fade) && fade.step == 4u);
    jc_fade_advance(&fade);
    assert(!jc_fade_is_active(&fade));
}

static void test_oracle_first_steps(void)
{
    static const struct expected {
        jc_fade_style_t style;
        size_t black_pixels;
    } expectations[] = {
        { JC_FADE_BOX, 32u * 24u },
        { JC_FADE_WIPE_RIGHT_TO_LEFT, 40u * TEST_HEIGHT },
        { JC_FADE_WIPE_LEFT_TO_RIGHT, 40u * TEST_HEIGHT },
        { JC_FADE_SPLIT, 40u * TEST_HEIGHT }
    };
    size_t index;

    for (index = 0u; index < sizeof(expectations) / sizeof(expectations[0]);
         ++index) {
        jc_fade_t fade;

        memset(pixels, 0xff, sizeof(pixels));
        assert(jc_fade_begin(&fade, JC_FADE_OUT, expectations[index].style));
        assert(jc_fade_apply(&fade, pixels, TEST_WIDTH, TEST_HEIGHT,
                             TEST_WIDTH, 0u));
        assert(count_color(pixels, TEST_PIXELS, 0u) ==
               expectations[index].black_pixels);
    }

    memset(pixels, 0xff, sizeof(pixels));
    {
        jc_fade_t fade;
        assert(jc_fade_begin(&fade, JC_FADE_OUT, JC_FADE_IRIS));
        assert(jc_fade_apply(&fade, pixels, TEST_WIDTH, TEST_HEIGHT,
                             TEST_WIDTH, 0u));
    }
    assert(pixels[(TEST_HEIGHT / 2u) * TEST_WIDTH + TEST_WIDTH / 2u] == 0u);
    assert(pixels[0] != 0u && pixels[TEST_PIXELS - 1u] != 0u);
}

static void test_out_in_are_complements(void)
{
    int style_value;

    for (style_value = (int)JC_FADE_IRIS;
         style_value < (int)JC_FADE_STYLE_COUNT; ++style_value) {
        jc_fade_t out;
        jc_fade_t in;
        uint32_t step;

        assert(jc_fade_begin_steps(&out, JC_FADE_OUT,
                                   (jc_fade_style_t)style_value, 4u));
        assert(jc_fade_begin_steps(&in, JC_FADE_IN,
                                   (jc_fade_style_t)style_value, 4u));
        for (step = 1u; step <= 4u; ++step) {
            size_t out_count;
            size_t in_count;

            memset(pixels, 0xff, sizeof(pixels));
            memset(inverse, 0xff, sizeof(inverse));
            assert(jc_fade_apply(&out, pixels, TEST_WIDTH, TEST_HEIGHT,
                                 TEST_WIDTH, 0u));
            assert(jc_fade_apply(&in, inverse, TEST_WIDTH, TEST_HEIGHT,
                                 TEST_WIDTH, 0u));
            out_count = count_color(pixels, TEST_PIXELS, 0u);
            in_count = count_color(inverse, TEST_PIXELS, 0u);
            assert(out_count + in_count == TEST_PIXELS);
            if (step == 4u) {
                assert(out_count == TEST_PIXELS);
                assert(in_count == 0u);
            }
            jc_fade_advance(&out);
            jc_fade_advance(&in);
        }
        assert(!jc_fade_is_active(&out) && !jc_fade_is_active(&in));
    }
}

static void test_pitch_and_validation(void)
{
    jc_fade_t fade;
    uint32_t padded[18];
    size_t index;

    for (index = 0u; index < sizeof(padded) / sizeof(padded[0]); ++index)
        padded[index] = 0x12345678u;
    assert(jc_fade_begin_steps(&fade, JC_FADE_OUT,
                               JC_FADE_WIPE_LEFT_TO_RIGHT, 1u));
    assert(jc_fade_apply(&fade, padded, 4u, 3u, 6u, 0x00abcdefu));
    for (index = 0u; index < 18u; ++index) {
        if (index % 6u < 4u)
            assert(padded[index] == 0x00abcdefu);
        else
            assert(padded[index] == 0x12345678u);
    }
    assert(!jc_fade_apply(&fade, NULL, 4u, 3u, 6u, 0u));
    assert(!jc_fade_apply(&fade, padded, 0u, 3u, 6u, 0u));
    assert(!jc_fade_apply(&fade, padded, 4u, 0u, 6u, 0u));
    assert(!jc_fade_apply(&fade, padded, 4u, 3u, 3u, 0u));
    if (UINT_MAX > (unsigned)INT_MAX)
        assert(!jc_fade_apply(&fade, padded, UINT_MAX, 3u, UINT_MAX, 0u));

    jc_fade_advance(&fade);
    assert(!jc_fade_is_active(&fade));
    assert(jc_fade_apply(&fade, padded, 4u, 3u, 6u, 0u));
}

int main(void)
{
    test_defaults_and_lifecycle();
    test_oracle_first_steps();
    test_out_in_are_complements();
    test_pitch_and_validation();
    puts("scene fade tests passed");
    return 0;
}
