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
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#define PVQ_PAGE 4096
#define EXPORT
#define STOCK_VIEW_COMPONENT_TEST
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
static void lockstep_sha256(void *data, size_t size, unsigned char hash[32]) { (void)data; (void)size; memset(hash, 0, 32); }
static int digest_matches(unsigned char hash[32], const char *wanted) { (void)hash; (void)wanted; return 1; }
static int register_frame_for_entry(uintptr_t address, unsigned char *frame) { (void)address; (void)frame; return 1; }
static void stock_view_component_test_before_cas(unsigned index) {
    if (collision && index == 1) {
        uint64_t foreign = UINT64_C(0x1111222233334444);
        memcpy((void *)0x177f9c0, &foreign, 8);
        if (collision == 2) memcpy((void *)0x1113338, &foreign, 8);
    }
}
#include "player_stock_view_component_preload_impl.h"
#define VIEW_ANCESTRY_TEST
#define VIEW_ANCESTRY_SHARED_COMPONENT
static uint32_t current_thread(void) { return (uint32_t)syscall(SYS_gettid); }
#include "player_view_ancestry_preload_impl.h"
uint64_t view_ancestry_ship_original(void *object, void *argument) {
    (void)object; (void)argument; return 0;
}
static unsigned calls[2], policies;
static unsigned policy_case;
static uint64_t original_weapon(void) { assert(errno == 11); ++calls[0]; errno = 31; return UINT64_C(0x123456789abcde01); }
static uint64_t original_engine(void) { assert(errno == 12); ++calls[1]; errno = 32; return UINT64_C(0xfedcba9876543200); }
static int policy(unsigned site, uintptr_t component, unsigned char original, unsigned char *result) {
    assert(site < 2 && component == 0x1234);
    ++policies; errno = 99;
    if (policy_case == 1) { *result = original; return 1; }
    if (policy_case == 2) { *result = 0xa6; return 1; }
    assert(original == (site == 0));
    *result = (unsigned char)!original; return 1;
}
__asm__(".text\n.global fixture_weapon\n.type fixture_weapon,@function\n"
        "fixture_weapon: push %rbx\nmov %rdi,%rbx\ncall player_stock_view_weapon_bridge\npop %rbx\nret\n"
        ".global fixture_engine\n.type fixture_engine,@function\n"
        "fixture_engine: push %r12\nmov %rdi,%r12\ncall player_stock_view_engine_bridge\npop %r12\nret\n");
extern uint64_t fixture_weapon(uintptr_t);
extern uint64_t fixture_engine(uintptr_t);
static uint64_t read_word(uintptr_t address) { uint64_t word; memcpy(&word, (void *)address, 8); return word; }
static void run_case(int scenario) {
    assert(mmap((void *)0x1113000, 4096, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) == (void *)0x1113000);
    assert(mmap((void *)0x177f000, 4096, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) == (void *)0x177f000);
    for (unsigned i = 0; i < 2; ++i) memcpy((void *)stock_view_component_blocks[i], &stock_view_component_words[i], 8);
    collision = scenario;
    int ok = install_stock_view_components();
    if (scenario) {
        assert(!ok && !atomic_load(&stock_view_component_ready));
        assert(read_word(stock_view_component_blocks[0]) == (scenario == 2 ?
               UINT64_C(0x1111222233334444) : stock_view_component_words[0]));
        assert(read_word(stock_view_component_blocks[1]) == UINT64_C(0x1111222233334444));
        return;
    }
    assert(ok && atomic_load(&stock_view_component_ready));
    for (unsigned i = 0; i < 2; ++i) assert(read_word(stock_view_component_blocks[i]) == stock_view_component_information.patched[i]);
    stock_view_component_original[0] = original_weapon;
    stock_view_component_original[1] = original_engine;
    errno = 11; assert(fixture_weapon(0x1234) == UINT64_C(0x123456789abcde01) && errno == 31);
    assert(stock_view_component_enter() == 0);
    assert(!x4mp_stock_view_component_bind(policy));
    assert(stock_view_component_finish(0, 0, 0, 0) == 0);
    assert(x4mp_stock_view_component_bind(policy) && !x4mp_stock_view_component_bind(policy));
    errno = 11; assert(fixture_weapon(0x1234) == UINT64_C(0x123456789abcde00) && errno == 31);
    errno = 12; assert(fixture_engine(0x1234) == UINT64_C(0xfedcba9876543201) && errno == 32);
    assert(calls[0] == 2 && calls[1] == 1 && policies == 2);
    policy_case = 1;
    assert(stock_view_component_enter() == 1);
    assert(stock_view_component_finish(1, 0, 0x1234, UINT64_C(0x123456789abcdea5)) ==
           UINT64_C(0x123456789abcdea5));
    pid_t bad_policy = fork(); assert(bad_policy >= 0);
    if (!bad_policy) {
        policy_case = 2;
        assert(stock_view_component_enter() == 1);
        (void)stock_view_component_finish(1, 0, 0x1234, UINT64_C(0x123456789abcdea5));
        _exit(0);
    }
    int bad_status = 0;
    assert(waitpid(bad_policy, &bad_status, 0) == bad_policy && WIFEXITED(bad_status) &&
           WEXITSTATUS(bad_status) == 223);
    policy_case = 0;
    view_ancestry_originals[0] = original_weapon;
    view_ancestry_originals[1] = original_engine;
    view_ancestry_installed = 1;
    assert(x4mp_view_ancestry_bind(current_thread(), 0x1000, 0x2000));
    assert(x4mp_view_ancestry_tick(1));
    errno = 11;
    assert(view_ancestry_weapon_dispatch((void *)0x1234) == UINT64_C(0x123456789abcde00) && errno == 31);
    errno = 12;
    assert(view_ancestry_engine_dispatch((void *)0x1234, 0x5000) == UINT64_C(0xfedcba9876543201) && errno == 32);
    assert(calls[0] == 3 && calls[1] == 2 && policies == 5);
    view_ancestry_snapshot snapshot;
    assert(x4mp_view_ancestry_finish(&snapshot));
    assert(snapshot.sites[0].started == 1 && snapshot.sites[0].returned == 1 &&
           snapshot.sites[1].started == 1 && snapshot.sites[1].returned == 1);
}
int main(void) {
    for (int scenario = 0; scenario < 3; ++scenario) {
        pid_t child = fork(); assert(child >= 0);
        if (!child) { run_case(scenario); _exit(0); }
        int status = 0; assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    pid_t child = fork(); assert(child >= 0);
    if (!child) {
        atomic_store(&stock_view_component_active, 2);
        (void)stock_view_component_enter();
        _exit(0);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 223);
    return 0;
}
