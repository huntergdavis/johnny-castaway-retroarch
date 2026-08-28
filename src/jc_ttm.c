/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Bounded C rewrite of the TTM format/opcode behavior documented by Wilson
 * Reborn (crates/wilson-dgds/src/ttm.rs) and Johnny Reborn (ttm.c/resource.c).
 * No upstream source text is copied; the references are GPL-3.0-or-later.
 */
#include "jc_ttm.h"

#include "jc_decompress.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct jc_ttm_cursor {
    const uint8_t *data;
    size_t size;
    size_t position;
    jc_script_error_t *error;
} jc_ttm_cursor_t;

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
    error->domain = JC_SCRIPT_DOMAIN_TTM_PARSE;
    error->offset = offset;
    error->opcode = opcode;
    va_start(args, format);
    vsnprintf(error->message, sizeof(error->message), format, args);
    va_end(args);
    return false;
}

static bool take(jc_ttm_cursor_t *cursor, size_t count, const uint8_t **bytes)
{
    if (count > cursor->size - cursor->position)
        return set_error(cursor->error, JC_SCRIPT_ERROR_TRUNCATED,
                         cursor->position, 0u,
                         "TTM is truncated: need %lu bytes, have %lu",
                         (unsigned long)count,
                         (unsigned long)(cursor->size - cursor->position));
    *bytes = cursor->data + cursor->position;
    cursor->position += count;
    return true;
}

static bool read_u8(jc_ttm_cursor_t *cursor, uint8_t *value)
{
    const uint8_t *bytes = NULL;
    if (!take(cursor, 1u, &bytes))
        return false;
    *value = bytes[0];
    return true;
}

static bool read_u16(jc_ttm_cursor_t *cursor, uint16_t *value)
{
    const uint8_t *bytes = NULL;
    if (!take(cursor, 2u, &bytes))
        return false;
    *value = (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
    return true;
}

static bool read_u32(jc_ttm_cursor_t *cursor, uint32_t *value)
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

static bool expect_tag(jc_ttm_cursor_t *cursor, const char expected[4])
{
    const uint8_t *bytes = NULL;
    size_t offset = cursor->position;

    if (!take(cursor, 4u, &bytes))
        return false;
    if (memcmp(bytes, expected, 4u) != 0)
        return set_error(cursor->error, JC_SCRIPT_ERROR_BAD_TAG, offset, 0u,
                         "TTM expected chunk %.4s, found %02X%02X%02X%02X",
                         expected, bytes[0], bytes[1], bytes[2], bytes[3]);
    return true;
}

static bool read_cstring(jc_ttm_cursor_t *cursor, char *output,
                         size_t output_size, const char *field)
{
    size_t index;

    for (index = 0u; index < JC_TTM_NAME_BYTES; ++index) {
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
                             "TTM %s exceeds destination storage", field);
        if (byte < 0x20u || byte > 0x7eu)
            return set_error(cursor->error, JC_SCRIPT_ERROR_BAD_OPERAND,
                             cursor->position - 1u, 0u,
                             "TTM %s contains a non-printable byte", field);
        output[index] = (char)byte;
    }
    return set_error(cursor->error, JC_SCRIPT_ERROR_BAD_SIZE,
                     cursor->position, 0u,
                     "TTM %s is not NUL-terminated within %u bytes",
                     field, (unsigned)JC_TTM_NAME_BYTES);
}

const char *jc_ttm_opcode_name(uint16_t opcode)
{
    switch (opcode) {
    case 0x001fu: return "SAVE_BACKGROUND";
    case 0x0080u: return "DRAW_BACKGROUND";
    case 0x0110u: return "PURGE";
    case 0x0ff0u: return "UPDATE";
    case 0x1021u: return "SET_DELAY";
    case 0x1051u: return "SET_BMP_SLOT";
    case 0x1061u: return "SET_PALETTE_SLOT";
    case 0x1101u: return "LOCAL_TAG";
    case 0x1111u: return "TAG";
    case 0x1121u: return "TTM_UNKNOWN_1";
    case 0x1201u: return "GOTO_TAG";
    case 0x2002u: return "SET_COLORS";
    case 0x2012u: return "SET_FRAME1";
    case 0x2022u: return "TIMER";
    case 0x4004u: return "SET_CLIP_ZONE";
    case 0x4110u: return "FADE_OUT";
    case 0x4120u: return "FADE_IN";
    case 0x4204u: return "COPY_ZONE_TO_BG";
    case 0x4214u: return "SAVE_IMAGE1";
    case 0xa002u: return "DRAW_PIXEL";
    case 0xa054u: return "SAVE_ZONE";
    case 0xa064u: return "RESTORE_ZONE";
    case 0xa0a4u: return "DRAW_LINE";
    case 0xa104u: return "DRAW_RECT";
    case 0xa404u: return "DRAW_CIRCLE";
    case 0xa504u: return "DRAW_SPRITE";
    case 0xa510u: return "DRAW_SPRITE1";
    case 0xa524u: return "DRAW_SPRITE_FLIP";
    case 0xa530u: return "DRAW_SPRITE3";
    case 0xa601u: return "CLEAR_SCREEN";
    case 0xb606u: return "DRAW_SCREEN";
    case 0xc020u: return "LOAD_SAMPLE";
    case 0xc030u: return "SELECT_SAMPLE";
    case 0xc040u: return "DESELECT_SAMPLE";
    case 0xc051u: return "PLAY_SAMPLE";
    case 0xc060u: return "STOP_SAMPLE";
    case 0xf01fu: return "LOAD_SCREEN";
    case 0xf02fu: return "LOAD_IMAGE";
    case 0xf05fu: return "LOAD_PALETTE";
    default: return NULL;
    }
}

bool jc_ttm_instruction_at(const jc_ttm_t *ttm, size_t offset,
                           jc_ttm_instruction_t *instruction,
                           jc_script_error_t *error)
{
    size_t position;
    size_t index;
    uint8_t arg_count;

    clear_error(error);
    if (ttm == NULL || instruction == NULL || ttm->bytecode == NULL)
        return set_error(error, JC_SCRIPT_ERROR_NULL_ARGUMENT, offset, 0u,
                         "TTM instruction decoder received a null argument");
    if (offset > ttm->bytecode_size || ttm->bytecode_size - offset < 2u)
        return set_error(error, JC_SCRIPT_ERROR_TRUNCATED, offset, 0u,
                         "TTM instruction is truncated before its opcode");
    memset(instruction, 0, sizeof(*instruction));
    instruction->offset = offset;
    instruction->opcode = (uint16_t)((uint16_t)ttm->bytecode[offset] |
                                     ((uint16_t)ttm->bytecode[offset + 1u] << 8));
    if (jc_ttm_opcode_name(instruction->opcode) == NULL)
        return set_error(error, JC_SCRIPT_ERROR_UNKNOWN_OPCODE, offset,
                         instruction->opcode, "unknown TTM opcode 0x%04X",
                         (unsigned)instruction->opcode);

    position = offset + 2u;
    arg_count = (uint8_t)(instruction->opcode & 0x000fu);
    if (arg_count == 0x0fu) {
        size_t start = position;
        while (position < ttm->bytecode_size &&
               ttm->bytecode[position] != 0u) {
            if (position - start >= JC_TTM_MAX_STRING_BYTES)
                return set_error(error, JC_SCRIPT_ERROR_LIMIT, offset,
                                 instruction->opcode,
                                 "TTM string operand exceeds %u bytes",
                                 (unsigned)JC_TTM_MAX_STRING_BYTES);
            ++position;
        }
        if (position >= ttm->bytecode_size)
            return set_error(error, JC_SCRIPT_ERROR_TRUNCATED, offset,
                             instruction->opcode,
                             "TTM string operand is not NUL-terminated");
        instruction->string = (const char *)(ttm->bytecode + start);
        instruction->string_length = position - start;
        ++position;
        if (((instruction->string_length + 1u) & 1u) != 0u) {
            if (position >= ttm->bytecode_size)
                return set_error(error, JC_SCRIPT_ERROR_TRUNCATED, offset,
                                 instruction->opcode,
                                 "TTM string operand is missing even-byte padding");
            ++position;
        }
    } else {
        if (arg_count > JC_TTM_MAX_ARGS)
            return set_error(error, JC_SCRIPT_ERROR_LIMIT, offset,
                             instruction->opcode,
                             "TTM opcode declares too many operands");
        if ((size_t)arg_count * 2u > ttm->bytecode_size - position)
            return set_error(error, JC_SCRIPT_ERROR_TRUNCATED, offset,
                             instruction->opcode,
                             "TTM opcode 0x%04X is truncated in its operands",
                             (unsigned)instruction->opcode);
        instruction->arg_count = arg_count;
        for (index = 0u; index < arg_count; ++index) {
            instruction->args[index] =
                (uint16_t)((uint16_t)ttm->bytecode[position] |
                           ((uint16_t)ttm->bytecode[position + 1u] << 8));
            position += 2u;
        }
    }
    instruction->next_offset = position;
    return true;
}

bool jc_ttm_find_tag(const jc_ttm_t *ttm, uint16_t id, size_t *offset)
{
    size_t index;

    if (ttm == NULL || offset == NULL)
        return false;
    for (index = 0u; index < ttm->label_count; ++index) {
        if (ttm->labels[index].id == id) {
            *offset = ttm->labels[index].offset;
            return true;
        }
    }
    return false;
}

bool jc_ttm_find_previous_tag(const jc_ttm_t *ttm, size_t before,
                              size_t *offset)
{
    size_t index;
    bool found = false;
    size_t result = 0u;

    if (ttm == NULL || offset == NULL)
        return false;
    for (index = 0u; index < ttm->label_count; ++index) {
        if (ttm->labels[index].offset >= before)
            continue;
        if (!found || ttm->labels[index].offset > result) {
            result = ttm->labels[index].offset;
            found = true;
        }
    }
    if (found)
        *offset = result;
    return found;
}

static bool validate_bytecode(jc_ttm_t *ttm, jc_script_error_t *error)
{
    size_t offset = 0u;

    while (offset < ttm->bytecode_size) {
        jc_ttm_instruction_t instruction;
        if (!jc_ttm_instruction_at(ttm, offset, &instruction, error))
            return false;
        if (instruction.opcode == 0x1101u || instruction.opcode == 0x1111u) {
            jc_ttm_tag_t *label;
            if (ttm->label_count >= JC_TTM_MAX_TAGS)
                return set_error(error, JC_SCRIPT_ERROR_LIMIT, offset,
                                 instruction.opcode,
                                 "TTM bytecode has more than %u labels",
                                 (unsigned)JC_TTM_MAX_TAGS);
            label = &ttm->labels[ttm->label_count++];
            memset(label, 0, sizeof(*label));
            label->id = instruction.args[0];
            label->offset = instruction.next_offset;
        }
        offset = instruction.next_offset;
    }
    return true;
}

bool jc_ttm_parse(jc_ttm_t *ttm, const uint8_t *data, size_t size,
                  uint8_t *bytecode_storage, size_t bytecode_capacity,
                  jc_script_error_t *error)
{
    jc_ttm_cursor_t cursor;
    const uint8_t *bytes = NULL;
    uint32_t value32;
    uint16_t count16;
    size_t index;
    size_t packed_offset;
    uint8_t method;
    size_t body_size;
    char decompression_error[JC_SCRIPT_ERROR_MESSAGE_BYTES];

    clear_error(error);
    if (ttm == NULL || data == NULL || bytecode_storage == NULL)
        return set_error(error, JC_SCRIPT_ERROR_NULL_ARGUMENT, 0u, 0u,
                         "TTM parser received a null argument");
    memset(ttm, 0, sizeof(*ttm));
    cursor.data = data;
    cursor.size = size;
    cursor.position = 0u;
    cursor.error = error;

    if (!expect_tag(&cursor, "VER:") || !read_u32(&cursor, &value32))
        return false;
    if (value32 != 5u)
        return set_error(error, JC_SCRIPT_ERROR_BAD_SIZE, 4u, 0u,
                         "TTM version field size is %lu, expected 5",
                         (unsigned long)value32);
    if (!take(&cursor, 5u, &bytes))
        return false;
    if (bytes[4] != 0u)
        return set_error(error, JC_SCRIPT_ERROR_BAD_SIZE, 8u, 0u,
                         "TTM version string is not NUL-terminated");
    memcpy(ttm->version, bytes, 5u);
    ttm->version[5] = '\0';

    if (!expect_tag(&cursor, "PAG:") || !read_u32(&cursor, &ttm->page_count) ||
        !take(&cursor, 2u, &bytes) || !expect_tag(&cursor, "TT3:"))
        return false;
    packed_offset = cursor.position;
    if (!read_u32(&cursor, &value32))
        return false;
    if (value32 < 5u)
        return set_error(error, JC_SCRIPT_ERROR_BAD_SIZE, packed_offset, 0u,
                         "TTM packed block size is smaller than its 5-byte header");
    body_size = (size_t)value32 - 5u;
    if (!read_u8(&cursor, &method) || !read_u32(&cursor, &value32))
        return false;
    if ((size_t)value32 > bytecode_capacity)
        return set_error(error, JC_SCRIPT_ERROR_LIMIT, packed_offset, 0u,
                         "TTM bytecode needs %lu bytes; capacity is %lu",
                         (unsigned long)value32,
                         (unsigned long)bytecode_capacity);
    if (!take(&cursor, body_size, &bytes))
        return false;
    if (!jc_decompress(method, bytes, body_size, bytecode_storage,
                       (size_t)value32, decompression_error,
                       sizeof(decompression_error)))
        return set_error(error, JC_SCRIPT_ERROR_DECOMPRESSION, packed_offset, 0u,
                         "TTM bytecode decompression failed: %s",
                         decompression_error);
    ttm->bytecode = bytecode_storage;
    ttm->bytecode_size = (size_t)value32;

    if (!expect_tag(&cursor, "TTI:") || !take(&cursor, 4u, &bytes) ||
        !expect_tag(&cursor, "TAG:") || !read_u32(&cursor, &value32) ||
        !read_u16(&cursor, &count16))
        return false;
    if ((uint64_t)value32 >
        (uint64_t)(cursor.size - cursor.position) + 2u)
        return set_error(error, JC_SCRIPT_ERROR_BAD_SIZE,
                         cursor.position - 6u, 0u,
                         "TTM TAG declared size exceeds remaining input");
    if (count16 > JC_TTM_MAX_TAGS)
        return set_error(error, JC_SCRIPT_ERROR_LIMIT,
                         cursor.position - 2u, 0u,
                         "TTM has %u tag descriptions; limit is %u",
                         (unsigned)count16, (unsigned)JC_TTM_MAX_TAGS);
    ttm->tag_count = count16;
    for (index = 0u; index < ttm->tag_count; ++index) {
        if (!read_u16(&cursor, &ttm->tags[index].id) ||
            !read_cstring(&cursor, ttm->tags[index].description,
                          sizeof(ttm->tags[index].description),
                          "tag description"))
            return false;
    }
    if (cursor.position != cursor.size)
        return set_error(error, JC_SCRIPT_ERROR_BAD_SIZE, cursor.position, 0u,
                         "TTM has %lu trailing bytes",
                         (unsigned long)(cursor.size - cursor.position));
    if (!validate_bytecode(ttm, error))
        return false;
    clear_error(error);
    return true;
}
