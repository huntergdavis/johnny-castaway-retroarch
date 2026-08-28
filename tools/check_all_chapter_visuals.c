/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Authentic-data visual acceptance tool. It never embeds original content. */
#include "jc_chapters.h"
#include "jc_director.h"
#include "libretro.h"

#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAME_WIDTH 640u
#define FRAME_HEIGHT 480u
#define FRAME_PIXELS ((size_t)FRAME_WIDTH * FRAME_HEIGHT)
#define MAX_PRESENTED_FRAMES 6000u
#define MAX_RUNTIME_TICKS 20000u
#define KEY_RUN_LIMIT 8u
#define KEY_COMPONENT_LIMIT 64u
#define MIN_MEANINGFUL_PIXELS 1024u
#define STATE_FLAG_RUNTIME_FINISHED (1u << 2)

typedef enum scene_class {
    SCENE_CLASS_ISLAND = 0,
    SCENE_CLASS_ENDING,
    SCENE_CLASS_OFFICE,
    SCENE_CLASS_SUZY
} scene_class_t;

typedef struct frame_metrics {
    uint64_t runtime_tick;
    uint64_t hash;
    size_t meaningful;
    size_t black;
    size_t blue_cyan;
    size_t dark_blue;
    size_t gray;
    size_t red;
    size_t key_pixels;
    size_t key_run;
    size_t key_component;
} frame_metrics_t;

typedef struct row_result {
    const jc_chapter_t *chapter;
    const char *tide;
    scene_class_t scene_class;
    bool island;
    uint64_t runtime_ticks;
    size_t presented_frames;
    size_t unique_hashes;
    frame_metrics_t start;
    frame_metrics_t middle;
    frame_metrics_t final;
    size_t max_meaningful;
    size_t max_key_pixels;
    size_t max_key_run;
    size_t max_key_component;
} row_result_t;

static const char *chapter_value = "screen";
static const char *initial_screen_value = "intro";
static const char *tide_value = "high";
static const char *playback_speed_value = "1";
static bool variable_updated;
static unsigned runtime_error_logs;
static bool capture_active;
static bool capture_overflow;
static frame_metrics_t captured[MAX_PRESENTED_FRAMES];
static size_t captured_count;
static uint8_t key_mask[FRAME_PIXELS];
static uint32_t key_queue[FRAME_PIXELS];

bool jc_test_runtime_status(uint64_t *ticks, bool *finished, bool *failed);

static uint64_t fnv1a_pixel(uint64_t hash, uint32_t pixel)
{
    hash ^= pixel;
    return hash * UINT64_C(1099511628211);
}

static size_t largest_key_component(size_t key_pixels)
{
    size_t largest = 0u;
    size_t index;

    if (key_pixels < KEY_COMPONENT_LIMIT)
        return key_pixels;
    for (index = 0u; index < FRAME_PIXELS; ++index) {
        size_t head = 0u;
        size_t tail = 0u;

        if (key_mask[index] == 0u)
            continue;
        key_mask[index] = 0u;
        key_queue[tail++] = (uint32_t)index;
        while (head < tail) {
            size_t current = key_queue[head++];
            size_t x = current % FRAME_WIDTH;
            size_t neighbor;

#define VISIT_KEY_NEIGHBOR(value_)                                      \
            do {                                                        \
                neighbor = (value_);                                    \
                if (key_mask[neighbor] != 0u) {                         \
                    key_mask[neighbor] = 0u;                            \
                    key_queue[tail++] = (uint32_t)neighbor;             \
                }                                                       \
            } while (0)
            if (x > 0u)
                VISIT_KEY_NEIGHBOR(current - 1u);
            if (x + 1u < FRAME_WIDTH)
                VISIT_KEY_NEIGHBOR(current + 1u);
            if (current >= FRAME_WIDTH)
                VISIT_KEY_NEIGHBOR(current - FRAME_WIDTH);
            if (current + FRAME_WIDTH < FRAME_PIXELS)
                VISIT_KEY_NEIGHBOR(current + FRAME_WIDTH);
#undef VISIT_KEY_NEIGHBOR
        }
        if (tail > largest)
            largest = tail;
        if (largest >= KEY_COMPONENT_LIMIT)
            return largest;
    }
    return largest;
}

static frame_metrics_t analyze_frame(const uint32_t *pixels)
{
    frame_metrics_t metrics;
    size_t run = 0u;
    size_t index;

    memset(&metrics, 0, sizeof(metrics));
    metrics.hash = UINT64_C(1469598103934665603);
    for (index = 0u; index < FRAME_PIXELS; ++index) {
        uint32_t pixel = pixels[index] & UINT32_C(0x00ffffff);
        bool is_key = pixel == UINT32_C(0x00a800a8);

        metrics.hash = fnv1a_pixel(metrics.hash, pixel);
        key_mask[index] = is_key ? 1u : 0u;
        if (pixel == 0u)
            ++metrics.black;
        if (pixel != 0u && !is_key)
            ++metrics.meaningful;
        if (pixel == UINT32_C(0x000000fc) ||
            pixel == UINT32_C(0x0000fcfc))
            ++metrics.blue_cyan;
        if (pixel == UINT32_C(0x000000a8))
            ++metrics.dark_blue;
        if (pixel == UINT32_C(0x00808080) ||
            pixel == UINT32_C(0x00d4d4d4))
            ++metrics.gray;
        if (pixel == UINT32_C(0x00fc0000))
            ++metrics.red;
        if (is_key) {
            ++metrics.key_pixels;
            ++run;
            if (run > metrics.key_run)
                metrics.key_run = run;
        } else {
            run = 0u;
        }
        if ((index + 1u) % FRAME_WIDTH == 0u)
            run = 0u;
    }
    metrics.key_component = largest_key_component(metrics.key_pixels);
    return metrics;
}

static void RETRO_CALLCONV video(const void *data, unsigned width,
                                 unsigned height, size_t pitch)
{
    frame_metrics_t metrics;

    if (data == NULL || width != FRAME_WIDTH || height != FRAME_HEIGHT ||
        pitch != FRAME_WIDTH * sizeof(uint32_t)) {
        fprintf(stderr, "invalid video frame geometry\n");
        capture_overflow = true;
        return;
    }
    metrics = analyze_frame((const uint32_t *)data);
    if (!capture_active)
        return;
    {
        bool finished;
        bool failed;
        (void)jc_test_runtime_status(&metrics.runtime_tick, &finished,
                                     &failed);
    }
    if (captured_count >= MAX_PRESENTED_FRAMES) {
        capture_overflow = true;
        return;
    }
    captured[captured_count++] = metrics;
}

static void RETRO_CALLCONV log_message(enum retro_log_level level,
                                       const char *format, ...)
{
    va_list arguments;
    char message[1024];

    va_start(arguments, format);
    (void)vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    if (level == RETRO_LOG_ERROR &&
        (strstr(message, "Johnny Castaway runtime:") != NULL ||
         strstr(message, "Johnny Castaway option update:") != NULL ||
         strstr(message, "Johnny Castaway island presentation:") != NULL))
        ++runtime_error_logs;
}

static const char *option_value(const char *key)
{
    if (strcmp(key, "johnny_castaway_chapter") == 0)
        return chapter_value;
    if (strcmp(key, "johnny_castaway_tide") == 0)
        return tide_value;
    if (strcmp(key, "johnny_castaway_playback_speed") == 0)
        return playback_speed_value;
    if (strcmp(key, "johnny_castaway_initial_screen") == 0)
        return initial_screen_value;
    if (strcmp(key, "johnny_castaway_story_seed") == 0)
        return "24";
    if (strcmp(key, "johnny_castaway_story_calendar") == 0)
        return "system";
    if (strcmp(key, "johnny_castaway_simulated_month") == 0 ||
        strcmp(key, "johnny_castaway_simulated_day") == 0)
        return "1";
    if (strcmp(key, "johnny_castaway_simulated_hour") == 0)
        return "12";
    if (strcmp(key, "johnny_castaway_raft_stage") == 0)
        return "auto";
    if (strcmp(key, "johnny_castaway_holiday_overlay") == 0)
        return "off";
    if (strcmp(key, "johnny_castaway_display_source") == 0)
        return "original";
    if (strcmp(key, "johnny_castaway_audio_enabled") == 0 ||
        strcmp(key, "johnny_castaway_ocean_enabled") == 0)
        return "disabled";
    if (strcmp(key, "johnny_castaway_audio_volume") == 0)
        return "100";
    if (strcmp(key, "johnny_castaway_ocean_volume") == 0)
        return "56";
    if (strcmp(key, "johnny_castaway_captions_enabled") == 0)
        return "disabled";
    if (strcmp(key, "johnny_castaway_caption_size") == 0)
        return "medium";
    if (strcmp(key, "johnny_castaway_caption_background") == 0)
        return "bar";
    if (strcmp(key, "johnny_castaway_caption_opacity") == 0)
        return "63";
    if (strcmp(key, "johnny_castaway_caption_position") == 0)
        return "bottom";
    return NULL;
}

static bool RETRO_CALLCONV environment(unsigned command, void *data)
{
    switch (command) {
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        *(unsigned *)data = 2u;
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
        return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        ((struct retro_log_callback *)data)->log = log_message;
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *variable = (struct retro_variable *)data;
        variable->value = option_value(variable->key);
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
    default:
        return false;
    }
}

static void RETRO_CALLCONV audio_sample(int16_t left, int16_t right)
{
    (void)left;
    (void)right;
}

static size_t RETRO_CALLCONV audio_batch(const int16_t *data, size_t frames)
{
    (void)data;
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

static int compare_u64(const void *left, const void *right)
{
    uint64_t a = *(const uint64_t *)left;
    uint64_t b = *(const uint64_t *)right;
    return a < b ? -1 : a > b ? 1 : 0;
}

static size_t unique_frame_hashes(void)
{
    uint64_t *hashes;
    size_t unique = 0u;
    size_t index;

    hashes = (uint64_t *)malloc(captured_count * sizeof(*hashes));
    if (hashes == NULL)
        return 0u;
    for (index = 0u; index < captured_count; ++index)
        hashes[index] = captured[index].hash;
    qsort(hashes, captured_count, sizeof(*hashes), compare_u64);
    for (index = 0u; index < captured_count; ++index) {
        if (index == 0u || hashes[index] != hashes[index - 1u])
            ++unique;
    }
    free(hashes);
    return unique;
}

static bool chapter_scene(const jc_chapter_t *chapter,
                          jc_story_scene_t *scene)
{
    size_t index;
    char ads_name[64];
    int written = snprintf(ads_name, sizeof(ads_name), "%s.ADS",
                           chapter->ads_name);

    if (written < 0 || (size_t)written >= sizeof(ads_name))
        return false;
    for (index = 0u; index < jc_director_scene_count(); ++index) {
        if (jc_director_scene(index, scene) &&
            strcmp(scene->ads_name, ads_name) == 0 &&
            scene->ads_tag == chapter->ads_tag)
            return true;
    }
    return false;
}

static scene_class_t classify_scene(const jc_chapter_t *chapter,
                                    bool island)
{
    if (island)
        return SCENE_CLASS_ISLAND;
    if (strcmp(chapter->slug, "johnny1") == 0)
        return SCENE_CLASS_ENDING;
    if (strcmp(chapter->slug, "johnny6") == 0)
        return SCENE_CLASS_OFFICE;
    return SCENE_CLASS_SUZY;
}

static const char *class_name(scene_class_t scene_class)
{
    switch (scene_class) {
    case SCENE_CLASS_ISLAND: return "island";
    case SCENE_CLASS_ENDING: return "ending";
    case SCENE_CLASS_OFFICE: return "office";
    case SCENE_CLASS_SUZY: return "suzy_beach";
    default: return "unknown";
    }
}

static bool validate_row(row_result_t *result)
{
    size_t index;
    size_t max_blue_cyan = 0u;
    size_t max_gray = 0u;
    size_t max_red = 0u;
    size_t max_black = 0u;
    size_t max_key_run_frame = 0u;
    size_t max_key_component_frame = 0u;

    if (capture_overflow || captured_count == 0u) {
        fprintf(stderr, "%s/%s: frame capture %s\n",
                result->chapter->slug, result->tide,
                capture_overflow ? "overflowed" : "is empty");
        return false;
    }
    result->presented_frames = captured_count;
    result->unique_hashes = unique_frame_hashes();
    result->start = captured[0];
    result->middle = captured[captured_count / 2u];
    result->final = captured[captured_count - 1u];
    for (index = 0u; index < captured_count; ++index) {
        const frame_metrics_t *frame = &captured[index];

        if (frame->meaningful > result->max_meaningful)
            result->max_meaningful = frame->meaningful;
        if (frame->key_pixels > result->max_key_pixels)
            result->max_key_pixels = frame->key_pixels;
        if (frame->key_run > result->max_key_run) {
            result->max_key_run = frame->key_run;
            max_key_run_frame = index;
        }
        if (frame->key_component > result->max_key_component) {
            result->max_key_component = frame->key_component;
            max_key_component_frame = index;
        }
        if (frame->blue_cyan > max_blue_cyan)
            max_blue_cyan = frame->blue_cyan;
        if (frame->gray > max_gray)
            max_gray = frame->gray;
        if (frame->red > max_red)
            max_red = frame->red;
        if (frame->black > max_black)
            max_black = frame->black;
    }
    if (result->runtime_ticks > MAX_RUNTIME_TICKS ||
        result->max_meaningful < MIN_MEANINGFUL_PIXELS ||
        result->unique_hashes < 2u ||
        result->max_key_run >= KEY_RUN_LIMIT ||
        result->max_key_component >= KEY_COMPONENT_LIMIT) {
        fprintf(stderr,
                "%s/%s: ticks=%llu frames=%zu unique=%zu meaningful=%zu "
                "key-pixels=%zu key-run=%zu key-component=%zu\n",
                result->chapter->slug, result->tide,
                (unsigned long long)result->runtime_ticks,
                result->presented_frames, result->unique_hashes,
                result->max_meaningful, result->max_key_pixels,
                result->max_key_run, result->max_key_component);
        fprintf(stderr,
                "%s/%s: max-run frame=%zu tick=%llu hash=%016llx; "
                "max-component frame=%zu tick=%llu hash=%016llx\n",
                result->chapter->slug, result->tide, max_key_run_frame,
                (unsigned long long)captured[max_key_run_frame].runtime_tick,
                (unsigned long long)captured[max_key_run_frame].hash,
                max_key_component_frame,
                (unsigned long long)
                    captured[max_key_component_frame].runtime_tick,
                (unsigned long long)captured[max_key_component_frame].hash);
        return false;
    }
    if ((result->scene_class == SCENE_CLASS_ISLAND &&
         max_blue_cyan <= FRAME_PIXELS / 2u) ||
        (result->scene_class == SCENE_CLASS_ENDING &&
         (max_black <= FRAME_PIXELS / 2u ||
          max_red <= FRAME_PIXELS / 20u)) ||
        (result->scene_class == SCENE_CLASS_OFFICE &&
         (max_black <= FRAME_PIXELS / 2u ||
          max_gray <= FRAME_PIXELS / 100u)) ||
        (result->scene_class == SCENE_CLASS_SUZY &&
         (max_black <= FRAME_PIXELS / 2u ||
          max_blue_cyan <= FRAME_PIXELS / 20u))) {
        fprintf(stderr,
                "%s/%s: %s signature failed "
                "max-black=%zu max-blue-cyan=%zu max-gray=%zu max-red=%zu\n",
                result->chapter->slug, result->tide,
                class_name(result->scene_class), max_black, max_blue_cyan,
                max_gray, max_red);
        return false;
    }
    return true;
}

static bool run_row(const jc_chapter_t *chapter, const char *tide,
                    row_result_t *result)
{
    jc_story_scene_t scene;
    bool finished = false;
    bool failed = false;
    unsigned errors_before;

    memset(result, 0, sizeof(*result));
    if (!chapter_scene(chapter, &scene))
        return false;
    result->chapter = chapter;
    result->tide = tide;
    result->island = (scene.flags & JC_SCENE_ISLAND) != 0u;
    result->scene_class = classify_scene(chapter, result->island);

    chapter_value = "screen";
    playback_speed_value = "1";
    variable_updated = true;
    capture_active = false;
    retro_run();

    chapter_value = chapter->slug;
    tide_value = tide;
    captured_count = 0u;
    capture_overflow = false;
    errors_before = runtime_error_logs;
    capture_active = true;
    variable_updated = true;
    retro_run();
    if (!jc_test_runtime_status(&result->runtime_ticks, &finished, &failed))
        return false;
    playback_speed_value = "4";
    variable_updated = true;
    while (!finished && !failed && !capture_overflow &&
           captured_count < MAX_PRESENTED_FRAMES) {
        retro_run();
        if (!jc_test_runtime_status(&result->runtime_ticks, &finished,
                                    &failed))
            return false;
    }
    capture_active = false;
    if (!finished || failed || runtime_error_logs != errors_before) {
        fprintf(stderr,
                "%s/%s: completion failed finished=%u runtime-failed=%u "
                "logs=%u ticks=%llu frames=%zu\n",
                chapter->slug, tide, finished ? 1u : 0u, failed ? 1u : 0u,
                runtime_error_logs - errors_before,
                (unsigned long long)result->runtime_ticks, captured_count);
        return false;
    }
    return validate_row(result);
}

static bool variants_match_expectation(const row_result_t *high,
                                       const row_result_t *low)
{
    bool signatures_equal =
        high->start.hash == low->start.hash &&
        high->middle.hash == low->middle.hash &&
        high->final.hash == low->final.hash;

    return high->island ? !signatures_equal : signatures_equal;
}

static bool validate_initial_screen(const char *value,
                                    const frame_metrics_t *frame)
{
    bool signature = false;

    if (frame->meaningful < MIN_MEANINGFUL_PIXELS ||
        frame->key_run >= KEY_RUN_LIMIT ||
        frame->key_component >= KEY_COMPONENT_LIMIT)
        return false;
    if (strcmp(value, "intro") == 0)
        signature = frame->black > FRAME_PIXELS / 2u &&
                    frame->blue_cyan > FRAME_PIXELS / 20u;
    else if (strcmp(value, "island_day") == 0)
        signature = frame->blue_cyan > FRAME_PIXELS / 2u;
    else if (strcmp(value, "island_night") == 0)
        signature = frame->dark_blue > FRAME_PIXELS / 2u &&
                    frame->blue_cyan < FRAME_PIXELS / 20u;
    else if (strcmp(value, "office") == 0)
        signature = frame->black > FRAME_PIXELS / 2u &&
                    frame->gray > FRAME_PIXELS / 100u;
    else if (strcmp(value, "suzy_beach") == 0)
        signature = frame->black > FRAME_PIXELS / 2u &&
                    frame->blue_cyan > FRAME_PIXELS / 20u;
    else if (strcmp(value, "ending") == 0)
        signature = frame->black > FRAME_PIXELS / 2u &&
                    frame->red > FRAME_PIXELS / 20u;
    if (!signature) {
        fprintf(stderr,
                "screen/%s: signature failed black=%zu blue-cyan=%zu "
                "dark-blue=%zu gray=%zu red=%zu key=%zu/%zu\n",
                value, frame->black, frame->blue_cyan, frame->dark_blue,
                frame->gray, frame->red, frame->key_run,
                frame->key_component);
    }
    return signature;
}

static bool run_initial_screen(const char *value, frame_metrics_t *frame)
{
    chapter_value = "screen";
    initial_screen_value = value;
    playback_speed_value = "1";
    captured_count = 0u;
    capture_overflow = false;
    capture_active = true;
    variable_updated = true;
    retro_run();
    capture_active = false;
    if (capture_overflow || captured_count != 1u)
        return false;
    *frame = captured[0];
    return validate_initial_screen(value, frame);
}

static void write_csv_header(FILE *output)
{
    fputs("record_type,slug,tide,scene_class,island,presented_frames,runtime_ticks,"
          "unique_hashes,start_hash,mid_hash,final_hash,max_meaningful,"
          "max_key_pixels,max_key_run,max_key_component\n", output);
}

static void write_csv_row(FILE *output, const row_result_t *result)
{
    fprintf(output,
            "scene_variant,%s,%s,%s,%u,%zu,%llu,%zu,%016llx,%016llx,%016llx,%zu,%zu,%zu,%zu\n",
            result->chapter->slug, result->tide,
            class_name(result->scene_class), result->island ? 1u : 0u,
            result->presented_frames,
            (unsigned long long)result->runtime_ticks,
            result->unique_hashes,
            (unsigned long long)result->start.hash,
            (unsigned long long)result->middle.hash,
            (unsigned long long)result->final.hash,
            result->max_meaningful, result->max_key_pixels,
            result->max_key_run, result->max_key_component);
}

static void write_csv_screen(FILE *output, const char *value,
                             const frame_metrics_t *frame)
{
    fprintf(output,
            "initial_screen,%s,n/a,static_screen,0,1,0,1,%016llx,%016llx,%016llx,%zu,%zu,%zu,%zu\n",
            value, (unsigned long long)frame->hash,
            (unsigned long long)frame->hash,
            (unsigned long long)frame->hash, frame->meaningful,
            frame->key_pixels, frame->key_run, frame->key_component);
}

int main(int argc, char **argv)
{
    struct retro_game_info game;
    FILE *csv_output = NULL;
    const char *chapter_filter = NULL;
    size_t chapter_index;
    unsigned passed_rows = 0u;
    static const char *const initial_screens[] = {
        "intro", "island_day", "island_night", "office", "suzy_beach",
        "ending"
    };
    frame_metrics_t screen_results[
        sizeof(initial_screens) / sizeof(initial_screens[0])];

    int argument_index;

    if (argc < 2) {
        fprintf(stderr, "usage: %s /path/to/RESOURCE.MAP "
                        "[--chapter slug] [--csv /path/to/results.csv]\n",
                argv[0]);
        return 2;
    }
    for (argument_index = 2; argument_index < argc; ++argument_index) {
        const char *option = argv[argument_index];
        const char *value;

        if (argument_index + 1 >= argc) {
            fprintf(stderr, "missing value for option: %s\n", option);
            return 2;
        }
        value = argv[++argument_index];
        if (strcmp(option, "--chapter") == 0)
            chapter_filter = value;
        else if (strcmp(option, "--csv") == 0) {
            csv_output = fopen(value, "wb");
            if (csv_output == NULL) {
                fprintf(stderr, "could not open CSV: %s\n", value);
                return 1;
            }
            write_csv_header(csv_output);
        } else {
            fprintf(stderr, "unknown option: %s\n", option);
            if (csv_output != NULL)
                fclose(csv_output);
            return 2;
        }
    }
    if (chapter_filter != NULL &&
        jc_chapter_lookup(chapter_filter) == NULL) {
        fprintf(stderr, "unknown chapter: %s\n", chapter_filter);
        if (csv_output != NULL)
            fclose(csv_output);
        return 2;
    }
    if (csv_output != NULL && ferror(csv_output)) {
        fputs("could not write CSV header\n", stderr);
        fclose(csv_output);
        return 1;
    }
    retro_set_environment(environment);
    retro_set_video_refresh(video);
    retro_set_audio_sample(audio_sample);
    retro_set_audio_sample_batch(audio_batch);
    retro_set_input_poll(input_poll);
    retro_set_input_state(input_state);
    retro_init();
    memset(&game, 0, sizeof(game));
    game.path = argv[1];
    if (!retro_load_game(&game)) {
        fputs("could not load authentic content\n", stderr);
        retro_deinit();
        if (csv_output != NULL)
            fclose(csv_output);
        return 1;
    }
    for (chapter_index = 0u; chapter_index < jc_chapter_count();
         ++chapter_index) {
        const jc_chapter_t *chapter = jc_chapter_at(chapter_index);
        row_result_t high;
        row_result_t low;

        if (chapter_filter != NULL &&
            strcmp(chapter->slug, chapter_filter) != 0)
            continue;
        if (!run_row(chapter, "high", &high)) {
            fprintf(stderr, "FAIL %s/high: visual acceptance failed\n",
                    chapter->slug);
            goto failed;
        }
        ++passed_rows;
        if (csv_output != NULL)
            write_csv_row(csv_output, &high);
        printf("PASS %03u/126 %-12s high ticks=%llu frames=%zu unique=%zu "
               "key=%zu/%zu\n", passed_rows, chapter->slug,
               (unsigned long long)high.runtime_ticks,
               high.presented_frames, high.unique_hashes,
               high.max_key_run, high.max_key_component);
        if (!run_row(chapter, "low", &low)) {
            fprintf(stderr, "FAIL %s/low: visual acceptance failed\n",
                    chapter->slug);
            goto failed;
        }
        ++passed_rows;
        if (!variants_match_expectation(&high, &low)) {
            fprintf(stderr,
                    "FAIL %s variants: %s signatures unexpectedly %s\n",
                    chapter->slug, high.island ? "island" : "non-island",
                    high.island ? "match" : "differ");
            goto failed;
        }
        if (csv_output != NULL)
            write_csv_row(csv_output, &low);
        printf("PASS %03u/126 %-12s low  ticks=%llu frames=%zu unique=%zu "
               "key=%zu/%zu\n", passed_rows, chapter->slug,
               (unsigned long long)low.runtime_ticks,
               low.presented_frames, low.unique_hashes,
               low.max_key_run, low.max_key_component);
    }
    for (chapter_index = 0u; chapter_filter == NULL &&
         chapter_index < sizeof(initial_screens) / sizeof(initial_screens[0]);
         ++chapter_index) {
        size_t previous;

        if (!run_initial_screen(initial_screens[chapter_index],
                                &screen_results[chapter_index])) {
            fprintf(stderr, "FAIL screen/%s: visual acceptance failed\n",
                    initial_screens[chapter_index]);
            goto failed;
        }
        for (previous = 0u; previous < chapter_index; ++previous) {
            if (screen_results[previous].hash ==
                screen_results[chapter_index].hash) {
                fprintf(stderr, "FAIL screens %s and %s have same hash\n",
                        initial_screens[previous],
                        initial_screens[chapter_index]);
                goto failed;
            }
        }
        if (csv_output != NULL)
            write_csv_screen(csv_output, initial_screens[chapter_index],
                             &screen_results[chapter_index]);
        printf("PASS SCREEN %-12s hash=%016llx key=%zu/%zu\n",
               initial_screens[chapter_index],
               (unsigned long long)screen_results[chapter_index].hash,
               screen_results[chapter_index].key_run,
               screen_results[chapter_index].key_component);
    }
    retro_unload_game();
    retro_deinit();
    if (csv_output != NULL && fclose(csv_output) != 0) {
        fputs("could not close CSV\n", stderr);
        return 1;
    }
    if (chapter_filter == NULL)
        printf("All %u fixed chapter/tide rows and six initial screens passed "
               "production visual acceptance\n", passed_rows);
    else
        printf("All %u filtered fixed chapter/tide rows passed production "
               "visual acceptance\n", passed_rows);
    return 0;

failed:
    retro_unload_game();
    retro_deinit();
    if (csv_output != NULL)
        fclose(csv_output);
    return 1;
}
