/* Standalone real-header fixture: cc -std=c11 test_gate_player_lifecycle_input.c -o /tmp/input-test */
#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const char *name; unsigned va, length; const char *hash; } lx_site;
static unsigned char ship[0x210], controller[0x40], keys[4];
static unsigned char flight, enabled, ignored;
static uint64_t autopilot;
static float digital;
static uintptr_t lifecycle_discard_ship, lifecycle_discard_controller;
static struct { uintptr_t player2; unsigned player2_owned, player2_uncertain; } factory_probe_state;
static struct { struct { uintptr_t target; } identity; } player_target_hold_resident;
static atomic_uint stock_observer_input_depth;
static struct { atomic_uint wrapper_depth; } sko_session;
static int pins, joined, worker, singleton, held, state_calls, markers, state_mode;
static const char *reason;
static jmp_buf refusal;
#define SEAT_PREFLIGHT_CONTROLLER_VTABLE 0x3b7c208
static void lifecycle_discard_require(int condition, const char *why) {
    if (!condition) { reason = why; longjmp(refusal, 1); }
}
static int site_ok(const lx_site *site) { (void)site; return pins; }
static int factory_probe_joined_boundary(void) { return joined; }
static void *gate_tls_self(void) { return ship; }
static int gate_tls_worker(void *tls) { return tls == ship && worker; }
static int factory_probe_singleton(uintptr_t player) { return player == 77 && singleton; }
static int player_target_hold_check(void) { return held; }
static uintptr_t lifecycle_discard_pointer(uintptr_t address) {
    uintptr_t value; memcpy(&value, (void *)address, 8); return value;
}
static void lifecycle_discard_read(uintptr_t address, void *out, size_t length) {
    memcpy(out, (void *)address, length);
}
static void lifecycle_discard_write(const char *text) {
    if (!markers) assert(strstr(text, "input=press id=2 state=1 keys=1000 digital=0\n"));
    else assert(strstr(text, "input=release id=2 state=0 keys=0000 digital=-1\n"));
    markers++;
}
static void fake_state(int id, unsigned char state) {
    assert(id == 2 && state <= 1);
    state_calls++;
    if (state_mode == 1 || (state_mode == 6 && !state)) return;
    keys[0] = state;
    if (state_mode == 2) { keys[0] = 0; keys[1] = state; }
    if (state_mode == 3) digital = -1;
    if (state_mode == 4) digital = NAN;
    if (state_mode == 5) singleton = 0;
}
static void *fixture_lx(unsigned va) {
    switch (va) {
    case SEAT_PREFLIGHT_CONTROLLER_VTABLE: return (void *)(uintptr_t)va;
    case 0x166c6f0: return (void *)fake_state;
    case 0x3d8daa8: return &flight;
    case 0x3d8da60: return &enabled;
    case 0x3e5b29a: return &ignored;
    case 0x3d8da58: return &autopilot;
    case 0x3d8da68: return keys;
    case 0x3d8d9ac: return &digital;
    default: abort();
    }
}
#define LX(va) fixture_lx(va)
#include "gate_player_lifecycle_input.h"

static void reset(void) {
    memset(ship, 0, sizeof(ship)); memset(controller, 0, sizeof(controller));
    memset(keys, 0, sizeof(keys));
    lifecycle_discard_ship = (uintptr_t)ship;
    lifecycle_discard_controller = (uintptr_t)controller;
    player_target_hold_resident.identity.target = (uintptr_t)ship;
    uintptr_t vtable = SEAT_PREFLIGHT_CONTROLLER_VTABLE;
    memcpy(controller, &vtable, 8);
    memcpy(controller + 0x20, &lifecycle_discard_ship, 8);
    memcpy(ship + 0x200, &lifecycle_discard_controller, 8);
    factory_probe_state.player2 = 77;
    factory_probe_state.player2_owned = 1;
    factory_probe_state.player2_uncertain = 0;
    pins = joined = worker = singleton = held = flight = enabled = 1;
    ignored = autopilot = digital = state_calls = markers = state_mode = 0;
    atomic_store(&stock_observer_input_depth, 0); atomic_store(&sko_session.wrapper_depth, 0);
    lifecycle_input_pressed = lifecycle_input_released = 0;
    reason = NULL;
}

static void exercise(int test) {
    switch (test) {
    case 0: pins = 0; break;
    case 1: singleton = 0; break;
    case 2: worker = 0; break;
    case 3: joined = 0; break;
    case 4: held = 0; break;
    case 5: atomic_store(&stock_observer_input_depth, 1); break;
    case 6: atomic_store(&sko_session.wrapper_depth, 1); break;
    case 7: flight = 0; break;
    case 8: enabled = 0; break;
    case 9: ignored = 1; break;
    case 10: autopilot = 1; break;
    case 11: keys[2] = 1; break;
    case 12: digital = NAN; break;
    case 13: digital = -1; break;
    case 14: state_mode = 1; break; /* STATE no-op. */
    case 15: state_mode = 2; break; /* Wrong key. */
    case 16: state_mode = 3; break; /* Direct axis mutation is not Update evidence. */
    case 17: state_mode = 4; break;
    case 18: state_mode = 5; break;
    case 19: memset(controller + 0x20, 0, 8); break;
    case 20: factory_probe_state.player2_uncertain = 1; break;
    case 28: autopilot = UINT64_C(0x100000000); break;
    }
    lifecycle_discard_input_press();
    /* A fixture supplies the inter-call observation; this header never owns Update. */
    digital = test == 21 ? 0 : test == 22 ? 1 : test == 23 ? NAN : -1;
    if (test == 24) state_mode = 6;
    if (test == 25) { lifecycle_discard_input_press(); return; }
    lifecycle_discard_input_release();
    if (test == 26) lifecycle_discard_input_release();
}

int main(void) {
    for (int test = 0; test <= 28; ++test) {
        reset();
        int rejected = setjmp(refusal);
        if (!rejected) exercise(test);
        assert(rejected == (test != 27));
        if (test <= 13 || test == 19 || test == 20 || test == 28) assert(state_calls == 0 && markers == 0);
        if (test >= 14 && test <= 18) assert(state_calls == 1 && markers == 0);
        if (test >= 21 && test <= 23) assert(state_calls == 1 && markers == 1);
        if (test == 24) assert(state_calls == 2 && markers == 1);
        if (test == 25) assert(state_calls == 1 && markers == 1);
        if (test == 26 || test == 27) assert(state_calls == 2 && markers == 2);
        if (rejected) assert(reason);
    }
    puts("PASS: 29 real-header STATE input cases; no native game or Update call");
}
