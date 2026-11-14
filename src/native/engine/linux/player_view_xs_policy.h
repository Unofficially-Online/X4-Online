#ifndef X4_PLAYER_VIEW_XS_POLICY_H
#define X4_PLAYER_VIEW_XS_POLICY_H

/* Dormant XS semantics. The embedding coordinator must supply an immutable,
 * authenticated worker/generation snapshot and a proven XS identity. enabled,
 * owner_tid and callback pointers must be immutably published before use.
 * Native distance and external originals run once; target dispatch may skip
 * its mutating original for an authenticated replacement. Failure requires
 * the caller to stop before the native frame continues. */
#include <errno.h>
#include <math.h>
#include <stdint.h>
#define PLAYER_VIEW_XS_SLOTS 2u

typedef struct {
    uintptr_t ship;
    uintptr_t sector;
    uint64_t target;
    float position[3];
    unsigned external;
} player_view_xs_slot;
typedef struct {
    uint64_t generation;
    player_view_xs_slot slots[PLAYER_VIEW_XS_SLOTS];
} player_view_xs_snapshot;
typedef struct {
    uintptr_t sector;
    uint64_t portable;
    float position[3];
} player_view_xs_candidate;
typedef struct {
    void *context;
    unsigned enabled;
    uint64_t owner_tid;
    uint64_t (*thread_id)(void *);
    /* positions requests sector-relative positions for external views. */
    int (*snapshot)(void *context, uint64_t generation, unsigned positions,
                    player_view_xs_snapshot *snapshot);
    /* 1: authenticated XS object; 0: unrelated; -1: unknown or drift. */
    int (*candidate)(void *context, uintptr_t object, unsigned position,
                     player_view_xs_candidate *candidate);
    uintptr_t (*native_target)(void);
    uint64_t (*native_external)(void);
    float (*native_distance)(void *, void *, unsigned);
} player_view_xs_ops;

static inline int player_view_xs_snapshot_valid(const player_view_xs_snapshot *snapshot,
                                                uint64_t generation, unsigned positions) {
    if (!snapshot || !generation || snapshot->generation != generation)
        return 0;
    for (unsigned i = 0; i < PLAYER_VIEW_XS_SLOTS; ++i) {
        const player_view_xs_slot *slot = &snapshot->slots[i];
        if (!slot->ship || slot->external > 1)
            return 0;
        if (positions && slot->external &&
            (!slot->sector || !isfinite(slot->position[0]) || !isfinite(slot->position[1]) ||
             !isfinite(slot->position[2])))
            return 0;
    }
    return snapshot->slots[0].ship != snapshot->slots[1].ship;
}
/* 0: disabled; 1: authenticated; -1: invalid. Thread check precedes all
 * mutable snapshot reads. */
static inline int player_view_xs_acquire(const player_view_xs_ops *ops, uint64_t generation,
                                         unsigned positions, player_view_xs_snapshot *snapshot) {
    if (!ops || !snapshot || !ops->snapshot || !ops->candidate)
        return -1;
    if (!ops->enabled)
        return 0;
    if (ops->enabled != 1 || !ops->thread_id || !ops->owner_tid ||
        ops->thread_id(ops->context) != ops->owner_tid ||
        ops->snapshot(ops->context, generation, positions, snapshot) != 1 ||
        !player_view_xs_snapshot_valid(snapshot, generation, positions))
        return -1;
    return 1;
}
static inline int player_view_xs_route_target(const player_view_xs_ops *ops, uint64_t generation,
                                              uintptr_t object, uintptr_t original,
                                              uintptr_t *out) {
    player_view_xs_snapshot snapshot = {0};
    if (!out)
        return 0;
    int admitted = player_view_xs_acquire(ops, generation, 0, &snapshot);
    if (admitted < 0)
        return 0;
    uintptr_t result = original;
    if (admitted) {
        player_view_xs_candidate candidate = {0};
        int kind = ops->candidate(ops->context, object, 0, &candidate);
        if (kind < 0 || kind > 1 || (kind == 1 && candidate.portable <= 1))
            return 0;
        if (kind == 1) {
            result = 0;
            for (unsigned i = 0; i < PLAYER_VIEW_XS_SLOTS; ++i)
                if (snapshot.slots[i].target > 1 && snapshot.slots[i].target == candidate.portable)
                    result = object;
        }
    }
    *out = result;
    return 1;
}
static inline int player_view_xs_route_external(const player_view_xs_ops *ops, uint64_t generation,
                                                uint64_t original, uint64_t *out) {
    player_view_xs_snapshot snapshot = {0};
    if (!out)
        return 0;
    int admitted = player_view_xs_acquire(ops, generation, 0, &snapshot);
    if (admitted < 0)
        return 0;
    *out = admitted ? (original & ~UINT64_C(0xff)) |
                          (snapshot.slots[0].external || snapshot.slots[1].external)
                    : original;
    return 1;
}
static inline int player_view_xs_route_distance(const player_view_xs_ops *ops, uint64_t generation,
                                                uintptr_t ship, float original, float *out) {
    player_view_xs_snapshot snapshot = {0};
    if (!out)
        return 0;
    int admitted = player_view_xs_acquire(ops, generation, 1, &snapshot);
    if (admitted < 0)
        return 0;
    float result = original;
    if (admitted) {
        player_view_xs_candidate candidate = {0};
        int kind = ops->candidate(ops->context, ship, 1, &candidate);
        if (kind < 0 || kind > 1)
            return 0;
        if (kind == 1) {
            if (!candidate.sector || !isfinite(candidate.position[0]) ||
                !isfinite(candidate.position[1]) || !isfinite(candidate.position[2]))
                return 0;
            result = 3.0e38f;
            for (unsigned i = 0; i < PLAYER_VIEW_XS_SLOTS; ++i) {
                const player_view_xs_slot *slot = &snapshot.slots[i];
                if (!slot->external || slot->sector != candidate.sector)
                    continue;
                float dx = slot->position[0] - candidate.position[0];
                float dy = slot->position[1] - candidate.position[1];
                float dz = slot->position[2] - candidate.position[2];
                float distance = dx * dx + dy * dy + dz * dz;
                if (!isfinite(distance))
                    return 0;
                if (distance < result)
                    result = distance;
            }
        }
    }
    *out = result;
    return 1;
}
static inline int player_view_xs_target(const player_view_xs_ops *ops, uint64_t generation,
                                        uintptr_t object, uintptr_t *out) {
    if (!ops || !ops->native_target)
        return 0;
    uintptr_t original = ops->native_target();
    int saved = errno;
    int ok = player_view_xs_route_target(ops, generation, object, original, out);
    errno = saved;
    return ok;
}
static inline int player_view_xs_external(const player_view_xs_ops *ops, uint64_t generation,
                                          uint64_t *out) {
    if (!ops || !ops->native_external)
        return 0;
    uint64_t original = ops->native_external();
    int saved = errno;
    int ok = player_view_xs_route_external(ops, generation, original, out);
    errno = saved;
    return ok;
}
static inline int player_view_xs_distance(const player_view_xs_ops *ops, uint64_t generation,
                                          void *ship, void *camera, unsigned flags, float *out) {
    if (!ops || !ops->native_distance)
        return 0;
    float original = ops->native_distance(ship, camera, flags);
    int saved = errno;
    int ok = player_view_xs_route_distance(ops, generation, (uintptr_t)ship, original, out);
    errno = saved;
    return ok;
}
#endif
