#ifndef X4_PLAYER_VIEW_COMPONENT_TRACE_PRELOAD_H
#define X4_PLAYER_VIEW_COMPONENT_TRACE_PRELOAD_H
#include <stdint.h>
#include "player_view_component_identity_trace.h"
#define COMPONENT_TRACE_VERSION 1u
/* Called only on the retained observer's worker. 0 outside, 1 parked P1,
 * 2 scoped P2, 3 unidentified transition. Returns zero if it cannot classify. */
typedef int (*component_trace_phase_fn)(uint64_t generation, unsigned *phase);
typedef struct {
    uint32_t version, closed, owner;
    uint64_t foreign_lower[2];
    component_trace trace;
} component_trace_snapshot;
typedef struct {
    uint32_t version, closed, owner;
    uint64_t foreign_lower;
    component_trace trace;
    uintptr_t results[COMPONENT_TRACE_LIMIT];
} component_precompare_snapshot;
int x4mp_component_precompare_finish(component_precompare_snapshot *out);
int x4mp_component_trace_bind(uint32_t owner, uintptr_t first, uintptr_t second, component_trace_phase_fn phase);
int x4mp_component_trace_finish(component_trace_snapshot *out);
#endif
