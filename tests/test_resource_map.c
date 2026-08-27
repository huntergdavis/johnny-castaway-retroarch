/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "jc_resource_map.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void write_le16(unsigned char *data, unsigned value)
{
    data[0] = (unsigned char)value;
    data[1] = (unsigned char)(value >> 8);
}

static void write_le32(unsigned char *data, unsigned long value)
{
    data[0] = (unsigned char)value;
    data[1] = (unsigned char)(value >> 8);
    data[2] = (unsigned char)(value >> 16);
    data[3] = (unsigned char)(value >> 24);
}

int main(void)
{
    unsigned char bytes[37] = {0};
    jc_resource_map_t map;
    char error[128];

    memcpy(bytes + 6, "RESOURCE.001", 12);
    write_le16(bytes + 19, 2);
    write_le32(bytes + 21, 100);
    write_le32(bytes + 25, 0);
    write_le32(bytes + 29, 50);
    write_le32(bytes + 33, 200);

    assert(jc_resource_map_parse(&map, bytes, sizeof(bytes), error, sizeof(error)));
    assert(strcmp(map.archive_name, "RESOURCE.001") == 0);
    assert(map.entry_count == 2);
    assert(map.entries[0].length == 100);
    assert(map.entries[1].offset == 200);
    assert(jc_resource_map_validate_archive(&map, 217, error, sizeof(error)));
    assert(!jc_resource_map_validate_archive(&map, 216, error, sizeof(error)));
    assert(!jc_resource_map_parse(&map, bytes, 20, error, sizeof(error)));

    memcpy(bytes + 6, "../ESCAPE.001", 13);
    assert(!jc_resource_map_parse(&map, bytes, sizeof(bytes), error, sizeof(error)));

    puts("jc_resource_map tests passed");
    return 0;
}
