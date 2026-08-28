/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Caption text and ADS/tag mappings are translated from Hunter Davis's
 * johnny-castaway-ps1 data/ps1/captions.json and ps1_captions.c at public
 * commit 25c5d8459 (GPL-3.0-or-later). Its credits state that the text was
 * authored fresh for that port from scene content. The mapping was rebuilt
 * by a content-driven audit on 2026-04-26. Rendering and PS1 I/O code are not
 * copied; this is a portable immutable catalog plus frame-ticked state.
 */
#include "jc_captions.h"

#include <ctype.h>
#include <string.h>

#define CAPTION(id_, text_) { (id_), (text_) }

static const jc_caption_entry_t captions[] = {
    CAPTION("intro", "This is Johnny Castaway.\nStranded on a tiny island\n"
                     "with one palm tree.\nHe wears white shorts and a hat."),
    CAPTION("christmas", "It is Christmas.\nA small tree with red bulbs\n"
                         "and a golden star."),
    CAPTION("halloween", "It is Halloween.\nA carved jack-o-lantern\n"
                         "sits on the island."),
    CAPTION("newyears", "It is New Years.\nA banner reads Happy New Year\n"
                        "on the palm tree."),
    CAPTION("stpatrick", "It is St Patrick's Day.\nFour-leaf clovers grow\n"
                         "on the island."),
    CAPTION("night", "It is night.\nThe island is bathed\nin moonlight."),
    CAPTION("day", "It is day.\nThe sun shines brightly."),
    CAPTION("regularday", "It is a regular day."),
    CAPTION("hightide", "It is high tide.\nWaves lap at the island."),
    CAPTION("lowtide", "It is low tide.\nWaves lap at the island."),
    CAPTION("fadeout", "The scene fades to black."),
    CAPTION("walking", "Johnny walks around\nthe island."),
    CAPTION("scene00", "Johnny dives off the palm tree.\n"
                       "A perfect flip into the ocean.\n"
                       "Crabs and seagull hold up\nlow scorecards."),
    CAPTION("scene01", "Johnny dives off the palm tree.\n"
                       "It turns into a belly-flop.\n"
                       "Crabs and seagull hold up\nlow scorecards."),
    CAPTION("scene02", "Johnny reads under the tree.\n"
                       "A seagull lands on his head.\n"
                       "He swings a club but misses\nand hits himself."),
    CAPTION("scene03", "Johnny bathes in the ocean.\n"
                       "A seagull steals his clothes\nfor its nest.\n"
                       "Johnny shivers angrily."),
    CAPTION("scene04", "Johnny reads under the tree.\n"
                       "A seagull swoops down\nand steals his book."),
    CAPTION("scene05", "Johnny climbs the palm tree.\n"
                       "He looks around, then dives.\n"
                       "He walks back and looks around."),
    CAPTION("scene06", "Johnny fans himself in heat.\n"
                       "He does a rain dance in a mask.\n"
                       "A cloud appears but no rain.\n"
                       "Lightning strikes him to ash."),
    CAPTION("scene07", "Johnny reads under the tree.\nHe falls asleep.\n"
                       "A coconut bonks his head.\nHe wakes and keeps reading."),
    CAPTION("scene08", "Johnny reads under the tree.\n"
                       "He scratches his head confused.\n"
                       "The book was upside down.\nHe flips it and reads on."),
    CAPTION("scene09", "Johnny bathes in the ocean.\n"
                       "He scrubs, smells the brush\nin disgust, grabs his clothes\n"
                       "and walks behind the tree."),
    CAPTION("scene10", "Johnny wears a mask and skirt.\n"
                       "A yacht couple takes photos.\n"
                       "His grass skirt falls open.\nThe yacht sails away."),
    CAPTION("scene11", "Johnny builds a sand castle.\nIt crumbles.\n"
                       "He stomps it in frustration."),
    CAPTION("scene12", "Johnny sleeps under the tree.\n"
                       "Lilliputians row ashore\nand tie him down.\n"
                       "A seagull nests on him."),
    CAPTION("scene13", "Johnny sleeps under the tree.\nZs float as he snores.\n"
                       "He walks to the island edge."),
    CAPTION("scene14", "Johnny builds a sand castle.\n"
                       "Lilliputians claim it as\ntheir fortress.\n"
                       "Tiny planes attack Johnny."),
    CAPTION("scene15", "Johnny tries to build a fire.\n"
                       "He rubs sticks together.\nIt finally lights!\n"
                       "He warms his hands, it dies."),
    CAPTION("scene16", "Johnny relaxes by a fire.\n"
                       "He roasts an old boot.\nHe eats the boot whole."),
    CAPTION("scene17", "Johnny sleeps under the tree.\n"
                       "Lilliputians row ashore\nand tie him down.\n"
                       "He goes back to sleep."),
    CAPTION("scene18", "Johnny goes fishing.\nHe catches a starfish.\n"
                       "He throws it back."),
    CAPTION("scene19", "Johnny goes fishing.\nHe catches a boot.\n"
                       "He keeps the boot."),
    CAPTION("scene20", "Johnny goes fishing.\nHe catches five green fish.\n"
                       "Then an angry octopus.\nThe octopus chokes him."),
    CAPTION("scene21", "Johnny goes fishing.\nHe catches a shark.\n"
                       "The shark drags him around\nthe ocean like a jet-ski."),
    CAPTION("scene22", "Johnny goes fishing.\nA shark eats him.\n"
                       "The shark spits him back out."),
    CAPTION("scene23", "Johnny goes fishing.\nHe catches a big green fish.\n"
                       "It spits water in his face."),
    CAPTION("scene24", "Johnny goes fishing.\nHe catches a crab.\n"
                       "It snaps his nose."),
    CAPTION("scene25", "Johnny goes fishing.\nHe catches a boot.\n"
                       "He keeps the boot."),
    CAPTION("scene26", "A clock spins wildly.\nSunset silhouette. A plane.\n"
                       "Johnny parachutes down.\nThe End."),
    CAPTION("scene27", "A bottle washes ashore.\nJohnny writes an S.O.S.\n"
                       "He corks the bottle\nand throws it out to sea."),
    CAPTION("scene28", "Johnny writes a message.\n"
                       "He imagines a clock at 3pm.\nHe throws the bottle out\n"
                       "to prepare for his date."),
    CAPTION("scene29", "A bottle washes ashore.\n"
                       "Johnny picks it up excitedly.\nSadly, it is his own S.O.S.\n"
                       "He throws it back out."),
    CAPTION("scene30", "Johnny writes an S.O.S.\nHe corks the bottle\n"
                       "and throws it out to sea."),
    CAPTION("scene31", "A clock spins wildly.\nJohnny types at an office PC.\n"
                       "He dreams of the island\nand the mermaid. He looks sad."),
    CAPTION("scene32", "Johnny sets up a fancy dinner.\nA mermaid appears.\n"
                       "They eat, toast champagne,\nand dance. She swims away."),
    CAPTION("scene33", "A mermaid swims up.\nShe gives Johnny a necklace.\n"
                       "He gives her a life preserver.\nHe proposes a date."),
    CAPTION("scene34", "Johnny fishes at the edge.\n"
                       "A mermaid swims up behind him.\nHe thinks it is a fish."),
    CAPTION("scene35", "Johnny fixes his raft.\nThe mermaid asks what he does.\n"
                       "He says he is leaving.\nShe is heartbroken."),
    CAPTION("scene36", "Johnny packs his bags.\nThe mermaid and shark say bye.\n"
                       "The shark shakes his hand.\nJohnny paddles away."),
    CAPTION("scene37", "Johnny fans himself in heat.\n"
                       "He fans harder and harder.\nHe melts into a puddle."),
    CAPTION("scene38", "Johnny goes to swim.\nHe dips a toe in the ocean.\n"
                       "A shark snaps at him.\nHe scrambles back to shore."),
    CAPTION("scene39", "Johnny stands at the edge.\n"
                       "He taps his foot nervously."),
    CAPTION("scene40", "Johnny adjusts his pants."),
    CAPTION("scene41", "Johnny looks over the ocean.\n"
                       "He adjusts his hat and pants."),
    CAPTION("scene42", "Johnny taps his foot."),
    CAPTION("scene43", "Johnny lifts his hat\nand looks around."),
    CAPTION("scene44", "Johnny taps his foot.\nHe lifts his hat\n"
                       "and looks around."),
    CAPTION("scene45", "Johnny taps his foot.\nHe looks back into\n"
                       "the distance."),
    CAPTION("scene46", "Johnny lifts his hat."),
    CAPTION("scene47", "Johnny taps his foot.\nHe looks at the palm tree."),
    CAPTION("scene48", "Johnny looks at his raft."),
    CAPTION("scene49", "Johnny looks over the ocean."),
    CAPTION("scene50", "Johnny looks around\nunder the palm tree shade."),
    CAPTION("scene51", "Johnny pulls out a spyglass\nand scans the horizon."),
    CAPTION("scene52", "Johnny pulls out a spyglass\nand scans the horizon."),
    CAPTION("scene53", "A frog clock spins wildly.\nA redhead finds the bottle.\n"
                       "She imagines a volcano island\nand a handsome man."),
    CAPTION("scene54", "A frog clock spins wildly.\nJohnny's raft reaches her.\n"
                       "She kisses him passionately,\nthen scolds him."),
    CAPTION("scene55", "Johnny scans with a spyglass.\nA plane flies overhead.\n"
                       "He looks the wrong way\nand misses it entirely."),
    CAPTION("scene56", "A red boat spots Johnny.\nHe waves excitedly.\n"
                       "The boat is enormous,\nit fills the whole screen."),
    CAPTION("scene57", "Johnny shakes the palm tree.\n"
                       "A coconut bonks his head\nand flies into the ocean."),
    CAPTION("scene58", "Johnny shakes the palm tree.\nA coconut falls down.\n"
                       "He chases and catches it.\nHe cracks and eats it."),
    CAPTION("scene59", "Johnny shakes the palm tree.\nA coconut falls down.\n"
                       "He cracks it on the tree\nand eats it."),
    CAPTION("scene60", ""),
    CAPTION("scene61", "A boat with partygoers sails up.\n"
                       "Johnny swims to the boat.\nHe returns very drunk,\n"
                       "wearing a party hat."),
    CAPTION("scene62", "Johnny builds up his raft."),
    CAPTION("scene63", "Johnny jogs around the island\n"
                       "in a grey jogging outfit.\nHe changes back to normal."),
    CAPTION("buildingdone", "Johnny finishes building.\n"
                            "He stands at the island edge\n"
                            "and admires his work."),
    CAPTION("visitorboat", "A boat reaches the island.\nJohnny climbs aboard\n"
                           "and sails away."),
    CAPTION("fishingraft", "Johnny goes fishing.\nHe catches a life raft.\n"
                           "He drags it ashore.")
};

#define ADS(caption_, ads_, tag_) { (caption_), (ads_), (tag_) }

static const jc_caption_ads_map_t ads_map[] = {
    ADS("scene00", "ACTIVITY", 1), ADS("scene01", "ACTIVITY", 12),
    ADS("scene06", "ACTIVITY", 11), ADS("scene04", "ACTIVITY", 10),
    ADS("scene02", "ACTIVITY", 4), ADS("scene05", "ACTIVITY", 5),
    ADS("scene07", "ACTIVITY", 6), ADS("scene03", "ACTIVITY", 7),
    ADS("scene08", "ACTIVITY", 8), ADS("scene09", "ACTIVITY", 9),
    ADS("scene11", "BUILDING", 1), ADS("scene14", "BUILDING", 4),
    ADS("scene15", "BUILDING", 3), ADS("scene16", "BUILDING", 2),
    ADS("scene35", "BUILDING", 5), ADS("scene62", "BUILDING", 7),
    ADS("buildingdone", "BUILDING", 6),
    ADS("scene18", "FISHING", 1), ADS("fishingraft", "FISHING", 2),
    ADS("scene20", "FISHING", 3), ADS("scene21", "FISHING", 4),
    ADS("scene22", "FISHING", 5), ADS("scene23", "FISHING", 6),
    ADS("scene24", "FISHING", 7), ADS("scene19", "FISHING", 8),
    ADS("scene26", "JOHNNY", 1), ADS("scene27", "JOHNNY", 2),
    ADS("scene29", "JOHNNY", 3), ADS("scene30", "JOHNNY", 4),
    ADS("scene28", "JOHNNY", 5), ADS("scene31", "JOHNNY", 6),
    ADS("scene32", "MARY", 1), ADS("scene34", "MARY", 3),
    ADS("scene33", "MARY", 2), ADS("scene35", "MARY", 4),
    ADS("scene36", "MARY", 5),
    ADS("scene37", "MISCGAG", 1), ADS("scene38", "MISCGAG", 2),
    ADS("scene39", "STAND", 1), ADS("scene40", "STAND", 2),
    ADS("scene46", "STAND", 3), ADS("scene42", "STAND", 4),
    ADS("scene41", "STAND", 5), ADS("scene47", "STAND", 6),
    ADS("scene43", "STAND", 7), ADS("scene44", "STAND", 8),
    ADS("scene45", "STAND", 9), ADS("scene48", "STAND", 10),
    ADS("scene50", "STAND", 11), ADS("scene49", "STAND", 12),
    ADS("scene51", "STAND", 15), ADS("scene52", "STAND", 16),
    ADS("scene53", "SUZY", 1), ADS("scene54", "SUZY", 2),
    ADS("scene12", "VISITOR", 1), ADS("scene10", "VISITOR", 3),
    ADS("scene56", "VISITOR", 4), ADS("visitorboat", "VISITOR", 5),
    ADS("scene61", "VISITOR", 6), ADS("scene55", "VISITOR", 7),
    ADS("walking", "WALKSTUF", 1), ADS("walking", "WALKSTUF", 2),
    ADS("walking", "WALKSTUF", 3)
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

size_t jc_caption_count(void)
{
    return sizeof(captions) / sizeof(captions[0]);
}

const jc_caption_entry_t *jc_caption_at(size_t index)
{
    return index < jc_caption_count() ? &captions[index] : NULL;
}

const jc_caption_entry_t *jc_caption_lookup(const char *id)
{
    size_t index;
    if (id == NULL)
        return NULL;
    for (index = 0u; index < jc_caption_count(); ++index) {
        if (strcmp(captions[index].id, id) == 0)
            return &captions[index];
    }
    return NULL;
}

const jc_caption_entry_t *jc_caption_for_ads(const char *ads_name,
                                             uint16_t ads_tag)
{
    size_t index;
    for (index = 0u; index < sizeof(ads_map) / sizeof(ads_map[0]); ++index) {
        if (ads_map[index].ads_tag == ads_tag &&
            ads_name_equal(ads_name, ads_map[index].ads_name))
            return jc_caption_lookup(ads_map[index].caption_id);
    }
    return NULL;
}

void jc_captions_init(jc_captions_t *state)
{
    if (state == NULL)
        return;
    memset(state, 0, sizeof(*state));
}

void jc_captions_set_enabled(jc_captions_t *state, bool enabled)
{
    if (state == NULL)
        return;
    state->enabled = enabled;
    if (!enabled)
        jc_captions_clear(state);
}

bool jc_captions_show(jc_captions_t *state, const char *id,
                      uint32_t duration_ticks)
{
    const jc_caption_entry_t *entry;
    if (state == NULL || !state->enabled)
        return false;
    entry = jc_caption_lookup(id);
    if (entry == NULL)
        return false;
    state->current = entry;
    state->remaining_ticks = duration_ticks != 0u ? duration_ticks :
        JC_CAPTION_DEFAULT_TICKS;
    return true;
}

bool jc_captions_show_ads(jc_captions_t *state, const char *ads_name,
                          uint16_t ads_tag, uint32_t duration_ticks)
{
    const jc_caption_entry_t *entry;
    if (state == NULL || !state->enabled)
        return false;
    entry = jc_caption_for_ads(ads_name, ads_tag);
    return entry != NULL && jc_captions_show(state, entry->id, duration_ticks);
}

void jc_captions_tick(jc_captions_t *state)
{
    if (state == NULL || state->current == NULL)
        return;
    if (state->remaining_ticks > 0u)
        --state->remaining_ticks;
    if (state->remaining_ticks == 0u)
        state->current = NULL;
}

void jc_captions_clear(jc_captions_t *state)
{
    if (state == NULL)
        return;
    state->current = NULL;
    state->remaining_ticks = 0u;
}

const char *jc_captions_current_text(const jc_captions_t *state)
{
    if (state == NULL || !state->enabled || state->current == NULL ||
        state->remaining_ticks == 0u)
        return NULL;
    return state->current->text;
}

