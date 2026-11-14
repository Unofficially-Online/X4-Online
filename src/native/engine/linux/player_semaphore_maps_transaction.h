#ifndef X4_PLAYER_SEMAPHORE_MAPS_TRANSACTION_H
#define X4_PLAYER_SEMAPHORE_MAPS_TRANSACTION_H
/* Private, bounded per-call mapping view. No result survives its caller.
 * Reparse once at commit and require every used mapping to remain identical;
 * unrelated mappings may change. Never use this to authorize unsafe reads.
 * Each call allocates at most 32768 * sizeof(player_semaphore_mapping)
 * (2.25 MiB on x86-64), then frees it. There is no cross-frame cache. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
enum { PLAYER_SEM_MAPS_LIMIT = 32768, PLAYER_SEM_MAPS_USED = 64 };
typedef struct {
    uintptr_t start, end;
    unsigned long long offset, inode;
    char permissions[5], device[24];
    unsigned anonymous;
} player_semaphore_mapping;
typedef struct {
    player_semaphore_mapping *rows;
    unsigned count, used_count, used[PLAYER_SEM_MAPS_USED];
} player_semaphore_maps_transaction;
static int player_semaphore_maps_row(const char *line, player_semaphore_mapping *row) {
    unsigned long long start, end; int consumed = 0;
    memset(row, 0, sizeof *row);
    if (sscanf(line, "%llx-%llx %4s %llx %23s %llu %n", &start, &end,
        row->permissions, &row->offset, row->device, &row->inode, &consumed) != 6 ||
        !consumed || start >= end || start > UINTPTR_MAX || end > UINTPTR_MAX) return 0;
    row->start = (uintptr_t)start; row->end = (uintptr_t)end;
    row->anonymous = line[consumed] == '\n' || !line[consumed];
    return 1;
}
static int player_semaphore_maps_begin(player_semaphore_maps_transaction *view) {
    *view = (player_semaphore_maps_transaction){0};
    view->rows = calloc(PLAYER_SEM_MAPS_LIMIT, sizeof *view->rows);
    if (!view->rows) return 0;
    FILE *file = fopen("/proc/self/maps", "re");
    if (!file) { free(view->rows); view->rows = NULL; return 0; }
    char line[PATH_MAX + 128]; int valid = 1;
    while (fgets(line, sizeof line, file)) {
        if (!strchr(line, '\n') || view->count == PLAYER_SEM_MAPS_LIMIT ||
            !player_semaphore_maps_row(line, &view->rows[view->count])) { valid = 0; break; }
        if (view->count && view->rows[view->count-1].end > view->rows[view->count].start) { valid = 0; break; }
        ++view->count;
    }
    if (ferror(file)) valid = 0;
    if (fclose(file)) valid = 0;
    if (!valid || !view->count) { free(view->rows); view->rows = NULL; return 0; }
    return 1;
}
static int player_semaphore_maps_find(player_semaphore_maps_transaction *view,
        uintptr_t address, char permissions[5], char *path, size_t capacity) {
    for (unsigned i = 0; i < view->count; ++i) {
        const player_semaphore_mapping *row = &view->rows[i];
        if (address < row->start) return 0;
        if (address >= row->end) continue;
        unsigned j;
        for (j = 0; j < view->used_count && view->used[j] != i; ++j) {}
        if (j == view->used_count) {
            if (view->used_count == PLAYER_SEM_MAPS_USED) return 0;
            view->used[view->used_count++] = i;
        }
        memcpy(permissions, row->permissions, 5);
        /* Only anonymous-vs-file-backed is consumed by this authenticator. */
        if (path && capacity) {
            if (!row->anonymous && capacity < 2) return 0;
            path[0] = row->anonymous ? 0 : '?';
            if (!row->anonymous) path[1] = 0;
        }
        return 1;
    }
    return 0;
}
static int player_semaphore_maps_commit(player_semaphore_maps_transaction *view) {
    FILE *file = fopen("/proc/self/maps", "re");
    if (!file) return 0;
    char line[PATH_MAX + 128]; unsigned matched = 0, count = 0;
    uint64_t found = 0; uintptr_t previous_end = 0; int valid = 1;
    while (fgets(line, sizeof line, file)) {
        player_semaphore_mapping row;
        if (!strchr(line, '\n') || ++count > PLAYER_SEM_MAPS_LIMIT ||
            !player_semaphore_maps_row(line, &row)) { valid = 0; break; }
        if (previous_end > row.start) { valid = 0; break; }
        previous_end = row.end;
        for (unsigned j = 0; j < view->used_count; ++j) {
            const player_semaphore_mapping *old = &view->rows[view->used[j]];
            if (row.start != old->start || row.end != old->end) continue;
            if ((found & (UINT64_C(1) << j)) || memcmp(&row, old, sizeof row)) { valid = 0; break; }
            found |= UINT64_C(1) << j; ++matched;
        }
        if (!valid) break;
    }
    if (ferror(file)) valid = 0;
    if (fclose(file)) valid = 0;
    return valid && matched == view->used_count;
}
#endif
