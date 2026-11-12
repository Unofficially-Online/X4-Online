#ifndef X4_PLAYER_PRESENTATION_PROBE_H
#define X4_PLAYER_PRESENTATION_PROBE_H

/* Diagnostic only. The caller retains objects and authenticates the worker;
 * reads are not a lock against engine threads. Cached lookup acquires a native
 * reference and release is mandatory even if a subsequent observation fails.
 * Only authenticated immediate cockpit parents are passed to native lookup. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef struct {
    void *context;
    int (*read)(void *, uintptr_t, void *, size_t);
    uintptr_t (*live)(void *, uintptr_t);
    uintptr_t (*cached)(void *, uintptr_t);
    void (*release)(void *, uintptr_t);
    int (*room_ok)(void *, uintptr_t);
} presentation_probe_ops;

typedef struct {
    uintptr_t player, parent, vector_begin, vector_end, vector_capacity, cache_parent;
    uintptr_t live, cached;
    uint32_t position, refs_before, refs_acquired, refs_after, marker_before, marker_after;
    unsigned live_called, cached_called, cached_omitted;
} presentation_probe_row;

static int presentation_probe_read(const presentation_probe_ops *ops, uintptr_t base, size_t offset,
                                   void *out, size_t length) {
    return base && offset <= UINTPTR_MAX - base && length <= UINTPTR_MAX - (base + offset) &&
           ops->read(ops->context, base + offset, out, length);
}

static int presentation_probe_sample(const presentation_probe_ops *ops, uintptr_t player,
                                     uint32_t bank, uintptr_t expected_room,
                                     presentation_probe_row *row) {
    if (!ops || !ops->read || !ops->live || !ops->cached || !ops->release || !ops->room_ok ||
        !row || !player || bank > 1) return 0;
    memset(row, 0, sizeof *row);
    row->player = player;
    if (!presentation_probe_read(ops, player, 0x70, &row->parent, 8) ||
        !presentation_probe_read(ops, player, 0x308, &row->vector_begin, 8) ||
        !presentation_probe_read(ops, player, 0x310, &row->vector_end, 8) ||
        !presentation_probe_read(ops, player, 0x318, &row->vector_capacity, 8) ||
        !presentation_probe_read(ops, player, 0x6aa8, &row->position, 4) ||
        !presentation_probe_read(ops, player, 0xe0 + bank * 0x80, &row->cache_parent, 8)) return 0;
    if (!expected_room || row->parent != expected_room || !ops->room_ok(ops->context, expected_room)) {
        row->cached_omitted = 1;
        return 1;
    }
    row->live_called = 1;
    row->live = ops->live(ops->context, player);
    if (row->live != expected_room) return 0;
    if (row->cache_parent != expected_room) {
        row->cached_omitted = 1;
        return 1;
    }
    if (!presentation_probe_read(ops, expected_room, 0x40, &row->refs_before, 4) ||
        !presentation_probe_read(ops, expected_room, 0x44, &row->marker_before, 4) ||
        !row->refs_before || row->refs_before == UINT32_MAX || row->marker_before != 2) return 0;
    row->cached_called = 1;
    row->cached = ops->cached(ops->context, player);
    if (!row->cached) return 0;
    int ok = row->cached == expected_room &&
        presentation_probe_read(ops, row->cached, 0x40, &row->refs_acquired, 4) &&
        row->refs_acquired == row->refs_before + 1;
    ops->release(ops->context, row->cached);
    if (!presentation_probe_read(ops, expected_room, 0x40, &row->refs_after, 4) ||
        !presentation_probe_read(ops, expected_room, 0x44, &row->marker_after, 4) ||
        row->marker_after != row->marker_before || row->refs_after != row->refs_before) ok = 0;
    return ok;
}
#endif
