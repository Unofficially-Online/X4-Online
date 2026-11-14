#ifndef X4_GATE_PLAYER_VIEW_COMPONENT_TRACE_H
#define X4_GATE_PLAYER_VIEW_COMPONENT_TRACE_H
/* Optional identity samples, under the existing retained ancestry observer. */
#include "player_view_component_trace_preload.h"
static int (*component_trace_bind_api)(uint32_t, uintptr_t, uintptr_t, component_trace_phase_fn);
static int (*component_trace_finish_api)(component_trace_snapshot *);
static component_trace_snapshot component_trace_receipt;
static unsigned component_trace_gate_enabled;
static component_precompare_snapshot component_precompare_receipt;
static int component_trace_gate_phase(uint64_t generation, unsigned *phase) {
    if (!phase || syscall(SYS_gettid) != (long)ancestry_tid) return 0;
    if (!player_recurring_physics_entered()) { *phase = 0; return 1; }
    if (generation != stats.issued || generation != player_recurring_tail_token.generation ||
        !gate_tls_worker(gate_tls_self())) { *phase = 3; return 1; }
    if (player_routes_scope && player_physics_identity_for(1)) *phase = 2;
    else if (!player_routes_scope && player_physics_identity()) *phase = 1;
    else *phase = 3;
    return 1;
}
static int component_trace_gate_provider(int (**bind)(uint32_t, uintptr_t, uintptr_t, component_trace_phase_fn),
                                         int (**finish)(component_trace_snapshot *)) {
    Dl_info module;
    if (!ancestry_sealed_provider.finish || !dladdr((void *)ancestry_sealed_provider.finish, &module) || !module.dli_fbase) return 0;
    void *b = dlsym(RTLD_DEFAULT, "x4mp_component_trace_bind");
    void *f = dlsym(RTLD_DEFAULT, "x4mp_component_trace_finish");
    if (!player_view_pin_same_module((uintptr_t)b, module.dli_fbase, "r-xp") ||
        !player_view_pin_same_module((uintptr_t)f, module.dli_fbase, "r-xp")) return 0;
    *bind = (int (*)(uint32_t, uintptr_t, uintptr_t, component_trace_phase_fn))b;
    *finish = (int (*)(component_trace_snapshot *))f;
    return 1;
}
static void view_component_trace_gate_begin(uint64_t generation) {
    const char *enabled = getenv("X4_DEV_VIEW_COMPONENT_TRACE");
    if (!enabled) return;
    if (strcmp(enabled, "1") || ancestry_mode != 3 || generation != stats.issued ||
        !component_trace_gate_provider(&component_trace_bind_api, &component_trace_finish_api) ||
        !component_trace_bind_api(ancestry_tid, ancestry_p1_ship, ancestry_p2_ship, component_trace_gate_phase))
        ancestry_fail("component_trace_bind");
    component_trace_gate_enabled = 1;
    dprintf(2, "X4 COMPONENT_TRACE v=1 stage=bind tick=%llu tid=%u p1=%llx p2=%llx\n",
            (unsigned long long)generation, ancestry_tid, (unsigned long long)ancestry_p1_ship,
            (unsigned long long)ancestry_p2_ship);
}
static void view_component_trace_gate_finish(uint64_t generation) {
    if (!component_trace_gate_enabled) return;
    int (*bind)(uint32_t, uintptr_t, uintptr_t, component_trace_phase_fn) = NULL;
    int (*finish)(component_trace_snapshot *) = NULL;
    if (ancestry_mode != 3 || stats.issued != generation || !ancestry_identity(&(ancestry_p1_identity){0}) ||
        !component_trace_gate_provider(&bind, &finish) || bind != component_trace_bind_api || finish != component_trace_finish_api ||
        !finish(&component_trace_receipt) || component_trace_receipt.version != COMPONENT_TRACE_VERSION ||
        !component_trace_receipt.closed || component_trace_receipt.owner != ancestry_tid ||
        component_trace_receipt.trace.generation != generation || !component_trace_receipt.trace.closed ||
        component_trace_receipt.trace.ships[0] != ancestry_p1_ship ||
        component_trace_receipt.trace.ships[1] != ancestry_p2_ship) ancestry_fail("component_trace_finish");
    for (unsigned site = 0; site < 2; ++site) {
        const component_trace_site *entry = &component_trace_receipt.trace.site[site];
        if (entry->count > COMPONENT_TRACE_LIMIT || entry->sampled != entry->count || entry->sampled > entry->calls)
            ancestry_fail("component_trace_counts");
        uint64_t previous_tick = 0, previous_ordinal = 0;
        for (unsigned i = 0; i < entry->count; ++i) {
            const component_trace_sample *row = &entry->samples[i];
            unsigned first = (unsigned)(row->generation % 8u), second = 8u + (unsigned)((row->generation / 8u) % 8u);
            if (row->generation < ancestry_first_tick || row->generation > generation || row->phase > 3 ||
                row->valid > 1 || row->failure > 6 || row->direct_slot > 2 || (row->ordinal != first && row->ordinal != second) ||
                (previous_tick && (row->generation < previous_tick ||
                 (row->generation == previous_tick && row->ordinal <= previous_ordinal))) ||
                (!site && row->engine_ancestor) ||
                (!row->valid && (!row->failure || row->direct_slot)) ||
                (row->valid && (row->failure || !row->object || !row->runtime || !row->parent || row->parent == row->object ||
                 row->vtable != (site ? 0x3ccad80u : 0x3c6a440u) ||
                 row->direct_slot != (row->parent == ancestry_p1_ship ? 1u : row->parent == ancestry_p2_ship ? 2u : 0u))))
                ancestry_fail("component_trace_sample");
            previous_tick = row->generation; previous_ordinal = row->ordinal;
            dprintf(2, "X4 COMPONENT_TRACE v=1 stage=sample site=%u index=%u tick=%llu ordinal=%llu phase=%u object=%llx class=%llx runtime=%llu parent=%llx ancestor=%llx valid=%u slot=%u failure=%u\n",
                site, i, (unsigned long long)row->generation, (unsigned long long)row->ordinal, row->phase,
                (unsigned long long)row->object, (unsigned long long)row->vtable, (unsigned long long)row->runtime,
                (unsigned long long)row->parent, (unsigned long long)row->engine_ancestor, row->valid, row->direct_slot, row->failure);
        }
        dprintf(2, "X4 COMPONENT_TRACE v=1 stage=site site=%u calls=%llu sampled=%llu missed=%llu capped=%llu foreign_lower=%llu rows=%u\n",
                site, (unsigned long long)entry->calls, (unsigned long long)entry->sampled,
                (unsigned long long)entry->missed, (unsigned long long)entry->capped,
                (unsigned long long)component_trace_receipt.foreign_lower[site], entry->count);
    }
    dprintf(2, "X4 COMPONENT_TRACE v=1 stage=close tick=%llu tid=%u sampling=bounded foreign=lower_bound\n",
            (unsigned long long)generation, ancestry_tid);
    const char *pre_enabled = getenv("X4_DEV_VIEW_PRECOMPARE_TRACE");
    if (pre_enabled) {
        Dl_info module;
        void *pre_finish = dlsym(RTLD_DEFAULT, "x4mp_component_precompare_finish");
        if (strcmp(pre_enabled, "1") || !ancestry_api.information.pre_enabled ||
            !dladdr((void *)ancestry_sealed_provider.finish, &module) || !module.dli_fbase ||
            !player_view_pin_same_module((uintptr_t)pre_finish, module.dli_fbase, "r-xp") ||
            !((int (*)(component_precompare_snapshot *))pre_finish)(&component_precompare_receipt) ||
            component_precompare_receipt.version != 1 || !component_precompare_receipt.closed ||
            component_precompare_receipt.owner != ancestry_tid || !component_precompare_receipt.trace.closed ||
            component_precompare_receipt.trace.generation != generation ||
            component_precompare_receipt.trace.ships[0] != ancestry_p1_ship ||
            component_precompare_receipt.trace.ships[1] != ancestry_p2_ship) ancestry_fail("precompare_finish");
        const component_trace_site *entry = &component_precompare_receipt.trace.site[1];
        if (entry->count > COMPONENT_TRACE_LIMIT || entry->sampled != entry->count || entry->sampled > entry->calls)
            ancestry_fail("precompare_counts");
        uint64_t previous_tick = 0, previous_ordinal = 0;
        for (unsigned i = 0; i < entry->count; ++i) {
            const component_trace_sample *row = &entry->samples[i];
            unsigned first = (unsigned)(row->generation % 8u), second = 8u + (unsigned)((row->generation / 8u) % 8u);
            if (row->generation < ancestry_first_tick || row->generation > generation || row->phase > 3 ||
                row->valid > 1 || row->failure > 6 || row->direct_slot > 2 ||
                (row->ordinal != first && row->ordinal != second) ||
                (previous_tick && (row->generation < previous_tick ||
                 (row->generation == previous_tick && row->ordinal <= previous_ordinal))) ||
                (!row->valid && (!row->failure || row->direct_slot)) ||
                (row->valid && (row->failure || !row->object || !row->runtime || !row->parent || row->parent == row->object ||
                 row->vtable != 0x3ccad80u || row->direct_slot !=
                 (row->parent == ancestry_p1_ship ? 1u : row->parent == ancestry_p2_ship ? 2u : 0u))))
                ancestry_fail("precompare_sample");
            previous_tick = row->generation; previous_ordinal = row->ordinal;
            dprintf(2, "X4 PRECOMPARE v=1 stage=sample index=%u tick=%llu ordinal=%llu phase=%u object=%llx class=%llx runtime=%llu parent=%llx ancestor=%llx result=%llx valid=%u slot=%u failure=%u\n",
                i, (unsigned long long)row->generation, (unsigned long long)row->ordinal, row->phase,
                (unsigned long long)row->object, (unsigned long long)row->vtable, (unsigned long long)row->runtime,
                (unsigned long long)row->parent, (unsigned long long)row->engine_ancestor,
                (unsigned long long)component_precompare_receipt.results[i], row->valid, row->direct_slot, row->failure);
        }
        dprintf(2, "X4 PRECOMPARE v=1 stage=site calls=%llu sampled=%llu missed=%llu capped=%llu foreign_lower=%llu rows=%u\n",
            (unsigned long long)entry->calls, (unsigned long long)entry->sampled,
            (unsigned long long)entry->missed, (unsigned long long)entry->capped,
            (unsigned long long)component_precompare_receipt.foreign_lower, entry->count);
    }
    component_trace_gate_enabled = 0;
}
#endif
