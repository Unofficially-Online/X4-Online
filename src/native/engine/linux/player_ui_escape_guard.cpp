#include "player_ui_escape_guard.h"
#include <dlfcn.h>
#include <errno.h>
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>

/* One TLS flag protects only the thread executing the disposable UI scope.
 * The process exits on a covered escape; no restoration is claimed. */
using jump_function = void (*)(jmp_buf, int);
static jump_function next_longjmp;
/* Preload-only initial-exec TLS is initialized before the guarded scope. */
static thread_local volatile sig_atomic_t ui_scope_active __attribute__((tls_model("initial-exec")));

extern "C" __attribute__((visibility("default"))) int player_ui_escape_guard_prepare(void) {
    if (ui_scope_active) return 0;
    jump_function target = __atomic_load_n(&next_longjmp, __ATOMIC_ACQUIRE);
    if (!target) {
        target = reinterpret_cast<jump_function>(dlsym(RTLD_NEXT, "_longjmp"));
        if (!target) return 0;
        __atomic_store_n(&next_longjmp, target, __ATOMIC_RELEASE);
    }
    return 1;
}

/* Pre-resolve for normal inactive operation; prepare rechecks before arming. */
#ifndef PLAYER_UI_GUARD_TEST_NO_CONSTRUCTOR
__attribute__((constructor)) static void player_ui_escape_guard_initialize(void) {
    (void)player_ui_escape_guard_prepare();
}
#endif

extern "C" __attribute__((visibility("default"), noreturn))
void _longjmp(jmp_buf environment, int value) noexcept {
    int saved_errno = errno;
    if (ui_scope_active) {
        static const char message[] = "X4 UI OVERLAY FATAL reason=guarded_longjmp\n";
        (void)write(2, message, sizeof message - 1);
        _exit(223);
    }
    jump_function target = __atomic_load_n(&next_longjmp, __ATOMIC_ACQUIRE);
    if (!target && player_ui_escape_guard_prepare())
        target = __atomic_load_n(&next_longjmp, __ATOMIC_ACQUIRE);
    if (!target) _exit(223);
    errno = saved_errno;
    target(environment, value);
    __builtin_unreachable();
}

extern "C" __attribute__((visibility("default"))) void player_ui_escape_guard_call(void (*original)(void *), void *object,
                                 int (*enter)(uintptr_t), int (*leave)(uintptr_t)) {
    if (!original || !enter || !leave || ui_scope_active ||
        !__atomic_load_n(&next_longjmp, __ATOMIC_ACQUIRE)) _exit(223);
    int before = errno;
    int after = 0;
    ui_scope_active = 1;
    try {
        if (!enter(reinterpret_cast<uintptr_t>(object))) _exit(223);
        errno = before;
        original(object);
        after = errno;
        if (!leave(reinterpret_cast<uintptr_t>(object))) _exit(223);
    } catch (...) {
        static const char message[] = "X4 UI OVERLAY FATAL reason=caught_cpp\n";
        (void)write(2, message, sizeof message - 1);
        _exit(223);
    }
    ui_scope_active = 0;
    errno = after;
}
