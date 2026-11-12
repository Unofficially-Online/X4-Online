#ifndef X4_GATE_PLAYER_CONTROL_BANK_CENSUS_H
#define X4_GATE_PLAYER_CONTROL_BANK_CENSUS_H

/* Default-off read-only baseline. Include after gate_stock_keyboard_identity.h.
 * Root wiring calls after_join only inside the EXISTING serialized admitted
 * post-path-join worker interval. All relevant writers/lifetimes must be excluded
 * throughout capture; repeated reads detect change, not concurrent access safety.
 * The gate must already have accepted the exact ELF file. No setters, patching,
 * bank exchanges, factory/seating calls or reference operations exist here.
 *
 * Five full raw value regions are retained for a later LOCAL exact roundtrip.
 * The diagnostic FNV digest covers explicitly listed numeric fields only:
 * opaque state, known pointer fields and local identities are excluded. It is
 * neither a complete bank hash nor a relay acceptance digest. Classification
 * remains the confidence-qualified ledger in player_control_banks.h; successful
 * reads/mappings do not prove SIM/DEVICE/LOCAL ownership or dependency closure.
 * The zone pointer is an observation ONLY, never a retained reference/handle.
 * Cached pointers must never be used as future read destinations.
 */
#include "player_control_banks.h"

typedef struct {
    void *context;
    int (*boundary)(void *context);
    int (*pins)(void *context);
    int (*mapped)(void *context, unsigned va, unsigned length);
    int (*read)(void *context, unsigned va, void *bytes, unsigned length);
    int (*identity)(void *context, stock_keyboard_identity_sample *sample);
    int (*zone_identity)(void *context, uintptr_t zone, uint64_t *runtime);
    uint64_t (*saved)(void *context, uint64_t runtime);
} pcb_census_operations;

typedef struct {
    unsigned offset;
    unsigned length;
} pcb_census_numeric_range;

/* Independently typed accesses, not every byte of a descriptor. In particular
 * main0x54..58, main0x8c..b0, controller opaque state, coefficient tables and
 * padding are absent. Numeric timers are included; their values may differ
 * legitimately across clients. This diagnostic imposes no equality criterion. */
static const pcb_census_numeric_range pcb_census_numeric_ranges[] = {
    {0x000, 0x054}, {0x058, 0x034}, {0x0b0, 0x002}, {0x0c0, 0x001}, {0x0c4, 0x01b},
    {0x0e0, 0x02b}, {0x10c, 0x004}, {0x278, 0x010}, {0x290, 0x010}, {0x2a4, 0x018},
    {0x2c4, 0x008}, {0x3c0, 0x001}, {0x3c4, 0x008}, {0x3cc, 0x003}, {0x3d0, 0x010},
};

typedef struct {
    unsigned complete;
    unsigned stable;
    unsigned context_ready;
    unsigned mapped_regions;
    unsigned digest_bytes;
    const char *reason;
    uint64_t numeric_digest;
    stock_keyboard_identity_sample identity;
    unsigned char values[PCB_VALUE_BYTES];
    uintptr_t zone_observed;
    uint64_t zone_runtime;
    uint64_t zone_saved;
    unsigned zone_identity_known;
    unsigned char reentry;
    unsigned char input_locks[3];
    uint32_t buffer_index;
} pcb_census_sample;

typedef struct {
    uintptr_t zone;
    uint64_t zone_runtime;
    unsigned char reentry;
    unsigned char input_locks[3];
    uint32_t buffer_index;
} pcb_census_shared;

static int pcb_census_read_shared(const pcb_census_operations *operations,
                                  pcb_census_shared *shared) {
    memset(shared, 0, sizeof(*shared));
    if (!operations->mapped(operations->context, 0x3e4cdd8, 8) ||
        !operations->mapped(operations->context, 0x3ee9b40, 1) ||
        !operations->mapped(operations->context, 0x3e5b298, 3) ||
        !operations->mapped(operations->context, 0x3d8e254, 4) ||
        !operations->read(operations->context, 0x3e4cdd8, &shared->zone, 8) ||
        !operations->read(operations->context, 0x3ee9b40, &shared->reentry, 1) ||
        !operations->read(operations->context, 0x3e5b298, shared->input_locks, 3) ||
        !operations->read(operations->context, 0x3d8e254, &shared->buffer_index, 4)) {
        return 0;
    }
    if (shared->zone &&
        !operations->zone_identity(operations->context, shared->zone, &shared->zone_runtime)) {
        return 0;
    }
    return 1;
}

/* identity() clears struct padding and supplies fresh native identity getters.
 * Its full normalized result is compared, excluding only the diagnostic reason
 * pointer. A changed context is reported, never silently labelled stable. */
static int pcb_census_collect(pcb_census_sample *sample, const pcb_census_operations *operations) {
    if (!sample) {
        return 0;
    }
    memset(sample, 0, sizeof(*sample));
    sample->reason = "operations";
    if (!operations || !operations->boundary || !operations->pins || !operations->mapped ||
        !operations->read || !operations->identity || !operations->zone_identity ||
        !operations->saved) {
        return 0;
    }
    sample->reason = "boundary";
    if (!operations->boundary(operations->context)) {
        return 0;
    }
    sample->reason = "pins";
    if (!operations->pins(operations->context)) {
        return 0;
    }
    sample->reason = "identity";
    if (!operations->identity(operations->context, &sample->identity)) {
        return 0;
    }
    pcb_census_shared first;
    pcb_census_shared last;
    sample->reason = "shared_read";
    if (!pcb_census_read_shared(operations, &first)) {
        return 0;
    }
    sample->reason = "update_in_flight";
    if (first.reentry) {
        return 0;
    }
    for (unsigned region_index = 0; region_index < PCB_REGION_COUNT; ++region_index) {
        const pcb_region *region = &pcb_regions[region_index];
        sample->reason = "mapping";
        if (!operations->mapped(operations->context, region->va, region->length)) {
            return 0;
        }
        ++sample->mapped_regions;
        sample->reason = "value_read";
        if (!operations->read(operations->context, region->va, sample->values + region->offset,
                              region->length)) {
            return 0;
        }
    }
    unsigned char checked[PCB_VALUE_BYTES];
    for (unsigned region_index = 0; region_index < PCB_REGION_COUNT; ++region_index) {
        const pcb_region *region = &pcb_regions[region_index];
        sample->reason = "mapping_readback";
        if (!operations->mapped(operations->context, region->va, region->length)) {
            return 0;
        }
        sample->reason = "value_readback";
        if (!operations->read(operations->context, region->va, checked + region->offset,
                              region->length)) {
            return 0;
        }
    }
    sample->reason = "value_changed";
    if (memcmp(sample->values, checked, sizeof(checked))) {
        return 0;
    }
    stock_keyboard_identity_sample identity_after = {0};
    sample->reason = "identity_readback";
    if (!operations->identity(operations->context, &identity_after)) {
        return 0;
    }
    stock_keyboard_identity_sample identity_before;
    memcpy(&identity_before, &sample->identity, sizeof(identity_before));
    identity_before.reason = NULL;
    identity_after.reason = NULL;
    sample->reason = "identity_changed";
    if (memcmp(&identity_before, &identity_after, sizeof(identity_before))) {
        return 0;
    }
    sample->reason = "shared_readback";
    if (!pcb_census_read_shared(operations, &last)) {
        return 0;
    }
    sample->reason = "shared_changed";
    if (memcmp(&first, &last, sizeof(first))) {
        return 0;
    }
    sample->reason = "boundary_changed";
    if (!operations->boundary(operations->context)) {
        return 0;
    }
    sample->zone_observed = first.zone;
    sample->zone_runtime = first.zone_runtime;
    sample->zone_saved =
        first.zone_runtime ? operations->saved(operations->context, first.zone_runtime) : 0;
    sample->zone_identity_known = sample->zone_saved != 0;
    sample->reentry = first.reentry;
    memcpy(sample->input_locks, first.input_locks, sizeof(sample->input_locks));
    sample->buffer_index = first.buffer_index;
    sample->context_ready = sample->identity.ready && !first.input_locks[0] &&
                            !first.input_locks[1] && !first.input_locks[2];
    uint64_t digest = UINT64_C(0xcbf29ce484222325);
    for (unsigned range_index = 0;
         range_index < sizeof(pcb_census_numeric_ranges) / sizeof(pcb_census_numeric_ranges[0]);
         ++range_index) {
        const pcb_census_numeric_range *range = &pcb_census_numeric_ranges[range_index];
        for (unsigned byte_index = 0; byte_index < range->length; ++byte_index) {
            digest ^= sample->values[range->offset + byte_index];
            digest *= UINT64_C(0x100000001b3);
            ++sample->digest_bytes;
        }
    }
    sample->numeric_digest = digest;
    sample->stable = 1;
    sample->complete = 1;
    sample->reason = "observed_only";
    return 1;
}

/* Native operations: no runtime override, engine writes or factory pins. Exact
 * hashes refuse overlap rather than accepting unknown modified helper code.
 * The portable preload's119d112 factory patch is outside every pin below.
 * stock identity collection also checks its existing getter/update/reset pins. */
static int player_bank_census_pins(void *context) {
    (void)context;
    static const lx_site pins[] = {
        {"writable-ELF-segment", 0x400158, 56,
         "4f09b0d10a52f5a7846749b2d2766f9890d89b5b6a8758fb042098cb63821871"},
        {"controller-upkeep", 0x1672370, 7095,
         "8d672ea7553c9bb4939fb173d8c86494f8eeb92068aa6dd14984e357d9d0c8d9"},
        {"controller-coefficients", 0x16666b0, 2773,
         "844509812707345ef5b8de3dfb05becfe3eedec23c188b70001546f0db7ae538"},
        {"coefficient-refresh", 0x165ae60, 527,
         "f5fc8fbe7392dbc6eb5b284028e8a9472cc5521e865ce498dc24ae602afceb8a"},
        {"coefficient-output", 0x1e04550, 163,
         "f13bb84d61735f8f4bb45356b0e164837de3ecb72766319b0c58fd24f26cf270"},
        {"controller-environment", 0x1653680, 433,
         "f03bfdda9cdd755791710af81abe9e78597a5e21e299fcc924ab6d337e6de78b"},
    };
    for (unsigned pin_index = 0; pin_index < sizeof(pins) / sizeof(pins[0]); ++pin_index) {
        if (!site_ok(&pins[pin_index])) {
            return 0;
        }
    }
    return 1;
}

/* All page boundaries and final byte, not just region starts. The pinned
 * writable PT_LOAD is35fe1f8..7bb6bb8; anonymous BSS starts at3da9000.
 * Permissions+placement are evidence, not allocator ownership for future writes. */
static int player_bank_census_mapped(void *context, unsigned va, unsigned length) {
    (void)context;
    if (!length || va < 0x35fe1f8u || va >= 0x7bb6bb8u || length > 0x7bb6bb8u - va) {
        return 0;
    }
    unsigned last = va + length - 1;
    unsigned current = va;
    char executable[PATH_MAX];
    if (!gate_exe_realpath(executable)) {
        return 0;
    }
    for (;;) {
        char permissions[5];
        char path[PATH_MAX];
        if (!gate_maps_find((uintptr_t)LX(current), permissions, path, sizeof(path)) ||
            strcmp(permissions, "rw-p") ||
            (current < 0x3da9000u ? strcmp(path, executable) : path[0] != 0)) {
            return 0;
        }
        if (current == last) {
            return 1;
        }
        unsigned next = (current & ~4095u) + 4096u;
        current = next > last ? last : next;
    }
}

static int player_bank_census_read(void *context, unsigned va, void *bytes, unsigned length) {
    (void)context;
    return read_local(LX(va), bytes, length);
}

static int player_bank_census_identity(void *context, stock_keyboard_identity_sample *sample) {
    (void)context;
    return stock_keyboard_identity_collect(sample);
}

static int player_bank_census_zone_identity(void *context, uintptr_t zone, uint64_t *runtime) {
    (void)context;
    return zone && zone <= UINTPTR_MAX - 16 &&
           read_local((const void *)(zone + 8), runtime, sizeof(*runtime));
}

static uint64_t player_bank_census_saved(void *context, uint64_t runtime) {
    (void)context;
    return world_identity_lookup(runtime);
}

static unsigned player_bank_census_requested;
static unsigned player_bank_census_attempted;
static uint64_t player_bank_census_tick;
static pcb_census_sample player_bank_census_snapshot;

static int player_bank_census_boundary(void *context) {
    (void)context;
    unsigned native_flags = 2;
    unsigned char loading = 1;
    sko_status observer = sko_get_status();
    return player_bank_census_requested && stock_observer_post_join_boundary && stats.active &&
           stats.issued >= 2 && stats.issued == player_bank_census_tick && boot_phase == 3 &&
           path_installed && stock_observer_worker_thread &&
           GetCurrentThreadId() == stock_observer_worker_thread &&
           GetCurrentThreadId() != main_thread && ui_thread == stock_observer_worker_thread &&
           gate_tls_worker(gate_tls_self()) && atomic_load(&stock_observer_requested) &&
           stock_observer_install_tick && observer.active &&
           observer.owned == (1u << SKO_SELECTED) - 1 && !observer.uncertain &&
           !observer.pending_readback && !observer.wrapper_depth && !observer.overflow &&
           !atomic_load(&stock_observer_input_depth) &&
           !atomic_load(&stock_observer_thread_failure) && !stock_observer_failed &&
           stock_observer_slot_valid() &&
           read_local(LX(STOCK_OBSERVER_NATIVE_FLAGS), &native_flags, sizeof(native_flags)) &&
           !(native_flags & 2) && read_local(LX(0x3daab00), &loading, 1) && !loading;
}

/* One capture per explicit request, afterjoin only. Request/status themselves
 * run on the accepted observer worker outside InputUpdate. No hooks to restore. */
static int player_control_bank_census_after_join(void) {
    if (!player_bank_census_requested) {
        return 1;
    }
    if (player_bank_census_attempted) {
        return player_bank_census_snapshot.complete;
    }
    if (stats.issued < 2) {
        return 1;
    }
    player_bank_census_attempted = 1;
    player_bank_census_tick = stats.issued;
    const pcb_census_operations operations = {
        NULL,
        player_bank_census_boundary,
        player_bank_census_pins,
        player_bank_census_mapped,
        player_bank_census_read,
        player_bank_census_identity,
        player_bank_census_zone_identity,
        player_bank_census_saved,
    };
    return pcb_census_collect(&player_bank_census_snapshot, &operations);
}

static int lua_player_control_bank_census_request(lua_State *state) {
    int accepted = !player_bank_census_requested && !player_bank_census_attempted &&
                   stock_observer_worker_boundary() && atomic_load(&sko_session.active);
    if (accepted) {
        player_bank_census_requested = 1;
    }
    p_lua_pushnumber(state, accepted);
    return 1;
}

static int lua_player_control_bank_census(lua_State *state) {
    if (!stock_observer_worker_boundary()) {
        p_lua_pushnil(state);
        return 1;
    }
    const pcb_census_sample *sample = &player_bank_census_snapshot;
    char output[1200];
    int length = snprintf(
        output, sizeof(output),
        "requested=%u attempted=%u captured_tick=%llu complete=%u stable=%u context_ready=%u "
        "reason=%s context_reason=%s regions=%u bytes=%u classification=static_ledger "
        "ownership=unproven digest_kind=fnv_numeric_subset digest_bytes=%u "
        "digest=%016llx excluded_bytes=%u player=%llx controller=%llx ship=%llx input_owner=%llx "
        "player_saved=%016llx ship_saved=%016llx zone_nonnull=%u zone_identity_known=%u "
        "zone_runtime=%llu zone_saved=%016llx zone_owned=0 reentry=%u locks=%u,%u,%u "
        "buffer_index=%u",
        player_bank_census_requested, player_bank_census_attempted,
        (unsigned long long)player_bank_census_tick, sample->complete, sample->stable,
        sample->context_ready, sample->reason ? sample->reason : "off",
        sample->identity.reason ? sample->identity.reason : "off", sample->mapped_regions,
        PCB_VALUE_BYTES, sample->digest_bytes, (unsigned long long)sample->numeric_digest,
        PCB_VALUE_BYTES - sample->digest_bytes, (unsigned long long)sample->identity.player,
        (unsigned long long)sample->identity.controller, (unsigned long long)sample->identity.ship,
        (unsigned long long)sample->identity.input_owner,
        (unsigned long long)sample->identity.player_saved,
        (unsigned long long)sample->identity.saved, sample->zone_observed != 0,
        sample->zone_identity_known, (unsigned long long)sample->zone_runtime,
        (unsigned long long)sample->zone_saved, sample->reentry, sample->input_locks[0],
        sample->input_locks[1], sample->input_locks[2], sample->buffer_index);
    if (length < 0 || (size_t)length >= sizeof(output)) {
        p_lua_pushnil(state);
    } else {
        p_lua_pushlstring(state, output, (size_t)length);
    }
    return 1;
}
#endif
