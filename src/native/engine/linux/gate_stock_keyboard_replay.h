/* Default-off keyboard replay experiment. Canonical mode retains the original
 * single-ship behavior; independent mode is restricted to the disposable
 * network physics experiment. Neither mode is the multi-player Linux port. The existing observer owns the selected handler
 * pointers; this adapter consumes those events only when explicitly requested.
 * All engine effects happen on the verified worker, after the path join.
 * Held controls are an outstanding cleanup obligation if a test stops early.
 */
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include "stock_keyboard_replay.h"
#include "player_stock_view_wire_handoff.h"
#include "player_ui_session_budget.h"
#include "gate_player_startup_phases.h"

static int player_cursor_mode(void);
static int player_space_mode(void);
static int player_space_collect_identity(stock_keyboard_identity_sample *sample, unsigned local, unsigned delayed);
static int player_space_request(unsigned independent);
static int player_space_admit(uint64_t generation);
static int player_space_local_state(unsigned *active);
static unsigned player_space_slot_state(unsigned slot);
static int player_space_apply(unsigned slot, uint64_t generation, unsigned active);
static int player_cursor_request(unsigned independent);
static int player_cursor_capture_only(uint64_t generation);
static int player_input_prepare_slots(void *context, uint64_t generation, const uint64_t saved[2],
                                      const stock_input_record records[2]);
static int player_cursor_capture_sample(uint64_t generation, float output[3]);
static int player_cursor_tail(uint64_t generation, unsigned final);
static keyboard_replay keyboard_probe_queue;
static player_stock_view_wire_handoff keyboard_view_wire;
static int player_stock_view_wire_request(void);
static int player_stock_view_wire_read_sealed(uint64_t generation, unsigned slot,
                                             player_stock_view_wire_sealed *out);
static unsigned keyboard_probe_requested;
static unsigned keyboard_probe_closed;
static unsigned keyboard_probe_held;
static unsigned keyboard_probe_inflight;
static uint64_t keyboard_probe_consumed;
static uint64_t keyboard_probe_replayed;
static uint64_t keyboard_probe_seals;
static uint64_t keyboard_probe_staged;
static stock_keyboard_identity_sample keyboard_probe_binding;
static const char *keyboard_probe_problem = "off";

static int keyboard_probe_fail(const char *reason) {
    if (!strcmp(keyboard_probe_problem, "off") || !strcmp(keyboard_probe_problem, "active")) {
        keyboard_probe_problem = reason;
    }
    return 0;
}

static int keyboard_probe_worker(void) {
    return keyboard_probe_requested && !keyboard_probe_closed && stats.active &&
        boot_phase == 3 && GetCurrentThreadId() == stock_observer_worker_thread &&
        ui_thread == stock_observer_worker_thread && gate_tls_worker(gate_tls_self()) &&
        atomic_load(&sko_session.active) && !stock_observer_thread_failure;
}

/* Fresh native getters establish current identity. Stored addresses are only
 * comparison values; they are never used as read or call destinations. */
static int keyboard_probe_identity(void) {
    stock_keyboard_identity_sample current = {0};
    unsigned local = 1, delayed = player_space_slot_state(1);
    if (player_space_mode() && !player_space_local_state(&local)) return keyboard_probe_fail("space_local_state");
    int readable = player_space_mode() ? player_space_collect_identity(&current, local, delayed) :
        (player_cursor_mode() ? stock_keyboard_identity_collect_cursor(&current) : stock_keyboard_identity_collect(&current));
    if (!readable || !current.ready) {
        return keyboard_probe_fail(current.reason ? current.reason : "identity_unreadable");
    }
    if (current.saved != keyboard_probe_queue.config.saved_ship_ids[0]) {
        return keyboard_probe_fail("canonical_ship_id");
    }
    for (unsigned index = 0; index < 4; ++index) {
        if (current.keys[index] != ((keyboard_probe_held >> index) & 1u)) {
            return keyboard_probe_fail("native_key_bank_mismatch");
        }
    }
    if (keyboard_probe_queue.config.independent_players) {
        /* Native STATE 6..9 keys: horizontal 0x72/73, vertical 0x74/75. */
        unsigned char strafe_keys[4], strafe_ignored;
        if (!read_local((const void *)LX(0x3d8da72), strafe_keys, sizeof strafe_keys) ||
            !read_local((const void *)LX(0x3e5b299), &strafe_ignored, 1) || strafe_ignored) {
            return keyboard_probe_fail("native_strafe_suppressed_or_unreadable");
        }
        unsigned char throttle_keys[2], throttle_ignored;
        if (!read_local((const void *)LX(0x3d8da7a), throttle_keys, sizeof throttle_keys) ||
            !read_local((const void *)LX(0x3e5b298), &throttle_ignored, 1) || throttle_ignored) {
            return keyboard_probe_fail("native_throttle_suppressed_or_unreadable");
        }
        for (unsigned index = 0; index < 2; ++index) {
            if (throttle_keys[index] != ((keyboard_probe_held >> (index + 11)) & 1u)) {
                return keyboard_probe_fail("native_throttle_bank_mismatch");
            }
        }
        for (unsigned index = 0; index < 4; ++index) {
            if (strafe_keys[index] != ((keyboard_probe_held >> (index + 4)) & 1u)) {
                return keyboard_probe_fail("native_strafe_bank_mismatch");
            }
        }
    }
    if (!keyboard_probe_binding.player) {
        keyboard_probe_binding = current;
    } else if (current.universe != keyboard_probe_binding.universe ||
               current.input_owner != keyboard_probe_binding.input_owner ||
               current.player != keyboard_probe_binding.player ||
               current.player_runtime != keyboard_probe_binding.player_runtime ||
               current.player_saved != keyboard_probe_binding.player_saved ||
               current.controller != keyboard_probe_binding.controller ||
               current.ship != keyboard_probe_binding.ship ||
               current.runtime != keyboard_probe_binding.runtime) {
        return keyboard_probe_fail("canonical_binding_changed");
    }
    return 1;
}

static int keyboard_probe_consume(void *context, const sko_event *event) {
    (void)context;
    /* Never restore a local effect because capture failed. The next boundary
     * stops the experiment; a late wrapper remains safe and resident. */
    if (!keyboard_probe_worker() || strcmp(keyboard_probe_problem, "active")) {
        keyboard_probe_fail("capture_worker");
        return 1;
    }
    if (atomic_load(&stock_observer_input_depth) != 1 ||
        event->thread != stock_observer_worker_thread || event->kind != STOCK_INPUT_STATE ||
        event->caller != (uintptr_t)LX(0x1051074u) || keyboard_probe_queue.tick != stats.issued) {
        keyboard_probe_fail("capture_boundary");
        return 1;
    }
    ++keyboard_probe_consumed;
    if (keyboard_replay_consume(&keyboard_probe_queue, event->kind, (unsigned)event->id,
                               event->state) != KEYBOARD_REPLAY_OK) {
        keyboard_probe_fail("capture_event");
    }
    return 1;
}

/* Main admission has joined the previous native worker before opening capture.
 * No engine getter or control handler is called from the main thread. */
static int keyboard_probe_admit(void) {
    if (!keyboard_probe_requested) {
        return 1;
    }
    if (keyboard_probe_closed || strcmp(keyboard_probe_problem, "active") ||
        !stock_observer_main_boundary || GetCurrentThreadId() != main_thread ||
        !stock_observer_install_tick || !atomic_load(&sko_session.active)) {
        return keyboard_probe_fail("admission");
    }
    sko_session.consume_event = keyboard_probe_consume;
    if (stats.issued < keyboard_probe_queue.config.first_capture) {
        return 1;
    }
    if (keyboard_replay_begin(&keyboard_probe_queue, stats.issued) != KEYBOARD_REPLAY_OK)
        return keyboard_probe_fail("open_capture_tick");
    return player_space_admit(stats.issued) || keyboard_probe_fail("space_admission");
}

static int keyboard_probe_apply_event(void *context, unsigned id, unsigned char state) {
    (void)context;
    if (!keyboard_probe_worker() || atomic_load(&stock_observer_input_depth) ||
        atomic_load(&sko_session.wrapper_depth) ||
        !keyboard_replay_allowed_state(&keyboard_probe_queue.config, id) || state > 1) {
        return keyboard_probe_fail("apply_boundary");
    }
    if (keyboard_probe_queue.config.independent_players && !keyboard_probe_identity()) {
        return 0;
    }
    if (id == 13 || id == 14) {
        player_throttle_require_pins();
    }
    /* Record the unresolved effect before entering engine code: a nested STOP
     * must not declare a neutral bank while the handler is still executing. */
    keyboard_probe_inflight = 1;
    ((void (*)(int, unsigned char))LX(SKO_STATE_VA))((int)id, state);
    if (!keyboard_probe_worker() || strcmp(keyboard_probe_problem, "active")) {
        return keyboard_probe_fail("apply_interrupted");
    }
    unsigned bit = 1u << (id - 2);
    if (state) {
        keyboard_probe_held |= bit;
    } else {
        keyboard_probe_held &= ~bit;
    }
    if (!keyboard_probe_identity()) {
        return 0;
    }
    keyboard_probe_inflight = 0;
    ++keyboard_probe_replayed;
    return 1;
}

/* The disposable physics owner implements this after the keyboard adapter is
 * declared. It must terminate on any P2 scope or update failure and return only
 * after canonical state has been restored. The immutable record is valid for
 * this call only, including when it contains no events. */
static void player_physics_network_apply_p2(uint64_t generation, const stock_input_record *record,
                                          const keyboard_replay *replay, unsigned view_admitted);

typedef struct {
    uintptr_t target;
    uint64_t runtime;
} keyboard_probe_independent_target;

/* Retain the exact validated P1 record and the key mask on both sides of its
 * effects. A failed or partial write makes the disposable publication fail. */
static int keyboard_probe_log_slot1(uint64_t generation, uint64_t saved_ship,
                                    const stock_input_record *record, unsigned held_before) {
    int caller_errno = errno;
    unsigned char bytes[STOCK_INPUT_MAX_BYTES];
    unsigned length = stock_input_encode(record, bytes, sizeof bytes);
    if (!length) {
        return keyboard_probe_fail("independent_record_encode");
    }
    static const char digits[] = "0123456789abcdef";
    char encoded[STOCK_INPUT_MAX_BYTES * 2 + 1];
    for (unsigned index = 0; index < length; ++index) {
        encoded[2 * index] = digits[bytes[index] >> 4];
        encoded[2 * index + 1] = digits[bytes[index] & 15];
    }
    encoded[2 * length] = 0;
    char message[768];
    int size = snprintf(message, sizeof message,
        "X4LAN KEYBOARD_SLOT1 tick=%llu capture=%u saved=%016llx record=%s held_before=%u held_after=%u\n",
        (unsigned long long)generation, (unsigned)record->capture16, (unsigned long long)saved_ship,
        encoded, held_before, keyboard_probe_held);
    if (size <= 0 || (size_t)size >= sizeof message) {
        return keyboard_probe_fail("independent_record_format");
    }
    size_t written = 0;
    while (written < (size_t)size) {
        ssize_t count = write(STDERR_FILENO, message + written, (size_t)size - written);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            return keyboard_probe_fail("independent_record_write");
        }
        written += (size_t)count;
    }
    errno = caller_errno;
    return 1;
}

static int keyboard_probe_apply_slot(void *context, uint64_t generation, unsigned slot,
                                     uint64_t saved_ship, const stock_input_record *record) {
    const keyboard_probe_independent_target *expected = context;
    if (!expected || !record || generation != stats.issued || slot < 1 || slot > 2 ||
        saved_ship != keyboard_probe_queue.config.saved_ship_ids[slot - 1] ||
        !keyboard_probe_worker() || atomic_load(&stock_observer_input_depth) ||
        atomic_load(&sko_session.wrapper_depth) || !keyboard_probe_identity()) {
        return keyboard_probe_fail("independent_slot_identity");
    }
    if (slot == 1) {
        unsigned held_before = keyboard_probe_held;
        for (unsigned index = 0; index < record->count; ++index) {
            const stock_input_event *event = &record->events[index];
            if (keyboard_replay_is_space_event(&keyboard_probe_queue.config, event)) {
                if (!player_space_apply(1, generation, event->value)) return keyboard_probe_fail("space_p1_effect");
                continue;
            }
            if (!keyboard_probe_apply_event(NULL, event->id, (unsigned char)event->value)) {
                return 0;
            }
        }
        return keyboard_probe_log_slot1(generation, saved_ship, record, held_before);
    }
    /* The retained target is resolved through the sealed import map; matching
     * the configured descriptor alone does not establish the target's identity. */
    uintptr_t target = 0;
    uint64_t runtime = 0;
    if (saved_ship != SEAT_PREFLIGHT_TARGET_SAVED ||
        !player_seat_preflight_resolve(NULL, saved_ship, &target, &runtime) ||
        target != expected->target || runtime != expected->runtime ||
        (generation != player_startup()->take && (!player_physics_identity() || !player_target_hold_resident.target_owned ||
            player_target_hold_resident.identity.target != target ||
            player_target_hold_resident.identity.target_runtime != runtime))) {
        return keyboard_probe_fail("independent_target_identity");
    }
    player_physics_network_apply_p2(generation, record, &keyboard_probe_queue, keyboard_view_wire.admitted);
    if (!keyboard_probe_worker() || strcmp(keyboard_probe_problem, "active") ||
        atomic_load(&stock_observer_input_depth) || atomic_load(&sko_session.wrapper_depth) ||
        !player_physics_identity() || !player_target_hold_resident.target_owned ||
        player_target_hold_resident.identity.target != target ||
        player_target_hold_resident.identity.target_runtime != runtime || !keyboard_probe_identity()) {
        return keyboard_probe_fail("independent_scope_return");
    }
    return 1;
}

/* Check the admitted VIEW route after its transaction has committed and
 * before either player's first native effect. The first check therefore sees
 * the initial bind; later checks also cover the delayed drain frames. */
static int keyboard_probe_prepare_independent_slots(void *context, uint64_t generation,
                                                    const uint64_t saved[2],
                                                    const stock_input_record records[2]) {
    if (!player_input_prepare_slots(context, generation, saved, records)) return 0;
    if (!keyboard_view_wire.admitted) return !keyboard_probe_queue.config.view_capture;
    return keyboard_probe_queue.config.view_capture == 1 && keyboard_view_wire.ready &&
        keyboard_view_wire.ready(keyboard_view_wire.context, &keyboard_probe_queue.config,
                                 generation) == 1;
}

/* Called by the recurring physics owner only, after its worker/main boundary is
 * authenticated. Each successful due frame applies P1 events once and delegates
 * exactly one P2 Update, including for an empty P2 record. */
static int keyboard_probe_independent_after_path_join(uint64_t generation) {
    if (!keyboard_probe_requested || !keyboard_probe_queue.config.independent_players ||
        generation != stats.issued || generation < keyboard_probe_queue.config.first_capture +
                                               keyboard_probe_queue.config.delay ||
        !player_recurring_physics_entered() || !keyboard_probe_worker() ||
        strcmp(keyboard_probe_problem, "active") || atomic_load(&stock_observer_input_depth) ||
        atomic_load(&sko_session.wrapper_depth) || !stock_observer_slot_valid() || !sko_validate(1) ||
        !keyboard_probe_identity() || (generation != player_startup()->take && !player_physics_identity())) {
        return keyboard_probe_fail("independent_replay_boundary");
    }
    keyboard_probe_independent_target target = {0};
    if (keyboard_probe_queue.config.saved_ship_ids[1] != SEAT_PREFLIGHT_TARGET_SAVED ||
        !player_target_hold_pins() || !player_seat_preflight_resolve(NULL, SEAT_PREFLIGHT_TARGET_SAVED, &target.target, &target.runtime) ||
        !target.target || !target.runtime ||
        (generation != player_startup()->take && (!player_target_hold_resident.target_owned ||
            player_target_hold_resident.identity.target != target.target ||
            player_target_hold_resident.identity.target_runtime != target.runtime))) {
        return keyboard_probe_fail("independent_target_admission");
    }
    if (player_space_mode()) {
        for (unsigned slot = 1; slot <= 2; ++slot) {
            unsigned queued = 2;
            if (!keyboard_replay_space_slot_state(&keyboard_probe_queue, slot, &queued) ||
                queued != player_space_slot_state(slot)) return keyboard_probe_fail("space_slot_prestate");
        }
    }
    keyboard_replay_result result = keyboard_replay_publish_slots_prepared(
        &keyboard_probe_queue, generation, keyboard_probe_prepare_independent_slots, keyboard_probe_apply_slot, &target);
    if (result != KEYBOARD_REPLAY_OK) {
        keyboard_replay_latch(&keyboard_probe_queue,
                              result < 0 ? result : KEYBOARD_REPLAY_QUEUE_FAILURE);
        return keyboard_probe_fail("independent_frame_not_ready");
    }
    return 1;
}

/* The final discard is valid only after real synchronized releases leave the
 * canonical P1 key bank neutral. This does not release or alter any input. */
static int keyboard_probe_independent_neutral(void) {
    keyboard_replay_status status = {0};
    return keyboard_probe_requested && !keyboard_probe_closed &&
        keyboard_probe_queue.config.independent_players == 1 && keyboard_probe_worker() &&
        !strcmp(keyboard_probe_problem, "active") && !keyboard_probe_held && !keyboard_probe_inflight &&
        !atomic_load(&stock_observer_input_depth) && !atomic_load(&sko_session.wrapper_depth) &&
        keyboard_replay_get_status(&keyboard_probe_queue, &status) && status.configured && !status.stopped &&
        !status.failure && status.phase != KEYBOARD_REPLAY_APPLYING && keyboard_probe_identity();
}

/* Called only after the existing path semaphore was joined and restored, and
 * before returning to MoveWorker's current VBI input/controller passes. */
static int keyboard_probe_after_path_join(void) {
    if (!keyboard_probe_requested || stats.issued < keyboard_probe_queue.config.first_capture) {
        return 1;
    }
    if (!keyboard_probe_worker() || strcmp(keyboard_probe_problem, "active") ||
        atomic_load(&stock_observer_input_depth) || atomic_load(&sko_session.wrapper_depth) ||
        !stock_observer_slot_valid() || !sko_validate(1) || !keyboard_probe_identity()) {
        return keyboard_probe_fail("replay_boundary");
    }
    if (keyboard_probe_queue.config.independent_players) {
        if (stats.issued >= keyboard_probe_queue.config.first_capture + keyboard_probe_queue.config.delay) {
            return 1; /* The recurring owner must publish at its authenticated boundary. */
        }
        keyboard_replay_result result = keyboard_replay_publish_slots(
            &keyboard_probe_queue, stats.issued, NULL, NULL);
        if (result != KEYBOARD_REPLAY_NOT_DUE) return keyboard_probe_fail("independent_open_capture");
        return player_cursor_capture_only(stats.issued) || keyboard_probe_fail("cursor_preapply_stage");
    }
    keyboard_replay_result result = keyboard_replay_publish(
        &keyboard_probe_queue, stats.issued, keyboard_probe_apply_event, NULL);
    return result == KEYBOARD_REPLAY_OK || result == KEYBOARD_REPLAY_NOT_DUE ||
        keyboard_probe_fail("frame_not_ready");
}

static int keyboard_probe_stop(void) {
    if (!keyboard_probe_requested) {
        return 1;
    }
    keyboard_probe_closed = 1;
    keyboard_replay_stop(&keyboard_probe_queue);
    /* This module must not fabricate a synchronized release after a failed
     * session. Test windows must include the real releases and their delay. */
    return !keyboard_probe_held && !keyboard_probe_inflight;
}

static int keyboard_probe_hex_id(const char *text, size_t length, uint64_t *value) {
    if (!text || length != 16) {
        return 0;
    }
    uint64_t decoded = 0;
    for (size_t index = 0; index < length; ++index) {
        unsigned digit = text[index] >= '0' && text[index] <= '9' ? (unsigned)(text[index] - '0')
            : text[index] >= 'a' && text[index] <= 'f' ? (unsigned)(text[index] - 'a' + 10) : 16;
        if (digit == 16) {
            return 0;
        }
        decoded = (decoded << 4) | digit;
    }
    *value = decoded;
    return decoded != 0;
}

static int lua_stock_keyboard_replay_request(lua_State *L) {
    double slot = p_lua_tonumber(L, 1);
    double last = p_lua_tonumber(L, 2);
    double delay = p_lua_tonumber(L, 3);
    double ahead = p_lua_tonumber(L, 4);
    double independent = p_lua_tonumber(L, 7);
    double phase_version = p_lua_tonumber(L, 8);
    const char *physics_mode = getenv("X4_DEV_PLAYER_PHYSICS");
    size_t first_length = 0;
    size_t second_length = 0;
    const char *first = p_lua_tolstring(L, 5, &first_length);
    const char *second = p_lua_tolstring(L, 6, &second_length);
    keyboard_replay_config config = {0};
    const char *session_text = getenv("X4_DEV_UI_SESSION_FRAMES");
    player_ui_session_budget requested_session = {0};
    int session_valid = !session_text || player_ui_session_parse_phased(session_text, player_startup(), &requested_session);
    int valid = session_valid && !keyboard_probe_requested && !keyboard_probe_queue.initialized &&
        stock_observer_wanted() && accept_ui() && wire_version == 3 && wire_clients == 2 &&
        slot == stats.slot && ahead == wire_lookahead && path_installed &&
        stats.active && stats.issued == 1 && gate_tls_worker(gate_tls_self()) &&
        GetCurrentThreadId() == stock_observer_worker_thread &&
        (slot == 1 || slot == 2) && (independent == 0 || independent == 1) &&
        phase_version == (independent ? player_startup()->version : 0) &&
        (!independent || (physics_mode && !strcmp(physics_mode, "network-frames-discard") &&
                          delay == 2 && ahead == 0)) &&
        (!session_text || (independent && last == requested_session.last_capture)) && isfinite(last) && floor(last) == last && last >= 3 &&
        last < LINUX_STOCK_TICK_MAX - 30 && isfinite(delay) && floor(delay) == delay &&
        delay >= 2 && delay <= 30 && isfinite(ahead) && floor(ahead) == ahead && ahead >= 0 && ahead <= 28 &&
        keyboard_probe_hex_id(first, first_length, &config.saved_ship_ids[0]) &&
        keyboard_probe_hex_id(second, second_length, &config.saved_ship_ids[1]) &&
        (!independent || config.saved_ship_ids[1] == SEAT_PREFLIGHT_TARGET_SAVED);
    if (valid) valid = player_cursor_request((unsigned)independent);
    if (valid) valid = player_space_request((unsigned)independent);
    if (valid) valid = player_stock_view_wire_request();
    if (valid) {
        config.own_slot = (unsigned)slot;
        config.independent_players = (unsigned)independent;
        config.cursor_active_only = (unsigned)player_cursor_mode();
        config.space_context8 = (unsigned)player_space_mode();
        config.scripted_throttle = independent && player_throttle_enabled();
        config.first_capture = independent ? player_startup()->capture : 3;
        config.last_capture = (uint64_t)last;
        config.delay = (unsigned)delay;
        config.lookahead = (unsigned)ahead;
        valid = player_stock_view_wire_configure(&keyboard_view_wire, &config,
            requested_session.frames ? player_startup()->presentation_first + 6u : 0,
            requested_session.presentation_last) && keyboard_replay_init(&keyboard_probe_queue) &&
            keyboard_replay_configure(&keyboard_probe_queue, &config) == KEYBOARD_REPLAY_OK;
    }
    if (valid) valid = player_view_host_capture_start() && player_view_render_capture_start();
    if (valid) {
        keyboard_probe_requested = 1;
        keyboard_probe_problem = "active";
    }
    p_lua_pushnumber(L, valid);
    return 1;
}

static int lua_stock_keyboard_replay_capture(lua_State *L) {
    if (!keyboard_probe_requested || stats.issued < keyboard_probe_queue.config.first_capture ||
        stats.issued > keyboard_probe_queue.config.last_capture) {
        p_lua_pushnil(L);
        return 1;
    }
    unsigned char bytes[STOCK_INPUT_MAX_BYTES];
    unsigned length = 0;
    if (!keyboard_probe_worker() || !stock_observer_worker_boundary() ||
        strcmp(keyboard_probe_problem, "active") || !keyboard_probe_identity()) {
        keyboard_probe_fail("capture_boundary");
        p_lua_pushnil(L);
        return 1;
    }
    float cursor[3];
    const float *sample = NULL;
    if (player_cursor_mode()) {
        if (!player_cursor_capture_sample(stats.issued, cursor)) {
            keyboard_probe_fail("cursor_sample_missing");
            p_lua_pushnil(L);
            return 1;
        }
        unsigned active = 1;
        if (player_space_mode() && !player_space_local_state(&active)) {
            keyboard_probe_fail("space_capture_state"); p_lua_pushnil(L); return 1;
        }
        if (active) sample = cursor;
    }
    if (!player_view_host_capture_seal(stats.issued) || !player_view_render_capture_seal(stats.issued)) {
        keyboard_probe_fail("view_capture_seal"); p_lua_pushnil(L); return 1;
    }
    linux_stock_view_sample view;
    const linux_stock_view_sample *presentation = NULL;
    if (keyboard_probe_queue.config.view_capture) {
        player_stock_view_wire_sealed sealed;
        unsigned present = 0;
        if (!player_stock_view_wire_read_sealed(stats.issued, stats.slot, &sealed) ||
            !player_stock_view_wire_sample(&keyboard_view_wire, &keyboard_probe_queue,
                stats.issued, &sealed, &view, &present)) {
            keyboard_probe_fail("view_wire_sample"); p_lua_pushnil(L); return 1;
        }
        if (present) presentation = &view;
    }
    if (keyboard_replay_seal_presentation(&keyboard_probe_queue, sample, presentation,
            bytes, sizeof(bytes), &length) != KEYBOARD_REPLAY_OK) {
        keyboard_probe_fail("capture_seal");
        p_lua_pushnil(L);
        return 1;
    }
    char text[STOCK_INPUT_MAX_BYTES * 2 + 1];
    static const char digits[] = "0123456789abcdef";
    for (unsigned index = 0; index < length; ++index) {
        text[index * 2] = digits[bytes[index] >> 4];
        text[index * 2 + 1] = digits[bytes[index] & 15];
    }
    ++keyboard_probe_seals;
    p_lua_pushlstring(L, text, length * 2);
    return 1;
}

static int lua_stock_keyboard_replay_stage(lua_State *L) {
    size_t length = 0;
    const char *text = p_lua_tolstring(L, 2, &length);
    double slot = p_lua_tonumber(L, 1);
    unsigned char bytes[STOCK_INPUT_MAX_BYTES];
    int valid = keyboard_probe_worker() && stock_observer_worker_boundary() &&
        !strcmp(keyboard_probe_problem, "active") && (slot == 1 || slot == 2) && text &&
        length >= STOCK_INPUT_HEADER_BYTES * 2 && length <= sizeof(bytes) * 2 && !(length & 1);
    for (size_t index = 0; valid && index < length; ++index) {
        unsigned digit = text[index] >= '0' && text[index] <= '9' ? (unsigned)(text[index] - '0')
            : text[index] >= 'a' && text[index] <= 'f' ? (unsigned)(text[index] - 'a' + 10) : 16;
        if (digit == 16) {
            valid = 0;
        } else if (!(index & 1)) {
            bytes[index / 2] = (unsigned char)(digit << 4);
        } else {
            bytes[index / 2] |= (unsigned char)digit;
        }
    }
    if (valid) {
        valid = keyboard_replay_stage(&keyboard_probe_queue, (unsigned)slot, bytes, length / 2) == KEYBOARD_REPLAY_OK;
    }
    if (valid) {
        ++keyboard_probe_staged;
    } else if (keyboard_probe_requested) {
        keyboard_probe_fail("stage_record");
    }
    p_lua_pushnumber(L, valid);
    return 1;
}

static int lua_stock_keyboard_replay_status(lua_State *L) {
    if (!accept_ui()) {
        p_lua_pushnil(L);
        return 1;
    }
    char text[512];
    int length = snprintf(text, sizeof(text),
        "mode=%s requested=%u closed=%u tick=%llu applied=%llu "
        "consumed=%llu replayed=%llu seals=%llu staged=%llu held=%u inflight=%u phase=%u failure=%d queue=%d problem=%s",
        keyboard_probe_queue.config.independent_players ? "independent_keyboard_probe" : "canonical_keyboard_probe",
        keyboard_probe_requested, keyboard_probe_closed, (unsigned long long)keyboard_probe_queue.tick,
        (unsigned long long)keyboard_probe_queue.last_applied_tick,
        (unsigned long long)keyboard_probe_consumed, (unsigned long long)keyboard_probe_replayed,
        (unsigned long long)keyboard_probe_seals, (unsigned long long)keyboard_probe_staged,
        keyboard_probe_held, keyboard_probe_inflight, keyboard_probe_queue.phase, keyboard_probe_queue.failure,
        keyboard_probe_queue.queue_result, keyboard_probe_problem);
    if (length < 0 || (size_t)length >= sizeof(text)) {
        p_lua_pushnil(L);
    } else {
        p_lua_pushlstring(L, text, (size_t)length);
    }
    return 1;
}
