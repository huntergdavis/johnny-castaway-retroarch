/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "jc_resource_map.h"

#include <stdio.h>
#include <string.h>

#define JC_MAP_HEADER_BYTES 21u
#define JC_MAP_ENTRY_BYTES 8u
#define JC_ARCHIVE_ENTRY_HEADER_BYTES 17u

static uint16_t read_le16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static bool archive_name_is_safe(const char *name)
{
    const unsigned char *cursor = (const unsigned char *)name;

    if (*cursor == '\0')
        return false;
    for (; *cursor != '\0'; ++cursor) {
        if (*cursor == '/' || *cursor == '\\' || *cursor == ':')
            return false;
        if (*cursor < 0x20u || *cursor == 0x7fu)
            return false;
    }
    return true;
}

static bool fail(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size > 0u)
        snprintf(error, error_size, "%s", message);
    return false;
}

bool jc_resource_map_parse(jc_resource_map_t *map, const uint8_t *data,
                           size_t size, char *error, size_t error_size)
{
    uint16_t count;
    size_t required;
    size_t index;

    if (map == NULL || data == NULL)
        return fail(error, error_size, "RESOURCE.MAP parser received null input");
    if (size < JC_MAP_HEADER_BYTES)
        return fail(error, error_size, "RESOURCE.MAP is truncated before its header");

    memset(map, 0, sizeof(*map));
    memcpy(map->archive_name, data + 6u, JC_RESOURCE_NAME_BYTES);
    map->archive_name[JC_RESOURCE_NAME_BYTES] = '\0';
    if (memchr(map->archive_name, '\0', JC_RESOURCE_NAME_BYTES) == NULL)
        return fail(error, error_size, "RESOURCE.MAP archive name is not terminated");
    if (!archive_name_is_safe(map->archive_name))
        return fail(error, error_size, "RESOURCE.MAP archive name is unsafe");

    count = read_le16(data + 19u);
    if (count == 0u || count > JC_RESOURCE_MAP_MAX_ENTRIES)
        return fail(error, error_size, "RESOURCE.MAP entry count is invalid");
    required = JC_MAP_HEADER_BYTES + (size_t)count * JC_MAP_ENTRY_BYTES;
    if (size < required)
        return fail(error, error_size, "RESOURCE.MAP is truncated in its entry table");

    map->entry_count = count;
    for (index = 0u; index < count; ++index) {
        const uint8_t *entry = data + JC_MAP_HEADER_BYTES + index * JC_MAP_ENTRY_BYTES;
        map->entries[index].length = read_le32(entry);
        map->entries[index].offset = read_le32(entry + 4u);
    }

    if (error != NULL && error_size > 0u)
        error[0] = '\0';
    return true;
}

bool jc_resource_map_validate_archive(const jc_resource_map_t *map,
                                      uint64_t archive_size,
                                      char *error, size_t error_size)
{
    size_t index;

    if (map == NULL)
        return fail(error, error_size, "archive validation received a null map");
    for (index = 0u; index < map->entry_count; ++index) {
        uint64_t minimum_end = (uint64_t)map->entries[index].offset +
                               JC_ARCHIVE_ENTRY_HEADER_BYTES;
        if (minimum_end > archive_size) {
            if (error != NULL && error_size > 0u)
                snprintf(error, error_size,
                         "RESOURCE.MAP entry %lu points outside the archive",
                         (unsigned long)index);
            return false;
        }
    }
    if (error != NULL && error_size > 0u)
        error[0] = '\0';
    return true;
}
