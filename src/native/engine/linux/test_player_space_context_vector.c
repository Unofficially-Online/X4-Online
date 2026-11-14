#include <assert.h>
#include <stdio.h>
#include "player_space_context_vector.h"

#define ORIGINAL UINT64_C(0x165d950)
#define REPLACEMENT UINT64_C(0x800000)
#define FOREIGN UINT64_C(0x900000)

typedef struct {
    _Alignas(8) uint64_t owner[5];
    _Alignas(8) uint64_t vector[PLAYER_SPACE_VECTOR_MAX + 1];
    unsigned reads, ranges, swaps, fail_read, fail_range;
    unsigned change_before_read, move_header_before_read, change_after_cas, reject, uncertain;
    uintptr_t cas_slot;
} fixture;

static void setup(fixture *state, unsigned count) {
    memset(state, 0, sizeof *state);
    state->owner[2] = (uintptr_t)state->vector;
    state->owner[3] = (uintptr_t)(state->vector + count);
    state->owner[4] = (uintptr_t)(state->vector + count);
    for (unsigned index = 0; index < count; ++index) state->vector[index] = 0x100000u + index;
    if (count) state->vector[count / 2] = ORIGINAL;
}
static int contains(uintptr_t start, size_t size, uintptr_t address, size_t bytes) {
    return address >= start && bytes <= size && address - start <= size - bytes;
}
static int readable(fixture *state, uintptr_t address, size_t bytes) {
    return contains((uintptr_t)state->owner + 0x10u, 24, address, bytes) ||
        contains((uintptr_t)state->vector, sizeof state->vector, address, bytes);
}
static int range(void *context, uintptr_t address, size_t bytes, unsigned writable) {
    fixture *state = context;
    if (++state->ranges == state->fail_range) return 0;
    return readable(state, address, bytes) && (!writable || contains((uintptr_t)state->vector, sizeof state->vector, address, bytes));
}
static int read_value(void *context, uintptr_t address, void *out, size_t bytes) {
    fixture *state = context;
    if (++state->reads == state->fail_read) return 0;
    if (state->reads == state->change_before_read) state->vector[0] ^= 1u;
    if (state->reads == state->move_header_before_read) state->owner[3] -= 8;
    if (!readable(state, address, bytes)) return 0;
    memcpy(out, (void *)address, bytes);
    return 1;
}
static player_space_vector_swap cas(void *context, uintptr_t address, uint64_t expected, uint64_t replacement) {
    fixture *state = context;
    ++state->swaps;
    state->cas_slot = address;
    assert(!(address & 7u) && readable(state, address, 8));
    assert(expected == ORIGINAL && replacement == REPLACEMENT);
    if (state->uncertain) return PLAYER_SPACE_VECTOR_SWAP_UNKNOWN;
    if (state->reject) *(uint64_t *)address = FOREIGN;
    int changed = __atomic_compare_exchange_n((uint64_t *)address, &expected, replacement, 0,
                                              __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    if (!changed) return PLAYER_SPACE_VECTOR_SWAP_REJECTED;
    if (state->change_after_cas == 1) *(uint64_t *)address = FOREIGN;
    if (state->change_after_cas == 2) state->owner[3] -= 8;
    if (state->change_after_cas == 3) state->vector[0] = REPLACEMENT;
    return PLAYER_SPACE_VECTOR_SWAP_APPLIED;
}
static player_space_vector_ops ops(fixture *state) {
    return (player_space_vector_ops){state, range, read_value, cas};
}
static player_space_vector_status install(fixture *state, player_space_vector_installation *out) {
    player_space_vector_ops api = ops(state);
    return player_space_vector_install(&api, (uintptr_t)state->owner, ORIGINAL, REPLACEMENT, out);
}
static void successful_bounds(void) {
    fixture state;
    for (unsigned count = 1; count <= 256; count = count == 1 ? 256 : 257) {
        setup(&state, count);
        uint64_t before[256]; memcpy(before, state.vector, sizeof before);
        player_space_vector_installation result = {0};
        assert(install(&state, &result) == PLAYER_SPACE_VECTOR_OK);
        assert(result.attempted && result.verified && result.mutation == PLAYER_SPACE_VECTOR_MUTATION_APPLIED);
        assert(result.slot == (uintptr_t)&state.vector[count / 2]);
        assert(state.swaps == 1 && state.cas_slot == result.slot);
        for (unsigned index = 0; index < count; ++index)
            assert(state.vector[index] == (index == count / 2 ? REPLACEMENT : before[index]));
        unsigned swaps = state.swaps;
        assert(install(&state, &result) == PLAYER_SPACE_VECTOR_ALREADY_ATTEMPTED && state.swaps == swaps);
    }
    setup(&state, 3);
    state.owner[4] = (uintptr_t)(state.vector + 256);
    state.vector[255] = ORIGINAL; /* spare capacity is not a registered entry */
    player_space_vector_installation result = {0};
    assert(install(&state, &result) == PLAYER_SPACE_VECTOR_OK && result.verified);
    assert(state.vector[255] == ORIGINAL);
}
static void invalid_extents(void) {
    fixture state;
    for (unsigned variant = 0; variant < 9; ++variant) {
        setup(&state, 3);
        if (variant == 0) state.owner[2] = 0;
        if (variant == 1) ++state.owner[2];
        if (variant == 2) --state.owner[3];
        if (variant == 3) ++state.owner[4];
        if (variant == 4) state.owner[3] = state.owner[2] - 8;
        if (variant == 5) state.owner[4] = state.owner[3] - 8;
        if (variant == 6) state.owner[4] = state.owner[2] + 257 * 8;
        if (variant == 7) state.owner[4] = state.owner[2];
        if (variant == 8) { state.owner[2] = UINTPTR_MAX - 15u; state.owner[3] = state.owner[4] = UINTPTR_MAX - 7u; }
        player_space_vector_installation result = {0};
        assert(install(&state, &result) == PLAYER_SPACE_VECTOR_EXTENT);
        assert(!state.swaps && !result.slot && !result.attempted && !result.verified);
    }
    setup(&state, 3);
    player_space_vector_ops api = ops(&state);
    player_space_vector_snapshot snapshot;
    memset(&snapshot, 0xff, sizeof snapshot);
    assert(player_space_vector_lookup(&api, (uintptr_t)state.owner + 1, ORIGINAL, REPLACEMENT, &snapshot) == PLAYER_SPACE_VECTOR_ARGUMENT);
    assert(!snapshot.slot);
    memset(&snapshot, 0xff, sizeof snapshot);
    assert(player_space_vector_lookup(&api, UINTPTR_MAX - 7u, ORIGINAL, REPLACEMENT, &snapshot) == PLAYER_SPACE_VECTOR_ARGUMENT);
    assert(!snapshot.slot);
    assert(player_space_vector_lookup(&api, (uintptr_t)state.owner, ORIGINAL, ORIGINAL, &snapshot) == PLAYER_SPACE_VECTOR_ARGUMENT);
    assert(!snapshot.slot);
}
static void unique_fresh_original(void) {
    fixture state;
    for (unsigned variant = 0; variant < 4; ++variant) {
        setup(&state, 3);
        if (variant == 0) state.vector[1] = FOREIGN;
        if (variant == 1) state.vector[0] = ORIGINAL;
        if (variant == 2) state.vector[0] = REPLACEMENT;
        if (variant == 3) state.owner[3] = state.owner[2];
        player_space_vector_installation result = {0};
        player_space_vector_status expected = variant == 2 ? PLAYER_SPACE_VECTOR_REPLACEMENT_PRESENT : PLAYER_SPACE_VECTOR_ORIGINAL_COUNT;
        assert(install(&state, &result) == expected);
        assert(!state.swaps && !result.slot && result.mutation == PLAYER_SPACE_VECTOR_MUTATION_NONE);
    }
    setup(&state, 3);
    state.change_before_read = 5; /* second full vector differs from the first */
    player_space_vector_installation result = {0};
    assert(install(&state, &result) == PLAYER_SPACE_VECTOR_CHANGED && !state.swaps);
    setup(&state, 3);
    state.move_header_before_read = 3;
    memset(&result, 0, sizeof result);
    assert(install(&state, &result) == PLAYER_SPACE_VECTOR_CHANGED && !state.swaps && !result.slot);
}
static void unavailable_reads_and_write(void) {
    fixture state;
    for (unsigned failed = 1; failed <= 6; ++failed) {
        setup(&state, 3); state.fail_read = failed;
        player_space_vector_installation result = {0};
        assert(install(&state, &result) == PLAYER_SPACE_VECTOR_READ);
        assert(!state.swaps && !result.attempted && !result.slot);
    }
    for (unsigned failed = 1; failed <= 5; ++failed) {
        setup(&state, 3); state.fail_range = failed;
        player_space_vector_installation result = {0};
        assert(install(&state, &result) == (failed == 5 ? PLAYER_SPACE_VECTOR_WRITE_PERMISSION : PLAYER_SPACE_VECTOR_EXTENT));
        assert(!state.swaps && !result.attempted && !result.slot);
    }
}
static void explicit_mutation_outcomes(void) {
    fixture state;
    for (unsigned variant = 0; variant < 7; ++variant) {
        setup(&state, 3);
        if (variant == 0) state.reject = 1;
        if (variant == 1) state.uncertain = 1;
        if (variant == 2) state.fail_read = 7;
        if (variant >= 3 && variant <= 5) state.change_after_cas = variant - 2;
        if (variant == 6) state.fail_read = 9;
        player_space_vector_installation result = {0};
        player_space_vector_status expected = variant == 0 ? PLAYER_SPACE_VECTOR_CAS_REJECTED :
            variant == 1 ? PLAYER_SPACE_VECTOR_CAS_UNCERTAIN : variant == 2 ? PLAYER_SPACE_VECTOR_READBACK : PLAYER_SPACE_VECTOR_OWNERSHIP;
        assert(install(&state, &result) == expected && state.swaps == 1);
        assert(result.attempted && !result.verified);
        assert(result.mutation == (variant == 0 ? PLAYER_SPACE_VECTOR_MUTATION_NONE :
                                   variant == 1 ? PLAYER_SPACE_VECTOR_MUTATION_UNKNOWN : PLAYER_SPACE_VECTOR_MUTATION_APPLIED));
        assert(install(&state, &result) == PLAYER_SPACE_VECTOR_ALREADY_ATTEMPTED && state.swaps == 1);
    }
}
int main(void) {
    successful_bounds(); invalid_extents(); unique_fresh_original();
    unavailable_reads_and_write(); explicit_mutation_outcomes();
    puts("SPACE vector bounds, unique lookup, exact CAS and explicit ownership failure PASS");
    return 0;
}
