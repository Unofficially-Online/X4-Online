#define _GNU_SOURCE
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "stock_keyboard_observer.h"
typedef struct stock_keyboard_identity_sample {
    int ready;
    const char *reason;
    uintptr_t universe;
    uintptr_t player;
    uintptr_t controller;
    uintptr_t ship;
    uintptr_t input_owner;
    uintptr_t controller_owner;
    uintptr_t player_vtable;
    uintptr_t controller_vtable;
    uintptr_t ship_vtable;
    uint64_t player_runtime;
    uint64_t player_saved;
    uint64_t runtime;
    uint64_t saved;
    uint64_t autopilot;
    uint32_t count;
    unsigned char context_cf0;
    unsigned char requested_space;
    unsigned char flight;
    unsigned char enabled;
    unsigned char ignore;
    unsigned char ship_d1;
    unsigned char controller_d9;
    unsigned char mouse[3];
    unsigned char keys[4];
    float axes[3];
    float cursor[3];
} stock_keyboard_identity_sample;
typedef struct lua_State {
    double numbers[9];
    const char *strings[8];
    double result;
    int nil;
    char text[1024];
} lua_State;

static double p_lua_tonumber(lua_State *state, int index) {
    return state->numbers[index];
}

static const char *p_lua_tolstring(lua_State *state, int index, size_t *length) {
    const char *text = state->strings[index];
    *length = text ? strlen(text) : 0;
    return text;
}

static void p_lua_pushnumber(lua_State *state, double number) {
    state->result = number;
}

static void p_lua_pushnil(lua_State *state) {
    state->nil = 1;
}

static void p_lua_pushlstring(lua_State *state, const char *text, size_t length) {
    assert(length < sizeof state->text);
    memcpy(state->text, text, length);
    state->text[length] = 0;
}

static struct {
    unsigned active;
    unsigned slot;
    uint64_t issued;
} stats;

static unsigned boot_phase = 3;
static unsigned main_thread = 1;
static unsigned ui_thread = 2;
static unsigned stock_observer_worker_thread = 2;
static unsigned stock_observer_main_boundary;
static unsigned stock_observer_install_tick;
static unsigned wire_version = 3;
static unsigned wire_clients = 2;
static unsigned wire_lookahead;
static unsigned path_installed = 1;
static atomic_uint stock_observer_input_depth;
static atomic_uint stock_observer_thread_failure;
static unsigned thread_id = 2;
static int boundary = 1;
static int slot_valid = 1;
static int identity_valid = 1;
static stock_keyboard_identity_sample sample;

static unsigned GetCurrentThreadId(void) { return thread_id; }
static int gate_tls_self(void) { return 1; }
static int gate_tls_worker(int value) { return value; }
static int stock_observer_wanted(void) { return 1; }
static int accept_ui(void) { return 1; }
static int stock_observer_worker_boundary(void) { return boundary; }
static int stock_observer_slot_valid(void) { return slot_valid; }

static int stock_keyboard_identity_collect(stock_keyboard_identity_sample *output) {
    *output = sample;
    return identity_valid;
}

#define SEAT_PREFLIGHT_TARGET_SAVED UINT64_C(2)
static struct {
    unsigned target_owned;
    struct { uintptr_t target; uint64_t target_runtime; } identity;
} player_target_hold_resident;
static int physics_ready, physics_entered = 1, target_valid = 1, pins_valid = 1, target_pins_valid = 1;
static int player_target_hold_pins(void) { return target_pins_valid; }
static int player_physics_identity(void) { return physics_ready; }
static int player_recurring_physics_entered(void) { return physics_entered; }
static int player_seat_preflight_resolve(void *context, uint64_t saved, uintptr_t *target, uint64_t *runtime) {
    (void)context;
    if (!target_valid || saved != SEAT_PREFLIGHT_TARGET_SAVED) {
        return 0;
    }
    *target = 0x1234;
    *runtime = 0x5678;
    return 1;
}
static int fixture_sko_validate(int quiescent) { return quiescent && pins_valid; }
#define sko_validate fixture_sko_validate

static unsigned char strafe_keys[4], strafe_ignored, throttle_keys[2], throttle_ignored;
static int player_throttle_enabled(void) { return 0; }
static void player_throttle_require_pins(void) {}
static int strafe_unreadable;
static int read_local(const void *address, void *out, size_t length) {
    if (strafe_unreadable) return 0;
    if ((uintptr_t)address == 0x3d8da72 && length == 4) memcpy(out, strafe_keys, 4);
    else if ((uintptr_t)address == 0x3d8da7a && length == 2) memcpy(out, throttle_keys, 2);
    else if ((uintptr_t)address == 0x3e5b298 && length == 1) memcpy(out, &throttle_ignored, 1);
    else if ((uintptr_t)address == 0x3e5b299 && length == 1) memcpy(out, &strafe_ignored, 1);
    else return 0;
    return 1;
}
static void mock_handler(int id, unsigned char state);
#define LX(value) ((value) == SKO_STATE_VA ? (uintptr_t)&mock_handler : (uintptr_t)(value))
#include "gate_stock_keyboard_replay.h"

static int inside_stop;
static int stop_result;
static int handler_calls;
static int mock_updates_bank = 1;
static unsigned p2_calls;
static uint64_t p2_tick;
static stock_input_record p2_record;
static int p2_break_return, p2_stop, omit_foreign, omit_p1_events, omit_p1_release;

static void player_physics_network_apply_p2(uint64_t generation, const stock_input_record *record) {
    ++p2_calls;
    p2_tick = generation;
    p2_record = *record;
    player_target_hold_resident.target_owned = 1;
    player_target_hold_resident.identity.target = 0x1234;
    player_target_hold_resident.identity.target_runtime = 0x5678;
    physics_ready = !p2_break_return;
    if (p2_stop) {
        (void)keyboard_probe_stop();
    }
}

static void mock_handler(int id, unsigned char state) {
    if (mock_updates_bank) {
        if (id <= 5) sample.keys[id - 2] = state;
        else if (id <= 9) strafe_keys[id - 6] = state;
        else throttle_keys[id - 13] = state;
    }
    ++handler_calls;
    if (inside_stop) {
        stop_result = keyboard_probe_stop();
    }
}

static void setup_fixture_mode(unsigned independent, unsigned own_slot) {
    keyboard_replay_config config = {
        .own_slot = own_slot,
        .delay = 2,
        .lookahead = 0,
        .first_capture = 3,
        .last_capture = 8,
        .saved_ship_ids = {1, 2},
        .independent_players = independent,
    };
    assert(keyboard_replay_init(&keyboard_probe_queue));
    assert(keyboard_replay_configure(&keyboard_probe_queue, &config) == KEYBOARD_REPLAY_OK);
    keyboard_probe_requested = 1;
    keyboard_probe_problem = "active";
    stats.active = 1;
    stats.issued = 3;
    atomic_store(&sko_session.active, 1);
    sample.ready = 1;
    sample.saved = 1;
    sample.player = 1;
    sample.controller = 2;
    sample.ship = 3;
    sample.runtime = 4;
}

static void setup_fixture(void) {
    setup_fixture_mode(0, 1);
}

static sko_event selected_event(void) {
    sko_event event = {
        .thread = 2,
        .caller = 0x1051074,
        .kind = 1,
        .id = 2,
        .state = 1,
    };
    return event;
}

static void test_default_off(void) {
    assert(!keyboard_probe_requested);
    assert(keyboard_probe_admit());
    assert(keyboard_probe_after_path_join());
    assert(keyboard_probe_stop());
    assert(!handler_calls);
}

static void test_inflight_stop(void) {
    setup_fixture();
    inside_stop = 1;
    assert(!keyboard_probe_apply_event(NULL, 2, 1));
    assert(!stop_result);
    assert(keyboard_probe_inflight);
    assert(handler_calls == 1);
    assert(!keyboard_probe_apply_event(NULL, 3, 1));
    assert(handler_calls == 1);
}

static void test_held_stop(void) {
    setup_fixture();
    assert(keyboard_probe_identity());
    assert(keyboard_probe_apply_event(NULL, 2, 1));
    assert(keyboard_probe_held == 1);
    assert(!keyboard_probe_stop());
    assert(handler_calls == 1);
}

static void test_wrong_owner(void) {
    setup_fixture();
    atomic_store(&stock_observer_thread_failure, 1);
    atomic_store(&stock_observer_input_depth, 1);
    sko_event event = selected_event();
    assert(keyboard_probe_consume(NULL, &event) == 1);
    assert(!keyboard_probe_consumed);
    assert(strcmp(keyboard_probe_problem, "active"));
}

static void test_initial_key(void) {
    setup_fixture();
    sample.keys[0] = 1;
    assert(!keyboard_probe_identity());
}

static void test_wrong_apply_phase(void) {
    setup_fixture();
    atomic_store(&stock_observer_input_depth, 1);
    assert(!keyboard_probe_apply_event(NULL, 2, 1));
    assert(!handler_calls);
}

static void test_wrong_capture_phase(void) {
    setup_fixture();
    sko_event event = selected_event();
    assert(keyboard_probe_consume(NULL, &event) == 1);
    assert(keyboard_probe_consumed == 0);
    assert(strcmp(keyboard_probe_problem, "active"));
}

static void test_wrong_stage_phase(void) {
    setup_fixture();
    lua_State state = {0};
    state.numbers[1] = 1;
    state.strings[2] = "00000000000000000000000000000000";
    boundary = 0;
    lua_stock_keyboard_replay_stage(&state);
    assert(!state.result);
    assert(strcmp(keyboard_probe_problem, "active"));
}

static void test_press_release_readback(void) {
    setup_fixture();
    assert(keyboard_probe_identity());
    assert(keyboard_probe_apply_event(NULL, 2, 1));
    assert(sample.keys[0] == 1);
    assert(keyboard_probe_held == 1);
    assert(!keyboard_probe_inflight);
    assert(keyboard_probe_apply_event(NULL, 2, 0));
    assert(sample.keys[0] == 0);
    assert(keyboard_probe_held == 0);
    assert(!keyboard_probe_inflight);
    assert(handler_calls == 2);
    assert(keyboard_probe_stop());
}

static void test_bad_readback(void) {
    setup_fixture();
    assert(keyboard_probe_identity());
    mock_updates_bank = 0;
    assert(!keyboard_probe_apply_event(NULL, 2, 1));
    assert(keyboard_probe_inflight);
    assert(!keyboard_probe_stop());
    assert(handler_calls == 1);
}

static void test_changed_binding(const char *field) {
    setup_fixture();
    assert(keyboard_probe_identity());
    if (!strcmp(field, "universe")) {
        ++sample.universe;
    } else if (!strcmp(field, "input_owner")) {
        ++sample.input_owner;
    } else if (!strcmp(field, "player")) {
        ++sample.player;
    } else if (!strcmp(field, "player_runtime")) {
        ++sample.player_runtime;
    } else if (!strcmp(field, "player_saved")) {
        ++sample.player_saved;
    } else if (!strcmp(field, "controller")) {
        ++sample.controller;
    } else if (!strcmp(field, "ship")) {
        ++sample.ship;
    } else if (!strcmp(field, "runtime")) {
        ++sample.runtime;
    } else if (!strcmp(field, "saved")) {
        ++sample.saved;
    } else {
        assert(!"unknown binding field");
    }
    assert(!keyboard_probe_identity());
    assert(!handler_calls);
}

static void request_arguments(lua_State *state) {
    memset(state, 0, sizeof *state);
    state->numbers[1] = 1;
    state->numbers[2] = 8;
    state->numbers[3] = 2;
    state->numbers[4] = 0;
    state->strings[5] = "0000000000000001";
    state->strings[6] = "0000000000000002";
    stats.active = 1;
    stats.slot = 1;
    stats.issued = 1;
}

static void test_request_default(void) {
    lua_State state;
    request_arguments(&state);
    unsetenv("X4_DEV_PLAYER_PHYSICS");
    lua_stock_keyboard_replay_request(&state);
    assert(state.result == 1 && !keyboard_probe_queue.config.independent_players);
    lua_stock_keyboard_replay_status(&state);
    assert(strstr(state.text, "mode=canonical_keyboard_probe"));
}

static void test_request_independent_mode(void) {
    lua_State state;
    request_arguments(&state);
    state.numbers[7] = 1;
    unsetenv("X4_DEV_PLAYER_PHYSICS");
    lua_stock_keyboard_replay_request(&state);
    assert(state.result == 0 && !keyboard_probe_requested);
    setenv("X4_DEV_PLAYER_PHYSICS", "single-frame-discard", 1);
    lua_stock_keyboard_replay_request(&state);
    assert(state.result == 0 && !keyboard_probe_requested);
    setenv("X4_DEV_PLAYER_PHYSICS", "network-frames-discard", 1);
    state.numbers[3] = 3;
    lua_stock_keyboard_replay_request(&state);
    assert(state.result == 0 && !keyboard_probe_requested);
    state.numbers[3] = 2;
    state.numbers[4] = 1;
    wire_lookahead = 1;
    lua_stock_keyboard_replay_request(&state);
    assert(state.result == 0 && !keyboard_probe_requested);
    wire_lookahead = 0;
    state.numbers[4] = 0;
    state.numbers[7] = 2;
    lua_stock_keyboard_replay_request(&state);
    assert(state.result == 0 && !keyboard_probe_requested);
    state.numbers[7] = 1;
    state.strings[6] = "0000000000000003";
    lua_stock_keyboard_replay_request(&state);
    assert(state.result == 0 && !keyboard_probe_requested);
    state.strings[6] = "0000000000000002";
    lua_stock_keyboard_replay_request(&state);
    assert(state.result == 1 && keyboard_probe_queue.config.independent_players == 1);
    lua_stock_keyboard_replay_status(&state);
    assert(strstr(state.text, "mode=independent_keyboard_probe"));
}

static void test_request_session_budget(void) {
    lua_State state;
    request_arguments(&state);
    state.numbers[7] = 1;
    setenv("X4_DEV_PLAYER_PHYSICS", "network-frames-discard", 1);
    setenv("X4_DEV_UI_SESSION_FRAMES", "18000", 1);
    state.numbers[2] = 18001;
    lua_stock_keyboard_replay_request(&state);
    assert(state.result == 0 && !keyboard_probe_requested);
    state.numbers[2] = 18002;
    state.numbers[7] = 0;
    lua_stock_keyboard_replay_request(&state);
    assert(state.result == 0 && !keyboard_probe_requested);
    state.numbers[7] = 1;
    lua_stock_keyboard_replay_request(&state);
    assert(state.result == 1 && keyboard_probe_queue.config.last_capture == 18002);
}

static void test_request_common_phases(void) {
    lua_State state;
    request_arguments(&state);
    state.numbers[7] = 1;
    state.numbers[2] = 937;
    setenv("X4_DEV_PLAYER_PHYSICS", "network-frames-discard", 1);
    setenv("X4_DEV_UI_SESSION_FRAMES", "900", 1);
    setenv("X4LAN_PLAYER_CONTROL_RECURRING", "1", 1);
    setenv("X4LAN_PLAYER_CONTROL_RECURRING_TAIL", "1", 1);
    setenv("X4LAN_PLAYER_CONTROL_RECURRING_END", "939", 1);
    setenv("X4_DEV_PLAYER_PHASES", "v1-t40", 1);

    lua_stock_keyboard_replay_request(&state);
    assert(state.result == 0 && !keyboard_probe_requested);
    state.numbers[8] = 1;
    lua_stock_keyboard_replay_request(&state);
    assert(state.result == 1);
    assert(keyboard_probe_queue.config.first_capture == 38);
    assert(keyboard_probe_queue.config.last_capture == 937);
}

static unsigned encode_foreign(unsigned char *bytes, unsigned slot, int empty) {
    stock_input_record record = {0};
    record.capture16 = 3;
    if (!empty && slot == 1) {
        record.count = 2;
        record.events[0] = (stock_input_event){STOCK_INPUT_STATE, 2, 1};
        record.events[1] = (stock_input_event){STOCK_INPUT_STATE, 2, 0};
    } else if (!empty) {
        record.count = 1;
        record.events[0] = (stock_input_event){STOCK_INPUT_STATE, 3, 1};
    }
    unsigned length = stock_input_encode(&record, bytes, STOCK_INPUT_MAX_BYTES);
    assert(length);
    return length;
}

static void prepare_independent_frame(unsigned own_slot, int empty_p2) {
    setup_fixture_mode(1, own_slot);
    assert(keyboard_replay_begin(&keyboard_probe_queue, 3) == KEYBOARD_REPLAY_OK);
    assert(keyboard_probe_after_path_join());
    if (own_slot == 1 && !omit_p1_events) {
        assert(keyboard_replay_consume(&keyboard_probe_queue, STOCK_INPUT_STATE, 2, 1) == KEYBOARD_REPLAY_OK);
        if (!omit_p1_release) {
            assert(keyboard_replay_consume(&keyboard_probe_queue, STOCK_INPUT_STATE, 2, 0) == KEYBOARD_REPLAY_OK);
        }
    } else if (own_slot == 2 && !empty_p2) {
        assert(keyboard_replay_consume(&keyboard_probe_queue, STOCK_INPUT_STATE, 3, 1) == KEYBOARD_REPLAY_OK);
    }
    unsigned char local[STOCK_INPUT_MAX_BYTES], foreign[STOCK_INPUT_MAX_BYTES];
    unsigned local_length = 0;
    assert(keyboard_replay_seal(&keyboard_probe_queue, local, sizeof local, &local_length) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_stage(&keyboard_probe_queue, own_slot, local, local_length) == KEYBOARD_REPLAY_OK);
    unsigned other_slot = own_slot == 1 ? 2 : 1;
    unsigned foreign_length = encode_foreign(foreign, other_slot, empty_p2 && other_slot == 2);
    if (!omit_foreign) {
        assert(keyboard_replay_stage(&keyboard_probe_queue, other_slot, foreign, foreign_length) == KEYBOARD_REPLAY_OK);
    }
    stats.issued = 4;
    assert(keyboard_replay_begin(&keyboard_probe_queue, 4) == KEYBOARD_REPLAY_OK);
    assert(keyboard_probe_after_path_join());
    assert(keyboard_replay_seal(&keyboard_probe_queue, local, sizeof local, &local_length) == KEYBOARD_REPLAY_OK);
    stats.issued = 5;
    assert(keyboard_replay_begin(&keyboard_probe_queue, 5) == KEYBOARD_REPLAY_OK);
    assert(keyboard_probe_after_path_join());
    assert(!handler_calls && !p2_calls); /* Due work waits for the recurring owner. */
}

static void test_independent_dispatch_slot1(void) {
    prepare_independent_frame(1, 0);
    assert(keyboard_probe_independent_after_path_join(5));
    assert(handler_calls == 2 && p2_calls == 1 && p2_tick == 5);
    assert(p2_record.count == 1 && p2_record.events[0].id == 3 && p2_record.events[0].value == 1);
    assert(keyboard_probe_independent_neutral());
    keyboard_probe_held = 1;
    assert(!keyboard_probe_independent_neutral());
}

static void test_independent_dispatch_slot2(void) {
    prepare_independent_frame(2, 0);
    assert(keyboard_probe_independent_after_path_join(5));
    assert(handler_calls == 2 && p2_calls == 1 && p2_tick == 5);
    assert(p2_record.count == 1 && p2_record.events[0].id == 3 && p2_record.events[0].value == 1);
    assert(keyboard_probe_independent_neutral());
}

static void prepare_second_independent_frame(void) {
    prepare_independent_frame(1, 0);
    assert(keyboard_probe_independent_after_path_join(5));
    unsigned char local[STOCK_INPUT_MAX_BYTES], foreign[STOCK_INPUT_MAX_BYTES];
    unsigned local_length = 0;
    /* Tick 5's local input capture is still required before advancing. */
    assert(keyboard_replay_seal(&keyboard_probe_queue, local, sizeof local, &local_length) == KEYBOARD_REPLAY_OK);
    stock_input_record record = {0};
    record.capture16 = 4;
    local_length = stock_input_encode(&record, local, sizeof local);
    assert(local_length);
    assert(keyboard_replay_stage(&keyboard_probe_queue, 1, local, local_length) == KEYBOARD_REPLAY_OK);
    record.count = 1;
    record.events[0] = (stock_input_event){STOCK_INPUT_STATE, 4, 1};
    unsigned foreign_length = stock_input_encode(&record, foreign, sizeof foreign);
    assert(foreign_length &&
           keyboard_replay_stage(&keyboard_probe_queue, 2, foreign, foreign_length) == KEYBOARD_REPLAY_OK);
    stats.issued = 6;
    assert(keyboard_replay_begin(&keyboard_probe_queue, 6) == KEYBOARD_REPLAY_OK);
    assert(keyboard_probe_after_path_join());
}

static void test_independent_second_frame(void) {
    prepare_second_independent_frame();
    assert(keyboard_probe_independent_after_path_join(6));
    assert(handler_calls == 2 && p2_calls == 2 && p2_tick == 6);
    assert(p2_record.count == 1 && p2_record.events[0].id == 4);
    assert(keyboard_probe_independent_neutral());
}

static void test_independent_second_frame_lost_hold(void) {
    prepare_second_independent_frame();
    player_target_hold_resident.target_owned = 0;
    assert(!keyboard_probe_independent_after_path_join(6));
    assert(handler_calls == 2 && p2_calls == 1);
}

static void test_independent_p1_held(void) {
    omit_p1_release = 1;
    prepare_independent_frame(1, 0);
    assert(keyboard_probe_independent_after_path_join(5));
    assert(handler_calls == 1 && p2_calls == 1 && keyboard_probe_held == 1 && sample.keys[0] == 1);
    assert(!keyboard_probe_independent_neutral());
}

static void test_independent_empty_p1(void) {
    omit_p1_events = 1;
    prepare_independent_frame(1, 0);
    assert(keyboard_probe_independent_after_path_join(5));
    assert(!handler_calls && p2_calls == 1 && p2_record.count == 1);
    assert(keyboard_probe_independent_neutral());
}

static void test_independent_empty_p2(void) {
    prepare_independent_frame(1, 1);
    assert(keyboard_probe_independent_after_path_join(5));
    assert(handler_calls == 2 && p2_calls == 1 && p2_record.count == 0);
}

static void test_independent_missing_record_poisoned(void) {
    omit_foreign = 1;
    prepare_independent_frame(1, 0);
    assert(!keyboard_probe_independent_after_path_join(5));
    assert(!handler_calls && !p2_calls && keyboard_probe_queue.failure == KEYBOARD_REPLAY_QUEUE_FAILURE);
    assert(!keyboard_probe_independent_after_path_join(5));
}

static void test_independent_bad_target(void) {
    prepare_independent_frame(1, 0);
    target_valid = 0;
    assert(!keyboard_probe_independent_after_path_join(5));
    assert(!handler_calls && !p2_calls);
    assert(!keyboard_probe_independent_after_path_join(5));
}

static void test_independent_wrong_boundary(void) {
    prepare_independent_frame(1, 0);
    physics_entered = 0;
    assert(!keyboard_probe_independent_after_path_join(5));
    assert(!handler_calls && !p2_calls);
}

static void test_independent_pins_refused(void) {
    prepare_independent_frame(1, 0);
    pins_valid = 0;
    assert(!keyboard_probe_independent_after_path_join(5));
    assert(!handler_calls && !p2_calls);
}

static void test_independent_target_pins_refused(void) {
    prepare_independent_frame(1, 0);
    target_pins_valid = 0;
    assert(!keyboard_probe_independent_after_path_join(5));
    assert(!handler_calls && !p2_calls);
}

static void test_independent_input_depth_refused(void) {
    prepare_independent_frame(1, 0);
    atomic_store(&stock_observer_input_depth, 1);
    assert(!keyboard_probe_independent_after_path_join(5));
    assert(!handler_calls && !p2_calls);
}

static void test_independent_wrapper_depth_refused(void) {
    prepare_independent_frame(1, 0);
    atomic_store(&sko_session.wrapper_depth, 1);
    assert(!keyboard_probe_independent_after_path_join(5));
    assert(!handler_calls && !p2_calls);
}

static void test_independent_wrong_tick(void) {
    prepare_independent_frame(1, 0);
    assert(!keyboard_probe_independent_after_path_join(6));
    assert(!handler_calls && !p2_calls);
}

static void test_independent_p2_return_failure(void) {
    prepare_independent_frame(1, 0);
    p2_break_return = 1;
    assert(!keyboard_probe_independent_after_path_join(5));
    assert(handler_calls == 2 && p2_calls == 1);
    assert(keyboard_probe_queue.failure == KEYBOARD_REPLAY_CALLBACK_FAILURE);
    assert(!keyboard_probe_independent_after_path_join(5) && p2_calls == 1);
}

static void test_independent_p2_stop(void) {
    prepare_independent_frame(1, 0);
    p2_stop = 1;
    assert(!keyboard_probe_independent_after_path_join(5));
    assert(handler_calls == 2 && p2_calls == 1);
    assert(keyboard_probe_queue.failure == KEYBOARD_REPLAY_CALLBACK_FAILURE);
    assert(!keyboard_probe_independent_neutral());
}

static void test_independent_strafe(void) {
    setup_fixture_mode(1, 1);
    assert(keyboard_probe_apply_event(NULL, 6, 1));
    assert(keyboard_probe_held == 16 && strafe_keys[0] == 1);
    assert(keyboard_probe_apply_event(NULL, 7, 1));
    assert(keyboard_probe_held == 48 && strafe_keys[1] == 1);
    assert(keyboard_probe_identity());
    assert(keyboard_probe_apply_event(NULL, 6, 0));
    assert(keyboard_probe_held == 32);
    assert(keyboard_probe_apply_event(NULL, 7, 0));
    assert(!keyboard_probe_held && keyboard_probe_identity());
    assert(keyboard_probe_apply_event(NULL, 13, 1));
    assert(keyboard_probe_apply_event(NULL, 14, 1));
    assert(keyboard_probe_held == 0x1800 && throttle_keys[0] && throttle_keys[1]);
    assert(keyboard_probe_apply_event(NULL, 13, 0));
    assert(keyboard_probe_apply_event(NULL, 14, 0));
    assert(!keyboard_probe_held && keyboard_probe_identity());
}
static void test_independent_vertical_strafe(void) {
    setup_fixture_mode(1, 1);
    assert(keyboard_probe_apply_event(NULL, 8, 1));
    assert(keyboard_probe_held == 64 && strafe_keys[2] == 1);
    assert(keyboard_probe_apply_event(NULL, 9, 1));
    assert(keyboard_probe_held == 192 && strafe_keys[3] == 1);
    assert(keyboard_probe_identity());
    assert(keyboard_probe_apply_event(NULL, 8, 0));
    assert(keyboard_probe_held == 128);
    assert(keyboard_probe_apply_event(NULL, 9, 0));
    assert(!keyboard_probe_held && keyboard_probe_identity());
}
static void test_vertical_strafe_bad_readback(void) {
    setup_fixture_mode(1, 1); mock_updates_bank=0;
    assert(!keyboard_probe_apply_event(NULL, 9, 1));
    assert(handler_calls == 1 && keyboard_probe_inflight);
}
static void test_vertical_strafe_suppressed(void) {
    setup_fixture_mode(1, 1); strafe_ignored=1;
    assert(!keyboard_probe_apply_event(NULL, 8, 1));
    assert(!handler_calls && !keyboard_probe_inflight);
}
static void test_default_vertical_refused(void) {
    setup_fixture(); assert(!keyboard_probe_apply_event(NULL, 8, 1)); assert(!handler_calls);
}
static void test_independent_strafe_suppressed(void) {
    setup_fixture_mode(1, 1); strafe_ignored=1;
    assert(!keyboard_probe_apply_event(NULL, 6, 1));
    assert(!handler_calls && !keyboard_probe_inflight);
}
static void test_independent_strafe_bad_readback(void) {
    setup_fixture_mode(1, 1); mock_updates_bank=0;
    assert(!keyboard_probe_apply_event(NULL, 7, 1));
    assert(handler_calls == 1 && keyboard_probe_inflight);
}
static void test_independent_strafe_unreadable(void) {
    setup_fixture_mode(1, 1); strafe_unreadable=1;
    assert(!keyboard_probe_apply_event(NULL, 6, 1));
    assert(!handler_calls);
}
static void test_default_strafe_refused(void) {
    setup_fixture(); assert(!keyboard_probe_apply_event(NULL, 6, 1)); assert(!handler_calls);
}
static void test_independent_strafe_invalid(void) {
    setup_fixture_mode(1, 1); assert(!keyboard_probe_apply_event(NULL, 10, 1)); assert(!handler_calls);
}
static void test_independent_strafe_value(void) {
    setup_fixture_mode(1, 1); assert(!keyboard_probe_apply_event(NULL, 6, 2)); assert(!handler_calls);
}

int main(int argc, char **argv) {
    struct test_case {
        const char *name;
        void (*run)(void);
    };
    static const struct test_case cases[] = {
        {"independent_vertical", test_independent_vertical_strafe},
        {"vertical_bad_readback", test_vertical_strafe_bad_readback},
        {"vertical_suppressed", test_vertical_strafe_suppressed},
        {"default_vertical_refused", test_default_vertical_refused},
        {"independent_strafe", test_independent_strafe},
        {"strafe_suppressed", test_independent_strafe_suppressed},
        {"strafe_bad_readback", test_independent_strafe_bad_readback},
        {"strafe_unreadable", test_independent_strafe_unreadable},
        {"default_strafe_refused", test_default_strafe_refused},
        {"strafe_invalid", test_independent_strafe_invalid},
        {"strafe_value", test_independent_strafe_value},
        {"default", test_default_off},
        {"inflight", test_inflight_stop},
        {"held", test_held_stop},
        {"badowner", test_wrong_owner},
        {"initial", test_initial_key},
        {"wrongphase", test_wrong_apply_phase},
        {"capture", test_wrong_capture_phase},
        {"stage", test_wrong_stage_phase},
        {"press_release", test_press_release_readback},
        {"bad_readback", test_bad_readback},
        {"request_default", test_request_default},
        {"request_independent", test_request_independent_mode},
        {"request_session", test_request_session_budget},
        {"request_common_phases", test_request_common_phases},
        {"independent_slot1", test_independent_dispatch_slot1},
        {"independent_slot2", test_independent_dispatch_slot2},
        {"independent_empty", test_independent_empty_p2},
        {"independent_empty_p1", test_independent_empty_p1},
        {"independent_p1_held", test_independent_p1_held},
        {"independent_frame6", test_independent_second_frame},
        {"independent_frame6_lost_hold", test_independent_second_frame_lost_hold},
        {"independent_bad_target", test_independent_bad_target},
        {"independent_missing", test_independent_missing_record_poisoned},
        {"independent_boundary", test_independent_wrong_boundary},
        {"independent_pins", test_independent_pins_refused},
        {"independent_target_pins", test_independent_target_pins_refused},
        {"independent_input_depth", test_independent_input_depth_refused},
        {"independent_wrapper_depth", test_independent_wrapper_depth_refused},
        {"independent_wrong_tick", test_independent_wrong_tick},
        {"independent_p2_failure", test_independent_p2_return_failure},
        {"independent_p2_stop", test_independent_p2_stop},
    };
    assert(argc == 2);
    if (!strncmp(argv[1], "binding_", 8)) {
        test_changed_binding(argv[1] + 8);
        return 0;
    }
    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        if (!strcmp(argv[1], cases[index].name)) {
            cases[index].run();
            return 0;
        }
    }
    return 2;
}
