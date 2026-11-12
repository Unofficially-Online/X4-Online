#define _GNU_SOURCE
#define VIEW_ANCESTRY_TEST
#define VIEW_ANCESTRY_SYNTHETIC
#define EXPORT
#define PVQ_PAGE 4096u
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
extern void __register_frame(void *);
struct dwarf_eh_bases { void *text, *data, *function; };
extern void *_Unwind_Find_FDE(const void *, struct dwarf_eh_bases *);
static uint32_t current_thread(void) { return (uint32_t)syscall(SYS_gettid); }
static int single_thread(void) { return 1; }
static int is_rx(uintptr_t address) { return address != 0; }
static int ui_code_matches(uintptr_t address, size_t length, const char *hash) {
    return address && length && hash && strlen(hash) == 64;
}
static unsigned char *allocate_near(void) {
    void *page = mmap((void *)0x100000u, PVQ_PAGE, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    return page == MAP_FAILED ? 0 : page;
}
static void emit_entry_stub(unsigned char *page, uintptr_t destination) {
    page[0] = 0xff; page[1] = 0x25; memset(page + 2, 0, 4);
    memcpy(page + 6, &destination, sizeof destination);
}
static int register_frame_for_entry(uintptr_t start, unsigned char *frame) {
    static const unsigned char cie[20] = {16,0,0,0,0,0,0,0,1,0,1,0x78,16,0x0c,7,8,0x90,1,0,0};
    uint32_t length = 20, offset = 24; uint64_t address = start, range = 14;
    memcpy(frame, cie, 20); memcpy(frame + 20, &length, 4); memcpy(frame + 24, &offset, 4);
    memcpy(frame + 28, &address, 8); memcpy(frame + 36, &range, 8);
    __register_frame(frame); return 1;
}
#include "player_view_ancestry_preload_impl.h"
static void map_page(uintptr_t address) {
    void *result = mmap((void *)address, PVQ_PAGE, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(result == (void *)address);
}
int main(void) {
    map_page(0x1113000u); map_page(0x177f000u); map_page(0x129b000u);
    const uint64_t weapon = UINT64_C(0x835b00c57e82e818);
    const uint64_t engine = UINT64_C(0x75c0840068765be8);
    const uint64_t ship = UINT64_C(0xfd89485554415541);
    memcpy((void *)0x1113338u, &weapon, 8);
    memcpy((void *)0x177f9c0u, &engine, 8);
    memcpy((void *)0x129bad0u, &ship, 8);
    /* Continuation starts at +5 with mov rbp,rdi, then returns the sentinel. */
    const unsigned char tail[] = {0x48,0xb8,0x78,0x56,0x34,0x12,0xef,0xcd,0xab,0x90,
                                  0x5d,0x41,0x5c,0x41,0x5d,0xc3};
    memcpy((void *)0x129bad8u, tail, sizeof tail);
    assert(!mprotect((void *)0x1113000u, PVQ_PAGE, PROT_READ | PROT_EXEC));
    assert(!mprotect((void *)0x177f000u, PVQ_PAGE, PROT_READ | PROT_EXEC));
    assert(!mprotect((void *)0x129b000u, PVQ_PAGE, PROT_READ | PROT_EXEC));
    /* A collision must be rejected before any site is changed. */
    assert(!mprotect((void *)0x177f000u, PVQ_PAGE, PROT_READ | PROT_WRITE));
    ((unsigned char *)0x177f9c0u)[0] ^= 1;
    assert(!mprotect((void *)0x177f000u, PVQ_PAGE, PROT_READ | PROT_EXEC));
    assert(!view_ancestry_install());
    assert(*(const uint64_t *)0x1113338u == weapon && *(const uint64_t *)0x129bad0u == ship);
    assert(!mprotect((void *)0x177f000u, PVQ_PAGE, PROT_READ | PROT_WRITE));
    memcpy((void *)0x177f9c0u, &engine, 8);
    assert(!mprotect((void *)0x177f000u, PVQ_PAGE, PROT_READ | PROT_EXEC));
    assert(view_ancestry_install());
    view_ancestry_info info;
    assert(x4mp_view_ancestry_info(&info));
    assert(info.blocks[2] == 0x129bad0u && info.original_words[2] == ship);
    struct dwarf_eh_bases bases;
    for (unsigned index = 0; index < VIEW_ANCESTRY_SITES; ++index)
        assert(_Unwind_Find_FDE((void *)info.stubs[index], &bases));
    assert(_Unwind_Find_FDE((void *)info.ship_trampoline, &bases));
    uint64_t (*method)(void *, void *) = (void *)0x129bad0u;
    errno = 47;
    assert(method((void *)0x55u, (void *)0x66u) == UINT64_C(0x90abcdef12345678));
    assert(errno == 47);
    uintptr_t player[16] = {0}, other[16] = {0};
    player[0] = 0x3d21748; player[1] = 99;
    assert(x4mp_view_ancestry_bind(current_thread(), (uintptr_t)player, (uintptr_t)other));
    assert(x4mp_view_ancestry_tick(9));
    errno = 47;
    assert(method(player, other) == UINT64_C(0x90abcdef12345678) && errno == 47);
    view_ancestry_snapshot snapshot;
    assert(x4mp_view_ancestry_finish(&snapshot));
    assert(snapshot.sites[2].started == 1 && snapshot.sites[2].returned == 1);
    assert(snapshot.sites[2].samples[0].category == 1);
    assert(snapshot.sites[2].samples[0].argument == (uintptr_t)other);
    return 0;
}
