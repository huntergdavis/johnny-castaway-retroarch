/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "jc_core.h"

#include <string.h>

#define JC_STATE_MAGIC 0x4a435354u
#define JC_STATE_VERSION 1u

typedef struct jc_serialized_state {
    uint32_t magic;
    uint32_t version;
    uint64_t frame_number;
    uint32_t phase;
} jc_serialized_state_t;

static void fill_rect(jc_core_t *core, unsigned x, unsigned y,
                      unsigned width, unsigned height, uint32_t color)
{
    unsigned row;
    unsigned column;
    unsigned x_end = x + width;
    unsigned y_end = y + height;

    if (x_end > JC_FRAME_WIDTH)
        x_end = JC_FRAME_WIDTH;
    if (y_end > JC_FRAME_HEIGHT)
        y_end = JC_FRAME_HEIGHT;

    for (row = y; row < y_end; ++row) {
        uint32_t *pixels = &core->framebuffer[row * JC_FRAME_WIDTH];
        for (column = x; column < x_end; ++column)
            pixels[column] = color;
    }
}

static void render_diagnostic_frame(jc_core_t *core)
{
    unsigned y;
    unsigned wave = (unsigned)((core->frame_number / 3u) % 32u);
    unsigned sun_x = 80u + (unsigned)(core->phase % 480u);

    fill_rect(core, 0, 0, JC_FRAME_WIDTH, 300, 0x006bb7d6u);
    fill_rect(core, 0, 300, JC_FRAME_WIDTH, 180, 0x001f78a8u);

    for (y = 304; y < JC_FRAME_HEIGHT; y += 16) {
        unsigned offset = (wave + y) % 32u;
        fill_rect(core, offset, y, 96, 2, 0x008bd6e8u);
        fill_rect(core, offset + 160, y, 96, 2, 0x008bd6e8u);
        fill_rect(core, offset + 320, y, 96, 2, 0x008bd6e8u);
        fill_rect(core, offset + 480, y, 96, 2, 0x008bd6e8u);
    }

    fill_rect(core, sun_x, 72, 28, 28, 0x00ffd65au);
    fill_rect(core, 190, 320, 260, 70, 0x00d8ba72u);
    fill_rect(core, 300, 210, 12, 120, 0x006e472au);
    fill_rect(core, 246, 194, 120, 20, 0x002f8a4bu);
    fill_rect(core, 266, 174, 80, 20, 0x003aa65bu);
}

void jc_core_init(jc_core_t *core)
{
    memset(core, 0, sizeof(*core));
    render_diagnostic_frame(core);
}

void jc_core_reset(jc_core_t *core)
{
    core->frame_number = 0u;
    core->phase = 0u;
    if (!core->has_content_frame)
        render_diagnostic_frame(core);
}

void jc_core_step(jc_core_t *core)
{
    ++core->frame_number;
    core->phase = (core->phase + 1u) % 480u;
    if (!core->has_content_frame)
        render_diagnostic_frame(core);
}

bool jc_core_set_content_frame(jc_core_t *core, const jc_surface_t *surface,
                               const jc_palette_t *palette)
{
    unsigned x_offset;
    unsigned y_offset;
    unsigned y;

    if (surface == NULL || palette == NULL || surface->width > JC_FRAME_WIDTH ||
        surface->height > JC_FRAME_HEIGHT)
        return false;
    memset(core->framebuffer, 0, sizeof(core->framebuffer));
    x_offset = (JC_FRAME_WIDTH - surface->width) / 2u;
    y_offset = (JC_FRAME_HEIGHT - surface->height) / 2u;
    for (y = 0u; y < surface->height; ++y) {
        unsigned x;
        const uint8_t *source = surface->pixels + (size_t)y * surface->pitch;
        uint32_t *destination = core->framebuffer +
            (size_t)(y + y_offset) * JC_FRAME_WIDTH + x_offset;
        for (x = 0u; x < surface->width; ++x)
            destination[x] = palette->xrgb[source[x]];
    }
    core->has_content_frame = true;
    jc_core_reset(core);
    return true;
}

void jc_core_clear_content_frame(jc_core_t *core)
{
    core->has_content_frame = false;
    jc_core_reset(core);
}

const uint32_t *jc_core_framebuffer(const jc_core_t *core)
{
    return core->framebuffer;
}

size_t jc_core_serialize_size(void)
{
    return sizeof(jc_serialized_state_t);
}

bool jc_core_serialize(const jc_core_t *core, void *data, size_t size)
{
    jc_serialized_state_t state;

    if (data == NULL || size < sizeof(state))
        return false;

    state.magic = JC_STATE_MAGIC;
    state.version = JC_STATE_VERSION;
    state.frame_number = core->frame_number;
    state.phase = core->phase;
    memcpy(data, &state, sizeof(state));
    return true;
}

bool jc_core_unserialize(jc_core_t *core, const void *data, size_t size)
{
    jc_serialized_state_t state;

    if (data == NULL || size < sizeof(state))
        return false;

    memcpy(&state, data, sizeof(state));
    if (state.magic != JC_STATE_MAGIC || state.version != JC_STATE_VERSION)
        return false;

    core->frame_number = state.frame_number;
    core->phase = state.phase;
    if (!core->has_content_frame)
        render_diagnostic_frame(core);
    return true;
}
