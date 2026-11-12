#ifndef X4_GATE_PLAYER_CAMERA_CONTEXT_ANCHOR_H
#define X4_GATE_PLAYER_CAMERA_CONTEXT_ANCHOR_H

#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include "input_context_census.h"

/* An optional, one-shot, process-discard anchor. This compares observed
 * payloads at explicit boundaries; it does not exclude concurrent readers or
 * establish what happened between those observations. */
typedef struct {
    _Atomic unsigned active;
    uint64_t thread;
    uintptr_t owner, manager, ship, controller;
    unsigned char payloads[15][16];
} player_camera_context_anchor;
static player_camera_context_anchor player_camera_anchor;

static inline int camera_context_key(const unsigned char *payload, uint32_t context, uint32_t requester) {
    return input_context_census_u32(payload) == context && input_context_census_u32(payload + 4) == requester;
}
static inline int camera_context_fields(const unsigned char *payload, uint32_t priority, unsigned enabled) {
    return input_context_census_u32(payload + 8) == priority && payload[12] == enabled;
}
static inline int camera_context_complete(const input_context_census_sample *sample,
                                          const input_context_census_entries *entries, unsigned count) {
    if (!sample || !entries || sample->complete != 1 || sample->tree_valid != 1 || sample->target_present != 7 ||
        sample->node_count != count || entries->count != count || count > INPUT_CONTEXT_CENSUS_MAX_NODES)
        return 0;
    uint64_t previous = 0;
    unsigned targets = 0;
    for (unsigned i = 0; i < count; ++i) {
        const unsigned char *p = entries->payloads[i];
        uint32_t context = input_context_census_u32(p), requester = input_context_census_u32(p + 4);
        uint64_t key = ((uint64_t)(context ^ UINT32_C(0x80000000)) << 32) |
                       (requester ^ UINT32_C(0x80000000));
        if (i && key <= previous) return 0;
        previous = key;
        static const uint32_t wanted[] = {8, 15, 16};
        for (unsigned j = 0; j < 3; ++j) {
            if (context == wanted[j] && requester == 6) {
                if (memcmp(p, sample->payloads[j], 16)) return 0;
                targets |= 1u << j;
            }
        }
    }
    return targets == 7;
}
static inline int camera_context_transition(const input_context_census_sample *before,
        const input_context_census_entries *old, const input_context_census_sample *after,
        const input_context_census_entries *now) {
    if (!camera_context_complete(before, old, 13) || !camera_context_complete(after, now, 15) ||
        !before->manager || before->manager != after->manager) return 0;
    unsigned removed = 0, added11 = 0, added13 = 0, added14 = 0, retained = 0;
    unsigned i = 0, j = 0;
    while (i < old->count || j < now->count) {
        if (i < old->count && camera_context_key(old->payloads[i], 10, 6)) {
            if (removed++ || !camera_context_fields(old->payloads[i], 0, 1)) return 0;
            ++i; continue;
        }
        if (j < now->count && camera_context_key(now->payloads[j], 11, 6)) {
            if (added11++ || !camera_context_fields(now->payloads[j], 0, 1)) return 0;
            ++j; continue;
        }
        if (j < now->count && camera_context_key(now->payloads[j], 13, 20)) {
            if (added13++ || !camera_context_fields(now->payloads[j], 5, 0)) return 0;
            ++j; continue;
        }
        if (j < now->count && camera_context_key(now->payloads[j], 14, 20)) {
            if (added14++ || !camera_context_fields(now->payloads[j], 5, 0)) return 0;
            ++j; continue;
        }
        if (i == old->count || j == now->count || memcmp(old->payloads[i], now->payloads[j], 16)) return 0;
        ++i; ++j; ++retained;
    }
    return removed == 1 && added11 == 1 && added13 == 1 && added14 == 1 && retained == 12;
}
static inline int camera_context_anchor_adopt(player_camera_context_anchor *anchor, uint64_t thread,
        uintptr_t owner, uintptr_t ship, uintptr_t controller,
        const input_context_census_sample *before, const input_context_census_entries *old,
        const input_context_census_sample *after, const input_context_census_entries *now) {
    if (!anchor || atomic_load_explicit(&anchor->active, memory_order_acquire) || !thread || !owner ||
        owner > UINTPTR_MAX - 0x10 || !ship || !controller || !before || !after ||
        before->manager != owner + 0x10 || !camera_context_transition(before, old, after, now)) return 0;
    anchor->thread = thread; anchor->owner = owner; anchor->manager = before->manager;
    anchor->ship = ship; anchor->controller = controller;
    memcpy(anchor->payloads, now->payloads, sizeof anchor->payloads);
    atomic_store_explicit(&anchor->active, 1, memory_order_release);
    return 1;
}
static inline int camera_context_anchor_matches(const player_camera_context_anchor *anchor, uint64_t thread,
        uintptr_t owner, uintptr_t ship, uintptr_t controller, const input_context_census_sample *sample,
        const input_context_census_entries *current) {
    return anchor && atomic_load_explicit(&anchor->active, memory_order_acquire) == 1 &&
        thread == anchor->thread && owner == anchor->owner && ship == anchor->ship &&
        controller == anchor->controller && sample && sample->manager == anchor->manager &&
        camera_context_complete(sample, current, 15) &&
        !memcmp(anchor->payloads, current->payloads, sizeof anchor->payloads);
}
#endif
