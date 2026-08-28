/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Portable caption presentation for the libretro framebuffer.
 *
 * Presentation policy was informed by Hunter Davis's PS1 caption renderer at
 * https://github.com/huntergdavis/johnny-castaway-ps1, commit 795539901:
 * centered lines, bottom placement, and a dark backing band. No PS1 GPU code,
 * font atlas, coordinates, or media are copied. The small 5x7 glyph patterns
 * below were independently encoded from conventional block-letter shapes.
 */
#include "jc_caption_render.h"

#include <limits.h>
#include <string.h>

typedef struct caption_line {
    size_t begin;
    size_t end;
    size_t columns;
} caption_line_t;

static void set_rows(uint8_t rows[7], uint8_t a, uint8_t b, uint8_t c,
                     uint8_t d, uint8_t e, uint8_t f, uint8_t g)
{
    rows[0] = a;
    rows[1] = b;
    rows[2] = c;
    rows[3] = d;
    rows[4] = e;
    rows[5] = f;
    rows[6] = g;
}
static bool glyph_rows(unsigned char character, uint8_t rows[7])
{
    unsigned char c = character;
    if (c >= 'a' && c <= 'z')
        c = (unsigned char)(c - 'a' + 'A');
    memset(rows, 0, 7u);
    switch (c) {
    case ' ': return true;
    case 'A': set_rows(rows, 14, 17, 17, 31, 17, 17, 17); return true;
    case 'B': set_rows(rows, 30, 17, 17, 30, 17, 17, 30); return true;
    case 'C': set_rows(rows, 14, 17, 16, 16, 16, 17, 14); return true;
    case 'D': set_rows(rows, 30, 17, 17, 17, 17, 17, 30); return true;
    case 'E': set_rows(rows, 31, 16, 16, 30, 16, 16, 31); return true;
    case 'F': set_rows(rows, 31, 16, 16, 30, 16, 16, 16); return true;
    case 'G': set_rows(rows, 14, 17, 16, 23, 17, 17, 14); return true;
    case 'H': set_rows(rows, 17, 17, 17, 31, 17, 17, 17); return true;
    case 'I': set_rows(rows, 14, 4, 4, 4, 4, 4, 14); return true;
    case 'J': set_rows(rows, 7, 2, 2, 2, 18, 18, 12); return true;
    case 'K': set_rows(rows, 17, 18, 20, 24, 20, 18, 17); return true;
    case 'L': set_rows(rows, 16, 16, 16, 16, 16, 16, 31); return true;
    case 'M': set_rows(rows, 17, 27, 21, 21, 17, 17, 17); return true;
    case 'N': set_rows(rows, 17, 25, 21, 19, 17, 17, 17); return true;
    case 'O': set_rows(rows, 14, 17, 17, 17, 17, 17, 14); return true;
    case 'P': set_rows(rows, 30, 17, 17, 30, 16, 16, 16); return true;
    case 'Q': set_rows(rows, 14, 17, 17, 17, 21, 18, 13); return true;
    case 'R': set_rows(rows, 30, 17, 17, 30, 20, 18, 17); return true;
    case 'S': set_rows(rows, 15, 16, 16, 14, 1, 1, 30); return true;
    case 'T': set_rows(rows, 31, 4, 4, 4, 4, 4, 4); return true;
    case 'U': set_rows(rows, 17, 17, 17, 17, 17, 17, 14); return true;
    case 'V': set_rows(rows, 17, 17, 17, 17, 17, 10, 4); return true;
    case 'W': set_rows(rows, 17, 17, 17, 21, 21, 21, 10); return true;
    case 'X': set_rows(rows, 17, 17, 10, 4, 10, 17, 17); return true;
    case 'Y': set_rows(rows, 17, 17, 10, 4, 4, 4, 4); return true;
    case 'Z': set_rows(rows, 31, 1, 2, 4, 8, 16, 31); return true;
    case '0': set_rows(rows, 14, 17, 19, 21, 25, 17, 14); return true;
    case '1': set_rows(rows, 4, 12, 4, 4, 4, 4, 14); return true;
    case '2': set_rows(rows, 14, 17, 1, 2, 4, 8, 31); return true;
    case '3': set_rows(rows, 30, 1, 1, 14, 1, 1, 30); return true;
    case '4': set_rows(rows, 2, 6, 10, 18, 31, 2, 2); return true;
    case '5': set_rows(rows, 31, 16, 16, 30, 1, 1, 30); return true;
    case '6': set_rows(rows, 14, 16, 16, 30, 17, 17, 14); return true;
    case '7': set_rows(rows, 31, 1, 2, 4, 8, 8, 8); return true;
    case '8': set_rows(rows, 14, 17, 17, 14, 17, 17, 14); return true;
    case '9': set_rows(rows, 14, 17, 17, 15, 1, 1, 14); return true;
    case '.': set_rows(rows, 0, 0, 0, 0, 0, 6, 6); return true;
    case ',': set_rows(rows, 0, 0, 0, 0, 6, 6, 4); return true;
    case '\'': set_rows(rows, 4, 4, 2, 0, 0, 0, 0); return true;
    case '"': set_rows(rows, 10, 10, 5, 0, 0, 0, 0); return true;
    case '!': set_rows(rows, 4, 4, 4, 4, 4, 0, 4); return true;
    case '?': set_rows(rows, 14, 17, 1, 2, 4, 0, 4); return true;
    case '-': set_rows(rows, 0, 0, 0, 14, 0, 0, 0); return true;
    case ':': set_rows(rows, 0, 4, 4, 0, 4, 4, 0); return true;
    case ';': set_rows(rows, 0, 6, 6, 0, 6, 6, 4); return true;
    case '/': set_rows(rows, 1, 2, 2, 4, 8, 8, 16); return true;
    case '\\': set_rows(rows, 16, 8, 8, 4, 2, 2, 1); return true;
    case '(': set_rows(rows, 2, 4, 8, 8, 8, 4, 2); return true;
    case ')': set_rows(rows, 8, 4, 2, 2, 2, 4, 8); return true;
    case '+': set_rows(rows, 0, 4, 4, 31, 4, 4, 0); return true;
    case '=': set_rows(rows, 0, 0, 31, 0, 31, 0, 0); return true;
    case '&': set_rows(rows, 12, 18, 20, 8, 21, 18, 13); return true;
    case '%': set_rows(rows, 25, 26, 2, 4, 8, 11, 19); return true;
    case '#': set_rows(rows, 10, 31, 10, 10, 31, 10, 0); return true;
    case '_': set_rows(rows, 0, 0, 0, 0, 0, 0, 31); return true;
    case '*': set_rows(rows, 0, 21, 14, 31, 14, 21, 0); return true;
    case '[': set_rows(rows, 14, 8, 8, 8, 8, 8, 14); return true;
    case ']': set_rows(rows, 14, 2, 2, 2, 2, 2, 14); return true;
    case '<': set_rows(rows, 2, 4, 8, 16, 8, 4, 2); return true;
    case '>': set_rows(rows, 8, 4, 2, 1, 2, 4, 8); return true;
    case '$': set_rows(rows, 4, 15, 20, 14, 5, 30, 4); return true;
    case '@': set_rows(rows, 14, 17, 23, 21, 23, 16, 14); return true;
    default: return false;
    }
}

bool jc_caption_glyph_supported(unsigned char character)
{
    uint8_t rows[7];
    if (character == '\n' || character == '\r' || character == '\t')
        return true;
    return glyph_rows(character, rows);
}

static void replacement_rows(uint8_t rows[7])
{
    set_rows(rows, 31, 17, 21, 21, 21, 17, 31);
}

void jc_caption_render_options_init(jc_caption_render_options_t *options)
{
    if (options == NULL)
        return;
    options->text_size = JC_CAPTION_TEXT_MEDIUM;
    options->background = JC_CAPTION_BACKGROUND_BAR;
    options->anchor = JC_CAPTION_ANCHOR_BOTTOM;
    options->foreground_xrgb = 0x00ffffffu;
    options->background_xrgb = 0x00000000u;
    options->horizontal_margin = 40u;
    options->vertical_margin = 10u;
    options->padding = 8u;
    options->max_text_width = 0u;
    options->background_opacity = 160u;
}

static bool is_horizontal_space(unsigned char c)
{
    return c == ' ' || c == '\t';
}

static size_t column_count(const char *text, size_t begin, size_t end)
{
    size_t count = 0u;
    size_t index;
    for (index = begin; index < end; ++index) {
        if ((unsigned char)text[index] != '\r')
            ++count;
    }
    return count;
}

static size_t trim_line_end(const char *text, size_t begin, size_t end)
{
    while (end > begin) {
        unsigned char c = (unsigned char)text[end - 1u];
        if (!is_horizontal_space(c) && c != '\r')
            break;
        --end;
    }
    return end;
}

static size_t layout_lines(const char *text, size_t length, size_t max_columns,
                           caption_line_t lines[JC_CAPTION_RENDER_MAX_LINES],
                           size_t *consumed, bool *truncated)
{
    size_t line_count = 0u;
    size_t position = 0u;

    while (position < length && line_count < JC_CAPTION_RENDER_MAX_LINES) {
        size_t begin;
        size_t scan;
        size_t columns = 0u;
        size_t last_space = SIZE_MAX;
        size_t end = position;
        size_t next = position;

        while (position < length && is_horizontal_space((unsigned char)text[position]))
            ++position;
        if (position < length && text[position] == '\n') {
            lines[line_count].begin = position;
            lines[line_count].end = position;
            lines[line_count].columns = 0u;
            ++line_count;
            ++position;
            continue;
        }
        if (position >= length)
            break;

        begin = position;
        scan = position;
        while (scan < length) {
            unsigned char c = (unsigned char)text[scan];
            if (c == '\n') {
                end = scan;
                next = scan + 1u;
                break;
            }
            if (c == '\r') {
                ++scan;
                continue;
            }
            if (columns == max_columns) {
                if (last_space != SIZE_MAX) {
                    end = last_space;
                    next = last_space + 1u;
                    while (next < length &&
                           is_horizontal_space((unsigned char)text[next]))
                        ++next;
                } else {
                    end = scan;
                    next = scan;
                }
                break;
            }
            if (is_horizontal_space(c))
                last_space = scan;
            ++columns;
            ++scan;
        }
        if (scan == length) {
            end = length;
            next = length;
        }
        end = trim_line_end(text, begin, end);
        lines[line_count].begin = begin;
        lines[line_count].end = end;
        lines[line_count].columns = column_count(text, begin, end);
        ++line_count;
        if (next <= position)
            break;
        position = next;
    }

    *consumed = position;
    *truncated = position < length;
    return line_count;
}

static uint32_t blend_xrgb(uint32_t destination, uint32_t source,
                           unsigned opacity)
{
    unsigned inverse = 255u - opacity;
    unsigned dr = (destination >> 16) & 255u;
    unsigned dg = (destination >> 8) & 255u;
    unsigned db = destination & 255u;
    unsigned sr = (source >> 16) & 255u;
    unsigned sg = (source >> 8) & 255u;
    unsigned sb = source & 255u;
    unsigned r = (sr * opacity + dr * inverse + 127u) / 255u;
    unsigned g = (sg * opacity + dg * inverse + 127u) / 255u;
    unsigned b = (sb * opacity + db * inverse + 127u) / 255u;
    return (uint32_t)((r << 16) | (g << 8) | b);
}

static void fill_backdrop(uint32_t *pixels, size_t width, size_t height,
                          size_t stride, int x, int y, unsigned rect_width,
                          unsigned rect_height, uint32_t color,
                          unsigned opacity, bool *clipped)
{
    int x0 = x;
    int y0 = y;
    int x1;
    int y1;
    int row;
    int column;
    if (rect_width > (unsigned)INT_MAX || rect_height > (unsigned)INT_MAX)
        return;
    x1 = x + (int)rect_width;
    y1 = y + (int)rect_height;
    if (x0 < 0) {
        x0 = 0;
        *clipped = true;
    }
    if (y0 < 0) {
        y0 = 0;
        *clipped = true;
    }
    if (x1 > (int)width) {
        x1 = (int)width;
        *clipped = true;
    }
    if (y1 > (int)height) {
        y1 = (int)height;
        *clipped = true;
    }
    if (x0 >= x1 || y0 >= y1) {
        *clipped = true;
        return;
    }
    if (opacity == 0u)
        return;
    for (row = y0; row < y1; ++row) {
        uint32_t *destination = pixels + (size_t)row * stride;
        for (column = x0; column < x1; ++column) {
            destination[column] = opacity == 255u ? color & 0x00ffffffu :
                blend_xrgb(destination[column], color, opacity);
        }
    }
}

static void draw_glyph(uint32_t *pixels, size_t width, size_t height,
                       size_t stride, int x, int y, unsigned scale,
                       const uint8_t rows[7], uint32_t color,
                       size_t *foreground_pixels, bool *clipped)
{
    unsigned glyph_y;
    for (glyph_y = 0u; glyph_y < 7u; ++glyph_y) {
        unsigned glyph_x;
        for (glyph_x = 0u; glyph_x < 5u; ++glyph_x) {
            unsigned scaled_y;
            if ((rows[glyph_y] & (uint8_t)(1u << (4u - glyph_x))) == 0u)
                continue;
            for (scaled_y = 0u; scaled_y < scale; ++scaled_y) {
                unsigned scaled_x;
                int destination_y = y + (int)(glyph_y * scale + scaled_y);
                for (scaled_x = 0u; scaled_x < scale; ++scaled_x) {
                    int destination_x = x + (int)(glyph_x * scale + scaled_x);
                    if (destination_x < 0 || destination_y < 0 ||
                        destination_x >= (int)width ||
                        destination_y >= (int)height) {
                        *clipped = true;
                        continue;
                    }
                    pixels[(size_t)destination_y * stride +
                           (size_t)destination_x] = color & 0x00ffffffu;
                    ++*foreground_pixels;
                }
            }
        }
    }
}

static unsigned clipped_coordinate(int value, size_t limit)
{
    if (value <= 0)
        return 0u;
    if ((size_t)value >= limit)
        return (unsigned)limit;
    return (unsigned)value;
}

bool jc_caption_render(uint32_t *pixels, size_t width, size_t height,
                       size_t stride, const char *text, size_t text_length,
                       const jc_caption_render_options_t *options,
                       jc_caption_render_result_t *result)
{
    caption_line_t lines[JC_CAPTION_RENDER_MAX_LINES];
    jc_caption_render_options_t defaults;
    jc_caption_render_result_t local_result;
    const jc_caption_render_options_t *active = options;
    size_t available_width;
    size_t wrap_width;
    size_t max_columns;
    size_t line_count;
    size_t max_line_width = 0u;
    size_t index;
    unsigned scale;
    unsigned cell_width;
    unsigned line_step;
    unsigned glyph_height;
    unsigned text_height;
    unsigned padding;
    unsigned block_width;
    unsigned block_height;
    int block_x;
    int block_y;
    int text_y;

    memset(&local_result, 0, sizeof(local_result));
    if (result != NULL)
        memset(result, 0, sizeof(*result));
    if (pixels == NULL || width == 0u || height == 0u || stride < width ||
        stride > SIZE_MAX / height || width > (size_t)INT_MAX ||
        height > (size_t)INT_MAX || (text == NULL && text_length != 0u))
        return false;
    if (active == NULL) {
        jc_caption_render_options_init(&defaults);
        active = &defaults;
    }
    if (active->text_size > JC_CAPTION_TEXT_LARGE ||
        active->background > JC_CAPTION_BACKGROUND_BAR ||
        active->anchor > JC_CAPTION_ANCHOR_BOTTOM)
        return false;
    if (text_length == 0u) {
        if (result != NULL)
            *result = local_result;
        return true;
    }

    scale = (unsigned)active->text_size + 1u;
    cell_width = 6u * scale;
    line_step = 8u * scale;
    glyph_height = 7u * scale;
    padding = active->background == JC_CAPTION_BACKGROUND_NONE ?
        0u : active->padding;
    if ((size_t)active->horizontal_margin + padding > width / 2u)
        return false;
    available_width = width -
        2u * ((size_t)active->horizontal_margin + padding);
    wrap_width = active->max_text_width != 0u &&
        active->max_text_width < available_width ?
        active->max_text_width : available_width;
    max_columns = (wrap_width + scale) / cell_width;
    if (max_columns == 0u)
        return false;

    line_count = layout_lines(text, text_length, max_columns, lines,
                              &local_result.consumed_bytes,
                              &local_result.truncated);
    local_result.line_count = line_count;
    if (line_count == 0u) {
        if (result != NULL)
            *result = local_result;
        return true;
    }
    for (index = 0u; index < line_count; ++index) {
        size_t line_width = lines[index].columns == 0u ? 0u :
            lines[index].columns * cell_width - scale;
        if (line_width > max_line_width)
            max_line_width = line_width;
    }
    if (max_line_width > UINT_MAX ||
        (line_count - 1u) > (UINT_MAX - glyph_height) / line_step)
        return false;
    text_height = (unsigned)((line_count - 1u) * line_step + glyph_height);
    if (active->background == JC_CAPTION_BACKGROUND_BAR)
        block_width = (unsigned)width;
    else if (max_line_width > UINT_MAX - 2u * padding)
        return false;
    else
        block_width = (unsigned)max_line_width + 2u * padding;
    if (text_height > UINT_MAX - 2u * padding)
        return false;
    block_height = text_height + 2u * padding;
    block_x = active->background == JC_CAPTION_BACKGROUND_BAR ? 0 :
        ((int)width - (int)block_width) / 2;
    switch (active->anchor) {
    case JC_CAPTION_ANCHOR_TOP:
        block_y = active->vertical_margin;
        break;
    case JC_CAPTION_ANCHOR_CENTER:
        block_y = ((int)height - (int)block_height) / 2;
        break;
    case JC_CAPTION_ANCHOR_BOTTOM:
    default:
        block_y = (int)height - (int)active->vertical_margin -
            (int)block_height;
        break;
    }
    text_y = block_y + (int)padding;

    if (active->background != JC_CAPTION_BACKGROUND_NONE) {
        fill_backdrop(pixels, width, height, stride, block_x, block_y,
                      block_width, block_height, active->background_xrgb,
                      active->background_opacity, &local_result.clipped);
    }
    for (index = 0u; index < line_count; ++index) {
        size_t byte_index;
        size_t column = 0u;
        size_t line_width = lines[index].columns == 0u ? 0u :
            lines[index].columns * cell_width - scale;
        int x = ((int)width - (int)line_width) / 2;
        int y = text_y + (int)(index * line_step);
        for (byte_index = lines[index].begin;
             byte_index < lines[index].end; ++byte_index) {
            unsigned char c = (unsigned char)text[byte_index];
            uint8_t rows[7];
            bool supported;
            if (c == '\r')
                continue;
            if (c == '\t')
                c = ' ';
            supported = glyph_rows(c, rows);
            if (!supported) {
                replacement_rows(rows);
                ++local_result.replacement_count;
            }
            if (c != ' ' || !supported) {
                draw_glyph(pixels, width, height, stride,
                           x + (int)(column * cell_width), y, scale, rows,
                           active->foreground_xrgb,
                           &local_result.foreground_pixels,
                           &local_result.clipped);
            }
            ++column;
        }
    }

    local_result.x = clipped_coordinate(block_x, width);
    local_result.y = clipped_coordinate(block_y, height);
    {
        int right = block_x + (int)block_width;
        int bottom = block_y + (int)block_height;
        unsigned clipped_right = clipped_coordinate(right, width);
        unsigned clipped_bottom = clipped_coordinate(bottom, height);
        local_result.width = clipped_right > local_result.x ?
            clipped_right - local_result.x : 0u;
        local_result.height = clipped_bottom > local_result.y ?
            clipped_bottom - local_result.y : 0u;
        if (block_x < 0 || block_y < 0 || right > (int)width ||
            bottom > (int)height)
            local_result.clipped = true;
    }
    if (result != NULL)
        *result = local_result;
    return true;
}
