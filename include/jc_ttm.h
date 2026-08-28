/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef JC_TTM_H
#define JC_TTM_H

#include "jc_script_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JC_TTM_MAX_TAGS 256u
#define JC_TTM_NAME_BYTES 40u
#define JC_TTM_MAX_ARGS 14u
#define JC_TTM_MAX_STRING_BYTES 255u

typedef struct jc_ttm_tag {
    uint16_t id;
    size_t offset;
    char description[JC_TTM_NAME_BYTES + 1u];
} jc_ttm_tag_t;

typedef struct jc_ttm {
    char version[6];
    uint32_t page_count;
    jc_ttm_tag_t tags[JC_TTM_MAX_TAGS];
    size_t tag_count;
    jc_ttm_tag_t labels[JC_TTM_MAX_TAGS];
    size_t label_count;
    uint8_t *bytecode;
    size_t bytecode_size;
} jc_ttm_t;

typedef struct jc_ttm_instruction {
    uint16_t opcode;
    uint16_t args[JC_TTM_MAX_ARGS];
    uint8_t arg_count;
    const char *string;
    size_t string_length;
    size_t offset;
    size_t next_offset;
} jc_ttm_instruction_t;

bool jc_ttm_parse(jc_ttm_t *ttm, const uint8_t *data, size_t size,
                  uint8_t *bytecode_storage, size_t bytecode_capacity,
                  jc_script_error_t *error);
bool jc_ttm_instruction_at(const jc_ttm_t *ttm, size_t offset,
                           jc_ttm_instruction_t *instruction,
                           jc_script_error_t *error);
bool jc_ttm_find_tag(const jc_ttm_t *ttm, uint16_t id, size_t *offset);
bool jc_ttm_find_previous_tag(const jc_ttm_t *ttm, size_t before,
                              size_t *offset);
const char *jc_ttm_opcode_name(uint16_t opcode);

#endif
