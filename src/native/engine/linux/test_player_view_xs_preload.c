#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#define PVQ_PAGE 4096
#define EXPORT
#define PLAYER_VIEW_XS_TEST
static unsigned checks, collision;
static int single_thread(void) {
    if (++checks == 3 && collision) ((unsigned char *)0x129a779)[0] = 0x90;
    return 1;
}
static int is_rx(uintptr_t address) { (void)address; return 1; }
static unsigned char *allocate_near(void) {
    void *p = mmap((void *)0x1400000, 4096, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}
static void emit_entry_stub(unsigned char *page, uintptr_t target) {
    page[0] = 0xff; page[1] = 0x25; memset(page + 2, 0, 4); memcpy(page + 6, &target, 8);
}
static void lockstep_sha256(void *data, size_t size, unsigned char hash[32]) { (void)data; (void)size; memset(hash, 0, 32); }
static int digest_matches(unsigned char hash[32], const char *wanted) { (void)hash; (void)wanted; return 1; }
static int register_frame_for_entry(uintptr_t address, unsigned char *frame) { (void)address; (void)frame; return 1; }
#include "player_view_xs_preload_impl.h"
__asm__(".text\n.global fixture_xs_target\n.type fixture_xs_target,@function\n"
        "fixture_xs_target: push %r12\nmov %rdi,%r12\ncall player_view_xs_target_bridge\n"
        "pop %r12\nret\n");
extern uintptr_t fixture_xs_target(uintptr_t);
static unsigned calls[3];
static uintptr_t target_original(void) { ++calls[0]; assert(errno == 11); errno = 21; return 0x111; }
static uint64_t external_original(void) { ++calls[1]; assert(errno == 12); errno = 22; return UINT64_C(0xaabbccddeeff00a5); }
static float distance_original(void *ship, void *camera, unsigned flags) {
    ++calls[2]; assert(ship == (void *)0x123 && camera == (void *)0x456 && flags == 1 && errno == 13);
    errno = 23; return 2.0f;
}
static int target_decision = 1;
static int route_target(uintptr_t ship, uintptr_t original, uintptr_t *out) {
    assert(ship == 0x123 && original == 0);
    errno = 91;
    *out = ship;
    return target_decision;
}
static int route_external(uint64_t original, uint64_t *out) { errno = 92; *out = (original & ~UINT64_C(0xff)) | 1; return 1; }
static int route_distance(uintptr_t ship, uintptr_t camera, unsigned flags, float original, float *out) {
    assert(ship == 0x123 && camera == 0x456 && flags == 1 && original == 2.0f); errno = 93; *out = 3.0f; return 1;
}
static void run_case(unsigned fail) {
    assert(mmap((void *)0x129a000, 4096, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) == (void *)0x129a000);
    for (unsigned i = 0; i < 3; ++i) memcpy((void *)xs_sites[i], xs_original_bytes[i], 5);
    collision = fail;
    if (fail) {
        assert(!install_view_xs());
        assert(!memcmp((void *)xs_sites[0], xs_original_bytes[0], 5));
        assert(!memcmp((void *)xs_sites[1], xs_original_bytes[1], 5));
        return;
    }
    assert(install_view_xs());
    assert(xs_still_owned() && !x4mp_view_xs_bound());
    xs_target_original = target_original;
    errno = 11; assert(fixture_xs_target(0x123) == 0x111 && errno == 21);
    assert(!x4mp_view_xs_bind(route_target, route_external, route_distance));
    xs_target_original = (uintptr_t (*)(void))0x1c90380;
    assert(x4mp_view_xs_bind(route_target, route_external, route_distance));
    assert(x4mp_view_xs_bound());
    xs_target_original = target_original; xs_external_original = external_original; xs_distance_original = distance_original;
    xs_test_targets[0] = (uintptr_t)target_original;
    xs_test_targets[1] = (uintptr_t)external_original;
    xs_test_targets[2] = (uintptr_t)distance_original;
    assert(!x4mp_view_xs_bind(route_target, route_external, route_distance));
    errno = 11; assert(fixture_xs_target(0x123) == 0x123 && errno == 11);
    errno = 12; assert(player_view_xs_external_dispatch() == UINT64_C(0xaabbccddeeff0001) && errno == 22);
    errno = 13; assert(player_view_xs_distance_dispatch((void *)0x123, (void *)0x456, 1) == 3.0f && errno == 23);
    assert(calls[0] == 1 && calls[1] == 1 && calls[2] == 1);
    target_decision = 2;
    errno = 11; assert(fixture_xs_target(0x123) == 0x111 && errno == 21);
    assert(calls[0] == 2);
    target_decision = 0;
    pid_t bad_scope = fork(); assert(bad_scope >= 0);
    if (!bad_scope) { errno = 11; (void)fixture_xs_target(0x123); _exit(99); }
    int status = 0;
    assert(waitpid(bad_scope, &status, 0) == bad_scope && WIFEXITED(status) && WEXITSTATUS(status) == 223);
}
int main(void) {
    for (unsigned fail = 0; fail < 2; ++fail) {
        pid_t child = fork(); assert(child >= 0);
        if (!child) { run_case(fail); _exit(0); }
        int status = 0; assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    return 0;
}
