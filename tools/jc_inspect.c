/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "jc_content.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    jc_content_t content;
    char error[256];

    if (argc != 2) {
        fprintf(stderr, "usage: %s /path/to/RESOURCE.MAP\n", argv[0]);
        return 2;
    }
    if (!jc_content_load(&content, argv[1], NULL, error, sizeof(error))) {
        fprintf(stderr, "invalid Johnny Castaway content: %s\n", error);
        return 1;
    }

    printf("map: %s\n", content.map_path);
    printf("archive: %s (%llu bytes)\n", content.archive_path,
           (unsigned long long)content.archive_size);
    printf("resources: %u\n", (unsigned)content.map.entry_count);
    jc_content_unload(&content);
    return 0;
}
