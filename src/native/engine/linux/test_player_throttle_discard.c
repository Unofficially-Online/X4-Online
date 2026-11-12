#define _POSIX_C_SOURCE 200809L
/* Engine-free fixture for the disposable STATE13 sequence and readbacks. */
#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lockstep_stock_record.h"

typedef struct { const char *name; uintptr_t va; size_t size; const char *hash; } lx_site;
static unsigned char ship[0x200], member_a[0x400], member_b[0x400];
static unsigned char controller[0x19160], factor_table[0x80];
static uintptr_t factor_table_pointer;
static uintptr_t lifecycle_discard_ship, lifecycle_discard_controller;
static struct { void *player2; } factory_probe_state;
static uintptr_t member_list[2], node[2];
static unsigned char keys[2], ignored;
static float command, lower = -1.0f, upper = 1.0f, original;
static int correct_identity = 1, entered = 1, pins = 1, empty_collection;
static unsigned state_calls, log_rows;
static const char *failure;
static jmp_buf escape;

static void lifecycle_discard_require(int condition, const char *reason) {
    if (!condition) {
        failure = reason;
        longjmp(escape, 1);
    }
}
static int player_recurring_physics_entered(void) { return entered; }
static int lifecycle_input_pins(void) { return pins; }
static int site_ok(const lx_site *site) { return pins && site->va && strlen(site->hash) == 64; }
static int factory_probe_singleton(void *player) { return correct_identity && player == factory_probe_state.player2; }
static void lifecycle_discard_write(const char *message) { assert(message && *message); ++log_rows; }
static int read_local(const void *address, void *output, size_t length) {
    memcpy(output, address, length);
    return 1;
}
static void lifecycle_discard_read(uintptr_t address, void *output, size_t length) {
    memcpy(output, (void *)address, length);
}
static void *ship_getter(void) { return correct_identity ? ship : NULL; }
static void *controller_getter(void) { return correct_identity ? (void *)lifecycle_discard_controller : NULL; }
static void iterator_begin(void *cursor, void *actual_ship) {
    assert(actual_ship == ship);
    if (!empty_collection) {
        memcpy(cursor, &(uintptr_t){(uintptr_t)node}, sizeof(uintptr_t));
    }
}
static void iterator_next(void *cursor) {
    uint32_t index = 0;
    memcpy(&index, (unsigned char *)cursor + 0x10, 4);
    if (++index == 2) {
        memset(cursor, 0, sizeof(uintptr_t));
    } else {
        memcpy((unsigned char *)cursor + 0x10, &index, 4);
    }
}
static void native_state(int id, unsigned char value) {
    assert(id == 13 && value <= 1);
    ++state_calls;
    keys[0] = value;
    original = command;
    if (value) {
        uint32_t changed = 0x3f800000;
        memcpy(member_a + 0x2f0, &changed, 4);
        member_a[0x2f4] = 1;
    }
}
static void *lookup(uintptr_t address) {
    switch (address) {
        case 0x1dc8d40: return (void *)ship_getter;
        case 0x1dd19d0: return (void *)controller_getter;
        case 0x147f790: return (void *)iterator_begin;
        case 0xee29b0: return (void *)iterator_next;
        case 0x166c6f0: return (void *)native_state;
        case 0x3db83c8: return &factor_table_pointer;
        case 0x3e5b298: return &ignored;
        case 0x3d8da7a: return keys;
        case 0x3d8d9dc: return &command;
        case 0x3d8d9e8: return &lower;
        case 0x3d8d9ec: return &upper;
        case 0x3d8da00: return &original;
        default: abort();
    }
}
#define LX(address) lookup(address)
#include "gate_player_throttle_discard.h"

static void reset(void) {
    memset(ship, 0, sizeof ship);
    memset(member_a, 0, sizeof member_a);
    memset(member_b, 0, sizeof member_b);
    lifecycle_discard_ship = (uintptr_t)ship;
    lifecycle_discard_controller = (uintptr_t)controller;
    factor_table_pointer = (uintptr_t)factor_table;
    memset(factor_table, 0, sizeof factor_table);
    memset(controller, 0, sizeof controller);
    factory_probe_state.player2 = (void *)0x1234;
    member_list[0] = (uintptr_t)member_a;
    member_list[1] = (uintptr_t)member_b;
    node[0] = (uintptr_t)member_list;
    node[1] = (uintptr_t)(member_list + 2);
    memcpy(ship + 0xa8, &(uintptr_t){0x7766}, sizeof(uintptr_t));
    memset(keys, 0, sizeof keys);
    command = original = 0;
    lower = -1.0f;
    upper = 1.0f;
    ignored = 0;
    entered = pins = correct_identity = 1;
    empty_collection = 0;
    player_throttle_phase = state_calls = log_rows = 0;
    failure = NULL;
}

int main(void) {
    stock_input_record empty = {0};
    unsetenv("X4_DEV_P2_THROTTLE");
    reset();
    player_throttle_apply(5, &empty);
    assert(state_calls == 1 && keys[0] == 1 && player_throttle_phase == 1);
    command = 0.2f; /* Represents the intervening native Update. */
    player_throttle_after_update(5);
    player_throttle_apply(6, &empty);
    player_throttle_after_update(6);
    assert(state_calls == 2 && !keys[0] && player_throttle_phase == 2 && log_rows == 18);

    reset();
    empty_collection = 1;
    player_throttle_apply(5, &empty);
    player_throttle_after_update(5);
    player_throttle_apply(6, &empty);
    player_throttle_after_update(6);
    assert(state_calls == 2 && !keys[0] && log_rows == 6);

    reset();
    assert(setenv("X4_DEV_P2_THROTTLE", "state13-warm-discard", 1) == 0);
    player_throttle_apply(5, &empty);
    player_throttle_after_update(5);
    assert(state_calls == 0 && player_throttle_phase == 0);
    player_throttle_apply(6, &empty);
    player_throttle_after_update(6);
    player_throttle_apply(7, &empty);
    player_throttle_after_update(7);
    assert(state_calls == 2 && !keys[0] && player_throttle_phase == 2 && log_rows == 18);
    reset();
    float bad_factor = NAN;
    memcpy(factor_table + 0x68, &bad_factor, sizeof bad_factor);
    if (!setjmp(escape)) { player_throttle_apply(6, &empty); assert(0); }
    assert(!strcmp(failure, "throttle_factor_values") && !state_calls);
    unsetenv("X4_DEV_P2_THROTTLE");

    reset();
    pins = 0;
    if (!setjmp(escape)) { player_throttle_apply(5, &empty); assert(0); }
    assert(!strcmp(failure, "throttle_input_pin") && !state_calls);

    reset();
    ignored = 1;
    if (!setjmp(escape)) { player_throttle_apply(5, &empty); assert(0); }
    assert(!strcmp(failure, "throttle_keys_or_values") && !state_calls);

    reset();
    correct_identity = 0;
    if (!setjmp(escape)) { player_throttle_apply(5, &empty); assert(0); }
    assert(!strcmp(failure, "throttle_target_identity") && !state_calls);

    reset();
    command = NAN;
    if (!setjmp(escape)) { player_throttle_apply(5, &empty); assert(0); }
    assert(!strcmp(failure, "throttle_keys_or_values") && !state_calls);

    reset();
    player_throttle_apply(5, &empty);
    member_list[1] = (uintptr_t)member_a;
    if (!setjmp(escape)) { player_throttle_after_update(5); assert(0); }
    assert(!strcmp(failure, "throttle_member_replaced") && state_calls == 1);

    reset();
    empty.count = 1;
    if (!setjmp(escape)) { player_throttle_apply(5, &empty); assert(0); }
    assert(!strcmp(failure, "throttle_script_admission") && !state_calls);
    puts("scripted P2 throttle admission, snapshots and release PASS");
}
