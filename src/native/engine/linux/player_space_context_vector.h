#ifndef X4_PLAYER_SPACE_CONTEXT_VECTOR_H
#define X4_PLAYER_SPACE_CONTEXT_VECTOR_H
/* Pure bounded lookup/CAS core for the pinned InputOwner callback vector.
 * No engine lookup, callback invocation, mprotect, fallback slot or activation.
 * The adapter must authenticate InputOwner and both function addresses, and
 * exclude vector registration/reallocation/free and concurrent callback use
 * across this entire operation. Fresh snapshots and CAS detect changes; they
 * cannot protect a vector allocation's lifetime or make a live hook safe.
 * Any outcome with mutation != NONE is resident: fail-stop on an unverified
 * outcome. There is deliberately no rollback or automatic retry after CAS.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PLAYER_SPACE_VECTOR_MAX 256u

typedef enum {
    PLAYER_SPACE_VECTOR_OK = 0,
    PLAYER_SPACE_VECTOR_ARGUMENT,
    PLAYER_SPACE_VECTOR_EXTENT,
    PLAYER_SPACE_VECTOR_READ,
    PLAYER_SPACE_VECTOR_ORIGINAL_COUNT,
    PLAYER_SPACE_VECTOR_REPLACEMENT_PRESENT,
    PLAYER_SPACE_VECTOR_CHANGED,
    PLAYER_SPACE_VECTOR_WRITE_PERMISSION,
    PLAYER_SPACE_VECTOR_CAS_REJECTED,
    PLAYER_SPACE_VECTOR_CAS_UNCERTAIN,
    PLAYER_SPACE_VECTOR_READBACK,
    PLAYER_SPACE_VECTOR_OWNERSHIP,
    PLAYER_SPACE_VECTOR_ALREADY_ATTEMPTED
} player_space_vector_status;

typedef enum {
    PLAYER_SPACE_VECTOR_MUTATION_NONE = 0,
    PLAYER_SPACE_VECTOR_MUTATION_APPLIED,
    PLAYER_SPACE_VECTOR_MUTATION_UNKNOWN
} player_space_vector_mutation;

typedef enum {
    PLAYER_SPACE_VECTOR_SWAP_REJECTED = 0, /* guaranteed no change by this CAS */
    PLAYER_SPACE_VECTOR_SWAP_APPLIED = 1, /* exact expected value changed */
    PLAYER_SPACE_VECTOR_SWAP_UNKNOWN = 2  /* uncertain backend outcome */
} player_space_vector_swap;

typedef struct {
    void *context;
    int (*range)(void *, uintptr_t address, size_t bytes, unsigned writable);
    int (*read)(void *, uintptr_t address, void *output, size_t bytes);
    player_space_vector_swap (*cas)(void *, uintptr_t address, uint64_t expected, uint64_t replacement);
} player_space_vector_ops;

typedef struct {
    uintptr_t begin, end, capacity, slot;
    size_t count;
    uint64_t entries[PLAYER_SPACE_VECTOR_MAX];
} player_space_vector_snapshot;

typedef struct {
    uintptr_t owner, slot, begin, end, capacity;
    uint64_t original, replacement;
    unsigned attempted, verified;
    player_space_vector_mutation mutation;
    player_space_vector_status status;
} player_space_vector_installation;

static inline player_space_vector_status player_space_vector_snapshot_read(
    const player_space_vector_ops *ops, uintptr_t owner, player_space_vector_snapshot *out) {
    uint64_t header[3];
    if (out) memset(out, 0, sizeof *out);
    if (!ops || !ops->range || !ops->read || !out || !owner || (owner & 7u) ||
        owner > UINTPTR_MAX - 0x28u || sizeof(uintptr_t) != 8) return PLAYER_SPACE_VECTOR_ARGUMENT;
    if (!ops->range(ops->context, owner + 0x10u, sizeof header, 0)) return PLAYER_SPACE_VECTOR_EXTENT;
    if (!ops->read(ops->context, owner + 0x10u, header, sizeof header)) return PLAYER_SPACE_VECTOR_READ;
    uintptr_t begin = (uintptr_t)header[0], end = (uintptr_t)header[1], capacity = (uintptr_t)header[2];
    if (!begin || ((begin | end | capacity) & 7u) || end < begin || capacity < end ||
        capacity - begin > PLAYER_SPACE_VECTOR_MAX * sizeof(uint64_t)) return PLAYER_SPACE_VECTOR_EXTENT;
    size_t bytes = (size_t)(end - begin), allocated = (size_t)(capacity - begin);
    if (!allocated || !ops->range(ops->context, begin, allocated, 0)) return PLAYER_SPACE_VECTOR_EXTENT;
    if (bytes && !ops->read(ops->context, begin, out->entries, bytes)) return PLAYER_SPACE_VECTOR_READ;
    /* A moved header invalidates every slot just read. Never keep a candidate
     * when the second header read fails or any extent changed. */
    uint64_t after[3];
    if (!ops->read(ops->context, owner + 0x10u, after, sizeof after)) return PLAYER_SPACE_VECTOR_READ;
    if (memcmp(header, after, sizeof header)) return PLAYER_SPACE_VECTOR_CHANGED;
    out->begin = begin;
    out->end = end;
    out->capacity = capacity;
    out->count = bytes / sizeof(uint64_t);
    return PLAYER_SPACE_VECTOR_OK;
}

static inline player_space_vector_status player_space_vector_lookup(
    const player_space_vector_ops *ops, uintptr_t owner, uint64_t original, uint64_t replacement,
    player_space_vector_snapshot *out) {
    if (out) memset(out, 0, sizeof *out);
    if (!original || !replacement || original == replacement || !out) return PLAYER_SPACE_VECTOR_ARGUMENT;
    player_space_vector_status status = player_space_vector_snapshot_read(ops, owner, out);
    if (status != PLAYER_SPACE_VECTOR_OK) return status;
    size_t found = 0, replacements = 0;
    uintptr_t slot = 0;
    for (size_t index = 0; index < out->count; ++index) {
        if (out->entries[index] == original) {
            ++found;
            slot = out->begin + index * sizeof(uint64_t);
        }
        if (out->entries[index] == replacement) ++replacements;
    }
    if (replacements) return PLAYER_SPACE_VECTOR_REPLACEMENT_PRESENT;
    if (found != 1) return PLAYER_SPACE_VECTOR_ORIGINAL_COUNT;
    out->slot = slot; /* Publish only the one fresh, completely checked match. */
    return PLAYER_SPACE_VECTOR_OK;
}

static inline int player_space_vector_same(const player_space_vector_snapshot *first,
                                           const player_space_vector_snapshot *second) {
    return first->begin == second->begin && first->end == second->end && first->capacity == second->capacity &&
        first->count == second->count && !memcmp(first->entries, second->entries, first->count * sizeof(uint64_t));
}

static inline player_space_vector_status player_space_vector_install(
    const player_space_vector_ops *ops, uintptr_t owner, uint64_t original, uint64_t replacement,
    player_space_vector_installation *installation) {
    if (!installation || !ops || !ops->cas) return PLAYER_SPACE_VECTOR_ARGUMENT;
    if (installation->attempted || installation->mutation != PLAYER_SPACE_VECTOR_MUTATION_NONE || installation->verified)
        return PLAYER_SPACE_VECTOR_ALREADY_ATTEMPTED;
    memset(installation, 0, sizeof *installation);
    player_space_vector_snapshot first, fresh, after;
    player_space_vector_status status = player_space_vector_lookup(ops, owner, original, replacement, &first);
    if (status != PLAYER_SPACE_VECTOR_OK) return installation->status = status;
    status = player_space_vector_lookup(ops, owner, original, replacement, &fresh);
    if (status != PLAYER_SPACE_VECTOR_OK) return installation->status = status;
    if (!player_space_vector_same(&first, &fresh) || first.slot != fresh.slot)
        return installation->status = PLAYER_SPACE_VECTOR_CHANGED;
    if (!ops->range(ops->context, fresh.slot, sizeof(uint64_t), 1))
        return installation->status = PLAYER_SPACE_VECTOR_WRITE_PERMISSION;
    installation->owner = owner;
    installation->slot = fresh.slot;
    installation->begin = fresh.begin;
    installation->end = fresh.end;
    installation->capacity = fresh.capacity;
    installation->original = original;
    installation->replacement = replacement;
    installation->attempted = 1;
    player_space_vector_swap swapped = ops->cas(ops->context, fresh.slot, original, replacement);
    if (swapped == PLAYER_SPACE_VECTOR_SWAP_REJECTED)
        return installation->status = PLAYER_SPACE_VECTOR_CAS_REJECTED;
    if (swapped != PLAYER_SPACE_VECTOR_SWAP_APPLIED) {
        installation->mutation = PLAYER_SPACE_VECTOR_MUTATION_UNKNOWN;
        return installation->status = PLAYER_SPACE_VECTOR_CAS_UNCERTAIN;
    }
    installation->mutation = PLAYER_SPACE_VECTOR_MUTATION_APPLIED;
    uint64_t actual = 0;
    if (!ops->read(ops->context, fresh.slot, &actual, sizeof actual))
        return installation->status = PLAYER_SPACE_VECTOR_READBACK;
    if (actual != replacement) return installation->status = PLAYER_SPACE_VECTOR_OWNERSHIP;
    status = player_space_vector_snapshot_read(ops, owner, &after);
    if (status != PLAYER_SPACE_VECTOR_OK) return installation->status = PLAYER_SPACE_VECTOR_OWNERSHIP;
    fresh.entries[(fresh.slot - fresh.begin) / sizeof(uint64_t)] = replacement;
    if (!player_space_vector_same(&fresh, &after)) return installation->status = PLAYER_SPACE_VECTOR_OWNERSHIP;
    installation->verified = 1;
    return installation->status = PLAYER_SPACE_VECTOR_OK;
}
#endif
