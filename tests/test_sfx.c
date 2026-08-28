/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Synthetic-only tests: no original Johnny Castaway audio is read or copied. */
#include "jc_sfx.h"
#include "libretro.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TEST_DIRECTORY "build/tests/sfx-fixture"
#define TEST_CONTENT TEST_DIRECTORY "/RESOURCE.MAP"

static void put_u16le(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void put_u32le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static size_t make_wav(uint8_t *wav, size_t capacity,
                       const uint8_t *samples, size_t sample_count)
{
    size_t size = 44u + sample_count + (sample_count & 1u);

    assert(size <= capacity);
    memset(wav, 0, size);
    memcpy(wav, "RIFF", 4u);
    put_u32le(wav + 4u, (uint32_t)(size - 8u));
    memcpy(wav + 8u, "WAVEfmt ", 8u);
    put_u32le(wav + 16u, 16u);
    put_u16le(wav + 20u, 1u);
    put_u16le(wav + 22u, 1u);
    put_u32le(wav + 24u, JC_WAV_SOURCE_RATE);
    put_u32le(wav + 28u, JC_WAV_SOURCE_RATE);
    put_u16le(wav + 32u, 1u);
    put_u16le(wav + 34u, 8u);
    memcpy(wav + 36u, "data", 4u);
    put_u32le(wav + 40u, (uint32_t)sample_count);
    memcpy(wav + 44u, samples, sample_count);
    return size;
}

static void make_directory(const char *path)
{
    if (mkdir(path, 0777) != 0)
        assert(errno == EEXIST);
}

static void prepare_directory(void)
{
    unsigned sample_id;
    char path[JC_SFX_PATH_MAX];

    make_directory("build");
    make_directory("build/tests");
    make_directory(TEST_DIRECTORY);
    for (sample_id = 0u; sample_id < JC_AUDIO_ORIGINAL_SAMPLE_COUNT;
         ++sample_id) {
        assert(jc_sfx_build_sample_path(path, sizeof(path), TEST_CONTENT,
                                        sample_id) == JC_SFX_OK);
        (void)remove(path);
    }
}

static void clean_directory(void)
{
    prepare_directory();
    assert(rmdir(TEST_DIRECTORY) == 0);
}

static void write_file(const char *path, const void *data, size_t size)
{
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    assert(fwrite(data, 1u, size, file) == size);
    assert(fclose(file) == 0);
}

static void write_oversized_file(const char *path)
{
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    assert(fseek(file, (long)JC_SFX_MAX_WAV_BYTES, SEEK_SET) == 0);
    assert(fputc(0, file) == 0);
    assert(fclose(file) == 0);
}

static void test_path_safety_and_errors(void)
{
    char path[64];
    char small[8];
    char long_path[JC_SFX_PATH_MAX + 32u];
    jc_sfx_t sfx;
    jc_audio_t audio;
    jc_sfx_report_t report;
    struct retro_vfs_interface incomplete_vfs;

    assert(jc_sfx_build_sample_path(path, sizeof(path), "RESOURCE.MAP", 0u) ==
           JC_SFX_OK);
    assert(strcmp(path, "sound0.wav") == 0);
    assert(jc_sfx_build_sample_path(path, sizeof(path), "/games/RESOURCE.001",
                                    24u) == JC_SFX_OK);
    assert(strcmp(path, "/games/sound24.wav") == 0);
    assert(jc_sfx_build_sample_path(path, sizeof(path),
                                    "C:\\games\\RESOURCE.MAP", 3u) ==
           JC_SFX_OK);
    assert(strcmp(path, "C:\\games\\sound3.wav") == 0);
    assert(jc_sfx_build_sample_path(path, sizeof(path), "RESOURCE.bin", 0u) ==
           JC_SFX_ERR_UNSUPPORTED_CONTENT_PATH);
    assert(path[0] == '\0');
    assert(jc_sfx_build_sample_path(path, sizeof(path), "RESOURCE.MAP",
                                    JC_AUDIO_ORIGINAL_SAMPLE_COUNT) ==
           JC_SFX_ERR_INVALID_ARGUMENT);
    assert(jc_sfx_build_sample_path(small, sizeof(small), "/x/RESOURCE.MAP",
                                    24u) == JC_SFX_ERR_PATH_TOO_LONG);
    assert(small[0] == '\0');
    memset(long_path, 'a', sizeof(long_path));
    memcpy(long_path + sizeof(long_path) - 5u, ".MAP", 5u);
    assert(jc_sfx_build_sample_path(path, sizeof(path), long_path, 0u) ==
           JC_SFX_ERR_PATH_TOO_LONG);

    jc_sfx_init(&sfx);
    jc_audio_init(&audio);
    memset(&incomplete_vfs, 0, sizeof(incomplete_vfs));
    assert(jc_sfx_load(&sfx, &audio, TEST_CONTENT, &incomplete_vfs, &report) ==
           JC_SFX_ERR_INCOMPLETE_VFS);
    assert(report.status == JC_SFX_ERR_INCOMPLETE_VFS);
    assert(jc_sfx_load(&sfx, &audio, "not-content.txt", NULL, &report) ==
           JC_SFX_ERR_UNSUPPORTED_CONTENT_PATH);
    assert(strcmp(jc_sfx_status_string(report.status),
                  "content path is not a MAP or 001 file") == 0);
    assert(jc_sfx_load(NULL, &audio, TEST_CONTENT, NULL, &report) ==
           JC_SFX_ERR_INVALID_ARGUMENT);
}

static void test_all_missing_is_optional(void)
{
    jc_sfx_t sfx;
    jc_audio_t audio;
    jc_sfx_report_t report;

    prepare_directory();
    jc_sfx_init(&sfx);
    jc_audio_init(&audio);
    assert(jc_sfx_load(&sfx, &audio, TEST_CONTENT, NULL, &report) == JC_SFX_OK);
    assert(report.status == JC_SFX_OK);
    assert(report.attempted_count == 23u);
    assert(report.loaded_count == 0u);
    assert(report.missing_count == JC_AUDIO_ORIGINAL_SAMPLE_COUNT);
    assert(report.invalid_count == 0u);
    assert(report.files[11].status == JC_SFX_FILE_MISSING);
    assert(report.files[13].status == JC_SFX_FILE_MISSING);
    assert(report.missing_ids == (((uint32_t)1u << 25u) - 1u));
    jc_sfx_unload(&sfx, &audio);
}

static void test_valid_stdio_load_and_mix(void)
{
    static const uint8_t samples[] = {255u, 0u};
    uint8_t wav[64];
    char path[JC_SFX_PATH_MAX];
    int16_t output[8];
    size_t size = make_wav(wav, sizeof(wav), samples, sizeof(samples));
    jc_sfx_t sfx;
    jc_audio_t audio;
    jc_sfx_report_t report;

    prepare_directory();
    assert(jc_sfx_build_sample_path(path, sizeof(path), TEST_CONTENT, 0u) ==
           JC_SFX_OK);
    write_file(path, wav, size);
    assert(jc_sfx_build_sample_path(path, sizeof(path), TEST_CONTENT, 24u) ==
           JC_SFX_OK);
    write_file(path, wav, size);

    jc_sfx_init(&sfx);
    jc_audio_init(&audio);
    assert(jc_sfx_load(&sfx, &audio, TEST_CONTENT, NULL, &report) == JC_SFX_OK);
    assert(report.loaded_count == 2u && report.missing_count == 23u);
    assert(report.invalid_count == 0u);
    assert(report.loaded_ids == (((uint32_t)1u << 0u) |
                                 ((uint32_t)1u << 24u)));
    assert(jc_audio_has_sample(&audio, 0u));
    assert(jc_audio_has_sample(&audio, 24u));
    assert(jc_audio_trigger(&audio, 0u) == 0);
    assert(jc_audio_mix(&audio, output, 4u) == 4u);
    assert(output[0] > 0 && output[1] > 0);
    jc_sfx_unload(&sfx, &audio);
    assert(!jc_audio_has_sample(&audio, 0u));
    assert(!jc_audio_has_sample(&audio, 24u));
}

static void test_all_original_effects_load_and_mix(void)
{
    uint8_t wav[64];
    char path[JC_SFX_PATH_MAX];
    int16_t output[8];
    uint32_t expected_ids = 0u;
    unsigned sample_id;
    jc_sfx_t sfx;
    jc_audio_t audio;
    jc_sfx_report_t report;

    prepare_directory();
    for (sample_id = 0u; sample_id < JC_AUDIO_ORIGINAL_SAMPLE_COUNT;
         ++sample_id) {
        uint8_t sample;
        size_t size;

        if (sample_id == 11u || sample_id == 13u)
            continue;
        sample = (uint8_t)(129u + sample_id);
        size = make_wav(wav, sizeof(wav), &sample, 1u);
        assert(jc_sfx_build_sample_path(path, sizeof(path), TEST_CONTENT,
                                        sample_id) == JC_SFX_OK);
        write_file(path, wav, size);
        expected_ids |= (uint32_t)1u << sample_id;
    }

    jc_sfx_init(&sfx);
    jc_audio_init(&audio);
    assert(jc_sfx_load(&sfx, &audio, TEST_CONTENT, NULL, &report) == JC_SFX_OK);
    assert(report.attempted_count == 23u);
    assert(report.loaded_count == 23u);
    assert(report.missing_count == 2u);
    assert(report.invalid_count == 0u);
    assert(report.loaded_ids == expected_ids);
    assert(report.missing_ids == (((uint32_t)1u << 11u) |
                                  ((uint32_t)1u << 13u)));

    for (sample_id = 0u; sample_id < JC_AUDIO_ORIGINAL_SAMPLE_COUNT;
         ++sample_id) {
        unsigned frame;

        if (sample_id == 11u || sample_id == 13u) {
            assert(!jc_audio_has_sample(&audio, sample_id));
            assert(jc_audio_trigger(&audio, sample_id) ==
                   JC_AUDIO_INVALID_VOICE);
            continue;
        }
        assert(jc_audio_has_sample(&audio, sample_id));
        assert(jc_audio_trigger(&audio, sample_id) != JC_AUDIO_INVALID_VOICE);
        assert(jc_audio_mix(&audio, output, 4u) == 4u);
        for (frame = 0u; frame < 4u; ++frame) {
            int16_t expected = (int16_t)((sample_id + 1u) * 256u);
            assert(output[frame * 2u] == expected);
            assert(output[frame * 2u + 1u] == expected);
        }
        assert(jc_audio_active_voice_count(&audio) == 0u);
    }

    jc_sfx_unload(&sfx, &audio);
    for (sample_id = 0u; sample_id < JC_AUDIO_ORIGINAL_SAMPLE_COUNT;
         ++sample_id)
        assert(!jc_audio_has_sample(&audio, sample_id));
}

static void test_malformed_and_oversized_rejection(void)
{
    static const uint8_t malformed[] = {'N', 'O', 'P', 'E'};
    char path[JC_SFX_PATH_MAX];
    jc_sfx_t sfx;
    jc_audio_t audio;
    jc_sfx_report_t report;

    prepare_directory();
    assert(jc_sfx_build_sample_path(path, sizeof(path), TEST_CONTENT, 1u) ==
           JC_SFX_OK);
    write_file(path, malformed, sizeof(malformed));
    assert(jc_sfx_build_sample_path(path, sizeof(path), TEST_CONTENT, 2u) ==
           JC_SFX_OK);
    write_oversized_file(path);

    jc_sfx_init(&sfx);
    jc_audio_init(&audio);
    assert(jc_sfx_load(&sfx, &audio, TEST_CONTENT, NULL, &report) == JC_SFX_OK);
    assert(report.loaded_count == 0u);
    assert(report.missing_count == 23u);
    assert(report.invalid_count == 2u);
    assert(report.files[1].status == JC_SFX_FILE_INVALID_WAV);
    assert(report.files[1].wav_status == JC_WAV_ERR_TRUNCATED);
    assert(report.files[2].status == JC_SFX_FILE_OVERSIZED);
    assert(report.files[2].file_size == (uint64_t)JC_SFX_MAX_WAV_BYTES + 1u);
    assert(report.invalid_ids == (((uint32_t)1u << 1u) |
                                  ((uint32_t)1u << 2u)));
    assert(report.oversized_ids == ((uint32_t)1u << 2u));
    assert(!jc_audio_has_sample(&audio, 1u));
    assert(!jc_audio_has_sample(&audio, 2u));
    jc_sfx_unload(&sfx, &audio);
}

typedef struct mock_file {
    const char *path;
    const uint8_t *data;
    size_t size;
} mock_file_t;

struct retro_vfs_file_handle {
    const mock_file_t *file;
    size_t position;
};

static mock_file_t mock_file;
static unsigned mock_open_count;
static unsigned mock_close_count;
static int mock_bad_open_arguments;
static size_t mock_fail_read_after;

static struct retro_vfs_file_handle *RETRO_CALLCONV
mock_open(const char *path, unsigned mode, unsigned hints)
{
    struct retro_vfs_file_handle *handle;

    ++mock_open_count;
    if (mode != RETRO_VFS_FILE_ACCESS_READ ||
        hints != RETRO_VFS_FILE_ACCESS_HINT_SEQUENTIAL_BULK)
        mock_bad_open_arguments = 1;
    if (strcmp(path, mock_file.path) != 0)
        return NULL;
    handle = (struct retro_vfs_file_handle *)malloc(sizeof(*handle));
    assert(handle != NULL);
    handle->file = &mock_file;
    handle->position = 0u;
    return handle;
}

static int RETRO_CALLCONV mock_close(struct retro_vfs_file_handle *handle)
{
    ++mock_close_count;
    free(handle);
    return 0;
}

static int64_t RETRO_CALLCONV mock_size(struct retro_vfs_file_handle *handle)
{
    return (int64_t)handle->file->size;
}

static int64_t RETRO_CALLCONV mock_read(struct retro_vfs_file_handle *handle,
                                        void *data, uint64_t length)
{
    size_t available = handle->file->size - handle->position;
    size_t requested = length > (uint64_t)SIZE_MAX ? SIZE_MAX : (size_t)length;
    size_t count = requested < available ? requested : available;

    if (handle->position >= mock_fail_read_after)
        return -1;
    if (count > 3u)
        count = 3u; /* Exercise the loader's bounded partial-read loop. */
    memcpy(data, handle->file->data + handle->position, count);
    handle->position += count;
    return (int64_t)count;
}

static void test_frontend_vfs_load(void)
{
    static const uint8_t samples[] = {0u, 255u};
    uint8_t wav[64];
    int16_t output[16];
    struct retro_vfs_interface vfs;
    jc_sfx_t sfx;
    jc_audio_t audio;
    jc_sfx_report_t report;

    mock_file.path = "/virtual/sound3.wav";
    mock_file.data = wav;
    mock_file.size = make_wav(wav, sizeof(wav), samples, sizeof(samples));
    mock_open_count = 0u;
    mock_close_count = 0u;
    mock_bad_open_arguments = 0;
    mock_fail_read_after = SIZE_MAX;
    memset(&vfs, 0, sizeof(vfs));
    vfs.open = mock_open;
    vfs.close = mock_close;
    vfs.size = mock_size;
    vfs.read = mock_read;

    jc_sfx_init(&sfx);
    jc_audio_init(&audio);
    assert(jc_sfx_load(&sfx, &audio, "/virtual/RESOURCE.001", &vfs,
                       &report) == JC_SFX_OK);
    assert(report.loaded_count == 1u && report.files[3].status == JC_SFX_FILE_LOADED);
    assert(mock_open_count == 23u && mock_close_count == 1u);
    assert(!mock_bad_open_arguments);
    assert(jc_audio_trigger(&audio, 3u) == 0);
    assert(jc_audio_mix(&audio, output, 8u) == 8u);
    assert(output[0] < 0 && output[8] > 0);
    jc_sfx_unload(&sfx, &audio);

    mock_open_count = 0u;
    mock_close_count = 0u;
    mock_fail_read_after = 3u;
    assert(jc_sfx_load(&sfx, &audio, "/virtual/RESOURCE.001", &vfs,
                       &report) == JC_SFX_OK);
    assert(report.loaded_count == 0u && report.invalid_count == 1u);
    assert(report.files[3].status == JC_SFX_FILE_IO_ERROR);
    assert(report.io_error_ids == ((uint32_t)1u << 3u));
    assert(mock_open_count == 23u && mock_close_count == 1u);
    assert(!jc_audio_has_sample(&audio, 3u));
    jc_sfx_unload(&sfx, &audio);
}

int main(void)
{
    test_path_safety_and_errors();
    test_all_missing_is_optional();
    test_valid_stdio_load_and_mix();
    test_all_original_effects_load_and_mix();
    test_malformed_and_oversized_rejection();
    test_frontend_vfs_load();
    clean_directory();
    puts("SFX loader tests passed");
    return 0;
}
