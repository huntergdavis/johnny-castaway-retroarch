/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef JC_CAPTION_RENDER_H
#define JC_CAPTION_RENDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JC_CAPTION_RENDER_MAX_LINES 32u

typedef enum jc_caption_text_size {
    JC_CAPTION_TEXT_SMALL = 0,
    JC_CAPTION_TEXT_MEDIUM,
    JC_CAPTION_TEXT_LARGE
} jc_caption_text_size_t;

typedef enum jc_caption_background {
    JC_CAPTION_BACKGROUND_NONE = 0,
    JC_CAPTION_BACKGROUND_BOX,
    JC_CAPTION_BACKGROUND_BAR
} jc_caption_background_t;

typedef enum jc_caption_anchor {
    JC_CAPTION_ANCHOR_TOP = 0,
    JC_CAPTION_ANCHOR_CENTER,
    JC_CAPTION_ANCHOR_BOTTOM
} jc_caption_anchor_t;

typedef struct jc_caption_render_options {
    jc_caption_text_size_t text_size;
    jc_caption_background_t background;
    jc_caption_anchor_t anchor;
    uint32_t foreground_xrgb;
    uint32_t background_xrgb;
    uint16_t horizontal_margin;
    uint16_t vertical_margin;
    uint16_t padding;
    uint16_t max_text_width;
    uint8_t background_opacity;
} jc_caption_render_options_t;

typedef struct jc_caption_render_result {
    size_t consumed_bytes;
    size_t line_count;
    size_t replacement_count;
    size_t foreground_pixels;
    unsigned x;
    unsigned y;
    unsigned width;
    unsigned height;
    bool truncated;
    bool clipped;
} jc_caption_render_result_t;

void jc_caption_render_options_init(jc_caption_render_options_t *options);

/*
 * Draw bounded ASCII caption text over an XRGB8888 framebuffer. Stride is in
 * pixels, not bytes. The renderer never allocates and never reads beyond
 * text_length. Printable glyphs are drawn in an embedded 5x7 font; unsupported
 * bytes use a visible replacement box. Existing alpha bits are ignored and
 * output pixels are normalized to 0x00RRGGBB.
 */
bool jc_caption_render(uint32_t *pixels, size_t width, size_t height,
                       size_t stride, const char *text, size_t text_length,
                       const jc_caption_render_options_t *options,
                       jc_caption_render_result_t *result);

bool jc_caption_glyph_supported(unsigned char character);

#endif
