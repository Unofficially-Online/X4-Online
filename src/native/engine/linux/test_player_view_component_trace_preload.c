#define _GNU_SOURCE
#define VIEW_ANCESTRY_TEST
#define EXPORT
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>
static uint32_t current_thread(void) { return (uint32_t)syscall(SYS_gettid); }
#include "player_view_ancestry_preload_impl.h"
static unsigned calls[2], pre_calls;
static uintptr_t pre_original(void) { ++pre_calls; errno = 33; return 0xdeadbeef; }
static uint64_t weapon_original(void) { ++calls[0]; errno = 31; return 5; }
static uint64_t engine_original(void) { ++calls[1]; errno = 32; return 7; }
uint64_t view_ancestry_ship_original(void *self, void *argument) { (void)self; (void)argument; return 0; }
__asm__(".text\n.global component_trace_weapon_fixture\n.type component_trace_weapon_fixture,@function\n"
        "component_trace_weapon_fixture: push %rbx\nmov %rdi,%rbx\ncall view_ancestry_weapon_bridge\npop %rbx\nret\n"
        ".global component_precompare_fixture\n.type component_precompare_fixture,@function\n"
        "component_precompare_fixture: push %rbx\npush %r12\nsub $8,%rsp\nmov %rdi,%r12\nmov %rsi,%rbx\n"
        "call view_precompare_bridge\nadd $8,%rsp\npop %r12\npop %rbx\nret\n"
        ".global component_trace_engine_fixture\n.type component_trace_engine_fixture,@function\n"
        "component_trace_engine_fixture: push %rbx\npush %r12\nsub $8,%rsp\nmov %rdi,%r12\nmov %rsi,%rbx\n"
        "call view_ancestry_engine_bridge\nadd $8,%rsp\npop %r12\npop %rbx\nret\n");
extern uintptr_t component_precompare_fixture(void *, void *);
extern uint64_t component_trace_weapon_fixture(void *);
extern uint64_t component_trace_engine_fixture(void *, void *);
static int phase(uint64_t generation, unsigned *out) { assert(generation == 8); *out = 2; return 1; }
static void *foreign(void *object) {
    assert(component_trace_weapon_fixture(object) == 5);
    assert(component_precompare_fixture(object, object) == 0xdeadbeef);
    return 0;
}
int main(void) {
    struct component { uintptr_t vtable; uint64_t runtime; char padding[0x60]; uintptr_t parent; };
    struct component first = {0x3d21748, 1, {0}, 0}, second = {0x3d21748, 2, {0}, 0};
    struct component weapon = {0x3c6a440, 3, {0}, (uintptr_t)&second};
    struct component engine = {0x3ccad80, 4, {0}, (uintptr_t)&second};
    view_precompare_original = pre_original;
    view_ancestry_originals[0] = weapon_original; view_ancestry_originals[1] = engine_original;
    assert(!view_ancestry_ship_dispatch(NULL, NULL));
    view_ancestry_installed = 1;
    assert(x4mp_view_ancestry_bind(current_thread(), (uintptr_t)&first, (uintptr_t)&second));
    assert(x4mp_view_ancestry_tick(8));
    assert(x4mp_component_trace_bind(current_thread(), (uintptr_t)&first, (uintptr_t)&second, phase));
    for (unsigned i = 0; i < 10; ++i) {
        errno = 11; assert(component_trace_weapon_fixture(&weapon) == 5 && errno == 31);
        errno = 12; assert(component_trace_engine_fixture(&engine, &second) == 7 && errno == 32);
        errno = 13; assert(component_precompare_fixture(&engine, &second) == 0xdeadbeef && errno == 33);
    }
    pthread_t thread; assert(!pthread_create(&thread, 0, foreign, &weapon)); assert(!pthread_join(thread, 0));
    component_trace_snapshot snapshot;
    assert(x4mp_component_trace_finish(&snapshot));
    assert(snapshot.version == 1 && snapshot.closed && snapshot.foreign_lower[0] == 1);
    assert(snapshot.trace.site[0].count == 2 && snapshot.trace.site[1].count == 2);
    assert(snapshot.trace.site[0].samples[0].direct_slot == 2 && snapshot.trace.site[0].samples[0].phase == 2);
    assert(snapshot.trace.site[1].samples[0].direct_slot == 2 &&
           snapshot.trace.site[1].samples[0].engine_ancestor == (uintptr_t)&second);
    component_precompare_snapshot early;
    assert(x4mp_component_precompare_finish(&early));
    assert(early.foreign_lower == 1 && early.trace.site[1].count == 2 && early.trace.site[1].calls == 10);
    assert(early.trace.site[1].samples[0].direct_slot == 2 && early.trace.site[1].samples[0].phase == 2);
    assert(early.trace.site[1].samples[0].engine_ancestor == (uintptr_t)&second && early.results[0] == 0xdeadbeef);
    assert(calls[0] == 11 && calls[1] == 10 && pre_calls == 11);
    return 0;
}
