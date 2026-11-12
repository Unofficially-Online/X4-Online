#ifndef X4_GATE_PLAYER_LIVE_POSE_OBSERVER_H
#define X4_GATE_PLAYER_LIVE_POSE_OBSERVER_H

/* Disposable observation of genuine FPC calls. The target is a pointer value,
 * not a retained object; pointer reuse and renderer selection remain unknown. */
#include "player_live_pose_preload_pin.h"

static int ui_live_pose_enabled;
static player_live_pose_api ui_live_pose_api;
static player_live_pose_snapshot ui_live_pose_result;
static player_live_pose_scope_snapshot ui_live_pose_scope_result;
static uintptr_t ui_live_pose_target;

static int ui_live_pose_mark_begin(unsigned state) {
    return !ui_live_pose_enabled || (ui_live_pose_api.scope_begin && ui_live_pose_api.scope_begin(state));
}
static int ui_live_pose_mark(unsigned state) {
    return !ui_live_pose_enabled || (ui_live_pose_api.scope && ui_live_pose_api.scope(state));
}

static int ui_live_pose_advance(unsigned completed, uint64_t generation) {
    if (!ui_live_pose_enabled) return 1;
    if (!completed) return generation == 9;
    return ui_live_pose_api.tick && ui_live_pose_api.tick(generation);
}

static void ui_live_pose_bind(void) {
    const presentation_fork_request *request = &presentation_fork.request;
    player_ui_seat_pose sample = {0};
    uint32_t bank = 2;
    unsigned char *tls = gate_tls_self();
    uint32_t view = 0, view_after = 0, bank_after = 2;
    long tid = syscall(SYS_gettid);
    if (!tls || !gate_tls_worker(tls) ||
        !presentation_fork_read(NULL, (uintptr_t)(tls + GATE_TLS_LINUX_VIEW), &view, 4) || view != 1 ||
        !presentation_fork_read(NULL, (uintptr_t)LX(0x3e77430), &bank, 4) || bank != ui_overlay_expected_bank(&ui_overlay_probe) ||
        !ui_overlay_owner(NULL, request->player1, request->player2, request->room, ui_overlay_expected_bank(&ui_overlay_probe)) ||
        !player_ui_seat_sample(presentation_fork_read, NULL, request->player1, bank, &sample) ||
        !sample.controller || sample.vtable != (uintptr_t)LX(0x3b5edf8) ||
        gate_tls_self() != tls ||
        !presentation_fork_read(NULL, (uintptr_t)(tls + GATE_TLS_LINUX_VIEW), &view_after, 4) || view_after != view ||
        !presentation_fork_read(NULL, (uintptr_t)LX(0x3e77430), &bank_after, 4) || bank_after != bank ||
        !ui_overlay_owner(NULL, request->player1, request->player2, request->room, ui_overlay_expected_bank(&ui_overlay_probe)) ||
        tid <= 0 || (unsigned long)tid > UINT32_MAX ||
        !ui_live_pose_api.bind((uint32_t)tid, sample.controller)) ui_overlay_die("live_pose_bind");
    ui_live_pose_target = sample.controller;
    dprintf(2, "X4 UI LIVE_POSE v=1 stage=bind tick=9 target=%llx lifetime=pointer_only\n",
            (unsigned long long)ui_live_pose_target);
}

static void ui_live_pose_hex(const unsigned char *bytes, char output[129]) {
    static const char digits[] = "0123456789abcdef";
    for (unsigned index = 0; index < 64; ++index) {
        output[index * 2] = digits[bytes[index] >> 4];
        output[index * 2 + 1] = digits[bytes[index] & 15];
    }
    output[128] = '\0';
}

static void ui_live_pose_close(void) {
    if (!ui_live_pose_api.finish(&ui_live_pose_result) || !ui_live_pose_result.closed ||
        ui_live_pose_result.started != ui_live_pose_result.returned || ui_live_pose_result.nested ||
        ui_live_pose_result.count > PLAYER_LIVE_POSE_ROWS ||
        ui_live_pose_result.started != ui_live_pose_result.count + ui_live_pose_result.overflow ||
        ui_live_pose_result.started != ui_live_pose_result.phase_move + ui_live_pose_result.phase_render +
            ui_live_pose_result.phase_both + ui_live_pose_result.phase_other ||
        ui_live_pose_result.started != ui_live_pose_result.mode_zero + ui_live_pose_result.mode_one +
            ui_live_pose_result.mode_other) ui_overlay_die("live_pose_close");
    for (unsigned index = 0; index < ui_live_pose_result.count; ++index) {
        const player_live_pose_row *row = &ui_live_pose_result.rows[index];
        char before[129], after[129];
        ui_live_pose_hex(row->before, before);
        ui_live_pose_hex(row->after, after);
        dprintf(2, "X4 UI LIVE_POSE v=1 stage=sample index=%u last_move=%llu caller=%llx "
                "time=%016llx mode=%u move=%u render=%u before_valid=%u after_valid=%u before=%s after=%s\n",
                index, (unsigned long long)row->last_move, (unsigned long long)row->caller,
                (unsigned long long)row->time_bits, row->mode, row->move_depth, row->render_depth,
                row->before_valid, row->after_valid, before, after);
    }
    dprintf(2, "X4 UI LIVE_POSE v=1 stage=close tick=168 started=%llu returned=%llu nested=%llu "
            "rows=%u omitted=%llu move=%llu render=%llu both=%llu other=%llu mode0=%llu mode1=%llu "
            "mode_other=%llu foreign=unobserved closed=1 lifetime=pointer_only\n",
            (unsigned long long)ui_live_pose_result.started, (unsigned long long)ui_live_pose_result.returned,
            (unsigned long long)ui_live_pose_result.nested, ui_live_pose_result.count,
            (unsigned long long)ui_live_pose_result.overflow, (unsigned long long)ui_live_pose_result.phase_move,
            (unsigned long long)ui_live_pose_result.phase_render, (unsigned long long)ui_live_pose_result.phase_both,
            (unsigned long long)ui_live_pose_result.phase_other, (unsigned long long)ui_live_pose_result.mode_zero,
            (unsigned long long)ui_live_pose_result.mode_one, (unsigned long long)ui_live_pose_result.mode_other);
    if (!ui_live_pose_api.scope_result(&ui_live_pose_scope_result) ||
        ui_live_pose_scope_result.state != PLAYER_LIVE_POSE_OUTSIDE || ui_live_pose_scope_result.closes != 160 ||
        ui_live_pose_scope_result.opens != ui_live_pose_scope_result.closes + ui_live_pose_scope_result.pauses ||
        ui_live_pose_scope_result.counts[PLAYER_LIVE_POSE_OUTSIDE] +
            ui_live_pose_scope_result.counts[PLAYER_LIVE_POSE_OPEN] +
            ui_live_pose_scope_result.counts[PLAYER_LIVE_POSE_PAUSED] +
            ui_live_pose_scope_result.counts[PLAYER_LIVE_POSE_CHANGING] != ui_live_pose_result.started)
        ui_overlay_die("live_pose_scope_close");
    static const char *names[] = {"outside", "open", "paused", "changing"};
    for (unsigned scope = 0; scope < PLAYER_LIVE_POSE_SCOPE_COUNT; ++scope) {
        if (ui_live_pose_scope_result.count[scope] > PLAYER_LIVE_POSE_SCOPE_ROWS ||
            ui_live_pose_scope_result.count[scope] !=
                (ui_live_pose_scope_result.counts[scope] < PLAYER_LIVE_POSE_SCOPE_ROWS ?
                 ui_live_pose_scope_result.counts[scope] : PLAYER_LIVE_POSE_SCOPE_ROWS))
            ui_overlay_die("live_pose_scope_count");
        for (unsigned index = 0; index < ui_live_pose_scope_result.count[scope]; ++index) {
            const player_live_pose_scope_row *representative = &ui_live_pose_scope_result.rows[scope][index];
            const player_live_pose_row *row = &representative->row;
            char before[129], after[129];
            ui_live_pose_hex(row->before, before);
            ui_live_pose_hex(row->after, after);
            dprintf(2, "X4 UI POSE_SCOPE v=1 stage=sample scope=%s ordinal=%llu last_move=%llu caller=%llx "
                    "time=%016llx mode=%u move=%u render=%u before_valid=%u after_valid=%u before=%s after=%s\n",
                    names[scope], (unsigned long long)representative->ordinal, (unsigned long long)row->last_move,
                    (unsigned long long)row->caller, (unsigned long long)row->time_bits, row->mode,
                    row->move_depth, row->render_depth, row->before_valid, row->after_valid, before, after);
        }
    }
    dprintf(2, "X4 UI POSE_SCOPE v=1 stage=close tick=168 outside=%llu open=%llu paused=%llu changing=%llu "
            "opens=%llu pauses=%llu closes=%llu state=outside\n",
            (unsigned long long)ui_live_pose_scope_result.counts[PLAYER_LIVE_POSE_OUTSIDE],
            (unsigned long long)ui_live_pose_scope_result.counts[PLAYER_LIVE_POSE_OPEN],
            (unsigned long long)ui_live_pose_scope_result.counts[PLAYER_LIVE_POSE_PAUSED],
            (unsigned long long)ui_live_pose_scope_result.counts[PLAYER_LIVE_POSE_CHANGING],
            (unsigned long long)ui_live_pose_scope_result.opens, (unsigned long long)ui_live_pose_scope_result.pauses,
            (unsigned long long)ui_live_pose_scope_result.closes);
}
#endif
