#ifndef X4_GATE_PLAYER_UI_PLAN_KICK_H
#define X4_GATE_PLAYER_UI_PLAN_KICK_H

#include "player_ui_plan_kick.h"

/* Only touched by the authenticated UI/physics worker. */
static input_context_census_entries ui_plan_kick_entries;
static uintptr_t ui_plan_kick_presenter;
static uint64_t *ui_plan_kick_rng;
static int ui_plan_kick_enabled;

static int ui_plan_kick_pins(void) {
    static const lx_site sites[] = {
        {"kick-cockpit", 0xa78130, 0x66, "fb9fb4bdf42e7d81a9dc202cff6e005eb4472a468ec2e82ddddb22fd0aad62a4"},
        {"kick-firstperson", 0xa781a0, 0x66, "3f837e21bca5a488efbdf35a86016437dc12ad836ca66de89eca8bc9c4f41955"},
        {"kick-owner-allocation", 0xf4d325, 63, "b1a1ddb04483fcb743e711e08d8dfa6ca55a048ffb496b17a98f4b68e4923af3"},
        {"kick-owner-lock-init", 0xf4d4d2, 124, "472e09a11ad90269ac1fd27b92249980c3361ce23ded67f450faa6d25228471a"},
        {"kick-recursive-lock", 0x1a23520, 51, "9fc30df3cf40de77df3cb6ec7ce7f5e0fdcff5ad827708e7b68abc41ca1cec24"},
        {"kick-lock", 0x1a23650, 20, "038af7d8eb54c7ed6b55e30ca283090ef065f0c80d0622fc394eb9041748890d"},
        {"kick-unlock", 0x1a236a0, 20, "1748e005766dbd9545ae98502ba01e74e2e66656756e59a7c1865597bc3c00f4"}
    };
    for (unsigned i = 0; i < sizeof sites / sizeof sites[0]; ++i)
        if (!site_ok(&sites[i])) return 0;
    return 1;
}
static int ui_plan_kick_capture(void *context, player_ui_plan_kick_sample *out) {
    native_ui_overlay_probe *probe = context;
    presentation_ui_plan before = {0}, after = {0};
    uintptr_t universe = 0, universe_after = 0;
    input_context_census_sample census = {0};
    const input_context_census_operations operations = {NULL, player_seat_preflight_read};
    unsigned char digest[32];
    memset(out, 0, sizeof *out);
    unsigned char *tls = gate_tls_self();
    if (!tls || !gate_tls_worker(tls) || gate_tls_rng(tls) != ui_plan_kick_rng ||
        !ui_overlay_owner(NULL, presentation_fork.request.player1, presentation_fork.request.player2,
                          presentation_fork.request.room, presentation_fork.request.bank) ||
        !presentation_ui_plan_collect(&before) || before.presenter != ui_plan_kick_presenter ||
        before.presenter + 8 != probe->object ||
        !presentation_fork_read(NULL, (uintptr_t)LX(0x3db6948), &universe, 8) ||
        universe != presentation_fork.universe ||
        !player_ui_viewstate_sample(presentation_native_read, NULL, universe, before.presenter,
                                    before.controller, &out->view) ||
        !sko_session.owner || sko_session.owner > UINTPTR_MAX - 0x10 ||
        !input_context_census_collect_entries(&operations, sko_session.owner + 0x10, &census,
                                              &ui_plan_kick_entries) ||
        !census.tree_valid || census.node_count != ui_plan_kick_entries.count ||
        !presentation_fork_read(NULL, (uintptr_t)LX(0x3db6948), &universe_after, 8) ||
        universe != universe_after || !presentation_ui_plan_collect(&after) ||
        before.manager != after.manager || before.presenter != after.presenter ||
        before.controller != after.controller || before.first_person != after.first_person ||
        out->view.first_person != before.first_person || gate_tls_self() != tls ||
        gate_tls_rng(tls) != ui_plan_kick_rng) return 0;
    out->manager = before.manager;
    out->presenter = before.presenter;
    out->controller = before.controller;
    out->context_count = ui_plan_kick_entries.count;
    lockstep_sha256(ui_plan_kick_entries.payloads, out->context_count * 16u, digest);
    hex32(digest, out->context_hash);
    out->rng = *ui_plan_kick_rng;
    return 1;
}
static int ui_plan_kick_rng_valid(void *context, uint64_t *word) {
    (void)context;
    unsigned char *tls = gate_tls_self();
    return tls && gate_tls_worker(tls) && gate_tls_rng(tls) == word && word == ui_plan_kick_rng &&
        player_recurring_physics_entered() && player_physics_identity();
}
static void ui_plan_kick_handler(void *context, int first_person) {
    (void)context;
    ((void (*)(void *))LX(first_person ? 0xa781a0 : 0xa78130))((void *)ui_plan_kick_presenter);
}
static void ui_plan_kick_emit(void *context, const char *stage, const player_ui_plan_kick_sample *sample) {
    (void)context;
    char plan[2 * PLAYER_UI_VIEWSTATE_PLAN_MAX + 1];
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < sample->view.plan_length; ++i) {
        plan[i * 2] = hex[sample->view.plan[i] >> 4];
        plan[i * 2 + 1] = hex[sample->view.plan[i] & 15];
    }
    plan[sample->view.plan_length * 2] = 0;
    dprintf(2, "X4 UI PLAN KICK v=1 stage=%s tick=9 manager=%llx presenter=%llx controller=%llx "
        "selector=%u first=%u plan_len=%zu plan=%s rng=%016llx contexts=%u context_hash=%s "
        "camera_candidate=%llx camera_mode=%u external_target=%llx target_id=%llu\n",
        stage, (unsigned long long)sample->manager, (unsigned long long)sample->presenter,
        (unsigned long long)sample->controller, sample->view.selector, (unsigned)sample->view.first_person,
        sample->view.plan_length, plan, (unsigned long long)sample->rng, sample->context_count,
        sample->context_hash, (unsigned long long)sample->view.camera, sample->view.camera_mode,
        (unsigned long long)sample->view.target, (unsigned long long)sample->view.target_id);
}
static int ui_plan_kick_trylock(uintptr_t presenter) {
    unsigned char lock_check[sizeof(pthread_mutex_t)];
    if (!presenter || presenter > UINTPTR_MAX - 0x80 ||
        !presentation_fork_read(NULL, presenter + 0x80, lock_check, sizeof lock_check) ||
        !fs_mutex_ok((unsigned char *)presenter + 0x78)) return 0;
    return pthread_mutex_trylock((pthread_mutex_t *)(presenter + 0x80)) == 0;
}
static void ui_plan_kick_once(native_ui_overlay_probe *probe) {
    presentation_ui_plan plan = {0};
    if (!ui_plan_kick_enabled || stats.issued != 9 || ui_overlay_multi_count != 160 ||
        !ui_overlay_viewstate_pinned || !ui_plan_kick_pins() ||
        !presentation_ui_plan_collect(&plan) || plan.presenter + 8 != probe->object ||
        !player_recurring_physics_entered() || !player_physics_identity() ||
        !(ui_plan_kick_rng = gate_tls_rng(gate_tls_self()))) ui_overlay_die("plan_kick_admission");
    ui_plan_kick_presenter = plan.presenter;
    pthread_mutex_t *mutex = (pthread_mutex_t *)(plan.presenter + 0x80);
    if (!ui_plan_kick_trylock(plan.presenter)) ui_overlay_die("plan_kick_lock");
    player_ui_plan_kick_sample samples[3] = {0};
    uint64_t restored = 0;
    const player_ui_plan_kick_ops operations = {probe, ui_plan_kick_capture, ui_plan_kick_handler,
                                                ui_plan_kick_emit, ui_plan_kick_rng_valid, ui_plan_kick_rng};
    int ok = player_ui_plan_kick_run(&operations, samples, &restored);
    if (pthread_mutex_unlock(mutex)) ui_overlay_die("plan_kick_unlock");
    if (!ok) ui_overlay_die("plan_kick_sequence");
    int context_changed = samples[0].context_count != samples[2].context_count ||
                          strcmp(samples[0].context_hash, samples[2].context_hash);
    int camera_changed = samples[0].view.camera != samples[2].view.camera ||
        samples[0].view.camera_mode != samples[2].view.camera_mode ||
        samples[0].view.target != samples[2].view.target || samples[0].view.target_id != samples[2].view.target_id;
    dprintf(2, "X4 UI PLAN KICK v=1 stage=result tick=9 success=1 rng_before=%016llx "
        "rng_after=%016llx rng_restored=%016llx context_changed=%d camera_changed=%d\n",
        (unsigned long long)samples[0].rng, (unsigned long long)samples[2].rng,
        (unsigned long long)restored, !!context_changed, !!camera_changed);
}
#endif
