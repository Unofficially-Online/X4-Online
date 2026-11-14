#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include "player_view_xs_policy.h"
#include "player_view_xs_native_pin.h"

typedef struct { player_view_xs_snapshot snapshot; player_view_xs_candidate candidate; int active, kind; unsigned pins, fail_pin; } fixture;
static fixture test;
static unsigned target_calls, external_calls, distance_calls;
static uintptr_t target(void) { ++target_calls; errno = 31; return 0x111; }
static uint64_t external(void) { ++external_calls; errno = 32; return UINT64_C(0x123456789abcdea5); }
static float distance(void *ship, void *camera, unsigned flags) {
    assert(ship == (void *)0x123 && camera == (void *)0x456 && flags == 1);
    ++distance_calls; errno = 33; return 42.0f;
}
static uint64_t thread_id(void *context) { (void)context; return 7; }
static int snapshot(void *context, uint64_t generation, unsigned positions, player_view_xs_snapshot *out) {
    fixture *value = context; (void)generation; (void)positions; errno = 99; *out = value->snapshot; return value->active;
}
static int candidate(void *context, uintptr_t object, unsigned position, player_view_xs_candidate *out) {
    fixture *value = context; (void)position; assert(object == 0x123); *out = value->candidate; return value->kind;
}
static int sealed(void *context) { (void)context; return 1; }
static int pin(void *context, uintptr_t address, size_t length, const char *hash) {
    fixture *value = context; assert(address && length && strlen(hash) == 64);
    return ++value->pins != value->fail_pin;
}
static uintptr_t address(void *context, uintptr_t value) { (void)context; return value; }
static int read_pin(void *context, uintptr_t at, void *out, size_t size) {
    (void)context;
    if (at == 0x3d29e98 && size == 8) { uintptr_t slot = 0x129a680; memcpy(out, &slot, size); return 1; }
    if (at == 0x2d25b20 && size == 4) { uint32_t threshold = 0x4a742400; memcpy(out, &threshold, size); return 1; }
    return 0;
}
int main(void) {
    test.snapshot.generation = 40;
    test.snapshot.slots[0] = (player_view_xs_slot){.ship = 1, .sector = 9, .target = 700,
                                                  .position = {3, 4, 0}, .external = 1};
    test.snapshot.slots[1] = (player_view_xs_slot){.ship = 2, .sector = 8, .target = 1,
                                                  .position = {0, 0, 0}, .external = 1};
    test.candidate = (player_view_xs_candidate){.sector = 9, .portable = 700, .position = {0, 0, 0}};
    player_view_xs_ops ops = {.context = &test, .enabled = 1, .owner_tid = 7, .thread_id = thread_id,
                              .snapshot = snapshot, .candidate = candidate, .native_target = target,
                              .native_external = external, .native_distance = distance};
    uintptr_t target_result = 0; uint64_t external_result = 0; float distance_result = 0;
    test.active = 1; test.kind = 1;
    assert(player_view_xs_target(&ops, 40, 0x123, &target_result) && target_result == 0x123 && errno == 31);
    assert(player_view_xs_external(&ops, 40, &external_result) && external_result == UINT64_C(0x123456789abcde01) && errno == 32);
    assert(player_view_xs_distance(&ops, 40, (void *)0x123, (void *)0x456, 1, &distance_result) && distance_result == 25 && errno == 33);
    test.snapshot.slots[0].target = 701;
    assert(player_view_xs_target(&ops, 40, 0x123, &target_result) && target_result == 0);
    test.snapshot.slots[0].external = 0;
    assert(player_view_xs_distance(&ops, 40, (void *)0x123, (void *)0x456, 1, &distance_result) && distance_result == 3.0e38f);
    test.kind = 0;
    assert(player_view_xs_target(&ops, 40, 0x123, &target_result) && target_result == 0x111);
    assert(player_view_xs_distance(&ops, 40, (void *)0x123, (void *)0x456, 1, &distance_result) && distance_result == 42);
    test.kind = 1; test.snapshot.generation = 39;
    assert(!player_view_xs_target(&ops, 40, 0x123, &target_result) && errno == 31);
    test.snapshot.generation = 40; test.candidate.portable = 0;
    target_result = 0xbeef;
    assert(!player_view_xs_target(&ops, 40, 0x123, &target_result) && target_result == 0xbeef);
    test.candidate.portable = 700; test.snapshot.slots[1].position[0] = NAN;
    distance_result = -7.0f;
    assert(!player_view_xs_distance(&ops, 40, (void *)0x123, (void *)0x456, 1, &distance_result) &&
           distance_result == -7.0f && errno == 33);
    ops.owner_tid = 8;
    assert(!player_view_xs_external(&ops, 40, &external_result) && errno == 32);
    ops.owner_tid = 7; test.active = -1;
    assert(!player_view_xs_external(&ops, 40, &external_result) && errno == 32);
    ops.enabled = 0;
    assert(player_view_xs_external(&ops, 40, &external_result) && external_result == UINT64_C(0x123456789abcdea5));
    assert(target_calls == 5 && external_calls == 4 && distance_calls == 4);
    player_view_xs_pin_ops pins = {&test, sealed, pin, read_pin, address};
    assert(player_view_xs_native_preflight(&pins) && test.pins == 5);
    test.pins = 0; test.fail_pin = 3;
    assert(!player_view_xs_native_preflight(&pins) && test.pins == 3);
    return 0;
}
