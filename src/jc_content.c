/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "jc_content.h"
#include "libretro.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JC_MAP_MAX_BYTES (21u + JC_RESOURCE_MAP_MAX_ENTRIES * 8u)

typedef struct jc_file {
    const struct retro_vfs_interface *vfs;
    struct retro_vfs_file_handle *vfs_handle;
    FILE *stdio_handle;
} jc_file_t;

static bool fail(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size > 0u)
        snprintf(error, error_size, "%s", message);
    return false;
}

static bool path_has_extension(const char *path, const char *extension)
{
    size_t path_length = strlen(path);
    size_t extension_length = strlen(extension);
    size_t index;

    if (path_length < extension_length)
        return false;
    path += path_length - extension_length;
    for (index = 0u; index < extension_length; ++index) {
        char left = path[index];
        char right = extension[index];
        if (left >= 'A' && left <= 'Z')
            left = (char)(left - 'A' + 'a');
        if (right >= 'A' && right <= 'Z')
            right = (char)(right - 'A' + 'a');
        if (left != right)
            return false;
    }
    return true;
}

static bool sibling_path(char *output, size_t output_size,
                         const char *path, const char *name)
{
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    const char *separator = slash;
    size_t prefix_length;
    size_t name_length = strlen(name);

    if (backslash != NULL && (separator == NULL || backslash > separator))
        separator = backslash;
    prefix_length = separator == NULL ? 0u : (size_t)(separator - path + 1);
    if (prefix_length + name_length + 1u > output_size)
        return false;
    memcpy(output, path, prefix_length);
    memcpy(output + prefix_length, name, name_length + 1u);
    return true;
}

static bool file_open(jc_file_t *file, const char *path,
                      const struct retro_vfs_interface *vfs)
{
    memset(file, 0, sizeof(*file));
    if (vfs != NULL && vfs->open != NULL) {
        file->vfs = vfs;
        file->vfs_handle = vfs->open(path, RETRO_VFS_FILE_ACCESS_READ,
                                     RETRO_VFS_FILE_ACCESS_HINT_NONE);
        return file->vfs_handle != NULL;
    }
    file->stdio_handle = fopen(path, "rb");
    return file->stdio_handle != NULL;
}

static void file_close(jc_file_t *file)
{
    if (file->vfs_handle != NULL)
        file->vfs->close(file->vfs_handle);
    if (file->stdio_handle != NULL)
        fclose(file->stdio_handle);
    memset(file, 0, sizeof(*file));
}

static int64_t file_size(jc_file_t *file)
{
    long size;

    if (file->vfs_handle != NULL)
        return file->vfs->size(file->vfs_handle);
    if (fseek(file->stdio_handle, 0, SEEK_END) != 0)
        return -1;
    size = ftell(file->stdio_handle);
    if (size < 0 || fseek(file->stdio_handle, 0, SEEK_SET) != 0)
        return -1;
    return (int64_t)size;
}

static bool file_read_exact(jc_file_t *file, void *data, size_t size)
{
    if (file->vfs_handle != NULL)
        return file->vfs->read(file->vfs_handle, data, size) == (int64_t)size;
    return fread(data, 1u, size, file->stdio_handle) == size;
}

static bool file_seek(jc_file_t *file, uint64_t offset)
{
    if (offset > 0x7fffffffffffffffull)
        return false;
    if (file->vfs_handle != NULL)
        return file->vfs->seek(file->vfs_handle, (int64_t)offset,
                               RETRO_VFS_SEEK_POSITION_START) >= 0;
    if (offset > (uint64_t)LONG_MAX)
        return false;
    return fseek(file->stdio_handle, (long)offset, SEEK_SET) == 0;
}

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool resource_name_equal(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0') {
        char a = *left++;
        char b = *right++;
        if (a >= 'a' && a <= 'z')
            a = (char)(a - 'a' + 'A');
        if (b >= 'a' && b <= 'z')
            b = (char)(b - 'a' + 'A');
        if (a != b)
            return false;
    }
    return *left == '\0' && *right == '\0';
}

static bool read_resource_info(jc_file_t *file, const jc_content_t *content,
                               unsigned index, jc_resource_info_t *resource,
                               char *error, size_t error_size)
{
    uint8_t header[JC_RESOURCE_NAME_BYTES + 4u];
    uint64_t offset;

    if (index >= content->map.entry_count)
        return fail(error, error_size, "resource index is out of range");
    offset = content->map.entries[index].offset;
    if (!file_seek(file, offset) || !file_read_exact(file, header, sizeof(header)))
        return fail(error, error_size, "could not read an archive entry header");
    if (memchr(header, '\0', JC_RESOURCE_NAME_BYTES) == NULL)
        return fail(error, error_size, "archive entry name is not terminated");

    memset(resource, 0, sizeof(*resource));
    memcpy(resource->name, header, JC_RESOURCE_NAME_BYTES);
    resource->body_size = read_le32(header + JC_RESOURCE_NAME_BYTES);
    resource->body_offset = offset + sizeof(header);
    resource->map_index = index;
    if (resource->body_offset + resource->body_size > content->archive_size)
        return fail(error, error_size, "archive resource body exceeds the file");
    return true;
}

bool jc_content_load(jc_content_t *content, const char *content_path,
                     const struct retro_vfs_interface *vfs,
                     char *error, size_t error_size)
{
    uint8_t map_bytes[JC_MAP_MAX_BYTES];
    jc_file_t map_file;
    jc_file_t archive_file;
    int64_t map_size;
    int64_t archive_size;

    if (content == NULL || content_path == NULL || content_path[0] == '\0')
        return fail(error, error_size, "no Johnny Castaway content path was supplied");
    memset(content, 0, sizeof(*content));

    if (path_has_extension(content_path, ".001")) {
        if (!sibling_path(content->map_path, sizeof(content->map_path),
                          content_path, "RESOURCE.MAP"))
            return fail(error, error_size, "content path is too long");
    } else if (snprintf(content->map_path, sizeof(content->map_path), "%s",
                        content_path) >= (int)sizeof(content->map_path)) {
        return fail(error, error_size, "content path is too long");
    }

    if (!file_open(&map_file, content->map_path, vfs))
        return fail(error, error_size, "could not open RESOURCE.MAP");
    map_size = file_size(&map_file);
    if (map_size < 0 || (uint64_t)map_size > sizeof(map_bytes)) {
        file_close(&map_file);
        return fail(error, error_size, "RESOURCE.MAP has an invalid size");
    }
    if (!file_read_exact(&map_file, map_bytes, (size_t)map_size)) {
        file_close(&map_file);
        return fail(error, error_size, "could not read RESOURCE.MAP");
    }
    file_close(&map_file);

    if (!jc_resource_map_parse(&content->map, map_bytes, (size_t)map_size,
                               error, error_size))
        return false;
    if (!sibling_path(content->archive_path, sizeof(content->archive_path),
                      content->map_path, content->map.archive_name))
        return fail(error, error_size, "archive path is too long");
    if (!file_open(&archive_file, content->archive_path, vfs))
        return fail(error, error_size, "could not open the RESOURCE.001 archive");
    archive_size = file_size(&archive_file);
    file_close(&archive_file);
    if (archive_size < 0)
        return fail(error, error_size, "could not determine archive size");
    if (!jc_resource_map_validate_archive(&content->map, (uint64_t)archive_size,
                                          error, error_size))
        return false;

    content->archive_size = (uint64_t)archive_size;
    content->ready = true;
    if (error != NULL && error_size > 0u)
        error[0] = '\0';
    return true;
}

void jc_content_unload(jc_content_t *content)
{
    if (content != NULL)
        memset(content, 0, sizeof(*content));
}

bool jc_content_find_resource(const jc_content_t *content, const char *name,
                              const struct retro_vfs_interface *vfs,
                              jc_resource_info_t *resource,
                              char *error, size_t error_size)
{
    jc_file_t archive;
    unsigned index;

    if (content == NULL || !content->ready || name == NULL || resource == NULL)
        return fail(error, error_size, "resource lookup received invalid input");
    if (!file_open(&archive, content->archive_path, vfs))
        return fail(error, error_size, "could not reopen the resource archive");
    for (index = 0u; index < content->map.entry_count; ++index) {
        jc_resource_info_t candidate;
        if (!read_resource_info(&archive, content, index, &candidate,
                                error, error_size)) {
            file_close(&archive);
            return false;
        }
        if (resource_name_equal(candidate.name, name)) {
            *resource = candidate;
            file_close(&archive);
            return true;
        }
    }
    file_close(&archive);
    return fail(error, error_size, "requested resource was not found in the archive");
}

bool jc_content_read_resource(const jc_content_t *content,
                              const jc_resource_info_t *resource,
                              const struct retro_vfs_interface *vfs,
                              uint8_t *data, size_t size,
                              char *error, size_t error_size)
{
    jc_file_t archive;
    bool success;

    if (content == NULL || resource == NULL || data == NULL ||
        size < resource->body_size)
        return fail(error, error_size, "resource read buffer is too small");
    if (!file_open(&archive, content->archive_path, vfs))
        return fail(error, error_size, "could not reopen the resource archive");
    success = file_seek(&archive, resource->body_offset) &&
              file_read_exact(&archive, data, resource->body_size);
    file_close(&archive);
    if (!success)
        return fail(error, error_size, "could not read the resource body");
    if (error != NULL && error_size > 0u)
        error[0] = '\0';
    return true;
}
