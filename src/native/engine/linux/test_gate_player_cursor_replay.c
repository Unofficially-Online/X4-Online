#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include "stock_keyboard_replay.h"
#include "player_view_pin_map_batch.h"
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
static int provider_reject;
static int provider_require_batch;
static int player_view_query_provider(const char *sha, uint64_t size, player_view_query_api *out) {
    (void)sha; (void)size; (void)out;
    if (provider_require_batch) assert(player_view_active_map_batch);
    return !provider_reject;
}
static int player_cursor_preload_provider(const player_view_query_api *view, player_cursor_preload_api *out) {
    (void)view; out->bind=bind_cursor; out->targets=bind_targets; return 1;
}
static struct { uint64_t issued; } stats;
static struct { uint64_t generation; } player_recurring_tail_token;
typedef struct { unsigned version; uint64_t apply; } fixture_phase;
static fixture_phase phase = {1, 40};
static fixture_phase *player_startup(void) { return &phase; }
static uint64_t player_recurring_end = 100;
static int player_network_busy, entered = 1, focus = 1;
static int keyboard_probe_requested = 1;
static keyboard_replay keyboard_probe_queue;
static struct { uintptr_t controller, ship; } keyboard_probe_binding;
static unsigned char universe[0x300], p1[64], p2[64], ship1[64], ship2[64], controller1[64], controller2[64];
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
    if (va == 0x3d8daa8) return &flight;
    if (va == 0x3d8d9b8) return native_cursor;
    return NULL;
}
#define LX(va) fake_address(va)
static int player_space_mode(void) { return 0; }
static int player_space_local_state(unsigned *active) { *active = 1; return 1; }
static unsigned player_space_slot_state(unsigned slot) { (void)slot; return 1; }
static int player_cursor_input_request(unsigned enabled) { (void)enabled; return 1; }
static int player_cursor_input_observe(uint64_t generation) { (void)generation; return 1; }
#include "gate_player_cursor_replay.h"

static void set_player(uintptr_t player) { memcpy(universe + 0x238, &player, sizeof player); }
static void tick(uint64_t value) { stats.issued = player_recurring_tail_token.generation = value; }
int main(void) {
    uintptr_t value = (uintptr_t)ship1; memcpy(controller1 + 0x20, &value, sizeof value);
    value = (uintptr_t)ship2; memcpy(controller2 + 0x20, &value, sizeof value);
    universe_pointer = (uintptr_t)universe;
    factory_probe_player1 = (uintptr_t)p1; factory_probe_state.player2 = (uintptr_t)p2;
    keyboard_probe_binding.controller = (uintptr_t)controller1; keyboard_probe_binding.ship = (uintptr_t)ship1;
    lifecycle_discard_controller = (uintptr_t)controller2; lifecycle_discard_ship = (uintptr_t)ship2;
    keyboard_probe_queue.config.first_capture = 38;
    keyboard_probe_queue.config.saved_ship_ids[0] = 111; keyboard_probe_queue.config.saved_ship_ids[1] = 222;
    tick(1); assert(!setenv("X4_DEV_PLAYER_CURSOR", "1", 1));
    provider_require_batch = 1;
    provider_reject = 1;
    assert(!player_cursor_update_original_pin() && !player_view_active_map_batch);
    provider_reject = 0;
    assert(player_cursor_update_original_pin() && !player_view_active_map_batch);
    player_view_map_batch nested = {0};
    player_view_active_map_batch = &nested;
    assert(!player_cursor_update_original_pin() && player_view_active_map_batch == &nested);
    player_view_active_map_batch = NULL;
    provider_require_batch = 0;
    assert(player_cursor_request(1) && player_cursor_mode() && bound_callback);
    tick(38); set_player(factory_probe_player1); assert(player_cursor_capture_only(38));
    native_cursor[0] = 0.5f; assert(bound_callback((uintptr_t)controller1));
    float sample[3]; assert(player_cursor_capture_sample(38, sample) && sample[0] == 0.5f && native_cursor[0] == 0);
    tick(39); assert(player_cursor_capture_only(39)); focus = 0;
    native_cursor[0] = 0.75f; assert(bound_callback((uintptr_t)controller1));
    assert(player_cursor_capture_sample(39, sample) && sample[0] == 0);
    tick(40); focus = 1;
    stock_input_record records[2] = {{.flags=STOCK_INPUT_ACTIVE|STOCK_INPUT_CURSOR,.capture16=38,.cursor={1,0,0}},
                                     {.flags=STOCK_INPUT_ACTIVE|STOCK_INPUT_CURSOR,.capture16=38,.cursor={2,0,0}}};
    uint64_t saved[2] = {111,222}; player_cursor_convergence next; assert(player_cursor_prepare_transaction(40, saved, records, &next)); player_cursor_state = next;
    player_routes_scope = &player_physics_routes.routes; player_network_busy = 1; set_player(factory_probe_state.player2);
    flight = 0;
    assert(!bound_callback((uintptr_t)controller2) && native_cursor[0] == 0);
    flight = 1;
    set_player(factory_probe_player1);
    assert(!bound_callback((uintptr_t)controller2) && native_cursor[0] == 0);
    set_player(factory_probe_state.player2);
    assert(bound_callback((uintptr_t)controller2) && native_cursor[0] == 2);
    player_routes_scope = NULL; player_network_busy = 0; set_player(factory_probe_player1);
    native_cursor[0] = 0.125f; assert(bound_callback((uintptr_t)controller1) && native_cursor[0] == 1);
    assert(player_cursor_capture_sample(40, sample) && sample[0] == 0.125f);
    assert(player_cursor_tail(40, 0));
    assert(!bound_callback((uintptr_t)controller1)); /* duplicate fails closed */
    return 0;
}
