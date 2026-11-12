#ifndef X4_GATE_PLAYER_UI_SEAT_CORRECTION_H
#define X4_GATE_PLAYER_UI_SEAT_CORRECTION_H

#include <pthread.h>
#include "player_ui_published_pose.h"

/* Bounded disposable experiment. Every scratch block is immutable once
 * published and remains resident until process exit. This does not exclude
 * concurrent readers or establish the lifetime of the P1 controller. */
#define UI_CORRECTION_CAPACITY (160u * (PLAYER_UI_OVERLAY_MAX_UPDATES + 1u))
static _Alignas(16) float ui_correction_scratch[UI_CORRECTION_CAPACITY][16];
static _Alignas(16) float ui_correction_delta[16];
static player_ui_published_pose_lease ui_correction_lease;
static player_live_correction_api ui_correction_api;
static player_live_correction_snapshot ui_correction_result;
static unsigned ui_correction_applies, ui_correction_restores;
static uintptr_t ui_correction_stack_low, ui_correction_stack_high;
static uintptr_t ui_correction_first_slot, ui_correction_first_original;
static uintptr_t ui_correction_move_slot, ui_correction_move_original;
static unsigned char ui_correction_move_source[64];
static unsigned ui_correction_bank_moves[2];
#ifdef GATE_TLS_TEST
/* Fixture-only identity substitutions exercise the real transaction ordering. */
static int (*ui_correction_test_canonical)(void);
static int (*ui_correction_test_owner)(void *, uintptr_t, unsigned);
#endif

/* A refusal mask keeps the original identity comparison, and tells a
 * diagnostic run exactly which retained fields differed. */
static uint32_t ui_correction_identity_difference(const ui_seat_query_identity *current,
                                                   const ui_seat_query_identity *saved) {
    uint32_t difference = 0;
    if (current->actor != saved->actor) difference |= UINT32_C(1) << 0;
    if (current->actor_table != saved->actor_table) difference |= UINT32_C(1) << 1;
    if (current->actor_parent != saved->actor_parent) difference |= UINT32_C(1) << 2;
    if (current->actor_controller != saved->actor_controller) difference |= UINT32_C(1) << 3;
    if (current->actor_controller_table != saved->actor_controller_table) difference |= UINT32_C(1) << 4;
    if (current->ship != saved->ship) difference |= UINT32_C(1) << 5;
    if (current->ship_table != saved->ship_table) difference |= UINT32_C(1) << 6;
    if (current->ship_reference_table != saved->ship_reference_table) difference |= UINT32_C(1) << 7;
    if (current->ship_room != saved->ship_room) difference |= UINT32_C(1) << 8;
    if (current->room != saved->room) difference |= UINT32_C(1) << 9;
    if (current->room_table != saved->room_table) difference |= UINT32_C(1) << 10;
    if (current->room_reference_table != saved->room_reference_table) difference |= UINT32_C(1) << 11;
    if (current->room_parent != saved->room_parent) difference |= UINT32_C(1) << 12;
    if (current->ship_runtime != saved->ship_runtime) difference |= UINT32_C(1) << 13;
    if (current->room_runtime != saved->room_runtime) difference |= UINT32_C(1) << 14;
    if (current->position != saved->position) difference |= UINT32_C(1) << 15;
    return difference;
}
static int ui_correction_same_identity(const ui_seat_query_identity *current,
                                       const ui_seat_query_identity *saved) {
    return !ui_correction_identity_difference(current, saved);
}
static void ui_correction_refusal(const char *stage, const native_ui_overlay_probe *probe) {
    dprintf(2, "X4 UI SEAT_CORRECTION REFUSAL tick=%llu stage=%s applies=%u restores=%u "
            "identity_status=%u touched=%u published_status=%u published_applies=%u published_restores=%u\n",
            (unsigned long long)stats.issued, stage, probe->applies, probe->restores,
            (unsigned)probe->transaction.status, probe->transaction.touched,
            (unsigned)ui_correction_lease.status, ui_correction_applies, ui_correction_restores);
}
static int ui_correction_environment(void) {
    unsigned char *tls = gate_tls_self();
    uint32_t bank = UINT32_MAX, view = UINT32_MAX;
    const presentation_fork_request *request = &presentation_fork.request;
    uint32_t expected_bank = ui_overlay_expected_bank(&ui_overlay_probe);
    int worker = tls && gate_tls_worker(tls);
    int entered = worker && player_recurring_physics_entered();
    int owner = entered && ui_overlay_owner(NULL, request->player1, request->player2, request->room, expected_bank);
    int view_read = owner && presentation_fork_read(NULL, (uintptr_t)(tls + GATE_TLS_LINUX_VIEW), &view, 4);
    int bank_read = view_read && view == 1 && presentation_fork_read(NULL, (uintptr_t)LX(0x3e77430), &bank, 4);
    if (bank_read && bank == expected_bank) return 1;
    /* Extra reads occur only after refusal; unknown values are marked unread. */
    int observed_view_read = view_read;
    int observed_bank_read = bank_read;
    if (!observed_view_read && worker)
        observed_view_read = presentation_fork_read(NULL, (uintptr_t)(tls + GATE_TLS_LINUX_VIEW), &view, 4);
    if (!observed_bank_read)
        observed_bank_read = presentation_fork_read(NULL, (uintptr_t)LX(0x3e77430), &bank, 4);
    dprintf(2, "X4 UI SEAT_CORRECTION REFUSAL tick=%llu stage=environment worker=%d entered=%d owner=%d "
            "view_read=%d view=%u bank_read=%d bank=%u expected_bank=%u\n",
            (unsigned long long)stats.issued, worker, entered, owner, observed_view_read, view,
            observed_bank_read, bank, expected_bank);
    return 0;
}
static int ui_correction_canonical(void) {
#ifdef GATE_TLS_TEST
    if (ui_correction_test_canonical)
        return ui_overlay_bank_matches(&ui_overlay_probe, ui_overlay_probe.move_bank) && ui_correction_test_canonical();
#endif
    if (!ui_seat_query_complete) {
        dprintf(2, "X4 UI SEAT_CORRECTION REFUSAL tick=%llu stage=query_incomplete\n", (unsigned long long)stats.issued);
        return 0;
    }
    if (!ui_correction_environment()) return 0;
    for (unsigned index = 0; index < 2; ++index) {
        ui_seat_query_identity current;
        int read_ok = ui_seat_query_identity_read(index, &current);
        const ui_seat_query_identity *saved = &ui_seat_query_identities[index];
        uint32_t difference = ui_correction_identity_difference(&current, saved);
        if (!read_ok || difference) {
            dprintf(2, "X4 UI SEAT_CORRECTION REFUSAL tick=%llu stage=identity role=%s read=%d diff=%08x "
                    "parent=%llx expected_parent=%llx controller=%llx expected_controller=%llx "
                    "vtable=%llx expected_vtable=%llx position=%u expected_position=%u "
                    "ship_count=%u ship_marker=%u ship_gate=%u room_count=%u room_marker=%u room_gate=%u\n",
                    (unsigned long long)stats.issued, index ? "p2" : "p1", read_ok, difference,
                    (unsigned long long)current.actor_parent, (unsigned long long)saved->actor_parent,
                    (unsigned long long)current.actor_controller, (unsigned long long)saved->actor_controller,
                    (unsigned long long)current.actor_controller_table, (unsigned long long)saved->actor_controller_table,
                    current.position, saved->position, current.ship_count, current.ship_marker, current.ship_class_gate,
                    current.room_count, current.room_marker, current.room_class_gate);
            return 0;
        }
    }
    return ui_correction_environment();
}
static int ui_correction_owner(void *context, uintptr_t actor, unsigned bank) {
#ifdef GATE_TLS_TEST
    if (ui_correction_test_owner)
        return ui_overlay_bank_matches(&ui_overlay_probe, bank) && ui_correction_test_owner(context, actor, bank);
#endif
    native_ui_overlay_probe *probe = context;
    const ui_seat_query_identity *p1 = &ui_seat_query_identities[0];
    const ui_seat_query_identity *p2 = &ui_seat_query_identities[1];
    ui_seat_query_identity current_p2;
    uintptr_t controller = 0, table = 0, parent = 0, published_parent = 0, ship_room = 0, room_parent = 0;
    uintptr_t original_parent = 0, original_published_parent = 0;
    uint32_t position = 0, original_position = 0;
    uint32_t ship_count = 0, room_count = 0, ship_marker = 0, room_marker = 0;
    uint32_t ship_gate = 1, room_gate = 1;
    uint64_t ship_id = 0, room_id = 0;
    if (!probe || actor != p1->actor || bank != ui_overlay_expected_bank(probe) || !ui_correction_environment() ||
        !ui_seat_query_identity_read(1, &current_p2) || !ui_correction_same_identity(&current_p2, p2) ||
        !presentation_fork_read(NULL, actor + 0x200, &controller, 8) || controller != p1->actor_controller ||
        !presentation_fork_read(NULL, controller, &table, 8) || table != p1->actor_controller_table ||
        table != (uintptr_t)LX(0x3b5edf8) ||
        !presentation_fork_read(NULL, actor + 0x70, &parent, 8) || parent != p2->room ||
        !presentation_fork_read(NULL, actor + 0xe0 + bank * 0x80, &published_parent, 8) || published_parent != p2->room ||
        !presentation_fork_read(NULL, actor + 0x6aa8, &position, 4) || position != p2->position ||
        !presentation_fork_word(p1->ship, p1->ship_table) ||
        !presentation_fork_word(p1->ship + 0x38, p1->ship_reference_table) ||
        !presentation_fork_word(p1->room, p1->room_table) ||
        !presentation_fork_word(p1->room + 0x38, p1->room_reference_table) ||
        !presentation_fork_read(NULL, p1->ship + 0x40, &ship_count, 4) || !ship_count || ship_count >= INT32_MAX - 1 ||
        !presentation_fork_read(NULL, p1->ship + 0x44, &ship_marker, 4) || ship_marker != 2 ||
        !presentation_fork_read(NULL, p1->ship + 0x68, &ship_gate, 4) || ship_gate ||
        !presentation_fork_read(NULL, p1->room + 0x40, &room_count, 4) || !room_count || room_count >= INT32_MAX - 1 ||
        !presentation_fork_read(NULL, p1->room + 0x44, &room_marker, 4) || room_marker != 2 ||
        !presentation_fork_read(NULL, p1->room + 0x68, &room_gate, 4) || room_gate ||
        !presentation_fork_read(NULL, p1->ship + 0x408, &ship_room, 8) || ship_room != p1->room ||
        !presentation_fork_read(NULL, p1->room + 0x70, &room_parent, 8) || room_parent != p1->ship ||
        !presentation_fork_read(NULL, p1->ship + 8, &ship_id, 8) || ship_id != p1->ship_runtime ||
        !presentation_fork_read(NULL, p1->room + 8, &room_id, 8) || room_id != p1->room_runtime) return 0;
    memcpy(&original_parent, probe->transaction.original[0], sizeof original_parent);
    memcpy(&original_position, probe->transaction.original[2], sizeof original_position);
    memcpy(&original_published_parent, probe->transaction.original[3], sizeof original_published_parent);
    return original_parent == p1->room && original_published_parent == p1->room &&
        original_position == p1->position && ui_correction_environment();
}
static int ui_correction_read(void *context, uintptr_t address, void *output, size_t size) {
    (void)context;
    return presentation_fork_read(NULL, address, output, size);
}
#ifdef GATE_TLS_TEST
static unsigned *ui_correction_test_exchange_count;
#endif
static int ui_correction_exchange(void *context, uintptr_t address, uintptr_t expected, uintptr_t desired) {
    (void)context;
    char permissions[5];
    if ((address & 7) || !gate_maps_find(address, permissions, NULL, 0) || strcmp(permissions, "rw-p")) return -1;
#ifdef GATE_TLS_TEST
    if (ui_correction_test_exchange_count) ++*ui_correction_test_exchange_count;
#endif
    return __atomic_compare_exchange_n((uintptr_t *)address, &expected, desired, 0,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) ? 1 : 0;
}
static void ui_correction_move_begin(void) {
    ui_correction_move_slot = 0;
    ui_correction_move_original = 0;
    memset(ui_correction_move_source, 0, sizeof ui_correction_move_source);
}
static int ui_correction_apply(native_ui_overlay_probe *probe) {
    if (ui_correction_applies >= UI_CORRECTION_CAPACITY) return 0;
    memset(&ui_correction_lease, 0, sizeof ui_correction_lease);
    const player_ui_published_pose_ops operations = {
        probe, ui_correction_owner, ui_correction_read, ui_correction_exchange
    };
    uint32_t bank = ui_overlay_expected_bank(probe);
    size_t offset = 0xe0 + bank * 0x80 + 0x28;
    if (presentation_fork.request.player1 > UINTPTR_MAX - offset) return 0;
    uintptr_t slot = presentation_fork.request.player1 + offset;
    uintptr_t original = 0;
    unsigned char raw[64], checked[64];
    if (!ui_correction_owner(probe, presentation_fork.request.player1, bank) ||
        !ui_correction_read(NULL, slot, &original, 8) || !original ||
        !ui_correction_read(NULL, original, raw, sizeof raw)) return 0;
    if (!ui_correction_move_original) {
        ui_correction_move_slot = slot;
        ui_correction_move_original = original;
        memcpy(ui_correction_move_source, raw, sizeof raw);
    } else if (slot != ui_correction_move_slot || original != ui_correction_move_original ||
               memcmp(raw, ui_correction_move_source, sizeof raw)) return 0;
    if (!player_ui_published_pose_begin(&ui_correction_lease, &operations,
                                        presentation_fork.request.player1, bank,
                                        ui_correction_scratch[ui_correction_applies], ui_correction_delta)) return 0;
    if (ui_correction_lease.slot != slot || ui_correction_lease.original != original ||
        !ui_correction_read(NULL, original, checked, sizeof checked) || memcmp(raw, checked, sizeof raw)) return 0;
    if (!ui_correction_applies) {
        ui_correction_first_slot = slot;
        ui_correction_first_original = original;
        char source[129], corrected[129];
        ui_live_pose_hex(raw, source);
        ui_live_pose_hex((const unsigned char *)ui_correction_scratch[0], corrected);
        dprintf(2, "X4 UI SEAT_CORRECTION v=1 stage=published tick=9 slot=%llx original=%llx scratch=%llx raw=%s corrected=%s\n",
                (unsigned long long)slot, (unsigned long long)original,
                (unsigned long long)(uintptr_t)ui_correction_scratch[0], source, corrected);
    }
    ++ui_correction_applies;
    return 1;
}
static int ui_correction_restore(void) {
    if (!player_ui_published_pose_end(&ui_correction_lease)) return 0;
    uintptr_t observed = 0;
    unsigned char current[64];
    if (!ui_correction_read(NULL, ui_correction_move_slot, &observed, 8) ||
        observed != ui_correction_move_original ||
        !ui_correction_read(NULL, observed, current, sizeof current) ||
        memcmp(current, ui_correction_move_source, sizeof current) ||
        !ui_overlay_bank_matches(&ui_overlay_probe, ui_overlay_probe.move_bank)) return 0;
    if (!ui_correction_restores) {
        if (ui_correction_first_slot != ui_correction_move_slot ||
            observed != ui_correction_first_original) return 0;
        dprintf(2, "X4 UI SEAT_CORRECTION v=1 stage=published_restore tick=9 slot=%llx original=%llx observed=%llx\n",
                (unsigned long long)ui_correction_first_slot, (unsigned long long)ui_correction_first_original,
                (unsigned long long)observed);
    }
    ++ui_correction_restores;
    return 1;
}
static int ui_correction_move_closed(unsigned ordinal) {
    const native_ui_overlay_probe *probe = &ui_overlay_probe;
    uint32_t bank = probe->move_bank;
    uintptr_t observed = 0;
    unsigned char current[64];
    if (bank > 1 || !ui_correction_canonical() || !ui_correction_move_original ||
        !ui_correction_read(NULL, ui_correction_move_slot, &observed, 8) ||
        observed != ui_correction_move_original ||
        !ui_correction_read(NULL, observed, current, sizeof current) ||
        memcmp(current, ui_correction_move_source, sizeof current) ||
        !ui_overlay_bank_matches(probe, bank)) return 0;
    ++ui_correction_bank_moves[bank];
    if (ui_correction_bank_moves[bank] == 1)
        dprintf(2, "X4 UI SEAT_BANK v=1 stage=sample tick=%llu bank=%u slot=%llx original=%llx restored=%llx\n",
                (unsigned long long)stats.issued, bank, (unsigned long long)ui_correction_move_slot,
                (unsigned long long)ui_correction_move_original, (unsigned long long)observed);
    if (ordinal == 160) {
        if (ui_correction_bank_moves[0] + ui_correction_bank_moves[1] != 160) return 0;
        dprintf(2, "X4 UI SEAT_BANK v=1 stage=close bank0=%u bank1=%u moves=160\n",
                ui_correction_bank_moves[0], ui_correction_bank_moves[1]);
    }
    return 1;
}
static void ui_correction_prepare(void) {
    pthread_attr_t attributes;
    void *stack = NULL;
    size_t size = 0;
    long tid = syscall(SYS_gettid);
    if (!ui_correction_canonical() || ui_seat_query_identities[0].actor_controller != ui_live_pose_target ||
        !player_ui_seat_transform_delta(ui_seat_query_poses[0], ui_seat_query_poses[1], ui_correction_delta) ||
        tid <= 0 || (unsigned long)tid > UINT32_MAX || pthread_getattr_np(pthread_self(), &attributes))
        ui_overlay_die("seat_correction_prepare");
    int get_result = pthread_attr_getstack(&attributes, &stack, &size);
    int destroy_result = pthread_attr_destroy(&attributes);
    uintptr_t local = (uintptr_t)&attributes;
    ui_correction_stack_low = (uintptr_t)stack;
    if (get_result || destroy_result || !stack || size < 64 || size > UINTPTR_MAX - ui_correction_stack_low)
        ui_overlay_die("seat_correction_stack");
    ui_correction_stack_high = ui_correction_stack_low + size;
    if (local < ui_correction_stack_low || local >= ui_correction_stack_high ||
        !ui_correction_canonical() || !ui_correction_api.configure((uint32_t)tid, ui_correction_delta,
                                                                    ui_correction_stack_low, ui_correction_stack_high))
        ui_overlay_die("seat_correction_configure");
    char delta[129];
    ui_live_pose_hex((const unsigned char *)ui_correction_delta, delta);
    dprintf(2, "X4 UI SEAT_CORRECTION v=1 stage=ready tick=9 target=%llx delta=%s stack_low=%llx stack_high=%llx\n",
            (unsigned long long)ui_live_pose_target, delta, (unsigned long long)ui_correction_stack_low,
            (unsigned long long)ui_correction_stack_high);
}
static void ui_correction_close(void) {
    if (!ui_correction_api.result(&ui_correction_result) || !ui_correction_result.calls ||
        ui_correction_result.calls != ui_live_pose_scope_result.counts[PLAYER_LIVE_POSE_OPEN] ||
        ui_correction_result.count != (ui_correction_result.calls < PLAYER_LIVE_CORRECTION_ROWS ?
                                      ui_correction_result.calls : PLAYER_LIVE_CORRECTION_ROWS) ||
        ui_correction_applies != ui_correction_restores ||
        ui_correction_applies != ui_live_pose_scope_result.opens ||
        ui_correction_lease.status != PLAYER_POSE_CLOSED)
        ui_overlay_die("seat_correction_close");
    for (unsigned index = 0; index < ui_correction_result.count; ++index) {
        const player_live_correction_row *row = &ui_correction_result.rows[index];
        char raw[129], corrected[129];
        ui_live_pose_hex(row->raw, raw);
        ui_live_pose_hex(row->corrected, corrected);
        dprintf(2, "X4 UI SEAT_CORRECTION v=1 stage=sample ordinal=%llu last_move=%llu caller=%llx time=%016llx raw=%s corrected=%s\n",
                (unsigned long long)row->ordinal, (unsigned long long)row->last_move,
                (unsigned long long)row->caller, (unsigned long long)row->time_bits, raw, corrected);
    }
    dprintf(2, "X4 UI SEAT_CORRECTION v=1 stage=close tick=168 applies=%u restores=%u calls=%llu rows=%u published_restored=1 state=outside\n",
            ui_correction_applies, ui_correction_restores, (unsigned long long)ui_correction_result.calls,
            ui_correction_result.count);
}
#endif
