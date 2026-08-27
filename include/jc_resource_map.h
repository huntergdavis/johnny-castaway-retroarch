/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef JC_RESOURCE_MAP_H
#define JC_RESOURCE_MAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JC_RESOURCE_MAP_MAX_ENTRIES 256u
#define JC_RESOURCE_NAME_BYTES 13u

typedef struct jc_resource_map_entry {
    uint32_t length;
    uint32_t offset;
} jc_resource_map_entry_t;

typedef struct jc_resource_map {
    char archive_name[JC_RESOURCE_NAME_BYTES + 1u];
    uint16_t entry_count;
    jc_resource_map_entry_t entries[JC_RESOURCE_MAP_MAX_ENTRIES];
} jc_resource_map_t;

bool jc_resource_map_parse(jc_resource_map_t *map, const uint8_t *data,
                           size_t size, char *error, size_t error_size);
bool jc_resource_map_validate_archive(const jc_resource_map_t *map,
                                      uint64_t archive_size,
                                      char *error, size_t error_size);

#endif
