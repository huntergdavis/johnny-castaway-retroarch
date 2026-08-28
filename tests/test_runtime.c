/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "jc_runtime.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct buffer {
    uint8_t data[4096];
    size_t size;
} buffer_t;

typedef struct event_log {
    unsigned ads_instructions;
    unsigned starts;
    unsigned stops;
    unsigned frames;
    unsigned sounds;
} event_log_t;

static const char runtime_map_path[] = "build/tests/RUNTIME.MAP";
static const char runtime_archive_path[] = "build/tests/RUNTIME.001";

static void require(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static void put_u8(buffer_t *buffer, uint8_t value)
{
    require(buffer->size < sizeof(buffer->data), "test buffer overflow");
    buffer->data[buffer->size++] = value;
}

static void put_u16(buffer_t *buffer, uint16_t value)
{
    put_u8(buffer, (uint8_t)value);
    put_u8(buffer, (uint8_t)(value >> 8));
}

static void put_u32(buffer_t *buffer, uint32_t value)
{
    put_u8(buffer, (uint8_t)value);
    put_u8(buffer, (uint8_t)(value >> 8));
    put_u8(buffer, (uint8_t)(value >> 16));
    put_u8(buffer, (uint8_t)(value >> 24));
}

static void put_bytes(buffer_t *buffer, const void *data, size_t size)
{
    const uint8_t *source = (const uint8_t *)data;
    size_t index;

    for (index = 0u; index < size; ++index)
        put_u8(buffer, source[index]);
}

static void put_cstring(buffer_t *buffer, const char *value)
{
    put_bytes(buffer, value, strlen(value) + 1u);
}

static void put_op(buffer_t *buffer, uint16_t opcode,
                   const uint16_t *args, size_t arg_count)
{
    size_t index;

    put_u16(buffer, opcode);
    for (index = 0u; index < arg_count; ++index)
        put_u16(buffer, args[index]);
}

static void put_string_op(buffer_t *buffer, uint16_t opcode,
                          const char *value)
{
    put_u16(buffer, opcode);
    put_cstring(buffer, value);
    if ((buffer->size & 1u) != 0u)
        put_u8(buffer, 0u);
}

static void make_ads(buffer_t *file, const buffer_t *code)
{
    memset(file, 0, sizeof(*file));
    put_bytes(file, "VER:", 4u);
    put_u32(file, 5u);
    put_bytes(file, "1.20\0", 5u);
    put_bytes(file, "ADS:", 4u);
    put_u32(file, 0u);
    put_bytes(file, "RES:", 4u);
    put_u32(file, 0u);
    put_u16(file, 1u);
    put_u16(file, 3u);
    put_cstring(file, "A.TTM");
    put_bytes(file, "SCR:", 4u);
    put_u32(file, (uint32_t)code->size + 5u);
    put_u8(file, 0u);
    put_u32(file, (uint32_t)code->size);
    put_bytes(file, code->data, code->size);
    put_bytes(file, "TAG:", 4u);
    put_u32(file, 0u);
    put_u16(file, 1u);
    put_u16(file, 1u);
    put_cstring(file, "start");
}

static void make_ttm(buffer_t *file, const buffer_t *code)
{
    memset(file, 0, sizeof(*file));
    put_bytes(file, "VER:", 4u);
    put_u32(file, 5u);
    put_bytes(file, "1.20\0", 5u);
    put_bytes(file, "PAG:", 4u);
    put_u32(file, 1u);
    put_u16(file, 0u);
    put_bytes(file, "TT3:", 4u);
    put_u32(file, (uint32_t)code->size + 5u);
    put_u8(file, 0u);
    put_u32(file, (uint32_t)code->size);
    put_bytes(file, code->data, code->size);
    put_bytes(file, "TTI:", 4u);
    put_u32(file, 0u);
    put_bytes(file, "TAG:", 4u);
    put_u32(file, 0u);
    put_u16(file, 1u);
    put_u16(file, 1u);
    put_cstring(file, "one");
}

static buffer_t make_screen(void)
{
    buffer_t file = {{0}, 0u};
    uint8_t packed[8];

    memset(packed, 0x33, sizeof(packed));
    put_bytes(&file, "SCR:", 4u);
    put_u16(&file, 0u);
    put_u16(&file, 0u);
    put_bytes(&file, "DIM:", 4u);
    put_u32(&file, 4u);
    put_u16(&file, 4u);
    put_u16(&file, 4u);
    put_bytes(&file, "BIN:", 4u);
    put_u32(&file, (uint32_t)sizeof(packed) + 5u);
    put_u8(&file, 0u);
    put_u32(&file, (uint32_t)sizeof(packed));
    put_bytes(&file, packed, sizeof(packed));
    return file;
}

static buffer_t make_image(void)
{
    const uint8_t packed[] = {0x15u, 0x23u};
    buffer_t file = {{0}, 0u};

    put_bytes(&file, "BMP:", 4u);
    put_u16(&file, 2u);
    put_u16(&file, 2u);
    put_bytes(&file, "INF:", 4u);
    put_u32(&file, 0u);
    put_u16(&file, 1u);
    put_u16(&file, 2u);
    put_u16(&file, 2u);
    put_bytes(&file, "BIN:", 4u);
    put_u32(&file, (uint32_t)sizeof(packed) + 5u);
    put_u8(&file, 0u);
    put_u32(&file, (uint32_t)sizeof(packed));
    put_bytes(&file, packed, sizeof(packed));
    return file;
}

static buffer_t make_palette(void)
{
    buffer_t file = {{0}, 0u};
    size_t index;

    put_bytes(&file, "PAL:", 4u);
    put_u32(&file, 0u);
    put_bytes(&file, "VGA:", 4u);
    put_u32(&file, 0u);
    for (index = 0u; index < JC_PALETTE_COLORS; ++index) {
        put_u8(&file, index == 1u ? 1u : index == 5u ? 42u : 0u);
        put_u8(&file, index == 1u ? 2u : 0u);
        put_u8(&file, index == 1u ? 3u : index == 5u ? 42u : 0u);
    }
    return file;
}

static void write_u16le(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void write_u32le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static void write_entry(FILE *archive, uint8_t *map_entry,
                        const char *name, const buffer_t *body)
{
    uint8_t header[JC_RESOURCE_NAME_BYTES + 4u] = {0};
    long offset = ftell(archive);

    require(offset >= 0, "could not determine archive offset");
    require(strlen(name) < JC_RESOURCE_NAME_BYTES,
            "synthetic resource name is too long");
    memcpy(header, name, strlen(name));
    write_u32le(header + JC_RESOURCE_NAME_BYTES, (uint32_t)body->size);
    require(fwrite(header, 1u, sizeof(header), archive) == sizeof(header),
            "could not write archive header");
    require(fwrite(body->data, 1u, body->size, archive) == body->size,
            "could not write archive body");
    write_u32le(map_entry, (uint32_t)(sizeof(header) + body->size));
    write_u32le(map_entry + 4u, (uint32_t)offset);
}

static void make_content(void)
{
    buffer_t ads_code = {{0}, 0u};
    buffer_t ttm_code = {{0}, 0u};
    buffer_t ads;
    buffer_t ttm;
    buffer_t screen = make_screen();
    buffer_t image = make_image();
    buffer_t palette = make_palette();
    uint8_t map[21u + 5u * 8u] = {0};
    uint16_t args[4];
    FILE *archive;
    FILE *map_file;

    put_op(&ads_code, 1u, NULL, 0u);
    args[0] = 3u; args[1] = 1u; args[2] = 0u; args[3] = 0u;
    put_op(&ads_code, 0x2005u, args, 4u);
    put_op(&ads_code, 0x1510u, NULL, 0u);
    make_ads(&ads, &ads_code);

    args[0] = 1u;
    put_op(&ttm_code, 0x1111u, args, 1u);
    args[0] = 1u;
    put_op(&ttm_code, 0x1021u, args, 1u);
    put_string_op(&ttm_code, 0xf01fu, "BG.SCR");
    put_string_op(&ttm_code, 0xf05fu, "P.PAL");
    args[0] = 0u;
    put_op(&ttm_code, 0x1051u, args, 1u);
    put_string_op(&ttm_code, 0xf02fu, "S.BMP");
    args[0] = 1u; args[1] = 1u; args[2] = 0u; args[3] = 0u;
    put_op(&ttm_code, 0xa504u, args, 4u);
    args[0] = 7u;
    put_op(&ttm_code, 0xc051u, args, 1u);
    put_op(&ttm_code, 0x0ff0u, NULL, 0u);
    put_op(&ttm_code, 0x0110u, NULL, 0u);
    make_ttm(&ttm, &ttm_code);

    memcpy(map + 6u, "RUNTIME.001", 12u);
    write_u16le(map + 19u, 5u);
    archive = fopen(runtime_archive_path, "wb");
    require(archive != NULL, "could not create synthetic archive");
    write_entry(archive, map + 21u, "SCENE.ADS", &ads);
    write_entry(archive, map + 29u, "A.TTM", &ttm);
    write_entry(archive, map + 37u, "BG.SCR", &screen);
    write_entry(archive, map + 45u, "S.BMP", &image);
    write_entry(archive, map + 53u, "P.PAL", &palette);
    require(fclose(archive) == 0, "could not close synthetic archive");
    map_file = fopen(runtime_map_path, "wb");
    require(map_file != NULL, "could not create synthetic map");
    require(fwrite(map, 1u, sizeof(map), map_file) == sizeof(map),
            "could not write synthetic map");
    require(fclose(map_file) == 0, "could not close synthetic map");
}

static bool record_event(void *userdata, const jc_script_event_t *event,
                         jc_script_error_t *error)
{
    event_log_t *log = (event_log_t *)userdata;

    (void)error;
    if (event->domain == JC_SCRIPT_DOMAIN_ADS_VM &&
        event->kind == JC_SCRIPT_EVENT_INSTRUCTION)
        ++log->ads_instructions;
    if (event->kind == JC_SCRIPT_EVENT_SCENE_STARTED)
        ++log->starts;
    else if (event->kind == JC_SCRIPT_EVENT_SCENE_STOPPED)
        ++log->stops;
    else if (event->kind == JC_SCRIPT_EVENT_FRAME_READY)
        ++log->frames;
    if (event->domain == JC_SCRIPT_DOMAIN_TTM_VM &&
        event->kind == JC_SCRIPT_EVENT_INSTRUCTION &&
        event->opcode == 0xc051u)
        ++log->sounds;
    return true;
}

static uint8_t output_at(const jc_runtime_t *runtime,
                         unsigned x, unsigned y)
{
    const jc_surface_t *surface = jc_runtime_output(runtime);

    require(surface != NULL && x < surface->width && y < surface->height,
            "runtime output lookup is out of bounds");
    return surface->pixels[(size_t)y * surface->pitch + x];
}

int main(void)
{
    jc_content_t content;
    jc_runtime_config_t config;
    jc_runtime_t *runtime;
    jc_script_error_t error;
    event_log_t log;
    char content_error[160];
    jc_script_tick_result_t result;
    uint8_t first_pixel;
    unsigned wait_tick;

    make_content();
    require(jc_content_load(&content, runtime_map_path, NULL,
                            content_error, sizeof(content_error)),
            content_error);
    memset(&log, 0, sizeof(log));
    memset(&config, 0, sizeof(config));
    config.content = &content;
    config.width = 4u;
    config.height = 4u;
    config.transparent_source_index = -1;
    config.event_callback = record_event;
    config.event_userdata = &log;
    runtime = (jc_runtime_t *)malloc(sizeof(*runtime));
    require(runtime != NULL, "could not allocate runtime test state");
    require(jc_runtime_init(runtime, &config, &error), error.message);
    require(jc_runtime_tick(runtime, &error) == JC_SCRIPT_TICK_ERROR &&
                error.code == JC_SCRIPT_ERROR_NULL_ARGUMENT,
            "tick before scene load did not return a structured error");

    require(jc_runtime_start_ads(runtime, "SCENE.ADS", 1u, 12345u,
                                 &error), error.message);
    require(runtime->ttm_loaded[3] && !runtime->ttm_loaded[1] &&
                runtime->vm.slots[3] == &runtime->ttms[3],
            "ADS resource id was not preserved as the exact TTM slot");
    require(jc_runtime_is_active(runtime),
            "started ADS scene is not active");
    result = jc_runtime_tick(runtime, &error);
    require(result == JC_SCRIPT_TICK_FRAME, error.message);
    require(log.starts == 1u && log.frames == 1u && log.sounds == 1u &&
                log.ads_instructions > 0u,
            "runtime did not forward complete ADS/TTM/audio events");
    require(jc_runtime_palette(runtime) != NULL &&
                runtime->renderer.transparent_source_index == 5,
            "runtime palette or transparency detection is wrong");
    require(output_at(runtime, 1u, 1u) == 1u &&
                output_at(runtime, 2u, 1u) == 3u &&
                output_at(runtime, 1u, 2u) == 2u,
            "runtime frame does not contain the expected sprite");
    first_pixel = output_at(runtime, 1u, 1u);
    for (wait_tick = 0u; wait_tick < 7u; ++wait_tick)
        require(jc_runtime_tick(runtime, &error) ==
                    JC_SCRIPT_TICK_WAITING,
                "TTM cadence did not consume seven exact waiting ticks");
    require(jc_runtime_tick(runtime, &error) == JC_SCRIPT_TICK_FINISHED,
            error.message);
    require(!jc_runtime_is_active(runtime) && log.stops == 1u,
            "runtime did not finish and stop its scene deterministically");
    require(jc_runtime_tick(runtime, &error) == JC_SCRIPT_TICK_FINISHED,
            "finished runtime tick was not stable");

    require(jc_runtime_reset(runtime, &error), error.message);
    require(!runtime->scene_loaded && jc_runtime_palette(runtime) == NULL,
            "runtime reset retained scene or palette state");
    require(jc_runtime_start_ads(runtime, "SCENE.ADS", 1u, 12345u,
                                 &error), error.message);
    require(jc_runtime_tick(runtime, &error) == JC_SCRIPT_TICK_FRAME,
            error.message);
    require(output_at(runtime, 1u, 1u) == first_pixel,
            "same seed and scene did not restart deterministically");

    require(!jc_runtime_start_ads(runtime, "MISSING.ADS", 1u, 1u,
                                  &error),
            "missing ADS resource was accepted");
    require(error.code == JC_SCRIPT_ERROR_UNBOUND_RESOURCE &&
                runtime->initialized && !runtime->scene_loaded,
            "missing ADS did not roll back to an initialized clean runtime");

    jc_runtime_destroy(runtime);
    free(runtime);
    jc_content_unload(&content);
    require(remove(runtime_map_path) == 0,
            "could not remove synthetic runtime map");
    require(remove(runtime_archive_path) == 0,
            "could not remove synthetic runtime archive");
    puts("runtime tests passed");
    return 0;
}
