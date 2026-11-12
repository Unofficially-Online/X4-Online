#ifndef X4_PLAYER_PRESENTATION_OVERLAY_H
#define X4_PLAYER_PRESENTATION_OVERLAY_H

/* Engine-free transaction only: the caller must hold these objects and prove
 * that all relevant readers/writers are excluded for the entire lease. P1,
 * P2, room, and P2 vector storage must remain held and immutable throughout.
 * The owner callback authenticates external proof; it does not create it. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    PRESENTATION_PARENT_OFFSET = 0x70,
    PRESENTATION_VECTOR_OFFSET = 0x308,
    PRESENTATION_POSITION_OFFSET = 0x6aa8,
    PRESENTATION_VIEW_PARENT_OFFSET = 0xe0,
    PRESENTATION_VIEW_BANK_STRIDE = 0x80,
    PRESENTATION_PLAYER_EXTENT = 0x6aac
};

typedef struct {
    void *context;
    int (*owner)(void *context, uintptr_t player1, uintptr_t player2, uintptr_t room, uint32_t bank);
    int (*read)(void *context, uintptr_t address, void *output, size_t length);
    int (*write)(void *context, uintptr_t address, const void *input, size_t length);
} player_presentation_overlay_ops;

typedef enum {
    PRESENTATION_OVERLAY_NEW,
    PRESENTATION_OVERLAY_ACTIVE,
    PRESENTATION_OVERLAY_CLOSED,
    PRESENTATION_OVERLAY_FAILED,
    PRESENTATION_OVERLAY_UNCERTAIN
} player_presentation_overlay_status;

typedef struct {
    uintptr_t begin, end, capacity;
} player_presentation_vector;

typedef struct {
    player_presentation_overlay_status status;
    player_presentation_overlay_ops ops;
    uintptr_t player1, player2, room;
    uint32_t bank;
    uintptr_t addresses[4];
    unsigned char original[4][24];
    unsigned char replacement[4][24];
    unsigned touched;
} player_presentation_overlay;

static int presentation_overlay_address(uintptr_t base, size_t offset, size_t length, uintptr_t *out) {
    const uintptr_t max_user = UINT64_C(0x00007fffffffffff);
    if (!base || base > max_user || offset > max_user - base || length > max_user - (base + offset)) return 0;
    *out = base + offset;
    return 1;
}

static int presentation_overlay_vector_valid(const player_presentation_vector *vector) {
    const uintptr_t max_user = UINT64_C(0x00007fffffffffff);
    if (!vector->begin) return !vector->end && !vector->capacity;
    return vector->begin <= vector->end && vector->end <= vector->capacity &&
        vector->capacity <= max_user && !(vector->begin & 7) && !(vector->end & 7) &&
        !(vector->capacity & 7);
}

static int presentation_overlay_owned(player_presentation_overlay *overlay) {
    return overlay->ops.owner(overlay->ops.context, overlay->player1, overlay->player2,
                              overlay->room, overlay->bank);
}

static int presentation_overlay_read(player_presentation_overlay *overlay, uintptr_t address,
                                     void *output, size_t length) {
    return presentation_overlay_owned(overlay) &&
        overlay->ops.read(overlay->ops.context, address, output, length) &&
        presentation_overlay_owned(overlay);
}

/* A false write may have changed bytes, so it joins the rollback obligation.
 * If a rollback write itself fails, stop immediately with uncertainty: its
 * owner is not rechecked and no further writes are attempted. */
static int presentation_overlay_restore(player_presentation_overlay *overlay) {
    static const size_t sizes[4] = {8, 24, 4, 8};
    while (overlay->touched) {
        unsigned index = overlay->touched - 1;
        unsigned char observed[24];
        if (!presentation_overlay_owned(overlay) ||
            !overlay->ops.write(overlay->ops.context, overlay->addresses[index],
                                overlay->original[index], sizes[index]) ||
            !presentation_overlay_read(overlay, overlay->addresses[index], observed, sizes[index]) ||
            memcmp(observed, overlay->original[index], sizes[index])) {
            overlay->status = PRESENTATION_OVERLAY_UNCERTAIN;
            return 0;
        }
        --overlay->touched;
    }
    return 1;
}

static int player_presentation_overlay_begin(player_presentation_overlay *overlay,
        const player_presentation_overlay_ops *ops, uintptr_t player1, uintptr_t player2,
        uintptr_t room, uint32_t bank) {
    static const size_t sizes[4] = {8, 24, 4, 8};
    static const size_t offsets[3] = {PRESENTATION_PARENT_OFFSET, PRESENTATION_VECTOR_OFFSET,
                                      PRESENTATION_POSITION_OFFSET};
    if (!overlay) return 0;
    if (overlay->status != PRESENTATION_OVERLAY_NEW) {
        if (overlay->status == PRESENTATION_OVERLAY_ACTIVE) overlay->status = PRESENTATION_OVERLAY_UNCERTAIN;
        return 0;
    }
    if (!ops || !ops->owner || !ops->read || !ops->write) {
        overlay->status = PRESENTATION_OVERLAY_FAILED;
        return 0;
    }
    overlay->ops = *ops;
    overlay->player1 = player1;
    overlay->player2 = player2;
    overlay->room = room;
    overlay->bank = bank;
    overlay->status = PRESENTATION_OVERLAY_FAILED;
    uintptr_t p1end, p2end, ignored;
    if (bank > 1 || player1 == player2 || room == player1 || room == player2 ||
        !presentation_overlay_address(player1, 0, PRESENTATION_PLAYER_EXTENT, &p1end) ||
        !presentation_overlay_address(player2, 0, PRESENTATION_PLAYER_EXTENT, &p2end) ||
        !presentation_overlay_address(room, 0, 1, &ignored) ||
        !(player1 + PRESENTATION_PLAYER_EXTENT <= player2 || player2 + PRESENTATION_PLAYER_EXTENT <= player1) ||
        (room >= player1 && room < player1 + PRESENTATION_PLAYER_EXTENT) ||
        (room >= player2 && room < player2 + PRESENTATION_PLAYER_EXTENT)) return 0;
    (void)p1end;
    (void)p2end;
    size_t all_offsets[4] = {offsets[0], offsets[1], offsets[2], PRESENTATION_VIEW_PARENT_OFFSET + bank * PRESENTATION_VIEW_BANK_STRIDE};
    for (unsigned i = 0; i < 4; ++i) {
        uintptr_t source;
        if (!presentation_overlay_address(player1, all_offsets[i], sizes[i], &overlay->addresses[i]) ||
            !presentation_overlay_address(player2, all_offsets[i], sizes[i], &source) ||
            !presentation_overlay_read(overlay, overlay->addresses[i], overlay->original[i], sizes[i])) return 0;
        if (i < 3 && !presentation_overlay_read(overlay, source, overlay->replacement[i], sizes[i])) return 0;
    }
    uintptr_t old_parent, old_view_parent, new_parent;
    player_presentation_vector old_vector, new_vector;
    memcpy(&old_parent, overlay->original[0], 8);
    memcpy(&old_view_parent, overlay->original[3], 8);
    memcpy(&new_parent, overlay->replacement[0], 8);
    memcpy(&old_vector, overlay->original[1], sizeof old_vector);
    memcpy(&new_vector, overlay->replacement[1], sizeof new_vector);
    if (!old_parent || old_parent != old_view_parent || new_parent != room ||
        !presentation_overlay_address(old_parent, 0, 1, &ignored) ||
        !presentation_overlay_vector_valid(&old_vector) || !presentation_overlay_vector_valid(&new_vector)) return 0;
    memcpy(overlay->replacement[3], &room, 8);
    for (unsigned i = 0; i < 4; ++i) {
        unsigned char observed[24];
        if (!presentation_overlay_owned(overlay)) {
            if (overlay->touched) overlay->status = PRESENTATION_OVERLAY_UNCERTAIN;
            return 0;
        }
        overlay->touched = i + 1;
        if (!overlay->ops.write(overlay->ops.context, overlay->addresses[i], overlay->replacement[i], sizes[i]) ||
            !presentation_overlay_read(overlay, overlay->addresses[i], observed, sizes[i]) ||
            memcmp(observed, overlay->replacement[i], sizes[i])) {
            if (presentation_overlay_restore(overlay)) overlay->status = PRESENTATION_OVERLAY_FAILED;
            return 0;
        }
    }
    overlay->status = PRESENTATION_OVERLAY_ACTIVE;
    return 1;
}

static int player_presentation_overlay_end(player_presentation_overlay *overlay,
        uintptr_t player1, uintptr_t player2, uintptr_t room, uint32_t bank) {
    if (!overlay || overlay->status != PRESENTATION_OVERLAY_ACTIVE) return 0;
    if (player1 != overlay->player1 || player2 != overlay->player2 || room != overlay->room || bank != overlay->bank) {
        overlay->status = PRESENTATION_OVERLAY_UNCERTAIN;
        return 0;
    }
    static const size_t sizes[4] = {8, 24, 4, 8};
    for (unsigned i = 0; i < 4; ++i) {
        unsigned char observed[24];
        if (!presentation_overlay_read(overlay, overlay->addresses[i], observed, sizes[i]) ||
            memcmp(observed, overlay->replacement[i], sizes[i])) {
            overlay->status = PRESENTATION_OVERLAY_UNCERTAIN;
            return 0;
        }
    }
    if (!presentation_overlay_restore(overlay)) return 0;
    overlay->status = PRESENTATION_OVERLAY_CLOSED;
    return 1;
}
#endif
