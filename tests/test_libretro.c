/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "libretro.h"

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned video_calls;
static unsigned audio_frames;
static bool options_registered;
static bool story_category_found;
static bool video_category_found;
static bool audio_category_found;
static bool controller_registered;
static bool frame_has_color;
static bool variable_updated;
static const char *initial_screen_value = "intro";
static const char *display_source_value = "original";
static const char *audio_enabled_value = "enabled";
static const char *audio_volume_value = "100";
static uint64_t frame_hash;

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

static size_t make_palette(uint8_t *body)
{
    memset(body, 0, 16u + 256u * 3u);
    memcpy(body, "PAL:", 4u);
    memcpy(body + 8u, "VGA:", 4u);
    body[16u + 1u * 3u] = 63u;
    body[16u + 2u * 3u + 1u] = 63u;
    body[16u + 3u * 3u + 2u] = 63u;
    body[16u + 4u * 3u] = 63u;
    body[16u + 4u * 3u + 1u] = 63u;
    body[16u + 4u * 3u + 2u] = 63u;
    return 16u + 256u * 3u;
}

static size_t make_screen(uint8_t *body, uint8_t first, uint8_t second)
{
    memset(body, 0, 35u);
    memcpy(body, "SCR:", 4u);
    memcpy(body + 8u, "DIM:", 4u);
    write_u32le(body + 12u, 4u);
    write_u16le(body + 16u, 2u);
    write_u16le(body + 18u, 2u);
    memcpy(body + 20u, "BIN:", 4u);
    write_u32le(body + 24u, 7u);
    body[28u] = 0u;
    write_u32le(body + 29u, 2u);
    body[33u] = (uint8_t)((first << 4) | first);
    body[34u] = (uint8_t)((second << 4) | second);
    return 35u;
}

static void write_archive_entry(FILE *archive, uint8_t *map_entry,
                                const char *name, const uint8_t *body,
                                size_t body_size)
{
    long offset = ftell(archive);
    uint8_t header[17] = {0};
    assert(offset >= 0 && body_size <= UINT32_MAX);
    assert(strlen(name) < 13u);
    memcpy(header, name, strlen(name));
    write_u32le(header + 13u, (uint32_t)body_size);
    assert(fwrite(header, 1u, sizeof(header), archive) == sizeof(header));
    assert(fwrite(body, 1u, body_size, archive) == body_size);
    write_u32le(map_entry, (uint32_t)(sizeof(header) + body_size));
    write_u32le(map_entry + 4u, (uint32_t)offset);
}

static const char *make_synthetic_content(void)
{
    static const char map_path[] = "build/tests/RESOURCE.MAP";
    uint8_t map[21u + 3u * 8u] = {0};
    uint8_t palette[16u + 256u * 3u];
    uint8_t intro[35];
    uint8_t island[35];
    FILE *archive;
    FILE *map_file;

    memcpy(map + 6u, "RESOURCE.001", 13u);
    write_u16le(map + 19u, 3u);
    make_palette(palette);
    make_screen(intro, 1u, 1u);
    make_screen(island, 2u, 3u);
    archive = fopen("build/tests/RESOURCE.001", "wb");
    assert(archive != NULL);
    write_archive_entry(archive, map + 21u, "JOHNCAST.PAL", palette,
                        sizeof(palette));
    write_archive_entry(archive, map + 29u, "INTRO.SCR", intro, sizeof(intro));
    write_archive_entry(archive, map + 37u, "ISLAND2.SCR", island,
                        sizeof(island));
    assert(fclose(archive) == 0);
    map_file = fopen(map_path, "wb");
    assert(map_file != NULL);
    assert(fwrite(map, 1u, sizeof(map), map_file) == sizeof(map));
    assert(fclose(map_file) == 0);
    return map_path;
}

static void RETRO_CALLCONV mock_log(enum retro_log_level level, const char *format, ...)
{
    va_list arguments;
    (void)level;
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
}

static bool RETRO_CALLCONV environment(unsigned command, void *data)
{
    switch (command) {
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        *(unsigned *)data = 2u;
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2: {
        struct retro_core_options_v2 *options = (struct retro_core_options_v2 *)data;
        struct retro_core_option_v2_category *category = options->categories;
        options_registered = options->definitions != NULL;
        while (category != NULL && category->key != NULL) {
            if (strcmp(category->key, "story") == 0)
                story_category_found = true;
            if (strcmp(category->key, "video") == 0)
                video_category_found = true;
            if (strcmp(category->key, "audio") == 0)
                audio_category_found = true;
            ++category;
        }
        return true;
    }
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
        controller_registered = data != NULL;
        return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        ((struct retro_log_callback *)data)->log = mock_log;
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *variable = (struct retro_variable *)data;
        if (strcmp(variable->key, "johnny_castaway_initial_screen") == 0)
            variable->value = initial_screen_value;
        else if (strcmp(variable->key, "johnny_castaway_display_source") == 0)
            variable->value = display_source_value;
        else if (strcmp(variable->key, "johnny_castaway_audio_enabled") == 0)
            variable->value = audio_enabled_value;
        else if (strcmp(variable->key, "johnny_castaway_audio_volume") == 0)
            variable->value = audio_volume_value;
        else
            variable->value = NULL;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *(bool *)data = variable_updated;
        variable_updated = false;
        return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return *(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_XRGB8888;
    case RETRO_ENVIRONMENT_GET_VFS_INTERFACE:
        return false;
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
        return true;
    default:
        return false;
    }
}

static void RETRO_CALLCONV video(const void *data, unsigned width,
                                 unsigned height, size_t pitch)
{
    const uint32_t *pixels = (const uint32_t *)data;
    size_t count = (size_t)width * height;
    size_t index;
    uint64_t hash = 1469598103934665603ull;
    assert(data != NULL);
    assert(width == 640u && height == 480u);
    assert(pitch == width * sizeof(uint32_t));
    for (index = 0u; index < count; ++index) {
        hash ^= pixels[index];
        hash *= 1099511628211ull;
        if (pixels[index] != 0u)
            frame_has_color = true;
    }
    frame_hash = hash;
    ++video_calls;
}

static void RETRO_CALLCONV audio_sample(int16_t left, int16_t right)
{
    (void)left;
    (void)right;
}

static size_t RETRO_CALLCONV audio_batch(const int16_t *data, size_t frames)
{
    assert(data != NULL);
    audio_frames += (unsigned)frames;
    return frames;
}

static void RETRO_CALLCONV input_poll(void)
{
}

static int16_t RETRO_CALLCONV input_state(unsigned port, unsigned device,
                                          unsigned index, unsigned id)
{
    (void)port;
    (void)device;
    (void)index;
    (void)id;
    return 0;
}

int main(int argc, char **argv)
{
    struct retro_game_info game = {0};
    struct retro_system_info system_info;
    void *state;
    size_t state_size;
    uint64_t intro_hash;
    uint64_t diagnostic_hash;

    retro_set_environment(environment);
    retro_set_video_refresh(video);
    retro_set_audio_sample(audio_sample);
    retro_set_audio_sample_batch(audio_batch);
    retro_set_input_poll(input_poll);
    retro_set_input_state(input_state);
    retro_init();
    retro_get_system_info(&system_info);
    assert(strcmp(system_info.library_name, "Johnny Castaway") == 0);
    assert(options_registered && story_category_found && video_category_found &&
           audio_category_found);
    assert(controller_registered);

    game.path = argc == 2 ? argv[1] : make_synthetic_content();
    assert(retro_load_game(&game));
    retro_run();
    assert(video_calls == 1u && frame_has_color);
    assert(audio_frames == 882u);
    intro_hash = frame_hash;

    display_source_value = "diagnostic";
    variable_updated = true;
    retro_run();
    diagnostic_hash = frame_hash;
    assert(diagnostic_hash != intro_hash);

    display_source_value = "original";
    initial_screen_value = "island_day";
    variable_updated = true;
    retro_run();
    assert(frame_hash != intro_hash && frame_hash != diagnostic_hash);

    audio_enabled_value = "disabled";
    audio_volume_value = "25";
    variable_updated = true;
    retro_run();

    state_size = retro_serialize_size();
    state = malloc(state_size);
    assert(state != NULL);
    assert(retro_serialize(state, state_size));
    retro_run();
    assert(retro_unserialize(state, state_size));
    free(state);

    retro_unload_game();
    retro_deinit();
    puts("libretro mock-frontend test passed");
    return 0;
}
