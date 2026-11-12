#define _GNU_SOURCE
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "player_view_query_preload.h"

/* Test only: authenticate through controllable provider stubs. */
#define X4_PLAYER_VIEW_QUERY_PRELOAD_PIN_H
typedef struct {
    int (*configure)(uint32_t, uint32_t);
    int (*phase)(uint64_t, unsigned);
    int (*snapshot)(player_view_query_snapshot *);
    int (*finish)(player_view_query_snapshot *);
    player_view_query_info information;
} player_view_query_api;
typedef struct {
    unsigned (*info)(player_ui_callback_snapshot *);
    int (*snapshot)(player_ui_callback_snapshot *);
    int (*finish)(void);
} player_ui_callback_api;
typedef struct { int unused; } lua_State;
static struct { int active, issued, slot; } stats = {1, 7, 1};
static struct { struct { int independent_players; } config; } keyboard_probe_queue = {{1}};
static struct { _Atomic int wrapper_depth; } sko_session;
static _Atomic int player_physics_ready = 1, stock_observer_input_depth;
static int boot_phase = 3, wire_clients = 2, keyboard_probe_requested = 1, player_recurring_physics = 3;
static uint32_t ui_thread = 42, stock_observer_worker_thread = 42;
static uint64_t stock_observer_input_tick = 7;
static int player_network_busy, player_physics_replay_busy, player_physics_upkeep_busy, keyboard_probe_inflight;
static void *player_routes_scope;
static int ui_ok = 1, worker_ok = 1, tls_ok = 1, identity_ok = 1, provider_ok = 1, pin_ok = 1;
static int configure_ok = 1, phase_ok = 1, finish_ok = 1, configure_calls, phase_calls, finish_calls;
static unsigned report_count = 1;
static long mocked_kernel_tid = 9001;
#define VIEW_QUERY_KERNEL_TID() (mocked_kernel_tid)
static double lua_input, lua_number;
static int lua_nil;
static char lua_string[16384];
static int accept_ui(void) { return ui_ok; }
static int stock_observer_worker_boundary(void) { return worker_ok; }
static void *gate_tls_self(void) { return &stats; }
static int gate_tls_worker(void *tls) { return tls && tls_ok; }
static int player_physics_identity(void) { return identity_ok; }
static uint32_t GetCurrentThreadId(void) { return 42; }
static void p_lua_pushnumber(lua_State *state, double value) { (void)state; lua_number = value; lua_nil = 0; }
static double p_lua_tonumber(lua_State *state, int index) { (void)state; (void)index; return lua_input; }
static void p_lua_pushnil(lua_State *state) { (void)state; lua_nil = 1; }
static void p_lua_pushlstring(lua_State *state, const char *text, size_t length) {
    (void)state; assert(length < sizeof lua_string); memcpy(lua_string, text, length); lua_string[length] = 0; lua_nil = 0;
}
static int player_view_pin_digest_string(const char *sha) {
    if (!sha || strlen(sha) != 64) return 0;
    for (unsigned i = 0; i < 64; ++i) if (!((sha[i] >= '0' && sha[i] <= '9') || (sha[i] >= 'a' && sha[i] <= 'f'))) return 0;
    return 1;
}
static int mock_configure(uint32_t ui, uint32_t worker) { assert(ui == (uint32_t)mocked_kernel_tid && worker == (uint32_t)mocked_kernel_tid && ui != 42); ++configure_calls; return configure_ok; }
static int mock_phase(uint64_t tick, unsigned probe) { assert(tick >= 7 && probe <= 1); ++phase_calls; return phase_ok; }
static int mock_finish(player_view_query_snapshot *out) {
    ++finish_calls;
    if (!finish_ok) return 0;
    *out = (player_view_query_snapshot){.ui_calls=3, .worker_calls=4, .other_calls=5, .probe_calls=1, .overflow=2, .count=1,
        .rows={{.caller=0xabcd, .result=123, .first_tick=7, .last_tick=12, .count=9, .in_image=1, .probe=0}}};
    out->count = report_count;
    return 1;
}
static int player_view_query_occupied_pin(const char *sha, uint64_t size) { return sha && size && pin_ok; }
static int player_view_query_provider(const char *sha, uint64_t size, player_view_query_api *out) {
    if (!sha || !size || !provider_ok) return 0;
    *out = (player_view_query_api){.configure=mock_configure, .phase=mock_phase, .finish=mock_finish}; return 1;
}
static int ui_closed, ui_snapshot_good = 1;
static unsigned mock_ui_info(player_ui_callback_snapshot *out) { (void)out; return 1; }
static int mock_ui_finish(void) { ui_closed = 1; return 1; }
static int mock_ui_snapshot(player_ui_callback_snapshot *out) {
    *out = (player_ui_callback_snapshot){.closed=ui_closed, .object=0x20008, .owner_tid=9001,
        .entered={1,1}, .left={1,1}, .first_tid={9001,9001}};
    if (!ui_snapshot_good) out->inflight[0] = 1;
    return 1;
}
static int player_ui_callback_provider(const player_view_query_api *provider, player_ui_callback_api *out) {
    if (!provider || !provider_ok) return 0;
    *out = (player_ui_callback_api){mock_ui_info, mock_ui_snapshot, mock_ui_finish};
    return 1;
}
#include "gate_player_view_query.h"

static void reset(void) {
    view_query_attempted = view_query_active = view_query_finished = view_query_failed = 0;
    ui_callback_close_requested = ui_closed = 0; ui_snapshot_good = 1;
    memset(&ui_callback_api, 0, sizeof ui_callback_api);
    unsetenv("X4MP_UI_CALLBACK_OBSERVER");
    view_query_failure_stage = NULL; view_query_failure_bit = 0;
    stats.active = 1; stats.issued = 7; stats.slot = 1;
    ui_ok = worker_ok = tls_ok = identity_ok = provider_ok = pin_ok = 1;
    configure_ok = phase_ok = finish_ok = 1; report_count = 1; mocked_kernel_tid = 9001;
    configure_calls = phase_calls = finish_calls = lua_nil = 0; lua_number = -1; lua_input = 0;
    lua_string[0] = 0;
    assert(!setenv("X4MP_VIEW_QUERY_PRELOAD_SHA256", "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", 1));
    assert(!setenv("X4MP_VIEW_QUERY_PRELOAD_SIZE", "123", 1));
}
int main(void) {
    lua_State state = {0};
    reset();
    const char *sha = NULL; uint64_t size = 0;
    assert(view_query_expected_library(&sha, &size) && size == 123);
    const char *bad_sizes[] = {"", "0", "-1", "1 ", "18446744073709551616"};
    for (unsigned i = 0; i < sizeof bad_sizes / sizeof *bad_sizes; ++i) {
        assert(!setenv("X4MP_VIEW_QUERY_PRELOAD_SIZE", bad_sizes[i], 1));
        assert(!view_query_expected_library(&sha, &size));
    }
    assert(!setenv("X4MP_VIEW_QUERY_PRELOAD_SIZE", "123", 1));
    assert(!setenv("X4MP_VIEW_QUERY_PRELOAD_SHA256", "ABC", 1));
    assert(!view_query_expected_library(&sha, &size));

    reset();
    assert(lua_player_view_query_observe(&state) == 1 && lua_number == 1);
    assert(configure_calls == 1 && phase_calls == 1);
    stats.issued = 8; lua_input = 0;
    assert(lua_player_view_query_phase(&state) == 1 && lua_number == 1 && phase_calls == 2);
    assert(lua_player_view_query_finish(&state) == 1 && !lua_nil && finish_calls == 1);
    assert(strstr(lua_string, "ui_calls=3 worker_calls=4 other_calls=5 probe_calls=1 overflow=2 rows=1"));
    assert(strstr(lua_string, "data=abcd/123/7/12/9/1/0"));
    assert(lua_player_view_query_finish(&state) == 1 && lua_nil && finish_calls == 1);

    reset();
    assert(!setenv("X4MP_UI_CALLBACK_OBSERVER", "1", 1));
    assert(lua_player_view_query_observe(&state) == 1 && lua_number == 1);
    stats.issued = 8;
    assert(lua_player_ui_callback_close(&state) == 1 && lua_number == 1);
    assert(lua_player_ui_callback_result(&state) == 1 && !lua_nil);
    assert(strstr(lua_string, "owner=9001 object=20008"));
    ui_snapshot_good = 0;
    assert(lua_player_ui_callback_result(&state) == 1 && lua_nil && view_query_failed);

    reset(); worker_ok = 0;
    assert(lua_player_view_query_observe(&state) == 1 && lua_number == 0 && view_query_failed);
    assert(!strcmp(view_query_failure_stage, "observe_admission") && view_query_failure_bit == (1u << 9));
    worker_ok = 1;
    assert(lua_player_view_query_observe(&state) == 1 && lua_number == 0 && configure_calls == 0);
    assert(!strcmp(view_query_failure_stage, "observe_admission") && view_query_failure_bit == (1u << 9));
    assert(lua_player_view_query_finish(&state) == 1 && lua_nil);
    reset(); assert(lua_player_view_query_observe(&state) == 1 && lua_number == 1);
    phase_ok = 0; lua_input = 0;
    assert(lua_player_view_query_phase(&state) == 1 && lua_number == 0 && view_query_failed);
    phase_ok = 1;
    assert(lua_player_view_query_phase(&state) == 1 && lua_number == 0);
    assert(lua_player_view_query_finish(&state) == 1 && lua_nil && finish_calls == 0);
    reset(); pin_ok = 0;
    assert(lua_player_view_query_observe(&state) == 1 && lua_number == 0 && configure_calls == 0);
    assert(!strcmp(view_query_failure_stage, "observe_original_pin"));
    reset(); provider_ok = 0;
    assert(lua_player_view_query_observe(&state) == 1 && lua_number == 0 && configure_calls == 0);
    assert(!strcmp(view_query_failure_stage, "observe_provider"));
    reset(); assert(!setenv("X4MP_VIEW_QUERY_PRELOAD_SIZE", "0", 1));
    assert(lua_player_view_query_observe(&state) == 1 && lua_number == 0 && configure_calls == 0);
    assert(!strcmp(view_query_failure_stage, "observe_library_env"));
    reset(); configure_ok = 0;
    assert(lua_player_view_query_observe(&state) == 1 && lua_number == 0 && configure_calls == 1 && phase_calls == 0);
    assert(!strcmp(view_query_failure_stage, "observe_configure"));
    reset(); phase_ok = 0;
    assert(lua_player_view_query_observe(&state) == 1 && lua_number == 0 && configure_calls == 1 && phase_calls == 1);
    assert(!strcmp(view_query_failure_stage, "observe_phase"));
    reset(); assert(lua_player_view_query_observe(&state) == 1 && lua_number == 1);
    finish_ok = 0;
    assert(lua_player_view_query_finish(&state) == 1 && lua_nil && view_query_failed);
    reset(); assert(lua_player_view_query_observe(&state) == 1 && lua_number == 1);
    report_count = PLAYER_VIEW_QUERY_ROWS + 1;
    assert(lua_player_view_query_finish(&state) == 1 && lua_nil && view_query_failed);
    reset(); lua_input = 2;
    assert(lua_player_view_query_phase(&state) == 1 && lua_number == 0 && view_query_failed);
    assert(lua_player_view_query_observe(&state) == 1 && lua_number == 0 && configure_calls == 0);
    for (unsigned index = 0; index < 3; ++index) {
        const long invalid[] = {0, -1, (long)UINT32_MAX + 1};
        reset(); mocked_kernel_tid = invalid[index];
        assert(lua_player_view_query_observe(&state) == 1 && lua_number == 0 && configure_calls == 0);
        assert(!strcmp(view_query_failure_stage, "observe_kernel_tid"));
    }
    reset(); mocked_kernel_tid = syscall(SYS_gettid);
    assert(mocked_kernel_tid > 0 && mocked_kernel_tid != 42);
    assert(lua_player_view_query_observe(&state) == 1 && lua_number == 1 && configure_calls == 1);
    reset(); stats.issued = 6;
    assert(lua_player_view_query_observe(&state) == 1 && lua_number == 0 && configure_calls == 0);
    reset(); identity_ok = 0;
    assert(lua_player_view_query_observe(&state) == 1 && lua_number == 0 && configure_calls == 0);
    assert(!strcmp(view_query_failure_stage, "observe_admission") && view_query_failure_bit == (1u << 18));
    puts("view-query adapter PASS");
}
