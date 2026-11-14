#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
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
#include "player_view_render_fov_observer_impl.h"
static unsigned originals, observations;
static float original(void *camera) { assert(camera == (void *)7 && errno == 11); ++originals; errno = 29; return 1.25f; }
static void observe(uintptr_t camera, float value) { assert(camera == 7 && value == 1.25f); ++observations; errno = 42; }
int main(void) {
    (void)install_render_fov_observer; render_fov_original = original;
    errno = 11; assert(player_view_render_fov_dispatch((void *)7) == 1.25f && errno == 29 && !observations);
    atomic_store(&render_fov_ready, 1);
    assert(x4mp_view_render_fov_bind(observe)); assert(!x4mp_view_render_fov_bind(observe));
    errno = 11; assert(player_view_render_fov_dispatch((void *)7) == 1.25f && errno == 29);
    assert(originals == 2 && observations == 1);
    return 0;
}
