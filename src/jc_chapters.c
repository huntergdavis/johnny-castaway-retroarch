/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Scene Explorer metadata is translated from Hunter Davis's
 * johnny-castaway-ps1 src/pause_menu/scene_explorer_data.h, SCEXPL.DAT and
 * config/ps1/cd_layout.xml at public commit 25c5d8459 (GPL-3.0-or-later).
 * The original ADS/tag and caption IDs are cross-checked against the PS1
 * foreground family table and the audited caption mapping. The PS1 SCR names
 * identify the actual 320x240 RGB555 menu thumbnails; this core need not ship
 * them and can render its own preview from the ADS/tag pair.
 */
#include "jc_chapters.h"

#include <ctype.h>
#include <string.h>

#define CHAPTER(slug_, title_, ads_, caption_, preview_, tag_, frames_, family_) \
    { (slug_), (title_), (ads_), (caption_), (preview_), (tag_), (frames_),     \
      (family_), true }

static const char *const family_names[] = {
    "FISHING", "JOHNNY", "MARY", "VISITOR", "ACTIVITY",
    "OTHER", "STAND", "WALKSTUF", "BUILDING"
};

static const jc_chapter_t chapters[] = {
    CHAPTER("fishing1", "FISHING 1 - Catches a starfish, throws it back",
            "FISHING", "scene18", "SXFI1.SCR", 1, 156, 0),
    CHAPTER("fishing2", "FISHING 2 - Hooks a Titanic life preserver",
            "FISHING", "fishingraft", "SXFI2.SCR", 2, 295, 0),
    CHAPTER("fishing3", "FISHING 3 - Octopus steals the fish and walks off",
            "FISHING", "scene20", "SXFI3.SCR", 3, 329, 0),
    CHAPTER("fishing4", "FISHING 4 - Hooks a shark, gets pulled around like a speedboat",
            "FISHING", "scene21", "SXFI4.SCR", 4, 133, 0),
    CHAPTER("fishing5", "FISHING 5 - Eaten by a shark, then spat back out",
            "FISHING", "scene22", "SXFI5.SCR", 5, 69, 0),
    CHAPTER("fishing6", "FISHING 6 - Fish spits water in his face, thrown back",
            "FISHING", "scene23", "SXFI6.SCR", 6, 112, 0),
    CHAPTER("fishing7", "FISHING 7 - Catches a starfish (right-side variant), throws it back",
            "FISHING", "scene24", "SXFI7.SCR", 7, 80, 0),
    CHAPTER("fishing8", "FISHING 8 - Catches a fish (right-side variant)",
            "FISHING", "scene19", "SXFI8.SCR", 8, 133, 0),
    CHAPTER("johnny1", "JOHNNY 1 - The End",
            "JOHNNY", "scene26", "SXJO1.SCR", 1, 112, 1),
    CHAPTER("johnny2", "JOHNNY 2 - Bottle washes up; Johnny puts an SOS note inside",
            "JOHNNY", "scene27", "SXJO2.SCR", 2, 146, 1),
    CHAPTER("johnny3", "JOHNNY 3 - Writes and sends a letter to Suzy",
            "JOHNNY", "scene29", "SXJO3.SCR", 3, 104, 1),
    CHAPTER("johnny4", "JOHNNY 4 - His own SOS bottle washes back",
            "JOHNNY", "scene30", "SXJO4.SCR", 4, 94, 1),
    CHAPTER("johnny5", "JOHNNY 5 - Sends an SOS bottle",
            "JOHNNY", "scene28", "SXJO5.SCR", 5, 91, 1),
    CHAPTER("johnny6", "JOHNNY 6 - At his office desk, daydreaming of the island",
            "JOHNNY", "scene31", "SXJO6.SCR", 6, 82, 1),
    CHAPTER("mary1", "MARY 1 - Date with Mary the mermaid",
            "MARY", "scene32", "SXMA1.SCR", 1, 821, 2),
    CHAPTER("mary2", "MARY 2 - Mary visits while Johnny fishes; he mistakes her for a fish, catches a boot",
            "MARY", "scene33", "SXMA2.SCR", 2, 234, 2),
    CHAPTER("mary3", "MARY 3 - Mary and Johnny exchange gifts and plan a date",
            "MARY", "scene34", "SXMA3.SCR", 3, 256, 2),
    CHAPTER("mary4", "MARY 4 - Mary's feelings hurt as Johnny works on the raft",
            "MARY", "scene35", "SXMA4.SCR", 4, 154, 2),
    CHAPTER("mary5", "MARY 5 - Packs the raft and says goodbye",
            "MARY", "scene36", "SXMA5.SCR", 5, 150, 2),
    CHAPTER("visitor1", "VISITOR 1 - Misses a speedboat passing by",
            "VISITOR", "scene12", "SXVI1.SCR", 1, 67, 3),
    CHAPTER("visitor3", "VISITOR 3 - Waves down what looks like a small boat, but it's huge",
            "VISITOR", "scene10", "SXVI3.SCR", 3, 145, 3),
    CHAPTER("visitor4", "VISITOR 4 - Shakes the palm; coconut rolls into the ocean",
            "VISITOR", "scene56", "SXVI4.SCR", 4, 55, 3),
    CHAPTER("visitor5", "VISITOR 5 - Throws a coconut at a plane; it crashes",
            "VISITOR", "visitorboat", "SXVI5.SCR", 5, 180, 3),
    CHAPTER("visitor6", "VISITOR 6 - Shakes tree, cracks coconut on it, eats",
            "VISITOR", "scene61", "SXVI6.SCR", 6, 121, 3),
    CHAPTER("visitor7", "VISITOR 7 - Cracks a coconut on the tree, eats it (no-shake variant)",
            "VISITOR", "scene55", "SXVI7.SCR", 7, 91, 3),
    CHAPTER("activity1", "ACTIVITY 01 - Climbs the palm and belly-flops",
            "ACTIVITY", "scene00", "SXAC1.SCR", 1, 194, 4),
    CHAPTER("activity4", "ACTIVITY 04 - Climbs the palm and dives in",
            "ACTIVITY", "scene02", "SXAC4.SCR", 4, 190, 4),
    CHAPTER("activity5", "ACTIVITY 05 - Rain dance, struck by lightning",
            "ACTIVITY", "scene05", "SXAC5.SCR", 5, 169, 4),
    CHAPTER("activity6", "ACTIVITY 06 - Reads, falls asleep, coconut bonk",
            "ACTIVITY", "scene07", "SXAC6.SCR", 6, 105, 4),
    CHAPTER("activity7", "ACTIVITY 07 - Reads a book upside-down",
            "ACTIVITY", "scene03", "SXAC7.SCR", 7, 57, 4),
    CHAPTER("activity8", "ACTIVITY 08 - Bathes, then walks behind tree to dress",
            "ACTIVITY", "scene08", "SXAC8.SCR", 8, 87, 4),
    CHAPTER("activity9", "ACTIVITY 09 - Rain dance, boat with couple passes, costume drops, naked",
            "ACTIVITY", "scene09", "SXAC9.SCR", 9, 255, 4),
    CHAPTER("activity10", "ACTIVITY 10 - Reads; seagull steals book",
            "ACTIVITY", "scene04", "SXAC10.SCR", 10, 164, 4),
    CHAPTER("activity11", "ACTIVITY 11 - Bird steals Johnny's clothes",
            "ACTIVITY", "scene06", "SXAC11.SCR", 11, 208, 4),
    CHAPTER("activity12", "ACTIVITY 12 - Bird on head, Johnny clubs himself",
            "ACTIVITY", "scene01", "SXAC12.SCR", 12, 252, 4),
    CHAPTER("miscgag1", "MISCGAG 1 - Heat melts Johnny",
            "MISCGAG", "scene37", "SXMG1.SCR", 1, 69, 5),
    CHAPTER("miscgag2", "MISCGAG 2 - Goes to bathe; a shark scares him off",
            "MISCGAG", "scene38", "SXMG2.SCR", 2, 84, 5),
    CHAPTER("suzy1", "SUZY 1 - Suzy finds a letter from Johnny",
            "SUZY", "scene53", "SXSU1.SCR", 1, 177, 5),
    CHAPTER("suzy2", "SUZY 2 - Johnny drifts in on his raft and meets Suzy",
            "SUZY", "scene54", "SXSU2.SCR", 2, 133, 5),
    CHAPTER("stand1", "STAND 01 - Standing at the edge of the island",
            "STAND", "scene39", "SXST1.SCR", 1, 17, 6),
    CHAPTER("stand2", "STAND 02 - Standing, adjusting pants",
            "STAND", "scene40", "SXST2.SCR", 2, 78, 6),
    CHAPTER("stand3", "STAND 03 - Standing at edge of island, adjusts hat",
            "STAND", "scene46", "SXST3.SCR", 3, 47, 6),
    CHAPTER("stand4", "STAND 04 - Standing at front of island, adjusts hat",
            "STAND", "scene42", "SXST4.SCR", 4, 106, 6),
    CHAPTER("stand5", "STAND 05 - Standing at front of island, looking out over the ocean",
            "STAND", "scene41", "SXST5.SCR", 5, 130, 6),
    CHAPTER("stand6", "STAND 06 - Looks out at the ocean, scratches head",
            "STAND", "scene47", "SXST6.SCR", 6, 118, 6),
    CHAPTER("stand7", "STAND 07 - Looks right, lifts hat",
            "STAND", "scene43", "SXST7.SCR", 7, 47, 6),
    CHAPTER("stand8", "STAND 08 - Right side of island, looks around, scratches head",
            "STAND", "scene44", "SXST8.SCR", 8, 43, 6),
    CHAPTER("stand9", "STAND 09 - By the palm, looks around, adjusts pants",
            "STAND", "scene45", "SXST9.SCR", 9, 46, 6),
    CHAPTER("stand10", "STAND 10 - Looks at his raft, looks around",
            "STAND", "scene48", "SXST10.SCR", 10, 46, 6),
    CHAPTER("stand11", "STAND 11 - Left side of island, looks around",
            "STAND", "scene50", "SXST11.SCR", 11, 87, 6),
    CHAPTER("stand12", "STAND 12 - Looks forward, adjusts hat",
            "STAND", "scene49", "SXST12.SCR", 12, 238, 6),
    CHAPTER("stand15", "STAND 15 - Looks around with a spyglass",
            "STAND", "scene51", "SXST15.SCR", 15, 45, 6),
    CHAPTER("stand16", "STAND 16 - Spyglass, right side of island",
            "STAND", "scene52", "SXST16.SCR", 16, 36, 6),
    CHAPTER("walkstuf1", "WALKSTUF 1 - Parties on a yacht, comes back drunk, passes out",
            "WALKSTUF", "walking", "SXWK1.SCR", 1, 216, 7),
    CHAPTER("walkstuf2", "WALKSTUF 2 - Works on the raft",
            "WALKSTUF", "walking", "SXWK2.SCR", 2, 57, 7),
    CHAPTER("walkstuf3", "WALKSTUF 3 - Jogs around the island",
            "WALKSTUF", "walking", "SXWK3.SCR", 3, 439, 7),
    CHAPTER("building1", "BUILDING 1 - Sandcastle slumps, Johnny stomps it",
            "BUILDING", "scene11", "SXBL1.SCR", 1, 106, 8),
    CHAPTER("building2", "BUILDING 2 - Lilliputians take over the sandcastle and launch airplanes",
            "BUILDING", "scene16", "SXBL2.SCR", 2, 334, 8),
    CHAPTER("building3", "BUILDING 3 - Rolls over and takes a nap",
            "BUILDING", "scene15", "SXBL3.SCR", 3, 360, 8),
    CHAPTER("building4", "BUILDING 4 - Lilliputians tie Johnny down while he sleeps",
            "BUILDING", "scene14", "SXBL4.SCR", 4, 428, 8),
    CHAPTER("building5", "BUILDING 5 - Builds a fire and sits by it",
            "BUILDING", "scene35", "SXBL5.SCR", 5, 474, 8),
    CHAPTER("building6", "BUILDING 6 - Lilliputians tie Johnny up (no-bird variant)",
            "BUILDING", "buildingdone", "SXBL6.SCR", 6, 307, 8),
    CHAPTER("building7", "BUILDING 7 - Builds a fire, grills a fish, eats it",
            "BUILDING", "scene62", "SXBL7.SCR", 7, 460, 8)
};

static bool ads_name_equal(const char *left, const char *right)
{
    size_t index = 0u;
    if (left == NULL || right == NULL)
        return false;
    while (left[index] != '\0' && left[index] != '.' && right[index] != '\0') {
        if (toupper((unsigned char)left[index]) !=
            toupper((unsigned char)right[index]))
            return false;
        ++index;
    }
    return right[index] == '\0' &&
           (left[index] == '\0' || strcmp(left + index, ".ADS") == 0 ||
            strcmp(left + index, ".ads") == 0);
}

size_t jc_chapter_count(void)
{
    return sizeof(chapters) / sizeof(chapters[0]);
}

const jc_chapter_t *jc_chapter_at(size_t index)
{
    return index < jc_chapter_count() ? &chapters[index] : NULL;
}

const jc_chapter_t *jc_chapter_lookup(const char *slug)
{
    size_t index;
    if (slug == NULL)
        return NULL;
    for (index = 0u; index < jc_chapter_count(); ++index) {
        if (strcmp(chapters[index].slug, slug) == 0)
            return &chapters[index];
    }
    return NULL;
}

const jc_chapter_t *jc_chapter_for_ads(const char *ads_name,
                                       uint16_t ads_tag)
{
    size_t index;
    for (index = 0u; index < jc_chapter_count(); ++index) {
        if (chapters[index].ads_tag == ads_tag &&
            ads_name_equal(ads_name, chapters[index].ads_name))
            return &chapters[index];
    }
    return NULL;
}

const char *jc_chapter_family_name(uint8_t family_index)
{
    return family_index < sizeof(family_names) / sizeof(family_names[0]) ?
        family_names[family_index] : NULL;
}
