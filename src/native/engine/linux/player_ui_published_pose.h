#ifndef X4_PLAYER_UI_PUBLISHED_POSE_H
#define X4_PLAYER_UI_PUBLISHED_POSE_H

#include <stdint.h>
#include <string.h>
#include "player_ui_seat_transform.h"

/* The caller must keep the actor, original pose and aligned scratch resident,
 * and provide its own reader/lifetime exclusion. A successful lease alone
 * establishes neither. CAS returns 1 on exchange, 0 on mismatch, -1 if its
 * effect is unknown. Any false owner/read result is a refusal. */
typedef struct {
    void *context;
    int (*owner)(void *context, uintptr_t actor, unsigned bank);
    int (*read)(void *context, uintptr_t address, void *output, size_t size);
    int (*compare_exchange)(void *context, uintptr_t address,
                            uintptr_t expected, uintptr_t desired);
} player_ui_published_pose_ops;

typedef enum {
    PLAYER_POSE_NEW,
    PLAYER_POSE_ACTIVE,
    PLAYER_POSE_CLOSED,
    PLAYER_POSE_REFUSED,
    PLAYER_POSE_UNCERTAIN
} player_ui_published_pose_status;

/* Includes the known control-position field at actor + 0x6aa8. */
#define PLAYER_UI_POSE_ACTOR_EXTENT 0x6aacu

typedef struct {
    player_ui_published_pose_ops ops;
    uintptr_t actor, slot, original, scratch;
    unsigned bank;
    player_ui_published_pose_status status;
} player_ui_published_pose_lease;

static inline int player_ui_published_pose_range(uintptr_t address, size_t size) {
    return address && size && size - 1 <= UINTPTR_MAX - address;
}

static inline int player_ui_published_pose_overlap(uintptr_t a, size_t a_size,
                                                    uintptr_t b, size_t b_size) {
    return a <= b ? b - a < a_size : a - b < b_size;
}

/* Failures before the CAS are refused; after a possible mutation, uncertainty
 * is retained and no attempt is made to write through stale ownership.
 * Rejection before the source range is known leaves NEW unchanged. */
static inline int player_ui_published_pose_begin(player_ui_published_pose_lease *lease,
                                                  const player_ui_published_pose_ops *ops,
                                                  uintptr_t actor, unsigned bank,
                                                  float scratch[16], const float delta[16]) {
    if (!lease || lease->status != PLAYER_POSE_NEW) return 0;
    if (!ops || !ops->owner || !ops->read || !ops->compare_exchange ||
        !actor || (actor & 7) || bank > 1 || !scratch || ((uintptr_t)scratch & 15) || !delta) return 0;
    size_t slot_offset = 0xe0u + bank * 0x80u + 0x28u;
    if (actor > UINTPTR_MAX - slot_offset) return 0;
    uintptr_t slot = actor + slot_offset;
    uintptr_t scratch_address = (uintptr_t)scratch;
    if (!player_ui_published_pose_range(slot, sizeof(uintptr_t)) ||
        !player_ui_published_pose_range(scratch_address, 64) ||
        !player_ui_published_pose_range(actor, PLAYER_UI_POSE_ACTOR_EXTENT) ||
        !player_ui_published_pose_range((uintptr_t)lease, sizeof *lease) ||
        !player_ui_published_pose_range((uintptr_t)delta, 64) ||
        player_ui_published_pose_overlap((uintptr_t)lease, sizeof *lease, actor, PLAYER_UI_POSE_ACTOR_EXTENT) ||
        player_ui_published_pose_overlap(scratch_address, 64, actor, PLAYER_UI_POSE_ACTOR_EXTENT) ||
        player_ui_published_pose_overlap(scratch_address, 64, (uintptr_t)delta, 64) ||
        player_ui_published_pose_overlap(scratch_address, 64, (uintptr_t)ops, sizeof *ops) ||
        player_ui_published_pose_overlap((uintptr_t)lease, sizeof *lease, (uintptr_t)delta, 64) ||
        player_ui_published_pose_overlap((uintptr_t)lease, sizeof *lease, (uintptr_t)ops, sizeof *ops) ||
        player_ui_published_pose_overlap(scratch_address, 64, (uintptr_t)lease, sizeof *lease)) return 0;
    uintptr_t original = 0;
    float canonical[16], checked[16], corrected[16];
    if (!ops->owner(ops->context, actor, bank) ||
        !ops->read(ops->context, slot, &original, sizeof original) || !original || (original & 15) ||
        !player_ui_published_pose_range(original, 64) ||
        player_ui_published_pose_overlap(original, 64, scratch_address, 64) ||
        player_ui_published_pose_overlap(original, 64, slot, sizeof(uintptr_t)) ||
        player_ui_published_pose_overlap(original, 64, (uintptr_t)lease, sizeof *lease)) return 0;
    lease->status = PLAYER_POSE_REFUSED;
    lease->ops = *ops;
    lease->actor = actor;
    lease->bank = bank;
    lease->slot = slot;
    lease->scratch = scratch_address;
    if (!ops->read(ops->context, original, canonical, sizeof canonical) ||
        !player_ui_seat_transform_apply(canonical, delta, corrected) ||
        !ops->read(ops->context, original, checked, sizeof checked) ||
        memcmp(canonical, checked, sizeof canonical) || !ops->owner(ops->context, actor, bank)) return 0;
    lease->original = original;
    memcpy(scratch, corrected, sizeof corrected);
    if (!ops->owner(ops->context, actor, bank)) return 0;
    int exchanged = ops->compare_exchange(ops->context, slot, original, scratch_address);
    if (exchanged != 1) {
        if (exchanged != 0) lease->status = PLAYER_POSE_UNCERTAIN;
        return 0;
    }
    uintptr_t current = 0;
    if (!ops->owner(ops->context, actor, bank) ||
        !ops->read(ops->context, slot, &current, sizeof current) || current != scratch_address ||
        !ops->owner(ops->context, actor, bank)) {
        lease->status = PLAYER_POSE_UNCERTAIN;
        return 0;
    }
    lease->status = PLAYER_POSE_ACTIVE;
    return 1;
}

static inline int player_ui_published_pose_end(player_ui_published_pose_lease *lease) {
    if (!lease || lease->status != PLAYER_POSE_ACTIVE) return 0;
    player_ui_published_pose_ops *ops = &lease->ops;
    uintptr_t current = 0;
    if (!ops->owner(ops->context, lease->actor, lease->bank) ||
        !ops->read(ops->context, lease->slot, &current, sizeof current) || current != lease->scratch ||
        !ops->owner(ops->context, lease->actor, lease->bank)) {
        lease->status = PLAYER_POSE_UNCERTAIN;
        return 0;
    }
    if (ops->compare_exchange(ops->context, lease->slot, lease->scratch, lease->original) != 1) {
        lease->status = PLAYER_POSE_UNCERTAIN;
        return 0;
    }
    if (!ops->owner(ops->context, lease->actor, lease->bank) ||
        !ops->read(ops->context, lease->slot, &current, sizeof current) || current != lease->original ||
        !ops->owner(ops->context, lease->actor, lease->bank)) {
        lease->status = PLAYER_POSE_UNCERTAIN;
        return 0;
    }
    lease->status = PLAYER_POSE_CLOSED;
    return 1;
}
#endif
