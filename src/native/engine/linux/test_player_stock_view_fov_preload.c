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
static int single_thread(void) { return 1; }
static int is_rx(uintptr_t address) { (void)address; return 1; }
static void *allocate_near(void) { return NULL; }
static void emit_entry_stub(unsigned char *page, uintptr_t target) { (void)page; (void)target; }
static void lockstep_sha256(void *data, size_t size, unsigned char hash[32]) { (void)data; (void)size; memset(hash, 0, 32); }
static int digest_matches(unsigned char hash[32], const char *wanted) { (void)hash; (void)wanted; return 1; }
static int register_frame_for_entry(uintptr_t address, unsigned char *frame) { (void)address; (void)frame; return 1; }
#include "player_stock_view_fov_preload_impl.h"

static unsigned original_calls;
static float original_fov(void *camera) {
    assert(camera == (void *)(uintptr_t)7 && errno == 11);
    ++original_calls;
    errno = 29;
    return 2.0f;
}
static int mode_policy(uintptr_t controller, int original, unsigned site, int *result) {
    assert(controller == 42 && original == 7 && site == 1);
    errno = 99;
    *result = 1;
    return 1;
}
static int value_policy(uintptr_t controller, float original, float *result) {
    assert(controller == 42 && original == 2.0f && errno == 29);
    errno = 98;
    *result = 1.5f;
    return 1;
}
static unsigned observer_calls;
static void observer(uintptr_t controller, uintptr_t camera, float value) {
    assert(controller == 42 && camera == 7 && value == 2.0f); ++observer_calls; errno = 100;
}
static int refuse_mode(uintptr_t controller, int original, unsigned site, int *result) {
    (void)controller; (void)original; (void)site; (void)result; return 0;
}
int main(void) {
    uint64_t patch;
    assert(stock_view_fov_patch_word(stock_view_fov_sites[0], stock_view_fov_sites[0] + 100,
                                     0, stock_view_fov_original_words[0], &patch));
    unsigned char code[8]; memcpy(code, &patch, 8);
    assert(code[0] == 0xe9 && code[5] == 0x90 && code[6] == 0xf3 && code[7] == 0x0f);
    assert(stock_view_fov_patch_word(stock_view_fov_sites[2], stock_view_fov_sites[2] + 100,
                                     1, stock_view_fov_original_words[2], &patch));
    memcpy(code, &patch, 8);
    assert(code[0] == 0xe8 && code[5] == 0xf3 && code[6] == 0x0f && code[7] == 0x10);
    stock_view_fov_original = original_fov;
    errno = 11;
    assert(player_stock_view_fov_dispatch((void *)(uintptr_t)7, 42) == 2.0f && errno == 29);
    atomic_store(&stock_view_fov_ready, 1);
    assert(x4mp_stock_view_fov_observe_bind(observer));
    assert(!x4mp_stock_view_fov_observe_bind(observer));
    errno = 11;
    assert(player_stock_view_fov_dispatch((void *)(uintptr_t)7, 42) == 2.0f && errno == 29);
    assert(original_calls == 2 && observer_calls == 1);
    assert(!stock_view_policy_enter(&stock_view_fov_active, &stock_view_fov_inflight));
    assert(!x4mp_stock_view_fov_bind(mode_policy, value_policy));
    stock_view_policy_leave(&stock_view_fov_inflight);
    assert(x4mp_stock_view_fov_bind(mode_policy, value_policy));
    assert(!x4mp_stock_view_fov_bind(mode_policy, value_policy));
    errno = 11;
    assert(player_stock_view_mode_dispatch(42, 7, 1) == 1 && errno == 11);
    errno = 11;
    assert(player_stock_view_fov_dispatch((void *)(uintptr_t)7, 42) == 1.5f && errno == 29);
    assert(original_calls == 3 && observer_calls == 2);
    pid_t child = fork();
    assert(child >= 0);
    if (!child) { stock_view_fov_mode_policy = refuse_mode; player_stock_view_mode_dispatch(42, 7, 1); _exit(1); }
    int status = 0;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 223);
    for (int site = 0; site < 2; ++site) {
        child = fork(); assert(child >= 0);
        if (!child) {
            atomic_store(&stock_view_fov_active, 2);
            if (site) (void)player_stock_view_fov_dispatch((void *)(uintptr_t)7, 42);
            else (void)player_stock_view_mode_dispatch(42, 7, 1);
            _exit(0);
        }
        assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 223);
    }
    return 0;
}
