/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef JC_DECOMPRESS_H
#define JC_DECOMPRESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JC_COMPRESSION_NONE 0u
#define JC_COMPRESSION_RLE 1u
#define JC_COMPRESSION_LZW 2u

bool jc_decompress(uint8_t method, const uint8_t *input, size_t input_size,
                   uint8_t *output, size_t output_size,
                   char *error, size_t error_size);

#endif
