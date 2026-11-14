#define _GNU_SOURCE
#include <assert.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include "player_stock_view_wire_handoff.h"
#include "player_startup_phases.h"
#include "player_ui_session_budget.h"

static struct { unsigned slot; uint64_t issued; } stats;
static struct { uint64_t generation; } player_recurring_tail_token;
static struct { uint64_t saved[2]; } stock_view_gate_state;
static unsigned stock_view_gate_bound;
static _Atomic unsigned host_view_enabled, render_view_enabled;
static unsigned ui_projection_scope_enabled, ui_camera_refill_enabled, ui_game_render_enabled;
static player_ui_session_budget player_session_budget;
static player_stock_view_wire_handoff keyboard_view_wire;
static player_startup_phases phase;
static unsigned provider_ready = 1, frame_ready = 1, worker_ready = 1, physics_ready = 1;
static unsigned provider_queries;

static const player_startup_phases *player_startup(void) { return &phase; }
static int keyboard_probe_worker(void) { return worker_ready; }
static int player_recurring_physics_entered(void) { return physics_ready; }
static int player_view_xs_ready_before_apply(void) { ++provider_queries; return provider_ready; }
static int player_view_xs_ready_at_generation(uint64_t generation) {
    return frame_ready && generation == stats.issued;
}
static int player_stock_view_wire_enable(player_stock_view_wire_ready_fn ready, void *context) {
    return player_stock_view_wire_bind(&keyboard_view_wire, ready, context);
}
#include "gate_player_stock_view_wire_runtime.h"

static const char *const common[] = {
    "X4_DEV_STOCK_VIEW_WIRE", "X4MP_VIEW_QUERY_OBSERVER", "X4_DEV_STOCK_VIEW_FOV",
    "X4_DEV_STOCK_VIEW_SHARED", "X4_DEV_STOCK_VIEW_COMPONENT_POLICY", "X4_DEV_VIEW_XS",
    "X4_DEV_VIEW_ANCESTRY", "X4_DEV_VIEW_COMPONENT_TRACE", "X4_DEV_VIEW_PRECOMPARE_TRACE",
    "X4_DEV_PLAYER_CURSOR", "X4_DEV_PLAYER_FOCUS"
};
static const char *const guest[] = {
    "X4MP_UI_CALLBACK_OBSERVER", "X4_DEV_RENDER_FOV_OBSERVE", "X4_DEV_RENDER_FOV_CAPTURE",
    "X4_DEV_UI_RENDER_LEASE", "X4_DEV_UI_LIVE_POSE_OBSERVER", "X4_DEV_UI_SEAT_QUERY",
    "X4_DEV_UI_SEAT_CORRECTION", "X4_DEV_UI_CAMERA_REFILL", "X4_DEV_UI_PROJECTION_FOV_SCOPE"
};

static keyboard_replay_config setup(unsigned slot) {
    for (unsigned i = 0; i < sizeof common / sizeof common[0]; ++i) assert(!setenv(common[i], "1", 1));
    for (unsigned i = 0; i < sizeof guest / sizeof guest[0]; ++i) {
        assert(!(slot == 2 ? setenv(guest[i], "1", 1) : unsetenv(guest[i])));
    }
    assert(!(slot == 1 ? setenv("X4_DEV_HOST_VIEW_CAPTURE", "1", 1) : unsetenv("X4_DEV_HOST_VIEW_CAPTURE")));
    assert(!unsetenv("X4_DEV_STOCK_VIEW_FOV_OBSERVE"));
    assert(player_startup_phases_parse("v1-t40", &phase));
    assert(player_ui_session_parse_phased("180", &phase, &player_session_budget));
    stats.slot = slot; stats.issued = 1;
    player_recurring_tail_token.generation = 1;
    stock_view_gate_bound = view_wire_runtime_configured = 0;
    keyboard_view_wire = (player_stock_view_wire_handoff){0};
    provider_ready = frame_ready = worker_ready = physics_ready = 1;
    host_view_enabled = slot == 1;
    render_view_enabled = slot == 2;
    ui_projection_scope_enabled = ui_camera_refill_enabled = ui_game_render_enabled = slot == 2;
    stock_view_gate_state.saved[0] = 101; stock_view_gate_state.saved[1] = 202;
    return (keyboard_replay_config){.own_slot = slot, .delay = 2,
        .first_capture = 38, .last_capture = 217, .saved_ship_ids = {101, 202},
        .independent_players = 1, .cursor_active_only = 1};
}

static int configure(keyboard_replay_config *config) {
    return player_stock_view_wire_request() && player_stock_view_wire_configure(
        &keyboard_view_wire, config, 50, 213);
}

static int frame(keyboard_replay_config *config, uint64_t generation) {
    stats.issued = player_recurring_tail_token.generation = generation;
    return view_wire_runtime_ready(NULL, config, generation);
}

int main(void) {
    /* Nothing is enabled in the default process. A partial rewrite cannot
     * silently run without the wire coordinator. */
    assert(player_stock_view_wire_request());
    assert(!keyboard_view_wire.ready);
    assert(!setenv("X4_DEV_STOCK_VIEW_FOV", "1", 1));
    assert(!player_stock_view_wire_request());
    for (unsigned slot = 1; slot <= 2; ++slot) {
        keyboard_replay_config config = setup(slot);
        for (unsigned i = 0; i < sizeof common / sizeof common[0]; ++i) {
            assert(!setenv(common[i], "0", 1));
            assert(!player_stock_view_wire_request());
            assert(!setenv(common[i], "1", 1));
        }
        if (slot == 2) for (unsigned i = 0; i < sizeof guest / sizeof guest[0]; ++i) {
            assert(!unsetenv(guest[i]));
            assert(!player_stock_view_wire_request());
            assert(!setenv(guest[i], "1", 1));
        }
        assert(!setenv("X4_DEV_STOCK_VIEW_FOV_OBSERVE", "1", 1));
        assert(!player_stock_view_wire_request());
        assert(!unsetenv("X4_DEV_STOCK_VIEW_FOV_OBSERVE"));
        unsigned queries_before = provider_queries;
        assert(configure(&config) && config.view_capture == 1);
        assert(!player_stock_view_wire_request());
        assert(frame(&config, 38) && frame(&config, 39));
        stock_view_gate_bound = 1;
        assert(!frame(&config, 39));
        assert(frame(&config, 40) && frame(&config, 50) && frame(&config, 213));
        assert(frame(&config, 214) && frame(&config, 217));
        assert(frame(&config, 218) && frame(&config, 219));
        assert(!frame(&config, 220));
        assert(provider_queries == queries_before + 1); /* no disk verification per frame */
        config.saved_ship_ids[1] = 203;
        assert(!frame(&config, 100));
        config.saved_ship_ids[1] = 202;
        stock_view_gate_state.saved[1] = 203;
        assert(!frame(&config, 100));
        stock_view_gate_state.saved[1] = 202;
        frame_ready = 0; assert(!frame(&config, 100)); frame_ready = 1;
        worker_ready = 0; assert(!frame(&config, 100)); worker_ready = 1;
        physics_ready = 0; assert(!frame(&config, 100)); physics_ready = 1;
        config.space_context8 = 1; assert(!frame(&config, 100)); config.space_context8 = 0;
        config.view_capture = 0; assert(!frame(&config, 100)); config.view_capture = 1;
        if (slot == 1) host_view_enabled = 0; else ui_projection_scope_enabled = 0;
        assert(!frame(&config, 100));
    }
    keyboard_replay_config config = setup(2);
    provider_ready = 0;
    assert(!configure(&config) && !config.view_capture && !view_wire_runtime_configured);
    config = setup(2); config.last_capture--;
    assert(!configure(&config));
    config = setup(2); config.scripted_throttle = 1;
    assert(!configure(&config));
    return 0;
}
