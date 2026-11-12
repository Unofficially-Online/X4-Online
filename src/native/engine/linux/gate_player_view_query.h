#ifndef X4_GATE_PLAYER_VIEW_QUERY_H
#define X4_GATE_PLAYER_VIEW_QUERY_H

/* The preload observes a stock getter without changing its result. This
 * adapter supplies the authenticated UI thread and marks our own Lua work,
 * which must not be mistaken for an ordinary game-UI call. */
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#ifndef VIEW_QUERY_KERNEL_TID
#define VIEW_QUERY_KERNEL_TID() syscall(SYS_gettid)
#endif
#include "player_view_query_preload_pin.h"

static player_view_query_api view_query_api;
static player_ui_callback_api ui_callback_api;
static unsigned ui_callback_close_requested;
static unsigned view_query_attempted, view_query_active, view_query_finished;
static unsigned view_query_failed;
/* The first refusal stays resident, so later calls cannot overwrite the cause. */
static const char *view_query_failure_stage;
static uint32_t view_query_failure_bit;

static void view_query_refuse(const char *stage, uint32_t failed_bit) {
    if (view_query_failed) return;
    view_query_failed = 1;
    view_query_failure_stage = stage;
    view_query_failure_bit = failed_bit;
    char message[320];
    int saved_errno = errno;
    int length;
    if (failed_bit == (1u << 9)) {
        length = snprintf(message, sizeof message,
            "X4LAN VIEW_QUERY_DIAGNOSTIC v=1 stage=%s failed_bit=0x%x tick=%llu "
            "thread=%u ui_thread=%u worker_thread=%u worker_tls=%d input_tick=%llu input_depth=%d wrapper_depth=%d\n",
            stage, failed_bit, (unsigned long long)stats.issued,
            (unsigned)GetCurrentThreadId(), (unsigned)ui_thread, (unsigned)stock_observer_worker_thread,
            gate_tls_worker(gate_tls_self()), (unsigned long long)__atomic_load_n(&stock_observer_input_tick, __ATOMIC_ACQUIRE),
            atomic_load(&stock_observer_input_depth), atomic_load(&sko_session.wrapper_depth));
    } else {
        length = snprintf(message, sizeof message,
            "X4LAN VIEW_QUERY_DIAGNOSTIC v=1 stage=%s failed_bit=0x%x tick=%llu\n",
            stage, failed_bit, (unsigned long long)stats.issued);
    }
    if (length > 0 && (size_t)length < sizeof message)
        (void)write(STDERR_FILENO, message, (size_t)length);
    errno = saved_errno;
}

static int view_query_expected_library(const char **sha, uint64_t *size) {
    const char *text = getenv("X4MP_VIEW_QUERY_PRELOAD_SIZE");
    *sha = getenv("X4MP_VIEW_QUERY_PRELOAD_SHA256");
    if (!player_view_pin_digest_string(*sha) || !text || !*text) return 0;
    uint64_t value = 0;
    for (const unsigned char *cursor = (const unsigned char *)text; *cursor; ++cursor) {
        if (*cursor < '0' || *cursor > '9' || value > (UINT64_MAX - (*cursor - '0')) / 10) return 0;
        value = value * 10 + (*cursor - '0');
    }
    if (!value) return 0;
    *size = value;
    return 1;
}

static int view_query_occupied_original_pin(void) {
    const char *sha;
    uint64_t size;
    return view_query_expected_library(&sha, &size) && player_view_query_occupied_pin(sha, size);
}

/* Bit identifies the first failed predicate, in the original short-circuit
 * order. Later predicates are not evaluated after a refusal. */
static uint32_t view_query_admission_failure(void) {
    if (!accept_ui()) return 1u << 0;
    if (!stats.active) return 1u << 1;
    if (boot_phase != 3) return 1u << 2;
    if (stats.slot != 1 && stats.slot != 2) return 1u << 3;
    if (wire_clients != 2) return 1u << 4;
    if (!keyboard_probe_requested) return 1u << 5;
    if (keyboard_probe_queue.config.independent_players != 1) return 1u << 6;
    if (player_recurring_physics != 3) return 1u << 7;
    if (atomic_load(&player_physics_ready) != 1) return 1u << 8;
    if (!stock_observer_worker_boundary()) return 1u << 9;
    if (!gate_tls_worker(gate_tls_self())) return 1u << 10;
    if (player_routes_scope) return 1u << 11;
    if (player_network_busy) return 1u << 12;
    if (player_physics_replay_busy) return 1u << 13;
    if (player_physics_upkeep_busy) return 1u << 14;
    if (keyboard_probe_inflight) return 1u << 15;
    if (atomic_load(&stock_observer_input_depth)) return 1u << 16;
    if (atomic_load(&sko_session.wrapper_depth)) return 1u << 17;
    if (!player_physics_identity()) return 1u << 18;
    return 0;
}

static int lua_player_view_query_observe(lua_State *state) {
    int accepted = 0;
    const char *sha;
    uint64_t size;
    if (view_query_failed) {
        /* Preserve the first refusal. */
    } else if (view_query_attempted) {
        view_query_refuse("observe_repeat", 0);
    } else {
        view_query_attempted = 1;
        if (stats.issued != 7) {
            view_query_refuse("observe_tick", 0);
        } else {
            uint32_t failed_bit = view_query_admission_failure();
            if (failed_bit) view_query_refuse("observe_admission", failed_bit);
            else if (!view_query_expected_library(&sha, &size)) view_query_refuse("observe_library_env", 0);
            else if (!player_view_query_provider(sha, size, &view_query_api)) view_query_refuse("observe_provider", 0);
            else if (!view_query_occupied_original_pin()) view_query_refuse("observe_original_pin", 0);
            else if (getenv("X4MP_UI_CALLBACK_OBSERVER") &&
                     (strcmp(getenv("X4MP_UI_CALLBACK_OBSERVER"), "1") ||
                      !player_ui_callback_provider(&view_query_api, &ui_callback_api)))
                view_query_refuse("ui_callback_provider", 0);
            else {
                /* The gate's thread token is pthread-derived; this provider
                 * authenticates kernel TIDs. Convert only at this boundary. */
                long kernel_tid = VIEW_QUERY_KERNEL_TID();
                if (kernel_tid <= 0 || (uint64_t)kernel_tid > UINT32_MAX)
                    view_query_refuse("observe_kernel_tid", 0);
                else if (!view_query_api.configure((uint32_t)kernel_tid, (uint32_t)kernel_tid))
                    view_query_refuse("observe_configure", 0);
                else if (!view_query_api.phase(stats.issued, 1)) view_query_refuse("observe_phase", 0);
                else accepted = 1;
            }
        }
    }
    if (accepted) view_query_active = 1;
    p_lua_pushnumber(state, accepted);
    return 1;
}

static int lua_player_view_query_phase(lua_State *state) {
    double probe = p_lua_tonumber(state, 1);
    int accepted = !view_query_failed && (probe == 0 || probe == 1);
    /* The onUpdate bracket also runs during loading and after the final
     * frozen report. Only the admitted observation interval reaches the DSO. */
    if (accepted && view_query_active && !view_query_finished) {
        accepted = accept_ui() && gate_tls_worker(gate_tls_self()) &&
            view_query_api.phase(stats.issued, (unsigned)probe);
    }
    if (!accepted) view_query_refuse("phase", 0);
    p_lua_pushnumber(state, accepted);
    return 1;
}

static int lua_player_ui_callback_close(lua_State *state) {
    const char *sha;
    uint64_t size;
    player_view_query_api authenticated;
    player_ui_callback_api callbacks;
    int accepted = view_query_active && !view_query_finished && !view_query_failed &&
        !ui_callback_close_requested && !view_query_admission_failure() &&
        view_query_expected_library(&sha, &size) &&
        player_view_query_provider(sha, size, &authenticated) &&
        authenticated.finish == view_query_api.finish &&
        player_ui_callback_provider(&authenticated, &callbacks) &&
        callbacks.info == ui_callback_api.info && callbacks.snapshot == ui_callback_api.snapshot &&
        callbacks.finish == ui_callback_api.finish && callbacks.finish();
    if (accepted) {
        ui_callback_api = callbacks;
        ui_callback_close_requested = 1;
    } else {
        view_query_refuse("ui_callback_close", 0);
    }
    p_lua_pushnumber(state, accepted);
    return 1;
}

static int lua_player_ui_callback_result(lua_State *state) {
    const char *sha;
    uint64_t size;
    player_view_query_api authenticated;
    player_ui_callback_api callbacks;
    player_ui_callback_snapshot sample = {0};
    int accepted = view_query_active && !view_query_finished && !view_query_failed &&
        ui_callback_close_requested && !view_query_admission_failure() &&
        view_query_expected_library(&sha, &size) &&
        player_view_query_provider(sha, size, &authenticated) &&
        authenticated.finish == view_query_api.finish &&
        player_ui_callback_provider(&authenticated, &callbacks) &&
        callbacks.finish == ui_callback_api.finish && callbacks.snapshot == ui_callback_api.snapshot &&
        callbacks.snapshot(&sample) && sample.closed == 1 && sample.object && sample.owner_tid;
    for (unsigned i = 0; accepted && i < 2; ++i) {
        if (!sample.entered[i] || sample.entered[i] != sample.left[i] || sample.inflight[i] ||
            sample.foreign[i] || sample.nested[i] || sample.object_mismatch[i] ||
            sample.thread_mismatch[i] || sample.first_tid[i] != sample.owner_tid) accepted = 0;
    }
    if (!accepted) {
        view_query_refuse("ui_callback_result", 0);
        p_lua_pushnil(state);
        return 1;
    }
    char result[512];
    int length = snprintf(result, sizeof result,
        "v=1 owner=%u object=%llx move=%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%u "
        "render=%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%u",
        sample.owner_tid, (unsigned long long)sample.object,
#define UI_RESULT_FIELDS(i) (unsigned long long)sample.entered[i], (unsigned long long)sample.left[i], \
        (unsigned long long)sample.queries[i], (unsigned long long)sample.foreign[i], \
        (unsigned long long)sample.nested[i], (unsigned long long)sample.object_mismatch[i], \
        (unsigned long long)sample.inflight[i], (unsigned long long)sample.thread_mismatch[i], sample.first_tid[i]
        UI_RESULT_FIELDS(0), UI_RESULT_FIELDS(1));
#undef UI_RESULT_FIELDS
    if (length <= 0 || (size_t)length >= sizeof result) {
        view_query_refuse("ui_callback_format", 0);
        p_lua_pushnil(state);
    } else {
        p_lua_pushlstring(state, result, (size_t)length);
    }
    return 1;
}

static int lua_player_view_query_finish(lua_State *state) {
    const char *sha;
    uint64_t size;
    player_view_query_api authenticated;
    player_view_query_snapshot sample = {0};
    const char *failure = NULL;
    uint32_t failed_bit = 0;
    if (!view_query_active || view_query_finished || view_query_failed) failure = "finish_state";
    else if ((failed_bit = view_query_admission_failure()) != 0) failure = "finish_admission";
    else if (!view_query_expected_library(&sha, &size)) failure = "finish_library_env";
    else if (!player_view_query_provider(sha, size, &authenticated)) failure = "finish_provider";
    else if (authenticated.finish != view_query_api.finish) failure = "finish_provider_changed";
    else if (!view_query_api.finish(&sample)) failure = "finish_callback";
    else if (sample.count > PLAYER_VIEW_QUERY_ROWS) failure = "finish_row_count";
    if (failure) {
        view_query_refuse(failure, failed_bit);
        p_lua_pushnil(state);
        return 1;
    }
    view_query_finished = 1;
    char output[16384];
    int length = snprintf(output, sizeof output,
        "v=1 finished=1 ui_calls=%llu worker_calls=%llu other_calls=%llu probe_calls=%llu overflow=%llu rows=%u data=",
        (unsigned long long)sample.ui_calls, (unsigned long long)sample.worker_calls,
        (unsigned long long)sample.other_calls, (unsigned long long)sample.probe_calls,
        (unsigned long long)sample.overflow, sample.count);
    for (unsigned index = 0; index < sample.count; ++index) {
        const player_view_query_row *row = &sample.rows[index];
        if (length < 0 || (size_t)length >= sizeof output) break;
        int written = snprintf(output + length, sizeof output - (size_t)length,
            "%s%llx/%llu/%llu/%llu/%llu/%u/%u", index ? ";" : "",
            (unsigned long long)row->caller, (unsigned long long)row->result,
            (unsigned long long)row->first_tick, (unsigned long long)row->last_tick,
            (unsigned long long)row->count, row->in_image, row->probe);
        if (written < 0) { length = -1; break; }
        length += written;
    }
    if (length < 0 || (size_t)length >= sizeof output) {
        view_query_refuse("finish_format", 0);
        p_lua_pushnil(state);
    } else {
        p_lua_pushlstring(state, output, (size_t)length);
    }
    return 1;
}
#endif
