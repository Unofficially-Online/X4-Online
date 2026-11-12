#ifndef X4_GATE_PLAYER_CONTROL_BANK_SCOPE_H
#define X4_GATE_PLAYER_CONTROL_BANK_SCOPE_H

/* Default unused. Include after gate_player_zone_scope.h and the census header.
 * One resident, serialized, immediate same-zone transaction; NO native Update.
 * The caller's admit callback must freshly verify current phase/provenance,
 * pinned identity and component lifetimes, exclusion of all writers/readers,
 * keyboard-only state and the unchanged canonical input request tree. It must
 * cover initialization, every callback and cleanup, including P2's lifetime
 * transition. It is not enough to compare a thread number. The callback and its
 * context stay alive while any obligation remains. None of these checks proves
 * opaque bytes are reference-free or rolls back native controller side effects.
 * Never reuse/reset this resident object or resume simulation with obligations.
 * On an unresolved native zone effect preserve all state and discard the
 * disposable process. Only the caller may certify the input-tree policy. */
#include "player_control_banks.h"

typedef struct {
    void *context;
    int (*admit)(void *context, pcb_phase phase);
    unsigned char enabled;
    unsigned char contexts;
} player_bank_policy;

typedef struct {
    unsigned started;
    unsigned ready;
    unsigned closed;
    uint64_t thread;
    uint32_t buffer_index;
    player_bank_policy policy;
    pcb_slot slot;
    pcb_scope bank;
} player_bank_scope;

static player_bank_scope player_bank_resident;

static int player_bank_admission(player_bank_scope *scope) {
    return scope == &player_bank_resident && scope->started && !scope->closed &&
           GetCurrentThreadId() == scope->thread && factory_probe_joined_boundary() &&
           player_bank_census_pins(NULL) && scope->policy.admit &&
           scope->policy.admit(scope->policy.context, scope->bank.phase);
}

static int player_bank_guards(player_bank_scope *scope) {
    unsigned char reentry = 1;
    unsigned char locks[3] = {1, 1, 1};
    uint32_t buffer_index = 0;
    uintptr_t handle_a = 1;
    uintptr_t handle_b = 1;
    return player_bank_census_mapped(NULL, 0x3ee9b40, 1) &&
           player_bank_census_mapped(NULL, 0x3e5b298, 3) &&
           player_bank_census_mapped(NULL, 0x3d8e254, 4) &&
           player_bank_census_mapped(NULL, 0x3ed1140, 8) &&
           player_bank_census_mapped(NULL, 0x3ee2dc0, 8) &&
           player_bank_admission(scope) && read_local(LX(0x3ee9b40), &reentry, 1) && !reentry &&
           player_bank_admission(scope) && read_local(LX(0x3e5b298), locks, sizeof(locks)) &&
           !locks[0] && !locks[1] && !locks[2] &&
           player_bank_admission(scope) && read_local(LX(0x3d8e254), &buffer_index, sizeof(buffer_index)) &&
           buffer_index == scope->buffer_index &&
           player_bank_admission(scope) && read_local(LX(0x3ed1140), &handle_a, sizeof(handle_a)) && !handle_a &&
           player_bank_admission(scope) && read_local(LX(0x3ee2dc0), &handle_b, sizeof(handle_b)) && !handle_b;
}

static int player_bank_authorized(player_bank_scope *scope) {
    return player_bank_admission(scope) && player_bank_guards(scope);
}

static int player_bank_boundary(void *context) {
    player_bank_scope *scope = context;
    if (!player_bank_authorized(scope)) {
        return 0;
    }
    if (player_zone_resident.ledger.initialized) {
        uint32_t count = 0;
        return player_zone_snapshot(&player_zone_resident, &count);
    }
    return 1;
}

static uint64_t player_bank_thread(void *context) {
    (void)context;
    return (uint64_t)GetCurrentThreadId();
}

static int player_bank_guard_callback(void *context) {
    return player_bank_boundary(context);
}

/* All destination addresses are derived only from the fixed descriptors. */
static int player_bank_range(unsigned region, unsigned offset, unsigned length,
                             unsigned *address) {
    if (region >= PCB_REGION_COUNT || !length ||
        offset >= pcb_regions[region].length ||
        length > pcb_regions[region].length - offset) {
        return 0;
    }
    *address = pcb_regions[region].va + offset;
    return 1;
}

static int player_bank_read(void *context, unsigned region, unsigned offset,
                            unsigned char *bytes, unsigned length) {
    unsigned address = 0;
    if (!bytes || !player_bank_range(region, offset, length, &address) ||
        !player_bank_boundary(context) ||
        !player_bank_census_mapped(NULL, address, length)) {
        return 0;
    }
    return read_local(LX(address), bytes, length) && player_bank_boundary(context);
}

static pcb_write_result player_bank_replace_byte(void *context, unsigned region,
                                                 unsigned offset, unsigned char expected,
                                                 unsigned char replacement) {
    unsigned address = 0;
    if (!player_bank_range(region, offset, 1, &address) ||
        !player_bank_boundary(context) || !player_bank_census_mapped(NULL, address, 1) ||
        !player_bank_boundary(context)) {
        return PCB_NOT_CHANGED;
    }
    unsigned char actual = expected;
    unsigned char *target = (unsigned char *)LX(address);
    if (!__atomic_compare_exchange_n(target, &actual, replacement, 0,
                                     __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
        return PCB_NOT_CHANGED;
    }
    unsigned char verified = 0;
    if (!player_bank_boundary(context) || !read_local(target, &verified, 1) ||
        verified != replacement || !player_bank_boundary(context)) {
        return PCB_UNKNOWN;
    }
    return PCB_CHANGED;
}

/* The zone ledger callbacks are additionally gated by the same caller policy.
 * Their own native boundary, mapping, image and zone checks remain unchanged. */
static int player_bank_zone_boundary(void *context) {
    return context == &player_zone_resident && player_bank_authorized(&player_bank_resident) &&
           player_zone_boundary(context);
}
static int player_bank_zone_inspect(void *context, pz_location location) {
    return player_bank_zone_boundary(context) && player_zone_inspect(context, location);
}
static pz_effect player_bank_zone_assign(void *context, pz_location destination,
                                         pz_location source) {
    if (!player_bank_zone_boundary(context)) {
        return PZ_NO_EFFECT;
    }
    return player_zone_assign(context, destination, source);
}
static pcb_zone_result player_bank_zone_enter(void *context, const pcb_slot *slot) {
    if (!player_bank_boundary(context)) {
        return player_zone_resident.ledger.phase == PZ_IDLE
            ? PCB_ZONE_INACTIVE : PCB_ZONE_UNCERTAIN;
    }
    return pz_enter(&player_zone_resident.ledger, slot);
}
static int player_bank_zone_restore(void *context, const pcb_slot *slot) {
    if (!player_bank_boundary(context)) {
        return 0;
    }
    return pz_restore(&player_zone_resident.ledger, slot);
}
/* Anchor only from the native owned global; the scalar anchor never acquires
 * a reference. Install the restricted callbacks before the first assignment. */
static int player_bank_initialize_zone(const pcb_slot *slot) {
    player_zone_scope *zone = &player_zone_resident;
    uintptr_t anchor = 0;
    uint64_t runtime = 0;
    uint32_t count = 0;
    uint32_t marker = 0;
    if (zone->ledger.initialized || !player_bank_authorized(&player_bank_resident) ||
        !player_zone_pins() || !read_local(LX(PLAYER_ZONE_GLOBAL_VA), &anchor, sizeof(anchor)) ||
        !anchor || anchor > UINTPTR_MAX - 0x48 ||
        !read_local((void *)(anchor + 8), &runtime, sizeof(runtime)) || !runtime ||
        !read_local((void *)(anchor + 0x40), &count, sizeof(count)) || !count ||
        !read_local((void *)(anchor + 0x44), &marker, sizeof(marker)) || marker != 2) {
        return 0;
    }
    uint64_t saved = world_identity_lookup(runtime);
    if (!saved || !player_bank_authorized(&player_bank_resident)) {
        return 0;
    }
    zone->anchor = anchor;
    zone->runtime = runtime;
    zone->saved = saved;
    pz_operations operations = {zone, player_zone_thread, player_bank_zone_boundary,
                                player_bank_zone_inspect, player_bank_zone_assign};
    return pz_initialize(&zone->ledger, slot, &operations, player_zone_thread(zone)) &&
           pz_initialize_slot(&zone->ledger);
}

/* Caller certifies exact bool policy; all other 994 bytes retain canonical
 * values. Two full reads detect changes, but do not prove exclusion or ABA. */
static int player_bank_initialize(const player_bank_policy *policy) {
    player_bank_scope *scope = &player_bank_resident;
    if (scope->started || !policy || !policy->admit || policy->enabled > 1 ||
        policy->contexts > 1 || !factory_probe_joined_boundary() ||
        !player_bank_census_pins(NULL) ||
        !player_bank_census_mapped(NULL, 0x3d8e254, 4)) {
        return 0;
    }
    scope->started = 1;
    scope->thread = GetCurrentThreadId();
    scope->policy = *policy;
    if (!player_bank_admission(scope) || !read_local(LX(0x3d8e254), &scope->buffer_index, sizeof(scope->buffer_index)) ||
        !player_bank_authorized(scope)) {
        return 0;
    }
    pcb_operations operations = {scope, player_bank_thread, player_bank_boundary,
        player_bank_guard_callback, player_bank_read, player_bank_replace_byte,
        player_bank_zone_enter, player_bank_zone_restore};
    if (!pcb_scope_initialize(&scope->bank, &operations, scope->thread)) {
        return 0;
    }
    unsigned char initial[PCB_VALUE_BYTES];
    unsigned char verified[PCB_VALUE_BYTES];
    if (!pcb_read_values(&scope->bank, initial) || !pcb_read_values(&scope->bank, verified) ||
        memcmp(initial, verified, sizeof(initial)) || !player_bank_boundary(scope)) {
        return 0;
    }
    memcpy(scope->slot.bytes, initial, sizeof(initial));
    scope->slot.bytes[0xc0] = policy->enabled;
    scope->slot.bytes[0x109] = policy->contexts;
    scope->slot.initialized = 1;
    if (!player_bank_authorized(scope)) {
        return 0;
    }
    int initialized = player_bank_initialize_zone(&scope->slot);
    scope->ready = initialized && player_bank_boundary(scope);
    return scope->ready;
}

/* These helpers retain all state on failure. Cleanup may require retry at the
 * admitted boundary; a poisoned zone must never be retried or discarded early. */
static int player_bank_enter(void) {
    return player_bank_resident.ready &&
           pcb_enter(&player_bank_resident.bank, &player_bank_resident.slot);
}
static int player_bank_exit(void) {
    return pcb_exit(&player_bank_resident.bank);
}
static int player_bank_restore(void) {
    return pcb_restore(&player_bank_resident.bank);
}
static int player_bank_close(void) {
    player_bank_scope *scope = &player_bank_resident;
    if (!player_bank_authorized(scope) ||
        (scope->bank.initialized && scope->bank.phase != PCB_IDLE)) {
        return 0;
    }
    if (!player_zone_resident.ledger.initialized) {
        scope->closed = 1;
        return 1;
    }
    if (player_zone_resident.ledger.phase == PZ_INITIALIZING &&
        !pz_initialize_slot(&player_zone_resident.ledger)) {
        return 0;
    }
    if (!pz_destroy(&player_zone_resident.ledger)) {
        return 0;
    }
    scope->ready = 0;
    scope->closed = 1;
    return 1;
}
#endif
