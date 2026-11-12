#ifndef X4_GATE_PLAYER_VIEW_ANCESTRY_H
#define X4_GATE_PLAYER_VIEW_ANCESTRY_H

/* Diagnostic only. P2 target ownership is retained by the existing lifecycle;
 * this adapter holds P1's ship until owner-thread sampling has closed. */
#include "player_view_ancestry_preload_pin.h"

static player_view_ancestry_api ancestry_api;
static player_view_query_api ancestry_sealed_provider;
static unsigned ancestry_mode; /* 0 unchecked, 1 off, 2 awaiting bind, 3 active, 4 closed */
static uintptr_t ancestry_p1_ship, ancestry_p2_ship, ancestry_room;
static uint64_t ancestry_runtime, ancestry_first_tick;
static uint32_t ancestry_tid;
static unsigned ancestry_p1_owned, ancestry_p1_uncertain;

typedef struct {
    uintptr_t player, room, ship, table, reference_table, room_parent;
    uint64_t runtime;
    uint32_t count, marker;
} ancestry_p1_identity;

static _Noreturn void ancestry_fail(const char *reason) {
    dprintf(2, "X4 VIEW_ANCESTRY v=1 stage=failure tick=%llu reason=%s owned=%u uncertain=%u\n",
            (unsigned long long)stats.issued, reason, ancestry_p1_owned, ancestry_p1_uncertain);
    _exit(223);
}
static int ancestry_identity(ancestry_p1_identity *out) {
    if (!out || !player_recurring_physics_entered() || !player_physics_identity() ||
        player_routes_scope || !factory_probe_singleton(factory_probe_player1)) return 0;
    memset(out, 0, sizeof *out);
    out->player = factory_probe_player1;
    out->ship = factory_probe_identity.ship;
    if (!out->ship || !player_target_hold_read(out->player, 0x70, &out->room, 8) || !out->room ||
        !player_target_hold_read(out->room, 0x70, &out->room_parent, 8) || out->room_parent != out->ship ||
        !player_target_hold_read(out->ship, 0, &out->table, 8) ||
        out->table != (uintptr_t)LX(SEAT_PREFLIGHT_SHIP_VTABLE) ||
        !player_target_hold_read(out->ship, 8, &out->runtime, 8) || out->runtime != factory_probe_identity.runtime ||
        !player_target_hold_read(out->ship, PLAYER_TARGET_HOLD_REFERENCE_OFFSET, &out->reference_table, 8) ||
        out->reference_table != (uintptr_t)LX(PLAYER_TARGET_HOLD_SHIP_REFERENCE_VTABLE) ||
        !player_target_hold_read(out->ship, PLAYER_TARGET_HOLD_COUNT_OFFSET, &out->count, 4) ||
        !player_target_hold_read(out->ship, PLAYER_TARGET_HOLD_MARKER_OFFSET, &out->marker, 4) ||
        !out->count || out->count >= UINT32_MAX - 1 || out->marker != 2) return 0;
    if (ancestry_p1_owned && (out->ship != ancestry_p1_ship || out->room != ancestry_room ||
                              out->runtime != ancestry_runtime)) return 0;
    return player_target_hold_resident.target_owned == 1 && !player_target_hold_resident.uncertain &&
        player_target_hold_resident.identity.target == lifecycle_discard_ship &&
        (!ancestry_p1_owned || lifecycle_discard_ship == ancestry_p2_ship);
}
static void ancestry_configure(void) {
    if (ancestry_mode) return;
    const char *setting = getenv("X4_DEV_VIEW_ANCESTRY");
    if (!setting) { ancestry_mode = 1; return; }
    if (strcmp(setting, "1") || player_recurring_physics != 3 || !player_recurring_tail ||
        (stats.slot != 1 && stats.slot != 2)) ancestry_fail("configuration");
    ancestry_mode = 2;
}
static void player_view_ancestry_before(uint64_t generation) {
    ancestry_configure();
    if (ancestry_mode == 1 || ancestry_mode == 2) return;
    if (stats.issued != generation || player_recurring_tail_token.generation != generation)
        ancestry_fail("generation");
    ancestry_p1_identity identity;
    if (ancestry_mode != 3 || !ancestry_p1_owned || ancestry_p1_uncertain ||
        !ancestry_identity(&identity) || !ancestry_api.tick(generation)) ancestry_fail("advance");
}
static void player_view_ancestry_after(uint64_t generation) {
    if (ancestry_mode == 1 || ancestry_mode == 3) return;
    if (ancestry_mode != 2 || stats.issued != generation ||
        player_recurring_tail_token.generation != generation) ancestry_fail("admission");
    /* A negotiated startup may create P2 after the first recurring tick. */
    if (!atomic_load_explicit(&player_physics_ready, memory_order_acquire)) return;
    if (!player_target_hold_pins()) ancestry_fail("pins");
    long tid = syscall(SYS_gettid);
    ancestry_p1_identity before, check, after;
    if (tid <= 0 || (unsigned long)tid > UINT32_MAX || !gate_tls_worker(gate_tls_self()) ||
        !ancestry_identity(&before) || !ancestry_identity(&check) || memcmp(&before, &check, sizeof before))
        ancestry_fail("identity");
    ancestry_p1_ship = before.ship;
    ancestry_p2_ship = lifecycle_discard_ship;
    ancestry_room = before.room;
    ancestry_runtime = before.runtime;
    if (!ancestry_p2_ship || ancestry_p1_ship == ancestry_p2_ship || !factory_probe_identity.saved ||
        !player_target_hold_resident.identity.target_runtime) ancestry_fail("ships");
    const char *sha = NULL; uint64_t size = 0;
    if (!view_query_expected_library(&sha, &size) ||
        !player_view_query_provider(sha, size, &ancestry_sealed_provider) ||
        !player_view_ancestry_provider(&ancestry_sealed_provider, &ancestry_api)) ancestry_fail("provider");
    ancestry_p1_uncertain = 1;
    ((void (*)(void *))LX(PLAYER_TARGET_HOLD_ACQUIRE_VA))((void *)ancestry_p1_ship);
    if (!ancestry_identity(&after)) ancestry_fail("retain_readback");
    ++before.count;
    if (memcmp(&before, &after, sizeof before)) ancestry_fail("retain_changed");
    ancestry_p1_owned = 1;
    ancestry_p1_uncertain = 0;
    ancestry_tid = (uint32_t)tid;
    if (!ancestry_api.bind(ancestry_tid, ancestry_p1_ship, ancestry_p2_ship) || !ancestry_api.tick(generation))
        ancestry_fail("bind");
    ancestry_first_tick = generation;
    ancestry_mode = 3;
    dprintf(2, "X4 VIEW_ANCESTRY v=1 stage=bind tick=%llu slot=%u tid=%u p1=%llx p2=%llx p1_runtime=%llu p2_runtime=%llu p1_saved=%llu p2_saved=%llu ref_before=%u ref_after=%u\n",
            (unsigned long long)generation, stats.slot, ancestry_tid, (unsigned long long)ancestry_p1_ship,
            (unsigned long long)ancestry_p2_ship, (unsigned long long)ancestry_runtime,
            (unsigned long long)player_target_hold_resident.identity.target_runtime,
            (unsigned long long)factory_probe_identity.saved, (unsigned long long)SEAT_PREFLIGHT_TARGET_SAVED,
            before.count - 1, before.count);
}
static void player_view_ancestry_close(uint64_t generation) {
    if (ancestry_mode == 1) return;
    ancestry_p1_identity before, after;
    view_ancestry_snapshot snapshot;
    player_view_ancestry_api checked;
    if (ancestry_mode != 3 || stats.issued != generation ||
        player_recurring_tail_token.generation != generation || !ancestry_identity(&before) ||
        !player_view_ancestry_provider(&ancestry_sealed_provider, &checked) ||
        checked.bind != ancestry_api.bind || checked.tick != ancestry_api.tick || checked.finish != ancestry_api.finish ||
        memcmp(&checked.information, &ancestry_api.information, sizeof checked.information) ||
        !ancestry_api.finish(&snapshot) ||
        snapshot.version != VIEW_ANCESTRY_VERSION || !snapshot.installed || !snapshot.closed ||
        snapshot.first_ship != ancestry_p1_ship || snapshot.second_ship != ancestry_p2_ship)
        ancestry_fail("finish");
    static const char *names[] = {"weapon", "engine", "ship"};
    for (unsigned site = 0; site < VIEW_ANCESTRY_SITES; ++site) {
        view_ancestry_site *entry = &snapshot.sites[site];
        if (entry->started != entry->returned || entry->count > VIEW_ANCESTRY_SAMPLES ||
            entry->examined > 128 || entry->count > entry->examined ||
            entry->started != entry->examined + entry->overflow + entry->throttled)
            ancestry_fail("receipt_counts");
        unsigned category_counts[4] = {0};
        uint64_t prior_generation = 0;
        for (unsigned index = 0; index < entry->count; ++index) {
            const view_ancestry_sample *sample = &entry->samples[index];
            if (sample->tid != ancestry_tid || sample->generation < ancestry_first_tick ||
                sample->generation > generation || sample->generation <= prior_generation ||
                sample->count > VIEW_ANCESTRY_DEPTH || sample->category > 3 || sample->complete > 1 ||
                (sample->count && sample->nodes[0].component != sample->self) ||
                (site != 2 && sample->argument)) ancestry_fail("receipt_sample");
            prior_generation = sample->generation;
            unsigned first = 0, second = 0;
            for (unsigned node = 0; node < sample->count; ++node) {
                first |= sample->nodes[node].component == ancestry_p1_ship;
                second |= sample->nodes[node].component == ancestry_p2_ship;
            }
            unsigned expected_category = first != second ? (first ? 1u : 2u) :
                                         (!first && sample->complete ? 3u : 0u);
            if (sample->category != expected_category || ++category_counts[sample->category] > 2)
                ancestry_fail("receipt_category");
            char nodes[600]; size_t used = 0;
            nodes[0] = 0;
            for (unsigned node = 0; node < sample->count; ++node) {
                int written = snprintf(nodes + used, sizeof nodes - used, "%s%llx:%llx:%llu", node ? "," : "",
                    (unsigned long long)sample->nodes[node].component,
                    (unsigned long long)sample->nodes[node].vtable,
                    (unsigned long long)sample->nodes[node].runtime);
                if (written < 0 || (size_t)written >= sizeof nodes - used) ancestry_fail("receipt_format");
                used += (size_t)written;
            }
            dprintf(2, "X4 VIEW_ANCESTRY v=1 stage=sample site=%s index=%u tick=%llu tid=%u self=%llx arg=%llx category=%u complete=%u count=%u nodes=%s\n",
                names[site], index, (unsigned long long)sample->generation, sample->tid,
                (unsigned long long)sample->self, (unsigned long long)sample->argument,
                sample->category, sample->complete, sample->count, used ? nodes : "-");
        }
        for (unsigned category = 0; category < 4; ++category)
            if (entry->category_count[category] != category_counts[category]) ancestry_fail("receipt_categories");
        dprintf(2, "X4 VIEW_ANCESTRY v=1 stage=site site=%s started=%llu returned=%llu examined=%u throttled=%llu overflow=%llu foreign_lower=%llu rows=%u\n",
            names[site], (unsigned long long)entry->started, (unsigned long long)entry->returned,
            entry->examined, (unsigned long long)entry->throttled, (unsigned long long)entry->overflow,
            (unsigned long long)entry->foreign, entry->count);
    }
    if (before.count <= 1) ancestry_fail("release_last");
    ancestry_p1_uncertain = 1;
    ((void (*)(void *))LX(PLAYER_TARGET_HOLD_RELEASE_VA))((void *)(ancestry_p1_ship + PLAYER_TARGET_HOLD_REFERENCE_OFFSET));
    if (!ancestry_identity(&after)) ancestry_fail("release_readback");
    --before.count;
    if (memcmp(&before, &after, sizeof before)) ancestry_fail("release_changed");
    ancestry_p1_owned = 0;
    ancestry_p1_uncertain = 0;
    ancestry_mode = 4;
    dprintf(2, "X4 VIEW_ANCESTRY v=1 stage=close tick=%llu slot=%u ref_before=%u ref_after=%u sampling_closed=1 unpatched=0 foreign=lower_bound\n",
            (unsigned long long)generation, stats.slot, before.count + 1, before.count);
}
#endif
