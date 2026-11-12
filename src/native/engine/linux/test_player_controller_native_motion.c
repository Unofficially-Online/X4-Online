#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include "gate_player_controller_native_motion.h"
typedef struct {
    void *self;
    unsigned long mode;
    double time;
} result;
/* These continuations stand at the false-branch destinations and inspect the
 * exact registers and stack established by the copied executable prologue. */
__attribute__((naked, noinline)) static void continuation0(void) {
    __asm__ volatile("mov %rbx,0(%rbp); mov %r12,8(%rbp); movsd %xmm5,16(%rbp); add $0x138,%rsp; pop %rbx; "
                     "pop %rbp; pop %r12; pop %r13; pop %r14; pop %r15; ret");
}
__attribute__((naked, noinline)) static void continuation1(void) {
    __asm__ volatile("mov %rbx,0(%rbp); mov %r12,8(%rbp); movsd %xmm0,16(%rbp); add $0x50,%rsp; pop %rbx; "
                     "pop %rbp; pop %r12; pop %r13; pop %r14; ret");
}
__attribute__((naked, noinline)) static void continuation2(void) {
    __asm__ volatile("mov %rbp,0(%rbx); mov %r12,8(%rbx); movsd %xmm0,16(%rbx); add $0x30,%rsp; pop %rbx; "
                     "pop %rbp; pop %r12; ret");
}
int main(void) {
    (void)player_native_fde_pins;
    (void)player_native_bind;
    (void)player_routes_install;
    (void)player_routes_enter;
    (void)player_routes_leave;
    (void)player_routes_restore;
    long size = sysconf(_SC_PAGESIZE);
    assert(size >= 3 * PLAYER_NATIVE_STUB_SIZE);
    unsigned char *page =
        mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(page != MAP_FAILED);
    const unsigned char *original[] = {player_native_head0, player_native_head1, player_native_head2};
    const void *target[] = {(void *)continuation0, (void *)continuation1, (void *)continuation2};
    player_route_motion adapters[3];
    assert(player_native_build_false_branches(page, (size_t)size, original, target, adapters));
    assert(!player_native_build_false_branches(page, 1, original, target, adapters));
    unsigned char bad[sizeof player_native_head0];
    memcpy(bad, player_native_head0, sizeof bad);
    bad[0] ^= 1;
    const unsigned char *mismatch[] = {bad, player_native_head1, player_native_head2};
    assert(!player_native_build_false_branches(page, (size_t)size, mismatch, target, adapters));
    assert(!mprotect(page, (size_t)size, PROT_READ | PROT_EXEC));
    for (unsigned i = 0; i < 3; i++) {
        result r = {0};
        void *self = (void *)0x12345678;
        adapters[i](self, &r, 231, 123.625);
        assert(r.self == self && r.mode == 231 && r.time == 123.625);
    }
    assert(!munmap(page, (size_t)size));
    puts("ELF prologue branch adapters SysV ABI PASS");
}
