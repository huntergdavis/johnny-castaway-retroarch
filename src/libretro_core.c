/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "jc_content.h"
#include "jc_core.h"
#include "jc_palette.h"
#include "jc_scr.h"
#include "libretro.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AUDIO_FRAMES_PER_VIDEO_FRAME 882u

static retro_environment_t environment_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_log_printf_t log_cb;
static const struct retro_vfs_interface *vfs;
static jc_core_t core;
static jc_content_t content;
static bool game_loaded;
static bool reset_pressed;
static int16_t silence[AUDIO_FRAMES_PER_VIDEO_FRAME * 2u];
static bool diagnostic_display;
static char selected_screen[JC_RESOURCE_NAME_BYTES + 1u] = "INTRO.SCR";

static struct retro_core_option_v2_category option_categories[] = {
    {"story", "Story", "Choose how the Johnny Castaway story begins and progresses."},
    {"video", "Video", "Select the software-rendered image shown by the core."},
    {NULL, NULL, NULL}
};

static struct retro_core_option_v2_definition option_definitions[] = {
    {
        "johnny_castaway_initial_screen",
        "Story Initial Screen",
        "Initial Screen",
        "Select the original DGDS screen displayed when content loads. Changes apply immediately.",
        NULL,
        "story",
        {
            {"intro", "Intro"},
            {"island_day", "Island — Day"},
            {"island_night", "Island — Night"},
            {"office", "Johnny's Office"},
            {"suzy_beach", "Suzy's Beach"},
            {"ending", "Ending"},
            {NULL, NULL}
        },
        "intro"
    },
    {
        "johnny_castaway_display_source",
        "Video Display Source",
        "Display Source",
        "Show an original user-supplied DGDS screen or the built-in diagnostic pattern. Changes apply immediately.",
        NULL,
        "video",
        {
            {"original", "Original Data"},
            {"diagnostic", "Diagnostic Pattern"},
            {NULL, NULL}
        },
        "original"
    },
    {0}
};

static struct retro_core_options_v2 core_options = {
    option_categories,
    option_definitions
};

static struct retro_variable legacy_options[] = {
    {"johnny_castaway_initial_screen",
     "Story Initial Screen; intro|island_day|island_night|office|suzy_beach|ending"},
    {"johnny_castaway_display_source",
     "Video Display Source; original|diagnostic"},
    {NULL, NULL}
};

static void register_core_options(retro_environment_t cb)
{
    unsigned version = 0u;
    if (cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version) && version >= 2u)
        cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2, &core_options);
    else
        cb(RETRO_ENVIRONMENT_SET_VARIABLES, legacy_options);
}

static void read_core_options(void)
{
    struct retro_variable variable;

    variable.key = "johnny_castaway_initial_screen";
    variable.value = NULL;
    if (environment_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value != NULL) {
        const char *name = "INTRO.SCR";
        if (strcmp(variable.value, "island_day") == 0)
            name = "ISLAND2.SCR";
        else if (strcmp(variable.value, "island_night") == 0)
            name = "NIGHT.SCR";
        else if (strcmp(variable.value, "office") == 0)
            name = "JOFFICE.SCR";
        else if (strcmp(variable.value, "suzy_beach") == 0)
            name = "SUZBEACH.SCR";
        else if (strcmp(variable.value, "ending") == 0)
            name = "THEEND.SCR";
        snprintf(selected_screen, sizeof(selected_screen), "%s", name);
    }

    variable.key = "johnny_castaway_display_source";
    variable.value = NULL;
    diagnostic_display = environment_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) &&
                         variable.value != NULL &&
                         strcmp(variable.value, "diagnostic") == 0;
}

static bool load_selected_frame(char *error, size_t error_size)
{
    jc_resource_info_t palette_resource;
    jc_resource_info_t screen_resource;
    jc_palette_t palette;
    jc_surface_t screen;
    uint8_t *palette_data = NULL;
    uint8_t *screen_data = NULL;
    uint8_t *pixels = NULL;
    bool success = false;

    if (diagnostic_display) {
        jc_core_clear_content_frame(&core);
        return true;
    }
    if (!jc_content_find_resource(&content, "JOHNCAST.PAL", vfs,
                                  &palette_resource, error, error_size) ||
        !jc_content_find_resource(&content, selected_screen, vfs,
                                  &screen_resource, error, error_size))
        goto cleanup;
    palette_data = (uint8_t *)malloc(palette_resource.body_size);
    screen_data = (uint8_t *)malloc(screen_resource.body_size);
    pixels = (uint8_t *)malloc(JC_FRAME_WIDTH * JC_FRAME_HEIGHT);
    if (palette_data == NULL || screen_data == NULL || pixels == NULL) {
        snprintf(error, error_size, "not enough memory to decode the intro frame");
        goto cleanup;
    }
    if (!jc_content_read_resource(&content, &palette_resource, vfs,
                                  palette_data, palette_resource.body_size,
                                  error, error_size) ||
        !jc_content_read_resource(&content, &screen_resource, vfs,
                                  screen_data, screen_resource.body_size,
                                  error, error_size) ||
        !jc_palette_decode(&palette, palette_data, palette_resource.body_size,
                           error, error_size) ||
        !jc_scr_decode(screen_data, screen_resource.body_size, pixels,
                       JC_FRAME_WIDTH * JC_FRAME_HEIGHT, &screen,
                       error, error_size) ||
        !jc_core_set_content_frame(&core, &screen, &palette))
        goto cleanup;
    success = true;

cleanup:
    free(pixels);
    free(screen_data);
    free(palette_data);
    return success;
}

static void null_video(const void *data, unsigned width, unsigned height, size_t pitch)
{
    (void)data;
    (void)width;
    (void)height;
    (void)pitch;
}

static void null_audio(int16_t left, int16_t right)
{
    (void)left;
    (void)right;
}

static size_t null_audio_batch(const int16_t *data, size_t frames)
{
    (void)data;
    return frames;
}

static void null_input_poll(void)
{
}

static int16_t null_input_state(unsigned port, unsigned device,
                                unsigned index, unsigned id)
{
    (void)port;
    (void)device;
    (void)index;
    (void)id;
    return 0;
}

void retro_set_environment(retro_environment_t cb)
{
    static const struct retro_controller_description controller_types[] = {
        {"RetroPad", RETRO_DEVICE_JOYPAD}
    };
    static const struct retro_controller_info controller_ports[] = {
        {controller_types, 1u},
        {NULL, 0u}
    };
    struct retro_log_callback logging;
    static const struct retro_input_descriptor descriptors[] = {
        {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Restart story"},
        {0, 0, 0, 0, NULL}
    };
    bool support_no_game = false;

    environment_cb = cb;
    register_core_options(cb);
    cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void *)controller_ports);
    if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging))
        log_cb = logging.log;
    cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &support_no_game);
    cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, (void *)descriptors);
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
    video_cb = cb != NULL ? cb : null_video;
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
    audio_cb = cb != NULL ? cb : null_audio;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
    audio_batch_cb = cb != NULL ? cb : null_audio_batch;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
    input_poll_cb = cb != NULL ? cb : null_input_poll;
}

void retro_set_input_state(retro_input_state_t cb)
{
    input_state_cb = cb != NULL ? cb : null_input_state;
}

void retro_init(void)
{
    video_cb = video_cb != NULL ? video_cb : null_video;
    audio_cb = audio_cb != NULL ? audio_cb : null_audio;
    audio_batch_cb = audio_batch_cb != NULL ? audio_batch_cb : null_audio_batch;
    input_poll_cb = input_poll_cb != NULL ? input_poll_cb : null_input_poll;
    input_state_cb = input_state_cb != NULL ? input_state_cb : null_input_state;
    memset(silence, 0, sizeof(silence));
    jc_core_init(&core);
}

void retro_deinit(void)
{
    game_loaded = false;
}

unsigned retro_api_version(void)
{
    return RETRO_API_VERSION;
}

void retro_get_system_info(struct retro_system_info *info)
{
    memset(info, 0, sizeof(*info));
    info->library_name = "Johnny Castaway";
    info->library_version = "0.1.0-dev";
    info->valid_extensions = "map|001";
    info->need_fullpath = true;
    info->block_extract = false;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
    memset(info, 0, sizeof(*info));
    info->geometry.base_width = JC_FRAME_WIDTH;
    info->geometry.base_height = JC_FRAME_HEIGHT;
    info->geometry.max_width = JC_FRAME_WIDTH;
    info->geometry.max_height = JC_FRAME_HEIGHT;
    info->geometry.aspect_ratio = 4.0f / 3.0f;
    info->timing.fps = JC_FRAME_RATE;
    info->timing.sample_rate = JC_AUDIO_RATE;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
    (void)port;
    (void)device;
}

void retro_reset(void)
{
    jc_core_reset(&core);
}

void retro_run(void)
{
    bool pressed;
    bool options_updated = false;

    if (environment_cb != NULL &&
        environment_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &options_updated) &&
        options_updated) {
        char previous_screen[sizeof(selected_screen)];
        bool previous_diagnostic = diagnostic_display;
        char error[256];
        memcpy(previous_screen, selected_screen, sizeof(previous_screen));
        read_core_options();
        if (game_loaded &&
            (previous_diagnostic != diagnostic_display ||
             strcmp(previous_screen, selected_screen) != 0) &&
            !load_selected_frame(error, sizeof(error)) && log_cb != NULL)
            log_cb(RETRO_LOG_ERROR, "Johnny Castaway option update: %s\n", error);
    }

    input_poll_cb();
    pressed = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0,
                             RETRO_DEVICE_ID_JOYPAD_START) != 0;
    if (pressed && !reset_pressed)
        retro_reset();
    reset_pressed = pressed;

    if (game_loaded)
        jc_core_step(&core);

    video_cb(jc_core_framebuffer(&core), JC_FRAME_WIDTH, JC_FRAME_HEIGHT,
             JC_FRAME_WIDTH * sizeof(uint32_t));
    audio_batch_cb(silence, AUDIO_FRAMES_PER_VIDEO_FRAME);
}

size_t retro_serialize_size(void)
{
    return jc_core_serialize_size();
}

bool retro_serialize(void *data, size_t size)
{
    return jc_core_serialize(&core, data, size);
}

bool retro_unserialize(const void *data, size_t size)
{
    return jc_core_unserialize(&core, data, size);
}

void retro_cheat_reset(void)
{
}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
    (void)index;
    (void)enabled;
    (void)code;
}

bool retro_load_game(const struct retro_game_info *game)
{
    enum retro_pixel_format pixel_format = RETRO_PIXEL_FORMAT_XRGB8888;
    struct retro_vfs_interface_info vfs_info;
    char error[256];

    if (game == NULL || game->path == NULL || game->path[0] == '\0')
        return false;
    if (environment_cb == NULL ||
        !environment_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &pixel_format))
        return false;
    read_core_options();

    memset(&vfs_info, 0, sizeof(vfs_info));
    vfs_info.required_interface_version = 1;
    if (environment_cb(RETRO_ENVIRONMENT_GET_VFS_INTERFACE, &vfs_info))
        vfs = vfs_info.iface;
    else
        vfs = NULL;

    if (!jc_content_load(&content, game->path, vfs, error, sizeof(error))) {
        if (log_cb != NULL)
            log_cb(RETRO_LOG_ERROR, "Johnny Castaway: %s\n", error);
        return false;
    }
    if (log_cb != NULL)
        log_cb(RETRO_LOG_INFO,
               "Johnny Castaway: indexed %u resources from %s\n",
               (unsigned)content.map.entry_count, content.archive_path);

    jc_core_reset(&core);
    if (!load_selected_frame(error, sizeof(error))) {
        if (log_cb != NULL)
            log_cb(RETRO_LOG_ERROR, "Johnny Castaway: %s\n", error);
        jc_content_unload(&content);
        return false;
    }
    game_loaded = true;
    reset_pressed = false;
    return true;
}

bool retro_load_game_special(unsigned game_type,
                             const struct retro_game_info *info,
                             size_t num_info)
{
    (void)game_type;
    (void)info;
    (void)num_info;
    return false;
}

void retro_unload_game(void)
{
    game_loaded = false;
    jc_core_clear_content_frame(&core);
    jc_content_unload(&content);
}

unsigned retro_get_region(void)
{
    return RETRO_REGION_NTSC;
}

void *retro_get_memory_data(unsigned id)
{
    (void)id;
    return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
    (void)id;
    return 0;
}
