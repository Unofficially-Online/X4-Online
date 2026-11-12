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

static unsigned calls[3], check_reentry;
static uint64_t original_weapon(void) {
    ++calls[0]; assert(errno == 11);
    if (check_reentry) assert(!x4mp_view_ancestry_tick(10));
    errno = 31; return UINT64_C(0x123456789abcdef0);
}
static uint64_t original_engine(void) { ++calls[1]; assert(errno == 12); errno = 32; return UINT64_C(0xfedcba9876543210); }
uint64_t view_ancestry_ship_original(void *self, void *argument) {
    ++calls[2]; assert(self && argument && errno == 13); errno = 33; return UINT64_C(0xabcdeffedcba1234);
}
/* Exercise the actual bridge instructions with component in native callee-saved registers. */
__asm__(".text\n.global fixture_weapon\n.type fixture_weapon,@function\n"
        "fixture_weapon: push %rbx\nmov %rdi,%rbx\ncall view_ancestry_weapon_bridge\npop %rbx\nret\n"
        ".global fixture_engine\n.type fixture_engine,@function\n"
        "fixture_engine: push %r12\nmov %rdi,%r12\ncall view_ancestry_engine_bridge\npop %r12\nret\n");
extern uint64_t fixture_weapon(void *);
extern uint64_t fixture_engine(void *);
static void *foreign_call(void *object) {
    errno = 11;
    assert(fixture_weapon(object) == UINT64_C(0x123456789abcdef0));
    assert(errno == 31);
    return 0;
}
int main(void) {
    struct component { uintptr_t vtable; uint64_t id; char padding[0x60]; uintptr_t parent; };
    struct component ship = { 0x3d21748, 123, {0}, 0 };
    struct component weapon = { 0x3c6a440, 456, {0}, (uintptr_t)&ship };
    struct component second_ship = { 0x3d21748, 124, {0}, 0 };
    struct component second_weapon = { 0x3c6a440, 457, {0}, (uintptr_t)&second_ship };
    view_ancestry_originals[0] = original_weapon;
    view_ancestry_originals[1] = original_engine;
    view_ancestry_installed = 1;
    assert(x4mp_view_ancestry_bind(current_thread(), (uintptr_t)&ship, (uintptr_t)&second_ship));
    errno = 11; /* Binding alone never records a generation-zero row. */
    assert(fixture_weapon(&weapon) == UINT64_C(0x123456789abcdef0));
    assert(x4mp_view_ancestry_tick(9));
    assert(!x4mp_view_ancestry_tick(9));
    check_reentry = 1;
    errno = 11;
    assert(fixture_weapon(&weapon) == UINT64_C(0x123456789abcdef0) && errno == 31);
    check_reentry = 0;
    for (unsigned index = 0; index < 2; ++index) {
        errno = 11;
        assert(fixture_weapon(&weapon) == UINT64_C(0x123456789abcdef0));
    }
    errno = 12;
    assert(fixture_engine(&weapon) == UINT64_C(0xfedcba9876543210) && errno == 32);
    errno = 13;
    assert(view_ancestry_ship_dispatch(&ship, &weapon) == UINT64_C(0xabcdeffedcba1234) && errno == 33);
    /* Invalid pointers and cycles only produce bounded, incomplete receipts. */
    assert(x4mp_view_ancestry_tick(15));
    for (unsigned index = 0; index < 2; ++index) {
        errno = 11;
        assert(fixture_weapon(index ? &second_weapon : (void *)1) == UINT64_C(0x123456789abcdef0));
    }
    assert(x4mp_view_ancestry_tick(21));
    weapon.vtable = 0x1234;
    for (unsigned index = 0; index < 3; ++index) {
        errno = 11;
        assert(fixture_weapon(&weapon) == UINT64_C(0x123456789abcdef0));
    }
    weapon.vtable = 0x3c6a440;
    weapon.parent = (uintptr_t)&weapon;
    assert(x4mp_view_ancestry_tick(27));
    for (unsigned index = 0; index < 4; ++index) {
        errno = 11;
        assert(fixture_weapon(&weapon) == UINT64_C(0x123456789abcdef0));
    }
    weapon.parent = (uintptr_t)&ship;
    for (unsigned index = 0; index < 130; ++index) {
        assert(x4mp_view_ancestry_tick(33 + index * 6));
        for (unsigned call = 0; call < 16; ++call) {
            errno = 11;
            assert(fixture_weapon(&weapon) == UINT64_C(0x123456789abcdef0));
        }
    }
    pthread_t thread;
    assert(!pthread_create(&thread, 0, foreign_call, &weapon));
    assert(!pthread_join(thread, 0));
    view_ancestry_snapshot snapshot;
    assert(x4mp_view_ancestry_finish(&snapshot));
    assert(snapshot.closed && snapshot.sites[0].started == snapshot.sites[0].returned);
    assert(snapshot.sites[0].examined == 128 && snapshot.sites[0].overflow > 0 &&
           snapshot.sites[0].started == snapshot.sites[0].examined +
               snapshot.sites[0].overflow + snapshot.sites[0].throttled);
    assert(snapshot.sites[0].count <= VIEW_ANCESTRY_SAMPLES);
    assert(snapshot.sites[0].foreign == 1 && snapshot.sites[0].samples[0].self == (uintptr_t)&weapon);
    assert(snapshot.sites[0].samples[0].count == 2 && snapshot.sites[0].samples[0].complete);
    assert(snapshot.sites[0].samples[0].nodes[1].component == (uintptr_t)&ship);
    assert(snapshot.sites[0].category_count[1] && snapshot.sites[0].category_count[2]);
    assert(snapshot.sites[0].samples[1].self == (uintptr_t)&second_weapon);
    assert(snapshot.sites[1].samples[0].self == (uintptr_t)&weapon);
    assert(snapshot.sites[2].samples[0].argument == (uintptr_t)&weapon);
    assert(calls[0] == snapshot.sites[0].started + 2 && calls[1] == 1 && calls[2] == 1);
    assert(!x4mp_view_ancestry_finish(&snapshot));
    return 0;
}
