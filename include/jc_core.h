/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef JC_CORE_H
#define JC_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "jc_palette.h"
#include "jc_surface.h"

#define JC_FRAME_WIDTH 640u
#define JC_FRAME_HEIGHT 480u
#define JC_FRAME_RATE 50.0
#define JC_AUDIO_RATE 44100.0

typedef struct jc_core {
    uint64_t frame_number;
    uint32_t phase;
    bool has_content_frame;
    uint32_t framebuffer[JC_FRAME_WIDTH * JC_FRAME_HEIGHT];
} jc_core_t;

void jc_core_init(jc_core_t *core);
void jc_core_reset(jc_core_t *core);
void jc_core_step(jc_core_t *core);
bool jc_core_set_content_frame(jc_core_t *core, const jc_surface_t *surface,
                               const jc_palette_t *palette);
void jc_core_clear_content_frame(jc_core_t *core);
const uint32_t *jc_core_framebuffer(const jc_core_t *core);
size_t jc_core_serialize_size(void);
bool jc_core_serialize(const jc_core_t *core, void *data, size_t size);
bool jc_core_unserialize(jc_core_t *core, const void *data, size_t size);

#endif
