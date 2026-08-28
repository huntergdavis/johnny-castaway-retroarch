/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Nonblocking C99 translation of antigerme/wilson-reborn walk.rs at revision
 * 2d302f5, which in turn faithfully ports jno6809/jc_reborn walk.c.
 */
#include "jc_walk.h"

#include <stdint.h>
#include <string.h>

typedef uint16_t uint16;
#include "jc_walk_data.inc"

#define JC_WALK_DATA_COUNT (sizeof(walkData) / sizeof(walkData[0]))

static int calculate_turn_increment(int next_heading, int current_heading)
{
    int delta = (next_heading - current_heading) & 7;
    if (delta == 0)
        return 0;
    return delta < 4 ? 1 : -1;
}

bool jc_walk_init(jc_walker_t *walker, uint8_t from_spot,
                  uint8_t from_heading, uint8_t to_spot,
                  uint8_t to_heading, jc_rng_t *rng)
{
    if (walker == NULL || rng == NULL || from_spot >= JC_SPOT_COUNT ||
        to_spot >= JC_SPOT_COUNT || from_heading >= JC_HEADING_COUNT ||
        to_heading >= JC_HEADING_COUNT)
        return false;
    memset(walker, 0, sizeof(*walker));
    if (!jc_path_calculate(from_spot, to_spot, rng, &walker->path))
        return false;
    walker->current_spot = from_spot;
    walker->current_heading = from_heading;
    walker->next_spot = -1;
    walker->next_heading = to_heading;
    walker->final_spot = to_spot;
    walker->final_heading = to_heading;
    if (walker->current_spot == walker->final_spot) {
        walker->last_turn = true;
    } else {
        walker->path_index = 1u;
        walker->next_spot = walker->path.spots[walker->path_index];
        walker->next_heading =
            walkDataStartHeadings[walker->current_spot][walker->next_spot];
    }
    walker->turn_increment = calculate_turn_increment(
        walker->next_heading, walker->current_heading);
    return true;
}

bool jc_walk_next(jc_walker_t *walker, jc_walk_frame_t *frame)
{
    const uint16 *data;
    if (walker == NULL || frame == NULL || walker->arrived)
        return false;

    if (walker->next_heading != -1) {
        int delta = ((walker->next_heading - walker->current_heading) & 7) % 7;
        if (delta > 1) {
            walker->current_heading =
                (walker->current_heading + walker->turn_increment) & 7;
            walker->data_index = (size_t)(
                walkDataBookmarksTurns[walker->current_spot] +
                walker->current_heading);
            if (walker->last_turn)
                walker->data_index += 9u;
        } else if (walker->current_spot != walker->final_spot) {
            walker->next_heading = -1;
            walker->behind_tree =
                (walker->current_spot == 3 && walker->next_spot == 4) ||
                (walker->current_spot == 4 && walker->next_spot == 3);
            walker->data_index = (size_t)
                walkDataBookmarks[walker->current_spot][walker->next_spot];
        } else {
            walker->data_index = (size_t)(
                walkDataBookmarksTurns[walker->final_spot] +
                walker->final_heading + 9);
            walker->current_heading = walker->final_heading;
            walker->arrived = true;
        }
    } else {
        ++walker->data_index;
        if (walker->data_index >= JC_WALK_DATA_COUNT)
            return false;
        if (walkData[walker->data_index][1] == 0u) {
            walker->current_heading =
                walkDataEndHeadings[walker->current_spot][walker->next_spot];
            walker->current_spot = walker->next_spot;
            if (walker->current_spot != walker->final_spot) {
                ++walker->path_index;
                if (walker->path_index >= walker->path.length)
                    return false;
                walker->next_spot = walker->path.spots[walker->path_index];
                walker->next_heading =
                    walkDataStartHeadings[walker->current_spot][walker->next_spot];
            } else {
                walker->next_heading = walker->final_heading;
                walker->last_turn = true;
            }
            walker->turn_increment = calculate_turn_increment(
                walker->next_heading, walker->current_heading);
            walker->current_heading =
                (walker->current_heading + walker->turn_increment) & 7;
            walker->data_index = (size_t)(
                walkDataBookmarksTurns[walker->current_spot] +
                walker->current_heading);
            if (walker->last_turn) {
                walker->data_index += 9u;
                if (walker->current_heading == walker->final_heading)
                    walker->arrived = true;
            }
        }
    }

    if (walker->data_index >= JC_WALK_DATA_COUNT)
        return false;
    data = walkData[walker->data_index];
    frame->flip_horizontal = data[0] != 0u;
    frame->x = (int)data[1] - 1;
    frame->y = (int)data[2];
    frame->sprite = data[3];
    frame->delay_ticks = walker->arrived ? 80u : 6u;
    frame->behind_tree = walker->behind_tree;
    return true;
}
