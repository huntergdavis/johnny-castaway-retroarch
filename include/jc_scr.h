/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef JC_SCR_H
#define JC_SCR_H

#include "jc_surface.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool jc_scr_decode(const uint8_t *data, size_t size,
                   uint8_t *pixel_storage, size_t pixel_storage_size,
                   jc_surface_t *surface, char *error, size_t error_size);

#endif
