#ifndef X4_GATE_PLAYER_ZONE_SCOPE_H
#define X4_GATE_PLAYER_ZONE_SCOPE_H

/* Default unused. Include after gate_player_factory_probe.h and
 * gate_player_control_bank_census.h. This admits only a single zone, freshly
 * anchored by the native global at the joined worker boundary. All fields and
 * references must stay resident until successful cleanup or process discard.
 * The caller must serialize this domain and exclude every other native writer
 * outside the allowed active lifecycle, including exclusion of unobserved ABA
 * and changes to other ownership of the anchored zone. Count/readback checks
 * cannot establish those properties. A pending effect must survive STOP;
 * discard the isolated process on uncertainty. Neither a successful reference
 * scope nor these pins establish rollback of other controller effects. */
#include "player_zone_ledger.h"

#define PLAYER_ZONE_GLOBAL_VA 0x3e4cdd8u
#define PLAYER_ZONE_ASSIGN_VA 0xc61960u

typedef struct {
    pz_ledger ledger;
    void *slot_reference;
    void *canonical_reference;
    uintptr_t anchor;
    uint64_t runtime;
    uint64_t saved;
    unsigned slot_owned;
    unsigned canonical_owned;
    unsigned effect_pending;
    pz_location effect_destination;
    pz_location effect_source;
} player_zone_scope;

/* There is no runtime operations override. Tests replace the translation
 * unit's native dependencies at compile time; production uses this storage. */
static player_zone_scope player_zone_resident;

static int player_zone_pins(void) {
    static const lx_site pins[] = {
        {"zone-assign", PLAYER_ZONE_ASSIGN_VA, 92,
         "6e3bb2c9a7edc4c7e0fb6d54f0edab7c61afaabcfe959ab0f7723f0b9869c907"},
        {"zone-acquire", 0xa238b0, 21,
         "0e7c126afb520730e79452b02c5cedd696ecdc918f11b71aab1501dead392f84"},
        {"zone-release", 0xa14540, 37,
         "7368cef95d1efcc8cda06dfd53df92ca026f1da024481b82f3c7a86b8db75587"},
        {"controller-destructor", 0x164c1f0, 1268,
         "0c89cab7cb46b07e7b029854f2d6d49d71ceea7934aee8a0ab5b1ac351080ab2"},
        {"zone-reset", 0x1658910, 644,
         "0af8a1ffaa37ca2f3bb83dcbb159ceb707380d553d69456fc32f25e25314952d"},
        {"zone-environment", 0x1653680, 433,
         "f03bfdda9cdd755791710af81abe9e78597a5e21e299fcc924ab6d337e6de78b"},
    };
    for (unsigned index = 0; index < sizeof(pins) / sizeof(pins[0]); ++index) {
        if (!site_ok(&pins[index])) {
            return 0;
        }
    }
    return player_bank_census_mapped(NULL, PLAYER_ZONE_GLOBAL_VA, sizeof(void *));
}

static uint64_t player_zone_thread(void *context) {
    (void)context;
    return (uint64_t)GetCurrentThreadId();
}

static int player_zone_boundary(void *context) {
    player_zone_scope *scope = context;
    return scope == &player_zone_resident && !scope->effect_pending &&
           factory_probe_joined_boundary() && player_zone_pins();
}

/* Every successful snapshot starts from locations whose ownership is already
 * established. The anchor is an identity check, never an acquired pointer. */
static int player_zone_snapshot(player_zone_scope *scope, uint32_t *count) {
    uintptr_t live = 0;
    uint64_t runtime = 0;
    uint32_t marker = 0;
    if (!player_zone_boundary(scope) || !scope->anchor ||
        scope->anchor > UINTPTR_MAX - 0x48 ||
        !read_local(LX(PLAYER_ZONE_GLOBAL_VA), &live, sizeof(live)) ||
        (live && live != scope->anchor) ||
        ((scope->slot_reference != NULL) != (scope->slot_owned == 1)) ||
        ((scope->canonical_reference != NULL) != (scope->canonical_owned == 1)) ||
        scope->slot_owned > 1 || scope->canonical_owned > 1 ||
        (scope->slot_reference && (uintptr_t)scope->slot_reference != scope->anchor) ||
        (scope->canonical_reference && (uintptr_t)scope->canonical_reference != scope->anchor) ||
        (!live && scope->ledger.phase != PZ_ACTIVE && scope->ledger.phase != PZ_CAPTURED) ||
        (!live && !scope->slot_owned && !scope->canonical_owned) ||
        !read_local((void *)(scope->anchor + 8), &runtime, sizeof(runtime)) ||
        !read_local((void *)(scope->anchor + 0x40), count, sizeof(*count)) ||
        !read_local((void *)(scope->anchor + 0x44), &marker, sizeof(marker)) ||
        !runtime || runtime != scope->runtime || marker != 2 || !*count ||
        world_identity_lookup(runtime) != scope->saved ||
        *count < (unsigned)(live != 0) + scope->slot_owned + scope->canonical_owned) {
        return 0;
    }
    return 1;
}

static void **player_zone_location(player_zone_scope *scope, pz_location location) {
    switch (location) {
    case PZ_LIVE: return (void **)LX(PLAYER_ZONE_GLOBAL_VA);
    case PZ_SLOT: return &scope->slot_reference;
    case PZ_CANONICAL: return &scope->canonical_reference;
    default: return NULL;
    }
}

static int player_zone_inspect(void *context, pz_location location) {
    player_zone_scope *scope = context;
    uint32_t count = 0;
    return player_zone_location(scope, location) && player_zone_snapshot(scope, &count);
}

static pz_effect player_zone_assign(void *context, pz_location destination,
                                    pz_location source) {
    player_zone_scope *scope = context;
    void **destination_slot = player_zone_location(scope, destination);
    void **source_slot = player_zone_location(scope, source);
    void *empty = NULL;
    void *old_value = NULL;
    void *new_value = NULL;
    uint32_t before = 0;
    if (!destination_slot || destination == source ||
        (source != PZ_EMPTY && !source_slot) || !player_zone_snapshot(scope, &before) ||
        !read_local(destination_slot, &old_value, sizeof(old_value)) ||
        (source != PZ_EMPTY && !read_local(source_slot, &new_value, sizeof(new_value)))) {
        return PZ_NO_EFFECT;
    }
    if ((old_value && (uintptr_t)old_value != scope->anchor) ||
        (new_value && (uintptr_t)new_value != scope->anchor) ||
        (old_value && before <= 1) || (new_value && before >= INT32_MAX)) {
        return PZ_NO_EFFECT;
    }
    /* Both distinct locations already own their matching references. */
    if (old_value == new_value) {
        uint32_t checked_count = 0;
        void *checked_old = NULL;
        void *checked_source = NULL;
        if (!player_zone_snapshot(scope, &checked_count) || checked_count != before ||
            !read_local(destination_slot, &checked_old, sizeof(checked_old)) ||
            checked_old != old_value ||
            (source != PZ_EMPTY && (!read_local(source_slot, &checked_source, sizeof(checked_source)) ||
                                    checked_source != new_value)) ||
            !player_zone_boundary(scope)) {
            return PZ_NO_EFFECT;
        }
        return PZ_DONE;
    }
    /* Revalidate immediately before the native call, including the exact
     * pre-call slots and count; exclusive caller admission remains required. */
    void *before_live = NULL;
    void *before_slot = scope->slot_reference;
    void *before_canonical = scope->canonical_reference;
    uint32_t checked_count = 0;
    void *checked_old = NULL;
    void *checked_source = NULL;
    if (!player_zone_snapshot(scope, &checked_count) || checked_count != before ||
        !read_local(LX(PLAYER_ZONE_GLOBAL_VA), &before_live, sizeof(before_live)) ||
        !read_local(destination_slot, &checked_old, sizeof(checked_old)) ||
        checked_old != old_value ||
        (source != PZ_EMPTY && (!read_local(source_slot, &checked_source, sizeof(checked_source)) ||
                                checked_source != new_value)) ||
        !player_zone_boundary(scope)) {
        return PZ_NO_EFFECT;
    }
    scope->effect_pending = 1;
    scope->effect_destination = destination;
    scope->effect_source = source;
    void **returned = ((void **(*)(void **, void *const *))LX(PLAYER_ZONE_ASSIGN_VA))(
        destination_slot, source == PZ_EMPTY ? &empty : source_slot);
    /* Do not use snapshot here: pending effects intentionally reject callbacks. */
    void *after_value = NULL;
    void *after_source = NULL;
    void *after_live = NULL;
    void *after_slot = NULL;
    void *after_canonical = NULL;
    uint32_t after = 0;
    uint32_t marker = 0;
    uint64_t runtime = 0;
    if (returned != destination_slot || !factory_probe_joined_boundary() || !player_zone_pins() ||
        !read_local(destination_slot, &after_value, sizeof(after_value)) || after_value != new_value ||
        !read_local(LX(PLAYER_ZONE_GLOBAL_VA), &after_live, sizeof(after_live)) ||
        !read_local(&scope->slot_reference, &after_slot, sizeof(after_slot)) ||
        !read_local(&scope->canonical_reference, &after_canonical, sizeof(after_canonical)) ||
        (destination != PZ_LIVE && after_live != before_live) ||
        (destination != PZ_SLOT && after_slot != before_slot) ||
        (destination != PZ_CANONICAL && after_canonical != before_canonical) ||
        (source != PZ_EMPTY && (!read_local(source_slot, &after_source, sizeof(after_source)) ||
                                after_source != new_value)) ||
        !read_local((void *)(scope->anchor + 8), &runtime, sizeof(runtime)) ||
        !read_local((void *)(scope->anchor + 0x40), &after, sizeof(after)) ||
        !read_local((void *)(scope->anchor + 0x44), &marker, sizeof(marker)) ||
        runtime != scope->runtime || world_identity_lookup(runtime) != scope->saved || marker != 2 ||
        after != before + (new_value != NULL) - (old_value != NULL)) {
        return PZ_UNKNOWN;
    }
    if (destination == PZ_SLOT) {
        scope->slot_owned = new_value != NULL;
    } else if (destination == PZ_CANONICAL) {
        scope->canonical_owned = new_value != NULL;
    }
    scope->effect_pending = 0;
    return PZ_DONE;
}

/* Call once with a fresh slot at the joined boundary. No raw pointer input. */
static inline int player_zone_initialize(const pcb_slot *slot) {
    player_zone_scope *scope = &player_zone_resident;
    uintptr_t anchor = 0;
    uint64_t runtime = 0;
    uint32_t count = 0;
    uint32_t marker = 0;
    if (scope->ledger.initialized || !slot || !factory_probe_joined_boundary() ||
        !player_zone_pins() || !read_local(LX(PLAYER_ZONE_GLOBAL_VA), &anchor, sizeof(anchor)) ||
        !anchor || anchor > UINTPTR_MAX - 0x48 ||
        !read_local((void *)(anchor + 8), &runtime, sizeof(runtime)) || !runtime ||
        !read_local((void *)(anchor + 0x40), &count, sizeof(count)) || !count ||
        !read_local((void *)(anchor + 0x44), &marker, sizeof(marker)) || marker != 2) {
        return 0;
    }
    uint64_t saved = world_identity_lookup(runtime);
    if (!saved) {
        return 0;
    }
    scope->anchor = anchor;
    scope->runtime = runtime;
    scope->saved = saved;
    pz_operations operations = {scope, player_zone_thread, player_zone_boundary,
                                 player_zone_inspect, player_zone_assign};
    return pz_initialize(&scope->ledger, slot, &operations, player_zone_thread(scope)) &&
           pz_initialize_slot(&scope->ledger);
}
#endif
