#ifndef X4_GATE_PLAYER_REFERENCE_PROBE_H
#define X4_GATE_PLAYER_REFERENCE_PROBE_H

/* Include after gate_stock_keyboard_identity.h. Root wiring must run this only
 * inside the existing verified post-path-join worker scope, before its release.
 * verify_image must already have accepted the pinned ELF; helper hashes and
 * relocated reference vtables are checked again below.
 *
 * A post-join/depth check alone does not exclude arbitrary reference owners.
 * The embedding call site must establish stable canonical P1 lifetime and no
 * singleton/reference writer across this entire synchronous operation. The
 * native-call enable is default false: leave it false until that narrower
 * caller contract is justified. Read-only sampling does not grant permission.
 * Native reference operations are atomic, but exact count validation is not an
 * execution lock and does not prove exclusion. No signal/exception recovery is
 * invented here: a call that does not return retains its pre-call obligation.
 * Code, callback state and probe storage must remain resident after failure.
 */
#include "player_reference_probe.h"

_Static_assert(sizeof(stock_keyboard_identity_sample) <= P1_REFERENCE_IDENTITY_MAX,
               "the complete canonical identity sample must fit the reference probe");

static p1_reference_probe player_reference_probe_state;
static unsigned player_reference_native_calls_enabled;
static unsigned player_reference_observed;
static unsigned player_reference_inflight;
static uint32_t player_reference_initial_marker;
static unsigned player_reference_acquire_calls;
static unsigned player_reference_release_calls;
static unsigned player_reference_preconditions;
static uint64_t player_reference_run_tick;

static int player_reference_boundary(void *context) {
    (void)context;
    unsigned native_flags = 2;
    unsigned char loading = 1;
    sko_status observer = sko_get_status();
    return player_reference_inflight && stock_observer_post_join_boundary &&
        stats.active && stats.issued >= 2 && stats.issued == player_reference_run_tick && boot_phase == 3 && path_installed &&
        stock_observer_worker_thread && GetCurrentThreadId() == stock_observer_worker_thread &&
        GetCurrentThreadId() != main_thread && ui_thread == stock_observer_worker_thread &&
        gate_tls_worker(gate_tls_self()) && atomic_load(&stock_observer_requested) &&
        stock_observer_install_tick && observer.active && observer.owned == (1u << SKO_SELECTED) - 1 &&
        !observer.uncertain && !observer.pending_readback && !observer.wrapper_depth &&
        !observer.overflow && !atomic_load(&stock_observer_input_depth) &&
        !atomic_load(&stock_observer_thread_failure) && !stock_observer_failed &&
        stock_observer_slot_valid() &&
        read_local(LX(STOCK_OBSERVER_NATIVE_FLAGS), &native_flags, sizeof(native_flags)) &&
        !(native_flags & 2) && read_local(LX(0x3daab00u), &loading, sizeof(loading)) && !loading;
}

static int player_reference_pins(void *context, const p1_reference_pin *pins, unsigned count) {
    (void)context;
    if (count != sizeof(p1_reference_pins) / sizeof(p1_reference_pins[0])) {
        return 0;
    }
    for (unsigned pin_index = 0; pin_index < count; ++pin_index) {
        lx_site pin = {"P1-reference-helper", pins[pin_index].va,
                       pins[pin_index].length, pins[pin_index].sha256};
        if (!site_ok(&pin)) {
            return 0;
        }
    }
    uintptr_t zero_target = 0;
    return read_local(LX(P1_REFERENCE_SUBOBJECT_VTABLE_VA), &zero_target, sizeof(zero_target)) &&
        zero_target == (uintptr_t)LX(P1_REFERENCE_ZERO_TARGET_VA);
}

static int player_reference_sample(void *context, p1_reference_identity *identity) {
    (void)context;
    stock_keyboard_identity_sample current;
    memset(&current, 0, sizeof(current));
    if (!player_reference_boundary(NULL) || !stock_keyboard_identity_collect(&current) || !current.ready) {
        return 0;
    }
    /* The collector itself clears the complete struct before populating it.
     * Copy all fields and zero-initialized padding, including raw float bits;
     * do not replace complete identity proof with a handful of pointer checks. */
    memset(identity, 0, sizeof(*identity));
    identity->ready = current.ready;
    identity->player = current.player;
    identity->length = sizeof(current);
    memcpy(identity->bytes, &current, sizeof(current));
    return 1;
}

static int player_reference_read(void *context, uintptr_t player, p1_reference_state *state) {
    (void)context;
    if (!player_reference_boundary(NULL) || !player || player > UINTPTR_MAX - 0x48) {
        return 0;
    }
    memset(state, 0, sizeof(*state));
    if (!read_local((void *)player, &state->player_vtable, sizeof(state->player_vtable)) ||
        !read_local((void *)(player + 0x38), &state->reference_vtable, sizeof(state->reference_vtable)) ||
        state->reference_vtable != (uintptr_t)LX(P1_REFERENCE_SUBOBJECT_VTABLE_VA) ||
        !read_local((void *)(player + 0x40), &state->count, sizeof(state->count)) ||
        !read_local((void *)(player + 0x44), &state->marker, sizeof(state->marker)) ||
        !read_local((void *)state->reference_vtable, &state->zero_target, sizeof(state->zero_target))) {
        return 0;
    }
    return 1;
}

/* Immediate fresh validation before either native entry. Never call through a
 * cached Player pointer merely because it matched at an earlier boundary. */
static int player_reference_call_ready(uintptr_t player, p1_reference_obligation obligation,
                                       uint32_t expected_count) {
    p1_reference_identity identity = {0};
    p1_reference_state state = {0};
    return player_reference_native_calls_enabled &&
        player_reference_probe_state.obligation == obligation &&
        p1_reference_snapshot(&player_reference_probe_state, &identity, &state) &&
        identity.player == player && state.count == expected_count;
}

static p1_reference_call_result player_reference_acquire(void *context, uintptr_t function,
                                                        uintptr_t player) {
    (void)context;
    if (function != (uintptr_t)LX(P1_REFERENCE_ACQUIRE_VA) ||
        !player_reference_call_ready(player, P1_REFERENCE_ACQUIRE_PENDING,
                                     player_reference_probe_state.initial_count)) {
        return P1_REFERENCE_NOT_CALLED;
    }
    ++player_reference_acquire_calls;
    ((void (*)(void *))function)((void *)player);
    return P1_REFERENCE_RETURNED;
}

static p1_reference_call_result player_reference_release(void *context, uintptr_t function,
                                                        uintptr_t subobject) {
    (void)context;
    if (function != (uintptr_t)LX(P1_REFERENCE_RELEASE_VA) || subobject < 0x38 ||
        !player_reference_call_ready(subobject - 0x38, P1_REFERENCE_RELEASE_PENDING,
                                     player_reference_probe_state.initial_count + 1)) {
        return P1_REFERENCE_NOT_CALLED;
    }
    ++player_reference_release_calls;
    ((void (*)(void *))function)((void *)subobject);
    return P1_REFERENCE_RETURNED;
}

static int lua_player_reference_request(lua_State *L) {
    int valid = accept_ui() && stats.active && stats.issued == 1 && boot_phase == 3 &&
        path_installed && stock_observer_wanted() && atomic_load(&stock_observer_requested) &&
        stock_observer_worker_thread && GetCurrentThreadId() == stock_observer_worker_thread &&
        ui_thread == stock_observer_worker_thread && GetCurrentThreadId() != main_thread &&
        gate_tls_worker(gate_tls_self()) && !atomic_load(&stock_observer_input_depth) &&
        !atomic_load(&sko_session.wrapper_depth) && !atomic_load(&stock_observer_thread_failure);
    if (valid) {
        p1_reference_operations operations = {
            NULL, player_reference_boundary, player_reference_pins, player_reference_sample,
            player_reference_read, player_reference_acquire, player_reference_release
        };
        valid = p1_reference_request(&player_reference_probe_state, &operations,
                                     (uintptr_t)image, sizeof(stock_keyboard_identity_sample));
    }
    p_lua_pushnumber(L, valid);
    return 1;
}

/* Read-only eligibility evidence, including marker 1: never normalize marker
 * state merely to make the diagnostic eligible. The full identity must match
 * across two fresh collections; counts/markers are observations, not permission. */
static int player_reference_observe(void) {
    p1_reference_identity before = {0};
    p1_reference_identity after = {0};
    p1_reference_state first = {0};
    p1_reference_state last = {0};
    if (!player_reference_boundary(NULL)) {
        return p1_reference_fail(&player_reference_probe_state, "observation_boundary");
    }
    if (!player_reference_pins(NULL, p1_reference_pins,
                               sizeof(p1_reference_pins) / sizeof(p1_reference_pins[0]))) {
        return p1_reference_fail(&player_reference_probe_state, "observation_pins");
    }
    if (!player_reference_sample(NULL, &before) ||
        !player_reference_read(NULL, before.player, &first) ||
        !player_reference_sample(NULL, &after)) {
        return p1_reference_fail(&player_reference_probe_state, "observation_read");
    }
    if (after.player != before.player || after.length != before.length ||
        memcmp(before.bytes, after.bytes, before.length)) {
        return p1_reference_fail(&player_reference_probe_state, "observation_identity");
    }
    if (!player_reference_read(NULL, after.player, &last)) {
        return p1_reference_fail(&player_reference_probe_state, "observation_read");
    }
    if (first.player_vtable != (uintptr_t)LX(P1_REFERENCE_PLAYER_VTABLE_VA) ||
        last.player_vtable != (uintptr_t)LX(P1_REFERENCE_PLAYER_VTABLE_VA) ||
        first.zero_target != (uintptr_t)LX(P1_REFERENCE_ZERO_TARGET_VA) ||
        last.zero_target != (uintptr_t)LX(P1_REFERENCE_ZERO_TARGET_VA)) {
        return p1_reference_fail(&player_reference_probe_state, "observation_vtable");
    }
    if (player_reference_probe_state.failed) {
        return 0;
    }
    player_reference_probe_state.initial_identity = before;
    player_reference_probe_state.initial_count = first.count;
    player_reference_initial_marker = first.marker;
    player_reference_probe_state.last_observed_count = last.count;
    player_reference_probe_state.last_observed_marker = last.marker;
    player_reference_probe_state.reference_samples = 2;
    player_reference_preconditions = first.count >= 2 && first.count < INT32_MAX &&
        first.count == last.count && first.marker == 2 && last.marker == 2;
    player_reference_observed = 1;
    player_reference_probe_state.reason = "lifetime_contract_pending";
    return 1;
}

static int player_reference_after_join(void) {
    if (!player_reference_probe_state.requested) {
        return 1;
    }
    if (player_reference_inflight) {
        return p1_reference_fail(&player_reference_probe_state, "reentry");
    }
    if (player_reference_probe_state.failed) {
        return 0;
    }
    if (player_reference_probe_state.passed || player_reference_observed || stats.issued < 2) {
        return 1;
    }
    player_reference_run_tick = stats.issued;
    player_reference_inflight = 1;
    int result = player_reference_native_calls_enabled ? p1_reference_run(&player_reference_probe_state) :
                                                       player_reference_observe();
    player_reference_inflight = 0;
    return result && !player_reference_probe_state.failed;
}

static int player_reference_clean(void) {
    return player_reference_probe_state.obligation == P1_REFERENCE_NO_OBLIGATION;
}

static int lua_player_reference_status(lua_State *L) {
    if (!accept_ui()) {
        p_lua_pushnil(L);
        return 1;
    }
    p1_reference_probe_status status = p1_reference_status(&player_reference_probe_state);
    char output[400];
    int length = snprintf(output, sizeof(output),
        "requested=%u attempted=%u passed=%u failed=%u clean=%u obligation=%u reason=%s "
        "run_tick=%llu initial_count=%u last_count=%u initial_marker=%u marker=%u samples=%u "
        "observed=%u reference_preconditions=%u native_calls_enabled=%u acquire_calls=%u release_calls=%u",
        status.requested, status.attempted, status.passed, status.failed, status.clean,
        (unsigned)status.obligation, status.reason, (unsigned long long)player_reference_run_tick,
        status.initial_count, status.last_observed_count, player_reference_initial_marker,
        status.last_observed_marker,
        status.reference_samples, player_reference_observed, player_reference_preconditions,
        player_reference_native_calls_enabled, player_reference_acquire_calls,
        player_reference_release_calls);
    if (length < 0 || (size_t)length >= sizeof(output)) {
        p_lua_pushnil(L);
    } else {
        p_lua_pushlstring(L, output, (size_t)length);
    }
    return 1;
}
#endif
