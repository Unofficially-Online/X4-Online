#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "player_view_component_identity_trace.h"

typedef struct { uintptr_t vtable; uint64_t runtime; unsigned char pad[0x60]; uintptr_t parent; } object;
static object sample_object;
static unsigned reads, unstable;
static int read_object(uintptr_t address, void *out, unsigned size) {
    if (address != (uintptr_t)&sample_object && address != (uintptr_t)&sample_object.parent) return 0;
    memcpy(out, (void *)address, size);
    if (unstable && ++reads == 2) sample_object.parent = 0x3000;
    return 1;
}
static int valid_class(unsigned site, uintptr_t vtable) {
    return site < 2 && vtable == (site ? 0x3ccad80u : 0x3c6a440u);
}
int main(void) {
    component_trace trace;
    assert(!component_trace_begin(&trace, 1, 1));
    assert(component_trace_begin(&trace, 0x1000, 0x2000));
    assert(component_trace_tick(&trace, 8)); /* indices 0 and 9 */
    sample_object = (object){.vtable = 0x3c6a440, .runtime = 42, .parent = 0x2000};
    for (unsigned i = 0; i < 10; ++i)
        component_trace_record(&trace, 0, 3, (uintptr_t)&sample_object, 0, read_object, valid_class);
    assert(trace.site[0].calls == 10 && trace.site[0].sampled == 2);
    assert(trace.site[0].samples[0].direct_slot == 2 && trace.site[0].samples[0].phase == 3);
    sample_object.vtable = 0x3ccad80; sample_object.parent = 0x1000;
    component_trace_record(&trace, 1, 1, (uintptr_t)&sample_object, 0x1000, read_object, valid_class);
    assert(trace.site[1].samples[0].direct_slot == 1 && trace.site[1].samples[0].engine_ancestor == 0x1000);
    assert(component_trace_tick(&trace, 16));
    assert(trace.site[0].missed == 0 && trace.site[1].missed == 1);
    sample_object.vtable = 0x3c6a440; sample_object.parent = 0x2000; unstable = 1; reads = 0;
    component_trace_record(&trace, 0, 2, (uintptr_t)&sample_object, 0, read_object, valid_class);
    assert(!trace.site[0].samples[2].valid && !trace.site[0].samples[2].direct_slot);
    assert(component_trace_tick(&trace, 17)); /* first selected ordinal now 1 */
    unstable = 0; sample_object.parent = 0x1000;
    component_trace_record(&trace, 0, 0, (uintptr_t)&sample_object, 0, read_object, valid_class);
    assert(trace.site[0].count == 3);
    component_trace_record(&trace, 0, 0, (uintptr_t)&sample_object, 0, read_object, valid_class);
    assert(trace.site[0].count == 4 && trace.site[0].samples[3].ordinal == 1 &&
           trace.site[0].samples[3].direct_slot == 1);
    assert(component_trace_close(&trace) && !component_trace_close(&trace));
    assert(!component_trace_tick(&trace, 18));
    return 0;
}
