/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef JC_OCEAN_H
#define JC_OCEAN_H

#include <stddef.h>
#include <stdint.h>

#include "jc_vag.h"

/* Exact size of the embedded CC0-derived Sony VAG source. */
size_t jc_ocean_vag_size(void);

/* Probe or decode the embedded loop with the same bounded VAG implementation. */
jc_vag_status_t jc_ocean_probe(jc_vag_info_t *info);
jc_vag_status_t jc_ocean_decode_u8(uint8_t *output, size_t output_size,
                                   jc_vag_info_t *info);

#endif
