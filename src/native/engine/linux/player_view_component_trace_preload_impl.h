#ifndef X4_PLAYER_VIEW_COMPONENT_TRACE_PRELOAD_IMPL_H
#define X4_PLAYER_VIEW_COMPONENT_TRACE_PRELOAD_IMPL_H
#include "player_view_component_trace_preload.h"
static _Atomic unsigned component_trace_active;
static _Atomic uint64_t component_trace_foreign[2];
static uint32_t component_trace_owner;
static component_trace_phase_fn component_trace_phase;
static component_trace component_trace_state;
static component_trace component_precompare_state;
static uintptr_t component_precompare_results[COMPONENT_TRACE_LIMIT];
static _Atomic uint64_t component_precompare_foreign;

static int component_trace_read_native(uintptr_t address, void *destination, unsigned size) {
    return view_ancestry_copy(destination, address, size);
}
static int component_trace_class_native(unsigned site, uintptr_t vtable) {
    /* Observed primary classes only; other classes are retained as invalid rows. */
    return vtable == (site ? (uintptr_t)0x3ccad80u : (uintptr_t)0x3c6a440u);
}
static void view_component_precompare_observe(uintptr_t object, uintptr_t ancestor, uintptr_t result) {
    if (!atomic_load_explicit(&component_trace_active, memory_order_acquire)) return;
    if (current_thread() != component_trace_owner) {
        atomic_fetch_add_explicit(&component_precompare_foreign, 1, memory_order_relaxed);
        return;
    }
    unsigned phase = 3, before = component_precompare_state.site[1].count;
    if (component_trace_is_selected(&component_precompare_state, 1) &&
        (!component_trace_phase(component_precompare_state.generation, &phase) || phase > 3)) phase = 3;
    component_trace_record(&component_precompare_state, 1, phase, object, ancestor,
                           component_trace_read_native, component_trace_class_native);
    if (component_precompare_state.site[1].count > before) component_precompare_results[before] = result;
}
static void view_component_trace_observe(unsigned site, uintptr_t object, uintptr_t engine_ancestor) {
    if (!atomic_load_explicit(&component_trace_active, memory_order_acquire)) return;
    if (current_thread() != component_trace_owner) {
        atomic_fetch_add_explicit(&component_trace_foreign[site], 1, memory_order_relaxed);
        return;
    }
    unsigned phase = 3;
    if (component_trace_is_selected(&component_trace_state, site) &&
        (!component_trace_phase(component_trace_state.generation, &phase) || phase > 3)) phase = 3;
    component_trace_record(&component_trace_state, site, phase, object, engine_ancestor,
                           component_trace_read_native, component_trace_class_native);
}
static int view_component_trace_advance(uint64_t generation) {
    return !atomic_load_explicit(&component_trace_active, memory_order_acquire) ||
           (current_thread() == component_trace_owner && component_trace_tick(&component_trace_state, generation) &&
            component_trace_tick(&component_precompare_state, generation));
}
EXPORT int x4mp_component_trace_bind(uint32_t owner, uintptr_t first, uintptr_t second,
                                     component_trace_phase_fn phase) {
    if (!owner || current_thread() != owner || !phase ||
        !atomic_load_explicit(&view_ancestry_active, memory_order_acquire) ||
        view_ancestry_owner != owner || view_ancestry_capture.first_ship != first ||
        view_ancestry_capture.second_ship != second || !view_ancestry_generation ||
        atomic_load_explicit(&component_trace_active, memory_order_acquire) || component_trace_state.closed ||
        !component_trace_begin(&component_trace_state, first, second) ||
        !component_trace_begin(&component_precompare_state, first, second) ||
        !component_trace_tick(&component_precompare_state, view_ancestry_generation) ||
        !component_trace_tick(&component_trace_state, view_ancestry_generation)) return 0;
    component_trace_owner = owner; component_trace_phase = phase;
    atomic_store_explicit(&component_trace_active, 1, memory_order_release);
    return 1;
}
EXPORT int x4mp_component_trace_finish(component_trace_snapshot *out) {
    if (!out || !atomic_load_explicit(&component_trace_active, memory_order_acquire) ||
        current_thread() != component_trace_owner ||
        view_ancestry_capture.sites[0].started != view_ancestry_capture.sites[0].returned ||
        view_ancestry_capture.sites[1].started != view_ancestry_capture.sites[1].returned ||
        !component_trace_close(&component_trace_state) || !component_trace_close(&component_precompare_state)) return 0;
    atomic_store_explicit(&component_trace_active, 0, memory_order_release);
    out->version = COMPONENT_TRACE_VERSION; out->closed = 1; out->owner = component_trace_owner;
    for (unsigned site = 0; site < 2; ++site)
        out->foreign_lower[site] = atomic_load_explicit(&component_trace_foreign[site], memory_order_relaxed);
    out->trace = component_trace_state;
    return 1;
}
EXPORT int x4mp_component_precompare_finish(component_precompare_snapshot *out) {
    if (!out || current_thread() != component_trace_owner || !component_precompare_state.closed) return 0;
    out->version = 1; out->closed = 1; out->owner = component_trace_owner;
    out->foreign_lower = atomic_load_explicit(&component_precompare_foreign, memory_order_relaxed);
    out->trace = component_precompare_state;
    memcpy(out->results, component_precompare_results, sizeof out->results);
    return 1;
}
#endif
