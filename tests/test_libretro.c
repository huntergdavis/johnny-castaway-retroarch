/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "jc_story_options.h"
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
static bool accessibility_category_found;
static bool chapter_option_found;
static bool automatic_story_option_found;
static bool chapter_menu_metadata_complete;
static bool holiday_option_found;
static unsigned ocean_options_found;
static unsigned caption_options_found;
static unsigned story_control_options_found;
static bool controller_registered;
static bool frame_has_color;
static size_t holiday_bar_pixels;
static size_t holiday_bar_top_pixels;
static size_t holiday_bar_bottom_pixels;
static bool variable_updated;
static const char *initial_screen_value = "intro";
static const char *chapter_value = "screen";
static const char *holiday_value = "off";
static const char *story_seed_value = "24";
static const char *story_calendar_value = "system";
static const char *simulated_month_value = "1";
static const char *simulated_day_value = "1";
static const char *simulated_hour_value = "12";
static const char *playback_speed_value = "1";
static const char *tide_value = "auto";
static const char *raft_stage_value = "auto";
static const char *display_source_value = "original";
static const char *audio_enabled_value = "enabled";
static const char *audio_volume_value = "100";
static const char *ocean_enabled_value = "enabled";
static const char *ocean_volume_value = "56";
static const char *captions_enabled_value = "disabled";
static const char *caption_size_value = "medium";
static const char *caption_background_value = "bar";
static const char *caption_opacity_value = "63";
static const char *caption_position_value = "bottom";
static uint64_t frame_hash;
static bool last_audio_has_signal;
static size_t chapter_value_count;
static size_t holiday_value_count;
static unsigned automatic_scene_starts;
static unsigned automatic_walk_starts;
static unsigned automatic_fade_starts;
static unsigned runtime_error_logs;
static retro_core_options_update_display_callback_t option_display_callback;
static bool initial_screen_option_visible;
static unsigned initial_screen_visibility_updates;
static unsigned automatic_options_visible_mask;
static unsigned automatic_options_visibility_updates;
static unsigned simulated_options_visible_mask;
static unsigned simulated_options_visibility_updates;
static bool playback_speed_option_visible;
static unsigned playback_speed_visibility_updates;

typedef struct script_buffer {
    uint8_t data[1024];
    size_t size;
} script_buffer_t;

size_t jc_test_story_wave_frame(size_t position, size_t position_count,
                                uint64_t runtime_ticks);
bool jc_test_story_wave_composition(void);

static void assert_story_wave_cadence(void)
{
    assert(jc_test_story_wave_frame(0u, 3u, 0u) == 0u);
    assert(jc_test_story_wave_frame(1u, 3u, 0u) == 1u);
    assert(jc_test_story_wave_frame(2u, 3u, 0u) == 0u);
    assert(jc_test_story_wave_frame(0u, 3u, 7u) == 0u);
    assert(jc_test_story_wave_frame(2u, 3u, 8u) == 1u);
    assert(jc_test_story_wave_frame(2u, 3u, 6u) == 0u);
    assert(jc_test_story_wave_frame(2u, 3u, 9u) == 1u);
    assert(jc_test_story_wave_frame(0u, 3u, 16u) == 1u);
    assert(jc_test_story_wave_frame(1u, 3u, 24u) == 2u);
    assert(jc_test_story_wave_frame(0u, 3u, 48u) == 2u);
    assert(jc_test_story_wave_frame(0u, 3u, 72u) == 0u);

    assert(jc_test_story_wave_frame(0u, 4u, 0u) == 0u);
    assert(jc_test_story_wave_frame(1u, 4u, 0u) == 0u);
    assert(jc_test_story_wave_frame(2u, 4u, 0u) == 0u);
    assert(jc_test_story_wave_frame(3u, 4u, 0u) == 0u);
    assert(jc_test_story_wave_frame(1u, 4u, 8u) == 1u);
    assert(jc_test_story_wave_frame(2u, 4u, 16u) == 1u);
    assert(jc_test_story_wave_frame(3u, 4u, 24u) == 1u);
    assert(jc_test_story_wave_frame(0u, 4u, 32u) == 1u);
    assert(jc_test_story_wave_frame(1u, 4u, 72u) == 0u);
    assert(jc_test_story_wave_composition());
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

static uint32_t read_u32le(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint64_t read_u64le(const uint8_t *data)
{
    return (uint64_t)read_u32le(data) |
           ((uint64_t)read_u32le(data + 4u) << 32);
}

static void append_u8(script_buffer_t *buffer, uint8_t value)
{
    assert(buffer->size < sizeof(buffer->data));
    buffer->data[buffer->size++] = value;
}

static void append_u16(script_buffer_t *buffer, uint16_t value)
{
    append_u8(buffer, (uint8_t)value);
    append_u8(buffer, (uint8_t)(value >> 8));
}

static void append_u32(script_buffer_t *buffer, uint32_t value)
{
    append_u8(buffer, (uint8_t)value);
    append_u8(buffer, (uint8_t)(value >> 8));
    append_u8(buffer, (uint8_t)(value >> 16));
    append_u8(buffer, (uint8_t)(value >> 24));
}

static void append_bytes(script_buffer_t *buffer, const void *data,
                         size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    size_t index;
    for (index = 0u; index < size; ++index)
        append_u8(buffer, bytes[index]);
}

static void append_cstring(script_buffer_t *buffer, const char *text)
{
    append_bytes(buffer, text, strlen(text) + 1u);
}

static void append_op(script_buffer_t *buffer, uint16_t opcode,
                      const uint16_t *args, size_t arg_count)
{
    size_t index;
    append_u16(buffer, opcode);
    for (index = 0u; index < arg_count; ++index)
        append_u16(buffer, args[index]);
}

static void append_string_op(script_buffer_t *buffer, uint16_t opcode,
                             const char *text)
{
    append_u16(buffer, opcode);
    append_cstring(buffer, text);
    if ((buffer->size & 1u) != 0u)
        append_u8(buffer, 0u);
}

static void make_test_ads(script_buffer_t *file)
{
    script_buffer_t code = {{0}, 0u};
    uint16_t args[4];

    append_op(&code, 1u, NULL, 0u);
    args[0] = 3u;
    args[1] = 1u;
    args[2] = 0u;
    args[3] = 0u;
    append_op(&code, 0x2005u, args, 4u);
    append_op(&code, 0x1510u, NULL, 0u);

    memset(file, 0, sizeof(*file));
    append_bytes(file, "VER:", 4u);
    append_u32(file, 5u);
    append_bytes(file, "1.20\0", 5u);
    append_bytes(file, "ADS:", 4u);
    append_u32(file, 0u);
    append_bytes(file, "RES:", 4u);
    append_u32(file, 0u);
    append_u16(file, 1u);
    append_u16(file, 3u);
    append_cstring(file, "FISH.TTM");
    append_bytes(file, "SCR:", 4u);
    append_u32(file, (uint32_t)code.size + 5u);
    append_u8(file, 0u);
    append_u32(file, (uint32_t)code.size);
    append_bytes(file, code.data, code.size);
    append_bytes(file, "TAG:", 4u);
    append_u32(file, 0u);
    append_u16(file, 1u);
    append_u16(file, 1u);
    append_cstring(file, "start");
}

static void make_test_ttm(script_buffer_t *file)
{
    script_buffer_t code = {{0}, 0u};
    uint16_t argument = 1u;

    append_op(&code, 0x1111u, &argument, 1u);
    append_op(&code, 0x1021u, &argument, 1u);
    append_string_op(&code, 0xf01fu, "INTRO.SCR");
    append_string_op(&code, 0xf05fu, "JOHNCAST.PAL");
    append_op(&code, 0x0ff0u, NULL, 0u);
    append_op(&code, 0x0110u, NULL, 0u);

    memset(file, 0, sizeof(*file));
    append_bytes(file, "VER:", 4u);
    append_u32(file, 5u);
    append_bytes(file, "1.20\0", 5u);
    append_bytes(file, "PAG:", 4u);
    append_u32(file, 1u);
    append_u16(file, 0u);
    append_bytes(file, "TT3:", 4u);
    append_u32(file, (uint32_t)code.size + 5u);
    append_u8(file, 0u);
    append_u32(file, (uint32_t)code.size);
    append_bytes(file, code.data, code.size);
    append_bytes(file, "TTI:", 4u);
    append_u32(file, 0u);
    append_bytes(file, "TAG:", 4u);
    append_u32(file, 0u);
    append_u16(file, 1u);
    append_u16(file, 1u);
    append_cstring(file, "one");
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
    uint8_t map[21u + 5u * 8u] = {0};
    uint8_t palette[16u + 256u * 3u];
    uint8_t intro[35];
    uint8_t island[35];
    script_buffer_t ads;
    script_buffer_t ttm;
    FILE *archive;
    FILE *map_file;

    memcpy(map + 6u, "RESOURCE.001", 13u);
    write_u16le(map + 19u, 5u);
    make_palette(palette);
    make_screen(intro, 1u, 1u);
    make_screen(island, 2u, 3u);
    make_test_ads(&ads);
    make_test_ttm(&ttm);
    archive = fopen("build/tests/RESOURCE.001", "wb");
    assert(archive != NULL);
    write_archive_entry(archive, map + 21u, "JOHNCAST.PAL", palette,
                        sizeof(palette));
    write_archive_entry(archive, map + 29u, "INTRO.SCR", intro, sizeof(intro));
    write_archive_entry(archive, map + 37u, "ISLAND2.SCR", island,
                        sizeof(island));
    write_archive_entry(archive, map + 45u, "FISHING.ADS", ads.data,
                        ads.size);
    write_archive_entry(archive, map + 53u, "FISH.TTM", ttm.data,
                        ttm.size);
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
    char message[1024];

    va_start(arguments, format);
    (void)vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    fputs(message, stderr);
    if (strstr(message, "Johnny Castaway automatic scene:") != NULL)
        ++automatic_scene_starts;
    if (strstr(message, "Johnny Castaway automatic walk:") != NULL)
        ++automatic_walk_starts;
    if (strstr(message, "Johnny Castaway automatic fade:") != NULL)
        ++automatic_fade_starts;
    if (level == RETRO_LOG_ERROR &&
        (strstr(message, "Johnny Castaway runtime:") != NULL ||
         strstr(message, "Johnny Castaway automatic story:") != NULL))
        ++runtime_error_logs;
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
        struct retro_core_option_v2_definition *definition = options->definitions;
        options_registered = options->definitions != NULL;
        while (category != NULL && category->key != NULL) {
            if (strcmp(category->key, "story") == 0)
                story_category_found = true;
            if (strcmp(category->key, "video") == 0)
                video_category_found = true;
            if (strcmp(category->key, "audio") == 0)
                audio_category_found = true;
            if (strcmp(category->key, "accessibility") == 0)
                accessibility_category_found = true;
            ++category;
        }
        while (definition != NULL && definition->key != NULL) {
            if (strcmp(definition->key, "johnny_castaway_chapter") == 0) {
                size_t left;
                size_t right;
                bool labels_complete = true;
                bool values_unique = true;

                chapter_option_found = true;
                automatic_story_option_found =
                    definition->values[0].value != NULL &&
                    strcmp(definition->values[0].value, "automatic") == 0 &&
                    definition->default_value != NULL &&
                    strcmp(definition->default_value, "automatic") == 0;
                while (chapter_value_count < RETRO_NUM_CORE_OPTION_VALUES_MAX &&
                       definition->values[chapter_value_count].value != NULL)
                    ++chapter_value_count;
                for (left = 0u; left < chapter_value_count; ++left) {
                    if (definition->values[left].value[0] == '\0' ||
                        definition->values[left].label == NULL ||
                        definition->values[left].label[0] == '\0')
                        labels_complete = false;
                    for (right = left + 1u; right < chapter_value_count;
                         ++right) {
                        if (strcmp(definition->values[left].value,
                                   definition->values[right].value) == 0)
                            values_unique = false;
                    }
                }
                chapter_menu_metadata_complete =
                    labels_complete && values_unique &&
                    definition->category_key != NULL &&
                    strcmp(definition->category_key, "story") == 0 &&
                    definition->info != NULL &&
                    strstr(definition->info, "live in-game graphical preview") != NULL &&
                    strstr(definition->info, "do not support per-entry thumbnails") != NULL &&
                    definition->info_categorized != NULL;
            } else if (strcmp(definition->key,
                              "johnny_castaway_holiday_overlay") == 0) {
                holiday_option_found = true;
                while (holiday_value_count < RETRO_NUM_CORE_OPTION_VALUES_MAX &&
                       definition->values[holiday_value_count].value != NULL)
                    ++holiday_value_count;
            } else if (strcmp(definition->key,
                              "johnny_castaway_story_seed") == 0 ||
                       strcmp(definition->key,
                              "johnny_castaway_story_calendar") == 0 ||
                       strcmp(definition->key,
                              "johnny_castaway_simulated_month") == 0 ||
                       strcmp(definition->key,
                              "johnny_castaway_simulated_day") == 0 ||
                       strcmp(definition->key,
                              "johnny_castaway_simulated_hour") == 0 ||
                       strcmp(definition->key,
                              "johnny_castaway_playback_speed") == 0 ||
                       strcmp(definition->key,
                              "johnny_castaway_tide") == 0 ||
                       strcmp(definition->key,
                              "johnny_castaway_raft_stage") == 0) {
                ++story_control_options_found;
            } else if (strcmp(definition->key,
                              "johnny_castaway_ocean_enabled") == 0 ||
                       strcmp(definition->key,
                              "johnny_castaway_ocean_volume") == 0) {
                ++ocean_options_found;
            } else if (strcmp(definition->key,
                              "johnny_castaway_captions_enabled") == 0 ||
                       strcmp(definition->key,
                              "johnny_castaway_caption_size") == 0 ||
                       strcmp(definition->key,
                              "johnny_castaway_caption_background") == 0 ||
                       strcmp(definition->key,
                              "johnny_castaway_caption_opacity") == 0 ||
                       strcmp(definition->key,
                              "johnny_castaway_caption_position") == 0) {
                ++caption_options_found;
            }
            ++definition;
        }
        return true;
    }
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
        option_display_callback =
            ((struct retro_core_options_update_display_callback *)data)->callback;
        return option_display_callback != NULL;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY: {
        const struct retro_core_option_display *display =
            (const struct retro_core_option_display *)data;
        if (display != NULL && display->key != NULL &&
            strcmp(display->key, "johnny_castaway_initial_screen") == 0) {
            initial_screen_option_visible = display->visible;
            ++initial_screen_visibility_updates;
        } else if (display != NULL && display->key != NULL &&
                   strcmp(display->key,
                          "johnny_castaway_story_seed") == 0) {
            automatic_options_visible_mask =
                (automatic_options_visible_mask & ~1u) |
                (display->visible ? 1u : 0u);
            ++automatic_options_visibility_updates;
        } else if (display != NULL && display->key != NULL &&
                   strcmp(display->key,
                          "johnny_castaway_story_calendar") == 0) {
            automatic_options_visible_mask =
                (automatic_options_visible_mask & ~2u) |
                (display->visible ? 2u : 0u);
            ++automatic_options_visibility_updates;
        } else if (display != NULL && display->key != NULL &&
                   strcmp(display->key, "johnny_castaway_tide") == 0) {
            automatic_options_visible_mask =
                (automatic_options_visible_mask & ~4u) |
                (display->visible ? 4u : 0u);
            ++automatic_options_visibility_updates;
        } else if (display != NULL && display->key != NULL &&
                   strcmp(display->key,
                          "johnny_castaway_raft_stage") == 0) {
            automatic_options_visible_mask =
                (automatic_options_visible_mask & ~8u) |
                (display->visible ? 8u : 0u);
            ++automatic_options_visibility_updates;
        } else if (display != NULL && display->key != NULL &&
                   strcmp(display->key,
                          "johnny_castaway_simulated_month") == 0) {
            simulated_options_visible_mask =
                (simulated_options_visible_mask & ~1u) |
                (display->visible ? 1u : 0u);
            ++simulated_options_visibility_updates;
        } else if (display != NULL && display->key != NULL &&
                   strcmp(display->key,
                          "johnny_castaway_simulated_day") == 0) {
            simulated_options_visible_mask =
                (simulated_options_visible_mask & ~2u) |
                (display->visible ? 2u : 0u);
            ++simulated_options_visibility_updates;
        } else if (display != NULL && display->key != NULL &&
                   strcmp(display->key,
                          "johnny_castaway_simulated_hour") == 0) {
            simulated_options_visible_mask =
                (simulated_options_visible_mask & ~4u) |
                (display->visible ? 4u : 0u);
            ++simulated_options_visibility_updates;
        } else if (display != NULL && display->key != NULL &&
                   strcmp(display->key,
                          "johnny_castaway_playback_speed") == 0) {
            playback_speed_option_visible = display->visible;
            ++playback_speed_visibility_updates;
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
        else if (strcmp(variable->key, "johnny_castaway_chapter") == 0)
            variable->value = chapter_value;
        else if (strcmp(variable->key,
                        "johnny_castaway_holiday_overlay") == 0)
            variable->value = holiday_value;
        else if (strcmp(variable->key, "johnny_castaway_story_seed") == 0)
            variable->value = story_seed_value;
        else if (strcmp(variable->key,
                        "johnny_castaway_story_calendar") == 0)
            variable->value = story_calendar_value;
        else if (strcmp(variable->key,
                        "johnny_castaway_simulated_month") == 0)
            variable->value = simulated_month_value;
        else if (strcmp(variable->key,
                        "johnny_castaway_simulated_day") == 0)
            variable->value = simulated_day_value;
        else if (strcmp(variable->key,
                        "johnny_castaway_simulated_hour") == 0)
            variable->value = simulated_hour_value;
        else if (strcmp(variable->key,
                        "johnny_castaway_playback_speed") == 0)
            variable->value = playback_speed_value;
        else if (strcmp(variable->key, "johnny_castaway_tide") == 0)
            variable->value = tide_value;
        else if (strcmp(variable->key,
                        "johnny_castaway_raft_stage") == 0)
            variable->value = raft_stage_value;
        else if (strcmp(variable->key, "johnny_castaway_display_source") == 0)
            variable->value = display_source_value;
        else if (strcmp(variable->key, "johnny_castaway_audio_enabled") == 0)
            variable->value = audio_enabled_value;
        else if (strcmp(variable->key, "johnny_castaway_audio_volume") == 0)
            variable->value = audio_volume_value;
        else if (strcmp(variable->key, "johnny_castaway_ocean_enabled") == 0)
            variable->value = ocean_enabled_value;
        else if (strcmp(variable->key, "johnny_castaway_ocean_volume") == 0)
            variable->value = ocean_volume_value;
        else if (strcmp(variable->key, "johnny_castaway_captions_enabled") == 0)
            variable->value = captions_enabled_value;
        else if (strcmp(variable->key, "johnny_castaway_caption_size") == 0)
            variable->value = caption_size_value;
        else if (strcmp(variable->key, "johnny_castaway_caption_background") == 0)
            variable->value = caption_background_value;
        else if (strcmp(variable->key, "johnny_castaway_caption_opacity") == 0)
            variable->value = caption_opacity_value;
        else if (strcmp(variable->key, "johnny_castaway_caption_position") == 0)
            variable->value = caption_position_value;
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
    holiday_bar_pixels = 0u;
    holiday_bar_top_pixels = 0u;
    holiday_bar_bottom_pixels = 0u;
    for (index = 0u; index < count; ++index) {
        hash ^= pixels[index];
        hash *= 1099511628211ull;
        if (pixels[index] != 0u)
            frame_has_color = true;
        if (pixels[index] == 0x00132945u) {
            ++holiday_bar_pixels;
            if (index < count / 2u)
                ++holiday_bar_top_pixels;
            else
                ++holiday_bar_bottom_pixels;
        }
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
    size_t index;
    assert(data != NULL);
    last_audio_has_signal = false;
    for (index = 0u; index < frames * 2u; ++index) {
        if (data[index] != 0)
            last_audio_has_signal = true;
    }
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

static void assert_serialized_state_unchanged(const uint8_t *expected,
                                              size_t state_size,
                                              const char *case_name)
{
    uint8_t *actual = (uint8_t *)malloc(state_size);
    size_t index;

    assert(actual != NULL);
    assert(retro_serialize(actual, state_size));
    for (index = 0u; index < state_size; ++index) {
        if (actual[index] != expected[index]) {
            fprintf(stderr,
                    "save-state atomicity failure after %s at byte %zu: expected 0x%02x, got 0x%02x\n",
                    case_name, index, expected[index], actual[index]);
            assert(actual[index] == expected[index]);
        }
    }
    free(actual);
}

int main(int argc, char **argv)
{
    struct retro_game_info game = {0};
    struct retro_system_info system_info;
    uint8_t *state;
    uint8_t *roundtrip;
    uint8_t *malformed;
    uint8_t *oversized;
    uint8_t *legacy;
    size_t state_size;
    size_t legacy_size;
    uint64_t intro_hash;
    uint64_t diagnostic_hash;
    uint64_t chapter_hash;
    uint64_t restored_hash;
    bool restored_audio_signal;
    unsigned preview_startup_frames;
    unsigned audio_frames_before;
    unsigned automatic_starts_before;

    assert_story_wave_cadence();
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
           audio_category_found && accessibility_category_found);
    assert(chapter_option_found && automatic_story_option_found &&
           chapter_menu_metadata_complete && chapter_value_count == 65u);
    assert(holiday_option_found && holiday_value_count == 38u);
    assert(ocean_options_found == 2u && caption_options_found == 5u);
    assert(story_control_options_found == 8u);
    assert(jc_story_effective_raft_stage(3u, 5,
                                         JC_SCENE_NO_RAFT) == 0u);
    assert(jc_story_effective_raft_stage(3u, 5, 0u) == 5u);
    assert(jc_story_effective_raft_stage(3u, -1, 0u) == 3u);
    assert(controller_registered);
    assert(option_display_callback != NULL);
    assert(initial_screen_visibility_updates == 1u &&
           initial_screen_option_visible);
    assert(automatic_options_visibility_updates == 4u &&
           automatic_options_visible_mask == 0u);
    assert(simulated_options_visibility_updates == 3u &&
           simulated_options_visible_mask == 0u);
    assert(playback_speed_visibility_updates == 1u &&
           !playback_speed_option_visible);
    chapter_value = "automatic";
    assert(option_display_callback());
    assert(!initial_screen_option_visible);
    assert(automatic_options_visible_mask == 15u);
    assert(simulated_options_visible_mask == 0u);
    assert(playback_speed_option_visible);
    assert(!option_display_callback());
    story_calendar_value = "simulated";
    assert(option_display_callback());
    assert(simulated_options_visible_mask == 7u);
    chapter_value = "screen";
    assert(option_display_callback());
    assert(initial_screen_option_visible);
    assert(automatic_options_visible_mask == 0u);
    assert(simulated_options_visible_mask == 0u);
    assert(!playback_speed_option_visible);
    story_calendar_value = "system";

    game.path = argc == 2 ? argv[1] : make_synthetic_content();
    assert(retro_load_game(&game));
    retro_run();
    assert(video_calls == 1u && frame_has_color);
    assert(audio_frames == 882u);
    assert(last_audio_has_signal);
    intro_hash = frame_hash;

    display_source_value = "diagnostic";
    variable_updated = true;
    retro_run();
    diagnostic_hash = frame_hash;
    assert(diagnostic_hash != intro_hash);

    holiday_value = "valentines_day";
    variable_updated = true;
    retro_run();
    assert(holiday_bar_pixels > 1000u);

    holiday_value = "off";
    variable_updated = true;
    retro_run();
    assert(holiday_bar_pixels == 0u);

    display_source_value = "original";
    initial_screen_value = "island_day";
    variable_updated = true;
    retro_run();
    assert(frame_hash != intro_hash && frame_hash != diagnostic_hash);

    ocean_enabled_value = "disabled";
    variable_updated = true;
    retro_run();
    assert(!last_audio_has_signal);

    ocean_enabled_value = "enabled";
    variable_updated = true;
    retro_run();
    assert(last_audio_has_signal);

    initial_screen_value = "intro";
    variable_updated = true;
    retro_run();
    assert(frame_hash == intro_hash);

    automatic_starts_before = automatic_scene_starts;
    story_seed_value = "42";
    story_calendar_value = "simulated";
    simulated_month_value = "2";
    simulated_day_value = "31";
    simulated_hour_value = "23";
    tide_value = "low";
    raft_stage_value = "5";
    variable_updated = true;
    retro_run();
    assert(frame_hash == intro_hash);
    assert(automatic_scene_starts == automatic_starts_before);

    chapter_value = "fishing1";
    variable_updated = true;
    retro_run();
    for (preview_startup_frames = 0u;
         preview_startup_frames < 120u && frame_hash == intro_hash;
         ++preview_startup_frames)
        retro_run();
    chapter_hash = frame_hash;
    assert(chapter_hash != intro_hash && chapter_hash != diagnostic_hash);
    assert(!initial_screen_option_visible);

    automatic_starts_before = automatic_scene_starts;
    story_seed_value = "24";
    simulated_month_value = "12";
    simulated_day_value = "31";
    simulated_hour_value = "0";
    tide_value = "high";
    raft_stage_value = "0";
    playback_speed_value = "4";
    audio_frames_before = audio_frames;
    variable_updated = true;
    retro_run();
    assert(audio_frames == audio_frames_before + 882u);
    assert(automatic_scene_starts == automatic_starts_before);
    playback_speed_value = "1";
    variable_updated = true;
    retro_run();
    assert(automatic_scene_starts == automatic_starts_before);

    captions_enabled_value = "enabled";
    variable_updated = true;
    retro_run();
    assert(frame_hash != chapter_hash);

    caption_position_value = "top";
    variable_updated = true;
    retro_run();
    assert(frame_hash != chapter_hash);

    holiday_value = "valentines_day";
    variable_updated = true;
    retro_run();
    assert(holiday_bar_bottom_pixels > 1000u);
    assert(holiday_bar_top_pixels == 0u);

    holiday_value = "off";
    variable_updated = true;
    retro_run();

    audio_enabled_value = "disabled";
    audio_volume_value = "25";
    variable_updated = true;
    retro_run();
    assert(!last_audio_has_signal);

    audio_enabled_value = "enabled";
    variable_updated = true;
    retro_run();
    assert(last_audio_has_signal);

    state_size = retro_serialize_size();
    state = (uint8_t *)malloc(state_size);
    roundtrip = (uint8_t *)malloc(state_size);
    malformed = (uint8_t *)malloc(state_size);
    oversized = (uint8_t *)malloc(state_size + 1u);
    assert(state != NULL && roundtrip != NULL && malformed != NULL &&
           oversized != NULL);
    assert(retro_serialize(state, state_size));
    assert(state_size > 64u);
    assert(read_u32le(state) == 0x3253434au);
    assert(read_u32le(state + 4u) == 2u);
    assert(read_u32le(state + 8u) == 64u);
    assert(read_u32le(state + 12u) == state_size);

    chapter_value = "screen";
    captions_enabled_value = "disabled";
    variable_updated = true;
    retro_run();
    assert(retro_unserialize(state, state_size));
    assert(retro_serialize(roundtrip, state_size));
    assert(memcmp(roundtrip, state, state_size) == 0);

    retro_run();
    restored_hash = frame_hash;
    restored_audio_signal = last_audio_has_signal;
    assert(retro_unserialize(state, state_size));
    retro_run();
    assert(frame_hash == restored_hash);
    assert(last_audio_has_signal == restored_audio_signal);
    assert(retro_unserialize(state, state_size));

    assert(!retro_unserialize(state, state_size - 1u));
    assert_serialized_state_unchanged(state, state_size, "truncated input");

    memcpy(oversized, state, state_size);
    oversized[state_size] = 0u;
    assert(!retro_unserialize(oversized, state_size + 1u));
    assert_serialized_state_unchanged(state, state_size, "oversized input");

    memcpy(malformed, state, state_size);
    write_u32le(malformed + 4u, 3u);
    assert(!retro_unserialize(malformed, state_size));
    assert_serialized_state_unchanged(state, state_size, "unsupported version");

    memcpy(malformed, state, state_size);
    write_u32le(malformed + 12u, (uint32_t)state_size + 1u);
    assert(!retro_unserialize(malformed, state_size));
    assert_serialized_state_unchanged(state, state_size, "declared size mismatch");

    memcpy(malformed, state, state_size);
    write_u32le(malformed + 24u,
                read_u32le(malformed + 24u) | 0x80000000u);
    assert(!retro_unserialize(malformed, state_size));
    assert_serialized_state_unchanged(state, state_size, "unknown flags");

    memcpy(malformed, state, state_size);
    write_u32le(malformed + 28u, 999u);
    assert(!retro_unserialize(malformed, state_size));
    assert_serialized_state_unchanged(state, state_size, "chapter index");

    memcpy(malformed, state, state_size);
    write_u32le(malformed + 32u, 999u);
    assert(!retro_unserialize(malformed, state_size));
    assert_serialized_state_unchanged(state, state_size, "caption index");

    memcpy(malformed, state, state_size);
    write_u32le(malformed + 52u, 1u);
    assert(!retro_unserialize(malformed, state_size));
    assert_serialized_state_unchanged(state, state_size, "legacy reserved word");

    memcpy(malformed, state, state_size);
    write_u32le(malformed + 56u, 1u);
    assert(!retro_unserialize(malformed, state_size));
    assert_serialized_state_unchanged(state, state_size, "manual story metadata");

    legacy_size = state_size - 64u;
    legacy = (uint8_t *)malloc(legacy_size);
    assert(legacy != NULL);
    memcpy(legacy, state + 64u, legacy_size);
    retro_run();
    assert(retro_unserialize(legacy, legacy_size));
    assert(retro_unserialize(state, state_size));

    if (argc == 2) {
        unsigned frame;
        unsigned starts_before = automatic_scene_starts;
        unsigned walks_before = automatic_walk_starts;
        unsigned fades_before = automatic_fade_starts;
        uint64_t automatic_next_hash;
        bool automatic_next_audio;
        uint32_t flags;
        uint32_t compact_position;
        uint32_t old_position;
        uint64_t runtime_ticks_before;

        chapter_value = "automatic";
        captions_enabled_value = "enabled";
        variable_updated = true;
        retro_run();
        assert(automatic_scene_starts == starts_before + 1u);

        starts_before = automatic_scene_starts;
        story_seed_value = "42";
        variable_updated = true;
        retro_run();
        assert(automatic_scene_starts == starts_before + 1u);

        starts_before = automatic_scene_starts;
        story_calendar_value = "system";
        variable_updated = true;
        retro_run();
        assert(automatic_scene_starts == starts_before + 1u);

        starts_before = automatic_scene_starts;
        story_calendar_value = "simulated";
        simulated_month_value = "2";
        simulated_day_value = "31";
        simulated_hour_value = "23";
        variable_updated = true;
        retro_run();
        assert(automatic_scene_starts == starts_before + 1u);

        starts_before = automatic_scene_starts;
        tide_value = "low";
        variable_updated = true;
        retro_run();
        assert(automatic_scene_starts == starts_before + 1u);

        starts_before = automatic_scene_starts;
        raft_stage_value = "5";
        variable_updated = true;
        retro_run();
        assert(automatic_scene_starts == starts_before + 1u);

        assert(retro_serialize(state, state_size));
        runtime_ticks_before = read_u64le(state + 48u);
        playback_speed_value = "4";
        audio_frames_before = audio_frames;
        variable_updated = true;
        retro_run();
        assert(audio_frames == audio_frames_before + 882u);
        assert(automatic_scene_starts == starts_before + 1u);
        assert(retro_serialize(state, state_size));
        assert(read_u64le(state + 48u) == runtime_ticks_before + 4u);
        flags = read_u32le(state + 24u);
        compact_position = read_u32le(state + 60u);
        assert((flags & 0x80000000u) != 0u);
        assert(((flags >> 8) & 0x1ffu) == 58u);
        assert(((flags >> 17) & 0x1fu) == 23u);
        assert(((flags >> 22) & 0x0fu) == 2u);
        assert(((flags >> 26) & 0x1fu) == 28u);
        assert(((compact_position >> 25) & 1u) == 1u);
        assert(((compact_position >> 26) & 7u) == 5u);

        starts_before = automatic_scene_starts;
        story_seed_value = "24";
        story_calendar_value = "system";
        tide_value = "high";
        raft_stage_value = "0";
        variable_updated = true;
        retro_run();
        assert(automatic_scene_starts == starts_before + 1u);
        assert(retro_unserialize(state, state_size));
        assert(retro_serialize(roundtrip, state_size));
        assert(memcmp(roundtrip, state, state_size) == 0);

        memcpy(malformed, state, state_size);
        write_u32le(malformed + 24u, flags & 0xffu);
        old_position = (compact_position & 0x0fu) |
                       (((compact_position >> 4) & 0x1fu) << 8) |
                       (((compact_position >> 9) & 0x03u) << 16) |
                       (((compact_position >> 11) & 0x07u) << 18) |
                       (((compact_position >> 14) & 0x7ffu) << 21);
        write_u32le(malformed + 60u, old_position);
        story_seed_value = "42";
        story_calendar_value = "simulated";
        simulated_month_value = "2";
        simulated_day_value = "31";
        simulated_hour_value = "23";
        tide_value = "low";
        raft_stage_value = "5";
        variable_updated = true;
        retro_run();
        assert(retro_unserialize(malformed, state_size));
        assert(retro_unserialize(state, state_size));

        playback_speed_value = "1";
        variable_updated = true;
        retro_run();
        walks_before = automatic_walk_starts;
        for (frame = 0u;
             frame < 5000u && automatic_walk_starts == walks_before &&
             runtime_error_logs == 0u;
             ++frame)
            retro_run();
        assert(runtime_error_logs == 0u);
        assert(automatic_walk_starts == walks_before + 1u);
        assert(retro_serialize(state, state_size));
        compact_position = read_u32le(state + 60u);
        assert(((compact_position >> 9) & 0x3u) == 1u);
        assert(((compact_position >> 14) & 0x7ffu) == 0u);
        assert(retro_unserialize(state, state_size));
        assert(retro_serialize(roundtrip, state_size));
        assert(memcmp(roundtrip, state, state_size) == 0);
        playback_speed_value = "4";
        variable_updated = true;
        retro_run();
        assert(retro_serialize(state, state_size));
        assert(((read_u32le(state + 60u) >> 9) & 0x3u) == 1u);
        retro_run();
        automatic_next_hash = frame_hash;
        automatic_next_audio = last_audio_has_signal;
        assert(retro_unserialize(state, state_size));
        retro_run();
        assert(frame_hash == automatic_next_hash);
        assert(last_audio_has_signal == automatic_next_audio);

        for (frame = 0u;
             frame < 5000u &&
             automatic_scene_starts < starts_before + 2u &&
             runtime_error_logs == 0u;
             ++frame)
            retro_run();
        assert(runtime_error_logs == 0u);
        assert(automatic_scene_starts >= starts_before + 2u);
        assert(retro_serialize(state, state_size));
        assert((read_u32le(state + 24u) & (1u << 7)) != 0u);
        assert(read_u32le(state + 56u) != 0u);
        assert((read_u32le(state + 60u) & 0x0fu) >= 1u);
        assert(retro_serialize(roundtrip, state_size));
        assert(memcmp(roundtrip, state, state_size) == 0);
        retro_run();
        automatic_next_hash = frame_hash;
        automatic_next_audio = last_audio_has_signal;
        assert(retro_unserialize(state, state_size));
        assert(retro_serialize(roundtrip, state_size));
        assert(memcmp(roundtrip, state, state_size) == 0);
        retro_run();
        assert(frame_hash == automatic_next_hash);
        assert(last_audio_has_signal == automatic_next_audio);

        for (frame = 0u;
             frame < 30000u && automatic_fade_starts == fades_before &&
             runtime_error_logs == 0u;
             ++frame)
            retro_run();
        assert(runtime_error_logs == 0u);
        assert(automatic_fade_starts == fades_before + 1u);
        assert(retro_serialize(state, state_size));
        assert(((read_u32le(state + 60u) >> 9) & 0x3u) == 2u);
        retro_run();
        automatic_next_hash = frame_hash;
        automatic_next_audio = last_audio_has_signal;
        assert(retro_unserialize(state, state_size));
        retro_run();
        assert(frame_hash == automatic_next_hash);
        assert(last_audio_has_signal == automatic_next_audio);
        starts_before = automatic_scene_starts;
        for (frame = 0u;
             frame < 100u && automatic_scene_starts == starts_before &&
             runtime_error_logs == 0u;
             ++frame)
            retro_run();
        assert(runtime_error_logs == 0u);
        assert(automatic_scene_starts == starts_before + 1u);
        assert(retro_serialize(state, state_size));
        assert(((read_u32le(state + 60u) >> 26) & 7u) == 5u);
        assert(((read_u32le(state + 60u) >> 29) & 1u) == 1u);
        retro_run();
        automatic_next_hash = frame_hash;
        assert(retro_unserialize(state, state_size));
        assert(retro_serialize(roundtrip, state_size));
        assert(memcmp(roundtrip, state, state_size) == 0);
        retro_run();
        assert(frame_hash == automatic_next_hash);
        fprintf(stderr,
                "Johnny Castaway real-data automatic acceptance: walk/fade/rollover and state round-trips\n");
    }

    free(legacy);
    free(oversized);
    free(malformed);
    free(roundtrip);
    free(state);

    retro_unload_game();
    retro_deinit();
    puts("libretro mock-frontend test passed");
    return 0;
}
