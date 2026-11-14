#ifndef X4_PLAYER_VIEW_PIN_MAP_BATCH_H
#define X4_PLAYER_VIEW_PIN_MAP_BATCH_H

/* Per-validation snapshots only: the caller retains no mapping cache between
 * validations. Relevant mappings must agree before and after verification. */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PLAYER_VIEW_MAP_BYTES (1024u * 1024u)
#define PLAYER_VIEW_MAP_QUERIES 256u
#define PLAYER_VIEW_MAP_ENTRIES 16384u

typedef struct {
    uintptr_t start;
    uintptr_t end;
    size_t offset;
    size_t length;
} player_view_map_entry;

typedef struct {
    void *allocation;
    char *before;
    char *after;
    size_t before_length;
    player_view_map_entry *before_index;
    player_view_map_entry *after_index;
    size_t before_count;
    uintptr_t queries[PLAYER_VIEW_MAP_QUERIES];
    size_t query_count;
} player_view_map_batch;

static _Thread_local player_view_map_batch *player_view_active_map_batch __attribute__((unused));

static inline int player_view_map_read(char *buffer, size_t *length) {
    int fd = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return 0;
    size_t used = 0;
    int ok = 1;
    for (;;) {
        if (used == PLAYER_VIEW_MAP_BYTES) {
            ok = 0;
            break;
        }
        ssize_t count = read(fd, buffer + used, PLAYER_VIEW_MAP_BYTES - used);
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0) {
            ok = 0;
            break;
        }
        if (!count)
            break;
        used += (size_t)count;
    }
    if (close(fd))
        ok = 0;
    if (!ok)
        return 0;
    buffer[used] = 0;
    *length = used;
    return 1;
}

/* Index each map once; lookups below use the kernel's sorted ranges. */
static inline int player_view_map_index(const char *buffer, size_t length,
                                        player_view_map_entry *entries, size_t *count) {
    size_t offset = 0;
    size_t used = 0;
    while (offset < length) {
        const char *line = buffer + offset;
        const char *newline = memchr(line, '\n', length - offset);
        size_t line_length = newline ? (size_t)(newline - line) : length - offset;
        if (!line_length || line_length >= PATH_MAX + 128u || used == PLAYER_VIEW_MAP_ENTRIES)
            return 0;
        char *separator = NULL;
        errno = 0;
        unsigned long long start = strtoull(line, &separator, 16);
        if (errno || !separator || separator >= line + line_length || *separator != '-')
            return 0;
        char *after = NULL;
        unsigned long long end = strtoull(separator + 1, &after, 16);
        if (errno || !after || after >= line + line_length || *after != ' ' || start >= end ||
            start > UINTPTR_MAX || end > UINTPTR_MAX || (used && start < entries[used - 1].end))
            return 0;
        entries[used++] =
            (player_view_map_entry){(uintptr_t)start, (uintptr_t)end, offset, line_length};
        offset += line_length + (newline != NULL);
    }
    *count = used;
    return used != 0;
}

static inline const player_view_map_entry *
player_view_map_search(const player_view_map_entry *entries, size_t count, uintptr_t address) {
    size_t low = 0, high = count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (entries[middle].end <= address)
            low = middle + 1;
        else
            high = middle;
    }
    return low == count || address < entries[low].start ? NULL : &entries[low];
}

/* Match gate_maps_find's permissions/path interpretation. */
static inline int player_view_map_lookup(const char *buffer, const player_view_map_entry *entries,
                                         size_t count, uintptr_t address, char permissions[5],
                                         char path[PATH_MAX], char *mapping_line) {
    const player_view_map_entry *entry = player_view_map_search(entries, count, address);
    if (!entry)
        return 0;
    char copy[PATH_MAX + 128];
    memcpy(copy, buffer + entry->offset, entry->length);
    copy[entry->length] = 0;
    unsigned long long start, end, map_offset;
    unsigned long inode;
    char device[16];
    int consumed = 0;
    if (sscanf(copy, "%llx-%llx %4s %llx %15s %lu %n", &start, &end, permissions, &map_offset,
               device, &inode, &consumed) < 6)
        return 0;
    const char *name = consumed ? copy + consumed : "";
    size_t name_length = strlen(name);
    if (name_length >= PATH_MAX)
        return 0;
    memcpy(path, name, name_length + 1);
    if (mapping_line)
        memcpy(mapping_line, copy, entry->length + 1);
    return 1;
}

static inline int player_view_map_begin(player_view_map_batch *batch) {
    memset(batch, 0, sizeof(*batch));
    const size_t buffer_size = PLAYER_VIEW_MAP_BYTES + 16u;
    const size_t index_size = sizeof(player_view_map_entry) * PLAYER_VIEW_MAP_ENTRIES;
    batch->allocation = malloc(2u * buffer_size + 2u * index_size);
    if (!batch->allocation)
        return 0;
    batch->before = batch->allocation;
    batch->after = batch->before + buffer_size;
    batch->before_index = (player_view_map_entry *)(void *)(batch->after + buffer_size);
    batch->after_index = batch->before_index + PLAYER_VIEW_MAP_ENTRIES;
    if (!player_view_map_read(batch->before, &batch->before_length) ||
        !player_view_map_index(batch->before, batch->before_length, batch->before_index,
                               &batch->before_count)) {
        free(batch->allocation);
        memset(batch, 0, sizeof(*batch));
        return 0;
    }
    return 1;
}

static inline int player_view_map_find(player_view_map_batch *batch, uintptr_t address,
                                       char permissions[5], char *path, size_t capacity) {
    char full_path[PATH_MAX];
    if (!batch || batch->query_count == PLAYER_VIEW_MAP_QUERIES ||
        !player_view_map_lookup(batch->before, batch->before_index, batch->before_count, address,
                                permissions, full_path, NULL))
        return 0;
    batch->queries[batch->query_count++] = address;
    if (path && capacity) {
        size_t length = strlen(full_path);
        if (length >= capacity)
            length = capacity - 1;
        memcpy(path, full_path, length);
        path[length] = 0;
    }
    return 1;
}

static inline int player_view_map_end(player_view_map_batch *batch) {
    size_t after_length = 0;
    size_t after_count = 0;
    int ok = batch && batch->before && batch->after &&
             player_view_map_read(batch->after, &after_length) &&
             player_view_map_index(batch->after, after_length, batch->after_index, &after_count);
    for (size_t i = 0; ok && i < batch->query_count; ++i) {
        const player_view_map_entry *before =
            player_view_map_search(batch->before_index, batch->before_count, batch->queries[i]);
        const player_view_map_entry *after =
            player_view_map_search(batch->after_index, after_count, batch->queries[i]);
        ok = before && after && before->length == after->length &&
             !memcmp(batch->before + before->offset, batch->after + after->offset, before->length);
    }
    free(batch->allocation);
    memset(batch, 0, sizeof(*batch));
    return ok;
}
#endif
