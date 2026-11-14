#define _GNU_SOURCE
#define VIEW_ANCESTRY_TEST
#define EXPORT
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
static uint32_t current_thread(void) { return (uint32_t)syscall(SYS_gettid); }
#include "player_view_ancestry_preload_impl.h"
uint64_t view_ancestry_ship_original(void *self, void *argument) { (void)self; (void)argument; return 0; }
__asm__(".text\n.global precompare_policy_fixture\n.type precompare_policy_fixture,@function\n"
        "precompare_policy_fixture: push %rbx\npush %r12\nsub $8,%rsp\nmov %rdi,%r12\nmov %rsi,%rbx\n"
        "call view_precompare_bridge\nadd $8,%rsp\npop %r12\npop %rbx\nret\n");
extern uintptr_t precompare_policy_fixture(uintptr_t, uintptr_t);
static unsigned calls, policy_calls, scope, recursive, reject;
static _Atomic unsigned block_original, original_entered, release_original;
static uint32_t owner;
static uintptr_t original(void) {
    assert(errno == 11); ++calls;
    if (atomic_load(&block_original)) {
        atomic_store(&original_entered, 1);
        while (!atomic_load(&release_original)) sched_yield();
    }
    errno = 41; return UINT64_C(0xfedcba9876543210);
}
static int route(uintptr_t object, uintptr_t ancestor, uintptr_t value, uintptr_t *result) {
    ++policy_calls; assert(value == UINT64_C(0xfedcba9876543210)); errno = 99;
    if (reject) return 0;
    if (recursive) (void)precompare_policy_fixture(object, ancestor);
    if (current_thread() == owner && scope == 1 && object == 0x2222 && ancestor == 0x3333)
        *result = ancestor;
    else *result = value;
    return 1;
}
static void *foreign(void *unused) {
    (void)unused; errno = 11;
    assert(precompare_policy_fixture(0x2222, 0x3333) == UINT64_C(0xfedcba9876543210));
    assert(errno == 41); return NULL;
}
static void setup(void) {
    assert(mmap((void *)0x177f000, 4096, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) == (void *)0x177f000);
    assert(mmap((void *)0x1dd8000, 4096, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) == (void *)0x1dd8000);
    unsigned char thunk[12] = {0x48, 0xb8}; uintptr_t dest = (uintptr_t)original;
    memcpy(thunk + 2, &dest, 8); thunk[10] = 0xff; thunk[11] = 0xe0;
    memcpy((void *)0x1dd81c0, thunk, sizeof thunk);
    assert(!mprotect((void *)0x1dd8000, 4096, PROT_READ | PROT_EXEC));
    uint64_t pre = UINT64_C(0x1122334455667788), engine = UINT64_C(0x9988776655443322);
    memcpy((void *)0x177f9a0, &pre, 8); memcpy((void *)0x177f9c0, &engine, 8);
    view_ancestry_information.version = VIEW_ANCESTRY_VERSION;
    view_ancestry_information.installed = 1; view_ancestry_information.pre_enabled = 1;
    view_ancestry_information.pre_block = 0x177f9a0;
    view_ancestry_information.blocks[1] = 0x177f9c0;
    view_ancestry_information.pre_original_slot = (uintptr_t)&view_precompare_original;
    view_ancestry_information.pre_original_target = 0x1dd81c0;
    view_ancestry_information.pre_patched_word = pre;
    view_ancestry_information.patched_words[1] = engine;
    view_ancestry_installed = 1; owner = current_thread();
}
static void run(void) {
    setup(); assert(!view_ancestry_ship_dispatch(NULL, NULL)); errno = 11;
    assert(precompare_policy_fixture(0x2222, 0x3333) == UINT64_C(0xfedcba9876543210) && errno == 41);
    assert(calls == 1 && !policy_calls);
    ((unsigned char *)0x177f9a0)[0] ^= 1;
    assert(!x4mp_stock_view_precompare_bind(route));
    ((unsigned char *)0x177f9a0)[0] ^= 1;
    ((unsigned char *)0x177f9c0)[0] ^= 1;
    assert(!x4mp_stock_view_precompare_bind(route));
    ((unsigned char *)0x177f9c0)[0] ^= 1;
    atomic_store(&block_original, 1);
    pthread_t blocked; assert(!pthread_create(&blocked, NULL, foreign, NULL));
    while (!atomic_load(&original_entered)) sched_yield();
    assert(!x4mp_stock_view_precompare_bind(route));
    atomic_store(&release_original, 1); assert(!pthread_join(blocked, NULL));
    atomic_store(&block_original, 0);
    assert(!x4mp_stock_view_precompare_bind(NULL));
    assert(x4mp_stock_view_precompare_bind(route));
    assert(!x4mp_stock_view_precompare_bind(route));
    scope = 1; errno = 11;
    assert(precompare_policy_fixture(0x2222, 0x3333) == 0x3333 && errno == 41);
    errno = 11; assert(precompare_policy_fixture(0x1111, 0x3333) == UINT64_C(0xfedcba9876543210) && errno == 41);
    errno = 11; assert(precompare_policy_fixture(0x2222, 0x4444) == UINT64_C(0xfedcba9876543210) && errno == 41);
    scope = 0; errno = 11;
    assert(precompare_policy_fixture(0x2222, 0x3333) == UINT64_C(0xfedcba9876543210) && errno == 41);
    scope = 1; pthread_t thread; assert(!pthread_create(&thread, NULL, foreign, NULL)); assert(!pthread_join(thread, NULL));
    assert(calls == 7 && policy_calls == 5);
}
int main(void) {
    run();
    pid_t drift = fork(); assert(drift >= 0);
    if (!drift) { ((unsigned char *)0x177f9a0)[0] ^= 1; errno = 11;
        (void)precompare_policy_fixture(0x2222, 0x3333); _exit(0); }
    int drift_status; assert(waitpid(drift, &drift_status, 0) == drift &&
                             WIFEXITED(drift_status) && WEXITSTATUS(drift_status) == 223);
    pid_t refused = fork(); assert(refused >= 0);
    if (!refused) { reject = 1; errno = 11;
        (void)precompare_policy_fixture(0x2222, 0x3333); _exit(0); }
    int refused_status; assert(waitpid(refused, &refused_status, 0) == refused &&
                               WIFEXITED(refused_status) && WEXITSTATUS(refused_status) == 223);
    pid_t child = fork(); assert(child >= 0);
    if (!child) { recursive = 1; errno = 11; (void)precompare_policy_fixture(0x2222, 0x3333); _exit(0); }
    int status; assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 223);
    return 0;
}
