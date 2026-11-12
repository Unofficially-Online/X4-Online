#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct { const char *name; unsigned va, length; const char *hash; } lx_site;
typedef struct {
    unsigned char prefix[8];
    uint64_t runtime;
    unsigned char padding[0x30];
    uint32_t count;
    uint32_t marker;
} fake_zone;
static fake_zone zone;
static void *live;
static int boundary_ok, pins_ok, mapping_ok, read_failure, fail_readback, wrong_count, calls;
static uint64_t saved = 73;
static unsigned GetCurrentThreadId(void) { return 5; }
static int factory_probe_joined_boundary(void) { return boundary_ok; }
static int site_ok(const lx_site *site) { return pins_ok && site->hash[0] && site->length; }
static int player_bank_census_mapped(void *context, unsigned address, unsigned length) {
    (void)context; (void)address; (void)length; return mapping_ok;
}
static uint64_t world_identity_lookup(uint64_t runtime) {
    return runtime == zone.runtime ? saved : 0;
}
static int read_local(const void *source, void *destination, unsigned length) {
    if (read_failure) { read_failure = 0; return 0; }
    memcpy(destination, source, length);
    return 1;
}
static void **native_assign(void **destination, void *const *source) {
    ++calls;
    fake_zone *incoming = *source;
    fake_zone *old = *destination;
    if (incoming) { ++incoming->count; }
    *destination = incoming;
    if (old) { --old->count; }
    if (wrong_count) { ++zone.count; }
    if (fail_readback) { read_failure = 1; }
    return destination;
}
static void *fake_address(unsigned address) {
    if (address == 0x3e4cdd8u) { return &live; }
    if (address == 0xc61960u) { return (void *)(uintptr_t)native_assign; }
    return NULL;
}
#define LX(address) fake_address(address)
#include "gate_player_zone_scope.h"

static void setup(pcb_slot *slot) {
    memset(&zone, 0, sizeof(zone));
    memset(&player_zone_resident, 0, sizeof(player_zone_resident));
    zone.runtime = 42;
    zone.count = 1;
    zone.marker = 2;
    live = &zone;
    boundary_ok = pins_ok = mapping_ok = 1;
    read_failure = fail_readback = wrong_count = calls = 0;
    saved = 73;
    assert(player_zone_initialize(slot));
    assert(zone.count == 2 && calls == 1);
}
static void normal_scope(void) {
    pcb_slot slot = {0};
    setup(&slot);
    player_zone_scope *scope = &player_zone_resident;
    assert(pz_enter(&scope->ledger, &slot) == PCB_ZONE_ACTIVE);
    assert(zone.count == 3 && calls == 2);
    /* Native controller destruction clears the separately owned global. */
    live = NULL;
    --zone.count;
    assert(pz_restore(&scope->ledger, &slot));
    assert(live == &zone && scope->slot_reference == NULL && zone.count == 1);
    assert(pz_destroy(&scope->ledger));
}
static void reject_foreign_and_checks(void) {
    pcb_slot slot = {0};
    setup(&slot);
    player_zone_scope *scope = &player_zone_resident;
    assert(player_zone_assign(scope, PZ_SLOT, (pz_location)99) == PZ_NO_EFFECT);
    void *foreign = (void *)(uintptr_t)0x1234;
    live = foreign;
    assert(pz_enter(&scope->ledger, &slot) == PCB_ZONE_INACTIVE && calls == 1);
    live = &zone;
    saved = 0;
    assert(pz_enter(&scope->ledger, &slot) == PCB_ZONE_INACTIVE && calls == 1);
    saved = 73;
    pins_ok = 0;
    assert(pz_enter(&scope->ledger, &slot) == PCB_ZONE_INACTIVE && calls == 1);
    pins_ok = 1;
    scope->slot_owned = 0;
    assert(pz_enter(&scope->ledger, &slot) == PCB_ZONE_INACTIVE && calls == 1);
    scope->slot_owned = 1;
    zone.count = 1;
    assert(pz_destroy(&scope->ledger) == 0 && calls == 1);
    zone.count = 2;
    assert(pz_destroy(&scope->ledger));
}
static void reject_missing_owned_locations(void) {
    pcb_slot slot = {0};
    setup(&slot);
    player_zone_scope *scope = &player_zone_resident;
    /* Inflate count so that the aggregate lower bound cannot mask the bug. */
    zone.count = 20;
    scope->slot_reference = NULL;
    assert(scope->slot_owned == 1);
    assert(pz_enter(&scope->ledger, &slot) == PCB_ZONE_INACTIVE);
    assert(calls == 1);

    setup(&slot);
    scope = &player_zone_resident;
    assert(pz_enter(&scope->ledger, &slot) == PCB_ZONE_ACTIVE);
    assert(scope->canonical_owned == 1);
    zone.count = 20;
    scope->canonical_reference = NULL;
    int previous_calls = calls;
    assert(!pz_restore(&scope->ledger, &slot));
    assert(scope->ledger.phase == PZ_ACTIVE && calls == previous_calls);
    assert(!pz_destroy(&scope->ledger) && calls == previous_calls);
}
static void unknown_effect(void) {
    pcb_slot slot = {0};
    setup(&slot);
    player_zone_scope *scope = &player_zone_resident;
    wrong_count = 1;
    assert(pz_enter(&scope->ledger, &slot) == PCB_ZONE_UNCERTAIN);
    assert(scope->effect_pending && scope->ledger.phase == PZ_POISONED);
    int previous_calls = calls;
    assert(!pz_restore(&scope->ledger, &slot));
    assert(!pz_destroy(&scope->ledger) && calls == previous_calls);
    setup(&slot);
    scope = &player_zone_resident;
    fail_readback = 1;
    assert(pz_enter(&scope->ledger, &slot) == PCB_ZONE_UNCERTAIN);
    assert(scope->effect_pending && scope->ledger.phase == PZ_POISONED);
    previous_calls = calls;
    assert(!pz_restore(&scope->ledger, &slot));
    assert(calls == previous_calls);
}
int main(void) {
    pcb_slot initial_slot = {0};
    memset(&player_zone_resident, 0, sizeof(player_zone_resident));
    memset(&zone, 0, sizeof(zone));
    boundary_ok = pins_ok = mapping_ok = 1;
    live = &zone;
    zone.runtime = 42;
    zone.count = 1;
    zone.marker = 1;
    assert(!player_zone_initialize(&initial_slot));
    zone.marker = 2;
    saved = 0;
    assert(!player_zone_initialize(&initial_slot));
    normal_scope();
    reject_foreign_and_checks();
    reject_missing_owned_locations();
    unknown_effect();
    puts("4 adapter scenario groups passed");
}
