#ifndef X4_GATE_PLAYER_BANK_ROUNDTRIP_H
#define X4_GATE_PLAYER_BANK_ROUNDTRIP_H

/* Default unused canonical-bank diagnostic. Include after the bank adapter and
 * stock identity. No P2, controller lifecycle, Update or input dispatch.
 * The caller must exclude ALL input-tree and bank writers/readers, reentry and
 * object destruction throughout every observation and operation. Repeated
 * reads do not prove exclusion, lifetime or absence of ABA. Only immutable code
 * pins may be cached: the joined boundary must also exclude code mutation until
 * final pin validation. Failure means stop, retain state and do not resume the
 * simulation. This diagnostic does not prove independent player controls. */
#include "input_context_census.h"
#include <unistd.h>

#define BANK_ROUNDTRIP_REQUEST_TICK 2u
#define BANK_ROUNDTRIP_CAPTURE_TICK 5u
#define BANK_ROUNDTRIP_TREE_READS (INPUT_CONTEXT_CENSUS_MAX_NODES + 2u)

typedef struct {
    uintptr_t address;
    unsigned length;
    unsigned char bytes[0x40];
} bank_roundtrip_tree_entry;

typedef struct {
    unsigned count;
    bank_roundtrip_tree_entry reads[BANK_ROUNDTRIP_TREE_READS];
} bank_roundtrip_tree;

typedef struct {
    unsigned requested;
    unsigned attempted;
    unsigned complete;
    unsigned cleanup_complete;
    unsigned pins_cached;
    unsigned canonical_bytes_restored;
    unsigned input_tree_unchanged;
    unsigned temporary_seen;
    uint64_t captured_tick;
    const char *temporary_reason;
    uint32_t zone_count_after;
    const char *reason;
    uint64_t owner_thread;
    stock_keyboard_identity_sample identity;
    unsigned char actual_space;
    const char *actual_identity_reason;
    input_context_census_sample tree_sample;
    bank_roundtrip_tree tree;
    bank_roundtrip_tree observed_tree;
    unsigned char canonical[PCB_VALUE_BYTES];
    uintptr_t zone;
    uint64_t zone_runtime;
    uint64_t zone_saved;
    uint32_t zone_count;
} bank_roundtrip_probe;

static bank_roundtrip_probe bank_roundtrip_resident;

static inline int bank_roundtrip_fail(bank_roundtrip_probe *probe, const char *reason) {
    if (!probe->reason) {
        probe->reason = reason;
    }
    return 0;
}

static inline int bank_roundtrip_cached_identity_pins(void) {
    static const unsigned slots[][2] = {
        {0x3b61480, 0x136c820},
        {0x3b7c258, 0xd586b0},
        {0x3b7c2d8, 0xa863a0},
        {0x3d22938, 0xa389f0},
    };
    if (!bank_roundtrip_resident.pins_cached) {
        return 0;
    }
    for (unsigned index = 0; index < sizeof(slots) / sizeof(slots[0]); ++index) {
        uintptr_t actual = 0;
        if (!read_local(LX(slots[index][0]), &actual, sizeof(actual)) ||
            actual != (uintptr_t)LX(slots[index][1])) {
            return 0;
        }
    }
    return 1;
}

/* Use the same pinned getters and native reads as the existing collector. Only
 * immutable code pins are cached during the excluded interval and rechecked at
 * the end. The actual space/ready/reason are retained as observations. */
static inline int bank_roundtrip_identity(bank_roundtrip_probe *probe, pcb_phase phase) {
    const stock_keyboard_identity_ops operations = {
        bank_roundtrip_cached_identity_pins, stock_keyboard_identity_read,
        stock_keyboard_identity_address, stock_keyboard_identity_getter,
        world_identity_lookup, sko_session.owner,
    };
    stock_keyboard_identity_sample observed;
    if (!stock_keyboard_identity_collect_with(&observed, &operations)) {
        return bank_roundtrip_fail(probe, "identity_read");
    }
    probe->actual_space = observed.requested_space;
    probe->actual_identity_reason = observed.reason;
    if (!observed.requested_space) {
        probe->temporary_seen = 1;
        probe->temporary_reason = observed.reason;
    }
    if ((phase == PCB_ACTIVE && observed.requested_space != 0) ||
        (phase == PCB_IDLE && observed.requested_space != 1) ||
        ((phase == PCB_ENTERING || phase == PCB_RESTORING) && observed.requested_space > 1)) {
        return bank_roundtrip_fail(probe, "identity_space");
    }
    stock_keyboard_identity_sample expected = probe->identity;
    expected.ready = observed.ready = 0;
    expected.reason = observed.reason = NULL;
    expected.requested_space = observed.requested_space = 0;
    if (memcmp(&expected, &observed, sizeof(expected))) {
        return bank_roundtrip_fail(probe, "identity_changed");
    }
    return 1;
}

/* Census validates the whole tree; capture every exact read including headers,
 * node addresses, links, color and all 16 payload bytes. The order is the
 * census traversal order. Existing target node identities are never adopted. */
static inline int bank_roundtrip_tree_read(void *context, uintptr_t address,
                                           void *output, size_t length) {
    bank_roundtrip_tree *tree = context;
    if (tree->count >= BANK_ROUNDTRIP_TREE_READS || length > 0x40 ||
        GetCurrentThreadId() != bank_roundtrip_resident.owner_thread ||
        stats.issued != BANK_ROUNDTRIP_CAPTURE_TICK || !factory_probe_joined_boundary() ||
        !read_local((const void *)address, output, length)) {
        return 0;
    }
    bank_roundtrip_tree_entry *entry = &tree->reads[tree->count++];
    entry->address = address;
    entry->length = (unsigned)length;
    memcpy(entry->bytes, output, length);
    return 1;
}

static inline int bank_roundtrip_tree_capture(uintptr_t manager, bank_roundtrip_tree *tree,
                                               input_context_census_sample *sample) {
    memset(tree, 0, sizeof(*tree));
    const input_context_census_operations operations = {tree, bank_roundtrip_tree_read};
    return input_context_census_collect(&operations, manager, sample);
}

static inline int bank_roundtrip_tree_matches(bank_roundtrip_probe *probe) {
    input_context_census_sample sample;
    if (!bank_roundtrip_tree_capture(probe->identity.input_owner + 0x10,
                                      &probe->observed_tree, &sample)) {
        return bank_roundtrip_fail(probe, "tree_read");
    }
    if (probe->observed_tree.count != probe->tree.count ||
        memcmp(probe->observed_tree.reads, probe->tree.reads,
               probe->tree.count * sizeof(probe->tree.reads[0])) ||
        sample.node_count != probe->tree_sample.node_count ||
        sample.target_present != probe->tree_sample.target_present ||
        memcmp(sample.payloads, probe->tree_sample.payloads, sizeof(sample.payloads))) {
        return bank_roundtrip_fail(probe, "tree_changed");
    }
    return 1;
}

static inline int bank_roundtrip_admit(void *context, pcb_phase phase) {
    bank_roundtrip_probe *probe = context;
    if (!probe) {
        return 0;
    }
    if (probe != &bank_roundtrip_resident || !probe->attempted ||
        stats.issued != BANK_ROUNDTRIP_CAPTURE_TICK ||
        GetCurrentThreadId() != probe->owner_thread || !factory_probe_joined_boundary()) {
        return bank_roundtrip_fail(probe, "boundary");
    }
    return bank_roundtrip_identity(probe, phase) && bank_roundtrip_tree_matches(probe) &&
           factory_probe_joined_boundary();
}

static inline int bank_roundtrip_read_values(bank_roundtrip_probe *probe,
                                              unsigned char output[PCB_VALUE_BYTES]) {
    for (unsigned index = 0; index < PCB_REGION_COUNT; ++index) {
        const pcb_region *region = &pcb_regions[index];
        if (!bank_roundtrip_admit(probe, PCB_IDLE) ||
            !player_bank_census_mapped(NULL, region->va, region->length) ||
            !read_local(LX(region->va), output + region->offset, region->length)) {
            return bank_roundtrip_fail(probe, "bank_read");
        }
    }
    return bank_roundtrip_admit(probe, PCB_IDLE);
}

static inline int bank_roundtrip_zone(bank_roundtrip_probe *probe, int initial) {
    uintptr_t zone = 0;
    uint32_t count = 0;
    uint32_t marker = 0;
    uint64_t runtime = 0;
    if (!player_bank_census_mapped(NULL, PLAYER_ZONE_GLOBAL_VA, sizeof(zone)) ||
        !read_local(LX(PLAYER_ZONE_GLOBAL_VA), &zone, sizeof(zone)) || !zone ||
        zone > UINTPTR_MAX - 0x48 ||
        !read_local((void *)(zone + 8), &runtime, sizeof(runtime)) || !runtime ||
        !read_local((void *)(zone + 0x40), &count, sizeof(count)) || !count ||
        !read_local((void *)(zone + 0x44), &marker, sizeof(marker)) || marker != 2) {
        return bank_roundtrip_fail(probe, "zone_read");
    }
    uint64_t saved = world_identity_lookup(runtime);
    if (!saved) {
        return bank_roundtrip_fail(probe, "zone_identity");
    }
    if (initial) {
        probe->zone = zone;
        probe->zone_runtime = runtime;
        probe->zone_saved = saved;
        probe->zone_count = count;
        return 1;
    }
    probe->zone_count_after = count;
    if (zone != probe->zone || runtime != probe->zone_runtime ||
        saved != probe->zone_saved || count != probe->zone_count) {
        return bank_roundtrip_fail(probe, "zone_changed");
    }
    return 1;
}

/* Root owns request timing and hook; this helper does not install either. */
static inline int bank_roundtrip_request(void) {
    bank_roundtrip_probe *probe = &bank_roundtrip_resident;
    if (probe->requested || probe->attempted || stats.issued != BANK_ROUNDTRIP_REQUEST_TICK ||
        !stock_observer_worker_boundary() || !stats.active) {
        return 0;
    }
    probe->requested = 1;
    return 1;
}

/* Safe, bounded cleanup only. An ACTIVE bank must first capture through exit;
 * a RESTORING bank may retry its recorded obligations. Poisoned zone effects
 * cannot be resolved by this diagnostic. Never erase the first failure. */
static inline int bank_roundtrip_finish(void) {
    bank_roundtrip_probe *probe = &bank_roundtrip_resident;
    player_bank_scope *scope = &player_bank_resident;
    if (!probe->attempted || !bank_roundtrip_admit(probe, scope->bank.phase)) {
        return 0;
    }
    if (scope->bank.phase == PCB_ACTIVE && !player_bank_exit()) {
        return bank_roundtrip_fail(probe, "exit_pending");
    }
    if (scope->bank.phase == PCB_RESTORING && !player_bank_restore()) {
        return bank_roundtrip_fail(probe, "restore_pending");
    }
    if (scope->bank.phase != PCB_IDLE) {
        return bank_roundtrip_fail(probe, "bank_pending");
    }
    if (scope->started && !scope->closed && !player_bank_close()) {
        return bank_roundtrip_fail(probe, "close_pending");
    }
    if (!scope->started || scope->closed) {
        probe->cleanup_complete = 1;
    }
    return probe->cleanup_complete;
}

static inline int bank_roundtrip_run(void) {
    bank_roundtrip_probe *probe = &bank_roundtrip_resident;
    if (!probe->requested || probe->attempted || stats.issued != BANK_ROUNDTRIP_CAPTURE_TICK ||
        !factory_probe_joined_boundary()) {
        return 0;
    }
    probe->attempted = 1;
    probe->captured_tick = stats.issued;
    probe->owner_thread = GetCurrentThreadId();
    if (!stock_keyboard_identity_pins() || !stock_keyboard_identity_collect(&probe->identity) ||
        !probe->identity.ready || probe->identity.requested_space != 1 ||
        probe->identity.enabled != 1 || !player_zone_pins() ||
        !player_bank_census_pins(NULL)) {
        return bank_roundtrip_fail(probe, "initial_identity_or_pins");
    }
    probe->pins_cached = 1;
    probe->actual_space = probe->identity.requested_space;
    probe->actual_identity_reason = probe->identity.reason;
    if (probe->identity.input_owner > UINTPTR_MAX - 0x10 ||
        !bank_roundtrip_tree_capture(probe->identity.input_owner + 0x10,
                                      &probe->tree, &probe->tree_sample) ||
        !bank_roundtrip_zone(probe, 1) ||
        !bank_roundtrip_read_values(probe, probe->canonical)) {
        return bank_roundtrip_fail(probe, "initial_capture");
    }
    const player_bank_policy policy = {probe, bank_roundtrip_admit, 1, 0};
    if (!player_bank_initialize(&policy)) {
        bank_roundtrip_fail(probe, "initialize");
        bank_roundtrip_finish();
        return 0;
    }
    if (player_bank_resident.slot.bytes[0xc0] != 1 ||
        player_bank_resident.slot.bytes[0x109] != 0 ||
        memcmp(player_bank_resident.slot.bytes, probe->canonical, 0x109) ||
        memcmp(player_bank_resident.slot.bytes + 0x10a, probe->canonical + 0x10a,
               PCB_VALUE_BYTES - 0x10a)) {
        bank_roundtrip_fail(probe, "slot_changed");
        bank_roundtrip_finish();
        return 0;
    }
    if (!player_bank_enter()) {
        bank_roundtrip_fail(probe, "enter");
        bank_roundtrip_finish();
        return 0;
    }
    if (!player_bank_exit()) {
        bank_roundtrip_fail(probe, "exit");
        bank_roundtrip_finish();
        return 0;
    }
    if (!bank_roundtrip_finish()) {
        return 0;
    }
    unsigned char final_values[PCB_VALUE_BYTES];
    if (!bank_roundtrip_read_values(probe, final_values) ||
        memcmp(final_values, probe->canonical, sizeof(final_values)) ||
        !bank_roundtrip_zone(probe, 0) || !stock_keyboard_identity_pins() ||
        !player_zone_pins() || !player_bank_census_pins(NULL) ||
        !bank_roundtrip_admit(probe, PCB_IDLE) ||
        !player_bank_resident.closed || player_bank_resident.bank.zone_pending ||
        player_zone_resident.ledger.phase != PZ_DEAD || player_zone_resident.effect_pending ||
        player_zone_resident.slot_owned || player_zone_resident.canonical_owned ||
        player_zone_resident.slot_reference || player_zone_resident.canonical_reference) {
        return bank_roundtrip_fail(probe, "final_validation");
    }
    if (player_bank_resident.ready || player_bank_resident.bank.phase != PCB_IDLE ||
        player_bank_resident.bank.slot || player_zone_resident.ledger.key != &player_bank_resident.slot) {
        return bank_roundtrip_fail(probe, "ledger_not_closed");
    }
    for (unsigned index = 0; index < PCB_VALUE_BYTES; ++index) {
        if (player_bank_resident.bank.pending[index] != PCB_CLEAR) {
            return bank_roundtrip_fail(probe, "ledger_not_closed");
        }
    }
    probe->canonical_bytes_restored = 1;
    probe->input_tree_unchanged = 1;
    probe->complete = 1;
    return 1;
}

/* Failure remains non-clean even if owned cleanup succeeded: the root must stop
 * the disposable run and report the first failure, never resume simulation. */
static inline int bank_roundtrip_clean(void) {
    return !bank_roundtrip_resident.attempted ||
           (bank_roundtrip_resident.complete && bank_roundtrip_resident.cleanup_complete &&
            !bank_roundtrip_resident.reason);
}
static int lua_player_control_bank_probe_request(lua_State *state) {
    /* Activation is withheld: the existing path join does not exclude the
     * main SDL loop or establish ownership of every context-tree writer.
     * The separate readiness-only barrier experiment must not lift this hold.
     * Engine-free tests exercise bank_roundtrip_request() directly.
     */
    p_lua_pushnumber(state, 0);
    return 1;
}

static int lua_player_control_bank_probe_status(lua_State *state) {
    if (!stock_observer_worker_boundary()) {
        p_lua_pushnil(state);
        return 1;
    }
    const bank_roundtrip_probe *probe = &bank_roundtrip_resident;
    char output[1200];
    int length = snprintf(output, sizeof(output),
        "requested=%u attempted=%u captured_tick=%llu complete=%u clean=%u "
        "canonical_only=1 native_update_calls=0 canonical_bytes_restored=%u "
        "input_tree_unchanged=%u context_count=%u zone_count_before=%u zone_count_after=%u "
        "temporary_requested_space=%s temporary_identity_reason=%s actual_requested_space=%u "
        "actual_identity_reason=%s cleanup_complete=%u reason=%s",
        probe->requested, probe->attempted, (unsigned long long)probe->captured_tick,
        probe->complete, bank_roundtrip_clean(), probe->canonical_bytes_restored,
        probe->input_tree_unchanged, probe->tree_sample.node_count, probe->zone_count,
        probe->zone_count_after, probe->temporary_seen ? "0" : "not_observed",
        probe->temporary_reason ? probe->temporary_reason : "not_observed",
        probe->actual_space, probe->actual_identity_reason ? probe->actual_identity_reason : "not_observed",
        probe->cleanup_complete, probe->reason ? probe->reason : "none");
    if (length < 0 || (size_t)length >= sizeof(output)) {
        p_lua_pushnil(state);
    } else {
        p_lua_pushlstring(state, output, (size_t)length);
    }
    return 1;
}

/* Root wires these three lifecycle helpers at the existing joined boundary. */
static inline int player_control_bank_probe_after_join(void) {
    if (!bank_roundtrip_resident.requested) {
        return 1;
    }
    if (bank_roundtrip_resident.attempted) {
        return bank_roundtrip_resident.complete;
    }
    if (stats.issued < BANK_ROUNDTRIP_CAPTURE_TICK) {
        return 1;
    }
    if (stats.issued != BANK_ROUNDTRIP_CAPTURE_TICK) {
        bank_roundtrip_resident.attempted = 1;
        return bank_roundtrip_fail(&bank_roundtrip_resident, "capture_tick_missed");
    }
    return bank_roundtrip_run();
}
static inline int player_control_bank_probe_restore(void) {
    if (!bank_roundtrip_resident.attempted || bank_roundtrip_resident.complete) {
        return 1;
    }
    return bank_roundtrip_finish();
}
static inline int player_control_bank_probe_clean(void) {
    return bank_roundtrip_clean();
}

/* A failed bank experiment must never return to native simulation. STOP alone
 * only removes hooks; it cannot safely roll back an unknown reference effect.
 * Preserve a bounded raw record before discarding this opt-in test process.
 * _exit deliberately avoids native destructors and writes no game save.
 */
static inline _Noreturn void player_control_bank_probe_fatal(void) {
    const bank_roundtrip_probe *probe = &bank_roundtrip_resident;
    const player_bank_scope *bank = &player_bank_resident;
    const player_zone_scope *zone = &player_zone_resident;
    unsigned pending_bytes = 0;
    for (unsigned index = 0; index < PCB_VALUE_BYTES; ++index) {
        pending_bytes += bank->bank.pending[index] != PCB_CLEAR;
    }
    char diagnostic[1024];
    int length = snprintf(diagnostic, sizeof(diagnostic),
        "X4LAN NATIVE_GATE ERROR PLAYER_CONTROL_BANK_PROBE_FATAL exit=221 captured_tick=%llu "
        "reason=%s complete=%u cleanup_complete=%u bank_phase=%u pending_bytes=%u "
        "zone_phase=%u zone_pending=%u effect_pending=%u slot_owned=%u canonical_owned=%u\n",
        (unsigned long long)probe->captured_tick, probe->reason ? probe->reason : "unknown",
        probe->complete, probe->cleanup_complete, (unsigned)bank->bank.phase, pending_bytes,
        (unsigned)zone->ledger.phase, bank->bank.zone_pending, zone->effect_pending,
        zone->slot_owned, zone->canonical_owned);
    if (length > 0 && (size_t)length < sizeof(diagnostic)) {
        ssize_t written = write(STDERR_FILENO, diagnostic, (size_t)length);
        (void)written;
    }
    _exit(221);
}
#endif
