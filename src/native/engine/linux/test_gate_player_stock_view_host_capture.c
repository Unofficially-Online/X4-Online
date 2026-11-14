#define main guest_ui_fixture_main
#include "test_gate_player_stock_view_ui_selection.c"
#undef main
#include "player_stock_view_local_control_proof.h"
#define X4_GATE_PLAYER_STOCK_VIEW_CONTROL_PROOF_H

static struct {
    uintptr_t universe, player, controller, ship;
    uint64_t player_runtime, runtime;
} keyboard_probe_binding;
static uintptr_t factory_probe_player1;
static void *player_routes_scope;
static unsigned proof_ok = 1, worker_ok = 1;
static int keyboard_probe_worker(void) { return worker_ok; }
static int stock_observer_worker_boundary(void) { return 1; }
static int stock_view_control_native_pins(void) { return 1; }
static int stock_view_control_native_read(void *unused, uintptr_t address, void *out, size_t size) {
    (void)unused;
    return read_local((void *)address, out, size);
}
static int fake_local_proof(const player_stock_view_control_ops *ops) {
    return proof_ok && ops->authenticate(ops->context);
}
#define player_stock_view_local_control_equivalent fake_local_proof
#include "gate_player_stock_view_host_capture.h"

int main(void) {
    unsigned char player[0x80] = {0}, room[0x80] = {0}, controller[0x30] = {0}, ship[0x208] = {0};
    uintptr_t pointer = (uintptr_t)room;
    memcpy(player + 0x70, &pointer, 8);
    uint64_t room_id = 3;
    memcpy(room + 8, &room_id, 8);
    pointer = (uintptr_t)ship;
    memcpy(controller + 0x20, &pointer, 8);
    pointer = (uintptr_t)controller;
    memcpy(ship + 0x200, &pointer, 8);
    pointer = (uintptr_t)player;
    memcpy(universe + 0x238, &pointer, 8);
    pointer = (uintptr_t)camera;
    memcpy(universe + 0x3e8, &pointer, 8);
    manager_global = (uintptr_t)manager;
    manager[0x5c] = 1;
    pointer = (uintptr_t)entry;
    memcpy(manager + 0x40, &pointer, 8);
    memcpy(entry + 0x50, &handle, 8);
    keyboard_probe_binding.universe = (uintptr_t)universe;
    keyboard_probe_binding.player = (uintptr_t)player;
    keyboard_probe_binding.controller = (uintptr_t)controller;
    keyboard_probe_binding.ship = (uintptr_t)ship;
    keyboard_probe_binding.player_runtime = 1;
    keyboard_probe_binding.runtime = 4;
    factory_probe_player1 = (uintptr_t)player;
    stats.slot = 1;
    stats.issued = 44;
    player_recurring_tail_token.generation = 44;
    player_stock_view_capture_identity identity = {44, (uintptr_t)universe, (uintptr_t)player, (uintptr_t)controller, 1};
    player_stock_view_capture_sample sample = {0};
    assert(player_stock_view_host_capture_observed(NULL, &identity, (uintptr_t)camera, 1.25f, &sample));
    assert(sample.bits == 1 && sample.fov == 1.25f && sample.target == 99 && fov_calls == 0 && getter_calls == 2);
    assert(!player_stock_view_host_capture_observed(NULL, &identity, (uintptr_t)camera + 8, 1.25f, &sample));
    pointer = 0;
    memcpy(ship + 0x200, &pointer, 8);
    assert(!player_stock_view_host_capture_observed(NULL, &identity, (uintptr_t)camera, 1.25f, &sample));
    pointer = (uintptr_t)controller;
    memcpy(ship + 0x200, &pointer, 8);
    proof_ok = 0;
    assert(!player_stock_view_host_capture_observed(NULL, &identity, (uintptr_t)camera, 1.25f, &sample));
    proof_ok = 1;
    worker_ok = 0;
    assert(!player_stock_view_host_capture_observed(NULL, &identity, (uintptr_t)camera, 1.25f, &sample));
    worker_ok = 1;
    ++identity.generation;
    assert(!player_stock_view_host_capture_observed(NULL, &identity, (uintptr_t)camera, 1.25f, &sample));
    --identity.generation;
    stats.slot = 2;
    assert(!player_stock_view_host_capture_observed(NULL, &identity, (uintptr_t)camera, 1.25f, &sample));
    return 0;
}
