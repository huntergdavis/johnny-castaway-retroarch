/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Cooperative C rewrite of the 50 Hz ADS/TTM behavior in Johnny Reborn
 * (ads.c/ttm.c) and Wilson Reborn (ads_vm.rs/ttm_exec.rs). No upstream source
 * text is copied. The behavioral references are GPL-3.0-or-later.
 */
#include "jc_script_vm.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef enum ttm_outcome {
    TTM_OUTCOME_FRAME = 0,
    TTM_OUTCOME_FINISHED,
    TTM_OUTCOME_ERROR
} ttm_outcome_t;

static void clear_error(jc_script_error_t *error)
{
    if (error != NULL)
        memset(error, 0, sizeof(*error));
}

static bool set_error(jc_script_error_t *error, jc_script_domain_t domain,
                      jc_script_error_code_t code, size_t offset,
                      uint16_t opcode, const char *format, ...)
{
    va_list args;

    if (error == NULL)
        return false;
    memset(error, 0, sizeof(*error));
    error->code = code;
    error->domain = domain;
    error->offset = offset;
    error->opcode = opcode;
    va_start(args, format);
    vsnprintf(error->message, sizeof(error->message), format, args);
    va_end(args);
    return false;
}

static bool emit_event(jc_script_event_callback_t callback, void *userdata,
                       const jc_script_event_t *event,
                       jc_script_error_t *error)
{
    if (callback == NULL)
        return true;
    if (callback(userdata, event, error))
        return true;
    if (error != NULL && error->code == JC_SCRIPT_ERROR_NONE)
        return set_error(error, event->domain, JC_SCRIPT_ERROR_CALLBACK, 0u,
                         event->opcode, "script event callback rejected an event");
    return false;
}

static bool emit_ttm_instruction(const jc_script_thread_t *thread,
                                 size_t thread_index,
                                 const jc_ttm_instruction_t *instruction,
                                 jc_script_event_callback_t callback,
                                 void *userdata, jc_script_error_t *error)
{
    jc_script_event_t event;

    memset(&event, 0, sizeof(event));
    event.kind = JC_SCRIPT_EVENT_INSTRUCTION;
    event.domain = JC_SCRIPT_DOMAIN_TTM_VM;
    event.opcode = instruction->opcode;
    event.thread_index = thread_index;
    event.scene_slot = thread->scene_slot;
    event.scene_tag = thread->scene_tag;
    event.arg_count = instruction->arg_count;
    memcpy(event.args, instruction->args, sizeof(event.args));
    event.string = instruction->string;
    event.string_length = instruction->string_length;
    event.selected_bmp_slot = thread->selected_bmp_slot;
    event.foreground_color = thread->foreground_color;
    event.background_color = thread->background_color;
    return emit_event(callback, userdata, &event, error);
}

static bool emit_ads_instruction(const jc_ads_instruction_t *instruction,
                                 jc_script_event_callback_t callback,
                                 void *userdata, jc_script_error_t *error)
{
    jc_script_event_t event;

    memset(&event, 0, sizeof(event));
    event.kind = JC_SCRIPT_EVENT_INSTRUCTION;
    event.domain = JC_SCRIPT_DOMAIN_ADS_VM;
    event.opcode = instruction->opcode;
    event.arg_count = instruction->arg_count;
    memcpy(event.args, instruction->args,
           (size_t)instruction->arg_count * sizeof(instruction->args[0]));
    return emit_event(callback, userdata, &event, error);
}

static bool emit_thread_event(jc_script_event_kind_t kind,
                              const jc_script_thread_t *thread,
                              size_t thread_index,
                              jc_script_event_callback_t callback,
                              void *userdata, jc_script_error_t *error)
{
    jc_script_event_t event;

    memset(&event, 0, sizeof(event));
    event.kind = kind;
    event.domain = JC_SCRIPT_DOMAIN_TTM_VM;
    event.thread_index = thread_index;
    event.scene_slot = thread->scene_slot;
    event.scene_tag = thread->scene_tag;
    event.selected_bmp_slot = thread->selected_bmp_slot;
    event.foreground_color = thread->foreground_color;
    event.background_color = thread->background_color;
    return emit_event(callback, userdata, &event, error);
}

static void initialize_thread(jc_script_thread_t *thread, const jc_ttm_t *ttm,
                              uint16_t slot, uint16_t tag, size_t ip,
                              uint16_t arg3)
{
    int16_t signed_arg = (int16_t)arg3;

    memset(thread, 0, sizeof(*thread));
    thread->ttm = ttm;
    thread->state = 1u;
    thread->scene_slot = slot;
    thread->scene_tag = tag;
    thread->ip = ip;
    thread->delay = 4u;
    thread->foreground_color = 0x0fu;
    thread->background_color = 0x0fu;
    if (signed_arg < 0)
        thread->scene_timer = -(int32_t)signed_arg;
    else if (signed_arg > 0)
        thread->scene_iterations = (uint16_t)(arg3 - 1u);
}

static ttm_outcome_t execute_ttm(jc_script_thread_t *thread,
                                 size_t thread_index,
                                 jc_script_event_callback_t callback,
                                 void *userdata, size_t *budget,
                                 jc_script_error_t *error)
{
    for (;;) {
        jc_ttm_instruction_t instruction;

        if (*budget == 0u) {
            set_error(error, JC_SCRIPT_DOMAIN_TTM_VM,
                      JC_SCRIPT_ERROR_INSTRUCTION_BUDGET, thread->ip, 0u,
                      "TTM exceeded the per-tick instruction budget");
            return TTM_OUTCOME_ERROR;
        }
        --*budget;
        if (thread->ip >= thread->ttm->bytecode_size) {
            thread->state = 2u;
            return TTM_OUTCOME_FINISHED;
        }
        if (!jc_ttm_instruction_at(thread->ttm, thread->ip, &instruction,
                                   error)) {
            if (error != NULL)
                error->domain = JC_SCRIPT_DOMAIN_TTM_VM;
            return TTM_OUTCOME_ERROR;
        }
        thread->ip = instruction.next_offset;

        switch (instruction.opcode) {
        case 0x0110u:
            if (!emit_ttm_instruction(thread, thread_index, &instruction,
                                      callback, userdata, error))
                return TTM_OUTCOME_ERROR;
            if (thread->scene_timer > 0) {
                if (!jc_ttm_find_previous_tag(thread->ttm, instruction.offset,
                                              &thread->next_goto)) {
                    set_error(error, JC_SCRIPT_DOMAIN_TTM_VM,
                              JC_SCRIPT_ERROR_BAD_TARGET, instruction.offset,
                              instruction.opcode,
                              "PURGE cannot find a preceding TTM tag");
                    return TTM_OUTCOME_ERROR;
                }
                thread->has_next_goto = true;
                return TTM_OUTCOME_FRAME;
            }
            thread->state = 2u;
            return TTM_OUTCOME_FINISHED;
        case 0x0ff0u:
            if (!emit_ttm_instruction(thread, thread_index, &instruction,
                                      callback, userdata, error))
                return TTM_OUTCOME_ERROR;
            return TTM_OUTCOME_FRAME;
        case 0x1021u:
            thread->delay = instruction.args[0] > 4u
                                ? instruction.args[0] : 4u;
            break;
        case 0x1051u:
            if (instruction.args[0] >= JC_SCRIPT_MAX_BMP_SLOTS) {
                set_error(error, JC_SCRIPT_DOMAIN_TTM_VM,
                          JC_SCRIPT_ERROR_BAD_OPERAND, instruction.offset,
                          instruction.opcode,
                          "TTM bitmap slot %u exceeds limit %u",
                          (unsigned)instruction.args[0],
                          (unsigned)JC_SCRIPT_MAX_BMP_SLOTS);
                return TTM_OUTCOME_ERROR;
            }
            thread->selected_bmp_slot = instruction.args[0];
            break;
        case 0x1201u:
            if (!jc_ttm_find_tag(thread->ttm, instruction.args[0],
                                 &thread->next_goto)) {
                set_error(error, JC_SCRIPT_DOMAIN_TTM_VM,
                          JC_SCRIPT_ERROR_BAD_TARGET, instruction.offset,
                          instruction.opcode, "TTM GOTO tag %u does not exist",
                          (unsigned)instruction.args[0]);
                return TTM_OUTCOME_ERROR;
            }
            thread->has_next_goto = true;
            break;
        case 0x2002u:
            thread->foreground_color = (uint8_t)instruction.args[0];
            thread->background_color = (uint8_t)instruction.args[1];
            break;
        case 0x2022u:
            thread->delay = (uint16_t)(((uint32_t)instruction.args[0] +
                                        (uint32_t)instruction.args[1]) / 2u);
            break;
        default:
            break;
        }
        if (!emit_ttm_instruction(thread, thread_index, &instruction, callback,
                                  userdata, error))
            return TTM_OUTCOME_ERROR;
    }
}

bool jc_ttm_vm_init(jc_ttm_vm_t *vm, const jc_ttm_t *ttm,
                    uint16_t start_tag, jc_script_error_t *error)
{
    size_t offset = 0u;

    clear_error(error);
    if (vm == NULL || ttm == NULL)
        return set_error(error, JC_SCRIPT_DOMAIN_TTM_VM,
                         JC_SCRIPT_ERROR_NULL_ARGUMENT, 0u, 0u,
                         "TTM VM init received a null argument");
    if (start_tag != 0u && !jc_ttm_find_tag(ttm, start_tag, &offset))
        return set_error(error, JC_SCRIPT_DOMAIN_TTM_VM,
                         JC_SCRIPT_ERROR_BAD_TARGET, 0u, 0u,
                         "TTM start tag %u does not exist", (unsigned)start_tag);
    if (start_tag == 0u)
        (void)jc_ttm_find_tag(ttm, start_tag, &offset);
    memset(vm, 0, sizeof(*vm));
    initialize_thread(&vm->thread, ttm, 0u, start_tag, offset, 0u);
    vm->initialized = true;
    return true;
}

jc_script_tick_result_t jc_ttm_vm_tick(jc_ttm_vm_t *vm,
                                       jc_script_event_callback_t callback,
                                       void *userdata,
                                       jc_script_error_t *error)
{
    size_t budget = JC_SCRIPT_INSTRUCTION_BUDGET;
    ttm_outcome_t outcome;

    clear_error(error);
    if (vm == NULL || !vm->initialized) {
        set_error(error, JC_SCRIPT_DOMAIN_TTM_VM,
                  JC_SCRIPT_ERROR_NULL_ARGUMENT, 0u, 0u,
                  "TTM VM is not initialized");
        return JC_SCRIPT_TICK_ERROR;
    }
    if (vm->thread.state != 1u)
        return JC_SCRIPT_TICK_FINISHED;
    if (vm->thread.timer > 0u) {
        --vm->thread.timer;
        if (vm->thread.timer > 0u)
            return JC_SCRIPT_TICK_WAITING;
    }
    if (vm->thread.has_next_goto) {
        vm->thread.ip = vm->thread.next_goto;
        vm->thread.has_next_goto = false;
    }
    outcome = execute_ttm(&vm->thread, 0u, callback, userdata, &budget,
                          error);
    if (outcome == TTM_OUTCOME_ERROR)
        return JC_SCRIPT_TICK_ERROR;
    if (outcome == TTM_OUTCOME_FINISHED)
        return JC_SCRIPT_TICK_FINISHED;
    vm->thread.timer = vm->thread.delay;
    vm->thread.has_yielded = true;
    if (!emit_thread_event(JC_SCRIPT_EVENT_FRAME_READY, &vm->thread, 0u,
                           callback, userdata, error))
        return JC_SCRIPT_TICK_ERROR;
    return JC_SCRIPT_TICK_FRAME;
}

static bool is_scene_running(const jc_script_vm_t *vm, uint16_t slot,
                             uint16_t tag)
{
    size_t index;
    for (index = 0u; index < JC_SCRIPT_MAX_THREADS; ++index) {
        const jc_script_thread_t *thread = &vm->threads[index];
        if (thread->state == 1u && thread->scene_slot == slot &&
            thread->scene_tag == tag)
            return true;
    }
    return false;
}

static bool add_scene(jc_script_vm_t *vm, uint16_t slot, uint16_t tag,
                      uint16_t arg3, jc_script_event_callback_t callback,
                      void *userdata, jc_script_error_t *error)
{
    size_t index;
    size_t offset = 0u;

    if (slot >= JC_SCRIPT_MAX_TTM_SLOTS || vm->slots[slot] == NULL)
        return set_error(error, JC_SCRIPT_DOMAIN_ADS_VM,
                         JC_SCRIPT_ERROR_UNBOUND_RESOURCE, 0u, 0x2005u,
                         "ADS scene references unbound TTM slot %u",
                         (unsigned)slot);
    if (is_scene_running(vm, slot, tag))
        return true;
    if (tag != 0u && !jc_ttm_find_tag(vm->slots[slot], tag, &offset))
        return set_error(error, JC_SCRIPT_DOMAIN_ADS_VM,
                         JC_SCRIPT_ERROR_BAD_TARGET, 0u, 0x2005u,
                         "ADS scene references missing TTM tag %u in slot %u",
                         (unsigned)tag, (unsigned)slot);
    if (tag == 0u)
        (void)jc_ttm_find_tag(vm->slots[slot], tag, &offset);
    for (index = 0u; index < JC_SCRIPT_MAX_THREADS; ++index) {
        if (vm->threads[index].state == 0u)
            break;
    }
    if (index == JC_SCRIPT_MAX_THREADS)
        return set_error(error, JC_SCRIPT_DOMAIN_ADS_VM,
                         JC_SCRIPT_ERROR_LIMIT, 0u, 0x2005u,
                         "ADS exceeded the %u-thread runtime limit",
                         (unsigned)JC_SCRIPT_MAX_THREADS);
    initialize_thread(&vm->threads[index], vm->slots[slot], slot, tag,
                      offset, arg3);
    ++vm->active_threads;
    return emit_thread_event(JC_SCRIPT_EVENT_SCENE_STARTED,
                             &vm->threads[index], index, callback, userdata,
                             error);
}

static bool stop_thread(jc_script_vm_t *vm, size_t index,
                        jc_script_event_callback_t callback, void *userdata,
                        jc_script_error_t *error)
{
    if (vm->threads[index].state == 0u)
        return true;
    if (!emit_thread_event(JC_SCRIPT_EVENT_SCENE_STOPPED,
                           &vm->threads[index], index, callback, userdata,
                           error))
        return false;
    vm->threads[index].state = 0u;
    if (vm->active_threads > 0u)
        --vm->active_threads;
    return true;
}

static bool stop_by_tag(jc_script_vm_t *vm, uint16_t slot, uint16_t tag,
                        jc_script_event_callback_t callback, void *userdata,
                        jc_script_error_t *error)
{
    size_t index;
    for (index = 0u; index < JC_SCRIPT_MAX_THREADS; ++index) {
        if (vm->threads[index].state != 0u &&
            vm->threads[index].scene_slot == slot &&
            vm->threads[index].scene_tag == tag &&
            !stop_thread(vm, index, callback, userdata, error))
            return false;
    }
    return true;
}

static uint32_t next_random(jc_script_vm_t *vm)
{
    uint32_t value = vm->rng_state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    vm->rng_state = value;
    return value;
}

static bool execute_ads_chunk(jc_script_vm_t *vm, size_t start,
                              jc_script_event_callback_t callback,
                              void *userdata, size_t *budget,
                              jc_script_error_t *error);

static bool execute_random(jc_script_vm_t *vm,
                           jc_script_event_callback_t callback,
                           void *userdata, jc_script_error_t *error)
{
    uint32_t total = 0u;
    uint32_t choice;
    uint32_t partial = 0u;
    size_t index;
    const jc_script_random_op_t *selected = NULL;

    for (index = 0u; index < vm->random_op_count; ++index)
        total += vm->random_ops[index].weight;
    if (total == 0u)
        return true;
    choice = next_random(vm) % total;
    for (index = 0u; index < vm->random_op_count; ++index) {
        partial += vm->random_ops[index].weight;
        if (choice < partial) {
            selected = &vm->random_ops[index];
            break;
        }
    }
    if (selected == NULL)
        return true;
    if (selected->kind == 0u)
        return add_scene(vm, selected->slot, selected->tag, selected->plays,
                         callback, userdata, error);
    if (selected->kind == 1u)
        return stop_by_tag(vm, selected->slot, selected->tag, callback,
                           userdata, error);
    return true;
}

static bool add_random_op(jc_script_vm_t *vm, uint8_t kind, uint16_t slot,
                          uint16_t tag, uint16_t plays, uint16_t weight,
                          size_t offset, uint16_t opcode,
                          jc_script_error_t *error)
{
    jc_script_random_op_t *operation;
    if (vm->random_op_count >= JC_SCRIPT_MAX_RANDOM_OPS)
        return set_error(error, JC_SCRIPT_DOMAIN_ADS_VM,
                         JC_SCRIPT_ERROR_LIMIT, offset, opcode,
                         "ADS random block exceeds %u operations",
                         (unsigned)JC_SCRIPT_MAX_RANDOM_OPS);
    operation = &vm->random_ops[vm->random_op_count++];
    operation->kind = kind;
    operation->slot = slot;
    operation->tag = tag;
    operation->plays = plays;
    operation->weight = weight;
    return true;
}

static bool execute_ads_chunk(jc_script_vm_t *vm, size_t start,
                              jc_script_event_callback_t callback,
                              void *userdata, size_t *budget,
                              jc_script_error_t *error)
{
    size_t returns[JC_SCRIPT_MAX_GOSUB_DEPTH];
    size_t return_count = 0u;
    size_t offset = start;
    bool in_random = false;
    bool in_or = false;
    bool in_skip = false;
    bool in_local = false;

    for (;;) {
        jc_ads_instruction_t instruction;
        uint16_t a0;
        uint16_t a1;
        uint16_t a2;
        uint16_t a3;

        if (offset >= vm->ads->bytecode_size) {
            if (return_count > 0u) {
                offset = returns[--return_count];
                continue;
            }
            return true;
        }
        if (*budget == 0u)
            return set_error(error, JC_SCRIPT_DOMAIN_ADS_VM,
                             JC_SCRIPT_ERROR_INSTRUCTION_BUDGET, offset, 0u,
                             "ADS exceeded the per-tick instruction budget");
        --*budget;
        if (!jc_ads_instruction_at(vm->ads, offset, &instruction, error)) {
            if (error != NULL)
                error->domain = JC_SCRIPT_DOMAIN_ADS_VM;
            return false;
        }
        offset = instruction.next_offset;
        if (!emit_ads_instruction(&instruction, callback, userdata, error))
            return false;
        if (instruction.is_tag)
            continue;
        a0 = instruction.args[0];
        a1 = instruction.args[1];
        a2 = instruction.args[2];
        a3 = instruction.args[3];

        switch (instruction.opcode) {
        case 0x1070u:
            if (vm->local_trigger_count >= JC_SCRIPT_MAX_LOCAL_TRIGGERS)
                return set_error(error, JC_SCRIPT_DOMAIN_ADS_VM,
                                 JC_SCRIPT_ERROR_LIMIT, instruction.offset,
                                 instruction.opcode,
                                 "ADS local trigger limit exceeded");
            vm->local_triggers[vm->local_trigger_count].slot = a0;
            vm->local_triggers[vm->local_trigger_count].tag = a1;
            vm->local_triggers[vm->local_trigger_count].offset = offset;
            ++vm->local_trigger_count;
            in_local = true;
            break;
        case 0x1330u:
        case 0x1420u:
        case 0x2014u:
        case 0x4000u:
        case 0xf010u:
        case 0xfff0u:
            break;
        case 0x1350u:
            if (!in_or) {
                if (return_count > 0u) {
                    offset = returns[--return_count];
                    break;
                }
                return true;
            }
            in_or = false;
            break;
        case 0x1360u:
            if (is_scene_running(vm, a0, a1))
                in_skip = true;
            break;
        case 0x1370u:
            in_skip = !is_scene_running(vm, a0, a1);
            break;
        case 0x1430u:
            in_or = true;
            break;
        case 0x1510u:
            if (in_skip) {
                in_skip = false;
                break;
            }
            if (return_count > 0u) {
                offset = returns[--return_count];
                break;
            }
            return true;
        case 0x1520u:
            if (in_local)
                in_local = false;
            else if (!add_scene(vm, a1, a2, a3, callback, userdata, error))
                return false;
            break;
        case 0x2005u:
            if (!in_skip) {
                if (in_random) {
                    if (!add_random_op(vm, 0u, a0, a1, a2, a3,
                                       instruction.offset,
                                       instruction.opcode, error))
                        return false;
                } else if (!add_scene(vm, a0, a1, a2, callback, userdata,
                                      error))
                    return false;
            }
            break;
        case 0x2010u:
            if (!in_skip) {
                if (in_random) {
                    if (!add_random_op(vm, 1u, a0, a1, 0u, a2,
                                       instruction.offset,
                                       instruction.opcode, error))
                        return false;
                } else if (!stop_by_tag(vm, a0, a1, callback, userdata,
                                        error))
                    return false;
            }
            break;
        case 0x3010u:
            vm->random_op_count = 0u;
            in_random = true;
            break;
        case 0x3020u:
            if (in_random &&
                !add_random_op(vm, 2u, 0u, 0u, 0u, a0,
                               instruction.offset, instruction.opcode, error))
                return false;
            break;
        case 0x30ffu:
            if (!execute_random(vm, callback, userdata, error))
                return false;
            in_random = false;
            break;
        case 0xf200u:
            if (return_count >= JC_SCRIPT_MAX_GOSUB_DEPTH)
                return set_error(error, JC_SCRIPT_DOMAIN_ADS_VM,
                                 JC_SCRIPT_ERROR_LIMIT, instruction.offset,
                                 instruction.opcode,
                                 "ADS GOSUB depth exceeds %u",
                                 (unsigned)JC_SCRIPT_MAX_GOSUB_DEPTH);
            if (!jc_ads_find_tag(vm->ads, a0, &start))
                return set_error(error, JC_SCRIPT_DOMAIN_ADS_VM,
                                 JC_SCRIPT_ERROR_BAD_TARGET,
                                 instruction.offset, instruction.opcode,
                                 "ADS GOSUB tag %u does not exist", (unsigned)a0);
            returns[return_count++] = offset;
            offset = start;
            break;
        case 0xffffu:
            if (in_skip)
                in_skip = false;
            else
                vm->stop_requested = true;
            break;
        default:
            return set_error(error, JC_SCRIPT_DOMAIN_ADS_VM,
                             JC_SCRIPT_ERROR_UNKNOWN_OPCODE,
                             instruction.offset, instruction.opcode,
                             "unknown ADS opcode reached runtime");
        }
    }
}

static bool play_triggered(jc_script_vm_t *vm, uint16_t slot, uint16_t tag,
                           jc_script_event_callback_t callback,
                           void *userdata, size_t *budget,
                           jc_script_error_t *error)
{
    size_t starts[JC_SCRIPT_MAX_TRIGGERS];
    size_t start_count = 0u;
    size_t index;

    if (vm->local_trigger_count > 0u) {
        size_t write_index = 0u;
        for (index = 0u; index < vm->local_trigger_count; ++index) {
            jc_script_trigger_t trigger = vm->local_triggers[index];
            if (trigger.slot == slot && trigger.tag == tag)
                starts[start_count++] = trigger.offset;
            else
                vm->local_triggers[write_index++] = trigger;
        }
        vm->local_trigger_count = write_index;
    } else {
        for (index = 0u; index < vm->trigger_count; ++index) {
            if (vm->triggers[index].slot == slot &&
                vm->triggers[index].tag == tag)
                starts[start_count++] = vm->triggers[index].offset;
        }
    }
    for (index = 0u; index < start_count; ++index) {
        if (!execute_ads_chunk(vm, starts[index], callback, userdata, budget,
                               error))
            return false;
    }
    return true;
}

static bool finish_thread(jc_script_vm_t *vm, size_t index,
                          jc_script_event_callback_t callback, void *userdata,
                          size_t *budget, jc_script_error_t *error)
{
    jc_script_thread_t *thread = &vm->threads[index];
    uint16_t slot;
    uint16_t tag;
    size_t offset;

    if (thread->scene_iterations > 0u) {
        --thread->scene_iterations;
        if (thread->scene_tag != 0u &&
            !jc_ttm_find_tag(thread->ttm, thread->scene_tag, &offset))
            return set_error(error, JC_SCRIPT_DOMAIN_TTM_VM,
                             JC_SCRIPT_ERROR_BAD_TARGET, thread->ip, 0u,
                             "replay TTM tag disappeared");
        if (thread->scene_tag == 0u) {
            offset = 0u;
            (void)jc_ttm_find_tag(thread->ttm, 0u, &offset);
        }
        thread->state = 1u;
        thread->ip = offset;
        thread->timer = 0u;
        thread->has_next_goto = false;
        thread->has_yielded = false;
        return true;
    }
    slot = thread->scene_slot;
    tag = thread->scene_tag;
    if (!stop_thread(vm, index, callback, userdata, error))
        return false;
    if (!vm->stop_requested)
        return play_triggered(vm, slot, tag, callback, userdata, budget,
                              error);
    return true;
}

bool jc_script_vm_init(jc_script_vm_t *vm, const jc_ads_t *ads,
                       uint32_t seed, jc_script_error_t *error)
{
    clear_error(error);
    if (vm == NULL || ads == NULL)
        return set_error(error, JC_SCRIPT_DOMAIN_ADS_VM,
                         JC_SCRIPT_ERROR_NULL_ARGUMENT, 0u, 0u,
                         "script VM init received a null argument");
    memset(vm, 0, sizeof(*vm));
    vm->ads = ads;
    vm->rng_state = seed != 0u ? seed : 0x6d2b79f5u;
    vm->initialized = true;
    return true;
}

bool jc_script_vm_bind_ttm(jc_script_vm_t *vm, uint16_t slot,
                           const jc_ttm_t *ttm, jc_script_error_t *error)
{
    clear_error(error);
    if (vm == NULL || !vm->initialized || ttm == NULL)
        return set_error(error, JC_SCRIPT_DOMAIN_ADS_VM,
                         JC_SCRIPT_ERROR_NULL_ARGUMENT, 0u, 0u,
                         "TTM binding received an invalid argument");
    if (slot >= JC_SCRIPT_MAX_TTM_SLOTS)
        return set_error(error, JC_SCRIPT_DOMAIN_ADS_VM,
                         JC_SCRIPT_ERROR_BAD_OPERAND, 0u, 0u,
                         "TTM slot %u exceeds limit %u", (unsigned)slot,
                         (unsigned)JC_SCRIPT_MAX_TTM_SLOTS);
    vm->slots[slot] = ttm;
    return true;
}

static bool add_trigger(jc_script_vm_t *vm,
                        const jc_ads_instruction_t *instruction,
                        jc_script_error_t *error)
{
    jc_script_trigger_t *trigger;
    if (vm->trigger_count >= JC_SCRIPT_MAX_TRIGGERS)
        return set_error(error, JC_SCRIPT_DOMAIN_ADS_VM,
                         JC_SCRIPT_ERROR_LIMIT, instruction->offset,
                         instruction->opcode,
                         "ADS trigger count exceeds %u",
                         (unsigned)JC_SCRIPT_MAX_TRIGGERS);
    trigger = &vm->triggers[vm->trigger_count++];
    trigger->slot = instruction->args[0];
    trigger->tag = instruction->args[1];
    trigger->offset = instruction->next_offset;
    return true;
}

bool jc_script_vm_start(jc_script_vm_t *vm, uint16_t start_tag,
                        jc_script_event_callback_t callback, void *userdata,
                        jc_script_error_t *error)
{
    size_t start;
    size_t offset = 0u;
    size_t budget = JC_SCRIPT_INSTRUCTION_BUDGET;
    bool bookmarking = false;
    bool bookmark_if_not_running = false;

    clear_error(error);
    if (vm == NULL || !vm->initialized)
        return set_error(error, JC_SCRIPT_DOMAIN_ADS_VM,
                         JC_SCRIPT_ERROR_NULL_ARGUMENT, 0u, 0u,
                         "script VM is not initialized");
    if (!jc_ads_find_tag(vm->ads, start_tag, &start))
        return set_error(error, JC_SCRIPT_DOMAIN_ADS_VM,
                         JC_SCRIPT_ERROR_BAD_TARGET, 0u, 0u,
                         "ADS start tag %u does not exist", (unsigned)start_tag);

    vm->trigger_count = 0u;
    vm->local_trigger_count = 0u;
    vm->stop_requested = false;
    while (offset < vm->ads->bytecode_size) {
        jc_ads_instruction_t instruction;
        if (budget == 0u)
            return set_error(error, JC_SCRIPT_DOMAIN_ADS_VM,
                             JC_SCRIPT_ERROR_INSTRUCTION_BUDGET, offset, 0u,
                             "ADS trigger scan exceeded its instruction budget");
        --budget;
        if (!jc_ads_instruction_at(vm->ads, offset, &instruction, error))
            return false;
        offset = instruction.next_offset;
        if (instruction.is_tag) {
            bookmarking = instruction.opcode == start_tag;
            bookmark_if_not_running = bookmarking;
        } else if (bookmarking && instruction.opcode == 0x1350u) {
            bookmark_if_not_running = false;
            if (!add_trigger(vm, &instruction, error))
                return false;
        } else if (bookmarking && bookmark_if_not_running &&
                   instruction.opcode == 0x1360u) {
            if (!add_trigger(vm, &instruction, error))
                return false;
        } else if (bookmarking && instruction.opcode == 0x1370u) {
            bookmark_if_not_running = false;
        }
    }

    vm->started = true;
    budget = JC_SCRIPT_INSTRUCTION_BUDGET;
    if (!execute_ads_chunk(vm, start, callback, userdata, &budget, error)) {
        vm->started = false;
        return false;
    }
    return true;
}

jc_script_tick_result_t jc_script_vm_tick(jc_script_vm_t *vm,
                                          jc_script_event_callback_t callback,
                                          void *userdata,
                                          jc_script_error_t *error)
{
    size_t index;
    size_t budget = JC_SCRIPT_INSTRUCTION_BUDGET;
    bool frame_ready = false;

    clear_error(error);
    if (vm == NULL || !vm->initialized || !vm->started) {
        set_error(error, JC_SCRIPT_DOMAIN_ADS_VM,
                  JC_SCRIPT_ERROR_NULL_ARGUMENT, 0u, 0u,
                  "script VM is not started");
        return JC_SCRIPT_TICK_ERROR;
    }
    if (vm->active_threads == 0u)
        return JC_SCRIPT_TICK_FINISHED;

    for (index = 0u; index < JC_SCRIPT_MAX_THREADS; ++index) {
        jc_script_thread_t *thread = &vm->threads[index];
        ttm_outcome_t outcome;

        if (thread->state == 0u)
            continue;
        if (thread->timer > 0u) {
            --thread->timer;
            if (thread->timer > 0u)
                continue;
        }
        if (thread->state == 2u) {
            if (!finish_thread(vm, index, callback, userdata, &budget, error))
                return JC_SCRIPT_TICK_ERROR;
            continue;
        }
        if (thread->has_next_goto) {
            thread->ip = thread->next_goto;
            thread->has_next_goto = false;
        }
        if (thread->scene_timer > 0 && thread->has_yielded) {
            thread->scene_timer -= thread->delay;
            if (thread->scene_timer <= 0) {
                thread->state = 2u;
                if (!finish_thread(vm, index, callback, userdata, &budget,
                                   error))
                    return JC_SCRIPT_TICK_ERROR;
                continue;
            }
        }
        outcome = execute_ttm(thread, index, callback, userdata, &budget,
                              error);
        if (outcome == TTM_OUTCOME_ERROR)
            return JC_SCRIPT_TICK_ERROR;
        if (outcome == TTM_OUTCOME_FRAME) {
            thread->timer = thread->delay;
            thread->has_yielded = true;
            frame_ready = true;
            if (!emit_thread_event(JC_SCRIPT_EVENT_FRAME_READY, thread, index,
                                   callback, userdata, error))
                return JC_SCRIPT_TICK_ERROR;
        } else {
            thread->state = 2u;
            thread->timer = thread->delay;
            if (thread->timer == 0u &&
                !finish_thread(vm, index, callback, userdata, &budget, error))
                return JC_SCRIPT_TICK_ERROR;
        }
    }
    ++vm->tick_count;
    if (vm->active_threads == 0u)
        return JC_SCRIPT_TICK_FINISHED;
    return frame_ready ? JC_SCRIPT_TICK_FRAME : JC_SCRIPT_TICK_WAITING;
}

size_t jc_script_vm_active_threads(const jc_script_vm_t *vm)
{
    return vm != NULL ? vm->active_threads : 0u;
}
