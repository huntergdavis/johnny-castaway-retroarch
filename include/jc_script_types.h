/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef JC_SCRIPT_TYPES_H
#define JC_SCRIPT_TYPES_H

#include <stddef.h>
#include <stdint.h>

#define JC_SCRIPT_ERROR_MESSAGE_BYTES 160u

typedef enum jc_script_domain {
    JC_SCRIPT_DOMAIN_NONE = 0,
    JC_SCRIPT_DOMAIN_ADS_PARSE,
    JC_SCRIPT_DOMAIN_TTM_PARSE,
    JC_SCRIPT_DOMAIN_ADS_VM,
    JC_SCRIPT_DOMAIN_TTM_VM
} jc_script_domain_t;

typedef enum jc_script_error_code {
    JC_SCRIPT_ERROR_NONE = 0,
    JC_SCRIPT_ERROR_NULL_ARGUMENT,
    JC_SCRIPT_ERROR_TRUNCATED,
    JC_SCRIPT_ERROR_BAD_TAG,
    JC_SCRIPT_ERROR_BAD_SIZE,
    JC_SCRIPT_ERROR_LIMIT,
    JC_SCRIPT_ERROR_DECOMPRESSION,
    JC_SCRIPT_ERROR_UNKNOWN_OPCODE,
    JC_SCRIPT_ERROR_BAD_OPERAND,
    JC_SCRIPT_ERROR_BAD_TARGET,
    JC_SCRIPT_ERROR_UNBOUND_RESOURCE,
    JC_SCRIPT_ERROR_INSTRUCTION_BUDGET,
    JC_SCRIPT_ERROR_CALLBACK
} jc_script_error_code_t;

typedef struct jc_script_error {
    jc_script_error_code_t code;
    jc_script_domain_t domain;
    size_t offset;
    uint16_t opcode;
    char message[JC_SCRIPT_ERROR_MESSAGE_BYTES];
} jc_script_error_t;

#endif
