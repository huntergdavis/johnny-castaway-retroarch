/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Event-to-render bridge independently rewritten from the indexed-surface
 * behavior in Wilson Reborn (wilson-engine/src/{surface,ttm_exec}.rs) and
 * Johnny Reborn (graphics.c/ttm.c). Both references are GPL-3.0-or-later.
 */
#include "jc_ttm_renderer.h"

#include "jc_compositor.h"
#include "jc_scr.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JC_TTM_RENDERER_SURFACE_COUNT (5u + JC_SCRIPT_MAX_THREADS)

static void clear_error(jc_script_error_t *error)
{
    if (error != NULL)
        memset(error, 0, sizeof(*error));
}

static bool set_error(jc_script_error_t *error, jc_script_error_code_t code,
                      uint16_t opcode, const char *format, ...)
{
    va_list args;

    if (error == NULL)
        return false;
    memset(error, 0, sizeof(*error));
    error->code = code;
    error->domain = JC_SCRIPT_DOMAIN_TTM_VM;
    error->opcode = opcode;
    va_start(args, format);
    vsnprintf(error->message, sizeof(error->message), format, args);
    va_end(args);
    return false;
}

static bool surface_is_full_size(const jc_ttm_renderer_t *renderer,
                                 const jc_surface_t *surface)
{
    return surface != NULL && surface->pixels != NULL &&
           surface->width == renderer->width &&
           surface->height == renderer->height &&
           surface->pitch >= surface->width;
}

static void free_bmp_slots(jc_ttm_renderer_t *renderer)
{
    size_t slot;
    size_t image_slot;

    for (slot = 0u; slot < JC_SCRIPT_MAX_TTM_SLOTS; ++slot) {
        for (image_slot = 0u; image_slot < JC_SCRIPT_MAX_BMP_SLOTS;
             ++image_slot)
            jc_bmp_free(&renderer->bmp_slots[slot][image_slot]);
    }
}

void jc_ttm_renderer_destroy(jc_ttm_renderer_t *renderer)
{
    if (renderer == NULL)
        return;
    free_bmp_slots(renderer);
    free(renderer->pixel_storage);
    memset(renderer, 0, sizeof(*renderer));
}

static bool initialize_surface(jc_surface_t *surface, uint8_t *pixels,
                               size_t pixel_count, unsigned width,
                               unsigned height)
{
    return jc_surface_init(surface, pixels, pixel_count, width, height, width);
}

bool jc_ttm_renderer_init(jc_ttm_renderer_t *renderer,
                          unsigned width, unsigned height,
                          int transparent_source_index,
                          const jc_ttm_renderer_resources_t *resources,
                          jc_script_event_callback_t downstream,
                          void *downstream_userdata,
                          jc_script_error_t *error)
{
    size_t total_pixels;
    size_t offset = 0u;
    size_t index;

    clear_error(error);
    if (renderer == NULL)
        return set_error(error, JC_SCRIPT_ERROR_NULL_ARGUMENT, 0u,
                         "TTM renderer init received a null renderer");
    if (width == 0u || height == 0u ||
        width > JC_TTM_RENDERER_MAX_WIDTH ||
        height > JC_TTM_RENDERER_MAX_HEIGHT ||
        (size_t)height > JC_TTM_RENDERER_MAX_PIXELS / width)
        return set_error(error, JC_SCRIPT_ERROR_LIMIT, 0u,
                         "TTM renderer dimensions exceed %ux%u",
                         (unsigned)JC_TTM_RENDERER_MAX_WIDTH,
                         (unsigned)JC_TTM_RENDERER_MAX_HEIGHT);
    if (transparent_source_index < -1 || transparent_source_index > 255)
        return set_error(error, JC_SCRIPT_ERROR_BAD_OPERAND, 0u,
                         "transparent source index must be -1..255");
    if (resources != NULL && resources->load == NULL)
        return set_error(error, JC_SCRIPT_ERROR_NULL_ARGUMENT, 0u,
                         "resource interface has no load callback");

    memset(renderer, 0, sizeof(*renderer));
    renderer->width = width;
    renderer->height = height;
    renderer->pixel_count = (size_t)width * height;
    renderer->transparent_source_index = transparent_source_index;
    if (renderer->pixel_count > SIZE_MAX / JC_TTM_RENDERER_SURFACE_COUNT)
        return set_error(error, JC_SCRIPT_ERROR_LIMIT, 0u,
                         "TTM renderer pixel allocation overflows size_t");
    total_pixels = renderer->pixel_count * JC_TTM_RENDERER_SURFACE_COUNT;
    renderer->pixel_storage = (uint8_t *)calloc(total_pixels, 1u);
    if (renderer->pixel_storage == NULL)
        return set_error(error, JC_SCRIPT_ERROR_LIMIT, 0u,
                         "could not allocate bounded TTM renderer surfaces");
    renderer->pixel_storage_size = total_pixels;

#define INIT_RENDER_SURFACE(member)                                             \
    do {                                                                        \
        if (!initialize_surface(&(renderer->member),                            \
                                renderer->pixel_storage + offset,               \
                                renderer->pixel_count, width, height))          \
            goto init_failed;                                                   \
        offset += renderer->pixel_count;                                        \
    } while (0)
    INIT_RENDER_SURFACE(background);
    INIT_RENDER_SURFACE(saved_zones);
    INIT_RENDER_SURFACE(saved_background);
    INIT_RENDER_SURFACE(scratch);
    INIT_RENDER_SURFACE(output);
#undef INIT_RENDER_SURFACE
    for (index = 0u; index < JC_SCRIPT_MAX_THREADS; ++index) {
        if (!initialize_surface(&renderer->thread_layers[index],
                                renderer->pixel_storage + offset,
                                renderer->pixel_count, width, height))
            goto init_failed;
        offset += renderer->pixel_count;
        jc_surface_clear(&renderer->thread_layers[index],
                         JC_TTM_RENDERER_TRANSPARENT);
    }
    jc_surface_clear(&renderer->saved_zones, JC_TTM_RENDERER_TRANSPARENT);
    if (resources != NULL)
        renderer->resources = *resources;
    renderer->downstream = downstream;
    renderer->downstream_userdata = downstream_userdata;
    renderer->initialized = true;
    return true;

init_failed:
    free(renderer->pixel_storage);
    memset(renderer, 0, sizeof(*renderer));
    return set_error(error, JC_SCRIPT_ERROR_LIMIT, 0u,
                     "could not initialize TTM renderer surfaces");
}

bool jc_ttm_renderer_set_offset(jc_ttm_renderer_t *renderer,
                                int x, int y, jc_script_error_t *error)
{
    clear_error(error);
    if (renderer == NULL || !renderer->initialized)
        return set_error(error, JC_SCRIPT_ERROR_NULL_ARGUMENT, 0u,
                         "TTM renderer is not initialized");
    if (x < -32768 || x > 32767 || y < -32768 || y > 32767)
        return set_error(error, JC_SCRIPT_ERROR_LIMIT, 0u,
                         "TTM renderer offset exceeds signed 16-bit bounds");
    renderer->offset_x = x;
    renderer->offset_y = y;
    return true;
}

bool jc_ttm_renderer_set_background(jc_ttm_renderer_t *renderer,
                                    const jc_surface_t *background,
                                    jc_script_error_t *error)
{
    clear_error(error);
    if (renderer == NULL || !renderer->initialized ||
        !surface_is_full_size(renderer, background))
        return set_error(error, JC_SCRIPT_ERROR_BAD_OPERAND, 0u,
                         "background surface does not match the renderer");
    jc_surface_reset_clip(&renderer->background);
    jc_surface_clear(&renderer->background, 0u);
    jc_surface_blit(&renderer->background, 0, 0, background, -1, false);
    return true;
}

bool jc_ttm_renderer_compose(jc_ttm_renderer_t *renderer,
                             jc_script_error_t *error)
{
    jc_compositor_layer_t layers[1u + JC_SCRIPT_MAX_THREADS];
    size_t layer_count = 0u;
    size_t index;

    clear_error(error);
    if (renderer == NULL || !renderer->initialized)
        return set_error(error, JC_SCRIPT_ERROR_NULL_ARGUMENT, 0u,
                         "TTM renderer is not initialized");
    layers[layer_count++] = (jc_compositor_layer_t){
        &renderer->saved_zones, 0, 0, JC_TTM_RENDERER_TRANSPARENT,
        false, true
    };
    for (index = 0u; index < JC_SCRIPT_MAX_THREADS; ++index) {
        if (!renderer->thread_active[index])
            continue;
        layers[layer_count++] = (jc_compositor_layer_t){
            &renderer->thread_layers[index], 0, 0,
            JC_TTM_RENDERER_TRANSPARENT, false, true
        };
    }
    jc_surface_reset_clip(&renderer->output);
    if (!jc_compositor_compose(&renderer->output, &renderer->background,
                               layers, layer_count))
        return set_error(error, JC_SCRIPT_ERROR_BAD_OPERAND, 0u,
                         "TTM frame composition failed");
    return true;
}

static int signed_word(uint16_t value)
{
    return value <= 0x7fffu ? (int)value : (int)value - 65536;
}

static int coordinate(uint16_t value, int offset)
{
    return signed_word(value) + offset;
}

static void put_unclipped(jc_surface_t *surface, int x, int y, uint8_t color)
{
    if (x < 0 || y < 0 || x >= (int)surface->width ||
        y >= (int)surface->height)
        return;
    surface->pixels[(size_t)y * surface->pitch + (size_t)x] = color;
}

static int absolute_value(int value)
{
    return value < 0 ? -value : value;
}

static void draw_line(jc_surface_t *surface, int x1, int y1,
                      int x2, int y2, uint8_t color)
{
    int dx = absolute_value(x2 - x1);
    int dy = absolute_value(y2 - y1);
    int x_increment = x2 > x1 ? 1 : -1;
    int y_increment = y2 > y1 ? 1 : -1;
    int x = x1;
    int y = y1;
    int count;

    if (dy < dx) {
        int cumulative = (dx + 1) / 2;
        for (count = 0; count < dx; ++count) {
            put_unclipped(surface, x, y, color);
            x += x_increment;
            cumulative += dy;
            if (cumulative > dx) {
                cumulative -= dx;
                y += y_increment;
            }
        }
    } else {
        int cumulative = (dy + 1) / 2;
        for (count = 0; count < dy; ++count) {
            put_unclipped(surface, x, y, color);
            y += y_increment;
            cumulative += dx;
            if (cumulative > dy) {
                cumulative -= dy;
                x += x_increment;
            }
        }
    }
}

static void draw_horizontal(jc_surface_t *surface, int x1, int x2,
                            int y, uint8_t color)
{
    int x;
    for (x = x1; x <= x2; ++x)
        put_unclipped(surface, x, y, color);
}

static void draw_circle(jc_surface_t *surface, int x1, int y1,
                        int width, int height, uint8_t foreground,
                        uint8_t background)
{
    int radius;
    int center_x;
    int center_y;
    int x;
    int y;
    int decision;

    if (width != height || width < 2 || (width & 1) != 0)
        return;
    radius = width / 2 - 1;
    center_x = x1 + radius;
    center_y = y1 + radius;
    x = 0;
    y = radius;
    decision = 1 - radius;
    for (;;) {
        draw_horizontal(surface, center_x - x, center_x + x + 1,
                        center_y + y + 1, background);
        draw_horizontal(surface, center_x - x, center_x + x + 1,
                        center_y - y, background);
        draw_horizontal(surface, center_x - y, center_x + y + 1,
                        center_y + x + 1, background);
        draw_horizontal(surface, center_x - y, center_x + y + 1,
                        center_y - x, background);
        if (y - x <= 1)
            break;
        if (decision < 0)
            decision += (x << 1) + 3;
        else {
            decision += ((x - y) << 1) + 5;
            --y;
        }
        ++x;
    }
    if (foreground == background)
        return;
    x = 0;
    y = radius;
    decision = 1 - radius;
    for (;;) {
        put_unclipped(surface, center_x - x, center_y + y + 1, foreground);
        put_unclipped(surface, center_x + x + 1, center_y + y + 1,
                      foreground);
        put_unclipped(surface, center_x - x, center_y - y, foreground);
        put_unclipped(surface, center_x + x + 1, center_y - y, foreground);
        put_unclipped(surface, center_x - y, center_y + x + 1, foreground);
        put_unclipped(surface, center_x + y + 1, center_y + x + 1,
                      foreground);
        put_unclipped(surface, center_x - y, center_y - x, foreground);
        put_unclipped(surface, center_x + y + 1, center_y - x, foreground);
        if (y - x <= 1)
            break;
        if (decision < 0)
            decision += (x << 1) + 3;
        else {
            decision += ((x - y) << 1) + 5;
            --y;
        }
        ++x;
    }
}

static void copy_saved_zone(jc_ttm_renderer_t *renderer,
                            const jc_surface_t *source,
                            int x, int y, unsigned width, unsigned height)
{
    int64_t left = x;
    int64_t top = y;
    int64_t right = left + (int64_t)width;
    int64_t bottom = top + (int64_t)height;
    int row;
    int column;

    if (left < 0)
        left = 0;
    if (top < 0)
        top = 0;
    if (right > (int64_t)renderer->width)
        right = renderer->width;
    if (bottom > (int64_t)renderer->height)
        bottom = renderer->height;
    if (right <= left || bottom <= top)
        return;
    for (row = (int)top; row < (int)bottom; ++row) {
        for (column = (int)left; column < (int)right; ++column) {
            uint8_t pixel = source->pixels[(size_t)row * source->pitch +
                                           (size_t)column];
            if (pixel != JC_TTM_RENDERER_TRANSPARENT)
                renderer->saved_zones.pixels[
                    (size_t)row * renderer->saved_zones.pitch +
                    (size_t)column] = pixel;
        }
    }
}

static bool require_args(const jc_script_event_t *event, uint8_t count,
                         jc_script_error_t *error)
{
    if (event->arg_count >= count)
        return true;
    return set_error(error, JC_SCRIPT_ERROR_BAD_OPERAND, event->opcode,
                     "TTM opcode 0x%04X event has %u args; needs %u",
                     (unsigned)event->opcode, (unsigned)event->arg_count,
                     (unsigned)count);
}

static bool copy_resource_name(const jc_script_event_t *event, char *name,
                               jc_script_error_t *error)
{
    size_t index;

    if (event->string == NULL || event->string_length == 0u ||
        event->string_length > JC_TTM_RENDERER_NAME_BYTES)
        return set_error(error, JC_SCRIPT_ERROR_BAD_OPERAND, event->opcode,
                         "TTM resource name is empty or too long");
    for (index = 0u; index < event->string_length; ++index) {
        unsigned char byte = (unsigned char)event->string[index];
        if (byte < 0x20u || byte > 0x7eu)
            return set_error(error, JC_SCRIPT_ERROR_BAD_OPERAND,
                             event->opcode,
                             "TTM resource name contains a non-printable byte");
        name[index] = (char)byte;
    }
    name[event->string_length] = '\0';
    return true;
}

static bool load_resource(jc_ttm_renderer_t *renderer,
                          const jc_script_event_t *event,
                          const uint8_t **data, size_t *size, char *name,
                          jc_script_error_t *error)
{
    char resource_error[JC_SCRIPT_ERROR_MESSAGE_BYTES];

    if (!copy_resource_name(event, name, error))
        return false;
    if (renderer->resources.load == NULL)
        return set_error(error, JC_SCRIPT_ERROR_UNBOUND_RESOURCE,
                         event->opcode,
                         "no resource loader is bound for %s", name);
    resource_error[0] = '\0';
    *data = NULL;
    *size = 0u;
    if (!renderer->resources.load(renderer->resources.userdata, name, data,
                                  size, resource_error,
                                  sizeof(resource_error)))
        return set_error(error, JC_SCRIPT_ERROR_UNBOUND_RESOURCE,
                         event->opcode, "could not load %s: %s", name,
                         resource_error[0] != '\0' ? resource_error :
                                                    "loader rejected resource");
    if (*data == NULL || *size == 0u ||
        *size > JC_TTM_RENDERER_MAX_RESOURCE_BYTES) {
        if (renderer->resources.release != NULL && *data != NULL)
            renderer->resources.release(renderer->resources.userdata, *data,
                                        *size);
        *data = NULL;
        *size = 0u;
        return set_error(error, JC_SCRIPT_ERROR_LIMIT, event->opcode,
                         "resource %s has an invalid size", name);
    }
    return true;
}

static void release_resource(jc_ttm_renderer_t *renderer,
                             const uint8_t *data, size_t size)
{
    if (renderer->resources.release != NULL)
        renderer->resources.release(renderer->resources.userdata, data, size);
}

static bool handle_load_screen(jc_ttm_renderer_t *renderer,
                               const jc_script_event_t *event,
                               jc_script_error_t *error)
{
    const uint8_t *data;
    size_t size;
    char name[JC_TTM_RENDERER_NAME_BYTES + 1u];
    char resource_error[JC_SCRIPT_ERROR_MESSAGE_BYTES];
    jc_surface_t decoded;
    bool success;

    if (!load_resource(renderer, event, &data, &size, name, error))
        return false;
    resource_error[0] = '\0';
    success = jc_scr_decode(data, size, renderer->scratch.pixels,
                            renderer->pixel_count, &decoded, resource_error,
                            sizeof(resource_error));
    release_resource(renderer, data, size);
    if (!success)
        return set_error(error, JC_SCRIPT_ERROR_BAD_OPERAND, event->opcode,
                         "could not decode screen %s: %s", name,
                         resource_error);
    jc_surface_reset_clip(&renderer->background);
    jc_surface_clear(&renderer->background, 0u);
    jc_surface_blit(&renderer->background, 0, 0, &decoded, -1, false);
    return true;
}

static bool handle_load_image(jc_ttm_renderer_t *renderer,
                              const jc_script_event_t *event,
                              jc_script_error_t *error)
{
    const uint8_t *data;
    size_t size;
    char name[JC_TTM_RENDERER_NAME_BYTES + 1u];
    char resource_error[JC_SCRIPT_ERROR_MESSAGE_BYTES];
    jc_bmp_t parsed;
    jc_bmp_t *destination;
    size_t retained_pixels;
    size_t pixel;
    bool success;

    if (event->scene_slot >= JC_SCRIPT_MAX_TTM_SLOTS ||
        event->selected_bmp_slot >= JC_SCRIPT_MAX_BMP_SLOTS)
        return set_error(error, JC_SCRIPT_ERROR_BAD_OPERAND, event->opcode,
                         "LOAD_IMAGE references an out-of-range slot");
    if (!load_resource(renderer, event, &data, &size, name, error))
        return false;
    memset(&parsed, 0, sizeof(parsed));
    resource_error[0] = '\0';
    success = jc_bmp_parse(&parsed, data, size, resource_error,
                           sizeof(resource_error));
    release_resource(renderer, data, size);
    if (!success)
        return set_error(error, JC_SCRIPT_ERROR_BAD_OPERAND, event->opcode,
                         "could not decode image %s: %s", name,
                         resource_error);
    if (renderer->transparent_source_index >= 0) {
        for (pixel = 0u; pixel < parsed.pixel_storage_size; ++pixel) {
            if (parsed.pixel_storage[pixel] ==
                (uint8_t)renderer->transparent_source_index)
                parsed.pixel_storage[pixel] = JC_TTM_RENDERER_TRANSPARENT;
        }
    }
    destination =
        &renderer->bmp_slots[event->scene_slot][event->selected_bmp_slot];
    if (destination->pixel_storage_size > renderer->bmp_pixel_total) {
        jc_bmp_free(&parsed);
        return set_error(error, JC_SCRIPT_ERROR_LIMIT, event->opcode,
                         "renderer BMP accounting invariant was violated");
    }
    retained_pixels = renderer->bmp_pixel_total -
                      destination->pixel_storage_size;
    if (parsed.pixel_storage_size >
        JC_TTM_RENDERER_MAX_BMP_PIXELS - retained_pixels) {
        jc_bmp_free(&parsed);
        return set_error(error, JC_SCRIPT_ERROR_LIMIT, event->opcode,
                         "loaded BMPs exceed the renderer-wide pixel budget");
    }
    jc_bmp_free(destination);
    *destination = parsed;
    renderer->bmp_pixel_total = retained_pixels + parsed.pixel_storage_size;
    return true;
}

static bool handle_load_palette(jc_ttm_renderer_t *renderer,
                                const jc_script_event_t *event,
                                jc_script_error_t *error)
{
    const uint8_t *data;
    size_t size;
    char name[JC_TTM_RENDERER_NAME_BYTES + 1u];
    char resource_error[JC_SCRIPT_ERROR_MESSAGE_BYTES];
    bool success;

    if (!load_resource(renderer, event, &data, &size, name, error))
        return false;
    resource_error[0] = '\0';
    success = jc_palette_decode(&renderer->palette, data, size,
                                resource_error, sizeof(resource_error));
    release_resource(renderer, data, size);
    if (!success)
        return set_error(error, JC_SCRIPT_ERROR_BAD_OPERAND, event->opcode,
                         "could not decode palette %s: %s", name,
                         resource_error);
    renderer->has_palette = true;
    if (renderer->transparent_source_index < 0) {
        size_t index;
        for (index = 0u; index < JC_PALETTE_COLORS; ++index) {
            if (renderer->palette.xrgb[index] == 0x00a800a8u) {
                renderer->transparent_source_index = (int)index;
                break;
            }
        }
    }
    return true;
}

static bool draw_sprite(jc_ttm_renderer_t *renderer,
                        const jc_script_event_t *event,
                        jc_surface_t *layer, bool flip,
                        jc_script_error_t *error)
{
    jc_bmp_t *bmp;
    jc_surface_t sprite;
    uint16_t frame;
    uint16_t image_slot;

    if (!require_args(event, 4u, error))
        return false;
    frame = event->args[2];
    image_slot = event->args[3];
    if (event->scene_slot >= JC_SCRIPT_MAX_TTM_SLOTS ||
        image_slot >= JC_SCRIPT_MAX_BMP_SLOTS)
        return set_error(error, JC_SCRIPT_ERROR_BAD_OPERAND, event->opcode,
                         "DRAW_SPRITE references an out-of-range slot");
    bmp = &renderer->bmp_slots[event->scene_slot][image_slot];
    /*
     * The original host renderer treats an in-range empty BMP slot as an
     * intentionally absent frame and simply draws nothing.  This occurs in
     * authentic JOHNNY.ADS tag 6: the just-finished thread slot is reused for
     * SJWORK.TTM tag 9 (which loads BMP slot 3), while tag 3 runs later in the
     * same scheduler pass and attempts one draw before tag 9 gets its turn.
     * Keep the no-op narrow: invalid slot numbers and failed LOAD_IMAGE events
     * remain structured errors, as does an invalid decoded surface.
     */
    if (bmp->images == NULL)
        return true;
    /* Authentic scripts can request a frame beyond the currently installed
     * image count while recycling BMP slots (MARY.ADS tag 1 does this after
     * replacing slot 2 with SMDATE11.BMP).  Johnny Reborn's host renderer
     * validates that request and draws nothing; retain that safe behavior. */
    if (frame >= bmp->image_count)
        return true;
    if (!jc_bmp_image_surface(bmp, frame, &sprite))
        return set_error(error, JC_SCRIPT_ERROR_BAD_OPERAND, event->opcode,
                         "DRAW_SPRITE frame %u has an invalid decoded surface",
                         (unsigned)frame);
    if (sprite.width > renderer->width || sprite.height > renderer->height)
        return set_error(error, JC_SCRIPT_ERROR_LIMIT, event->opcode,
                         "sprite frame dimensions exceed the render canvas");
    jc_surface_blit(layer,
                    coordinate(event->args[0], renderer->offset_x),
                    coordinate(event->args[1], renderer->offset_y),
                    &sprite, JC_TTM_RENDERER_TRANSPARENT, flip);
    return true;
}

static bool handle_instruction(jc_ttm_renderer_t *renderer,
                               const jc_script_event_t *event,
                               jc_script_error_t *error)
{
    jc_surface_t *layer;
    int x1;
    int y1;
    int x2;
    int y2;

    if (event->thread_index >= JC_SCRIPT_MAX_THREADS)
        return set_error(error, JC_SCRIPT_ERROR_BAD_OPERAND, event->opcode,
                         "TTM instruction references an invalid thread");
    layer = &renderer->thread_layers[event->thread_index];
    switch (event->opcode) {
    case 0x001fu: /* SAVE_BACKGROUND: absent from JC data, but safely useful. */
        memcpy(renderer->saved_background.pixels,
               renderer->background.pixels, renderer->pixel_count);
        renderer->has_saved_background = true;
        break;
    case 0x0080u: /* DRAW_BACKGROUND is otherwise a no-op in Johnny Reborn. */
        if (renderer->has_saved_background)
            memcpy(renderer->background.pixels,
                   renderer->saved_background.pixels, renderer->pixel_count);
        break;
    case 0x4004u:
        if (!require_args(event, 4u, error))
            return false;
        x1 = coordinate(event->args[0], renderer->offset_x);
        y1 = coordinate(event->args[1], renderer->offset_y);
        x2 = coordinate(event->args[2], renderer->offset_x);
        y2 = coordinate(event->args[3], renderer->offset_y);
        jc_surface_set_clip(layer, x1, y1, x2 - x1, y2 - y1);
        break;
    case 0x4204u:
        if (!require_args(event, 4u, error))
            return false;
        copy_saved_zone(renderer, layer,
                        coordinate(event->args[0], renderer->offset_x),
                        coordinate(event->args[1], renderer->offset_y),
                        (unsigned)event->args[2] + 2u,
                        (unsigned)event->args[3]);
        break;
    case 0xa002u:
        if (!require_args(event, 2u, error))
            return false;
        put_unclipped(layer,
                      coordinate(event->args[0], renderer->offset_x),
                      coordinate(event->args[1], renderer->offset_y),
                      event->foreground_color);
        break;
    case 0xa064u:
        jc_surface_clear(&renderer->saved_zones,
                         JC_TTM_RENDERER_TRANSPARENT);
        break;
    case 0xa0a4u:
        if (!require_args(event, 4u, error))
            return false;
        draw_line(layer,
                  coordinate(event->args[0], renderer->offset_x),
                  coordinate(event->args[1], renderer->offset_y),
                  coordinate(event->args[2], renderer->offset_x),
                  coordinate(event->args[3], renderer->offset_y),
                  event->foreground_color);
        break;
    case 0xa104u:
        if (!require_args(event, 4u, error))
            return false;
        jc_surface_fill_rect(layer,
                             coordinate(event->args[0], renderer->offset_x),
                             coordinate(event->args[1], renderer->offset_y),
                             event->args[2], event->args[3],
                             event->foreground_color);
        break;
    case 0xa404u:
        if (!require_args(event, 4u, error))
            return false;
        if (event->args[2] > renderer->width ||
            event->args[3] > renderer->height)
            return set_error(error, JC_SCRIPT_ERROR_LIMIT, event->opcode,
                             "circle dimensions exceed the render canvas");
        draw_circle(layer,
                    coordinate(event->args[0], renderer->offset_x),
                    coordinate(event->args[1], renderer->offset_y),
                    event->args[2], event->args[3],
                    event->foreground_color, event->background_color);
        break;
    case 0xa504u:
        return draw_sprite(renderer, event, layer, false, error);
    case 0xa524u:
        return draw_sprite(renderer, event, layer, true, error);
    case 0xa601u:
        jc_surface_clear(layer, JC_TTM_RENDERER_TRANSPARENT);
        break;
    case 0xf01fu:
        return handle_load_screen(renderer, event, error);
    case 0xf02fu:
        return handle_load_image(renderer, event, error);
    case 0xf05fu:
        return handle_load_palette(renderer, event, error);
    default:
        /* Control/audio/dump-only instructions are VM-owned or downstream. */
        break;
    }
    return true;
}

bool jc_ttm_renderer_event(void *userdata, const jc_script_event_t *event,
                           jc_script_error_t *error)
{
    jc_ttm_renderer_t *renderer = (jc_ttm_renderer_t *)userdata;

    clear_error(error);
    if (renderer == NULL || !renderer->initialized || event == NULL)
        return set_error(error, JC_SCRIPT_ERROR_NULL_ARGUMENT, 0u,
                         "TTM renderer event received invalid input");
    if (event->domain == JC_SCRIPT_DOMAIN_TTM_VM) {
        if (event->thread_index >= JC_SCRIPT_MAX_THREADS)
            return set_error(error, JC_SCRIPT_ERROR_BAD_OPERAND,
                             event->opcode,
                             "TTM event references an invalid thread");
        if (event->kind == JC_SCRIPT_EVENT_SCENE_STARTED) {
            renderer->thread_active[event->thread_index] = true;
            jc_surface_clear(&renderer->thread_layers[event->thread_index],
                             JC_TTM_RENDERER_TRANSPARENT);
            jc_surface_reset_clip(
                &renderer->thread_layers[event->thread_index]);
        } else if (event->kind == JC_SCRIPT_EVENT_SCENE_STOPPED) {
            renderer->thread_active[event->thread_index] = false;
            jc_surface_clear(&renderer->thread_layers[event->thread_index],
                             JC_TTM_RENDERER_TRANSPARENT);
            jc_surface_reset_clip(
                &renderer->thread_layers[event->thread_index]);
        } else if (event->kind == JC_SCRIPT_EVENT_FRAME_READY) {
            if (!jc_ttm_renderer_compose(renderer, error))
                return false;
        } else if (event->kind == JC_SCRIPT_EVENT_INSTRUCTION &&
                   !handle_instruction(renderer, event, error)) {
            return false;
        }
    }
    if (renderer->downstream != NULL &&
        !renderer->downstream(renderer->downstream_userdata, event, error)) {
        if (error != NULL && error->code == JC_SCRIPT_ERROR_NONE)
            return set_error(error, JC_SCRIPT_ERROR_CALLBACK, event->opcode,
                             "downstream script callback rejected an event");
        return false;
    }
    return true;
}

const jc_surface_t *jc_ttm_renderer_output(const jc_ttm_renderer_t *renderer)
{
    if (renderer == NULL || !renderer->initialized)
        return NULL;
    return &renderer->output;
}

const jc_palette_t *jc_ttm_renderer_palette(const jc_ttm_renderer_t *renderer)
{
    if (renderer == NULL || !renderer->initialized || !renderer->has_palette)
        return NULL;
    return &renderer->palette;
}
