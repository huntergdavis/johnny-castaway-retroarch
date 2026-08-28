/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "jc_ttm_renderer.h"

#include <stdbool.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct bytes {
    uint8_t data[2048];
    size_t size;
} bytes_t;

typedef struct resources {
    bytes_t screen;
    bytes_t image;
    bytes_t palette;
    unsigned loads;
    unsigned downstream_samples;
} resources_t;

static void require(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static void append_u8(bytes_t *bytes, uint8_t value)
{
    require(bytes->size < sizeof(bytes->data), "test byte builder overflow");
    bytes->data[bytes->size++] = value;
}

static void append_u16(bytes_t *bytes, uint16_t value)
{
    append_u8(bytes, (uint8_t)value);
    append_u8(bytes, (uint8_t)(value >> 8));
}

static void append_u32(bytes_t *bytes, uint32_t value)
{
    append_u8(bytes, (uint8_t)value);
    append_u8(bytes, (uint8_t)(value >> 8));
    append_u8(bytes, (uint8_t)(value >> 16));
    append_u8(bytes, (uint8_t)(value >> 24));
}

static void append_data(bytes_t *bytes, const void *data, size_t size)
{
    const uint8_t *source = (const uint8_t *)data;
    size_t index;
    for (index = 0u; index < size; ++index)
        append_u8(bytes, source[index]);
}

static bytes_t make_screen(void)
{
    bytes_t bytes = {{0}, 0u};
    uint8_t packed[8];
    memset(packed, 0x33, sizeof(packed));
    append_data(&bytes, "SCR:", 4u);
    append_u16(&bytes, 0u);
    append_u16(&bytes, 0u);
    append_data(&bytes, "DIM:", 4u);
    append_u32(&bytes, 4u);
    append_u16(&bytes, 4u);
    append_u16(&bytes, 4u);
    append_data(&bytes, "BIN:", 4u);
    append_u32(&bytes, (uint32_t)sizeof(packed) + 5u);
    append_u8(&bytes, 0u);
    append_u32(&bytes, (uint32_t)sizeof(packed));
    append_data(&bytes, packed, sizeof(packed));
    return bytes;
}

static bytes_t make_image(void)
{
    const uint8_t packed[] = {0x15u, 0x23u};
    bytes_t bytes = {{0}, 0u};
    append_data(&bytes, "BMP:", 4u);
    append_u16(&bytes, 2u);
    append_u16(&bytes, 2u);
    append_data(&bytes, "INF:", 4u);
    append_u32(&bytes, 0u);
    append_u16(&bytes, 1u);
    append_u16(&bytes, 2u);
    append_u16(&bytes, 2u);
    append_data(&bytes, "BIN:", 4u);
    append_u32(&bytes, (uint32_t)sizeof(packed) + 5u);
    append_u8(&bytes, 0u);
    append_u32(&bytes, (uint32_t)sizeof(packed));
    append_data(&bytes, packed, sizeof(packed));
    return bytes;
}

static bytes_t make_palette(void)
{
    bytes_t bytes = {{0}, 0u};
    size_t index;
    append_data(&bytes, "PAL:", 4u);
    append_u32(&bytes, 0u);
    append_data(&bytes, "VGA:", 4u);
    append_u32(&bytes, 0u);
    for (index = 0u; index < JC_PALETTE_COLORS; ++index) {
        if (index == 1u) {
            append_u8(&bytes, 1u);
            append_u8(&bytes, 2u);
            append_u8(&bytes, 3u);
        } else if (index == 5u) {
            append_u8(&bytes, 42u);
            append_u8(&bytes, 0u);
            append_u8(&bytes, 42u);
        } else {
            append_u8(&bytes, 0u);
            append_u8(&bytes, 0u);
            append_u8(&bytes, 0u);
        }
    }
    return bytes;
}

static bool load_resource(void *userdata, const char *name,
                          const uint8_t **data, size_t *size,
                          char *error, size_t error_size)
{
    resources_t *resources = (resources_t *)userdata;
    const bytes_t *selected = NULL;
    ++resources->loads;
    if (strcmp(name, "BG.SCR") == 0)
        selected = &resources->screen;
    else if (strcmp(name, "S.BMP") == 0)
        selected = &resources->image;
    else if (strcmp(name, "P.PAL") == 0)
        selected = &resources->palette;
    if (selected == NULL) {
        if (error != NULL && error_size > 0u)
            snprintf(error, error_size, "not found");
        return false;
    }
    *data = selected->data;
    *size = selected->size;
    return true;
}

static bool downstream(void *userdata, const jc_script_event_t *event,
                       jc_script_error_t *error)
{
    resources_t *resources = (resources_t *)userdata;
    (void)error;
    if (event->kind == JC_SCRIPT_EVENT_INSTRUCTION &&
        event->opcode == 0xc051u)
        ++resources->downstream_samples;
    return true;
}

static jc_script_event_t event_base(jc_script_event_kind_t kind,
                                    uint16_t opcode)
{
    jc_script_event_t event;
    memset(&event, 0, sizeof(event));
    event.kind = kind;
    event.domain = JC_SCRIPT_DOMAIN_TTM_VM;
    event.opcode = opcode;
    event.thread_index = 0u;
    event.scene_slot = 1u;
    event.foreground_color = 7u;
    event.background_color = 6u;
    return event;
}

static void send_event(jc_ttm_renderer_t *renderer,
                       jc_script_event_t *event)
{
    jc_script_error_t error;
    require(jc_ttm_renderer_event(renderer, event, &error), error.message);
}

static void send_string(jc_ttm_renderer_t *renderer, uint16_t opcode,
                        const char *value, uint16_t bmp_slot)
{
    jc_script_event_t event = event_base(JC_SCRIPT_EVENT_INSTRUCTION, opcode);
    event.string = value;
    event.string_length = strlen(value);
    event.selected_bmp_slot = bmp_slot;
    send_event(renderer, &event);
}

static void send_args(jc_ttm_renderer_t *renderer, uint16_t opcode,
                      const uint16_t *args, uint8_t count,
                      uint8_t foreground, uint8_t background)
{
    jc_script_event_t event = event_base(JC_SCRIPT_EVENT_INSTRUCTION, opcode);
    event.arg_count = count;
    memcpy(event.args, args, (size_t)count * sizeof(args[0]));
    event.foreground_color = foreground;
    event.background_color = background;
    send_event(renderer, &event);
}

static uint8_t output_at(const jc_ttm_renderer_t *renderer,
                         unsigned x, unsigned y)
{
    const jc_surface_t *surface = jc_ttm_renderer_output(renderer);
    require(surface != NULL && x < surface->width && y < surface->height,
            "output pixel lookup out of bounds");
    return surface->pixels[(size_t)y * surface->pitch + x];
}

static void frame(jc_ttm_renderer_t *renderer)
{
    jc_script_event_t event =
        event_base(JC_SCRIPT_EVENT_FRAME_READY, 0u);
    send_event(renderer, &event);
}

static void clear_layer(jc_ttm_renderer_t *renderer)
{
    uint16_t arg = 0u;
    send_args(renderer, 0xa601u, &arg, 1u, 0u, 0u);
}

static void full_clip(jc_ttm_renderer_t *renderer)
{
    const uint16_t args[] = {0u, 0u, 4u, 4u};
    send_args(renderer, 0x4004u, args, 4u, 0u, 0u);
}

static void test_resources_sprites_and_palette(void)
{
    resources_t resources;
    jc_ttm_renderer_resources_t resource_api;
    jc_ttm_renderer_t renderer;
    jc_script_error_t error;
    jc_script_event_t lifecycle;
    const uint16_t sprite[] = {1u, 1u, 0u, 0u};

    memset(&resources, 0, sizeof(resources));
    resources.screen = make_screen();
    resources.image = make_image();
    resources.palette = make_palette();
    resource_api.load = load_resource;
    resource_api.release = NULL;
    resource_api.userdata = &resources;
    require(jc_ttm_renderer_init(&renderer, 4u, 4u, -1, &resource_api,
                                 downstream, &resources, &error),
            error.message);

    lifecycle = event_base(JC_SCRIPT_EVENT_SCENE_STARTED, 0u);
    send_event(&renderer, &lifecycle);
    send_string(&renderer, 0xf01fu, "BG.SCR", 0u);
    send_string(&renderer, 0xf05fu, "P.PAL", 0u);
    send_string(&renderer, 0xf02fu, "S.BMP", 0u);
    require(resources.loads == 3u, "resource callback count is incorrect");
    require(jc_ttm_renderer_palette(&renderer) != NULL &&
                jc_ttm_renderer_palette(&renderer)->xrgb[1] == 0x0004080cu,
            "palette resource was not decoded");
    require(renderer.transparent_source_index == 5,
            "palette magenta index was not detected");

    clear_layer(&renderer);
    full_clip(&renderer);
    send_args(&renderer, 0xa504u, sprite, 4u, 0u, 0u);
    frame(&renderer);
    require(output_at(&renderer, 1u, 1u) == 1u,
            "normal sprite first pixel is wrong");
    require(output_at(&renderer, 2u, 1u) == 3u,
            "transparent sprite pixel did not preserve background");
    require(output_at(&renderer, 1u, 2u) == 2u &&
                output_at(&renderer, 2u, 2u) == 3u,
            "normal sprite bottom row is wrong");

    clear_layer(&renderer);
    send_args(&renderer, 0xa524u, sprite, 4u, 0u, 0u);
    frame(&renderer);
    require(output_at(&renderer, 1u, 1u) == 3u &&
                output_at(&renderer, 2u, 1u) == 1u &&
                output_at(&renderer, 1u, 2u) == 3u &&
                output_at(&renderer, 2u, 2u) == 2u,
            "flipped sprite pixels are wrong");

    clear_layer(&renderer);
    {
        const uint16_t clip[] = {1u, 2u, 2u, 3u};
        send_args(&renderer, 0x4004u, clip, 4u, 0u, 0u);
        send_args(&renderer, 0xa504u, sprite, 4u, 0u, 0u);
        frame(&renderer);
        require(output_at(&renderer, 1u, 2u) == 2u &&
                    output_at(&renderer, 2u, 2u) == 3u,
                "sprite clipping did not restrict the destination");
    }

    lifecycle = event_base(JC_SCRIPT_EVENT_INSTRUCTION, 0xc051u);
    lifecycle.arg_count = 1u;
    lifecycle.args[0] = 9u;
    send_event(&renderer, &lifecycle);
    require(resources.downstream_samples == 1u,
            "audio instruction was not forwarded downstream");

    lifecycle = event_base(JC_SCRIPT_EVENT_SCENE_STOPPED, 0u);
    send_event(&renderer, &lifecycle);
    frame(&renderer);
    require(output_at(&renderer, 1u, 2u) == 3u,
            "stopped thread remained visible");
    jc_ttm_renderer_destroy(&renderer);
}

static void test_primitives_offsets_and_saved_zones(void)
{
    resources_t resources;
    jc_ttm_renderer_resources_t resource_api;
    jc_ttm_renderer_t renderer;
    jc_script_error_t error;
    jc_script_event_t lifecycle;
    uint16_t args[4];

    memset(&resources, 0, sizeof(resources));
    resources.screen = make_screen();
    resources.image = make_image();
    resources.palette = make_palette();
    resource_api.load = load_resource;
    resource_api.release = NULL;
    resource_api.userdata = &resources;
    require(jc_ttm_renderer_init(&renderer, 4u, 4u, 5, &resource_api,
                                 NULL, NULL, &error), error.message);
    lifecycle = event_base(JC_SCRIPT_EVENT_SCENE_STARTED, 0u);
    send_event(&renderer, &lifecycle);
    send_string(&renderer, 0xf01fu, "BG.SCR", 0u);

    clear_layer(&renderer);
    args[0] = 1u; args[1] = 1u; args[2] = 3u; args[3] = 3u;
    send_args(&renderer, 0x4004u, args, 4u, 0u, 0u);
    args[0] = 0u; args[1] = 0u; args[2] = 4u; args[3] = 4u;
    send_args(&renderer, 0xa104u, args, 4u, 7u, 0u);
    args[0] = 0u; args[1] = 0u;
    send_args(&renderer, 0xa002u, args, 2u, 8u, 0u);
    frame(&renderer);
    require(output_at(&renderer, 0u, 0u) == 8u,
            "DRAW_PIXEL incorrectly obeyed the rectangle clip");
    require(output_at(&renderer, 1u, 1u) == 7u &&
                output_at(&renderer, 2u, 2u) == 7u &&
                output_at(&renderer, 3u, 3u) == 3u,
            "clipped rectangle pixels are wrong");

    clear_layer(&renderer);
    full_clip(&renderer);
    require(jc_ttm_renderer_set_offset(&renderer, 1, 0, &error),
            error.message);
    args[0] = 0u; args[1] = 0u;
    send_args(&renderer, 0xa002u, args, 2u, 9u, 0u);
    require(jc_ttm_renderer_set_offset(&renderer, 0, 0, &error),
            error.message);
    args[0] = 0u; args[1] = 3u; args[2] = 3u; args[3] = 3u;
    send_args(&renderer, 0xa0a4u, args, 4u, 10u, 0u);
    frame(&renderer);
    require(output_at(&renderer, 1u, 0u) == 9u,
            "drawing offset was not applied");
    require(output_at(&renderer, 0u, 3u) == 10u &&
                output_at(&renderer, 2u, 3u) == 10u &&
                output_at(&renderer, 3u, 3u) == 3u,
            "Bresenham line does not match endpoint semantics");

    clear_layer(&renderer);
    full_clip(&renderer);
    args[0] = 1u; args[1] = 1u; args[2] = 1u; args[3] = 1u;
    send_args(&renderer, 0xa104u, args, 4u, 11u, 0u);
    send_args(&renderer, 0x4204u, args, 4u, 0u, 0u);
    clear_layer(&renderer);
    frame(&renderer);
    require(output_at(&renderer, 1u, 1u) == 11u,
            "saved-zone layer did not persist after clear");
    send_args(&renderer, 0xa064u, args, 4u, 0u, 0u);
    frame(&renderer);
    require(output_at(&renderer, 1u, 1u) == 3u,
            "RESTORE_ZONE did not release the saved-zone layer");

    args[0] = 0u; args[1] = 0u; args[2] = 4u; args[3] = 4u;
    clear_layer(&renderer);
    send_args(&renderer, 0xa404u, args, 4u, 12u, 6u);
    frame(&renderer);
    require(output_at(&renderer, 1u, 1u) == 6u ||
                output_at(&renderer, 1u, 1u) == 12u,
            "circle primitive produced no indexed pixels");
    jc_ttm_renderer_destroy(&renderer);
}

static void test_background_snapshot_and_errors(void)
{
    resources_t resources;
    jc_ttm_renderer_resources_t resource_api;
    jc_ttm_renderer_t renderer;
    jc_script_error_t error;
    jc_script_event_t event;
    jc_surface_t alternate;
    uint8_t alternate_pixels[16];
    const uint16_t bad_sprite[] = {0u, 0u, 0u, 1u};

    memset(&resources, 0, sizeof(resources));
    resources.screen = make_screen();
    resources.image = make_image();
    resources.palette = make_palette();
    resource_api.load = load_resource;
    resource_api.release = NULL;
    resource_api.userdata = &resources;
    require(jc_ttm_renderer_init(&renderer, 4u, 4u, 5, &resource_api,
                                 NULL, NULL, &error), error.message);
    event = event_base(JC_SCRIPT_EVENT_SCENE_STARTED, 0u);
    send_event(&renderer, &event);
    send_string(&renderer, 0xf01fu, "BG.SCR", 0u);
    event = event_base(JC_SCRIPT_EVENT_INSTRUCTION, 0x001fu);
    send_event(&renderer, &event);
    memset(alternate_pixels, 9, sizeof(alternate_pixels));
    require(jc_surface_init(&alternate, alternate_pixels,
                            sizeof(alternate_pixels), 4u, 4u, 4u),
            "alternate background init failed");
    require(jc_ttm_renderer_set_background(&renderer, &alternate, &error),
            error.message);
    event = event_base(JC_SCRIPT_EVENT_INSTRUCTION, 0x0080u);
    send_event(&renderer, &event);
    frame(&renderer);
    require(output_at(&renderer, 0u, 0u) == 3u,
            "saved background was not restored");

    event = event_base(JC_SCRIPT_EVENT_INSTRUCTION, 0xf02fu);
    event.string = "MISSING.BMP";
    event.string_length = strlen(event.string);
    require(!jc_ttm_renderer_event(&renderer, &event, &error),
            "missing resource was accepted");
    require(error.code == JC_SCRIPT_ERROR_UNBOUND_RESOURCE,
            "missing resource did not return a structured error");

    event = event_base(JC_SCRIPT_EVENT_INSTRUCTION, 0xa504u);
    event.arg_count = 4u;
    memcpy(event.args, bad_sprite, sizeof(bad_sprite));
    require(jc_ttm_renderer_event(&renderer, &event, &error),
            "in-range unloaded sprite slot was not treated as a no-op");
    require(error.code == JC_SCRIPT_ERROR_NONE,
            "in-range unloaded sprite no-op returned an error");
    event.args[3] = JC_SCRIPT_MAX_BMP_SLOTS;
    require(!jc_ttm_renderer_event(&renderer, &event, &error),
            "out-of-range sprite slot was accepted");
    require(error.code == JC_SCRIPT_ERROR_BAD_OPERAND,
            "out-of-range sprite slot did not return a structured error");
    send_string(&renderer, 0xf02fu, "S.BMP", 0u);
    event.args[2] = 1u;
    event.args[3] = 0u;
    require(jc_ttm_renderer_event(&renderer, &event, &error),
            "out-of-range frame in a loaded sprite was not a safe no-op");
    require(error.code == JC_SCRIPT_ERROR_NONE,
            "out-of-range loaded sprite no-op returned an error");
    require(!jc_ttm_renderer_set_offset(&renderer, INT_MAX, 0, &error),
            "oversized drawing offset was accepted");
    require(error.code == JC_SCRIPT_ERROR_LIMIT,
            "oversized offset did not return a structured error");
    jc_ttm_renderer_destroy(&renderer);
}

static void test_real_vm_event_bridge(void)
{
    resources_t resources;
    jc_ttm_renderer_resources_t resource_api;
    jc_ttm_renderer_t renderer;
    jc_ttm_vm_t vm;
    jc_ttm_t ttm;
    jc_script_error_t error;
    jc_script_event_t lifecycle;
    bytes_t code = {{0}, 0u};

    memset(&resources, 0, sizeof(resources));
    resources.screen = make_screen();
    resources.image = make_image();
    resources.palette = make_palette();
    resource_api.load = load_resource;
    resource_api.release = NULL;
    resource_api.userdata = &resources;

    append_u16(&code, 0xf01fu);
    append_data(&code, "BG.SCR\0", 7u);
    append_u8(&code, 0u); /* even-byte string padding */
    append_u16(&code, 0x1051u);
    append_u16(&code, 0u);
    append_u16(&code, 0xf02fu);
    append_data(&code, "S.BMP\0", 6u);
    append_u16(&code, 0xa504u);
    append_u16(&code, 1u);
    append_u16(&code, 1u);
    append_u16(&code, 0u);
    append_u16(&code, 0u);
    append_u16(&code, 0x0ff0u);
    append_u16(&code, 0x0110u);

    memset(&ttm, 0, sizeof(ttm));
    ttm.bytecode = code.data;
    ttm.bytecode_size = code.size;
    require(jc_ttm_renderer_init(&renderer, 4u, 4u, 5, &resource_api,
                                 NULL, NULL, &error), error.message);
    lifecycle = event_base(JC_SCRIPT_EVENT_SCENE_STARTED, 0u);
    lifecycle.scene_slot = 0u;
    send_event(&renderer, &lifecycle);
    require(jc_ttm_vm_init(&vm, &ttm, 0u, &error), error.message);
    require(jc_ttm_vm_tick(&vm, jc_ttm_renderer_event, &renderer, &error) ==
                JC_SCRIPT_TICK_FRAME,
            error.message);
    require(resources.loads == 2u,
            "real VM bridge did not load both screen and sprite resources");
    require(output_at(&renderer, 1u, 1u) == 1u &&
                output_at(&renderer, 2u, 1u) == 3u &&
                output_at(&renderer, 1u, 2u) == 2u,
            "real VM events did not render the expected sprite frame");
    jc_ttm_renderer_destroy(&renderer);
}

int main(void)
{
    test_resources_sprites_and_palette();
    test_primitives_offsets_and_saved_zones();
    test_background_snapshot_and_errors();
    test_real_vm_event_bridge();
    puts("TTM renderer tests passed");
    return 0;
}
