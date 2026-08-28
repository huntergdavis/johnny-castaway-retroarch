/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "jc_content.h"
#include "jc_core.h"
#include "jc_audio.h"
#include "jc_caption_render.h"
#include "jc_captions.h"
#include "jc_chapters.h"
#include "jc_ocean.h"
#include "jc_palette.h"
#include "jc_runtime.h"
#include "jc_scr.h"
#include "jc_wav.h"
#include "libretro.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AUDIO_FRAMES_PER_VIDEO_FRAME 882u
#define LEGACY_CHAPTER_BYTES 1024u
#define CHAPTER_OPTION_INDEX 1u
#define LEGACY_CHAPTER_INDEX 1u

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
static jc_audio_t audio;
static int16_t audio_output[AUDIO_FRAMES_PER_VIDEO_FRAME * 2u];
static uint32_t video_output[JC_FRAME_WIDTH * JC_FRAME_HEIGHT];
static bool diagnostic_display;
static char selected_screen[JC_RESOURCE_NAME_BYTES + 1u] = "INTRO.SCR";
static const jc_chapter_t *selected_chapter;
static jc_runtime_t runtime;
static bool runtime_ready;
static bool runtime_failed;
static jc_captions_t captions;
static jc_caption_render_options_t caption_render_options;
static uint8_t *ocean_pcm;
static jc_vag_info_t ocean_info;
static bool ocean_enabled = true;
static unsigned ocean_volume = 56u;
static bool chapter_options_populated;
static char legacy_chapter_value[LEGACY_CHAPTER_BYTES];

static struct retro_core_option_v2_category option_categories[] = {
    {"story", "Story", "Choose how the Johnny Castaway story begins and progresses."},
    {"video", "Video", "Select the software-rendered image shown by the core."},
    {"audio", "Audio", "Configure the deterministic Johnny Castaway sound mixer."},
    {"accessibility", "Accessibility", "Configure closed captions and presentation aids."},
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
        "johnny_castaway_chapter",
        "Story Chapter",
        "Chapter",
        "Start a specific original scene and use its first live-rendered frame as the graphical preview. Select Static Screen to use Initial Screen instead. Changes apply immediately.",
        NULL,
        "story",
        {{NULL, NULL}},
        "screen"
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
    {
        "johnny_castaway_audio_enabled",
        "Audio Enable",
        "Audio",
        "Enable sound effects produced by the deterministic core mixer. Changes apply immediately.",
        NULL,
        "audio",
        {
            {"enabled", "Enabled"},
            {"disabled", "Disabled"},
            {NULL, NULL}
        },
        "enabled"
    },
    {
        "johnny_castaway_audio_volume",
        "Audio Volume",
        "Volume",
        "Set master volume for ambience and sound effects. Changes apply immediately.",
        NULL,
        "audio",
        {
            {"100", "100%"},
            {"75", "75%"},
            {"50", "50%"},
            {"25", "25%"},
            {"0", "0%"},
            {NULL, NULL}
        },
        "100"
    },
    {
        "johnny_castaway_ocean_enabled",
        "Ocean Ambience",
        "Ocean Ambience",
        "Play the embedded CC0 ocean loop. Changes apply immediately.",
        NULL,
        "audio",
        {
            {"enabled", "Enabled"},
            {"disabled", "Disabled"},
            {NULL, NULL}
        },
        "enabled"
    },
    {
        "johnny_castaway_ocean_volume",
        "Ocean Ambience Volume",
        "Ocean Volume",
        "Set the ocean loop's gain before master volume. Changes apply immediately.",
        NULL,
        "audio",
        {
            {"100", "100%"},
            {"75", "75%"},
            {"56", "56% — PS1 Default"},
            {"50", "50%"},
            {"25", "25%"},
            {"0", "0%"},
            {NULL, NULL}
        },
        "56"
    },
    {
        "johnny_castaway_captions_enabled",
        "Closed Captions",
        "Closed Captions",
        "Show the freshly authored scene descriptions from the PS1 port. Changes apply immediately.",
        NULL,
        "accessibility",
        {
            {"disabled", "Disabled"},
            {"enabled", "Enabled"},
            {NULL, NULL}
        },
        "disabled"
    },
    {
        "johnny_castaway_caption_size",
        "Closed Caption Size",
        "Caption Size",
        "Select the software-rendered caption text size. Changes apply immediately.",
        NULL,
        "accessibility",
        {
            {"small", "Small"},
            {"medium", "Medium"},
            {"large", "Large"},
            {NULL, NULL}
        },
        "medium"
    },
    {
        "johnny_castaway_caption_background",
        "Closed Caption Background",
        "Caption Background",
        "Draw captions without a background, in a tight box, or over a full-width band. Changes apply immediately.",
        NULL,
        "accessibility",
        {
            {"bar", "Full-width Bar"},
            {"box", "Text Box"},
            {"none", "None"},
            {NULL, NULL}
        },
        "bar"
    },
    {
        "johnny_castaway_caption_opacity",
        "Closed Caption Background Opacity",
        "Background Opacity",
        "Set caption background opacity. Changes apply immediately.",
        NULL,
        "accessibility",
        {
            {"100", "100%"},
            {"75", "75%"},
            {"63", "63% — PS1-style Default"},
            {"50", "50%"},
            {"25", "25%"},
            {"0", "0%"},
            {NULL, NULL}
        },
        "63"
    },
    {
        "johnny_castaway_caption_position",
        "Closed Caption Position",
        "Caption Position",
        "Place captions at the bottom, center, or top of the frame. Changes apply immediately.",
        NULL,
        "accessibility",
        {
            {"bottom", "Bottom"},
            {"center", "Center"},
            {"top", "Top"},
            {NULL, NULL}
        },
        "bottom"
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
    {"johnny_castaway_chapter", NULL},
    {"johnny_castaway_display_source",
     "Video Display Source; original|diagnostic"},
    {"johnny_castaway_audio_enabled",
     "Audio Enable; enabled|disabled"},
    {"johnny_castaway_audio_volume",
     "Audio Volume; 100|75|50|25|0"},
    {"johnny_castaway_ocean_enabled",
     "Ocean Ambience; enabled|disabled"},
    {"johnny_castaway_ocean_volume",
     "Ocean Ambience Volume; 56|100|75|50|25|0"},
    {"johnny_castaway_captions_enabled",
     "Closed Captions; disabled|enabled"},
    {"johnny_castaway_caption_size",
     "Closed Caption Size; medium|small|large"},
    {"johnny_castaway_caption_background",
     "Closed Caption Background; bar|box|none"},
    {"johnny_castaway_caption_opacity",
     "Closed Caption Background Opacity; 63|100|75|50|25|0"},
    {"johnny_castaway_caption_position",
     "Closed Caption Position; bottom|center|top"},
    {NULL, NULL}
};

static bool append_legacy_chapter(const char *text, size_t *length)
{
    size_t text_length;

    if (text == NULL || length == NULL)
        return false;
    text_length = strlen(text);
    if (*length >= sizeof(legacy_chapter_value) ||
        text_length >= sizeof(legacy_chapter_value) - *length)
        return false;
    memcpy(legacy_chapter_value + *length, text, text_length + 1u);
    *length += text_length;
    return true;
}

static void populate_chapter_options(void)
{
    struct retro_core_option_value *values =
        option_definitions[CHAPTER_OPTION_INDEX].values;
    size_t count = jc_chapter_count();
    size_t index;
    size_t legacy_length = 0u;

    if (chapter_options_populated)
        return;
    values[0].value = "screen";
    values[0].label = "Static Screen";
    if (count + 2u <= RETRO_NUM_CORE_OPTION_VALUES_MAX) {
        for (index = 0u; index < count; ++index) {
            const jc_chapter_t *chapter = jc_chapter_at(index);
            values[index + 1u].value = chapter->slug;
            values[index + 1u].label = chapter->title;
        }
        values[count + 1u].value = NULL;
        values[count + 1u].label = NULL;
    }

    legacy_chapter_value[0] = '\0';
    if (!append_legacy_chapter("Story Chapter; screen", &legacy_length))
        return;
    for (index = 0u; index < count; ++index) {
        const jc_chapter_t *chapter = jc_chapter_at(index);
        if (!append_legacy_chapter("|", &legacy_length) ||
            !append_legacy_chapter(chapter->slug, &legacy_length))
            break;
    }
    legacy_options[LEGACY_CHAPTER_INDEX].value = legacy_chapter_value;
    chapter_options_populated = true;
}

static void register_core_options(retro_environment_t cb)
{
    unsigned version = 0u;
    populate_chapter_options();
    if (cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version) && version >= 2u)
        cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2, &core_options);
    else
        cb(RETRO_ENVIRONMENT_SET_VARIABLES, legacy_options);
}

static void read_core_options(void)
{
    struct retro_variable variable;
    const char *value;

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

    variable.key = "johnny_castaway_chapter";
    variable.value = NULL;
    selected_chapter = NULL;
    if (environment_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) &&
        variable.value != NULL && strcmp(variable.value, "screen") != 0)
        selected_chapter = jc_chapter_lookup(variable.value);

    variable.key = "johnny_castaway_display_source";
    variable.value = NULL;
    diagnostic_display = environment_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) &&
                         variable.value != NULL &&
                         strcmp(variable.value, "diagnostic") == 0;

    variable.key = "johnny_castaway_audio_enabled";
    variable.value = NULL;
    jc_audio_set_muted(&audio,
        environment_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) &&
        variable.value != NULL && strcmp(variable.value, "disabled") == 0);

    variable.key = "johnny_castaway_audio_volume";
    variable.value = NULL;
    if (environment_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) &&
        variable.value != NULL) {
        unsigned volume = 100u;
        if (strcmp(variable.value, "75") == 0)
            volume = 75u;
        else if (strcmp(variable.value, "50") == 0)
            volume = 50u;
        else if (strcmp(variable.value, "25") == 0)
            volume = 25u;
        else if (strcmp(variable.value, "0") == 0)
            volume = 0u;
        jc_audio_set_volume(&audio, volume);
    }

    variable.key = "johnny_castaway_ocean_enabled";
    variable.value = NULL;
    if (environment_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) &&
        variable.value != NULL)
        ocean_enabled = strcmp(variable.value, "disabled") != 0;

    variable.key = "johnny_castaway_ocean_volume";
    variable.value = NULL;
    if (environment_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) &&
        variable.value != NULL) {
        value = variable.value;
        ocean_volume = 56u;
        if (strcmp(value, "100") == 0)
            ocean_volume = 100u;
        else if (strcmp(value, "75") == 0)
            ocean_volume = 75u;
        else if (strcmp(value, "50") == 0)
            ocean_volume = 50u;
        else if (strcmp(value, "25") == 0)
            ocean_volume = 25u;
        else if (strcmp(value, "0") == 0)
            ocean_volume = 0u;
    }

    variable.key = "johnny_castaway_captions_enabled";
    variable.value = NULL;
    jc_captions_set_enabled(&captions,
        environment_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) &&
        variable.value != NULL && strcmp(variable.value, "enabled") == 0);

    variable.key = "johnny_castaway_caption_size";
    variable.value = NULL;
    if (environment_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) &&
        variable.value != NULL) {
        caption_render_options.text_size = JC_CAPTION_TEXT_MEDIUM;
        if (strcmp(variable.value, "small") == 0)
            caption_render_options.text_size = JC_CAPTION_TEXT_SMALL;
        else if (strcmp(variable.value, "large") == 0)
            caption_render_options.text_size = JC_CAPTION_TEXT_LARGE;
    }

    variable.key = "johnny_castaway_caption_background";
    variable.value = NULL;
    if (environment_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) &&
        variable.value != NULL) {
        caption_render_options.background = JC_CAPTION_BACKGROUND_BAR;
        if (strcmp(variable.value, "box") == 0)
            caption_render_options.background = JC_CAPTION_BACKGROUND_BOX;
        else if (strcmp(variable.value, "none") == 0)
            caption_render_options.background = JC_CAPTION_BACKGROUND_NONE;
    }

    variable.key = "johnny_castaway_caption_opacity";
    variable.value = NULL;
    if (environment_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) &&
        variable.value != NULL) {
        value = variable.value;
        caption_render_options.background_opacity = 160u;
        if (strcmp(value, "100") == 0)
            caption_render_options.background_opacity = 255u;
        else if (strcmp(value, "75") == 0)
            caption_render_options.background_opacity = 191u;
        else if (strcmp(value, "50") == 0)
            caption_render_options.background_opacity = 128u;
        else if (strcmp(value, "25") == 0)
            caption_render_options.background_opacity = 64u;
        else if (strcmp(value, "0") == 0)
            caption_render_options.background_opacity = 0u;
    }

    variable.key = "johnny_castaway_caption_position";
    variable.value = NULL;
    if (environment_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) &&
        variable.value != NULL) {
        caption_render_options.anchor = JC_CAPTION_ANCHOR_BOTTOM;
        if (strcmp(variable.value, "center") == 0)
            caption_render_options.anchor = JC_CAPTION_ANCHOR_CENTER;
        else if (strcmp(variable.value, "top") == 0)
            caption_render_options.anchor = JC_CAPTION_ANCHOR_TOP;
    }
}

static bool runtime_event(void *userdata, const jc_script_event_t *event,
                          jc_script_error_t *error)
{
    (void)userdata;
    (void)error;
    if (event != NULL && event->kind == JC_SCRIPT_EVENT_INSTRUCTION &&
        event->domain == JC_SCRIPT_DOMAIN_TTM_VM &&
        event->opcode == 0xc051u && event->arg_count >= 1u &&
        event->args[0] < JC_AUDIO_ORIGINAL_SAMPLE_COUNT)
        (void)jc_audio_trigger(&audio, event->args[0]);
    return true;
}

static void unload_ocean_ambience(void)
{
    jc_audio_unload_sample(&audio, JC_AUDIO_AMBIENCE_SAMPLE_ID);
    free(ocean_pcm);
    ocean_pcm = NULL;
    memset(&ocean_info, 0, sizeof(ocean_info));
}

static bool apply_ocean_policy(char *error, size_t error_size)
{
    jc_wav_pcm_t pcm;

    if (ocean_pcm == NULL || !ocean_info.has_loop ||
        ocean_info.loop_start >= ocean_info.loop_end ||
        ocean_info.loop_end > ocean_info.sample_count) {
        if (error != NULL && error_size > 0u)
            snprintf(error, error_size, "ocean PCM loop is unavailable");
        return false;
    }
    pcm.samples = ocean_pcm + ocean_info.loop_start;
    pcm.sample_count = ocean_info.loop_end - ocean_info.loop_start;
    pcm.sample_rate = ocean_info.sample_rate;
    pcm.channels = JC_WAV_SOURCE_CHANNELS;
    pcm.bits_per_sample = JC_WAV_SOURCE_BITS;
    if (!jc_audio_set_sample_ex(&audio, JC_AUDIO_AMBIENCE_SAMPLE_ID,
                                &pcm, true, ocean_volume)) {
        if (error != NULL && error_size > 0u)
            snprintf(error, error_size, "could not register ocean PCM");
        return false;
    }
    if (ocean_enabled &&
        jc_audio_trigger(&audio, JC_AUDIO_AMBIENCE_SAMPLE_ID) ==
            JC_AUDIO_INVALID_VOICE) {
        if (error != NULL && error_size > 0u)
            snprintf(error, error_size, "could not start ocean ambience");
        return false;
    }
    return true;
}

static bool load_ocean_ambience(char *error, size_t error_size)
{
    jc_vag_status_t status;

    unload_ocean_ambience();
    status = jc_ocean_probe(&ocean_info);
    if (status != JC_VAG_OK) {
        if (error != NULL && error_size > 0u)
            snprintf(error, error_size, "embedded ocean: %s",
                     jc_vag_status_string(status));
        return false;
    }
    ocean_pcm = (uint8_t *)malloc(ocean_info.sample_count);
    if (ocean_pcm == NULL) {
        if (error != NULL && error_size > 0u)
            snprintf(error, error_size, "not enough memory for ocean PCM");
        return false;
    }
    status = jc_ocean_decode_u8(ocean_pcm, ocean_info.sample_count,
                                &ocean_info);
    if (status != JC_VAG_OK) {
        if (error != NULL && error_size > 0u)
            snprintf(error, error_size, "embedded ocean decode: %s",
                     jc_vag_status_string(status));
        unload_ocean_ambience();
        return false;
    }
    if (!apply_ocean_policy(error, error_size)) {
        unload_ocean_ambience();
        return false;
    }
    return true;
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

static bool initialize_story_runtime(char *error, size_t error_size)
{
    jc_runtime_config_t config;
    jc_script_error_t script_error;

    if (runtime_ready)
        jc_runtime_destroy(&runtime);
    memset(&config, 0, sizeof(config));
    config.content = &content;
    config.vfs = vfs;
    config.width = JC_FRAME_WIDTH;
    config.height = JC_FRAME_HEIGHT;
    config.transparent_source_index = -1;
    config.event_callback = runtime_event;
    if (!jc_runtime_init(&runtime, &config, &script_error)) {
        snprintf(error, error_size, "runtime init: %s", script_error.message);
        runtime_ready = false;
        return false;
    }
    runtime_ready = true;
    runtime_failed = false;
    return true;
}

static bool start_selected_presentation(char *error, size_t error_size)
{
    jc_script_error_t script_error;

    runtime_failed = false;
    jc_captions_clear(&captions);
    if (!runtime_ready) {
        snprintf(error, error_size, "story runtime is unavailable");
        return false;
    }
    if (diagnostic_display) {
        if (!jc_runtime_reset(&runtime, &script_error)) {
            snprintf(error, error_size, "runtime reset: %s",
                     script_error.message);
            return false;
        }
        jc_core_clear_content_frame(&core);
        return true;
    }
    if (selected_chapter != NULL) {
        char ads_name[JC_RESOURCE_NAME_BYTES + 1u];
        int written = snprintf(ads_name, sizeof(ads_name), "%s.ADS",
                               selected_chapter->ads_name);
        if (written < 0 || (size_t)written >= sizeof(ads_name)) {
            snprintf(error, error_size, "chapter ADS name is too long");
            return false;
        }
        if (!jc_runtime_start_ads(&runtime, ads_name,
                                  selected_chapter->ads_tag,
                                  0x4a435241u, &script_error)) {
            snprintf(error, error_size, "chapter %s: %s",
                     selected_chapter->slug, script_error.message);
            return false;
        }
        (void)jc_captions_show(&captions, selected_chapter->caption_id,
                               JC_CAPTION_DEFAULT_TICKS);
        if (log_cb != NULL)
            log_cb(RETRO_LOG_INFO, "Johnny Castaway chapter: %s\n",
                   selected_chapter->title);
        return true;
    }
    if (!jc_runtime_reset(&runtime, &script_error)) {
        snprintf(error, error_size, "runtime reset: %s",
                 script_error.message);
        return false;
    }
    return load_selected_frame(error, error_size);
}

static void tick_story_runtime(void)
{
    jc_script_error_t error;
    jc_script_tick_result_t result;

    if (!runtime_ready || runtime_failed || diagnostic_display ||
        selected_chapter == NULL)
        return;
    result = jc_runtime_tick(&runtime, &error);
    if (result == JC_SCRIPT_TICK_FRAME) {
        const jc_surface_t *surface = jc_runtime_output(&runtime);
        const jc_palette_t *palette = jc_runtime_palette(&runtime);
        if (surface != NULL && palette != NULL)
            (void)jc_core_update_content_frame(&core, surface, palette);
    } else if (result == JC_SCRIPT_TICK_ERROR) {
        runtime_failed = true;
        if (log_cb != NULL)
            log_cb(RETRO_LOG_ERROR, "Johnny Castaway runtime: %s\n",
                   error.message);
    }
}

static void present_video_frame(void)
{
    const char *caption = jc_captions_current_text(&captions);

    memcpy(video_output, jc_core_framebuffer(&core), sizeof(video_output));
    if (caption != NULL) {
        (void)jc_caption_render(video_output, JC_FRAME_WIDTH, JC_FRAME_HEIGHT,
                                JC_FRAME_WIDTH, caption, strlen(caption),
                                &caption_render_options, NULL);
    }
    video_cb(video_output, JC_FRAME_WIDTH, JC_FRAME_HEIGHT,
             JC_FRAME_WIDTH * sizeof(uint32_t));
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
    memset(audio_output, 0, sizeof(audio_output));
    memset(video_output, 0, sizeof(video_output));
    memset(&runtime, 0, sizeof(runtime));
    memset(&ocean_info, 0, sizeof(ocean_info));
    ocean_pcm = NULL;
    runtime_ready = false;
    runtime_failed = false;
    selected_chapter = NULL;
    ocean_enabled = true;
    ocean_volume = 56u;
    jc_audio_init(&audio);
    jc_captions_init(&captions);
    jc_caption_render_options_init(&caption_render_options);
    jc_core_init(&core);
}

void retro_deinit(void)
{
    if (runtime_ready) {
        jc_runtime_destroy(&runtime);
        runtime_ready = false;
    }
    unload_ocean_ambience();
    jc_audio_clear_samples(&audio);
    if (content.ready)
        jc_content_unload(&content);
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
    char error[256];

    jc_core_reset(&core);
    jc_audio_reset(&audio);
    if (ocean_enabled &&
        jc_audio_has_sample(&audio, JC_AUDIO_AMBIENCE_SAMPLE_ID))
        (void)jc_audio_trigger(&audio, JC_AUDIO_AMBIENCE_SAMPLE_ID);
    if (game_loaded && !start_selected_presentation(error, sizeof(error)) &&
        log_cb != NULL)
        log_cb(RETRO_LOG_ERROR, "Johnny Castaway reset: %s\n", error);
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
        const jc_chapter_t *previous_chapter = selected_chapter;
        bool previous_ocean_enabled = ocean_enabled;
        unsigned previous_ocean_volume = ocean_volume;
        bool previous_captions_enabled = captions.enabled;
        char error[256];
        memcpy(previous_screen, selected_screen, sizeof(previous_screen));
        read_core_options();
        if (game_loaded &&
            (previous_diagnostic != diagnostic_display ||
             previous_chapter != selected_chapter ||
             (selected_chapter == NULL &&
              strcmp(previous_screen, selected_screen) != 0)) &&
            !start_selected_presentation(error, sizeof(error)) &&
            log_cb != NULL)
            log_cb(RETRO_LOG_ERROR, "Johnny Castaway option update: %s\n", error);
        if (game_loaded &&
            (previous_ocean_enabled != ocean_enabled ||
             previous_ocean_volume != ocean_volume) &&
            !apply_ocean_policy(error, sizeof(error)) && log_cb != NULL)
            log_cb(RETRO_LOG_ERROR, "Johnny Castaway ocean option: %s\n",
                   error);
        if (!previous_captions_enabled && captions.enabled &&
            selected_chapter != NULL)
            (void)jc_captions_show(&captions, selected_chapter->caption_id,
                                   JC_CAPTION_DEFAULT_TICKS);
    }

    input_poll_cb();
    pressed = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0,
                             RETRO_DEVICE_ID_JOYPAD_START) != 0;
    if (pressed && !reset_pressed)
        retro_reset();
    reset_pressed = pressed;

    if (game_loaded) {
        jc_core_step(&core);
        tick_story_runtime();
    }

    present_video_frame();
    jc_captions_tick(&captions);
    jc_audio_mix(&audio, audio_output, AUDIO_FRAMES_PER_VIDEO_FRAME);
    audio_batch_cb(audio_output, AUDIO_FRAMES_PER_VIDEO_FRAME);
}

size_t retro_serialize_size(void)
{
    return jc_core_serialize_size() + jc_audio_serialize_size();
}

bool retro_serialize(void *data, size_t size)
{
    size_t core_size = jc_core_serialize_size();
    if (data == NULL || size < retro_serialize_size())
        return false;
    return jc_core_serialize(&core, data, core_size) &&
           jc_audio_serialize(&audio, (uint8_t *)data + core_size,
                              size - core_size);
}

bool retro_unserialize(const void *data, size_t size)
{
    size_t core_size = jc_core_serialize_size();
    if (data == NULL || size < retro_serialize_size())
        return false;
    return jc_core_unserialize(&core, data, core_size) &&
           jc_audio_unserialize(&audio, (const uint8_t *)data + core_size,
                                size - core_size);
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
    if (content.ready)
        retro_unload_game();
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
    if (!initialize_story_runtime(error, sizeof(error)) ||
        !start_selected_presentation(error, sizeof(error))) {
        if (log_cb != NULL)
            log_cb(RETRO_LOG_ERROR, "Johnny Castaway: %s\n", error);
        if (runtime_ready) {
            jc_runtime_destroy(&runtime);
            runtime_ready = false;
        }
        jc_content_unload(&content);
        return false;
    }
    if (!load_ocean_ambience(error, sizeof(error)) && log_cb != NULL)
        log_cb(RETRO_LOG_WARN, "Johnny Castaway: %s\n", error);
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
    jc_captions_clear(&captions);
    if (runtime_ready) {
        jc_runtime_destroy(&runtime);
        runtime_ready = false;
    }
    unload_ocean_ambience();
    jc_audio_clear_samples(&audio);
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
