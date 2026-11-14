#define main guest_capture_fixture_main
#include "test_gate_player_view_render_capture.c"
#undef main
#include "player_stock_view_fov_preload.h"
#define X4_GATE_PLAYER_STOCK_VIEW_HOST_CAPTURE_H
#define X4_PLAYER_STOCK_VIEW_FOV_PRELOAD_PIN_H

static struct { uintptr_t universe, player, controller, ship; } keyboard_probe_binding;
static uintptr_t factory_probe_player1;
static int keyboard_probe_worker(void) { return 1; }
static player_stock_view_fov_observer host_bound;
static int host_bind(player_stock_view_fov_observer callback) {
    if (host_bound) return 0;
    host_bound = callback;
    return 1;
}
static int player_stock_view_fov_observer_provider(const player_view_query_api *sealed,
        int (**out)(player_stock_view_fov_observer)) {
    (void)sealed;
    *out = host_bind;
    return 1;
}
static int player_stock_view_host_capture_observed(void *unused, const player_stock_view_capture_identity *identity,
        uintptr_t camera, float fov, player_stock_view_capture_sample *out) {
    (void)unused;
    if (identity->local_slot != 1 || camera != 0xaaaa || fov != 1.25f) return 0;
    *out = (player_stock_view_capture_sample){1, fov, 55};
    return 1;
}
#include "gate_player_view_host_capture_runtime.h"

static void *foreign_host_thread(void *unused) {
    (void)unused;
    host_bound(keyboard_probe_binding.controller, 0xaaaa, 4.0f);
    return NULL;
}

int main(int argc, char **argv) {
    (void)argv;
    unsigned char universe[0x400] = {0}, controller[0x30] = {0}, ship_object[0x208] = {0};
    uintptr_t camera = 0xaaaa, player = 0xbbbb, ship = (uintptr_t)ship_object;
    uintptr_t controller_pointer = (uintptr_t)controller;
    memcpy(universe + 0x3e8, &camera, 8);
    memcpy(universe + 0x238, &player, 8);
    memcpy(controller + 0x20, &ship, 8);
    memcpy(ship_object + 0x200, &controller_pointer, 8);
    keyboard_probe_binding.universe = (uintptr_t)universe;
    keyboard_probe_binding.player = player;
    keyboard_probe_binding.controller = controller_pointer;
    keyboard_probe_binding.ship = ship;
    factory_probe_player1 = player;
    stats.slot = 1;
    stats.active = 1;
    boot_phase = 3;
    startup.version = 1;
    startup.presentation_first = 4;
    stock_observer_worker_thread = ui_thread = GetCurrentThreadId();
    player_session_budget.frames = 1;
    player_session_budget.presentation_count = 8;
    keyboard_probe_queue.config.first_capture = 3;
    keyboard_probe_queue.config.last_capture = 11;
    keyboard_probe_queue.config.delay = 2;
    assert(!setenv("X4_DEV_HOST_VIEW_CAPTURE", "1", 1));
    assert(!unsetenv("X4_DEV_STOCK_VIEW_FOV_OBSERVE"));
    assert(!unsetenv("X4_DEV_STOCK_VIEW_FOV"));
    assert(!player_view_host_capture_start() && !host_bound);
    assert(!setenv("X4_DEV_STOCK_VIEW_FOV", "invalid", 1));
    assert(!player_view_host_capture_start() && !host_bound);
    assert(!setenv("X4_DEV_STOCK_VIEW_FOV_OBSERVE", "1", 1));
    assert(!setenv("X4_DEV_STOCK_VIEW_FOV", "1", 1));
    assert(!player_view_host_capture_start() && !host_bound);
    assert(!unsetenv(argc > 1 ? "X4_DEV_STOCK_VIEW_FOV_OBSERVE" : "X4_DEV_STOCK_VIEW_FOV"));
    assert(player_view_host_capture_start() && host_bound);
    for (uint64_t tick = 3; tick < 10; ++tick) assert(player_view_host_capture_seal(tick));
    stats.issued = player_recurring_tail_token.generation = 9;
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, foreign_host_thread, NULL));
    assert(!pthread_join(thread, NULL));
    host_bound(keyboard_probe_binding.controller + 8, camera, 1.25f);
    assert(!host_view_fov.initialized);
    uintptr_t missing_controller = 0;
    memcpy(ship_object + 0x200, &missing_controller, 8);
    host_bound(keyboard_probe_binding.controller, camera, 1.25f);
    assert(!host_view_fov.initialized);
    memcpy(ship_object + 0x200, &controller_pointer, 8);
    host_bound(keyboard_probe_binding.controller, camera, 1.25f);
    stats.issued = player_recurring_tail_token.generation = 10;
    assert(player_view_host_capture_seal(10) && host_view_sealed_present && host_view_sealed.fov == 1.25f);
    stats.issued = player_recurring_tail_token.generation = 11;
    assert(!player_view_host_capture_seal(11)); /* missing fresh sample */
    return 0;
}
