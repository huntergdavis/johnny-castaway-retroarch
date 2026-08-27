/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "jc_decompress.h"

#include <stdio.h>
#include <string.h>

typedef struct bit_reader {
    const uint8_t *data;
    size_t bit_count;
    size_t position;
} bit_reader_t;

static bool fail(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size > 0u)
        snprintf(error, error_size, "%s", message);
    return false;
}

static bool read_bits(bit_reader_t *reader, unsigned count, uint16_t *value)
{
    unsigned index;
    uint16_t result = 0u;

    if (count > 16u || reader->position + count > reader->bit_count)
        return false;
    for (index = 0u; index < count; ++index) {
        size_t bit = reader->position + index;
        if ((reader->data[bit / 8u] & (uint8_t)(1u << (bit % 8u))) != 0u)
            result = (uint16_t)(result | (uint16_t)(1u << index));
    }
    reader->position += count;
    *value = result;
    return true;
}

static bool skip_bits(bit_reader_t *reader, size_t count)
{
    if (reader->position + count > reader->bit_count)
        return false;
    reader->position += count;
    return true;
}

static bool decompress_rle(const uint8_t *input, size_t input_size,
                           uint8_t *output, size_t output_size,
                           char *error, size_t error_size)
{
    size_t input_offset = 0u;
    size_t output_offset = 0u;

    while (output_offset < output_size) {
        uint8_t control;
        size_t count;
        if (input_offset >= input_size)
            return fail(error, error_size, "RLE stream ended before output was complete");
        control = input[input_offset++];
        count = (size_t)(control & 0x7fu);
        if (count == 0u)
            return fail(error, error_size, "RLE stream contains a zero-length run");
        if (count > output_size - output_offset)
            count = output_size - output_offset;

        if ((control & 0x80u) != 0u) {
            if (input_offset >= input_size)
                return fail(error, error_size, "RLE repeat is missing its value");
            memset(output + output_offset, input[input_offset++], count);
        } else {
            size_t encoded_count = (size_t)control;
            if (input_offset + encoded_count > input_size)
                return fail(error, error_size, "RLE literal exceeds its input");
            memcpy(output + output_offset, input + input_offset, count);
            input_offset += encoded_count;
        }
        output_offset += count;
    }
    return true;
}

static bool stack_push(uint8_t *stack, size_t *size, uint8_t value)
{
    if (*size >= 4096u)
        return false;
    stack[(*size)++] = value;
    return true;
}

static bool decompress_lzw(const uint8_t *input, size_t input_size,
                           uint8_t *output, size_t output_size,
                           char *error, size_t error_size)
{
    uint16_t prefix[4096] = {0};
    uint8_t append[4096] = {0};
    uint8_t stack[4096];
    size_t stack_size = 0u;
    bit_reader_t reader;
    unsigned code_bits = 9u;
    uint32_t free_entry = 257u;
    uint32_t segment_bits = 0u;
    uint16_t old_code;
    uint16_t last_byte;
    size_t output_offset = 0u;

    if (output_size == 0u)
        return true;
    reader.data = input;
    reader.bit_count = input_size * 8u;
    reader.position = 0u;
    if (!read_bits(&reader, code_bits, &old_code) || old_code > 255u)
        return fail(error, error_size, "LZW stream has an invalid first code");
    last_byte = old_code;
    output[output_offset++] = (uint8_t)old_code;

    while (output_offset < output_size) {
        uint16_t new_code;
        uint16_t code;

        if (!read_bits(&reader, code_bits, &new_code))
            return fail(error, error_size, "LZW stream ended before output was complete");
        segment_bits += code_bits;
        if (new_code == 256u) {
            uint32_t alignment = (uint32_t)code_bits << 3;
            uint32_t skip = (alignment - ((segment_bits - 1u) % alignment)) - 1u;
            if (!skip_bits(&reader, skip))
                return fail(error, error_size, "LZW clear code alignment exceeds input");
            code_bits = 9u;
            free_entry = 256u;
            segment_bits = 0u;
            continue;
        }
        if (new_code > free_entry)
            return fail(error, error_size, "LZW code references an undefined entry");

        code = new_code;
        if (code == free_entry) {
            if (!stack_push(stack, &stack_size, (uint8_t)last_byte))
                return fail(error, error_size, "LZW decode stack overflow");
            code = old_code;
        }
        while (code > 255u) {
            if (code >= free_entry ||
                !stack_push(stack, &stack_size, append[code]))
                return fail(error, error_size, "LZW dictionary chain is invalid");
            code = prefix[code];
        }
        if (!stack_push(stack, &stack_size, (uint8_t)code))
            return fail(error, error_size, "LZW decode stack overflow");
        last_byte = code;

        while (stack_size > 0u && output_offset < output_size)
            output[output_offset++] = stack[--stack_size];
        stack_size = 0u;

        if (free_entry < 4096u) {
            prefix[free_entry] = old_code;
            append[free_entry] = (uint8_t)last_byte;
            ++free_entry;
            if (free_entry >= (1u << code_bits) && code_bits < 12u) {
                ++code_bits;
                segment_bits = 0u;
            }
        }
        old_code = new_code;
    }
    return true;
}

bool jc_decompress(uint8_t method, const uint8_t *input, size_t input_size,
                   uint8_t *output, size_t output_size,
                   char *error, size_t error_size)
{
    if ((input == NULL && input_size != 0u) ||
        (output == NULL && output_size != 0u))
        return fail(error, error_size, "decompressor received null storage");

    switch (method) {
    case JC_COMPRESSION_NONE:
        if (input_size < output_size)
            return fail(error, error_size, "stored block is shorter than its output");
        memcpy(output, input, output_size);
        break;
    case JC_COMPRESSION_RLE:
        if (!decompress_rle(input, input_size, output, output_size,
                            error, error_size))
            return false;
        break;
    case JC_COMPRESSION_LZW:
        if (!decompress_lzw(input, input_size, output, output_size,
                            error, error_size))
            return false;
        break;
    default:
        return fail(error, error_size, "unknown DGDS compression method");
    }

    if (error != NULL && error_size > 0u)
        error[0] = '\0';
    return true;
}
