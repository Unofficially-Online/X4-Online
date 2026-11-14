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
static _Atomic uint32_t owner_tid;
static unsigned ui_depth[2], game_render_depth, camera_refill_depth;
static _Thread_local unsigned projection_fov_depth;
static unsigned projection_fov_completed;
static uint32_t current_thread(void) { return 7; }
static _Noreturn void ui_overlay_abort(void) { _exit(223); }
static int single_thread(void) { return 1; }
static int is_rx(uintptr_t address) { (void)address; return 1; }
static void *allocate_near(void) { return NULL; }
static void emit_entry_stub(unsigned char *page, uintptr_t target) { (void)page; (void)target; }
static void lockstep_sha256(void *data, size_t size, unsigned char hash[32]) { (void)data; (void)size; memset(hash, 0, 32); }
static int digest_matches(unsigned char hash[32], const char *wanted) { (void)hash; (void)wanted; return 1; }
static int register_frame_for_entry(uintptr_t address, unsigned char *frame) { (void)address; (void)frame; return 1; }
static int player_ui_escape_guard_prepare(void) { return 1; }
static void player_ui_escape_guard_call(void (*original)(void *), void *value,
                                       int (*enter)(uintptr_t), int (*leave)(uintptr_t)) {
    int before = errno;
    assert(enter((uintptr_t)value));
    errno = before;
    original(value);
    int after = errno;
    assert(leave((uintptr_t)value));
    errno = after;
}
#include "player_projection_fov_scope_preload_impl.h"
static unsigned calls, enters, leaves;
static int admitted = 1;
static float original(void *camera) { assert(camera == (void *)123 && errno == 11); ++calls; errno = 29; return 1.25f; }
static int enter(uintptr_t camera) { assert(camera == 123); ++enters; errno = 42; return admitted; }
static int leave(uintptr_t camera) { assert(camera == 123); ++leaves; errno = 43; return 1; }
int main(void) {
    (void)install_projection_fov_scope;
    projection_fov_original = original;
    errno = 11;
    assert(player_projection_fov_dispatch((void *)123) == 1.25f && errno == 29 && calls == 1 && !enters);
    atomic_store(&projection_fov_installed, 1);
    atomic_store(&owner_tid, 7);
    /* Lua binds beside camera refill during the admitted UI callback. A
     * worker outside that callback and a nested callback cannot bind. */
    assert(!x4mp_projection_fov_scope_bind(7, 123, enter, leave));
    ui_depth[0] = 2;
    assert(!x4mp_projection_fov_scope_bind(7, 123, enter, leave));
    ui_depth[0] = 1;
    assert(x4mp_projection_fov_scope_bind(7, 123, enter, leave));
    assert(!x4mp_projection_fov_scope_bind(7, 123, enter, leave));
    ui_depth[0] = 1;
    assert(x4mp_projection_fov_scope_activate());
    ui_depth[0] = 0;
    errno = 11;
    assert(player_projection_fov_dispatch((void *)123) == 1.25f && errno == 29);
    assert(calls == 2 && enters == 1 && leaves == 1);
    assert(projection_fov_completed == 1);
    admitted = 2; /* Native call still runs once when this epoch is outside the scope. */
    errno = 11;
    assert(player_projection_fov_dispatch((void *)123) == 1.25f && errno == 29 && calls == 3);
    assert(projection_fov_completed == 1); /* Stock-only calls do not close a lease. */
    pid_t child = fork();
    assert(child >= 0);
    if (!child) { projection_fov_depth = 1; (void)player_projection_fov_dispatch((void *)123); _exit(0); }
    int status = 0;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 223);
    assert(x4mp_projection_fov_scope_disarm());
    return 0;
}
