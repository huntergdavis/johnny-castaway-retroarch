/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Optional sibling-WAV convention derived from Hunter Davis's GPLv3 PS1
 * port README and sound_ps1.c at public revision 25c5d84593ac20cbee354eaab7779ab7397d6bbe:
 * sound0.wav through sound24.wav are optional, and IDs 11 and 13 are absent.
 * This portable loader is new code over this repository's jc_wav/jc_audio
 * ownership contracts. It embeds or redistributes no original audio.
 */
#include "jc_sfx.h"
#include "libretro.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JC_SFX_INITIALIZED 0x31584653u

typedef struct jc_sfx_file {
    const struct retro_vfs_interface *vfs;
    struct retro_vfs_file_handle *vfs_handle;
    FILE *stdio_handle;
} jc_sfx_file_t;

static int ascii_equal(char left, char right)
{
    if (left >= 'A' && left <= 'Z')
        left = (char)(left - 'A' + 'a');
    if (right >= 'A' && right <= 'Z')
        right = (char)(right - 'A' + 'a');
    return left == right;
}

static int path_has_extension(const char *path, const char *extension)
{
    size_t path_length = strlen(path);
    size_t extension_length = strlen(extension);
    size_t index;

    if (path_length < extension_length)
        return 0;
    path += path_length - extension_length;
    for (index = 0u; index < extension_length; ++index) {
        if (!ascii_equal(path[index], extension[index]))
            return 0;
    }
    return 1;
}

static void report_init(jc_sfx_report_t *report)
{
    memset(report, 0, sizeof(*report));
    report->status = JC_SFX_OK;
}

static void report_mark(jc_sfx_report_t *report, unsigned sample_id,
                        jc_sfx_file_status_t status, uint64_t file_size,
                        jc_wav_status_t wav_status)
{
    uint32_t bit = (uint32_t)1u << sample_id;
    jc_sfx_file_report_t *file = &report->files[sample_id];

    file->status = status;
    file->file_size = file_size;
    file->wav_status = wav_status;
    if (status == JC_SFX_FILE_LOADED) {
        report->loaded_ids |= bit;
        ++report->loaded_count;
    } else if (status == JC_SFX_FILE_MISSING) {
        report->missing_ids |= bit;
        ++report->missing_count;
    } else {
        report->invalid_ids |= bit;
        ++report->invalid_count;
        if (status == JC_SFX_FILE_OVERSIZED)
            report->oversized_ids |= bit;
        if (status == JC_SFX_FILE_IO_ERROR)
            report->io_error_ids |= bit;
    }
}

static int file_open(jc_sfx_file_t *file, const char *path,
                     const struct retro_vfs_interface *vfs)
{
    memset(file, 0, sizeof(*file));
    if (vfs != NULL) {
        file->vfs = vfs;
        file->vfs_handle = vfs->open(
            path, RETRO_VFS_FILE_ACCESS_READ,
            RETRO_VFS_FILE_ACCESS_HINT_SEQUENTIAL_BULK);
        return file->vfs_handle != NULL;
    }
    file->stdio_handle = fopen(path, "rb");
    return file->stdio_handle != NULL;
}

static int file_close(jc_sfx_file_t *file)
{
    int result = 0;

    if (file->vfs_handle != NULL)
        result = file->vfs->close(file->vfs_handle);
    else if (file->stdio_handle != NULL)
        result = fclose(file->stdio_handle);
    memset(file, 0, sizeof(*file));
    return result == 0;
}

static int64_t file_size(jc_sfx_file_t *file)
{
    long size;

    if (file->vfs_handle != NULL)
        return file->vfs->size(file->vfs_handle);
    if (fseek(file->stdio_handle, 0L, SEEK_END) != 0)
        return -1;
    size = ftell(file->stdio_handle);
    if (size < 0L || fseek(file->stdio_handle, 0L, SEEK_SET) != 0)
        return -1;
    return (int64_t)size;
}

static int file_read_exact(jc_sfx_file_t *file, uint8_t *data, size_t size)
{
    size_t offset = 0u;

    if (file->vfs_handle == NULL)
        return fread(data, 1u, size, file->stdio_handle) == size;
    while (offset < size) {
        int64_t read = file->vfs->read(file->vfs_handle, data + offset,
                                       (uint64_t)(size - offset));
        if (read <= 0 || (uint64_t)read > (uint64_t)(size - offset))
            return 0;
        offset += (size_t)read;
    }
    return 1;
}

void jc_sfx_init(jc_sfx_t *sfx)
{
    if (sfx == NULL)
        return;
    memset(sfx, 0, sizeof(*sfx));
    sfx->initialized = JC_SFX_INITIALIZED;
}

void jc_sfx_unload(jc_sfx_t *sfx, jc_audio_t *audio)
{
    unsigned sample_id;

    if (sfx == NULL || sfx->initialized != JC_SFX_INITIALIZED)
        return;
    for (sample_id = 0u; sample_id < JC_AUDIO_ORIGINAL_SAMPLE_COUNT;
         ++sample_id) {
        if (sfx->wav_data[sample_id] != NULL) {
            if (audio != NULL)
                jc_audio_unload_sample(audio, sample_id);
            free(sfx->wav_data[sample_id]);
            sfx->wav_data[sample_id] = NULL;
            sfx->wav_size[sample_id] = 0u;
        }
    }
}

jc_sfx_status_t jc_sfx_build_sample_path(char *output, size_t output_size,
                                         const char *content_path,
                                         unsigned sample_id)
{
    const char *slash;
    const char *backslash;
    const char *separator;
    char filename[24];
    size_t content_length;
    size_t prefix_length;
    size_t filename_length;
    int written;

    if (output != NULL && output_size > 0u)
        output[0] = '\0';
    if (output == NULL || output_size == 0u || content_path == NULL ||
        content_path[0] == '\0' ||
        sample_id >= JC_AUDIO_ORIGINAL_SAMPLE_COUNT)
        return JC_SFX_ERR_INVALID_ARGUMENT;
    content_length = strlen(content_path);
    if (content_length >= JC_SFX_PATH_MAX)
        return JC_SFX_ERR_PATH_TOO_LONG;
    if (!path_has_extension(content_path, ".map") &&
        !path_has_extension(content_path, ".001"))
        return JC_SFX_ERR_UNSUPPORTED_CONTENT_PATH;

    slash = strrchr(content_path, '/');
    backslash = strrchr(content_path, '\\');
    separator = slash;
    if (backslash != NULL && (separator == NULL || backslash > separator))
        separator = backslash;
    prefix_length = separator == NULL ? 0u : (size_t)(separator - content_path + 1);
    written = snprintf(filename, sizeof(filename), "sound%u.wav", sample_id);
    if (written < 0 || (size_t)written >= sizeof(filename))
        return JC_SFX_ERR_PATH_TOO_LONG;
    filename_length = (size_t)written;
    if (prefix_length > output_size - 1u ||
        filename_length > output_size - prefix_length - 1u)
        return JC_SFX_ERR_PATH_TOO_LONG;
    memcpy(output, content_path, prefix_length);
    memcpy(output + prefix_length, filename, filename_length + 1u);
    return JC_SFX_OK;
}

jc_sfx_status_t jc_sfx_load(jc_sfx_t *sfx, jc_audio_t *audio,
                            const char *content_path,
                            const struct retro_vfs_interface *vfs,
                            jc_sfx_report_t *report)
{
    char path[JC_SFX_PATH_MAX];
    unsigned sample_id;
    jc_sfx_status_t status;

    if (report == NULL)
        return JC_SFX_ERR_INVALID_ARGUMENT;
    report_init(report);
    if (sfx == NULL || sfx->initialized != JC_SFX_INITIALIZED ||
        audio == NULL || content_path == NULL) {
        report->status = JC_SFX_ERR_INVALID_ARGUMENT;
        return report->status;
    }
    if (vfs != NULL && (vfs->open == NULL || vfs->close == NULL ||
                        vfs->size == NULL || vfs->read == NULL)) {
        report->status = JC_SFX_ERR_INCOMPLETE_VFS;
        return report->status;
    }
    status = jc_sfx_build_sample_path(path, sizeof(path), content_path,
                                      JC_AUDIO_ORIGINAL_SAMPLE_COUNT - 1u);
    if (status != JC_SFX_OK) {
        report->status = status;
        return status;
    }

    jc_sfx_unload(sfx, audio);
    for (sample_id = 0u; sample_id < JC_AUDIO_ORIGINAL_SAMPLE_COUNT;
         ++sample_id) {
        jc_sfx_file_t file;
        int64_t signed_size;
        size_t size;
        uint8_t *bytes;
        int read_ok;
        int close_ok;
        jc_wav_pcm_t pcm;
        jc_wav_status_t wav_status;

        if (sample_id == 11u || sample_id == 13u) {
            report_mark(report, sample_id, JC_SFX_FILE_MISSING, 0u,
                        JC_WAV_OK);
            continue;
        }
        ++report->attempted_count;
        status = jc_sfx_build_sample_path(path, sizeof(path), content_path,
                                          sample_id);
        if (status != JC_SFX_OK) {
            jc_sfx_unload(sfx, audio);
            report_init(report);
            report->status = status;
            return status;
        }
        if (!file_open(&file, path, vfs)) {
            report_mark(report, sample_id, JC_SFX_FILE_MISSING, 0u,
                        JC_WAV_OK);
            continue;
        }
        signed_size = file_size(&file);
        if (signed_size < 0) {
            (void)file_close(&file);
            report_mark(report, sample_id, JC_SFX_FILE_IO_ERROR, 0u,
                        JC_WAV_OK);
            continue;
        }
        if ((uint64_t)signed_size > (uint64_t)JC_SFX_MAX_WAV_BYTES) {
            (void)file_close(&file);
            report_mark(report, sample_id, JC_SFX_FILE_OVERSIZED,
                        (uint64_t)signed_size, JC_WAV_OK);
            continue;
        }
        size = (size_t)signed_size;
        if (size == 0u) {
            (void)file_close(&file);
            report_mark(report, sample_id, JC_SFX_FILE_INVALID_WAV, 0u,
                        JC_WAV_ERR_TRUNCATED);
            continue;
        }
        bytes = (uint8_t *)malloc(size);
        if (bytes == NULL) {
            (void)file_close(&file);
            jc_sfx_unload(sfx, audio);
            report_init(report);
            report->status = JC_SFX_ERR_OUT_OF_MEMORY;
            return report->status;
        }
        read_ok = file_read_exact(&file, bytes, size);
        close_ok = file_close(&file);
        if (!read_ok || !close_ok) {
            free(bytes);
            report_mark(report, sample_id, JC_SFX_FILE_IO_ERROR,
                        (uint64_t)size, JC_WAV_OK);
            continue;
        }
        wav_status = jc_wav_parse(bytes, size, &pcm);
        if (wav_status != JC_WAV_OK ||
            !jc_audio_set_sample(audio, sample_id, &pcm)) {
            free(bytes);
            report_mark(report, sample_id, JC_SFX_FILE_INVALID_WAV,
                        (uint64_t)size,
                        wav_status == JC_WAV_OK
                            ? JC_WAV_ERR_UNSUPPORTED_FORMAT
                            : wav_status);
            continue;
        }
        sfx->wav_data[sample_id] = bytes;
        sfx->wav_size[sample_id] = size;
        report_mark(report, sample_id, JC_SFX_FILE_LOADED, (uint64_t)size,
                    JC_WAV_OK);
    }
    return JC_SFX_OK;
}

const char *jc_sfx_status_string(jc_sfx_status_t status)
{
    switch (status) {
    case JC_SFX_OK:
        return "ok";
    case JC_SFX_ERR_INVALID_ARGUMENT:
        return "invalid argument";
    case JC_SFX_ERR_UNSUPPORTED_CONTENT_PATH:
        return "content path is not a MAP or 001 file";
    case JC_SFX_ERR_PATH_TOO_LONG:
        return "derived sound path is too long";
    case JC_SFX_ERR_INCOMPLETE_VFS:
        return "libretro VFS interface is incomplete";
    case JC_SFX_ERR_OUT_OF_MEMORY:
        return "not enough memory for sound effects";
    default:
        return "unknown sound-effect loader error";
    }
}
