/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "jc_ads.h"
#include "jc_script_vm.h"
#include "jc_ttm.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct buffer {
    uint8_t data[32768];
    size_t size;
} buffer_t;

typedef struct event_log {
    unsigned frames;
    unsigned starts;
    unsigned stops;
    unsigned sounds;
    unsigned ads_instructions;
    bool reject;
} event_log_t;

static void require(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static void put_u8(buffer_t *buffer, uint8_t value)
{
    require(buffer->size < sizeof(buffer->data), "test buffer overflow");
    buffer->data[buffer->size++] = value;
}

static void put_u16(buffer_t *buffer, uint16_t value)
{
    put_u8(buffer, (uint8_t)value);
    put_u8(buffer, (uint8_t)(value >> 8));
}

static void put_u32(buffer_t *buffer, uint32_t value)
{
    put_u8(buffer, (uint8_t)value);
    put_u8(buffer, (uint8_t)(value >> 8));
    put_u8(buffer, (uint8_t)(value >> 16));
    put_u8(buffer, (uint8_t)(value >> 24));
}

static void put_bytes(buffer_t *buffer, const void *bytes, size_t size)
{
    const uint8_t *source = (const uint8_t *)bytes;
    size_t index;
    for (index = 0u; index < size; ++index)
        put_u8(buffer, source[index]);
}

static void put_cstring(buffer_t *buffer, const char *value)
{
    put_bytes(buffer, value, strlen(value) + 1u);
}

static void put_op(buffer_t *buffer, uint16_t opcode,
                   const uint16_t *args, size_t arg_count)
{
    size_t index;
    put_u16(buffer, opcode);
    for (index = 0u; index < arg_count; ++index)
        put_u16(buffer, args[index]);
}

static void make_ads_file(buffer_t *file, const buffer_t *code)
{
    memset(file, 0, sizeof(*file));
    put_bytes(file, "VER:", 4u);
    put_u32(file, 5u);
    put_bytes(file, "1.20\0", 5u);
    put_bytes(file, "ADS:", 4u);
    put_u32(file, 0u);
    put_bytes(file, "RES:", 4u);
    put_u32(file, 0u);
    put_u16(file, 1u);
    put_u16(file, 1u);
    put_cstring(file, "A.TTM");
    put_bytes(file, "SCR:", 4u);
    put_u32(file, (uint32_t)code->size + 5u);
    put_u8(file, 0u);
    put_u32(file, (uint32_t)code->size);
    put_bytes(file, code->data, code->size);
    put_bytes(file, "TAG:", 4u);
    put_u32(file, 0u);
    put_u16(file, 1u);
    put_u16(file, 1u);
    put_cstring(file, "start");
}

static void make_ttm_file(buffer_t *file, const buffer_t *code)
{
    memset(file, 0, sizeof(*file));
    put_bytes(file, "VER:", 4u);
    put_u32(file, 5u);
    put_bytes(file, "1.20\0", 5u);
    put_bytes(file, "PAG:", 4u);
    put_u32(file, 2u);
    put_u16(file, 0u);
    put_bytes(file, "TT3:", 4u);
    put_u32(file, (uint32_t)code->size + 5u);
    put_u8(file, 0u);
    put_u32(file, (uint32_t)code->size);
    put_bytes(file, code->data, code->size);
    put_bytes(file, "TTI:", 4u);
    put_u32(file, 0u);
    put_bytes(file, "TAG:", 4u);
    put_u32(file, 0u);
    put_u16(file, 2u);
    put_u16(file, 1u);
    put_cstring(file, "one");
    put_u16(file, 2u);
    put_cstring(file, "two");
}

static bool log_event(void *userdata, const jc_script_event_t *event,
                      jc_script_error_t *error)
{
    event_log_t *log = (event_log_t *)userdata;
    (void)error;
    if (log->reject)
        return false;
    if (event->kind == JC_SCRIPT_EVENT_FRAME_READY)
        ++log->frames;
    else if (event->kind == JC_SCRIPT_EVENT_SCENE_STARTED)
        ++log->starts;
    else if (event->kind == JC_SCRIPT_EVENT_SCENE_STOPPED)
        ++log->stops;
    else if (event->domain == JC_SCRIPT_DOMAIN_ADS_VM)
        ++log->ads_instructions;
    if (event->kind == JC_SCRIPT_EVENT_INSTRUCTION &&
        event->domain == JC_SCRIPT_DOMAIN_TTM_VM &&
        event->opcode == 0xc051u)
        ++log->sounds;
    return true;
}

static void build_ttm_code(buffer_t *code)
{
    uint16_t args[4];
    memset(code, 0, sizeof(*code));
    args[0] = 1u;
    put_op(code, 0x1111u, args, 1u);
    args[0] = 4u;
    put_op(code, 0x1021u, args, 1u);
    args[0] = 7u;
    put_op(code, 0xc051u, args, 1u);
    put_op(code, 0x0ff0u, NULL, 0u);
    put_op(code, 0x0110u, NULL, 0u);
    args[0] = 2u;
    put_op(code, 0x1111u, args, 1u);
    args[0] = 9u;
    put_op(code, 0xc051u, args, 1u);
    put_op(code, 0x0ff0u, NULL, 0u);
    put_op(code, 0x0110u, NULL, 0u);
}

static void test_parsers_and_errors(void)
{
    buffer_t ads_code = {{0}, 0u};
    buffer_t ads_file;
    buffer_t ttm_code;
    buffer_t ttm_file;
    jc_ads_t ads;
    jc_ttm_t ttm;
    jc_script_error_t error;
    uint8_t ads_storage[4096];
    uint8_t ttm_storage[4096];
    uint16_t args[4] = {1u, 1u, 0u, 0u};

    put_op(&ads_code, 1u, NULL, 0u);
    put_op(&ads_code, 0x2005u, args, 4u);
    put_op(&ads_code, 0x1510u, NULL, 0u);
    make_ads_file(&ads_file, &ads_code);
    require(jc_ads_parse(&ads, ads_file.data, ads_file.size, ads_storage,
                         sizeof(ads_storage), &error), error.message);
    require(ads.label_count == 1u && ads.resource_count == 1u,
            "ADS parser did not collect labels/resources");

    build_ttm_code(&ttm_code);
    make_ttm_file(&ttm_file, &ttm_code);
    require(jc_ttm_parse(&ttm, ttm_file.data, ttm_file.size, ttm_storage,
                         sizeof(ttm_storage), &error), error.message);
    require(ttm.label_count == 2u && ttm.page_count == 2u,
            "TTM parser did not collect labels/pages");

    ads_code.size = 0u;
    put_op(&ads_code, 0x9999u, NULL, 0u);
    make_ads_file(&ads_file, &ads_code);
    require(!jc_ads_parse(&ads, ads_file.data, ads_file.size, ads_storage,
                          sizeof(ads_storage), &error),
            "unknown ADS opcode was accepted");
    require(error.code == JC_SCRIPT_ERROR_UNKNOWN_OPCODE &&
                error.opcode == 0x9999u,
            "unknown ADS opcode error is not structured");

    ttm_code.size = 0u;
    put_u16(&ttm_code, 0xf02fu);
    put_u8(&ttm_code, 'A');
    make_ttm_file(&ttm_file, &ttm_code);
    require(!jc_ttm_parse(&ttm, ttm_file.data, ttm_file.size, ttm_storage,
                          sizeof(ttm_storage), &error),
            "unterminated TTM string was accepted");
    require(error.code == JC_SCRIPT_ERROR_TRUNCATED,
            "malformed TTM string error is not structured");

    ttm_code.size = 0u;
    put_op(&ttm_code, 0x9992u, args, 2u);
    make_ttm_file(&ttm_file, &ttm_code);
    require(!jc_ttm_parse(&ttm, ttm_file.data, ttm_file.size, ttm_storage,
                          sizeof(ttm_storage), &error),
            "unknown TTM opcode was accepted");
    require(error.code == JC_SCRIPT_ERROR_UNKNOWN_OPCODE &&
                error.opcode == 0x9992u,
            "unknown TTM opcode error is not structured");
}

static void test_ttm_tick_cadence_and_callback(void)
{
    buffer_t code;
    buffer_t file;
    jc_ttm_t ttm;
    jc_ttm_vm_t vm;
    jc_script_error_t error;
    uint8_t storage[4096];
    event_log_t log;
    unsigned index;

    build_ttm_code(&code);
    make_ttm_file(&file, &code);
    require(jc_ttm_parse(&ttm, file.data, file.size, storage,
                         sizeof(storage), &error), error.message);
    require(jc_ttm_vm_init(&vm, &ttm, 1u, &error), error.message);
    memset(&log, 0, sizeof(log));
    require(jc_ttm_vm_tick(&vm, log_event, &log, &error) ==
                JC_SCRIPT_TICK_FRAME,
            "first TTM tick did not yield a frame");
    require(log.frames == 1u && log.sounds == 1u,
            "first TTM frame events are incomplete");
    for (index = 0u; index < 3u; ++index)
        require(jc_ttm_vm_tick(&vm, log_event, &log, &error) ==
                    JC_SCRIPT_TICK_WAITING,
                "TTM delay did not wait an exact 50 Hz tick");
    require(jc_ttm_vm_tick(&vm, log_event, &log, &error) ==
                JC_SCRIPT_TICK_FINISHED,
            "TTM did not resume after exactly four ticks");

    require(jc_ttm_vm_init(&vm, &ttm, 1u, &error), error.message);
    memset(&log, 0, sizeof(log));
    log.reject = true;
    require(jc_ttm_vm_tick(&vm, log_event, &log, &error) ==
                JC_SCRIPT_TICK_ERROR,
            "callback rejection did not stop TTM execution");
    require(error.code == JC_SCRIPT_ERROR_CALLBACK,
            "callback rejection did not produce a structured error");
}

static void build_ads_chain(buffer_t *code)
{
    uint16_t args[4];
    memset(code, 0, sizeof(*code));
    put_op(code, 1u, NULL, 0u);
    put_op(code, 0x3010u, NULL, 0u);
    args[0] = 1u; args[1] = 1u; args[2] = 0u; args[3] = 1u;
    put_op(code, 0x2005u, args, 4u);
    args[0] = 0u;
    put_op(code, 0x3020u, args, 1u);
    put_op(code, 0x30ffu, NULL, 0u);
    put_op(code, 0x1510u, NULL, 0u);
    args[0] = 1u; args[1] = 1u;
    put_op(code, 0x1350u, args, 2u);
    args[0] = 1u; args[1] = 2u; args[2] = 0u; args[3] = 0u;
    put_op(code, 0x2005u, args, 4u);
    put_op(code, 0x1510u, NULL, 0u);
    put_op(code, 0xffffu, NULL, 0u);
}

static void test_ads_runtime_chaining(void)
{
    buffer_t ads_code;
    buffer_t ads_file;
    buffer_t ttm_code;
    buffer_t ttm_file;
    jc_ads_t ads;
    jc_ttm_t ttm;
    jc_script_vm_t vm;
    jc_script_error_t error;
    uint8_t ads_storage[4096];
    uint8_t ttm_storage[4096];
    event_log_t log;
    unsigned guard;

    build_ads_chain(&ads_code);
    make_ads_file(&ads_file, &ads_code);
    build_ttm_code(&ttm_code);
    make_ttm_file(&ttm_file, &ttm_code);
    require(jc_ads_parse(&ads, ads_file.data, ads_file.size, ads_storage,
                         sizeof(ads_storage), &error), error.message);
    require(jc_ttm_parse(&ttm, ttm_file.data, ttm_file.size, ttm_storage,
                         sizeof(ttm_storage), &error), error.message);
    require(jc_script_vm_init(&vm, &ads, 12345u, &error), error.message);
    require(jc_script_vm_bind_ttm(&vm, 1u, &ttm, &error), error.message);
    memset(&log, 0, sizeof(log));
    require(jc_script_vm_start(&vm, 1u, log_event, &log, &error),
            error.message);
    require(log.starts == 1u && jc_script_vm_active_threads(&vm) == 1u,
            "ADS random block did not launch exactly one initial scene");

    for (guard = 0u; guard < 100u; ++guard) {
        jc_script_tick_result_t result =
            jc_script_vm_tick(&vm, log_event, &log, &error);
        require(result != JC_SCRIPT_TICK_ERROR, error.message);
        if (log.starts >= 2u && log.sounds >= 2u)
            break;
    }
    require(guard < 100u, "ADS IF_LASTPLAYED chain did not fire");
    require(log.starts >= 2u && log.stops >= 1u,
            "ADS reactive scene lifecycle events are incomplete");
    require(log.ads_instructions > 0u,
            "ADS instructions were not exposed to the event callback");
}

int main(void)
{
    test_parsers_and_errors();
    test_ttm_tick_cadence_and_callback();
    test_ads_runtime_chaining();
    puts("script VM tests passed");
    return 0;
}
