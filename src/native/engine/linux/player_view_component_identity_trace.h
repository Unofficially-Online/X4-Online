#ifndef X4_PLAYER_VIEW_COMPONENT_IDENTITY_TRACE_H
#define X4_PLAYER_VIEW_COMPONENT_IDENTITY_TRACE_H
/* Dormant identity-only sampler. The embedding observer must authenticate the
 * owner thread, retained ships and phase; this code neither patches nor binds
 * hooks and never substitutes a result. A sample is a point observation, not a
 * lifetime, exclusion, exhaustive-call or object-ownership proof. */
#include <stdint.h>
#include <string.h>

#define COMPONENT_TRACE_SITES 2u
#define COMPONENT_TRACE_LIMIT 256u

typedef int (*component_trace_read)(uintptr_t, void *, unsigned);
typedef int (*component_trace_class)(unsigned, uintptr_t);
typedef struct {
    uint64_t generation, ordinal, runtime;
    uintptr_t object, vtable, parent, engine_ancestor;
    unsigned phase, valid, direct_slot, failure; /* 0 valid; 1..6 retain partial read */
} component_trace_sample;
typedef struct {
    uint64_t calls, sampled, missed, capped;
    unsigned ordinal, count;
    component_trace_sample samples[COMPONENT_TRACE_LIMIT];
} component_trace_site;
typedef struct {
    uint64_t generation;
    uintptr_t ships[2];
    unsigned closed;
    component_trace_site site[COMPONENT_TRACE_SITES];
} component_trace;

static inline int component_trace_begin(component_trace *trace, uintptr_t first, uintptr_t second) {
    if (!trace || !first || !second || first == second) return 0;
    memset(trace, 0, sizeof *trace);
    trace->ships[0] = first; trace->ships[1] = second;
    return 1;
}
static inline void component_trace_flush(component_trace *trace) {
    uint64_t previous = trace->generation;
    for (unsigned site = 0; site < COMPONENT_TRACE_SITES; ++site) {
        /* Count the selected ordinals that did not occur on the prior tick. */
        if (previous) {
            unsigned first = (unsigned)(previous % 8u);
            unsigned second = 8u + (unsigned)((previous / 8u) % 8u);
            trace->site[site].missed += trace->site[site].ordinal <= first;
            trace->site[site].missed += trace->site[site].ordinal <= second;
        }
        trace->site[site].ordinal = 0;
    }
}
static inline int component_trace_tick(component_trace *trace, uint64_t generation) {
    if (!trace || trace->closed || !trace->ships[0] || !generation || generation <= trace->generation) return 0;
    component_trace_flush(trace);
    trace->generation = generation;
    return 1;
}
static inline int component_trace_close(component_trace *trace) {
    if (!trace || trace->closed || !trace->generation) return 0;
    component_trace_flush(trace);
    trace->closed = 1;
    return 1;
}
static inline int component_trace_is_selected(const component_trace *trace, unsigned site) {
    if (!trace || trace->closed || site >= COMPONENT_TRACE_SITES || !trace->generation ||
        trace->site[site].count >= COMPONENT_TRACE_LIMIT) return 0;
    uint64_t ordinal = trace->site[site].ordinal;
    return ordinal == trace->generation % 8u || ordinal == 8u + (trace->generation / 8u) % 8u;
}
static inline void component_trace_record(component_trace *trace, unsigned site, unsigned phase,
                                   uintptr_t object, uintptr_t engine_ancestor,
                                   component_trace_read read, component_trace_class valid_class) {
    if (!trace || trace->closed || site >= COMPONENT_TRACE_SITES || !trace->generation || !read || !valid_class) return;
    component_trace_site *entry = &trace->site[site];
    uint64_t ordinal = entry->ordinal++;
    ++entry->calls;
    unsigned first = (unsigned)(trace->generation % 8u);
    unsigned second = 8u + (unsigned)((trace->generation / 8u) % 8u);
    if (ordinal != first && ordinal != second) return;
    if (entry->count == COMPONENT_TRACE_LIMIT) { ++entry->capped; return; }
    component_trace_sample *sample = &entry->samples[entry->count++];
    *sample = (component_trace_sample){.generation = trace->generation, .ordinal = ordinal,
        .object = object, .engine_ancestor = engine_ancestor, .phase = phase};
    ++entry->sampled;
    struct component_trace_words { uintptr_t vtable; uint64_t runtime; } before = {0}, after = {0};
    uintptr_t parent = 0, parent_after = 0;
    if (!object || object > UINTPTR_MAX - 0x78) { sample->failure = 1; return; }
    if (!read(object, &before, sizeof before)) { sample->failure = 2; return; }
    sample->vtable = before.vtable; sample->runtime = before.runtime;
    if (!valid_class(site, before.vtable) || !before.runtime) { sample->failure = 3; return; }
    if (!read(object + 0x70, &parent, sizeof parent)) { sample->failure = 4; return; }
    sample->parent = parent;
    if (!parent || parent == object) { sample->failure = 4; return; }
    if (!read(object, &after, sizeof after) || !read(object + 0x70, &parent_after, sizeof parent_after)) {
        sample->failure = 5; return;
    }
    if (before.vtable != after.vtable || before.runtime != after.runtime || parent != parent_after) {
        sample->failure = 6; return;
    }
    sample->valid = 1;
    sample->direct_slot = parent == trace->ships[0] ? 1u : parent == trace->ships[1] ? 2u : 0u;
}
#endif
