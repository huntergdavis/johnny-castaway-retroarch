/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef JC_CONTENT_H
#define JC_CONTENT_H

#include "jc_resource_map.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct retro_vfs_interface;

#define JC_CONTENT_PATH_MAX 4096u

typedef struct jc_content {
    char map_path[JC_CONTENT_PATH_MAX];
    char archive_path[JC_CONTENT_PATH_MAX];
    uint64_t archive_size;
    jc_resource_map_t map;
    bool ready;
} jc_content_t;

typedef struct jc_resource_info {
    char name[JC_RESOURCE_NAME_BYTES + 1u];
    uint32_t body_size;
    uint64_t body_offset;
    unsigned map_index;
} jc_resource_info_t;

bool jc_content_load(jc_content_t *content, const char *content_path,
                     const struct retro_vfs_interface *vfs,
                     char *error, size_t error_size);
void jc_content_unload(jc_content_t *content);
bool jc_content_find_resource(const jc_content_t *content, const char *name,
                              const struct retro_vfs_interface *vfs,
                              jc_resource_info_t *resource,
                              char *error, size_t error_size);
bool jc_content_read_resource(const jc_content_t *content,
                              const jc_resource_info_t *resource,
                              const struct retro_vfs_interface *vfs,
                              uint8_t *data, size_t size,
                              char *error, size_t error_size);

#endif
