/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef JC_ADS_H
#define JC_ADS_H

#include "jc_script_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JC_ADS_MAX_RESOURCES 32u
#define JC_ADS_MAX_TAGS 128u
#define JC_ADS_NAME_BYTES 40u
#define JC_ADS_MAX_ARGS 5u

typedef struct jc_ads_resource {
    uint16_t id;
    char name[JC_ADS_NAME_BYTES + 1u];
} jc_ads_resource_t;

typedef struct jc_ads_tag {
    uint16_t id;
    size_t offset;
    char description[JC_ADS_NAME_BYTES + 1u];
} jc_ads_tag_t;

typedef struct jc_ads {
    char version[6];
    jc_ads_resource_t resources[JC_ADS_MAX_RESOURCES];
    size_t resource_count;
    jc_ads_tag_t tags[JC_ADS_MAX_TAGS];
    size_t tag_count;
    jc_ads_tag_t labels[JC_ADS_MAX_TAGS];
    size_t label_count;
    uint8_t *bytecode;
    size_t bytecode_size;
} jc_ads_t;

typedef struct jc_ads_instruction {
    uint16_t opcode;
    uint16_t args[JC_ADS_MAX_ARGS];
    uint8_t arg_count;
    bool is_tag;
    size_t offset;
    size_t next_offset;
} jc_ads_instruction_t;

bool jc_ads_parse(jc_ads_t *ads, const uint8_t *data, size_t size,
                  uint8_t *bytecode_storage, size_t bytecode_capacity,
                  jc_script_error_t *error);
bool jc_ads_instruction_at(const jc_ads_t *ads, size_t offset,
                           jc_ads_instruction_t *instruction,
                           jc_script_error_t *error);
bool jc_ads_find_tag(const jc_ads_t *ads, uint16_t id, size_t *offset);
const char *jc_ads_opcode_name(uint16_t opcode);

#endif
