#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include "stock_keyboard_replay.h"
#define X4_PLAYER_CURSOR_PRELOAD_PIN_H

typedef struct { int unused; } player_view_query_api;
typedef struct { int (*bind)(uint32_t, int (*)(uintptr_t)); int (*targets)(uintptr_t, uintptr_t); } player_cursor_preload_api;
static int bind_targets(uintptr_t canonical, uintptr_t guest) { return canonical && guest && canonical != guest; }
static int (*bound_callback)(uintptr_t);
static int bind_cursor(uint32_t owner, int (*callback)(uintptr_t)) {
    if (owner != (uint32_t)syscall(SYS_gettid)) return 0;
    bound_callback = callback; return 1;
}
static int view_query_expected_library(const char **sha, uint64_t *size) { *sha="fixture"; *size=1; return 1; }
static int player_view_query_provider(const char *sha, uint64_t size, player_view_query_api *out) {
    (void)sha; (void)size; (void)out; return 1;
}
static int player_cursor_preload_provider(const player_view_query_api *view, player_cursor_preload_api *out) {
    (void)view; out->bind=bind_cursor; out->targets=bind_targets; return 1;
}
static struct { uint64_t issued; } stats;
static struct { uint64_t generation; } player_recurring_tail_token;
static struct { uintptr_t owner; } sko_session;
typedef struct { unsigned version; uint64_t apply; } fixture_phase;
static fixture_phase phase = {1, 40};
static fixture_phase *player_startup(void) { return &phase; }
static uint64_t player_recurring_end = 100;
static int player_network_busy, entered = 1, focus = 1;
static int keyboard_probe_requested = 1;
static keyboard_replay keyboard_probe_queue;
static struct { uintptr_t controller, ship; } keyboard_probe_binding;
static uintptr_t universe_pointer;
static uintptr_t factory_probe_player1, lifecycle_discard_controller, lifecycle_discard_ship;
static struct { uintptr_t player2; } factory_probe_state;
static struct { int routes; } player_physics_routes;
static void *player_routes_scope;
static float native_cursor[3];
static unsigned char flight = 1;
static unsigned long GetCurrentThreadId(void) { return 42; }
static int keyboard_probe_worker(void) { return 1; }
static int player_recurring_physics_entered(void) { return entered; }
static int player_physics_identity(void) { return 1; }
static int gate_tls_self(void) { return 1; }
static int gate_tls_worker(int self) { return self; }
static int player_stock_focus_wanted(void) { return 1; }
static int player_stock_focus_read(uint64_t generation, unsigned *value) {
    if (generation != stats.issued) return 0;
    *value = (unsigned)focus; return 1;
}
static int gate_read_local(const void *address, void *output, size_t length) { memcpy(output, address, length); return 1; }
static void *fake_address(unsigned va) {
    if (va == 0x3db6948) return &universe_pointer;
    if (va == 0x3daaaf8) return &sko_session.owner;
    if (va == 0x3e5b29a || va == 0x3daaaf0 || va == 0x3daad60) return &universe_pointer;
    if (va == 0x3d8daa8) return &flight;
    if (va == 0x3d8d9b8) return native_cursor;
    return NULL;
}
#define LX(va) fake_address(va)
static int player_space_mode(void) { return 0; }
static int player_space_local_state(unsigned *active) { *active = 1; return 1; }
static unsigned player_space_slot_state(unsigned slot) { (void)slot; return 1; }
#include "gate_player_cursor_replay.h"
#include "gate_player_cursor_input_trace.h"

int main(void) {
    assert(setenv("X4_DEV_PLAYER_CURSOR", "1", 1) == 0);
    assert(setenv("X4_DEV_CURSOR_INPUT_TRACE", "invalid", 1) == 0);
    assert(!player_cursor_request(1));
    assert(!bound_callback && !player_cursor_mode());
    assert(unsetenv("X4_DEV_PLAYER_CURSOR") == 0);
    assert(setenv("X4_DEV_CURSOR_INPUT_TRACE", "1", 1) == 0);
    assert(!player_cursor_request(1));
    assert(!bound_callback && !player_cursor_mode());
    assert(setenv("X4_DEV_PLAYER_CURSOR", "1", 1) == 0);
    assert(player_cursor_request(1));
    assert(bound_callback && player_cursor_mode());
    puts("invalid mouse trace configuration refuses before binding");
    return 0;
}
