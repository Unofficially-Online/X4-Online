#ifndef X4_PLAYER_VIEW_ANCESTRY_PRELOAD_H
#define X4_PLAYER_VIEW_ANCESTRY_PRELOAD_H
#include <stdint.h>

/* Source-only diagnostic: entries are point observations, not held references.
 * Foreign counts are a sampled lower bound: a foreign caller can pass admission
 * concurrently with owner closure. No global reader exclusion is implied. */
#define VIEW_ANCESTRY_VERSION 1u
#define VIEW_ANCESTRY_SITES 3u
#define VIEW_ANCESTRY_SAMPLES 8u
#define VIEW_ANCESTRY_DEPTH 8u

typedef struct {
    uintptr_t component, vtable;
    uint64_t runtime;
} view_ancestry_node;

typedef struct {
    uint64_t generation;
    uintptr_t self, argument;
    uint32_t tid, count, complete, category; /* 0 unknown, 1 P1, 2 P2, 3 other */
    view_ancestry_node nodes[VIEW_ANCESTRY_DEPTH];
} view_ancestry_sample;

typedef struct {
    uint64_t started, returned, overflow, throttled, foreign;
    uint32_t count, examined, category_count[4];
    view_ancestry_sample samples[VIEW_ANCESTRY_SAMPLES];
} view_ancestry_site;

typedef struct {
    uint32_t version, installed, closed;
    uintptr_t first_ship, second_ship;
    view_ancestry_site sites[VIEW_ANCESTRY_SITES];
} view_ancestry_snapshot;

typedef struct {
    uint32_t version, installed;
    uintptr_t blocks[VIEW_ANCESTRY_SITES], stubs[VIEW_ANCESTRY_SITES];
    uintptr_t dispatchers[VIEW_ANCESTRY_SITES], frames[VIEW_ANCESTRY_SITES];
    uint64_t original_words[VIEW_ANCESTRY_SITES], patched_words[VIEW_ANCESTRY_SITES];
    uintptr_t ship_trampoline, original_slots[2], original_targets[2];
    uint32_t offsets[VIEW_ANCESTRY_SITES], frame_size, stub_size, page_size, ship_trampoline_size;
} view_ancestry_info;

int x4mp_view_ancestry_info(view_ancestry_info *out);
int x4mp_view_ancestry_bind(uint32_t owner, uintptr_t first_ship, uintptr_t second_ship);
int x4mp_view_ancestry_tick(uint64_t generation);
int x4mp_view_ancestry_finish(view_ancestry_snapshot *out);
#endif
