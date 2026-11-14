#ifndef X4_GATE_PLAYER_UI_OVERLAY_PROBE_H
#define X4_GATE_PLAYER_UI_OVERLAY_PROBE_H

/* Disposable diagnostic: one, eight, or 160 guarded UI Move callbacks.
 * Other game threads are not excluded from the temporary P1 fields. */
#include "player_ui_overlay_lifecycle.h"
#include "player_presentation_overlay.h"
#include "player_ui_viewstate.h"
#include "player_ui_seat_pose.h"
#include "player_live_pose_preload.h"

/* Bounded UI handlers in one Move; each must pause and resume exactly once. */
enum { PLAYER_UI_OVERLAY_MAX_UPDATES = 8 };

typedef struct {
    player_ui_overlay_lifecycle lifecycle;
    player_presentation_overlay transaction;
    player_presentation_overlay_ops operations;
    presentation_fork_p1 canonical;
    unsigned char replacement[3][24];
    uintptr_t object;
    uint64_t before, during, paused, resumed, restored;
    unsigned applies, restores, final_restore;
    uint32_t move_bank;
    int move_bank_bound;
} native_ui_overlay_probe;
static native_ui_overlay_probe ui_overlay_probe;
static _Atomic unsigned ui_overlay_armed, ui_overlay_result_read, ui_overlay_main_passes;
static _Atomic unsigned ui_overlay_completed;
static unsigned ui_overlay_multi_count;
/* Seat queries and plan setup require the entire selected presentation window,
 * not the single/eight-callback diagnostics or a mismatched session budget. */
static int ui_overlay_full_window_selected(void) {
    unsigned expected = player_session_budget.frames ? player_session_budget.presentation_count : 160u;
    return expected != 0 && ui_overlay_multi_count == expected;
}
static int ui_correction_enabled, ui_game_render_enabled, ui_game_render_active;
static int ui_camera_refill_enabled, ui_camera_refill_active;
static int ui_projection_scope_enabled, ui_projection_scope_active;
static unsigned ui_projection_scope_closed;
static uint64_t ui_projection_scope_last_tick;
static unsigned ui_camera_refill_closed;
static player_game_render_bind_fn ui_game_render_bind;
static int (*ui_game_render_activate)(void);
static player_ui_overlay_progress_fn ui_game_render_progress;
static uintptr_t ui_game_render_object;
static _Atomic unsigned ui_game_render_closed;
static player_ui_overlay_progress_fn ui_overlay_progress;
static char ui_overlay_output[512];

static _Noreturn void ui_overlay_die(const char *reason) {
    static const char prefix[] = "X4 UI OVERLAY FATAL reason=";
    (void)write(2, prefix, sizeof prefix - 1);
    if (reason) (void)write(2, reason, strlen(reason));
    (void)write(2, "\n", 1);
    _exit(223);
}
static void ui_overlay_fatal(void *context, const char *reason) {
    (void)context;
    ui_overlay_die(reason);
}
static uint64_t ui_overlay_tid(void *context) {
    (void)context;
    long tid = syscall(SYS_gettid);
    return tid > 0 ? (uint64_t)tid : 0;
}
/* The fork's stored bank authenticates its retained graph. Correction uses a
 * separately captured bank for this Move and refuses any in-lease change. */
#ifdef GATE_TLS_TEST
static int (*ui_overlay_test_bank_read)(uint32_t *bank);
#endif
static int ui_overlay_read_worker_bank(uint32_t *bank) {
#ifdef GATE_TLS_TEST
    if (ui_overlay_test_bank_read) return ui_overlay_test_bank_read(bank);
#endif
    unsigned char *tls = gate_tls_self();
    uint32_t view = 0, view_after = 0, first = 2, second = 2;
    if (!bank || !tls || !gate_tls_worker(tls) ||
        !presentation_fork_read(NULL, (uintptr_t)(tls + GATE_TLS_LINUX_VIEW), &view, 4) || view != 1 ||
        !presentation_fork_read(NULL, (uintptr_t)LX(0x3e77430), &first, 4) || first > 1 ||
        gate_tls_self() != tls ||
        !presentation_fork_read(NULL, (uintptr_t)(tls + GATE_TLS_LINUX_VIEW), &view_after, 4) || view_after != view ||
        !presentation_fork_read(NULL, (uintptr_t)LX(0x3e77430), &second, 4) || second != first) return 0;
    *bank = first;
    return 1;
}
static int ui_overlay_bank_matches(const native_ui_overlay_probe *probe, uint32_t bank) {
    uint32_t current = 2;
    return probe && probe->move_bank_bound && bank == probe->move_bank &&
        ui_overlay_read_worker_bank(&current) && current == bank;
}
static uint32_t ui_overlay_expected_bank(const native_ui_overlay_probe *probe) {
    return ui_correction_enabled ? probe->move_bank : presentation_fork.request.bank;
}
static int ui_overlay_owner(void *context, uintptr_t p1, uintptr_t p2, uintptr_t room, uint32_t bank) {
    (void)context;
    uint32_t held_bank = presentation_fork.request.bank;
    return (!ui_correction_enabled || ui_overlay_bank_matches(&ui_overlay_probe, bank)) &&
        presentation_fork_owner(&presentation_fork, p1, p2, room,
                                ui_correction_enabled ? held_bank : bank) &&
        player_physics_worker == (uint64_t)GetCurrentThreadId() && player_physics_identity();
}
static int ui_overlay_capture_move_bank(native_ui_overlay_probe *probe) {
    uint32_t selected = 2;
    if (!probe || __atomic_load_n(&probe->lifecycle.state, __ATOMIC_ACQUIRE) != PLAYER_UI_OVERLAY_NEW ||
        !ui_overlay_read_worker_bank(&selected)) return 0;
    /* Initial query and its receipts are bound to the fork's original bank. */
    if (!atomic_load_explicit(&ui_overlay_completed, memory_order_acquire) && selected != presentation_fork.request.bank) return 0;
    probe->move_bank = selected;
    probe->move_bank_bound = 1;
    return ui_overlay_bank_matches(probe, selected);
}
static int ui_overlay_bind_move_bank(native_ui_overlay_probe *probe) {
    if (!ui_correction_enabled) return 1;
    const presentation_fork_request *request = &presentation_fork.request;
    if (!player_recurring_physics_entered() || !player_physics_identity() ||
        !presentation_fork_owner(&presentation_fork, request->player1, request->player2, request->room,
                                 request->bank)) return 0;
    return ui_overlay_capture_move_bank(probe);
}
static int ui_overlay_read(void *context, uintptr_t address, void *out, size_t length) {
    (void)context;
    return presentation_fork_read(NULL, address, out, length);
}
/* Only the four leased P1 ranges passed by player_presentation_overlay are written. */
static int ui_overlay_write(void *context, uintptr_t address, const void *in, size_t length) {
    (void)context;
    struct iovec local = {(void *)in, length}, remote = {(void *)address, length};
    return syscall(SYS_process_vm_writev, (long)syscall(SYS_getpid), &local, 1UL, &remote, 1UL, 0UL) == (long)length;
}
static int ui_overlay_canonical_matches(native_ui_overlay_probe *probe) {
    presentation_fork_p1 current;
    presentation_fork_request request = presentation_fork.request;
    request.bank = ui_overlay_expected_bank(probe);
    return (!ui_correction_enabled || ui_overlay_bank_matches(probe, request.bank)) &&
        presentation_fork_capture(&request,
            &(presentation_fork_ops){.context = NULL, .read = presentation_fork_read}, &current) &&
        !memcmp(&current, &probe->canonical, sizeof current) &&
        (!ui_correction_enabled || ui_overlay_bank_matches(probe, request.bank));
}
static int ui_overlay_p2_matches(native_ui_overlay_probe *probe) {
    static const size_t offsets[3] = {0x70, 0x308, 0x6aa8};
    static const size_t sizes[3] = {8, 24, 4};
    unsigned char actual[24];
    for (unsigned i = 0; i < 3; ++i) {
        if (!presentation_fork_read(NULL, presentation_fork.request.player2 + offsets[i], actual, sizes[i]) ||
            memcmp(actual, probe->replacement[i], sizes[i])) return 0;
    }
    return 1;
}
static int ui_live_pose_mark_begin(unsigned state);
static int ui_live_pose_mark(unsigned state);
static int ui_correction_canonical(void);
static void ui_correction_refusal(const char *stage, const native_ui_overlay_probe *probe);
static int ui_correction_apply(native_ui_overlay_probe *probe);
static int ui_correction_restore(void);
static void ui_correction_move_begin(void);
static int ui_correction_move_closed(unsigned ordinal);
static int ui_correction_allocate_session(void);
static void ui_correction_prepare(void);
static void ui_correction_close(void);
static int ui_overlay_apply(void *context) {
    native_ui_overlay_probe *probe = context;
    const presentation_fork_request *request = &presentation_fork.request;
    if (probe->applies && (!ui_overlay_canonical_matches(probe) || !ui_overlay_p2_matches(probe))) {
        if (ui_correction_enabled) ui_correction_refusal("identity_baseline", probe);
        return 0;
    }
    memset(&probe->transaction, 0, sizeof probe->transaction);
    if (!ui_live_pose_mark_begin(PLAYER_LIVE_POSE_OPEN)) {
        if (ui_correction_enabled) ui_correction_refusal("scope_changing", probe);
        return 0;
    }
    if (ui_correction_enabled && !ui_correction_canonical()) {
        ui_correction_refusal("canonical", probe);
        return 0;
    }
    if (!player_presentation_overlay_begin(&probe->transaction, &probe->operations,
            request->player1, request->player2, request->room, ui_overlay_expected_bank(probe))) {
        if (ui_correction_enabled) ui_correction_refusal("identity_install", probe);
        return 0;
    }
    if (ui_correction_enabled && !ui_correction_apply(probe)) {
        ui_correction_refusal("published_install", probe);
        return 0;
    }
    if (!ui_live_pose_mark(PLAYER_LIVE_POSE_OPEN)) {
        if (ui_correction_enabled) ui_correction_refusal("scope_open", probe);
        return 0;
    }
    ++probe->applies;
    if (probe->applies == 1) {
        memcpy(probe->canonical.parent, probe->transaction.original[0], 8);
        memcpy(probe->canonical.vector, probe->transaction.original[1], 24);
        memcpy(probe->canonical.position, probe->transaction.original[2], 4);
        memcpy(probe->canonical.cached_parent, probe->transaction.original[3], 8);
        for (unsigned i = 0; i < 3; ++i) memcpy(probe->replacement[i], probe->transaction.replacement[i], 24);
    }
    uint64_t observed = presentation_fork.occupied();
    if (probe->applies == 1) probe->during = observed;
    else probe->resumed = observed;
    if (observed != request->expected_p2_ship && ui_correction_enabled)
        ui_correction_refusal("occupied_readback", probe);
    return observed == request->expected_p2_ship;
}
static int ui_overlay_restore(void *context) {
    native_ui_overlay_probe *probe = context;
    const presentation_fork_request *request = &presentation_fork.request;
    unsigned next_scope = probe->final_restore ? PLAYER_LIVE_POSE_OUTSIDE : PLAYER_LIVE_POSE_PAUSED;
    if (!ui_live_pose_mark_begin(next_scope)) return 0;
    if (ui_correction_enabled && !ui_correction_restore()) return 0;
    if (!player_presentation_overlay_end(&probe->transaction, request->player1, request->player2,
                                          request->room, ui_overlay_expected_bank(probe)) ||
        !ui_overlay_canonical_matches(probe)) return 0;
    if (!ui_live_pose_mark(next_scope)) return 0;
    ++probe->restores;
    uint64_t observed = presentation_fork.occupied();
    if (probe->restores == 1) probe->paused = observed;
    else probe->restored = observed;
    return observed == request->expected_p1_ship;
}
static void ui_overlay_rearm_for_entry(native_ui_overlay_probe *probe, unsigned completed) {
    if (!ui_overlay_multi_count || !completed) return;
    if (__atomic_load_n(&probe->lifecycle.state, __ATOMIC_ACQUIRE) != PLAYER_UI_OVERLAY_CLOSED ||
        !player_ui_overlay_init(&probe->lifecycle, &probe->lifecycle.ops,
                                probe->lifecycle.owner_tid, probe->object)) ui_overlay_die("rearm");
    probe->applies = probe->restores = 0;
    probe->before = probe->during = probe->paused = probe->resumed = probe->restored = 0;
}
/* Failure to observe is reported independently of the existing overlay receipt. */
static int ui_overlay_viewstate_pins(void) {
    static const lx_site sites[] = {
        {"viewstate-plan-select", 0xa78010, 0x11e, "a96f6a6c9d631df528d4c5393996bf737886f2e8aee72e10d509db8223e3b10a"},
        {"viewstate-plan-current", 0xa2cb90, 0x11c, "a61d665e2e1c62b9e0323674002827efe5165a8ef46383f5d4b45349e6f12912"},
        {"viewstate-target", 0xa41a20, 0x33, "9cc867a678075ca47384ae1ee291288a2159ea248fe12ee15a3f593a01f57945"},
        {"viewstate-camera-mode", 0x1d7dc10, 1513, "ba339ecec42d12233c0f9e6616979cc132775454009ea3f6029030cfed2ddcdb"}
    };
    for (unsigned i = 0; i < sizeof sites / sizeof sites[0]; ++i)
        if (!site_ok(&sites[i])) return 0;
    return 1;
}
/* Candidate-only memory observations; missing or unstable input never affects the overlay. */
static void ui_overlay_seat_pose(const char *stage) {
    const presentation_fork_request *request = &presentation_fork.request;
    const uintptr_t actors[2] = {request->player1, request->player2};
    const uintptr_t ships[2] = {presentation_fork.p1_ship, presentation_fork.p2_ship};
    const uintptr_t rooms[2] = {presentation_fork.p1_room, request->room};
    const char *roles[2] = {"p1", "p2"};
    for (unsigned index = 0; index < 2; ++index) {
        player_ui_seat_pose sample = {0};
        uint32_t bank = 2, bank_after = 2, view = 0, view_after = 0;
        unsigned char *tls = gate_tls_self();
        uint64_t runtime = 0;
        int ok = tls && gate_tls_worker(tls) &&
            presentation_fork_read(NULL, (uintptr_t)(tls + GATE_TLS_LINUX_VIEW), &view, 4) && view == 1 &&
            ui_overlay_owner(NULL, request->player1, request->player2, request->room,
                             ui_overlay_expected_bank(&ui_overlay_probe)) &&
            presentation_fork_read(NULL, (uintptr_t)LX(0x3e77430), &bank, 4) &&
            bank == ui_overlay_expected_bank(&ui_overlay_probe) &&
            rooms[index] <= UINTPTR_MAX - 0x70 && ships[index] <= UINTPTR_MAX - 8 &&
            presentation_fork_word(rooms[index] + 0x70, ships[index]) &&
            presentation_fork_read(NULL, ships[index] + 8, &runtime, 8) &&
            runtime == (index ? request->expected_p2_ship : request->expected_p1_ship) &&
            player_ui_seat_sample(presentation_fork_read, NULL, actors[index], bank, &sample) &&
            presentation_fork_word(rooms[index] + 0x70, ships[index]) &&
            presentation_fork_read(NULL, ships[index] + 8, &runtime, 8) &&
            runtime == (index ? request->expected_p2_ship : request->expected_p1_ship) &&
            presentation_fork_read(NULL, (uintptr_t)LX(0x3e77430), &bank_after, 4) && bank_after == bank &&
            gate_tls_self() == tls &&
            presentation_fork_read(NULL, (uintptr_t)(tls + GATE_TLS_LINUX_VIEW), &view_after, 4) && view_after == view &&
            ui_overlay_owner(NULL, request->player1, request->player2, request->room,
                             ui_overlay_expected_bank(&ui_overlay_probe));
        char line[768];
        int length = ok ? player_ui_seat_format_at(line, sizeof line, stats.issued, stage, roles[index], ships[index],
                                                rooms[index], bank, &sample) : 0;
        if (length) (void)write(2, line, (size_t)length);
        else dprintf(2, "X4 UI SEAT_POSE v=1 tick=%llu stage=%s role=%s status=unavailable\n", (unsigned long long)stats.issued, stage, roles[index]);
    }
}
static void ui_overlay_viewstate(const char *stage, native_ui_overlay_probe *probe, int pinned) {
    ui_overlay_seat_pose(stage);
    presentation_ui_plan before = {0}, after = {0};
    player_ui_viewstate sample = {0};
    uintptr_t universe = 0, universe_after = 0;
    if (!pinned || !presentation_ui_plan_collect(&before) || before.presenter + 8 != probe->object ||
        !presentation_fork_read(NULL, (uintptr_t)LX(0x3db6948), &universe, 8) ||
        universe != presentation_fork.universe ||
        !player_ui_viewstate_sample(presentation_native_read, NULL, universe, before.presenter,
                                    before.controller, &sample) ||
        !presentation_fork_read(NULL, (uintptr_t)LX(0x3db6948), &universe_after, 8) ||
        universe != universe_after || !presentation_ui_plan_collect(&after) || before.manager != after.manager ||
        before.presenter != after.presenter || before.controller != after.controller ||
        before.first_person != after.first_person || sample.first_person != before.first_person) {
        dprintf(2, "X4 UI VIEWSTATE v=1 tick=%llu stage=%s status=unavailable\n", (unsigned long long)stats.issued, stage);
        return;
    }
    char line[512];
    int length = player_ui_viewstate_format_at(line, sizeof line, stats.issued, stage, before.manager, before.presenter,
                                             before.controller, universe, &sample);
    if (length) (void)write(2, line, (size_t)length);
    else dprintf(2, "X4 UI VIEWSTATE v=1 tick=%llu stage=%s status=unavailable\n", (unsigned long long)stats.issued, stage);
}
static int ui_overlay_viewstate_pinned;
#include "gate_player_ui_plan_kick.h"
#include "gate_player_ui_seat_query.h"
#include "gate_player_live_pose_observer.h"
#include "player_camera_refill_preload_pin.h"
#include "gate_player_ui_seat_correction.h"
static player_camera_refill_api ui_camera_refill_api;
static player_projection_fov_scope_api ui_projection_scope_api;
static uintptr_t ui_camera_refill_target;
static uint64_t ui_camera_refill_last_tick;
static uint32_t ui_camera_refill_bank;
static int ui_camera_refill_enter(uintptr_t camera, uintptr_t output);
static int ui_camera_refill_leave(uintptr_t camera, uintptr_t output);
static int ui_game_render_enter(uintptr_t object);
static int ui_game_render_leave(uintptr_t object);
static int ui_overlay_generation_matches(unsigned completed, uint64_t tick, uint64_t generation) {
    uint64_t expected_tick = player_startup()->presentation_first + (ui_overlay_multi_count ? completed : 0);
    return (!ui_overlay_multi_count || completed < ui_overlay_multi_count) && tick == expected_tick && generation == expected_tick;
}
static int ui_overlay_enter_callback(uintptr_t object) {
    native_ui_overlay_probe *probe = &ui_overlay_probe;
    unsigned completed = atomic_load_explicit(&ui_overlay_completed, memory_order_acquire);
    if (!ui_overlay_generation_matches(completed, stats.issued, player_recurring_tail_token.generation) ||
        !player_recurring_physics_entered() || !player_physics_identity() ||
        object != probe->object || !presentation_fork.occupied) ui_overlay_die("entry_admission");
    ui_overlay_rearm_for_entry(probe, completed);
    if (!ui_overlay_bind_move_bank(probe)) ui_overlay_die("move_bank_bind");
    if (ui_correction_enabled) ui_correction_move_begin();
    if (!completed) {
        ui_overlay_viewstate_pinned = ui_overlay_viewstate_pins();
        ui_overlay_viewstate("before", probe, ui_overlay_viewstate_pinned);
    }
    probe->before = presentation_fork.occupied();
    if (probe->before != presentation_fork.request.expected_p1_ship) ui_overlay_die("entry_identity");
    if (!completed) ui_seat_query_once();
    if (ui_live_pose_enabled && !completed) ui_live_pose_bind();
    if (ui_correction_enabled && !completed) ui_correction_prepare();
    if (ui_game_render_enabled && !completed && !ui_game_render_activate())
        ui_overlay_die("game_render_activate");
    if (ui_camera_refill_enabled && !completed && !ui_camera_refill_api.activate())
        ui_overlay_die("camera_refill_activate");
    if (ui_projection_scope_enabled && !completed && !ui_projection_scope_api.activate())
        ui_overlay_die("projection_scope_activate");
    if (!ui_live_pose_advance(completed, stats.issued)) ui_overlay_die("live_pose_tick");
    if ((ui_seat_query_enabled && presentation_fork.occupied() != presentation_fork.request.expected_p1_ship) ||
        !player_ui_overlay_enter(&probe->lifecycle, object)) ui_overlay_die("entry");
    if (!player_view_render_capture_ui()) ui_overlay_die("view_capture");
    if (!completed) {
        ui_overlay_viewstate("during", probe, ui_overlay_viewstate_pinned);
        if (ui_plan_kick_enabled) ui_plan_kick_once(probe);
    }
    return 1;
}
static _Noreturn void ui_overlay_return_failure(const native_ui_overlay_probe *probe) {
    char detail[384];
    int length = snprintf(detail, sizeof detail,
        "X4 UI OVERLAY RETURN tick=%llu applies=%u restores=%u phase=%u "
        "before=%llu overlay=%llu paused=%llu resumed=%llu restored=%llu\n",
        (unsigned long long)stats.issued, probe->applies, probe->restores,
        __atomic_load_n(&probe->lifecycle.state, __ATOMIC_ACQUIRE),
        (unsigned long long)probe->before, (unsigned long long)probe->during,
        (unsigned long long)probe->paused, (unsigned long long)probe->resumed,
        (unsigned long long)probe->restored);
    if (length > 0 && (size_t)length < sizeof detail) (void)write(2, detail, (size_t)length);
    ui_overlay_die("return");
}

static int ui_overlay_leave_callback(uintptr_t object) {
    native_ui_overlay_probe *probe = &ui_overlay_probe;
    unsigned updates = probe->restores;
    if (!updates || updates > PLAYER_UI_OVERLAY_MAX_UPDATES || probe->applies != updates + 1)
        ui_overlay_return_failure(probe);
    probe->final_restore = 1;
    if (!player_ui_overlay_return(&probe->lifecycle, object) || probe->restores != updates + 1)
        ui_overlay_return_failure(probe);
    probe->final_restore = 0;
    unsigned ordinal = atomic_load_explicit(&ui_overlay_completed, memory_order_acquire) + 1;
    if (ui_correction_enabled && !ui_correction_move_closed(ordinal)) ui_overlay_die("move_bank_closed");
    if (!ui_game_render_enabled && ui_live_pose_enabled && ordinal == ui_overlay_multi_count) ui_live_pose_close();
    if (!ui_game_render_enabled && ui_correction_enabled && ordinal == ui_overlay_multi_count) ui_correction_close();
    if (ordinal == 1) ui_overlay_viewstate("after", probe, ui_overlay_viewstate_pinned);
    char frame[512];
    char *output = ui_overlay_multi_count ? frame : ui_overlay_output;
    int prefix = ui_overlay_multi_count ?
        snprintf(output, sizeof frame, "v=3 tick=%llu ordinal=%u count=%u ",
                 (unsigned long long)stats.issued, ordinal, ui_overlay_multi_count) :
        snprintf(output, sizeof frame, "v=2 status=pass tick=9 ");
    if (prefix < 0 || (size_t)prefix >= sizeof frame) ui_overlay_die("format");
    int suffix = snprintf(output + prefix, sizeof frame - (size_t)prefix,
        "updates=%u applies=%u restores=%u "
        "before=%llu overlay=%llu paused=%llu resumed=%llu restored=%llu "
        "p1=%llx p2=%llx room=%llx object=%llx canonical_restored=1",
        updates, probe->applies, probe->restores,
        (unsigned long long)probe->before, (unsigned long long)probe->during,
        (unsigned long long)probe->paused, (unsigned long long)probe->resumed,
        (unsigned long long)probe->restored, (unsigned long long)presentation_fork.request.player1,
        (unsigned long long)presentation_fork.request.player2, (unsigned long long)presentation_fork.request.room,
        (unsigned long long)object);
    if (suffix < 0 || (size_t)suffix >= sizeof frame - (size_t)prefix) ui_overlay_die("format");
    int length = prefix + suffix;
    if (length <= 0 || (size_t)length >= sizeof frame) ui_overlay_die("format");
    if (ui_overlay_multi_count) {
        if (ordinal > ui_overlay_multi_count || stats.issued != ordinal + player_startup()->presentation_first - 1) ui_overlay_die("sequence");
        dprintf(2, "X4 UI OVERLAY FRAME %s\n", frame);
        atomic_store_explicit(&ui_overlay_completed, ordinal, memory_order_release);
    } else {
        dprintf(2, "X4 UI OVERLAY RESULT %s\n", ui_overlay_output);
    }
    return 1;
}

/* Runs around the pinned GameRender VBI callback, never around Lua's Move. */
static int ui_game_render_sequence(unsigned completed, unsigned closed, uint64_t tick,
                                   uint64_t generation, uintptr_t object, uintptr_t known_object) {
    unsigned limit = player_session_budget.frames ? player_session_budget.presentation_count : 160u;
    return completed == closed + 1 && completed <= limit && tick == completed + player_startup()->presentation_first - 1 &&
        generation == tick && object && (!known_object || object == known_object);
}
static int ui_game_render_enter(uintptr_t object) {
    native_ui_overlay_probe *probe = &ui_overlay_probe;
    unsigned completed = atomic_load_explicit(&ui_overlay_completed, memory_order_acquire);
    uintptr_t table = 0;
    if (!ui_game_render_enabled || ui_game_render_active ||
        !ui_game_render_sequence(completed, atomic_load_explicit(&ui_game_render_closed, memory_order_acquire),
                                 stats.issued, player_recurring_tail_token.generation, object, ui_game_render_object) ||
        !player_recurring_physics_entered() || !player_physics_identity() ||
        !presentation_fork_read(NULL, object, &table, sizeof table) || table != (uintptr_t)LX(0x3ab1270u))
        ui_overlay_die("game_render_admission");
    ui_game_render_object = object;
    ui_overlay_rearm_for_entry(probe, 1);
    if (!ui_overlay_bind_move_bank(probe)) ui_overlay_die("game_render_bank");
    ui_correction_move_begin();
    ui_game_render_active = 1;
    if (presentation_fork.occupied() != presentation_fork.request.expected_p1_ship ||
        !player_ui_overlay_enter(&probe->lifecycle, probe->object)) ui_overlay_die("game_render_apply");
    return 1;
}
static int ui_game_render_leave(uintptr_t object) {
    native_ui_overlay_probe *probe = &ui_overlay_probe;
    uintptr_t table = 0;
    if (!ui_game_render_active || object != ui_game_render_object ||
        !presentation_fork_read(NULL, object, &table, sizeof table) || table != (uintptr_t)LX(0x3ab1270u) ||
        stats.issued != ui_game_render_closed + player_startup()->presentation_first || probe->applies != 1 || probe->restores)
        ui_overlay_die("game_render_return");
    probe->final_restore = 1;
    if (!player_ui_overlay_return(&probe->lifecycle, probe->object) || probe->restores != 1 ||
        !ui_overlay_canonical_matches(probe)) ui_overlay_die("game_render_restore");
    probe->final_restore = 0;
    uintptr_t observed = 0;
    if (!presentation_fork_read(NULL, ui_correction_move_slot, &observed, sizeof observed) ||
        observed != ui_correction_move_original || !ui_overlay_bank_matches(probe, probe->move_bank))
        ui_overlay_die("game_render_readback");
    ui_game_render_active = 0;
    unsigned ordinal = atomic_fetch_add_explicit(&ui_game_render_closed, 1, memory_order_release) + 1;
    dprintf(2, "X4 UI GAME_RENDER v=1 stage=frame tick=%llu ordinal=%u tid=%llu object=%llx p1=%llx p2=%llx bank=%u slot=%llx original=%llx observed=%llx restored=1\n",
            (unsigned long long)stats.issued, ordinal, (unsigned long long)ui_overlay_tid(NULL),
            (unsigned long long)object, (unsigned long long)presentation_fork.request.player1,
            (unsigned long long)presentation_fork.request.player2, probe->move_bank,
            (unsigned long long)ui_correction_move_slot, (unsigned long long)ui_correction_move_original,
            (unsigned long long)observed);
    if (ordinal == ui_overlay_multi_count) {
        if (!ui_camera_refill_enabled) {
            ui_live_pose_close();
            ui_correction_close();
        }
        dprintf(2, "X4 UI GAME_RENDER v=1 stage=close tick=%llu count=%u restored=1\n",
                (unsigned long long)stats.issued, ui_overlay_multi_count);
    }
    return 1;
}

/* The original refill supplies the camera and destination. We never invoke it
 * ourselves; this scope only surrounds the matching stock call. */
static int ui_camera_refill_identity(uintptr_t camera, uintptr_t output, uint32_t *bank_out) {
    unsigned char *tls = gate_tls_self();
    uint32_t view = 0, bank = 2, view_after = 0, bank_after = 2;
    uintptr_t current = 0, actor = 0, current_after = 0, actor_after = 0;
    if (!tls || !gate_tls_worker(tls) || camera != ui_camera_refill_target || camera > UINTPTR_MAX - 0x788 ||
        !presentation_fork_read(NULL, presentation_fork.universe + 0x3e8, &current, sizeof current) || current != camera ||
        !presentation_fork_read(NULL, camera + 0x780, &actor, sizeof actor) || actor != presentation_fork.request.player1 ||
        !presentation_fork_read(NULL, (uintptr_t)(tls + GATE_TLS_LINUX_VIEW), &view, 4) || view != 1 ||
        !presentation_fork_read(NULL, (uintptr_t)LX(0x3d8e2a8), &bank, 4) || bank > 1 ||
        output != camera + 0x20 + bank * 0x2c0 ||
        gate_tls_self() != tls ||
        !presentation_fork_read(NULL, (uintptr_t)(tls + GATE_TLS_LINUX_VIEW), &view_after, 4) || view_after != view ||
        !presentation_fork_read(NULL, (uintptr_t)LX(0x3d8e2a8), &bank_after, 4) || bank_after != bank ||
        !presentation_fork_read(NULL, presentation_fork.universe + 0x3e8, &current_after, sizeof current_after) || current_after != camera ||
        !presentation_fork_read(NULL, camera + 0x780, &actor_after, sizeof actor_after) || actor_after != actor) return 0;
    *bank_out = bank;
    return 1;
}
static int ui_camera_refill_enter(uintptr_t camera, uintptr_t output) {
    uint64_t first = player_startup()->presentation_first + 1;
    if (stats.issued < first) return 2; /* stock passthrough before this epoch */
    native_ui_overlay_probe *probe = &ui_overlay_probe;
    if (!ui_camera_refill_enabled || ui_camera_refill_active || ui_game_render_active ||
        stats.issued > player_startup()->presentation_first + ui_overlay_multi_count - 1 ||
        (ui_camera_refill_last_tick && stats.issued != ui_camera_refill_last_tick + 1) ||
        (!ui_camera_refill_last_tick && stats.issued != first) ||
        stats.issued != player_recurring_tail_token.generation ||
        !player_recurring_physics_entered() || !player_physics_identity() ||
        !ui_camera_refill_identity(camera, output, &ui_camera_refill_bank) ||
        !ui_live_pose_advance(1, stats.issued)) ui_overlay_die("camera_refill_admission");
    ui_overlay_rearm_for_entry(probe, 1);
    if (!ui_overlay_bind_move_bank(probe)) ui_overlay_die("camera_refill_bank");
    ui_correction_move_begin();
    ui_camera_refill_active = 1;
    if (presentation_fork.occupied() != presentation_fork.request.expected_p1_ship ||
        !player_ui_overlay_enter(&probe->lifecycle, probe->object)) ui_overlay_die("camera_refill_apply");
    return 1;
}
static int ui_camera_refill_leave(uintptr_t camera, uintptr_t output) {
    if (stats.issued < player_startup()->presentation_first + 1) return 1;
    native_ui_overlay_probe *probe = &ui_overlay_probe;
    uint32_t bank = 2;
    if (!ui_camera_refill_active || !ui_camera_refill_identity(camera, output, &bank) ||
        bank != ui_camera_refill_bank || probe->applies != 1 || probe->restores)
        ui_overlay_die("camera_refill_return");
    probe->final_restore = 1;
    if (!player_ui_overlay_return(&probe->lifecycle, probe->object) || probe->restores != 1 ||
        !ui_overlay_canonical_matches(probe)) ui_overlay_die("camera_refill_restore");
    probe->final_restore = 0;
    uintptr_t observed = 0;
    if (!presentation_fork_read(NULL, ui_correction_move_slot, &observed, sizeof observed) ||
        observed != ui_correction_move_original || !ui_overlay_bank_matches(probe, probe->move_bank))
        ui_overlay_die("camera_refill_readback");
    ui_camera_refill_active = 0;
    ui_camera_refill_last_tick = stats.issued;
    ++ui_camera_refill_closed;
    dprintf(2, "X4 UI CAMERA_REFILL v=1 stage=frame tick=%llu ordinal=%u camera=%llx output=%llx camera_bank=%u actor_bank=%u slot=%llx original=%llx observed=%llx restored=1\n",
            (unsigned long long)stats.issued, ui_camera_refill_closed, (unsigned long long)camera,
            (unsigned long long)output, bank, probe->move_bank, (unsigned long long)ui_correction_move_slot,
            (unsigned long long)ui_correction_move_original, (unsigned long long)observed);
    return 1;
}
/* The projection call follows the same frame's successfully restored refill.
 * Nonmatching calls retain their original behaviour without entering a lease. */
static int ui_projection_scope_sequence(uint64_t generation, uint64_t first,
                                        unsigned count, unsigned completed,
                                        uint64_t refill_tick, unsigned refill_closed,
                                        uint64_t previous_projection_tick) {
    return count > 1 && generation > first && generation - first < count &&
        refill_tick == generation && previous_projection_tick != generation &&
        generation - first == completed && refill_closed == completed;
}
static int ui_projection_scope_enter(uintptr_t camera) {
    uint64_t generation = stats.issued;
    unsigned completed = atomic_load_explicit(&ui_overlay_completed, memory_order_acquire);
    if (!ui_projection_scope_sequence(generation, player_startup()->presentation_first,
                                      ui_overlay_multi_count, completed, ui_camera_refill_last_tick,
                                      ui_camera_refill_closed, ui_projection_scope_last_tick)) return 2;
    if (!ui_projection_scope_enabled || ui_projection_scope_active || ui_camera_refill_active ||
        ui_game_render_active || stats.issued != player_recurring_tail_token.generation ||
        !player_recurring_physics_entered() || !player_physics_identity() ||
        camera != ui_camera_refill_target || ui_camera_refill_bank > 1 ||
        camera > UINTPTR_MAX - 0x5a0) ui_overlay_die("projection_scope_admission");
    uint32_t camera_bank = 2;
    uintptr_t output = camera + 0x20 + ui_camera_refill_bank * 0x2c0;
    if (!ui_camera_refill_identity(camera, output, &camera_bank) || camera_bank != ui_camera_refill_bank ||
        !ui_live_pose_advance(1, generation)) ui_overlay_die("projection_scope_identity");
    native_ui_overlay_probe *probe = &ui_overlay_probe;
    ui_overlay_rearm_for_entry(probe, 1);
    if (!ui_overlay_bind_move_bank(probe)) ui_overlay_die("projection_scope_bank");
    ui_correction_move_begin();
    ui_projection_scope_active = 1;
    if (presentation_fork.occupied() != presentation_fork.request.expected_p1_ship ||
        !player_ui_overlay_enter(&probe->lifecycle, probe->object)) ui_overlay_die("projection_scope_apply");
    return 1;
}
static int ui_projection_scope_leave(uintptr_t camera) {
    if (!ui_projection_scope_active) return 1;
    native_ui_overlay_probe *probe = &ui_overlay_probe;
    uint32_t bank = 2;
    uintptr_t output = camera + 0x20 + ui_camera_refill_bank * 0x2c0;
    if (!ui_camera_refill_identity(camera, output, &bank) || bank != ui_camera_refill_bank ||
        probe->applies != 1 || probe->restores) ui_overlay_die("projection_scope_return");
    probe->final_restore = 1;
    if (!player_ui_overlay_return(&probe->lifecycle, probe->object) || probe->restores != 1 ||
        !ui_overlay_canonical_matches(probe)) ui_overlay_die("projection_scope_restore");
    probe->final_restore = 0;
    uintptr_t observed = 0;
    if (!presentation_fork_read(NULL, ui_correction_move_slot, &observed, sizeof observed) ||
        observed != ui_correction_move_original || !ui_overlay_bank_matches(probe, probe->move_bank))
        ui_overlay_die("projection_scope_readback");
    ui_projection_scope_active = 0;
    ui_projection_scope_last_tick = stats.issued;
    ++ui_projection_scope_closed;
    dprintf(2, "X4 UI PROJECTION_FOV v=1 stage=frame tick=%llu ordinal=%u camera=%llx bank=%u restored=1\n",
            (unsigned long long)stats.issued, ui_projection_scope_closed,
            (unsigned long long)camera, bank);
    return 1;
}

static void player_ui_camera_refill_tail(uint64_t generation) {
    if (!ui_camera_refill_enabled) return;
    uint64_t first = player_startup()->presentation_first + 1;
    uint64_t last = player_startup()->presentation_first + ui_overlay_multi_count - 1;
    if (generation < first || generation > last) return;
    if (ui_camera_refill_active || ui_camera_refill_last_tick != generation)
        ui_overlay_die("camera_refill_missing");
    if (generation == last) {
        if (ui_camera_refill_closed != ui_overlay_multi_count - 1 || !ui_camera_refill_api.disarm())
            ui_overlay_die("camera_refill_close");
        if (ui_projection_scope_enabled &&
            (ui_projection_scope_active || ui_projection_scope_closed != ui_overlay_multi_count - 1 ||
             !ui_projection_scope_api.disarm())) ui_overlay_die("projection_scope_close");
        if (ui_projection_scope_enabled)
            dprintf(2, "X4 UI PROJECTION_FOV v=1 stage=close tick=%llu count=%u restored=1\n",
                    (unsigned long long)generation, ui_projection_scope_closed);
        ui_live_pose_close();
        ui_correction_close();
        dprintf(2, "X4 UI CAMERA_REFILL v=1 stage=close tick=%llu count=%u restored=1\n",
                (unsigned long long)generation, ui_camera_refill_closed);
    }
}

static void player_ui_overlay_main_interlock(void) {
    if (!atomic_load_explicit(&ui_overlay_armed, memory_order_acquire)) return;
    unsigned state = __atomic_load_n(&ui_overlay_probe.lifecycle.state, __ATOMIC_ACQUIRE);
    uint64_t generation = atomic_load_explicit(&player_recurring_main_tick, memory_order_acquire);
    if (ui_overlay_multi_count) {
        unsigned completed = atomic_load_explicit(&ui_overlay_completed, memory_order_acquire);
        unsigned passes = atomic_fetch_add_explicit(&ui_overlay_main_passes, 1, memory_order_acq_rel);
        unsigned render_closed = atomic_load_explicit(&ui_game_render_closed, memory_order_acquire);
        if (ui_game_render_enabled &&
            (render_closed != completed || !ui_game_render_progress ||
             ui_game_render_progress() != (completed == 0 ? 4U : completed == ui_overlay_multi_count ? 3U : 1U)))
            ui_overlay_die("game_render_interlock");
        if (!stats.active || !ui_overlay_progress || generation < player_startup()->presentation_arm ||
            (generation <= player_startup()->presentation_arm + ui_overlay_multi_count &&
             (passes != generation - player_startup()->presentation_arm || completed != generation - player_startup()->presentation_arm)) ||
            (generation > player_startup()->presentation_arm + ui_overlay_multi_count && completed != ui_overlay_multi_count) ||
            (completed == 0 ? state != PLAYER_UI_OVERLAY_NEW : state != PLAYER_UI_OVERLAY_CLOSED) ||
            ui_overlay_progress() != (completed == ui_overlay_multi_count ? 3U : 1U)) ui_overlay_die("main_interlock");
        return;
    }
    if (state == PLAYER_UI_OVERLAY_NEW && stats.active && generation == player_startup()->presentation_arm &&
        atomic_fetch_add_explicit(&ui_overlay_main_passes, 1, memory_order_acq_rel) == 0) return;
    if (!player_ui_overlay_frame_interlock(&ui_overlay_probe.lifecycle)) ui_overlay_die("main_interlock");
}

static void ui_overlay_pause_update(native_ui_overlay_probe *probe) {
    if (probe->restores >= PLAYER_UI_OVERLAY_MAX_UPDATES) ui_overlay_die("update_limit");
    if (!player_ui_overlay_pause(&probe->lifecycle, probe->object)) ui_overlay_die("pause");
}

static int lua_pilot_present_pause(lua_State *state) {
    if (!atomic_load_explicit(&ui_overlay_armed, memory_order_acquire)) {
        p_lua_pushnumber(state, 0);
        return 1;
    }
    unsigned phase = __atomic_load_n(&ui_overlay_probe.lifecycle.state, __ATOMIC_ACQUIRE);
    if (phase == PLAYER_UI_OVERLAY_NEW || phase == PLAYER_UI_OVERLAY_CLOSED) {
        p_lua_pushnumber(state, 0);
        return 1;
    }
    ui_overlay_pause_update(&ui_overlay_probe);
    p_lua_pushnumber(state, 1);
    return 1;
}
static int lua_pilot_present_resume(lua_State *state) {
    if (!atomic_load_explicit(&ui_overlay_armed, memory_order_acquire)) {
        p_lua_pushnumber(state, 0);
        return 1;
    }
    if (!player_ui_overlay_resume(&ui_overlay_probe.lifecycle, ui_overlay_probe.object)) ui_overlay_die("resume");
    p_lua_pushnumber(state, 1);
    return 1;
}

static int lua_player_ui_overlay_abort(lua_State *state) {
    (void)state;
    ui_overlay_die("lua_frame_failed");
}

static int lua_player_ui_overlay_arm(lua_State *state) {
    const char *enabled = getenv("X4_DEV_UI_OVERLAY");
    const char *eight = getenv("X4_DEV_UI_OVERLAY_EIGHT");
    const char *long_run = getenv("X4_DEV_UI_OVERLAY_160");
    const char *plan_kick = getenv("X4_DEV_UI_PLAN_KICK");
    const char *seat_query = getenv("X4_DEV_UI_SEAT_QUERY");
    const char *live_pose = getenv("X4_DEV_UI_LIVE_POSE_OBSERVER");
    const char *seat_correction = getenv("X4_DEV_UI_SEAT_CORRECTION");
    const char *render_lease = getenv("X4_DEV_UI_RENDER_LEASE");
    const char *camera_refill = getenv("X4_DEV_UI_CAMERA_REFILL");
    int eight_mode = eight && !strcmp(eight, "1");
    int session_mode = player_session_budget.frames != 0;
    int long_mode = (long_run && !strcmp(long_run, "1")) || session_mode;
    int single_mode = enabled && !strcmp(enabled, "1");
    const char *sha = NULL; uint64_t size = 0;
    player_view_query_api provider;
    player_ui_callback_api callbacks;
    player_ui_callback_snapshot snapshot = {0};
    presentation_ui_plan plan = {0};
    long tid = syscall(SYS_gettid);
    if ((eight && !eight_mode) || (enabled && !single_mode) || (long_run && !long_mode) ||
        (plan_kick && (strcmp(plan_kick, "1") || !long_mode)) ||
        (seat_query && (strcmp(seat_query, "1") || !long_mode)) ||
        (live_pose && (strcmp(live_pose, "1") || !seat_query || !long_mode)) ||
        (seat_correction && (strcmp(seat_correction, "1") || !live_pose || !seat_query || !long_mode)) ||
        (render_lease && (strcmp(render_lease, "1") || !seat_correction)) ||
        (camera_refill && (strcmp(camera_refill, "1") || !render_lease)) ||
        (session_mode && (long_run || !render_lease || !seat_correction || player_recurring_physics != 3 ||
                          player_recurring_end != player_session_budget.recurring_end)) ||
        single_mode + eight_mode + long_mode != 1 ||
        atomic_load(&ui_overlay_armed) ||
        !accept_ui() || !stats.active || stats.issued != player_startup()->presentation_arm || stats.slot != 2 ||
        !stock_observer_worker_boundary() || !gate_tls_worker(gate_tls_self()) ||
        !player_recurring_physics_entered() || !player_physics_identity() ||
        atomic_load_explicit(&presentation_fork_state, memory_order_acquire) != 2 ||
        !atomic_load_explicit(&presentation_fork_result_read, memory_order_acquire) ||
        tid <= 0 || tid > UINT32_MAX || tid != presentation_fork.worker_tid ||
        !presentation_ui_plan_collect(&plan) || plan.presenter > UINTPTR_MAX - 8 ||
        !view_query_expected_library(&sha, &size) || !player_view_query_provider(sha, size, &provider) ||
        !player_ui_callback_provider(&provider, &callbacks) || !callbacks.snapshot(&snapshot) ||
        snapshot.object != plan.presenter + 8 || snapshot.owner_tid != (uint32_t)tid ||
        !presentation_fork_owner(&presentation_fork, presentation_fork.request.player1,
            presentation_fork.request.player2, presentation_fork.request.room, presentation_fork.request.bank)) {
        p_lua_pushnumber(state, 0);
        return 1;
    }
    player_ui_overlay_bind_fn bind = NULL;
    player_ui_overlay_bind_eight_fn bind_eight = NULL;
    player_ui_overlay_bind_count_fn bind_count = NULL;
    if (long_mode) {
        if (!player_ui_overlay_count_provider(&provider, &bind_count, &ui_overlay_progress)) {
            p_lua_pushnumber(state, 0);
            return 1;
        }
    } else if (eight_mode) {
        if (!player_ui_overlay_eight_provider(&provider, &bind_eight, &ui_overlay_progress)) {
            p_lua_pushnumber(state, 0);
            return 1;
        }
    } else {
        bind = player_ui_overlay_bind_provider(&provider);
    }
    if (live_pose && !player_live_pose_provider(&provider, &ui_live_pose_api)) {
        p_lua_pushnumber(state, 0);
        return 1;
    }
    if (seat_correction && !player_live_correction_provider(&ui_live_pose_api, &ui_correction_api)) {
        p_lua_pushnumber(state, 0);
        return 1;
    }
    if (render_lease && !player_game_render_provider(&provider, &ui_game_render_bind, &ui_game_render_activate, &ui_game_render_progress)) {
        p_lua_pushnumber(state, 0);
        return 1;
    }
    if (camera_refill && !player_camera_refill_provider(&provider, &ui_camera_refill_api)) {
        p_lua_pushnumber(state, 0);
        return 1;
    }
    const char *projection_scope = getenv("X4_DEV_UI_PROJECTION_FOV_SCOPE");
    if (projection_scope && (strcmp(projection_scope, "1") || !camera_refill || !render_lease || !session_mode ||
        !player_projection_fov_scope_provider(&provider, &ui_projection_scope_api))) {
        p_lua_pushnumber(state, 0);
        return 1;
    }
    ui_projection_scope_enabled = projection_scope != NULL;
    ui_camera_refill_enabled = camera_refill != NULL;
    ui_game_render_enabled = render_lease != NULL;
    ui_correction_enabled = seat_correction != NULL;
    ui_live_pose_enabled = live_pose != NULL;
    ui_overlay_multi_count = session_mode ? player_session_budget.presentation_count :
                             long_mode ? 160U : (eight_mode ? 8U : 0U);
    ui_plan_kick_enabled = plan_kick != NULL;
    ui_seat_query_enabled = seat_query != NULL;
    native_ui_overlay_probe *probe = &ui_overlay_probe;
    memset(probe, 0, sizeof *probe);
    probe->object = plan.presenter + 8;
    probe->operations = (player_presentation_overlay_ops){probe, ui_overlay_owner, ui_overlay_read, ui_overlay_write};
    const player_ui_overlay_ops lifecycle = {probe, ui_overlay_tid, ui_overlay_apply, ui_overlay_restore, ui_overlay_fatal};
    if ((!bind && !bind_eight && !bind_count) || !player_ui_overlay_init(&probe->lifecycle, &lifecycle, (uint64_t)tid, probe->object)) {
        p_lua_pushnumber(state, 0);
        return 1;
    }
    if (session_mode && !ui_correction_allocate_session()) ui_overlay_die("session_scratch");
    if (ui_game_render_enabled &&
        !ui_game_render_bind((uint32_t)tid, ui_game_render_enter, ui_game_render_leave))
        ui_overlay_die("game_render_bind");
    if (ui_camera_refill_enabled) {
        uintptr_t actor = 0;
        if (!presentation_fork_read(NULL, presentation_fork.universe + 0x3e8, &ui_camera_refill_target, sizeof ui_camera_refill_target) ||
            !ui_camera_refill_target ||
            !presentation_fork_read(NULL, ui_camera_refill_target + 0x780, &actor, sizeof actor) ||
            actor != presentation_fork.request.player1 ||
            !ui_camera_refill_api.bind((uint32_t)tid, ui_camera_refill_target,
                                       ui_camera_refill_enter, ui_camera_refill_leave))
            ui_overlay_die("camera_refill_bind");
    }
    if (ui_projection_scope_enabled &&
        !ui_projection_scope_api.bind((uint32_t)tid, ui_camera_refill_target,
                                      ui_projection_scope_enter, ui_projection_scope_leave))
        ui_overlay_die("projection_scope_bind");
    atomic_store_explicit(&ui_overlay_armed, 1, memory_order_release);
    int bound = long_mode ?
        bind_count((uint32_t)tid, probe->object, ui_overlay_enter_callback, ui_overlay_leave_callback, ui_overlay_multi_count) :
        eight_mode ? bind_eight((uint32_t)tid, probe->object, ui_overlay_enter_callback, ui_overlay_leave_callback) :
        bind((uint32_t)tid, probe->object, ui_overlay_enter_callback, ui_overlay_leave_callback);
    if (!bound) ui_overlay_die("bind");
    p_lua_pushnumber(state, 1);
    return 1;
}

static int lua_player_ui_overlay_result(lua_State *state) {
    if (!accept_ui() || !stats.active || stats.issued != (player_startup()->presentation_first + (ui_overlay_multi_count ? ui_overlay_multi_count : 1)) || stats.slot != 2 ||
        !stock_observer_worker_boundary() || !gate_tls_worker(gate_tls_self()) ||
        syscall(SYS_gettid) != (long)ui_overlay_probe.lifecycle.owner_tid || !player_physics_identity() ||
        !atomic_load_explicit(&ui_overlay_armed, memory_order_acquire) ||
        __atomic_load_n(&ui_overlay_probe.lifecycle.state, __ATOMIC_ACQUIRE) != PLAYER_UI_OVERLAY_CLOSED ||
        (ui_overlay_multi_count && (atomic_load_explicit(&ui_overlay_completed, memory_order_acquire) != ui_overlay_multi_count ||
                              !ui_overlay_progress || ui_overlay_progress() != 3)) ||
        (ui_game_render_enabled && (atomic_load_explicit(&ui_game_render_closed, memory_order_acquire) != ui_overlay_multi_count ||
                                    !ui_game_render_progress || ui_game_render_progress() != 3)) ||
        atomic_exchange_explicit(&ui_overlay_result_read, 1, memory_order_acq_rel) ||
        (!ui_overlay_multi_count && !ui_overlay_output[0])) {
        p_lua_pushnil(state);
    } else {
        if (ui_overlay_multi_count) {
            snprintf(ui_overlay_output, sizeof ui_overlay_output,
                     "v=3 status=pass first=%llu last=%llu count=%u",
                     (unsigned long long)player_startup()->presentation_first,
                     (unsigned long long)(player_startup()->presentation_arm + ui_overlay_multi_count), ui_overlay_multi_count);
            dprintf(2, "X4 UI OVERLAY RESULT %s\n", ui_overlay_output);
        }
        p_lua_pushlstring(state, ui_overlay_output, strlen(ui_overlay_output));
    }
    return 1;
}
#endif
