/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef JC_SFX_H
#define JC_SFX_H

#include <stddef.h>
#include <stdint.h>

#include "jc_audio.h"

struct retro_vfs_interface;

/* Original samples are tiny (the known largest WAV is about 20 KiB). */
#define JC_SFX_MAX_WAV_BYTES (1024u * 1024u)
#define JC_SFX_PATH_MAX 4096u

typedef enum jc_sfx_status {
    JC_SFX_OK = 0,
    JC_SFX_ERR_INVALID_ARGUMENT,
    JC_SFX_ERR_UNSUPPORTED_CONTENT_PATH,
    JC_SFX_ERR_PATH_TOO_LONG,
    JC_SFX_ERR_INCOMPLETE_VFS,
    JC_SFX_ERR_OUT_OF_MEMORY
} jc_sfx_status_t;

typedef enum jc_sfx_file_status {
    JC_SFX_FILE_UNSCANNED = 0,
    JC_SFX_FILE_MISSING,
    JC_SFX_FILE_LOADED,
    JC_SFX_FILE_INVALID_WAV,
    JC_SFX_FILE_OVERSIZED,
    JC_SFX_FILE_IO_ERROR
} jc_sfx_file_status_t;

typedef struct jc_sfx_file_report {
    jc_sfx_file_status_t status;
    jc_wav_status_t wav_status;
    uint64_t file_size;
} jc_sfx_file_report_t;

typedef struct jc_sfx_report {
    jc_sfx_status_t status;
    jc_sfx_file_report_t files[JC_AUDIO_ORIGINAL_SAMPLE_COUNT];
    uint32_t loaded_ids;
    uint32_t missing_ids;
    uint32_t invalid_ids;
    uint32_t oversized_ids;
    uint32_t io_error_ids;
    unsigned attempted_count;
    unsigned loaded_count;
    unsigned missing_count;
    unsigned invalid_count;
} jc_sfx_report_t;

/*
 * Owns the WAV storage referenced by jc_audio_t. Initialize before first use,
 * keep alive while the mixer is in use, and unload with the same mixer.
 */
typedef struct jc_sfx {
    uint8_t *wav_data[JC_AUDIO_ORIGINAL_SAMPLE_COUNT];
    size_t wav_size[JC_AUDIO_ORIGINAL_SAMPLE_COUNT];
    uint32_t initialized;
} jc_sfx_t;

void jc_sfx_init(jc_sfx_t *sfx);
void jc_sfx_unload(jc_sfx_t *sfx, jc_audio_t *audio);

/*
 * Build the fixed sibling name sound<ID>.wav beside a .map or .001 path.
 * The fixed filename prevents caller-controlled traversal. Output is cleared
 * on failure when output_size is nonzero.
 */
jc_sfx_status_t jc_sfx_build_sample_path(char *output, size_t output_size,
                                         const char *content_path,
                                         unsigned sample_id);

/*
 * Scan all 25 IDs. IDs 11 and 13, and every open failure, are optional
 * missing samples. Malformed, unsupported, oversized, and unreadable files
 * are rejected and reported without failing the scan. Fatal API/path/memory
 * failures return a non-OK status and roll back samples loaded by this call.
 * When vfs is non-NULL, only that frontend interface is used; NULL selects
 * the stdio fallback.
 */
jc_sfx_status_t jc_sfx_load(jc_sfx_t *sfx, jc_audio_t *audio,
                            const char *content_path,
                            const struct retro_vfs_interface *vfs,
                            jc_sfx_report_t *report);

const char *jc_sfx_status_string(jc_sfx_status_t status);

#endif
