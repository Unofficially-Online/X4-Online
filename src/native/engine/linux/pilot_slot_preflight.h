#ifndef X4_LINUX_PILOT_SLOT_PREFLIGHT_H
#define X4_LINUX_PILOT_SLOT_PREFLIGHT_H

/* Read-only preflight for two already-resolved fixture identities. This does
 * not retain any object, create a Player, seat a pilot or permit a bank swap.
 * Caller must prove image pins and a serialized, stable read boundary. */
#include <stddef.h>
#include <stdint.h>

#define PILOT_PREFLIGHT_SLOT_COUNT 2u
#define PILOT_PREFLIGHT_UNIVERSE_PLAYER 0x238u
#define PILOT_PREFLIGHT_PLAYER_MACRO 0x60u
#define PILOT_PREFLIGHT_REFERENCE_COUNT 0x40u
#define PILOT_PREFLIGHT_MACRO_CLASS 0x44u
#define PILOT_PREFLIGHT_OBJECT_ID 0x08u
#define PILOT_PREFLIGHT_PLAYER_CLASS 0x4cu

typedef struct {
    void *context;
    int (*read)(void *context, uintptr_t address, void *out, size_t length);
    /* Resolve against the sealed identity map, without touching the engine. */
    int (*resolve)(void *context, uint64_t saved_id, uint64_t *runtime_id, uintptr_t *object);
} pilot_preflight_io;
typedef struct {
    uint64_t saved;
    uint64_t runtime;
    uintptr_t object;
} pilot_preflight_ship;
typedef struct {
    uintptr_t universe;
    uintptr_t player;
    uintptr_t player_vtable;
    uintptr_t macro;
    uint32_t macro_class;
    int32_t references;
    pilot_preflight_ship ships[PILOT_PREFLIGHT_SLOT_COUNT];
} pilot_preflight_snapshot;
typedef enum {
    PILOT_PREFLIGHT_OK,
    PILOT_PREFLIGHT_ARGUMENT,
    PILOT_PREFLIGHT_READ,
    PILOT_PREFLIGHT_PLAYER,
    PILOT_PREFLIGHT_IDENTITY,
    PILOT_PREFLIGHT_CHANGED
} pilot_preflight_result;

static int pilot_preflight_read(const pilot_preflight_io *io, uintptr_t base,
                                size_t offset, void *out, size_t length) {
    if (!base ||
        offset > UINTPTR_MAX - base ||
        length > UINTPTR_MAX - (base + offset)) {
        return 0;
    }
    return io->read(io->context, base + offset, out, length);
}

static int pilot_preflight_aligned(uintptr_t pointer) {
    return pointer && (pointer & (sizeof(uintptr_t) - 1)) == 0;
}

static pilot_preflight_result pilot_preflight_collect(
        const pilot_preflight_io *io, uintptr_t universe_pointer,
        uintptr_t expected_player_vtable, const uint64_t saved_ids[2],
        pilot_preflight_snapshot *snapshot) {
    if (!pilot_preflight_read(io, universe_pointer, 0,
                              &snapshot->universe, sizeof(snapshot->universe)) ||
        !pilot_preflight_aligned(snapshot->universe) ||
        !pilot_preflight_read(io, snapshot->universe, PILOT_PREFLIGHT_UNIVERSE_PLAYER,
                              &snapshot->player, sizeof(snapshot->player))) {
        return PILOT_PREFLIGHT_READ;
    }
    if (!pilot_preflight_aligned(snapshot->player) ||
        !pilot_preflight_read(io, snapshot->player, 0,
                              &snapshot->player_vtable, sizeof(snapshot->player_vtable)) ||
        !pilot_preflight_read(io, snapshot->player, PILOT_PREFLIGHT_PLAYER_MACRO,
                              &snapshot->macro, sizeof(snapshot->macro)) ||
        !pilot_preflight_read(io, snapshot->player, PILOT_PREFLIGHT_REFERENCE_COUNT,
                              &snapshot->references, sizeof(snapshot->references)) ||
        !pilot_preflight_aligned(snapshot->macro) ||
        !pilot_preflight_read(io, snapshot->macro, PILOT_PREFLIGHT_MACRO_CLASS,
                              &snapshot->macro_class, sizeof(snapshot->macro_class))) {
        return PILOT_PREFLIGHT_READ;
    }
    if (snapshot->player_vtable != expected_player_vtable ||
        snapshot->macro_class != PILOT_PREFLIGHT_PLAYER_CLASS ||
        snapshot->references <= 0) {
        return PILOT_PREFLIGHT_PLAYER;
    }
    for (unsigned slot = 0; slot < PILOT_PREFLIGHT_SLOT_COUNT; ++slot) {
        pilot_preflight_ship *ship = &snapshot->ships[slot];
        uint64_t observed_runtime_id;
        ship->saved = saved_ids[slot];
        if (!io->resolve(io->context, ship->saved, &ship->runtime, &ship->object) ||
            !ship->runtime ||
            !pilot_preflight_aligned(ship->object) ||
            ship->object == snapshot->player ||
            !pilot_preflight_read(io, ship->object, PILOT_PREFLIGHT_OBJECT_ID,
                                  &observed_runtime_id, sizeof(observed_runtime_id)) ||
            observed_runtime_id != ship->runtime) {
            return PILOT_PREFLIGHT_IDENTITY;
        }
    }
    if (snapshot->ships[0].runtime == snapshot->ships[1].runtime ||
        snapshot->ships[0].object == snapshot->ships[1].object) {
        return PILOT_PREFLIGHT_IDENTITY;
    }
    return PILOT_PREFLIGHT_OK;
}

/* Two matching collections detect many changes, not ABA or object destruction
 * after return. Output is published only after both collections pass. */
static pilot_preflight_result pilot_slot_preflight(
        const pilot_preflight_io *io, uintptr_t universe_pointer,
        uintptr_t expected_player_vtable, const uint64_t saved_ids[2],
        int stable_boundary, pilot_preflight_snapshot *out) {
    if (!io ||
        !io->read ||
        !io->resolve ||
        !saved_ids ||
        !out ||
        !stable_boundary ||
        !pilot_preflight_aligned(universe_pointer) ||
        !pilot_preflight_aligned(expected_player_vtable) ||
        !saved_ids[0] ||
        !saved_ids[1] ||
        saved_ids[0] == saved_ids[1]) {
        return PILOT_PREFLIGHT_ARGUMENT;
    }

    const uint64_t fixture_saved_ids[2] = {saved_ids[0], saved_ids[1]};
    pilot_preflight_snapshot first = {0};
    pilot_preflight_snapshot second = {0};
    pilot_preflight_result result = pilot_preflight_collect(
        io, universe_pointer, expected_player_vtable, fixture_saved_ids, &first);
    if (result != PILOT_PREFLIGHT_OK) {
        return result;
    }
    result = pilot_preflight_collect(
        io, universe_pointer, expected_player_vtable, fixture_saved_ids, &second);
    if (result != PILOT_PREFLIGHT_OK) {
        return result;
    }
    if (first.universe != second.universe ||
        first.player != second.player ||
        first.player_vtable != second.player_vtable ||
        first.macro != second.macro ||
        first.macro_class != second.macro_class ||
        first.references != second.references) {
        return PILOT_PREFLIGHT_CHANGED;
    }
    for (unsigned slot = 0; slot < PILOT_PREFLIGHT_SLOT_COUNT; ++slot) {
        if (first.ships[slot].runtime != second.ships[slot].runtime ||
            first.ships[slot].object != second.ships[slot].object) {
            return PILOT_PREFLIGHT_CHANGED;
        }
    }
    *out = first;
    return PILOT_PREFLIGHT_OK;
}
#endif
