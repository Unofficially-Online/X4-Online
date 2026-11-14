#define _GNU_SOURCE
#define X4_PLAYER_VIEW_ANCESTRY_PRELOAD_PIN_H
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include "player_view_ancestry_preload.h"
#define PLAYER_TARGET_HOLD_ACQUIRE_VA 0x101u
#define PLAYER_TARGET_HOLD_RELEASE_VA 0x102u
#define PLAYER_TARGET_HOLD_REFERENCE_OFFSET 0x38u
#define PLAYER_TARGET_HOLD_COUNT_OFFSET 0x40u
#define PLAYER_TARGET_HOLD_MARKER_OFFSET 0x44u
#define PLAYER_TARGET_HOLD_SHIP_REFERENCE_VTABLE 0x500u
#define SEAT_PREFLIGHT_SHIP_VTABLE 0x600u
#define FWRR_FIRST_GENERATION 5u
#define SEAT_PREFLIGHT_TARGET_SAVED 175310u
static unsigned char player[0x80], room[0x80], ship[0x80], p2[0x80];
static struct { unsigned slot; uint64_t issued; } stats;
static struct { uintptr_t ship; uint64_t runtime, saved; } factory_probe_identity;
static uintptr_t factory_probe_player1, lifecycle_discard_ship;
static struct { int target_owned, uncertain; struct { uintptr_t target; uint64_t target_runtime; } identity; } player_target_hold_resident;
static int player_recurring_physics = 3, player_recurring_tail = 1, player_routes_scope;
static struct { uint64_t generation; } player_recurring_tail_token;
static _Atomic unsigned player_physics_ready = 1;
static int fake_identity = 1, fake_provider = 1, fake_bad_reference, fake_bad_receipt, acquire_calls, release_calls, binds, ticks, closes;
static void fake_acquire(void *object) { uint32_t *count = (uint32_t *)((char *)object + 0x40); if (fake_bad_reference != 1) ++*count; ++acquire_calls; }
static void fake_release(void *reference) { uint32_t *count = (uint32_t *)((char *)reference + 8); if (fake_bad_reference != 2) --*count; ++release_calls; }
static void *fake_lx(uintptr_t value) {
    if (value == PLAYER_TARGET_HOLD_ACQUIRE_VA) return (void *)fake_acquire;
    if (value == PLAYER_TARGET_HOLD_RELEASE_VA) return (void *)fake_release;
    return (void *)value;
}
#define LX(value) fake_lx(value)
static int player_recurring_physics_entered(void) { return 1; }
static void view_component_trace_gate_begin(uint64_t generation) { (void)generation; }
static void view_component_trace_gate_finish(uint64_t generation) { (void)generation; }
static int player_physics_identity(void) { return fake_identity; }
static int factory_probe_singleton(uintptr_t value) { return value == (uintptr_t)player; }
static int gate_tls_worker(void *value) { return value != NULL; }
static void *gate_tls_self(void) { return player; }
static int player_target_hold_pins(void) { return 1; }
static int player_target_hold_read(uintptr_t address, unsigned offset, void *out, size_t size) {
    memcpy(out, (void *)(address + offset), size); return 1;
}
typedef struct { int ignored; } player_view_query_api;
typedef struct { int (*bind)(uint32_t, uintptr_t, uintptr_t); int (*tick)(uint64_t); int (*finish)(view_ancestry_snapshot *); view_ancestry_info information; } player_view_ancestry_api;
static int view_query_expected_library(const char **sha, uint64_t *size) { *sha = "test"; *size = 1; return 1; }
static int player_view_query_provider(const char *sha, uint64_t size, player_view_query_api *out) {
    (void)sha; (void)size; (void)out; return fake_provider;
}
static int fake_bind(uint32_t tid, uintptr_t first, uintptr_t second) {
    assert(tid && first == (uintptr_t)ship && second == (uintptr_t)p2); ++binds; return 1;
}
static int fake_tick(uint64_t generation) { assert(generation >= 5); ++ticks; return 1; }
static int fake_finish(view_ancestry_snapshot *out) {
    memset(out, 0, sizeof *out); out->version = 1; out->installed = out->closed = 1;
    out->first_ship = (uintptr_t)ship; out->second_ship = (uintptr_t)p2;
    view_ancestry_site *entry = &out->sites[0];
    entry->started = entry->returned = entry->examined = entry->count = 1;
    entry->category_count[1] = 1;
    entry->samples[0].generation = 5; entry->samples[0].tid = (uint32_t)syscall(SYS_gettid);
    entry->samples[0].self = (uintptr_t)ship; entry->samples[0].count = 1;
    entry->samples[0].nodes[0].component = (uintptr_t)ship;
    entry->samples[0].category = fake_bad_receipt ? 2 : 1;
    ++closes; return 1;
}
static int player_view_ancestry_provider(const player_view_query_api *provider, player_view_ancestry_api *out) {
    (void)provider; *out = (player_view_ancestry_api){.bind = fake_bind, .tick = fake_tick, .finish = fake_finish}; return 1;
}
#include "gate_player_view_ancestry.h"
static void put(uint8_t *base, unsigned offset, uint64_t value, size_t length) { memcpy(base + offset, &value, length); }
static void setup(unsigned slot) {
    stats.slot = slot; stats.issued = 5; player_recurring_tail_token.generation = 5;
    factory_probe_player1 = (uintptr_t)player; factory_probe_identity.ship = (uintptr_t)ship;
    factory_probe_identity.runtime = 123; factory_probe_identity.saved = 100; lifecycle_discard_ship = (uintptr_t)p2;
    player_target_hold_resident.target_owned = 1; player_target_hold_resident.identity.target = (uintptr_t)p2;
    player_target_hold_resident.identity.target_runtime = 456;
    put(player, 0x70, (uintptr_t)room, 8); put(room, 0x70, (uintptr_t)ship, 8);
    put(ship, 0, SEAT_PREFLIGHT_SHIP_VTABLE, 8); put(ship, 8, 123, 8);
    put(ship, 0x38, PLAYER_TARGET_HOLD_SHIP_REFERENCE_VTABLE, 8);
    put(ship, 0x40, 7, 4); put(ship, 0x44, 2, 4);
}
static void scenario(int which) {
    setup(which == 2 ? 2 : 1);
    if (which == 0) {
        unsetenv("X4_DEV_VIEW_ANCESTRY"); player_view_ancestry_before(5); player_view_ancestry_after(5);
        player_view_ancestry_close(5); assert(!acquire_calls && !binds); return;
    }
    setenv("X4_DEV_VIEW_ANCESTRY", "1", 1);
    player_view_ancestry_before(5);
    if (which == 3) fake_provider = 0;
    if (which == 5) fake_bad_reference = 1;
    player_view_ancestry_after(5);
    assert(acquire_calls == 1 && binds == 1 && ticks == 1 && *(uint32_t *)(ship + 0x40) == 8);
    stats.issued = 6; player_recurring_tail_token.generation = which == 7 ? 5 : 6;
    if (which == 4) fake_identity = 0;
    player_view_ancestry_before(6); player_view_ancestry_after(6);
    if (which == 6) fake_bad_reference = 2;
    if (which == 8) fake_bad_receipt = 1;
    player_view_ancestry_close(6);
    assert(closes == 1 && release_calls == 1 && *(uint32_t *)(ship + 0x40) == 7 && ticks == 2);
}
int main(void) {
    for (int which = 0; which <= 8; ++which) {
        pid_t child = fork(); assert(child >= 0);
        if (!child) { scenario(which); _exit(0); }
        int status = 0; assert(waitpid(child, &status, 0) == child && WIFEXITED(status));
        assert(WEXITSTATUS(status) == (which >= 3 ? 223 : 0));
    }
    puts("view ancestry gate PASS");
}
