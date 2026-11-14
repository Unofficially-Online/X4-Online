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
#define STOCK_VIEW_FOV_TEST
static int collision;
static int single_thread(void) { return 1; }
static int is_rx(uintptr_t address) { (void)address; return 1; }
static void *allocate_near(void) {
    void *p = mmap((void *)0x1700000, 4096, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}
static void emit_entry_stub(unsigned char *page, uintptr_t target) {
    page[0] = 0xff; page[1] = 0x25; memset(page + 2, 0, 4); memcpy(page + 6, &target, 8);
}
static void lockstep_sha256(void *data, size_t size, unsigned char hash[32]) { (void)data; (void)size; memset(hash, 0, 32); }
static int digest_matches(unsigned char hash[32], const char *wanted) { (void)hash; (void)wanted; return 1; }
static int register_frame_for_entry(uintptr_t address, unsigned char *frame) { (void)address; (void)frame; return 1; }
static void stock_view_fov_test_before_cas(unsigned index) {
    if (collision && index == 1) {
        uint64_t foreign = UINT64_C(0x1111222233334444);
        memcpy((void *)0x1666e87, &foreign, sizeof foreign);
        if (collision == 2) memcpy((void *)0x1666d2a, &foreign, sizeof foreign);
    }
}
#include "player_stock_view_fov_preload_impl.h"
static uint64_t read_word(uintptr_t address) { uint64_t word; memcpy(&word, (void *)address, 8); return word; }
static void run_case(int should_collide) {
    void *mapped = mmap((void *)0x1666000, 4096, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(mapped == (void *)0x1666000);
    for (unsigned i = 0; i < 3; ++i) memcpy((void *)stock_view_fov_sites[i], &stock_view_fov_original_words[i], 8);
    collision = should_collide;
    int ok = install_stock_view_fov();
    if (should_collide) {
        assert(!ok && !atomic_load(&stock_view_fov_ready));
        assert(read_word(stock_view_fov_sites[0]) == (should_collide == 2 ?
               UINT64_C(0x1111222233334444) : stock_view_fov_original_words[0]));
        assert(read_word(stock_view_fov_sites[1]) == UINT64_C(0x1111222233334444));
        assert(read_word(stock_view_fov_sites[2]) == stock_view_fov_original_words[2]);
    } else {
        assert(ok && atomic_load(&stock_view_fov_ready));
        for (unsigned i = 0; i < 3; ++i) assert(read_word(stock_view_fov_sites[i]) == stock_view_fov_information.patched[i]);
    }
}
int main(void) {
    for (int collision_case = 0; collision_case < 3; ++collision_case) {
        pid_t child = fork(); assert(child >= 0);
        if (!child) { run_case(collision_case); _exit(0); }
        int status = 0; assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    return 0;
}
