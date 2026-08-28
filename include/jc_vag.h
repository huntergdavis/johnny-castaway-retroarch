/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef JC_VAG_H
#define JC_VAG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JC_VAG_HEADER_SIZE 48u
#define JC_VAG_BLOCK_SIZE 16u
#define JC_VAG_SAMPLES_PER_BLOCK 28u

typedef enum jc_vag_status {
    JC_VAG_OK = 0,
    JC_VAG_ERR_INVALID_ARGUMENT,
    JC_VAG_ERR_TRUNCATED,
    JC_VAG_ERR_BAD_MAGIC,
    JC_VAG_ERR_BAD_SIZE,
    JC_VAG_ERR_UNSUPPORTED_RATE,
    JC_VAG_ERR_BAD_BLOCK,
    JC_VAG_ERR_OUTPUT_TOO_SMALL
} jc_vag_status_t;

typedef struct jc_vag_info {
    uint32_t sample_rate;
    size_t sample_count;
    bool has_loop;
    size_t loop_start;
    size_t loop_end;
} jc_vag_info_t;

/*
 * Validate a standard 48-byte Sony VAGp file and report decoded sizing and
 * loop markers without allocating memory. loop_end is exclusive.
 */
jc_vag_status_t jc_vag_probe(const void *data, size_t size,
                             jc_vag_info_t *info);

/*
 * Decode PS1 SPU ADPCM into unsigned 8-bit mono PCM for jc_audio. The caller
 * owns output and must provide at least info.sample_count bytes.
 */
jc_vag_status_t jc_vag_decode_u8(const void *data, size_t size,
                                 uint8_t *output, size_t output_size,
                                 jc_vag_info_t *info);

const char *jc_vag_status_string(jc_vag_status_t status);

#endif
