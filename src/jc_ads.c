/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Bounded C rewrite of the ADS format/opcode behavior documented by Wilson
 * Reborn (crates/wilson-dgds/src/ads.rs) and Johnny Reborn (ads.c/resource.c).
 * No upstream source text is copied; the references are GPL-3.0-or-later.
 */
#include "jc_ads.h"

#include "jc_decompress.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define JC_ADS_RUNTIME_SLOT_LIMIT 10u

typedef struct jc_ads_cursor {
    const uint8_t *data;
    size_t size;
    size_t position;
    jc_script_error_t *error;
} jc_ads_cursor_t;

static void clear_error(jc_script_error_t *error)
{
    if (error != NULL)
        memset(error, 0, sizeof(*error));
}

static bool set_error(jc_script_error_t *error, jc_script_error_code_t code,
                      size_t offset, uint16_t opcode, const char *format, ...)
{
    va_list args;

    if (error == NULL)
        return false;
    memset(error, 0, sizeof(*error));
    error->code = code;
    error->domain = JC_SCRIPT_DOMAIN_ADS_PARSE;
    error->offset = offset;
    error->opcode = opcode;
    va_start(args, format);
    vsnprintf(error->message, sizeof(error->message), format, args);
    va_end(args);
    return false;
}

static bool take(jc_ads_cursor_t *cursor, size_t count, const uint8_t **bytes)
{
    if (count > cursor->size - cursor->position)
        return set_error(cursor->error, JC_SCRIPT_ERROR_TRUNCATED,
                         cursor->position, 0u,
                         "ADS is truncated: need %lu bytes, have %lu",
                         (unsigned long)count,
                         (unsigned long)(cursor->size - cursor->position));
    *bytes = cursor->data + cursor->position;
    cursor->position += count;
    return true;
}

static bool read_u8(jc_ads_cursor_t *cursor, uint8_t *value)
{
    const uint8_t *bytes = NULL;
    if (!take(cursor, 1u, &bytes))
        return false;
    *value = bytes[0];
    return true;
}

static bool read_u16(jc_ads_cursor_t *cursor, uint16_t *value)
{
    const uint8_t *bytes = NULL;
    if (!take(cursor, 2u, &bytes))
        return false;
    *value = (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
    return true;
}

static bool read_u32(jc_ads_cursor_t *cursor, uint32_t *value)
{
    const uint8_t *bytes = NULL;
    if (!take(cursor, 4u, &bytes))
        return false;
    *value = (uint32_t)bytes[0] |
             ((uint32_t)bytes[1] << 8) |
             ((uint32_t)bytes[2] << 16) |
             ((uint32_t)bytes[3] << 24);
    return true;
}

static bool expect_tag(jc_ads_cursor_t *cursor, const char expected[4])
{
    const uint8_t *bytes = NULL;
    size_t offset = cursor->position;

    if (!take(cursor, 4u, &bytes))
        return false;
    if (memcmp(bytes, expected, 4u) != 0)
        return set_error(cursor->error, JC_SCRIPT_ERROR_BAD_TAG, offset, 0u,
                         "ADS expected chunk %.4s, found %02X%02X%02X%02X",
                         expected, bytes[0], bytes[1], bytes[2], bytes[3]);
    return true;
}

static bool read_cstring(jc_ads_cursor_t *cursor, char *output,
                         size_t output_size, const char *field)
{
    size_t index;

    for (index = 0u; index < JC_ADS_NAME_BYTES; ++index) {
        uint8_t byte;
        if (!read_u8(cursor, &byte))
            return false;
        if (byte == 0u) {
            output[index] = '\0';
            return true;
        }
        if (index + 1u >= output_size)
            return set_error(cursor->error, JC_SCRIPT_ERROR_LIMIT,
                             cursor->position - 1u, 0u,
                             "ADS %s exceeds destination storage", field);
        if (byte < 0x20u || byte > 0x7eu)
            return set_error(cursor->error, JC_SCRIPT_ERROR_BAD_OPERAND,
                             cursor->position - 1u, 0u,
                             "ADS %s contains a non-printable byte", field);
        output[index] = (char)byte;
    }
    return set_error(cursor->error, JC_SCRIPT_ERROR_BAD_SIZE,
                     cursor->position, 0u,
                     "ADS %s is not NUL-terminated within %u bytes",
                     field, (unsigned)JC_ADS_NAME_BYTES);
}

static bool opcode_arg_count(uint16_t opcode, uint8_t *arg_count)
{
    switch (opcode) {
    case 0x1070u: *arg_count = 2u; return true;
    case 0x1330u: *arg_count = 2u; return true;
    case 0x1350u: *arg_count = 2u; return true;
    case 0x1360u: *arg_count = 2u; return true;
    case 0x1370u: *arg_count = 2u; return true;
    case 0x1420u: *arg_count = 0u; return true;
    case 0x1430u: *arg_count = 0u; return true;
    case 0x1510u: *arg_count = 0u; return true;
    case 0x1520u: *arg_count = 5u; return true;
    case 0x2005u: *arg_count = 4u; return true;
    case 0x2010u: *arg_count = 3u; return true;
    case 0x2014u: *arg_count = 0u; return true;
    case 0x3010u: *arg_count = 0u; return true;
    case 0x3020u: *arg_count = 1u; return true;
    case 0x30ffu: *arg_count = 0u; return true;
    case 0x4000u: *arg_count = 3u; return true;
    case 0xf010u: *arg_count = 0u; return true;
    case 0xf200u: *arg_count = 1u; return true;
    case 0xfff0u: *arg_count = 0u; return true;
    case 0xffffu: *arg_count = 0u; return true;
    default: return false;
    }
}

const char *jc_ads_opcode_name(uint16_t opcode)
{
    switch (opcode) {
    case 0x1070u: return "IF_LASTPLAYED_LOCAL";
    case 0x1330u: return "IF_UNKNOWN_1";
    case 0x1350u: return "IF_LASTPLAYED";
    case 0x1360u: return "IF_NOT_RUNNING";
    case 0x1370u: return "IF_IS_RUNNING";
    case 0x1420u: return "AND";
    case 0x1430u: return "OR";
    case 0x1510u: return "PLAY_SCENE";
    case 0x1520u: return "ADD_SCENE_LOCAL";
    case 0x2005u: return "ADD_SCENE";
    case 0x2010u: return "STOP_SCENE";
    case 0x2014u: return "UNKNOWN_5";
    case 0x3010u: return "RANDOM_START";
    case 0x3020u: return "NOP";
    case 0x30ffu: return "RANDOM_END";
    case 0x4000u: return "UNKNOWN_6";
    case 0xf010u: return "FADE_OUT";
    case 0xf200u: return "GOSUB_TAG";
    case 0xfff0u: return "END_IF";
    case 0xffffu: return "END";
    default: return NULL;
    }
}

bool jc_ads_instruction_at(const jc_ads_t *ads, size_t offset,
                           jc_ads_instruction_t *instruction,
                           jc_script_error_t *error)
{
    uint8_t arg_count;
    size_t index;
    size_t position;

    clear_error(error);
    if (ads == NULL || instruction == NULL || ads->bytecode == NULL)
        return set_error(error, JC_SCRIPT_ERROR_NULL_ARGUMENT, offset, 0u,
                         "ADS instruction decoder received a null argument");
    if (offset > ads->bytecode_size || ads->bytecode_size - offset < 2u)
        return set_error(error, JC_SCRIPT_ERROR_TRUNCATED, offset, 0u,
                         "ADS instruction is truncated before its opcode");

    memset(instruction, 0, sizeof(*instruction));
    instruction->offset = offset;
    instruction->opcode = (uint16_t)((uint16_t)ads->bytecode[offset] |
                                     ((uint16_t)ads->bytecode[offset + 1u] << 8));
    position = offset + 2u;
    if (!opcode_arg_count(instruction->opcode, &arg_count)) {
        if ((instruction->opcode & 0xff00u) != 0u)
            return set_error(error, JC_SCRIPT_ERROR_UNKNOWN_OPCODE, offset,
                             instruction->opcode,
                             "unknown ADS opcode 0x%04X",
                             (unsigned)instruction->opcode);
        instruction->is_tag = true;
        instruction->next_offset = position;
        return true;
    }

    if ((size_t)arg_count * 2u > ads->bytecode_size - position)
        return set_error(error, JC_SCRIPT_ERROR_TRUNCATED, offset,
                         instruction->opcode,
                         "ADS opcode 0x%04X is truncated in its operands",
                         (unsigned)instruction->opcode);
    instruction->arg_count = arg_count;
    for (index = 0u; index < arg_count; ++index) {
        instruction->args[index] =
            (uint16_t)((uint16_t)ads->bytecode[position] |
                       ((uint16_t)ads->bytecode[position + 1u] << 8));
        position += 2u;
    }
    instruction->next_offset = position;
    return true;
}

bool jc_ads_find_tag(const jc_ads_t *ads, uint16_t id, size_t *offset)
{
    size_t index;

    if (ads == NULL || offset == NULL)
        return false;
    for (index = 0u; index < ads->label_count; ++index) {
        if (ads->labels[index].id == id) {
            *offset = ads->labels[index].offset;
            return true;
        }
    }
    return false;
}

static bool validate_bytecode(jc_ads_t *ads, jc_script_error_t *error)
{
    size_t offset = 0u;

    while (offset < ads->bytecode_size) {
        jc_ads_instruction_t instruction;
        if (!jc_ads_instruction_at(ads, offset, &instruction, error))
            return false;
        if (instruction.is_tag) {
            jc_ads_tag_t *label;
            if (ads->label_count >= JC_ADS_MAX_TAGS)
                return set_error(error, JC_SCRIPT_ERROR_LIMIT, offset,
                                 instruction.opcode,
                                 "ADS bytecode has more than %u labels",
                                 (unsigned)JC_ADS_MAX_TAGS);
            label = &ads->labels[ads->label_count++];
            memset(label, 0, sizeof(*label));
            label->id = instruction.opcode;
            label->offset = instruction.next_offset;
        }
        offset = instruction.next_offset;
    }
    return true;
}

bool jc_ads_parse(jc_ads_t *ads, const uint8_t *data, size_t size,
                  uint8_t *bytecode_storage, size_t bytecode_capacity,
                  jc_script_error_t *error)
{
    jc_ads_cursor_t cursor;
    const uint8_t *bytes = NULL;
    uint32_t value32;
    uint16_t count16;
    size_t index;
    size_t packed_offset;
    uint8_t method;
    size_t body_size;
    char decompression_error[JC_SCRIPT_ERROR_MESSAGE_BYTES];

    clear_error(error);
    if (ads == NULL || data == NULL || bytecode_storage == NULL)
        return set_error(error, JC_SCRIPT_ERROR_NULL_ARGUMENT, 0u, 0u,
                         "ADS parser received a null argument");
    memset(ads, 0, sizeof(*ads));
    cursor.data = data;
    cursor.size = size;
    cursor.position = 0u;
    cursor.error = error;

    if (!expect_tag(&cursor, "VER:") || !read_u32(&cursor, &value32))
        return false;
    if (value32 != 5u)
        return set_error(error, JC_SCRIPT_ERROR_BAD_SIZE, 4u, 0u,
                         "ADS version field size is %lu, expected 5",
                         (unsigned long)value32);
    if (!take(&cursor, 5u, &bytes))
        return false;
    if (bytes[4] != 0u)
        return set_error(error, JC_SCRIPT_ERROR_BAD_SIZE, 8u, 0u,
                         "ADS version string is not NUL-terminated");
    memcpy(ads->version, bytes, 5u);
    ads->version[5] = '\0';

    if (!expect_tag(&cursor, "ADS:") || !take(&cursor, 4u, &bytes) ||
        !expect_tag(&cursor, "RES:") || !read_u32(&cursor, &value32) ||
        !read_u16(&cursor, &count16))
        return false;
    if (value32 > cursor.size - cursor.position)
        return set_error(error, JC_SCRIPT_ERROR_BAD_SIZE,
                         cursor.position - 6u, 0u,
                         "ADS RES declared size exceeds remaining input");
    if (count16 > JC_ADS_MAX_RESOURCES)
        return set_error(error, JC_SCRIPT_ERROR_LIMIT,
                         cursor.position - 2u, 0u,
                         "ADS has %u resources; limit is %u", (unsigned)count16,
                         (unsigned)JC_ADS_MAX_RESOURCES);
    ads->resource_count = count16;
    for (index = 0u; index < ads->resource_count; ++index) {
        if (!read_u16(&cursor, &ads->resources[index].id) ||
            !read_cstring(&cursor, ads->resources[index].name,
                          sizeof(ads->resources[index].name), "resource name"))
            return false;
        if (ads->resources[index].id >= JC_ADS_RUNTIME_SLOT_LIMIT)
            return set_error(error, JC_SCRIPT_ERROR_BAD_OPERAND,
                             cursor.position, 0u,
                             "ADS resource slot %u exceeds runtime slot limit %u",
                             (unsigned)ads->resources[index].id,
                             (unsigned)JC_ADS_RUNTIME_SLOT_LIMIT);
    }

    if (!expect_tag(&cursor, "SCR:"))
        return false;
    packed_offset = cursor.position;
    if (!read_u32(&cursor, &value32))
        return false;
    if (value32 < 5u)
        return set_error(error, JC_SCRIPT_ERROR_BAD_SIZE, packed_offset, 0u,
                         "ADS packed block size is smaller than its 5-byte header");
    body_size = (size_t)value32 - 5u;
    if (!read_u8(&cursor, &method) || !read_u32(&cursor, &value32))
        return false;
    if ((size_t)value32 > bytecode_capacity)
        return set_error(error, JC_SCRIPT_ERROR_LIMIT, packed_offset, 0u,
                         "ADS bytecode needs %lu bytes; capacity is %lu",
                         (unsigned long)value32,
                         (unsigned long)bytecode_capacity);
    if (!take(&cursor, body_size, &bytes))
        return false;
    if (!jc_decompress(method, bytes, body_size, bytecode_storage,
                       (size_t)value32, decompression_error,
                       sizeof(decompression_error)))
        return set_error(error, JC_SCRIPT_ERROR_DECOMPRESSION, packed_offset, 0u,
                         "ADS bytecode decompression failed: %s",
                         decompression_error);
    ads->bytecode = bytecode_storage;
    ads->bytecode_size = (size_t)value32;

    if (!expect_tag(&cursor, "TAG:") || !read_u32(&cursor, &value32) ||
        !read_u16(&cursor, &count16))
        return false;
    if ((uint64_t)value32 >
        (uint64_t)(cursor.size - cursor.position) + 2u)
        return set_error(error, JC_SCRIPT_ERROR_BAD_SIZE,
                         cursor.position - 6u, 0u,
                         "ADS TAG declared size exceeds remaining input");
    if (count16 > JC_ADS_MAX_TAGS)
        return set_error(error, JC_SCRIPT_ERROR_LIMIT,
                         cursor.position - 2u, 0u,
                         "ADS has %u tag descriptions; limit is %u",
                         (unsigned)count16, (unsigned)JC_ADS_MAX_TAGS);
    ads->tag_count = count16;
    for (index = 0u; index < ads->tag_count; ++index) {
        if (!read_u16(&cursor, &ads->tags[index].id) ||
            !read_cstring(&cursor, ads->tags[index].description,
                          sizeof(ads->tags[index].description),
                          "tag description"))
            return false;
    }
    if (cursor.position != cursor.size)
        return set_error(error, JC_SCRIPT_ERROR_BAD_SIZE, cursor.position, 0u,
                         "ADS has %lu trailing bytes",
                         (unsigned long)(cursor.size - cursor.position));
    if (!validate_bytecode(ads, error))
        return false;
    clear_error(error);
    return true;
}
