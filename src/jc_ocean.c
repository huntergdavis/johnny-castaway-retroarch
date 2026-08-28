/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The wrapper code is GPL-3.0-or-later. The embedded recording is derived
 * from BigSoundBank sound 0266, "Sea: Waves", dedicated to the public domain
 * under CC0 1.0. See docs/licenses/BigSoundBank-0266-CC0.md.
 */
#include "jc_ocean.h"

static const uint8_t ocean_vag[] = {
#include "jc_ocean_vag.inc"
};

size_t jc_ocean_vag_size(void)
{
    return sizeof(ocean_vag);
}

jc_vag_status_t jc_ocean_probe(jc_vag_info_t *info)
{
    return jc_vag_probe(ocean_vag, sizeof(ocean_vag), info);
}

jc_vag_status_t jc_ocean_decode_u8(uint8_t *output, size_t output_size,
                                   jc_vag_info_t *info)
{
    return jc_vag_decode_u8(ocean_vag, sizeof(ocean_vag), output,
                            output_size, info);
}
