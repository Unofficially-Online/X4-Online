#ifndef X4_GATE_PLAYER_STOCK_VIEW_WIRE_RUNTIME_H
#define X4_GATE_PLAYER_STOCK_VIEW_WIRE_RUNTIME_H

/* Bind the complete, finite VIEW route before requesting the keyboard queue.
 * Capture providers authenticate their local scope before sealing a sample;
 * replay providers authenticate both player identities before consuming it.
 * This coordinator joins those two contracts without inventing a sample in
 * the startup or presentation-drain intervals. */
static int player_view_xs_ready_before_apply(void);
static int player_view_xs_ready_at_generation(uint64_t generation);

static keyboard_replay_config view_wire_runtime_config;
static unsigned view_wire_runtime_configured;

static int view_wire_setting(const char *name) {
    const char *value = getenv(name);
    return value && !strcmp(value, "1");
}

static int view_wire_environment(void) {
    static const char *const common[] = {
        "X4_DEV_STOCK_VIEW_WIRE", "X4MP_VIEW_QUERY_OBSERVER",
        "X4_DEV_STOCK_VIEW_FOV", "X4_DEV_STOCK_VIEW_SHARED",
        "X4_DEV_STOCK_VIEW_COMPONENT_POLICY", "X4_DEV_VIEW_XS",
        "X4_DEV_VIEW_ANCESTRY", "X4_DEV_VIEW_COMPONENT_TRACE",
        "X4_DEV_VIEW_PRECOMPARE_TRACE", "X4_DEV_PLAYER_CURSOR",
        "X4_DEV_PLAYER_FOCUS"
    };
    for (unsigned i = 0; i < sizeof common / sizeof common[0]; ++i) {
        if (!view_wire_setting(common[i])) return 0;
    }
    if (getenv("X4_DEV_STOCK_VIEW_FOV_OBSERVE")) return 0;
    if (stats.slot == 1) {
        return view_wire_setting("X4_DEV_HOST_VIEW_CAPTURE") &&
            !getenv("X4_DEV_RENDER_FOV_CAPTURE");
    }
    if (stats.slot != 2 || getenv("X4_DEV_HOST_VIEW_CAPTURE")) return 0;
    static const char *const guest[] = {
        "X4MP_UI_CALLBACK_OBSERVER", "X4_DEV_RENDER_FOV_OBSERVE",
        "X4_DEV_RENDER_FOV_CAPTURE", "X4_DEV_UI_RENDER_LEASE",
        "X4_DEV_UI_LIVE_POSE_OBSERVER", "X4_DEV_UI_SEAT_QUERY",
        "X4_DEV_UI_SEAT_CORRECTION", "X4_DEV_UI_CAMERA_REFILL",
        "X4_DEV_UI_PROJECTION_FOV_SCOPE"
    };
    for (unsigned i = 0; i < sizeof guest / sizeof guest[0]; ++i) {
        if (!view_wire_setting(guest[i])) return 0;
    }
    return 1;
}

static int view_wire_same_config(const keyboard_replay_config *current) {
    const keyboard_replay_config *original = &view_wire_runtime_config;
    return current->own_slot == original->own_slot && current->delay == original->delay &&
        current->lookahead == original->lookahead && current->first_capture == original->first_capture &&
        current->last_capture == original->last_capture &&
        current->saved_ship_ids[0] == original->saved_ship_ids[0] &&
        current->saved_ship_ids[1] == original->saved_ship_ids[1] &&
        current->independent_players == original->independent_players &&
        current->scripted_throttle == original->scripted_throttle &&
        current->cursor_active_only == original->cursor_active_only &&
        current->space_context8 == original->space_context8;
}

static int view_wire_runtime_ready(void *context, const keyboard_replay_config *config,
                                   uint64_t generation) {
    (void)context;
    const player_startup_phases *phase = player_startup();
    if (!config || !view_wire_environment() || phase->version != 1 ||
        player_session_budget.frames < 180 || player_session_budget.frames > 900 ||
        config->own_slot != stats.slot || !config->independent_players ||
        !config->cursor_active_only || config->scripted_throttle || config->delay != 2 ||
        config->lookahead || config->first_capture != phase->capture ||
        config->last_capture != player_session_budget.last_capture ||
        !config->saved_ship_ids[0] || !config->saved_ship_ids[1] ||
        config->saved_ship_ids[0] == config->saved_ship_ids[1]) return 0;

    if (!generation) {
        if (view_wire_runtime_configured || config->view_capture || stats.issued != 1 ||
            !player_view_xs_ready_before_apply()) return 0;
        view_wire_runtime_config = *config;
        view_wire_runtime_configured = 1;
        return 1;
    }
    if (!view_wire_runtime_configured || config->view_capture != 1 ||
        !view_wire_same_config(config) || generation != stats.issued ||
        generation != player_recurring_tail_token.generation ||
        generation < phase->capture ||
        (generation > config->last_capture && generation - config->last_capture > config->delay) ||
        !keyboard_probe_worker() || !player_recurring_physics_entered()) return 0;

    /* The first two captures precede the first apply. Their sealed samples
     * must be absent; the handoff independently checks that exact interval. */
    if (generation < phase->apply) return !stock_view_gate_bound;
    if (!player_view_xs_ready_at_generation(generation) ||
        stock_view_gate_state.saved[0] != config->saved_ship_ids[0] ||
        stock_view_gate_state.saved[1] != config->saved_ship_ids[1]) return 0;
    if (generation >= keyboard_view_wire.first && generation <= keyboard_view_wire.last) {
        if (stats.slot == 1) {
            return atomic_load_explicit(&host_view_enabled, memory_order_acquire) == 1;
        }
        return atomic_load_explicit(&render_view_enabled, memory_order_acquire) == 1 &&
            ui_projection_scope_enabled && ui_camera_refill_enabled && ui_game_render_enabled;
    }
    return 1;
}

static int player_stock_view_wire_request(void) {
    const char *setting = getenv("X4_DEV_STOCK_VIEW_WIRE");
    if (!setting) return !getenv("X4_DEV_STOCK_VIEW_FOV") && !getenv("X4_DEV_VIEW_XS");
    if (strcmp(setting, "1") || view_wire_runtime_configured || !view_wire_environment()) return 0;
    return player_stock_view_wire_enable(view_wire_runtime_ready, NULL);
}
#endif
