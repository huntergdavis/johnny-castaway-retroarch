/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef JC_CAPTIONS_H
#define JC_CAPTIONS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JC_CAPTION_DEFAULT_TICKS 250u

typedef struct jc_caption_entry {
    const char *id;
    const char *text;
} jc_caption_entry_t;

typedef struct jc_caption_ads_map {
    const char *caption_id;
    const char *ads_name;
    uint16_t ads_tag;
} jc_caption_ads_map_t;

typedef struct jc_captions {
    const jc_caption_entry_t *current;
    uint32_t remaining_ticks;
    bool enabled;
} jc_captions_t;

size_t jc_caption_count(void);
const jc_caption_entry_t *jc_caption_at(size_t index);
const jc_caption_entry_t *jc_caption_lookup(const char *id);
const jc_caption_entry_t *jc_caption_for_ads(const char *ads_name,
                                             uint16_t ads_tag);

void jc_captions_init(jc_captions_t *captions);
void jc_captions_set_enabled(jc_captions_t *captions, bool enabled);
bool jc_captions_show(jc_captions_t *captions, const char *id,
                      uint32_t duration_ticks);
bool jc_captions_show_ads(jc_captions_t *captions, const char *ads_name,
                          uint16_t ads_tag, uint32_t duration_ticks);
void jc_captions_tick(jc_captions_t *captions);
void jc_captions_clear(jc_captions_t *captions);
const char *jc_captions_current_text(const jc_captions_t *captions);

#endif
