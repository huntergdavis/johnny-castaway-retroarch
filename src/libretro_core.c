/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "jc_content.h"
#include "jc_core.h"
#include "jc_audio.h"
#include "jc_bmp.h"
#include "jc_caption_render.h"
#include "jc_captions.h"
#include "jc_chapters.h"
#include "jc_fade.h"
#include "jc_holiday_overlay.h"
#include "jc_island_walk.h"
#include "jc_ocean.h"
#include "jc_palette.h"
#include "jc_runtime.h"
#include "jc_scr.h"
#include "jc_sfx.h"
#include "jc_story_player.h"
#include "jc_wav.h"
#include "libretro.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define AUDIO_FRAMES_PER_VIDEO_FRAME 882u
#define LEGACY_CHAPTER_BYTES 1024u
#define LEGACY_HOLIDAY_BYTES 1024u
#define CHAPTER_OPTION_INDEX 1u
#define HOLIDAY_OPTION_INDEX 2u
#define LEGACY_CHAPTER_INDEX 1u
#define LEGACY_HOLIDAY_INDEX 2u
#define CHAPTER_RUNTIME_SEED 0x4a435241u
#define STORY_PLAN_SEED 24u
#define WALK_RUNTIME_SEED_XOR 0x57414c4bu

typedef enum jc_automatic_transition {
    JC_AUTOMATIC_TRANSITION_NONE = 0,
    JC_AUTOMATIC_TRANSITION_WALK,
    JC_AUTOMATIC_TRANSITION_FADE_OUT
} jc_automatic_transition_t;

#define JC_LIBRETRO_STATE_MAGIC 0x3253434au
#define JC_LIBRETRO_STATE_VERSION 2u
#define JC_LIBRETRO_STATE_HEADER_SIZE 64u
#define JC_LIBRETRO_STATE_NO_INDEX UINT32_MAX
#define JC_LIBRETRO_STATE_MAX_RUNTIME_TICKS 65536u
#define JC_STORY_STATE_TRANSITION_SHIFT 16u
#define JC_STORY_STATE_FADE_SEQUENCE_SHIFT 18u
#define JC_STORY_STATE_PROGRESS_SHIFT 21u
#define JC_STORY_STATE_TRANSITION_MASK 0x3u
#define JC_STORY_STATE_FADE_SEQUENCE_MASK 0x7u
#define JC_STORY_STATE_PROGRESS_MASK 0x7ffu

enum jc_story_state_transition {
    JC_STORY_STATE_PLAYING = 0,
    JC_STORY_STATE_WALKING,
    JC_STORY_STATE_FADING,
    JC_STORY_STATE_FADE_COMPLETE
};

#define JC_LIBRETRO_STATE_FLAG_CHAPTER (1u << 0)
#define JC_LIBRETRO_STATE_FLAG_RUNTIME_SCENE (1u << 1)
#define JC_LIBRETRO_STATE_FLAG_RUNTIME_FINISHED (1u << 2)
#define JC_LIBRETRO_STATE_FLAG_CAPTIONS_ENABLED (1u << 3)
#define JC_LIBRETRO_STATE_FLAG_CAPTION_ACTIVE (1u << 4)
#define JC_LIBRETRO_STATE_FLAG_DIAGNOSTIC (1u << 5)
#define JC_LIBRETRO_STATE_FLAG_RESET_PRESSED (1u << 6)
#define JC_LIBRETRO_STATE_FLAG_AUTOMATIC_STORY (1u << 7)
#define JC_LIBRETRO_STATE_FLAGS_ALL \
    (JC_LIBRETRO_STATE_FLAG_CHAPTER | \
     JC_LIBRETRO_STATE_FLAG_RUNTIME_SCENE | \
     JC_LIBRETRO_STATE_FLAG_RUNTIME_FINISHED | \
     JC_LIBRETRO_STATE_FLAG_CAPTIONS_ENABLED | \
     JC_LIBRETRO_STATE_FLAG_CAPTION_ACTIVE | \
     JC_LIBRETRO_STATE_FLAG_DIAGNOSTIC | \
     JC_LIBRETRO_STATE_FLAG_RESET_PRESSED | \
     JC_LIBRETRO_STATE_FLAG_AUTOMATIC_STORY)

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
static jc_sfx_t sfx;
static int16_t audio_output[AUDIO_FRAMES_PER_VIDEO_FRAME * 2u];
static uint32_t video_output[JC_FRAME_WIDTH * JC_FRAME_HEIGHT];
static bool diagnostic_display;
static char selected_screen[JC_RESOURCE_NAME_BYTES + 1u] = "INTRO.SCR";
static const jc_chapter_t *selected_chapter;
static bool automatic_story;
static jc_story_player_t story_player;
static jc_automatic_transition_t automatic_transition;
static uint32_t automatic_transition_ticks;
static uint32_t fade_sequence;
static jc_fade_t scene_fade;
static jc_island_walk_t island_walk;
static jc_bmp_t johnny_walk_sprites;
static jc_bmp_t island_sprites;
static uint8_t *walk_frame_pixels;
static jc_surface_t walk_frame;
static bool walk_resources_ready;
static jc_palette_t walk_palette;
static bool walk_palette_ready;
static jc_runtime_t *runtime;
static bool runtime_ready;
static bool runtime_failed;
static bool runtime_replay_silent;
static jc_captions_t captions;
static jc_caption_render_options_t caption_render_options;
static uint8_t *ocean_pcm;
static jc_vag_info_t ocean_info;
static bool ocean_enabled = true;
static unsigned ocean_volume = 56u;
static bool chapter_options_populated;
static char legacy_chapter_value[LEGACY_CHAPTER_BYTES];
static bool holiday_options_populated;
static char legacy_holiday_value[LEGACY_HOLIDAY_BYTES];
static jc_holiday_overlay_selection_t holiday_selection;
static bool initial_screen_visibility_known;
static bool initial_screen_option_visible;

static const char *const serialized_screen_names[] = {
    "INTRO.SCR", "ISLAND2.SCR", "NIGHT.SCR", "JOFFICE.SCR",
    "SUZBEACH.SCR", "THEEND.SCR"
};

static void write_u32le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static void write_u64le(uint8_t *data, uint64_t value)
{
    write_u32le(data, (uint32_t)value);
    write_u32le(data + 4u, (uint32_t)(value >> 32));
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

static struct retro_core_option_v2_category option_categories[] = {
    {"story", "Story", "Choose automatic playback, a static screen, a holiday overlay, or any of 63 titled scenes."},
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
        "Story Playback / Chapter",
        "Playback / Chapter",
        "Play the story automatically, show the selected static screen, or select any of 63 titled scenes. A selected scene starts immediately as a live in-game graphical preview. If the frontend pauses while its menu is open, return to the game view to see the preview; libretro core-option menus do not support per-entry thumbnails.",
        "Play automatically, use Initial Screen, or select any of 63 titled scenes. A selected scene starts immediately as a live in-game graphical preview. If the frontend pauses while its menu is open, return to the game view to see the preview; libretro core-option menus do not support per-entry thumbnails.",
        "story",
        {{NULL, NULL}},
        "automatic"
    },
    {
        "johnny_castaway_holiday_overlay",
        "Holiday Overlay",
        "Holiday Overlay",
        "Automatic uses the frontend device's local calendar date. Off hides the overlay. Every named value forces a visible title/date preview without requiring proprietary holiday artwork. Changes apply immediately.",
        NULL,
        "story",
        {{NULL, NULL}},
        "auto"
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
    {"johnny_castaway_holiday_overlay", NULL},
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

static bool append_legacy_holiday(const char *text, size_t *length)
{
    size_t text_length;

    if (text == NULL || length == NULL)
        return false;
    text_length = strlen(text);
    if (*length >= sizeof(legacy_holiday_value) ||
        text_length >= sizeof(legacy_holiday_value) - *length)
        return false;
    memcpy(legacy_holiday_value + *length, text, text_length + 1u);
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
    values[0].value = "automatic";
    values[0].label = "Automatic Story";
    values[1].value = "screen";
    values[1].label = "Static Screen";
    if (count + 3u <= RETRO_NUM_CORE_OPTION_VALUES_MAX) {
        for (index = 0u; index < count; ++index) {
            const jc_chapter_t *chapter = jc_chapter_at(index);
            values[index + 2u].value = chapter->slug;
            values[index + 2u].label = chapter->title;
        }
        values[count + 2u].value = NULL;
        values[count + 2u].label = NULL;
    }

    legacy_chapter_value[0] = '\0';
    if (!append_legacy_chapter("Story Playback / Chapter; automatic|screen",
                               &legacy_length))
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

static void populate_holiday_options(void)
{
    struct retro_core_option_value *values =
        option_definitions[HOLIDAY_OPTION_INDEX].values;
    size_t count = jc_holiday_overlay_choice_count();
    size_t index;
    size_t legacy_length = 0u;

    if (holiday_options_populated ||
        count + 1u > RETRO_NUM_CORE_OPTION_VALUES_MAX)
        return;
    for (index = 0u; index < count; ++index) {
        jc_holiday_overlay_choice_t choice;
        if (!jc_holiday_overlay_choice_at(index, &choice))
            return;
        values[index].value = choice.value;
        values[index].label = choice.label;
    }
    values[count].value = NULL;
    values[count].label = NULL;

    legacy_holiday_value[0] = '\0';
    if (!append_legacy_holiday("Holiday Overlay; auto", &legacy_length))
        return;
    for (index = 1u; index < count; ++index) {
        jc_holiday_overlay_choice_t choice;
        if (!jc_holiday_overlay_choice_at(index, &choice) ||
            !append_legacy_holiday("|", &legacy_length) ||
            !append_legacy_holiday(choice.value, &legacy_length))
            return;
    }
    legacy_options[LEGACY_HOLIDAY_INDEX].value = legacy_holiday_value;
    holiday_options_populated = true;
}

static bool RETRO_CALLCONV update_option_visibility(void)
{
    struct retro_variable variable = {
        "johnny_castaway_chapter", NULL
    };
    struct retro_core_option_display display = {
        "johnny_castaway_initial_screen", false
    };

    if (environment_cb == NULL)
        return false;
    if (environment_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) &&
        variable.value != NULL)
        display.visible = strcmp(variable.value, "screen") == 0;
    if (initial_screen_visibility_known &&
        initial_screen_option_visible == display.visible)
        return false;
    if (!environment_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &display))
        return false;
    initial_screen_visibility_known = true;
    initial_screen_option_visible = display.visible;
    return true;
}

static void register_core_options(retro_environment_t cb)
{
    unsigned version = 0u;
    populate_chapter_options();
    populate_holiday_options();
    if (cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version) && version >= 2u)
        cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2, &core_options);
    else
        cb(RETRO_ENVIRONMENT_SET_VARIABLES, legacy_options);
}

static void read_core_options(void)
{
    struct retro_variable variable;
    const char *value;
    const jc_chapter_t *active_chapter = selected_chapter;
    bool was_automatic_story = automatic_story;

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
    automatic_story = false;
    selected_chapter = NULL;
    if (environment_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) &&
        variable.value != NULL) {
        automatic_story = strcmp(variable.value, "automatic") == 0;
        if (automatic_story && was_automatic_story)
            selected_chapter = active_chapter;
        else if (!automatic_story && strcmp(variable.value, "screen") != 0)
            selected_chapter = jc_chapter_lookup(variable.value);
    }

    variable.key = "johnny_castaway_holiday_overlay";
    variable.value = NULL;
    if (environment_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) &&
        variable.value != NULL) {
        jc_holiday_overlay_selection_t selection;
        if (jc_holiday_overlay_parse(variable.value, &selection))
            holiday_selection = selection;
    }

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
    if (!runtime_replay_silent && event != NULL &&
        event->kind == JC_SCRIPT_EVENT_INSTRUCTION &&
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

static bool load_frame_into(jc_core_t *target, const char *screen_name,
                            bool diagnostic, char *error,
                            size_t error_size)
{
    jc_resource_info_t palette_resource;
    jc_resource_info_t screen_resource;
    jc_palette_t palette;
    jc_surface_t screen;
    uint8_t *palette_data = NULL;
    uint8_t *screen_data = NULL;
    uint8_t *pixels = NULL;
    bool success = false;

    if (target == NULL || screen_name == NULL) {
        snprintf(error, error_size, "invalid frame target");
        return false;
    }
    if (diagnostic) {
        jc_core_clear_content_frame(target);
        return true;
    }
    if (!jc_content_find_resource(&content, "JOHNCAST.PAL", vfs,
                                  &palette_resource, error, error_size) ||
        !jc_content_find_resource(&content, screen_name, vfs,
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
        !jc_core_set_content_frame(target, &screen, &palette))
        goto cleanup;
    if (target == &core) {
        walk_palette = palette;
        walk_palette_ready = true;
    }
    success = true;

cleanup:
    free(pixels);
    free(screen_data);
    free(palette_data);
    return success;
}

static bool load_selected_frame(char *error, size_t error_size)
{
    return load_frame_into(&core, selected_screen, diagnostic_display,
                           error, error_size);
}

static const char *chapter_location_screen(const jc_chapter_t *chapter)
{
    if (chapter != NULL && strcmp(chapter->ads_name, "JOHNNY") == 0) {
        if (chapter->ads_tag == 1u)
            return "THEEND.SCR";
        if (chapter->ads_tag == 6u)
            return "JOFFICE.SCR";
    }
    if (chapter != NULL && strcmp(chapter->ads_name, "SUZY") == 0)
        return "SUZBEACH.SCR";
    return "ISLAND2.SCR";
}

static bool load_chapter_location(const jc_chapter_t *chapter,
                                  char *error, size_t error_size)
{
    return chapter != NULL &&
           load_frame_into(&core, chapter_location_screen(chapter), false,
                           error, error_size);
}

static void reset_automatic_transition(void)
{
    automatic_transition = JC_AUTOMATIC_TRANSITION_NONE;
    automatic_transition_ticks = 0u;
    jc_fade_init(&scene_fade);
    jc_island_walk_cancel(&island_walk);
}

static void unload_walk_resources(void)
{
    reset_automatic_transition();
    jc_island_walk_destroy(&island_walk);
    jc_bmp_free(&island_sprites);
    jc_bmp_free(&johnny_walk_sprites);
    free(walk_frame_pixels);
    walk_frame_pixels = NULL;
    memset(&walk_frame, 0, sizeof(walk_frame));
    walk_resources_ready = false;
    memset(&walk_palette, 0, sizeof(walk_palette));
    walk_palette_ready = false;
}

static bool load_content_bmp(const char *name, jc_bmp_t *bmp,
                             char *error, size_t error_size)
{
    jc_resource_info_t resource;
    uint8_t *data;
    bool success;

    if (!jc_content_find_resource(&content, name, vfs, &resource,
                                  error, error_size))
        return false;
    if (resource.body_size == 0u ||
        resource.body_size > JC_RUNTIME_MAX_RESOURCE_BYTES) {
        snprintf(error, error_size, "%s exceeds the walk resource limit",
                 name);
        return false;
    }
    data = (uint8_t *)malloc(resource.body_size);
    if (data == NULL) {
        snprintf(error, error_size, "not enough memory to decode %s", name);
        return false;
    }
    success = jc_content_read_resource(&content, &resource, vfs, data,
                                       resource.body_size, error,
                                       error_size) &&
              jc_bmp_parse(bmp, data, resource.body_size, error, error_size);
    free(data);
    return success;
}

static bool load_walk_resources(char *error, size_t error_size)
{
    bool have_island_sprites;

    unload_walk_resources();
    if (!load_content_bmp("JOHNWALK.BMP", &johnny_walk_sprites,
                          error, error_size))
        return false;
    have_island_sprites = load_content_bmp("BACKGRND.BMP", &island_sprites,
                                           error, error_size);
    if (!have_island_sprites) {
        jc_bmp_free(&island_sprites);
        if (error != NULL && error_size > 0u)
            error[0] = '\0';
    }
    walk_frame_pixels =
        (uint8_t *)malloc((size_t)JC_FRAME_WIDTH * JC_FRAME_HEIGHT);
    if (walk_frame_pixels == NULL ||
        !jc_surface_init(&walk_frame, walk_frame_pixels,
                         (size_t)JC_FRAME_WIDTH * JC_FRAME_HEIGHT,
                         JC_FRAME_WIDTH, JC_FRAME_HEIGHT, JC_FRAME_WIDTH) ||
        !jc_island_walk_init(&island_walk, JC_FRAME_WIDTH, JC_FRAME_HEIGHT,
                             5) ||
        !jc_island_walk_bind_sprites(
            &island_walk, &johnny_walk_sprites,
            have_island_sprites ? &island_sprites : NULL)) {
        snprintf(error, error_size, "could not initialize island walking: %s",
                 jc_island_walk_error(&island_walk));
        unload_walk_resources();
        return false;
    }
    walk_resources_ready = true;
    return true;
}

static bool initialize_story_runtime(char *error, size_t error_size)
{
    jc_runtime_config_t config;
    jc_script_error_t script_error;
    jc_runtime_t *candidate;

    if (runtime != NULL) {
        jc_runtime_destroy(runtime);
        free(runtime);
        runtime = NULL;
    }
    runtime_ready = false;
    candidate = (jc_runtime_t *)malloc(sizeof(*candidate));
    if (candidate == NULL) {
        snprintf(error, error_size, "not enough memory for story runtime");
        return false;
    }
    memset(&config, 0, sizeof(config));
    config.content = &content;
    config.vfs = vfs;
    config.width = JC_FRAME_WIDTH;
    config.height = JC_FRAME_HEIGHT;
    config.transparent_source_index = -1;
    config.event_callback = runtime_event;
    if (!jc_runtime_init(candidate, &config, &script_error)) {
        snprintf(error, error_size, "runtime init: %s", script_error.message);
        free(candidate);
        return false;
    }
    runtime = candidate;
    runtime_ready = true;
    runtime_failed = false;
    return true;
}

static void current_story_calendar(int *yday, uint8_t *hour,
                                   uint8_t *month, uint8_t *month_day)
{
    time_t now = time(NULL);
    struct tm *calendar = now == (time_t)-1 ? NULL : localtime(&now);

    *yday = 0;
    *hour = 12u;
    *month = 1u;
    *month_day = 1u;
    if (calendar != NULL) {
        *yday = calendar->tm_yday;
        *hour = (uint8_t)calendar->tm_hour;
        *month = (uint8_t)(calendar->tm_mon + 1);
        *month_day = (uint8_t)calendar->tm_mday;
    }
}

static bool start_automatic_scene(char *error, size_t error_size)
{
    const jc_scene_play_t *play = jc_story_player_current(&story_player);
    const jc_chapter_t *chapter;
    jc_script_error_t script_error;
    int offset_x = 0;
    int offset_y = 0;

    if (play == NULL) {
        snprintf(error, error_size, "automatic story has no current scene");
        return false;
    }
    chapter = jc_chapter_for_ads(play->scene.ads_name,
                                 play->scene.ads_tag);
    if (chapter == NULL) {
        snprintf(error, error_size,
                 "automatic scene %s:%u has no chapter mapping",
                 play->scene.ads_name, (unsigned)play->scene.ads_tag);
        return false;
    }
    if (!load_chapter_location(chapter, error, error_size))
        return false;
    if (!jc_runtime_start_ads(runtime, play->scene.ads_name,
                              play->scene.ads_tag,
                              jc_story_player_runtime_seed(&story_player),
                              &script_error)) {
        snprintf(error, error_size, "automatic scene %s:%u: %s",
                 play->scene.ads_name, (unsigned)play->scene.ads_tag,
                 script_error.message);
        return false;
    }
    if ((play->scene.flags & JC_SCENE_ISLAND) != 0u) {
        offset_x = story_player.run.island.x +
                   (play->left_island ? 272 : 0);
        offset_y = story_player.run.island.y;
    }
    if (!jc_ttm_renderer_set_offset(&runtime->renderer, offset_x, offset_y,
                                    &script_error)) {
        snprintf(error, error_size, "automatic scene offset: %s",
                 script_error.message);
        return false;
    }
    selected_chapter = chapter;
    (void)jc_captions_show_ads(&captions, play->scene.ads_name,
                               play->scene.ads_tag,
                               JC_CAPTION_DEFAULT_TICKS);
    if (log_cb != NULL)
        log_cb(RETRO_LOG_INFO,
               "Johnny Castaway automatic scene: day=%u plan=%u scene=%u/%u %s:%u\n",
               (unsigned)story_player.director.current_day,
               (unsigned)story_player.plan_seed,
               (unsigned)(story_player.scene_index + 1u),
               (unsigned)story_player.run.scene_count,
               play->scene.ads_name, (unsigned)play->scene.ads_tag);
    return true;
}

static bool restart_automatic_story(char *error, size_t error_size)
{
    int yday;
    uint8_t hour;
    uint8_t month;
    uint8_t month_day;

    current_story_calendar(&yday, &hour, &month, &month_day);
    if (!jc_story_player_start(&story_player, STORY_PLAN_SEED, 1u, yday,
                               hour, month, month_day)) {
        snprintf(error, error_size, "could not plan automatic story");
        return false;
    }
    fade_sequence = 0u;
    reset_automatic_transition();
    return start_automatic_scene(error, error_size);
}

static bool start_selected_presentation(char *error, size_t error_size)
{
    jc_script_error_t script_error;

    runtime_failed = false;
    reset_automatic_transition();
    jc_captions_clear(&captions);
    if (!runtime_ready || runtime == NULL) {
        snprintf(error, error_size, "story runtime is unavailable");
        return false;
    }
    if (diagnostic_display) {
        memset(&story_player, 0, sizeof(story_player));
        selected_chapter = NULL;
        if (!jc_runtime_reset(runtime, &script_error)) {
            snprintf(error, error_size, "runtime reset: %s",
                     script_error.message);
            return false;
        }
        jc_core_clear_content_frame(&core);
        return true;
    }
    if (automatic_story)
        return restart_automatic_story(error, error_size);
    memset(&story_player, 0, sizeof(story_player));
    if (selected_chapter != NULL) {
        char ads_name[JC_RESOURCE_NAME_BYTES + 1u];
        int written = snprintf(ads_name, sizeof(ads_name), "%s.ADS",
                               selected_chapter->ads_name);
        if (written < 0 || (size_t)written >= sizeof(ads_name)) {
            snprintf(error, error_size, "chapter ADS name is too long");
            return false;
        }
        if (!load_chapter_location(selected_chapter, error, error_size))
            return false;
        if (!jc_runtime_start_ads(runtime, ads_name,
                                  selected_chapter->ads_tag,
                                  CHAPTER_RUNTIME_SEED, &script_error)) {
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
    if (!jc_runtime_reset(runtime, &script_error)) {
        snprintf(error, error_size, "runtime reset: %s",
                 script_error.message);
        return false;
    }
    return load_selected_frame(error, error_size);
}

static bool advance_automatic_scene(char *error, size_t error_size)
{
    int yday;
    uint8_t hour;
    uint8_t month;
    uint8_t month_day;

    current_story_calendar(&yday, &hour, &month, &month_day);
    if (!jc_story_player_advance(&story_player, yday, hour, month,
                                 month_day)) {
        snprintf(error, error_size,
                 "could not advance the automatic story plan");
        return false;
    }
    return start_automatic_scene(error, error_size);
}

static bool begin_automatic_walk(const jc_scene_play_t *next,
                                 char *error, size_t error_size)
{
    const jc_surface_t *clean_island;
    jc_rng_t walk_rng;

    if (next == NULL || !next->has_walk_from ||
        next->scene.spot_start >= JC_SPOT_COUNT ||
        next->scene.heading_start >= JC_HEADING_COUNT) {
        snprintf(error, error_size, "next scene has invalid walk metadata");
        return false;
    }
    clean_island = &runtime->renderer.background;
    jc_rng_init(&walk_rng,
                jc_story_player_runtime_seed(&story_player) ^
                    WALK_RUNTIME_SEED_XOR ^
                    (uint32_t)(story_player.scene_index + 1u));
    if (!jc_island_walk_capture(&island_walk, clean_island) ||
        !jc_island_walk_begin(
            &island_walk, next->walk_from_spot, next->walk_from_heading,
            next->scene.spot_start, next->scene.heading_start,
            story_player.run.island.x, story_player.run.island.y,
            &walk_rng)) {
        snprintf(error, error_size, "%s", jc_island_walk_error(&island_walk));
        return false;
    }
    automatic_transition = JC_AUTOMATIC_TRANSITION_WALK;
    automatic_transition_ticks = 0u;
    if (log_cb != NULL)
        log_cb(RETRO_LOG_INFO,
               "Johnny Castaway automatic walk: %u/%u -> %u/%u\n",
               (unsigned)next->walk_from_spot,
               (unsigned)next->walk_from_heading,
               (unsigned)next->scene.spot_start,
               (unsigned)next->scene.heading_start);
    return true;
}

static bool begin_automatic_fade(char *error, size_t error_size)
{
    jc_fade_style_t style = jc_fade_style_for_sequence(fade_sequence);

    if (!jc_fade_begin(&scene_fade, JC_FADE_OUT, style)) {
        snprintf(error, error_size, "could not start sequence fade");
        return false;
    }
    automatic_transition = JC_AUTOMATIC_TRANSITION_FADE_OUT;
    automatic_transition_ticks = 0u;
    if (log_cb != NULL)
        log_cb(RETRO_LOG_INFO,
               "Johnny Castaway automatic fade: style=%u steps=%u\n",
               (unsigned)style,
               (unsigned)jc_fade_default_steps(style));
    return true;
}

static void tick_story_runtime(void)
{
    jc_script_error_t error;
    jc_script_tick_result_t result;
    char message[256];

    if (!runtime_ready || runtime == NULL || runtime_failed || diagnostic_display ||
        selected_chapter == NULL)
        return;
    if (automatic_story &&
        automatic_transition == JC_AUTOMATIC_TRANSITION_WALK) {
        jc_island_walk_result_t walk_result =
            jc_island_walk_tick(&island_walk, &walk_frame);
        const jc_palette_t *palette =
            walk_palette_ready ? &walk_palette : jc_runtime_palette(runtime);

        ++automatic_transition_ticks;
        if ((walk_result == JC_ISLAND_WALK_ACTIVE ||
             walk_result == JC_ISLAND_WALK_ARRIVED) && palette != NULL &&
            jc_core_update_content_frame(&core, &walk_frame, palette)) {
            if (walk_result == JC_ISLAND_WALK_ARRIVED) {
                reset_automatic_transition();
                if (!advance_automatic_scene(message, sizeof(message)))
                    runtime_failed = true;
            }
        } else if (walk_result == JC_ISLAND_WALK_ERROR) {
            snprintf(message, sizeof(message), "island walk: %s",
                     jc_island_walk_error(&island_walk));
            runtime_failed = true;
        } else if (palette == NULL) {
            snprintf(message, sizeof(message),
                     "island walk lost the scene palette");
            runtime_failed = true;
        } else {
            snprintf(message, sizeof(message),
                     "island walk frame conversion failed");
            runtime_failed = true;
        }
        if (runtime_failed && log_cb != NULL)
            log_cb(RETRO_LOG_ERROR,
                   "Johnny Castaway automatic story: %s\n", message);
        return;
    }
    if (automatic_story &&
        automatic_transition == JC_AUTOMATIC_TRANSITION_FADE_OUT) {
        if (jc_fade_is_active(&scene_fade))
            return;
        reset_automatic_transition();
        fade_sequence = (fade_sequence + 1u) % JC_FADE_STYLE_COUNT;
        if (!advance_automatic_scene(message, sizeof(message))) {
            runtime_failed = true;
            if (log_cb != NULL)
                log_cb(RETRO_LOG_ERROR,
                       "Johnny Castaway automatic story: %s\n", message);
        }
        return;
    }
    result = jc_runtime_tick(runtime, &error);
    if (result == JC_SCRIPT_TICK_FRAME) {
        const jc_surface_t *surface = jc_runtime_output(runtime);
        const jc_palette_t *palette = jc_runtime_palette(runtime);
        if (surface != NULL && palette != NULL) {
            walk_palette = *palette;
            walk_palette_ready = true;
            (void)jc_core_update_content_frame(&core, surface, palette);
        }
    } else if (result == JC_SCRIPT_TICK_ERROR) {
        runtime_failed = true;
        if (log_cb != NULL)
            log_cb(RETRO_LOG_ERROR, "Johnny Castaway runtime: %s\n",
                   error.message);
    } else if (result == JC_SCRIPT_TICK_FINISHED && automatic_story) {
        bool is_final = story_player.scene_index + 1u >=
                        story_player.run.scene_count;

        if (is_final) {
            if (!begin_automatic_fade(message, sizeof(message)))
                runtime_failed = true;
        } else {
            const jc_scene_play_t *next =
                &story_player.run.scenes[story_player.scene_index + 1u];
            if (next->has_walk_from && walk_resources_ready) {
                if (!begin_automatic_walk(next, message, sizeof(message)))
                    runtime_failed = true;
            } else if (!advance_automatic_scene(message, sizeof(message))) {
                runtime_failed = true;
            }
        }
        if (runtime_failed && log_cb != NULL)
            log_cb(RETRO_LOG_ERROR,
                   "Johnny Castaway automatic story: %s\n", message);
    }
}

static void present_video_frame(void)
{
    const char *caption = jc_captions_current_text(&captions);
    const jc_holiday_extra_t *holiday = NULL;
    jc_caption_anchor_t holiday_anchor = JC_CAPTION_ANCHOR_TOP;

    memcpy(video_output, jc_core_framebuffer(&core), sizeof(video_output));
    if (caption != NULL) {
        (void)jc_caption_render(video_output, JC_FRAME_WIDTH, JC_FRAME_HEIGHT,
                                JC_FRAME_WIDTH, caption, strlen(caption),
                                &caption_render_options, NULL);
    }
    if (holiday_selection.mode == JC_HOLIDAY_OVERLAY_AUTO) {
        time_t now = time(NULL);
        struct tm *calendar = now == (time_t)-1 ? NULL : localtime(&now);
        if (calendar != NULL) {
            holiday = jc_holiday_overlay_resolve(
                &holiday_selection, calendar->tm_year + 1900,
                calendar->tm_mon + 1, calendar->tm_mday);
        }
    } else {
        holiday = jc_holiday_overlay_resolve(&holiday_selection, 2000, 1, 1);
    }
    if (holiday != NULL) {
        if (caption != NULL &&
            caption_render_options.anchor == JC_CAPTION_ANCHOR_TOP)
            holiday_anchor = JC_CAPTION_ANCHOR_BOTTOM;
        (void)jc_holiday_overlay_render_anchored(
            video_output, JC_FRAME_WIDTH, JC_FRAME_HEIGHT, JC_FRAME_WIDTH,
            holiday, holiday_anchor, NULL);
    }
    if (automatic_transition == JC_AUTOMATIC_TRANSITION_FADE_OUT &&
        jc_fade_is_active(&scene_fade)) {
        (void)jc_fade_apply(&scene_fade, video_output, JC_FRAME_WIDTH,
                            JC_FRAME_HEIGHT, JC_FRAME_WIDTH, 0x00000000u);
        jc_fade_advance(&scene_fade);
        ++automatic_transition_ticks;
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
    static const struct retro_core_options_update_display_callback
        display_callback = {update_option_visibility};
    bool support_no_game = false;

    environment_cb = cb;
    initial_screen_visibility_known = false;
    register_core_options(cb);
    cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK,
       (void *)&display_callback);
    (void)update_option_visibility();
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
    runtime = NULL;
    memset(&ocean_info, 0, sizeof(ocean_info));
    ocean_pcm = NULL;
    runtime_ready = false;
    runtime_failed = false;
    runtime_replay_silent = false;
    selected_chapter = NULL;
    automatic_story = false;
    memset(&story_player, 0, sizeof(story_player));
    automatic_transition = JC_AUTOMATIC_TRANSITION_NONE;
    automatic_transition_ticks = 0u;
    fade_sequence = 0u;
    jc_fade_init(&scene_fade);
    memset(&island_walk, 0, sizeof(island_walk));
    memset(&johnny_walk_sprites, 0, sizeof(johnny_walk_sprites));
    memset(&island_sprites, 0, sizeof(island_sprites));
    walk_frame_pixels = NULL;
    memset(&walk_frame, 0, sizeof(walk_frame));
    walk_resources_ready = false;
    memset(&walk_palette, 0, sizeof(walk_palette));
    walk_palette_ready = false;
    holiday_selection.mode = JC_HOLIDAY_OVERLAY_AUTO;
    holiday_selection.holiday_id = 0;
    ocean_enabled = true;
    ocean_volume = 56u;
    jc_audio_init(&audio);
    jc_sfx_init(&sfx);
    jc_captions_init(&captions);
    jc_caption_render_options_init(&caption_render_options);
    jc_core_init(&core);
}

void retro_deinit(void)
{
    if (runtime != NULL) {
        jc_runtime_destroy(runtime);
        free(runtime);
        runtime = NULL;
    }
    runtime_ready = false;
    unload_walk_resources();
    unload_ocean_ambience();
    jc_sfx_unload(&sfx, &audio);
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
        bool previous_automatic_story = automatic_story;
        bool previous_ocean_enabled = ocean_enabled;
        unsigned previous_ocean_volume = ocean_volume;
        bool previous_captions_enabled = captions.enabled;
        char error[256];
        memcpy(previous_screen, selected_screen, sizeof(previous_screen));
        read_core_options();
        (void)update_option_visibility();
        if (game_loaded &&
            (previous_diagnostic != diagnostic_display ||
             previous_automatic_story != automatic_story ||
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

static uint32_t serialized_chapter_index(const jc_chapter_t *chapter)
{
    size_t index;

    if (chapter == NULL)
        return JC_LIBRETRO_STATE_NO_INDEX;
    for (index = 0u; index < jc_chapter_count(); ++index) {
        if (jc_chapter_at(index) == chapter)
            return (uint32_t)index;
    }
    return JC_LIBRETRO_STATE_NO_INDEX;
}

static uint32_t serialized_caption_index(const jc_caption_entry_t *caption)
{
    size_t index;

    if (caption == NULL)
        return JC_LIBRETRO_STATE_NO_INDEX;
    for (index = 0u; index < jc_caption_count(); ++index) {
        if (jc_caption_at(index) == caption)
            return (uint32_t)index;
    }
    return JC_LIBRETRO_STATE_NO_INDEX;
}

static uint32_t serialized_screen_index(const char *screen_name)
{
    size_t index;

    for (index = 0u;
         index < sizeof(serialized_screen_names) /
                     sizeof(serialized_screen_names[0]);
         ++index) {
        if (strcmp(serialized_screen_names[index], screen_name) == 0)
            return (uint32_t)index;
    }
    return JC_LIBRETRO_STATE_NO_INDEX;
}

static bool init_runtime_candidate(jc_runtime_t *candidate,
                                   jc_script_error_t *error)
{
    jc_runtime_config_t config;

    memset(candidate, 0, sizeof(*candidate));
    memset(&config, 0, sizeof(config));
    config.content = &content;
    config.vfs = vfs;
    config.width = JC_FRAME_WIDTH;
    config.height = JC_FRAME_HEIGHT;
    config.transparent_source_index = -1;
    config.event_callback = runtime_event;
    return jc_runtime_init(candidate, &config, error);
}

static bool restore_runtime_candidate(jc_runtime_t *candidate,
                                      const jc_chapter_t *chapter,
                                      bool diagnostic, bool has_scene,
                                      bool finished, uint64_t ticks,
                                      uint32_t seed, int offset_x,
                                      int offset_y)
{
    jc_script_error_t error;
    char ads_name[JC_RESOURCE_NAME_BYTES + 1u];
    uint64_t index;
    bool success = false;
    int written;

    if (!init_runtime_candidate(candidate, &error))
        return false;
    if (chapter == NULL || diagnostic) {
        return !has_scene && !finished && ticks == 0u;
    }
    if (!has_scene)
        goto failed;
    written = snprintf(ads_name, sizeof(ads_name), "%s.ADS",
                       chapter->ads_name);
    if (written < 0 || (size_t)written >= sizeof(ads_name))
        goto failed;
    runtime_replay_silent = true;
    if (!jc_runtime_start_ads(candidate, ads_name, chapter->ads_tag,
                              seed, &error))
        goto failed;
    if (!jc_ttm_renderer_set_offset(&candidate->renderer, offset_x, offset_y,
                                    &error))
        goto failed;
    for (index = 0u; index < ticks; ++index) {
        if (jc_runtime_tick(candidate, &error) == JC_SCRIPT_TICK_ERROR)
            goto failed;
    }
    if (candidate->vm.tick_count != ticks ||
        candidate->scene_finished != finished)
        goto failed;
    success = true;

failed:
    runtime_replay_silent = false;
    if (!success)
        jc_runtime_destroy(candidate);
    return success;
}

static bool restore_candidate_frame(jc_core_t *candidate_core,
                                    const jc_runtime_t *candidate_runtime,
                                    const jc_chapter_t *chapter,
                                    const char *screen_name,
                                    bool diagnostic)
{
    char error[256];

    jc_core_init(candidate_core);
    if (diagnostic)
        return true;
    if (chapter == NULL)
        return load_frame_into(candidate_core, screen_name, false,
                               error, sizeof(error));
    {
        const jc_surface_t *surface =
            jc_runtime_output(candidate_runtime);
        const jc_palette_t *palette =
            jc_runtime_palette(candidate_runtime);
        if (surface != NULL && palette != NULL)
            return jc_core_update_content_frame(candidate_core, surface,
                                                palette);
        return load_frame_into(candidate_core,
                               chapter_location_screen(chapter), false,
                               error, sizeof(error));
    }
}

static bool retro_unserialize_legacy(const uint8_t *bytes, size_t size)
{
    size_t core_size = jc_core_serialize_size();
    size_t audio_size = jc_audio_serialize_size();
    jc_core_t *candidate_core;
    jc_audio_t candidate_audio;
    bool success;

    if (size < core_size + audio_size)
        return false;
    candidate_core = (jc_core_t *)malloc(sizeof(*candidate_core));
    if (candidate_core == NULL)
        return false;
    *candidate_core = core;
    candidate_audio = audio;
    success = jc_core_unserialize(candidate_core, bytes, core_size) &&
              jc_audio_unserialize(&candidate_audio, bytes + core_size,
                                   audio_size);
    if (success) {
        core = *candidate_core;
        audio = candidate_audio;
    }
    free(candidate_core);
    return success;
}

size_t retro_serialize_size(void)
{
    return JC_LIBRETRO_STATE_HEADER_SIZE + jc_core_serialize_size() +
           jc_audio_serialize_size();
}

bool retro_serialize(void *data, size_t size)
{
    uint8_t *bytes = (uint8_t *)data;
    size_t core_size = jc_core_serialize_size();
    size_t audio_size = jc_audio_serialize_size();
    size_t total_size = retro_serialize_size();
    uint32_t flags = 0u;
    uint32_t chapter_index;
    uint32_t caption_index = JC_LIBRETRO_STATE_NO_INDEX;
    uint32_t screen_index;
    uint32_t runtime_seed = CHAPTER_RUNTIME_SEED;
    uint32_t story_plan_seed = 0u;
    uint32_t story_position = 0u;
    uint32_t story_transition = JC_STORY_STATE_PLAYING;
    uint32_t story_progress = 0u;
    uint64_t runtime_ticks = 0u;

    if (data == NULL || size < total_size || !game_loaded ||
        !runtime_ready || runtime == NULL || runtime_failed ||
        core_size > UINT32_MAX || audio_size > UINT32_MAX ||
        total_size > UINT32_MAX)
        return false;
    chapter_index = serialized_chapter_index(selected_chapter);
    screen_index = serialized_screen_index(selected_screen);
    if ((selected_chapter != NULL &&
         chapter_index == JC_LIBRETRO_STATE_NO_INDEX) ||
        screen_index == JC_LIBRETRO_STATE_NO_INDEX)
        return false;
    if (automatic_story) {
        flags |= JC_LIBRETRO_STATE_FLAG_AUTOMATIC_STORY;
        if (!diagnostic_display) {
            const jc_scene_play_t *play =
                jc_story_player_current(&story_player);
            if (play == NULL || selected_chapter == NULL ||
                story_player.director.current_day < 1u ||
                story_player.director.current_day > 11u ||
                story_player.scene_index > UINT8_MAX ||
                jc_chapter_for_ads(play->scene.ads_name,
                                   play->scene.ads_tag) != selected_chapter)
                return false;
            story_plan_seed = story_player.plan_seed;
            story_position =
                (uint32_t)story_player.director.current_day |
                ((uint32_t)story_player.scene_index << 8);
            runtime_seed = jc_story_player_runtime_seed(&story_player);
            if (fade_sequence >= JC_FADE_STYLE_COUNT)
                return false;
            if (automatic_transition == JC_AUTOMATIC_TRANSITION_WALK) {
                if (!runtime->scene_finished || !island_walk.active ||
                    automatic_transition_ticks == 0u ||
                    automatic_transition_ticks > JC_STORY_STATE_PROGRESS_MASK ||
                    story_player.scene_index + 1u >=
                        story_player.run.scene_count)
                    return false;
                story_transition = JC_STORY_STATE_WALKING;
                story_progress = automatic_transition_ticks;
            } else if (automatic_transition ==
                       JC_AUTOMATIC_TRANSITION_FADE_OUT) {
                if (!runtime->scene_finished || scene_fade.step == 0u ||
                    scene_fade.step > JC_STORY_STATE_PROGRESS_MASK ||
                    story_player.scene_index + 1u !=
                        story_player.run.scene_count)
                    return false;
                story_transition = jc_fade_is_active(&scene_fade)
                                       ? JC_STORY_STATE_FADING
                                       : JC_STORY_STATE_FADE_COMPLETE;
                story_progress = scene_fade.step;
            } else if (automatic_transition !=
                       JC_AUTOMATIC_TRANSITION_NONE) {
                return false;
            }
            story_position |=
                story_transition << JC_STORY_STATE_TRANSITION_SHIFT;
            story_position |=
                (fade_sequence & JC_STORY_STATE_FADE_SEQUENCE_MASK)
                << JC_STORY_STATE_FADE_SEQUENCE_SHIFT;
            story_position |=
                (story_progress & JC_STORY_STATE_PROGRESS_MASK)
                << JC_STORY_STATE_PROGRESS_SHIFT;
        }
    }
    if (selected_chapter != NULL) {
        flags |= JC_LIBRETRO_STATE_FLAG_CHAPTER;
        if (!diagnostic_display) {
            if (!runtime->scene_loaded)
                return false;
            flags |= JC_LIBRETRO_STATE_FLAG_RUNTIME_SCENE;
            runtime_ticks = runtime->vm.tick_count;
            if (runtime->scene_finished)
                flags |= JC_LIBRETRO_STATE_FLAG_RUNTIME_FINISHED;
        }
    }
    if (runtime_ticks > JC_LIBRETRO_STATE_MAX_RUNTIME_TICKS)
        return false;
    if (captions.enabled)
        flags |= JC_LIBRETRO_STATE_FLAG_CAPTIONS_ENABLED;
    if (captions.current != NULL && captions.remaining_ticks != 0u) {
        caption_index = serialized_caption_index(captions.current);
        if (!captions.enabled || caption_index == JC_LIBRETRO_STATE_NO_INDEX ||
            captions.remaining_ticks > JC_CAPTION_DEFAULT_TICKS)
            return false;
        flags |= JC_LIBRETRO_STATE_FLAG_CAPTION_ACTIVE;
    } else if (captions.remaining_ticks != 0u) {
        return false;
    }
    if (diagnostic_display)
        flags |= JC_LIBRETRO_STATE_FLAG_DIAGNOSTIC;
    if (reset_pressed)
        flags |= JC_LIBRETRO_STATE_FLAG_RESET_PRESSED;

    memset(bytes, 0, total_size);
    write_u32le(bytes, JC_LIBRETRO_STATE_MAGIC);
    write_u32le(bytes + 4u, JC_LIBRETRO_STATE_VERSION);
    write_u32le(bytes + 8u, JC_LIBRETRO_STATE_HEADER_SIZE);
    write_u32le(bytes + 12u, (uint32_t)total_size);
    write_u32le(bytes + 16u, (uint32_t)core_size);
    write_u32le(bytes + 20u, (uint32_t)audio_size);
    write_u32le(bytes + 24u, flags);
    write_u32le(bytes + 28u, chapter_index);
    write_u32le(bytes + 32u, caption_index);
    write_u32le(bytes + 36u, screen_index);
    write_u32le(bytes + 40u, runtime_seed);
    write_u32le(bytes + 44u, captions.remaining_ticks);
    write_u64le(bytes + 48u, runtime_ticks);
    write_u32le(bytes + 56u, story_plan_seed);
    write_u32le(bytes + 60u, story_position);
    return jc_core_serialize(&core,
                             bytes + JC_LIBRETRO_STATE_HEADER_SIZE,
                             core_size) &&
           jc_audio_serialize(&audio,
                              bytes + JC_LIBRETRO_STATE_HEADER_SIZE +
                                  core_size,
                              audio_size);
}

bool retro_unserialize(const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    size_t core_size = jc_core_serialize_size();
    size_t audio_size = jc_audio_serialize_size();
    size_t expected_size = retro_serialize_size();
    uint32_t flags;
    uint32_t chapter_index;
    uint32_t caption_index;
    uint32_t screen_index;
    uint32_t seed;
    uint32_t caption_ticks;
    uint32_t story_plan_seed;
    uint32_t story_position;
    uint32_t saved_story_transition = JC_STORY_STATE_PLAYING;
    uint32_t saved_fade_sequence = 0u;
    uint32_t saved_transition_progress = 0u;
    uint64_t runtime_ticks;
    const jc_chapter_t *candidate_chapter = NULL;
    jc_captions_t candidate_captions;
    jc_audio_t candidate_audio;
    jc_core_t *candidate_core = NULL;
    jc_runtime_t *candidate_runtime = NULL;
    jc_runtime_t *old_runtime;
    jc_story_player_t candidate_story_player;
    jc_fade_t candidate_fade;
    jc_island_walk_t candidate_island_walk;
    uint8_t *candidate_walk_pixels = NULL;
    jc_surface_t candidate_walk_frame;
    jc_automatic_transition_t candidate_transition =
        JC_AUTOMATIC_TRANSITION_NONE;
    bool candidate_walk_initialized = false;
    int runtime_offset_x = 0;
    int runtime_offset_y = 0;
    bool automatic;
    bool diagnostic;
    bool has_scene;
    bool finished;
    bool success = false;

    if (data == NULL)
        return false;
    if (size < 4u || read_u32le(bytes) != JC_LIBRETRO_STATE_MAGIC)
        return retro_unserialize_legacy(bytes, size);
    if (!game_loaded || !content.ready ||
        size < JC_LIBRETRO_STATE_HEADER_SIZE ||
        read_u32le(bytes + 4u) != JC_LIBRETRO_STATE_VERSION ||
        read_u32le(bytes + 8u) != JC_LIBRETRO_STATE_HEADER_SIZE ||
        read_u32le(bytes + 12u) != expected_size ||
        read_u32le(bytes + 16u) != core_size ||
        read_u32le(bytes + 20u) != audio_size || size != expected_size)
        return false;
    flags = read_u32le(bytes + 24u);
    chapter_index = read_u32le(bytes + 28u);
    caption_index = read_u32le(bytes + 32u);
    screen_index = read_u32le(bytes + 36u);
    seed = read_u32le(bytes + 40u);
    caption_ticks = read_u32le(bytes + 44u);
    runtime_ticks = read_u64le(bytes + 48u);
    story_plan_seed = read_u32le(bytes + 56u);
    story_position = read_u32le(bytes + 60u);
    diagnostic = (flags & JC_LIBRETRO_STATE_FLAG_DIAGNOSTIC) != 0u;
    automatic =
        (flags & JC_LIBRETRO_STATE_FLAG_AUTOMATIC_STORY) != 0u;
    has_scene = (flags & JC_LIBRETRO_STATE_FLAG_RUNTIME_SCENE) != 0u;
    finished = (flags & JC_LIBRETRO_STATE_FLAG_RUNTIME_FINISHED) != 0u;

    if ((flags & ~JC_LIBRETRO_STATE_FLAGS_ALL) != 0u ||
        screen_index >= sizeof(serialized_screen_names) /
                            sizeof(serialized_screen_names[0]) ||
        runtime_ticks > JC_LIBRETRO_STATE_MAX_RUNTIME_TICKS)
        return false;
    if ((flags & JC_LIBRETRO_STATE_FLAG_CHAPTER) != 0u) {
        if (chapter_index >= jc_chapter_count())
            return false;
        candidate_chapter = jc_chapter_at(chapter_index);
    } else if (chapter_index != JC_LIBRETRO_STATE_NO_INDEX) {
        return false;
    }
    memset(&candidate_story_player, 0, sizeof(candidate_story_player));
    jc_fade_init(&candidate_fade);
    memset(&candidate_island_walk, 0, sizeof(candidate_island_walk));
    memset(&candidate_walk_frame, 0, sizeof(candidate_walk_frame));
    if (automatic && !diagnostic) {
        int yday;
        uint8_t hour;
        uint8_t month;
        uint8_t month_day;
        uint8_t story_day = (uint8_t)(story_position & 0xffu);
        size_t story_scene_index = (size_t)((story_position >> 8) & 0xffu);
        const jc_scene_play_t *play;

        saved_story_transition =
            (story_position >> JC_STORY_STATE_TRANSITION_SHIFT) &
            JC_STORY_STATE_TRANSITION_MASK;
        saved_fade_sequence =
            (story_position >> JC_STORY_STATE_FADE_SEQUENCE_SHIFT) &
            JC_STORY_STATE_FADE_SEQUENCE_MASK;
        saved_transition_progress =
            (story_position >> JC_STORY_STATE_PROGRESS_SHIFT) &
            JC_STORY_STATE_PROGRESS_MASK;
        if (candidate_chapter == NULL || story_day < 1u || story_day > 11u ||
            saved_fade_sequence >= JC_FADE_STYLE_COUNT)
            return false;
        current_story_calendar(&yday, &hour, &month, &month_day);
        if (!jc_story_player_restore(
                &candidate_story_player, story_plan_seed, story_day,
                story_scene_index, yday, hour, month, month_day))
            return false;
        play = jc_story_player_current(&candidate_story_player);
        if (play == NULL ||
            jc_chapter_for_ads(play->scene.ads_name,
                               play->scene.ads_tag) != candidate_chapter ||
            jc_story_player_runtime_seed(&candidate_story_player) != seed)
            return false;
        if ((play->scene.flags & JC_SCENE_ISLAND) != 0u) {
            runtime_offset_x = candidate_story_player.run.island.x +
                               (play->left_island ? 272 : 0);
            runtime_offset_y = candidate_story_player.run.island.y;
        }
        if (saved_story_transition == JC_STORY_STATE_PLAYING) {
            if (saved_transition_progress != 0u)
                return false;
        } else if (saved_story_transition == JC_STORY_STATE_WALKING) {
            const jc_scene_play_t *next;
            if (!finished || !walk_resources_ready ||
                saved_transition_progress == 0u ||
                story_scene_index + 1u >=
                    candidate_story_player.run.scene_count)
                return false;
            next = &candidate_story_player.run.scenes[story_scene_index + 1u];
            if (!next->has_walk_from)
                return false;
            candidate_transition = JC_AUTOMATIC_TRANSITION_WALK;
        } else if (saved_story_transition == JC_STORY_STATE_FADING ||
                   saved_story_transition == JC_STORY_STATE_FADE_COMPLETE) {
            jc_fade_style_t style =
                jc_fade_style_for_sequence(saved_fade_sequence);
            if (!finished || story_scene_index + 1u !=
                                 candidate_story_player.run.scene_count ||
                saved_transition_progress == 0u ||
                saved_transition_progress > jc_fade_default_steps(style) ||
                !jc_fade_begin(&candidate_fade, JC_FADE_OUT, style))
                return false;
            candidate_fade.step = saved_transition_progress;
            candidate_fade.active =
                saved_story_transition == JC_STORY_STATE_FADING;
            if (!candidate_fade.active &&
                candidate_fade.step != candidate_fade.step_count)
                return false;
            candidate_transition = JC_AUTOMATIC_TRANSITION_FADE_OUT;
        } else {
            return false;
        }
    } else if (story_plan_seed != 0u || story_position != 0u ||
               seed != CHAPTER_RUNTIME_SEED) {
        return false;
    }
    if (candidate_chapter == NULL || diagnostic) {
        if (has_scene || finished || runtime_ticks != 0u)
            return false;
    } else if (!has_scene) {
        return false;
    }

    jc_captions_init(&candidate_captions);
    candidate_captions.enabled =
        (flags & JC_LIBRETRO_STATE_FLAG_CAPTIONS_ENABLED) != 0u;
    if ((flags & JC_LIBRETRO_STATE_FLAG_CAPTION_ACTIVE) != 0u) {
        if (!candidate_captions.enabled ||
            caption_index >= jc_caption_count() || caption_ticks == 0u ||
            caption_ticks > JC_CAPTION_DEFAULT_TICKS)
            return false;
        candidate_captions.current = jc_caption_at(caption_index);
        candidate_captions.remaining_ticks = caption_ticks;
    } else if (caption_index != JC_LIBRETRO_STATE_NO_INDEX ||
               caption_ticks != 0u) {
        return false;
    }

    candidate_core = (jc_core_t *)malloc(sizeof(*candidate_core));
    candidate_runtime = (jc_runtime_t *)malloc(sizeof(*candidate_runtime));
    if (candidate_core == NULL || candidate_runtime == NULL)
        goto cleanup;
    jc_core_init(candidate_core);
    candidate_audio = audio;
    if (!jc_core_unserialize(candidate_core,
                             bytes + JC_LIBRETRO_STATE_HEADER_SIZE,
                             core_size) ||
        !jc_audio_unserialize(&candidate_audio,
                              bytes + JC_LIBRETRO_STATE_HEADER_SIZE +
                                  core_size,
                              audio_size) ||
        !restore_runtime_candidate(candidate_runtime, candidate_chapter,
                                   diagnostic, has_scene, finished,
                                   runtime_ticks, seed, runtime_offset_x,
                                   runtime_offset_y) ||
        !restore_candidate_frame(candidate_core, candidate_runtime,
                                 candidate_chapter,
                                 serialized_screen_names[screen_index],
                                 diagnostic) ||
        !jc_core_unserialize(candidate_core,
                             bytes + JC_LIBRETRO_STATE_HEADER_SIZE,
                             core_size))
        goto cleanup;

    if (candidate_transition == JC_AUTOMATIC_TRANSITION_WALK) {
        const jc_scene_play_t *next =
            &candidate_story_player.run.scenes[
                candidate_story_player.scene_index + 1u];
        jc_rng_t walk_rng;
        uint32_t tick;

        candidate_walk_pixels =
            (uint8_t *)malloc((size_t)JC_FRAME_WIDTH * JC_FRAME_HEIGHT);
        if (candidate_walk_pixels == NULL ||
            !jc_surface_init(&candidate_walk_frame, candidate_walk_pixels,
                             (size_t)JC_FRAME_WIDTH * JC_FRAME_HEIGHT,
                             JC_FRAME_WIDTH, JC_FRAME_HEIGHT,
                             JC_FRAME_WIDTH) ||
            !jc_island_walk_init(&candidate_island_walk, JC_FRAME_WIDTH,
                                 JC_FRAME_HEIGHT, 5))
            goto cleanup;
        candidate_walk_initialized = true;
        jc_rng_init(&walk_rng,
                    jc_story_player_runtime_seed(&candidate_story_player) ^
                        WALK_RUNTIME_SEED_XOR ^
                        (uint32_t)(candidate_story_player.scene_index + 1u));
        if (!jc_island_walk_bind_sprites(
                &candidate_island_walk, &johnny_walk_sprites,
                island_sprites.image_count != 0u ? &island_sprites : NULL) ||
            !jc_island_walk_capture(&candidate_island_walk,
                                    &candidate_runtime->renderer.background) ||
            !jc_island_walk_begin(
                &candidate_island_walk, next->walk_from_spot,
                next->walk_from_heading, next->scene.spot_start,
                next->scene.heading_start,
                candidate_story_player.run.island.x,
                candidate_story_player.run.island.y, &walk_rng))
            goto cleanup;
        for (tick = 0u; tick < saved_transition_progress; ++tick) {
            jc_island_walk_result_t walk_result =
                jc_island_walk_tick(&candidate_island_walk,
                                    &candidate_walk_frame);
            if (walk_result != JC_ISLAND_WALK_ACTIVE)
                goto cleanup;
        }
        if (!walk_palette_ready ||
            !jc_core_update_content_frame(candidate_core,
                                          &candidate_walk_frame,
                                          &walk_palette) ||
            !jc_core_unserialize(candidate_core,
                                 bytes + JC_LIBRETRO_STATE_HEADER_SIZE,
                                 core_size))
            goto cleanup;
    }

    old_runtime = runtime;
    runtime = candidate_runtime;
    candidate_runtime = NULL;
    runtime_ready = true;
    runtime_failed = false;
    core = *candidate_core;
    audio = candidate_audio;
    captions = candidate_captions;
    selected_chapter = candidate_chapter;
    automatic_story = automatic;
    story_player = candidate_story_player;
    reset_automatic_transition();
    fade_sequence = saved_fade_sequence;
    automatic_transition = candidate_transition;
    automatic_transition_ticks = saved_transition_progress;
    scene_fade = candidate_fade;
    if (candidate_transition == JC_AUTOMATIC_TRANSITION_WALK) {
        jc_island_walk_destroy(&island_walk);
        island_walk = candidate_island_walk;
        memset(&candidate_island_walk, 0, sizeof(candidate_island_walk));
        candidate_walk_initialized = false;
        free(walk_frame_pixels);
        walk_frame_pixels = candidate_walk_pixels;
        candidate_walk_pixels = NULL;
        walk_frame = candidate_walk_frame;
    }
    snprintf(selected_screen, sizeof(selected_screen), "%s",
             serialized_screen_names[screen_index]);
    diagnostic_display = diagnostic;
    reset_pressed =
        (flags & JC_LIBRETRO_STATE_FLAG_RESET_PRESSED) != 0u;
    if (old_runtime != NULL) {
        jc_runtime_destroy(old_runtime);
        free(old_runtime);
    }
    success = true;

cleanup:
    runtime_replay_silent = false;
    if (candidate_walk_initialized)
        jc_island_walk_destroy(&candidate_island_walk);
    free(candidate_walk_pixels);
    if (candidate_runtime != NULL) {
        if (candidate_runtime->initialized)
            jc_runtime_destroy(candidate_runtime);
        free(candidate_runtime);
    }
    free(candidate_core);
    return success;
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
    jc_sfx_report_t sfx_report;
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

    if (!load_walk_resources(error, sizeof(error))) {
        if (log_cb != NULL)
            log_cb(RETRO_LOG_WARN,
                   "Johnny Castaway optional island walking: %s\n", error);
    } else if (log_cb != NULL) {
        log_cb(RETRO_LOG_INFO,
               "Johnny Castaway island walking: JOHNWALK and BACKGRND ready\n");
    }

    if (jc_sfx_load(&sfx, &audio, game->path, vfs, &sfx_report) != JC_SFX_OK) {
        if (log_cb != NULL)
            log_cb(RETRO_LOG_WARN,
                   "Johnny Castaway optional sound effects: %s\n",
                   jc_sfx_status_string(sfx_report.status));
    } else if (log_cb != NULL) {
        log_cb(RETRO_LOG_INFO,
               "Johnny Castaway optional sound effects: %u loaded, %u missing, %u invalid\n",
               sfx_report.loaded_count, sfx_report.missing_count,
               sfx_report.invalid_count);
    }

    jc_core_reset(&core);
    if (!initialize_story_runtime(error, sizeof(error)) ||
        !start_selected_presentation(error, sizeof(error))) {
        if (log_cb != NULL)
            log_cb(RETRO_LOG_ERROR, "Johnny Castaway: %s\n", error);
        if (runtime != NULL) {
            jc_runtime_destroy(runtime);
            free(runtime);
            runtime = NULL;
        }
        runtime_ready = false;
        unload_walk_resources();
        jc_sfx_unload(&sfx, &audio);
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
    if (runtime != NULL) {
        jc_runtime_destroy(runtime);
        free(runtime);
        runtime = NULL;
    }
    runtime_ready = false;
    unload_walk_resources();
    unload_ocean_ambience();
    jc_sfx_unload(&sfx, &audio);
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
