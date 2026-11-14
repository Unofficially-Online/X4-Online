#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Compile with the reviewed production header on the include path. All memory
 * below is synthetic; no engine function or process is called. */
static unsigned char input[0xa020], universe[0x300], controller[0x40], window_owner[16], window_context[0x200];
static unsigned char ignored, window_gate_value;
static uintptr_t input_global, universe_global, gate_global, owner_global;
static struct { uintptr_t owner; } sko_session;
static struct { uintptr_t controller, ship; } keyboard_probe_binding;
static struct { uint64_t issued; } stats;
static struct { uint64_t generation; } player_recurring_tail_token;
static uintptr_t factory_probe_player1;
static void *player_routes_scope;
static int player_cursor_ready, worker_ok, recurring_ok;
static uintptr_t failing_address;
static unsigned input_reads, drift_after_input_read;

static int readable_region(uintptr_t address, size_t length, const void *base, size_t size) {
    uintptr_t start = (uintptr_t)base;
    return address >= start && address - start <= size && length <= size - (address - start);
}
static int gate_read_local(const void *source, void *destination, size_t length) {
    uintptr_t address = (uintptr_t)source;
    if (address == failing_address) return 0;
    if (address == (uintptr_t)&input_global && ++input_reads == drift_after_input_read) {
        uintptr_t changed = input_global + 8;
        memcpy(destination, &changed, sizeof changed);
        return 1;
    }
    if (!(readable_region(address, length, input, sizeof input) ||
          readable_region(address, length, universe, sizeof universe) ||
          readable_region(address, length, controller, sizeof controller) ||
          readable_region(address, length, window_owner, sizeof window_owner) ||
          readable_region(address, length, window_context, sizeof window_context) ||
          readable_region(address, length, &ignored, sizeof ignored) ||
          readable_region(address, length, &window_gate_value, sizeof window_gate_value) ||
          readable_region(address, length, &input_global, sizeof input_global) ||
          readable_region(address, length, &universe_global, sizeof universe_global) ||
          readable_region(address, length, &gate_global, sizeof gate_global) ||
          readable_region(address, length, &owner_global, sizeof owner_global))) return 0;
    memcpy(destination, source, length);
    return 1;
}
static void *fake_address(uintptr_t va) {
    switch (va) {
    case 0x3daaaf8: return &input_global;
    case 0x3db6948: return &universe_global;
    case 0x3e5b29a: return &ignored;
    case 0x3daaaf0: return &gate_global;
    case 0x3daad60: return &owner_global;
    default: abort();
    }
}
#define LX(address) fake_address(address)
static int keyboard_probe_worker(void) { return worker_ok; }
static int player_recurring_physics_entered(void) { return recurring_ok; }
static unsigned long GetCurrentThreadId(void) { return 42; }
#include "gate_player_cursor_input_trace.h"

static void setup(void) {
    memset(input, 0, sizeof input);
    memset(universe, 0, sizeof universe);
    memset(controller, 0, sizeof controller);
    memset(window_owner, 0, sizeof window_owner);
    memset(window_context, 0, sizeof window_context);
    ignored = 0; window_gate_value = 1;
    input_global = sko_session.owner = (uintptr_t)input;
    universe_global = (uintptr_t)universe;
    factory_probe_player1 = 0x12340;
    memcpy(universe + 0x238, &factory_probe_player1, sizeof factory_probe_player1);
    keyboard_probe_binding.controller = (uintptr_t)controller;
    keyboard_probe_binding.ship = 0x98760;
    memcpy(controller + 0x20, &keyboard_probe_binding.ship, sizeof keyboard_probe_binding.ship);
    gate_global = (uintptr_t)&window_gate_value;
    owner_global = (uintptr_t)window_owner;
    uintptr_t context = (uintptr_t)window_context;
    memcpy(window_owner + 8, &context, sizeof context);
    input[0x9f33] = input[0xcf0] = 1;
    stats.issued = player_recurring_tail_token.generation = 38;
    player_routes_scope = NULL;
    player_cursor_ready = worker_ok = recurring_ok = 1;
    input_reads = drift_after_input_read = 0;
    failing_address = 0;
    memset(&player_cursor_input_trace, 0, sizeof player_cursor_input_trace);
    assert(setenv(CURSOR_INPUT_TRACE_ENV, "1", 1) == 0);
    assert(player_cursor_input_request(1));
}
static void failure_before_emit(void) {
    unsigned char old_input[sizeof input], old_universe[sizeof universe], old_controller[sizeof controller];
    memcpy(old_input, input, sizeof input);
    memcpy(old_universe, universe, sizeof universe);
    memcpy(old_controller, controller, sizeof controller);
    assert(!player_cursor_input_observe(38));
    assert(!player_cursor_input_trace.emitted);
    assert(!memcmp(old_input, input, sizeof input));
    assert(!memcmp(old_universe, universe, sizeof universe));
    assert(!memcmp(old_controller, controller, sizeof controller));
}
int main(void) {
    setup();
    unsigned char old_input[sizeof input], old_owner[sizeof window_owner], old_context[sizeof window_context];
    memcpy(old_input, input, sizeof input);
    memcpy(old_owner, window_owner, sizeof window_owner);
    memcpy(old_context, window_context, sizeof window_context);
    assert(player_cursor_input_observe(38));
    assert(player_cursor_input_trace.emitted == 1 && player_cursor_input_trace.last.input == (uintptr_t)input);
    assert(player_cursor_input_trace.last.window_gate_value == 1);
    assert(!memcmp(old_input, input, sizeof input));
    assert(!memcmp(old_owner, window_owner, sizeof window_owner));
    assert(!memcmp(old_context, window_context, sizeof window_context));
    assert(player_cursor_input_observe(38) && player_cursor_input_trace.emitted == 1);
    input[0x9f36] = 1;
    assert(player_cursor_input_observe(38) && player_cursor_input_trace.emitted == 2);

    setup(); worker_ok = 0; failure_before_emit();
    setup(); recurring_ok = 0; failure_before_emit();
    setup(); player_routes_scope = &ignored; failure_before_emit();
    setup(); player_cursor_ready = 0; failure_before_emit();
    setup(); player_recurring_tail_token.generation = 39; failure_before_emit();
    setup(); input_global += 8; failure_before_emit();
    setup(); factory_probe_player1++; failure_before_emit();
    setup(); keyboard_probe_binding.ship++; failure_before_emit();
    setup(); failing_address = (uintptr_t)&ignored; failure_before_emit();
    setup(); drift_after_input_read = 3; failure_before_emit();
    setup(); assert(setenv(CURSOR_INPUT_TRACE_ENV, "0", 1) == 0); assert(!player_cursor_input_request(1));
    setup(); assert(!player_cursor_input_request(0));
    assert(unsetenv(CURSOR_INPUT_TRACE_ENV) == 0);
    assert(player_cursor_input_request(0));
    worker_ok = 0;
    assert(player_cursor_input_observe(38));
    puts("cursor input adapter admission, identity and read-only checks passed");
    return 0;
}
