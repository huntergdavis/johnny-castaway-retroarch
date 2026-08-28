/* SPDX-License-Identifier: GPL-3.0-or-later */
/* C99 port of antigerme/wilson-reborn path.rs at revision 2d302f5. */
#include "jc_path.h"

#include <string.h>

typedef struct jc_route_section {
    uint8_t count;
    uint8_t next[6];
    uint8_t weight[6];
} jc_route_section_t;

#include "jc_path_data.inc"

static bool path_contains(const jc_path_t *path, uint8_t spot)
{
    size_t index;
    for (index = 0u; index < path->length; ++index) {
        if (path->spots[index] == spot)
            return true;
    }
    return false;
}

static bool pick_next(const jc_route_section_t *section, jc_rng_t *rng,
                      uint8_t *next)
{
    uint32_t total = 0u;
    uint32_t roll;
    unsigned index;
    for (index = 0u; index < section->count; ++index)
        total += section->weight[index];
    if (total == 0u)
        return false;
    roll = jc_rng_below(rng, total);
    for (index = 0u; index < section->count; ++index) {
        if (roll < section->weight[index]) {
            *next = section->next[index];
            return true;
        }
        roll -= section->weight[index];
    }
    return false;
}

static bool find_first_path(const jc_route_section_t sections[6],
                            uint8_t current, uint8_t destination,
                            bool visited[6], jc_path_t *path)
{
    const jc_route_section_t *section = &sections[current];
    unsigned index;
    path->spots[path->length++] = current;
    if (current == destination)
        return true;
    visited[current] = true;
    for (index = 0u; index < section->count; ++index) {
        uint8_t next = section->next[index];
        if (next < JC_SPOT_COUNT && !visited[next] &&
            find_first_path(sections, next, destination, visited, path))
            return true;
    }
    visited[current] = false;
    --path->length;
    return false;
}

bool jc_path_calculate(uint8_t from_spot, uint8_t to_spot,
                       jc_rng_t *rng, jc_path_t *path)
{
    const jc_route_section_t *sections;
    uint8_t current;
    bool visited[6] = {false, false, false, false, false, false};
    if (rng == NULL || path == NULL || from_spot >= JC_SPOT_COUNT ||
        to_spot >= JC_SPOT_COUNT)
        return false;
    memset(path, 0, sizeof(*path));
    path->spots[0] = from_spot;
    path->length = 1u;
    if (from_spot == to_spot)
        return true;

    sections = jc_route_sections[from_spot][to_spot];
    current = from_spot;
    while (current != to_spot && path->length < JC_PATH_MAX_SPOTS) {
        uint8_t next;
        if (!pick_next(&sections[current], rng, &next) ||
            next >= JC_SPOT_COUNT || path_contains(path, next))
            break;
        path->spots[path->length++] = next;
        current = next;
    }
    if (current == to_spot)
        return true;

    memset(path, 0, sizeof(*path));
    return find_first_path(sections, from_spot, to_spot, visited, path);
}
