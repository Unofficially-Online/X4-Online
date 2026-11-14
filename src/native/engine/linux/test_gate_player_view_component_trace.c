#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "player_view_component_trace_preload.h"
typedef struct { int ignored; } ancestry_p1_identity;
static struct { int (*finish)(void *); } ancestry_sealed_provider;
static struct { struct { unsigned pre_enabled; } information; } ancestry_api;
static unsigned ancestry_mode = 3;
static uint32_t ancestry_tid;
static uintptr_t ancestry_p1_ship = 0x10000, ancestry_p2_ship = 0x20000;
static uint64_t ancestry_first_tick = 8;
static struct { uint64_t issued; } stats = {8};
static struct { uint64_t generation; } player_recurring_tail_token = {8};
static void *player_routes_scope;
static int player_recurring_physics_entered(void) { return 1; }
static int player_physics_identity_for(unsigned scoped) { return scoped && player_routes_scope; }
static int player_physics_identity(void) { return !player_routes_scope; }
static void *gate_tls_self(void) { return &stats; }
static int gate_tls_worker(void *value) { return value != NULL; }
static int ancestry_identity(ancestry_p1_identity *value) { return value != NULL; }
static _Noreturn void ancestry_fail(const char *reason) { (void)reason; _exit(223); }
static int player_view_pin_same_module(uintptr_t p, void *base, const char *perms) {
    (void)base; (void)perms; return p != 0;
}
static int fake_view_finish(void *out) { (void)out; return 1; }
static component_trace_phase_fn bound_phase;
static int fake_bind(uint32_t owner, uintptr_t first, uintptr_t second, component_trace_phase_fn phase) {
    assert(owner == ancestry_tid && first == ancestry_p1_ship && second == ancestry_p2_ship);
    bound_phase = phase; return 1;
}
static int fake_finish(component_trace_snapshot *out) {
    memset(out, 0, sizeof *out); out->version = COMPONENT_TRACE_VERSION; out->closed = 1; out->owner = ancestry_tid;
    out->trace.closed = 1; out->trace.generation = 8;
    out->trace.ships[0] = ancestry_p1_ship; out->trace.ships[1] = ancestry_p2_ship;
    component_trace_site *site = &out->trace.site[0]; site->calls = site->sampled = site->count = 1;
    site->samples[0] = (component_trace_sample){.generation=8, .phase=2, .object=0x30000,
        .vtable=0x3c6a440, .runtime=7, .parent=0x20000, .valid=1, .direct_slot=2};
    return 1;
}
static int fake_pre_finish(component_precompare_snapshot *out) {
    memset(out, 0, sizeof *out); out->version = 1; out->closed = 1; out->owner = ancestry_tid;
    out->trace.closed = 1; out->trace.generation = 8;
    out->trace.ships[0] = ancestry_p1_ship; out->trace.ships[1] = ancestry_p2_ship;
    component_trace_site *site = &out->trace.site[1]; site->calls = site->sampled = site->count = 1;
    site->samples[0] = (component_trace_sample){.generation=8, .phase=1, .object=0x30000,
        .vtable=0x3ccad80, .runtime=7, .parent=0x20000, .engine_ancestor=0x20000, .valid=1, .direct_slot=2};
    out->results[0] = ancestry_p1_ship; return 1;
}
static int fake_dladdr(const void *p, Dl_info *info) { (void)p; memset(info, 0, sizeof *info); info->dli_fbase = (void *)1; return 1; }
static void *fake_dlsym(void *handle, const char *name) {
    (void)handle;
    if (!strcmp(name, "x4mp_component_precompare_finish")) return (void *)fake_pre_finish;
    if (!strcmp(name, "x4mp_component_trace_bind")) return (void *)fake_bind;
    if (!strcmp(name, "x4mp_component_trace_finish")) return (void *)fake_finish;
    return NULL;
}
#define dladdr fake_dladdr
#define dlsym fake_dlsym
#include "gate_player_view_component_trace.h"
int main(void) {
    ancestry_tid = (uint32_t)syscall(SYS_gettid); ancestry_sealed_provider.finish = fake_view_finish;
    assert(!setenv("X4_DEV_VIEW_COMPONENT_TRACE", "1", 1));
    assert(!setenv("X4_DEV_VIEW_PRECOMPARE_TRACE", "1", 1)); ancestry_api.information.pre_enabled = 1;
    view_component_trace_gate_begin(8); assert(bound_phase);
    unsigned phase = 0; assert(bound_phase(8, &phase) && phase == 1);
    player_routes_scope = &stats; assert(bound_phase(8, &phase) && phase == 2);
    player_routes_scope = NULL; view_component_trace_gate_finish(8);
    assert(!component_trace_gate_enabled);
    return 0;
}
