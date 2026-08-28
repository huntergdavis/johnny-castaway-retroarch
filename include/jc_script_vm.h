/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef JC_SCRIPT_VM_H
#define JC_SCRIPT_VM_H

#include "jc_ads.h"
#include "jc_ttm.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JC_SCRIPT_TICKS_PER_SECOND 50u
#define JC_SCRIPT_MAX_TTM_SLOTS 10u
#define JC_SCRIPT_MAX_THREADS 10u
#define JC_SCRIPT_MAX_TRIGGERS 100u
#define JC_SCRIPT_MAX_LOCAL_TRIGGERS 8u
#define JC_SCRIPT_MAX_RANDOM_OPS 10u
#define JC_SCRIPT_MAX_GOSUB_DEPTH 16u
#define JC_SCRIPT_INSTRUCTION_BUDGET 4096u
#define JC_SCRIPT_MAX_BMP_SLOTS 6u

typedef enum jc_script_event_kind {
    JC_SCRIPT_EVENT_INSTRUCTION = 0,
    JC_SCRIPT_EVENT_SCENE_STARTED,
    JC_SCRIPT_EVENT_SCENE_STOPPED,
    JC_SCRIPT_EVENT_FRAME_READY
} jc_script_event_kind_t;

typedef struct jc_script_event {
    jc_script_event_kind_t kind;
    jc_script_domain_t domain;
    uint16_t opcode;
    size_t thread_index;
    uint16_t scene_slot;
    uint16_t scene_tag;
    uint16_t args[JC_TTM_MAX_ARGS];
    uint8_t arg_count;
    const char *string;
    size_t string_length;
    uint16_t selected_bmp_slot;
    uint8_t foreground_color;
    uint8_t background_color;
} jc_script_event_t;

typedef bool (*jc_script_event_callback_t)(void *userdata,
                                           const jc_script_event_t *event,
                                           jc_script_error_t *error);

typedef enum jc_script_tick_result {
    JC_SCRIPT_TICK_WAITING = 0,
    JC_SCRIPT_TICK_FRAME,
    JC_SCRIPT_TICK_FINISHED,
    JC_SCRIPT_TICK_ERROR
} jc_script_tick_result_t;

typedef struct jc_script_thread {
    const jc_ttm_t *ttm;
    uint8_t state;
    uint16_t scene_slot;
    uint16_t scene_tag;
    size_t ip;
    size_t next_goto;
    bool has_next_goto;
    uint16_t delay;
    uint16_t timer;
    int32_t scene_timer;
    uint16_t scene_iterations;
    uint16_t selected_bmp_slot;
    uint8_t foreground_color;
    uint8_t background_color;
    bool has_yielded;
} jc_script_thread_t;

typedef struct jc_ttm_vm {
    jc_script_thread_t thread;
    bool initialized;
} jc_ttm_vm_t;

typedef struct jc_script_trigger {
    uint16_t slot;
    uint16_t tag;
    size_t offset;
} jc_script_trigger_t;

typedef struct jc_script_random_op {
    uint8_t kind;
    uint16_t slot;
    uint16_t tag;
    uint16_t plays;
    uint16_t weight;
} jc_script_random_op_t;

typedef struct jc_script_vm {
    const jc_ads_t *ads;
    const jc_ttm_t *slots[JC_SCRIPT_MAX_TTM_SLOTS];
    jc_script_thread_t threads[JC_SCRIPT_MAX_THREADS];
    jc_script_trigger_t triggers[JC_SCRIPT_MAX_TRIGGERS];
    size_t trigger_count;
    jc_script_trigger_t local_triggers[JC_SCRIPT_MAX_LOCAL_TRIGGERS];
    size_t local_trigger_count;
    jc_script_random_op_t random_ops[JC_SCRIPT_MAX_RANDOM_OPS];
    size_t random_op_count;
    size_t active_threads;
    uint32_t rng_state;
    uint64_t tick_count;
    bool initialized;
    bool started;
    bool stop_requested;
} jc_script_vm_t;

bool jc_ttm_vm_init(jc_ttm_vm_t *vm, const jc_ttm_t *ttm,
                    uint16_t start_tag, jc_script_error_t *error);
jc_script_tick_result_t jc_ttm_vm_tick(jc_ttm_vm_t *vm,
                                       jc_script_event_callback_t callback,
                                       void *userdata,
                                       jc_script_error_t *error);

bool jc_script_vm_init(jc_script_vm_t *vm, const jc_ads_t *ads,
                       uint32_t seed, jc_script_error_t *error);
bool jc_script_vm_bind_ttm(jc_script_vm_t *vm, uint16_t slot,
                           const jc_ttm_t *ttm, jc_script_error_t *error);
bool jc_script_vm_start(jc_script_vm_t *vm, uint16_t start_tag,
                        jc_script_event_callback_t callback, void *userdata,
                        jc_script_error_t *error);
jc_script_tick_result_t jc_script_vm_tick(jc_script_vm_t *vm,
                                          jc_script_event_callback_t callback,
                                          void *userdata,
                                          jc_script_error_t *error);
size_t jc_script_vm_active_threads(const jc_script_vm_t *vm);

#endif
