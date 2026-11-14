#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
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
#define STOCK_VIEW_SHIP_TEST
#define VIEW_ANCESTRY_SHARED_COMPONENT
static unsigned view_ancestry_installed;
static struct { uintptr_t blocks[3]; uint64_t original_words[3], patched_words[3]; } view_ancestry_information;
static int collision;
static int single_thread(void) { return 1; }
static int is_rx(uintptr_t address) { (void)address; return 1; }
static void *allocate_near(void) {
    void *p = mmap((void *)0x1400000, 4096, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}
static void emit_entry_stub(unsigned char *page, uintptr_t target) {
    page[0] = 0xff; page[1] = 0x25; memset(page + 2, 0, 4); memcpy(page + 6, &target, 8);
}
static void lockstep_sha256(void *data, size_t size, unsigned char hash[32]) {
    uint64_t entry;
    assert(size == 456);
    memcpy(&entry, data, sizeof entry);
    assert(entry == UINT64_C(0xfd89485554415541));
    memset(hash, 0, 32);
}
static int digest_matches(unsigned char hash[32], const char *wanted) { (void)hash; (void)wanted; return 1; }
static void stock_view_ship_test_before_cas(void) {
    if (collision) { uint64_t foreign = UINT64_C(0x1111222233334444); memcpy((void *)0x129bb21, &foreign, 8); }
}
#include "player_stock_view_ship_preload_impl.h"
static int policy(uintptr_t ship, int original, int *result) {
    assert(ship == 123 && original == 7); errno = 99; *result = 3; return 1;
}
static void run_case(int scenario) {
    void *mapped = mmap((void *)0x129b000, 4096, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(mapped == (void *)0x129b000);
    uint64_t original = UINT64_C(0xc985000008808a8b);
    memcpy((void *)0x129bb21, &original, 8);
    uint64_t entry = UINT64_C(0xfd89485554415541);
    if (scenario >= 2) {
        view_ancestry_installed = 1;
        view_ancestry_information.blocks[2] = 0x129bad0;
        view_ancestry_information.original_words[2] = entry;
        view_ancestry_information.patched_words[2] = UINT64_C(0x123456789abcdef0);
        entry = scenario == 2 ? view_ancestry_information.patched_words[2] : UINT64_C(0xabcdef);
    }
    memcpy((void *)0x129bad0, &entry, 8);
    collision = scenario == 1;
    int ok = install_stock_view_ship();
    uint64_t observed; memcpy(&observed, (void *)0x129bb21, 8);
    if (scenario == 1) {
        assert(!ok && !atomic_load(&stock_view_ship_ready) && observed == UINT64_C(0x1111222233334444));
    } else if (scenario == 3) {
        assert(!ok && !atomic_load(&stock_view_ship_ready) && observed == original);
    } else {
        assert(ok && atomic_load(&stock_view_ship_ready) && observed == stock_view_ship_information.patched);
        assert(!stock_view_policy_enter(&stock_view_ship_active, &stock_view_ship_inflight));
        assert(!x4mp_stock_view_ship_bind(policy));
        stock_view_policy_leave(&stock_view_ship_inflight);
        assert(x4mp_stock_view_ship_bind(policy) && !x4mp_stock_view_ship_bind(policy));
        errno = 11; assert(player_stock_view_ship_dispatch(123, 7) == 3 && errno == 11);
    }
}
int main(void) {
    for (int scenario = 0; scenario < 4; ++scenario) {
        pid_t child = fork(); assert(child >= 0);
        if (!child) { run_case(scenario); _exit(0); }
        int status = 0; assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    pid_t child = fork(); assert(child >= 0);
    if (!child) {
        atomic_store(&stock_view_ship_active, 2);
        (void)player_stock_view_ship_dispatch(123, 7);
        _exit(0);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 223);
    return 0;
}
