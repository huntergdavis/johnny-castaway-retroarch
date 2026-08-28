/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef JC_CHAPTERS_H
#define JC_CHAPTERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct jc_chapter {
    const char *slug;
    const char *title;
    const char *ads_name;
    const char *caption_id;
    const char *ps1_preview_name;
    uint16_t ads_tag;
    uint16_t ps1_frame_count;
    uint8_t family_index;
    bool ps1_validated;
} jc_chapter_t;

size_t jc_chapter_count(void);
const jc_chapter_t *jc_chapter_at(size_t index);
const jc_chapter_t *jc_chapter_lookup(const char *slug);
const jc_chapter_t *jc_chapter_for_ads(const char *ads_name,
                                       uint16_t ads_tag);
const char *jc_chapter_family_name(uint8_t family_index);

#endif
