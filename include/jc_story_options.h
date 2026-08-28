/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef JC_STORY_OPTIONS_H
#define JC_STORY_OPTIONS_H

#include "jc_director.h"

#include <stdint.h>

static inline uint8_t jc_story_effective_raft_stage(uint8_t planned_stage,
                                                    int forced_stage,
                                                    uint8_t final_scene_flags)
{
    if ((final_scene_flags & JC_SCENE_NO_RAFT) != 0u)
        return 0u;
    if (forced_stage >= 0 && forced_stage <= 5)
        return (uint8_t)forced_stage;
    return planned_stage;
}

#endif
