/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Archive-to-ADS runtime independently rewritten from the resource/slot
 * orchestration in Wilson Reborn (wilson-engine/src/ads_vm.rs) and Johnny
 * Reborn (ads.c/resource.c). No upstream source text is copied; both
 * references are GPL-3.0-or-later.
 */
#include "jc_runtime.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void clear_error(jc_script_error_t *error)
{
    if (error != NULL)
        memset(error, 0, sizeof(*error));
}

static bool set_error(jc_script_error_t *error,
                      jc_script_error_code_t code,
                      const char *format, ...)
{
    va_list args;

    if (error == NULL)
        return false;
    memset(error, 0, sizeof(*error));
    error->code = code;
    error->domain = JC_SCRIPT_DOMAIN_ADS_VM;
    va_start(args, format);
    vsnprintf(error->message, sizeof(error->message), format, args);
    va_end(args);
    return false;
}

static void clear_scene(jc_runtime_t *runtime)
{
    size_t slot;

    free(runtime->ads_bytecode);
    runtime->ads_bytecode = NULL;
    memset(&runtime->ads, 0, sizeof(runtime->ads));
    for (slot = 0u; slot < JC_SCRIPT_MAX_TTM_SLOTS; ++slot) {
        free(runtime->ttm_bytecode[slot]);
        runtime->ttm_bytecode[slot] = NULL;
        runtime->ttm_loaded[slot] = false;
        memset(&runtime->ttms[slot], 0, sizeof(runtime->ttms[slot]));
    }
    memset(&runtime->vm, 0, sizeof(runtime->vm));
    runtime->total_script_bytes = 0u;
    runtime->scene_loaded = false;
    runtime->scene_finished = false;
}

static bool content_resource_load(void *userdata, const char *name,
                                  const uint8_t **data, size_t *size,
                                  char *error, size_t error_size)
{
    jc_runtime_t *runtime = (jc_runtime_t *)userdata;
    jc_resource_info_t info;
    uint8_t *storage;

    if (data != NULL)
        *data = NULL;
    if (size != NULL)
        *size = 0u;
    if (runtime == NULL || runtime->content == NULL ||
        !runtime->content->ready || name == NULL || data == NULL ||
        size == NULL) {
        if (error != NULL && error_size > 0u)
            snprintf(error, error_size, "runtime resource request is invalid");
        return false;
    }
    if (!jc_content_find_resource(runtime->content, name, runtime->vfs,
                                  &info, error, error_size))
        return false;
    if (info.body_size == 0u ||
        info.body_size > JC_RUNTIME_MAX_RESOURCE_BYTES) {
        if (error != NULL && error_size > 0u)
            snprintf(error, error_size,
                     "resource body is empty or exceeds the runtime limit");
        return false;
    }
    storage = (uint8_t *)malloc(info.body_size);
    if (storage == NULL) {
        if (error != NULL && error_size > 0u)
            snprintf(error, error_size,
                     "could not allocate bounded resource storage");
        return false;
    }
    if (!jc_content_read_resource(runtime->content, &info, runtime->vfs,
                                  storage, info.body_size,
                                  error, error_size)) {
        free(storage);
        return false;
    }
    *data = storage;
    *size = info.body_size;
    return true;
}

static void content_resource_release(void *userdata, const uint8_t *data,
                                     size_t size)
{
    (void)userdata;
    (void)size;
    free((void *)data);
}

static bool initialize_renderer(jc_runtime_t *runtime,
                                jc_script_error_t *error)
{
    jc_ttm_renderer_resources_t resources;

    resources.load = content_resource_load;
    resources.release = content_resource_release;
    resources.userdata = runtime;
    return jc_ttm_renderer_init(&runtime->renderer, runtime->width,
                                runtime->height,
                                runtime->transparent_source_index,
                                &resources, runtime->event_callback,
                                runtime->event_userdata, error);
}

bool jc_runtime_init(jc_runtime_t *runtime,
                     const jc_runtime_config_t *config,
                     jc_script_error_t *error)
{
    clear_error(error);
    if (runtime == NULL || config == NULL || config->content == NULL ||
        !config->content->ready)
        return set_error(error, JC_SCRIPT_ERROR_NULL_ARGUMENT,
                         "runtime init requires loaded content");

    memset(runtime, 0, sizeof(*runtime));
    runtime->content = config->content;
    runtime->vfs = config->vfs;
    runtime->width = config->width;
    runtime->height = config->height;
    runtime->transparent_source_index = config->transparent_source_index;
    runtime->event_callback = config->event_callback;
    runtime->event_userdata = config->event_userdata;
    if (!initialize_renderer(runtime, error))
        return false;
    runtime->initialized = true;
    return true;
}

void jc_runtime_destroy(jc_runtime_t *runtime)
{
    if (runtime == NULL)
        return;
    clear_scene(runtime);
    jc_ttm_renderer_destroy(&runtime->renderer);
    memset(runtime, 0, sizeof(*runtime));
}

bool jc_runtime_reset(jc_runtime_t *runtime, jc_script_error_t *error)
{
    clear_error(error);
    if (runtime == NULL || !runtime->initialized)
        return set_error(error, JC_SCRIPT_ERROR_NULL_ARGUMENT,
                         "runtime reset requires an initialized runtime");
    clear_scene(runtime);
    jc_ttm_renderer_destroy(&runtime->renderer);
    if (!initialize_renderer(runtime, error)) {
        runtime->initialized = false;
        return false;
    }
    return true;
}

static bool load_resource_body(jc_runtime_t *runtime, const char *name,
                               uint8_t **data, size_t *size,
                               jc_script_error_t *error)
{
    jc_resource_info_t info;
    uint8_t *storage;
    char message[JC_SCRIPT_ERROR_MESSAGE_BYTES];

    *data = NULL;
    *size = 0u;
    message[0] = '\0';
    if (!jc_content_find_resource(runtime->content, name, runtime->vfs,
                                  &info, message, sizeof(message)))
        return set_error(error, JC_SCRIPT_ERROR_UNBOUND_RESOURCE,
                         "could not load %s: %s", name,
                         message[0] != '\0' ? message : "resource error");
    if (info.body_size == 0u)
        return set_error(error, JC_SCRIPT_ERROR_BAD_SIZE,
                         "%s has an empty resource body", name);
    if (info.body_size > JC_RUNTIME_MAX_RESOURCE_BYTES)
        return set_error(error, JC_SCRIPT_ERROR_LIMIT,
                         "%s exceeds the runtime resource limit", name);
    storage = (uint8_t *)malloc(info.body_size);
    if (storage == NULL)
        return set_error(error, JC_SCRIPT_ERROR_LIMIT,
                         "could not allocate bounded storage for %s", name);
    if (!jc_content_read_resource(runtime->content, &info, runtime->vfs,
                                  storage, info.body_size,
                                  message, sizeof(message))) {
        free(storage);
        return set_error(error, JC_SCRIPT_ERROR_UNBOUND_RESOURCE,
                         "could not read %s: %s", name,
                         message[0] != '\0' ? message : "resource error");
    }
    *data = storage;
    *size = info.body_size;
    return true;
}

static uint16_t read_le16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool validate_decoded_size(jc_runtime_t *runtime, size_t replacing,
                                  uint32_t decoded,
                                  jc_script_error_t *error)
{
    size_t retained;

    if (decoded > JC_RUNTIME_MAX_DECODED_SCRIPT_BYTES)
        return set_error(error, JC_SCRIPT_ERROR_LIMIT,
                         "decoded script exceeds the per-resource limit");
    if (replacing > runtime->total_script_bytes)
        return set_error(error, JC_SCRIPT_ERROR_LIMIT,
                         "decoded script accounting is invalid");
    retained = runtime->total_script_bytes - replacing;
    if (retained > JC_RUNTIME_MAX_TOTAL_SCRIPT_BYTES ||
        decoded > JC_RUNTIME_MAX_TOTAL_SCRIPT_BYTES - retained)
        return set_error(error, JC_SCRIPT_ERROR_LIMIT,
                         "ADS/TTM decoded data exceeds the aggregate limit");
    return true;
}

static bool ads_decoded_size(const uint8_t *data, size_t size,
                             uint32_t *decoded, jc_script_error_t *error)
{
    size_t position = 31u;
    uint16_t count;
    size_t index;

    if (size < position || memcmp(data, "VER:", 4u) != 0 ||
        memcmp(data + 13u, "ADS:", 4u) != 0 ||
        memcmp(data + 21u, "RES:", 4u) != 0)
        return set_error(error, JC_SCRIPT_ERROR_TRUNCATED,
                         "ADS is truncated before its resource table");
    count = read_le16(data + 29u);
    if (count > JC_ADS_MAX_RESOURCES)
        return set_error(error, JC_SCRIPT_ERROR_LIMIT,
                         "ADS resource table exceeds the runtime limit");
    for (index = 0u; index < count; ++index) {
        size_t length;

        if (size - position < 3u)
            return set_error(error, JC_SCRIPT_ERROR_TRUNCATED,
                             "ADS resource table is truncated");
        position += 2u;
        for (length = 0u; length < JC_ADS_NAME_BYTES; ++length) {
            if (length >= size - position)
                return set_error(error, JC_SCRIPT_ERROR_TRUNCATED,
                                 "ADS resource name is truncated");
            if (data[position + length] == 0u)
                break;
        }
        if (length == JC_ADS_NAME_BYTES)
            return set_error(error, JC_SCRIPT_ERROR_BAD_SIZE,
                             "ADS resource name is not terminated");
        position += length + 1u;
    }
    if (size - position < 13u || memcmp(data + position, "SCR:", 4u) != 0)
        return set_error(error, JC_SCRIPT_ERROR_TRUNCATED,
                         "ADS is truncated before its packed script");
    *decoded = read_le32(data + position + 9u);
    return true;
}

static bool ttm_decoded_size(const uint8_t *data, size_t size,
                             uint32_t *decoded, jc_script_error_t *error)
{
    if (size < 36u || memcmp(data, "VER:", 4u) != 0 ||
        memcmp(data + 13u, "PAG:", 4u) != 0 ||
        memcmp(data + 23u, "TT3:", 4u) != 0)
        return set_error(error, JC_SCRIPT_ERROR_TRUNCATED,
                         "TTM is truncated before its packed script");
    *decoded = read_le32(data + 32u);
    return true;
}

static bool load_ads(jc_runtime_t *runtime, const char *name,
                     jc_script_error_t *error)
{
    jc_ads_t parsed;
    uint8_t *body = NULL;
    uint8_t *temporary = NULL;
    size_t body_size = 0u;
    uint32_t decoded_size = 0u;
    bool success = false;

    if (!load_resource_body(runtime, name, &body, &body_size, error))
        return false;
    if (!ads_decoded_size(body, body_size, &decoded_size, error) ||
        !validate_decoded_size(runtime, 0u, decoded_size, error))
        goto done;
    temporary = (uint8_t *)malloc(decoded_size > 0u ? decoded_size : 1u);
    if (temporary == NULL) {
        set_error(error, JC_SCRIPT_ERROR_LIMIT,
                  "could not allocate bounded ADS decode storage");
        goto done;
    }
    if (!jc_ads_parse(&parsed, body, body_size, temporary,
                      decoded_size, error))
        goto done;
    parsed.bytecode = temporary;
    runtime->ads = parsed;
    runtime->ads_bytecode = temporary;
    runtime->total_script_bytes += parsed.bytecode_size;
    temporary = NULL;
    success = true;

done:
    free(temporary);
    free(body);
    return success;
}

static bool load_ttm(jc_runtime_t *runtime, uint16_t slot,
                     const char *name, jc_script_error_t *error)
{
    jc_ttm_t parsed;
    uint8_t *body = NULL;
    uint8_t *temporary = NULL;
    size_t body_size = 0u;
    uint32_t decoded_size = 0u;
    size_t replacing = 0u;
    bool success = false;

    if (slot >= JC_SCRIPT_MAX_TTM_SLOTS)
        return set_error(error, JC_SCRIPT_ERROR_BAD_OPERAND,
                         "ADS TTM slot %u exceeds the runtime limit",
                         (unsigned)slot);
    if (!load_resource_body(runtime, name, &body, &body_size, error))
        return false;
    if (runtime->ttm_loaded[slot])
        replacing = runtime->ttms[slot].bytecode_size;
    if (!ttm_decoded_size(body, body_size, &decoded_size, error) ||
        !validate_decoded_size(runtime, replacing, decoded_size, error))
        goto done;
    temporary = (uint8_t *)malloc(decoded_size > 0u ? decoded_size : 1u);
    if (temporary == NULL) {
        set_error(error, JC_SCRIPT_ERROR_LIMIT,
                  "could not allocate bounded TTM decode storage");
        goto done;
    }
    if (!jc_ttm_parse(&parsed, body, body_size, temporary,
                      decoded_size, error))
        goto done;
    if (runtime->ttm_loaded[slot]) {
        runtime->total_script_bytes -= runtime->ttms[slot].bytecode_size;
        free(runtime->ttm_bytecode[slot]);
        runtime->ttm_bytecode[slot] = NULL;
        runtime->ttm_loaded[slot] = false;
        memset(&runtime->ttms[slot], 0, sizeof(runtime->ttms[slot]));
    }
    parsed.bytecode = temporary;
    runtime->ttms[slot] = parsed;
    runtime->ttm_bytecode[slot] = temporary;
    runtime->ttm_loaded[slot] = true;
    runtime->total_script_bytes += parsed.bytecode_size;
    temporary = NULL;
    success = true;

done:
    free(temporary);
    free(body);
    return success;
}

static void rollback_start(jc_runtime_t *runtime)
{
    jc_script_error_t ignored;

    (void)jc_runtime_reset(runtime, &ignored);
}

bool jc_runtime_start_ads(jc_runtime_t *runtime, const char *ads_name,
                          uint16_t start_tag, uint32_t seed,
                          jc_script_error_t *error)
{
    jc_script_error_t saved_error;
    size_t index;

    clear_error(error);
    if (runtime == NULL || !runtime->initialized || ads_name == NULL ||
        ads_name[0] == '\0')
        return set_error(error, JC_SCRIPT_ERROR_NULL_ARGUMENT,
                         "runtime start received invalid input");
    if (strlen(ads_name) > JC_RUNTIME_NAME_BYTES)
        return set_error(error, JC_SCRIPT_ERROR_LIMIT,
                         "ADS resource name exceeds the runtime limit");
    if (!jc_runtime_reset(runtime, error))
        return false;
    if (!load_ads(runtime, ads_name, error))
        goto failed;
    for (index = 0u; index < runtime->ads.resource_count; ++index) {
        const jc_ads_resource_t *resource = &runtime->ads.resources[index];
        if (!load_ttm(runtime, resource->id, resource->name, error))
            goto failed;
    }
    if (!jc_script_vm_init(&runtime->vm, &runtime->ads, seed, error))
        goto failed;
    for (index = 0u; index < JC_SCRIPT_MAX_TTM_SLOTS; ++index) {
        if (runtime->ttm_loaded[index] &&
            !jc_script_vm_bind_ttm(&runtime->vm, (uint16_t)index,
                                   &runtime->ttms[index], error))
            goto failed;
    }
    if (!jc_script_vm_start(&runtime->vm, start_tag,
                            jc_ttm_renderer_event, &runtime->renderer,
                            error))
        goto failed;
    runtime->scene_loaded = true;
    runtime->scene_finished = false;
    return true;

failed:
    if (error != NULL)
        saved_error = *error;
    rollback_start(runtime);
    if (error != NULL)
        *error = saved_error;
    return false;
}

jc_script_tick_result_t jc_runtime_tick(jc_runtime_t *runtime,
                                        jc_script_error_t *error)
{
    jc_script_tick_result_t result;

    clear_error(error);
    if (runtime == NULL || !runtime->initialized || !runtime->scene_loaded) {
        set_error(error, JC_SCRIPT_ERROR_NULL_ARGUMENT,
                  "runtime tick requires a loaded ADS scene");
        return JC_SCRIPT_TICK_ERROR;
    }
    if (runtime->scene_finished)
        return JC_SCRIPT_TICK_FINISHED;
    result = jc_script_vm_tick(&runtime->vm, jc_ttm_renderer_event,
                               &runtime->renderer, error);
    if (result == JC_SCRIPT_TICK_FINISHED)
        runtime->scene_finished = true;
    return result;
}

bool jc_runtime_is_active(const jc_runtime_t *runtime)
{
    return runtime != NULL && runtime->initialized && runtime->scene_loaded &&
           !runtime->scene_finished &&
           jc_script_vm_active_threads(&runtime->vm) > 0u;
}

const jc_surface_t *jc_runtime_output(const jc_runtime_t *runtime)
{
    if (runtime == NULL || !runtime->initialized)
        return NULL;
    return jc_ttm_renderer_output(&runtime->renderer);
}

const jc_palette_t *jc_runtime_palette(const jc_runtime_t *runtime)
{
    if (runtime == NULL || !runtime->initialized)
        return NULL;
    return jc_ttm_renderer_palette(&runtime->renderer);
}
