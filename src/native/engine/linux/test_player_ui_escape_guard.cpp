#include "player_ui_escape_guard.h"
#include <cassert>
#include <dlfcn.h>
#include <cerrno>
#include <cstring>
#include <setjmp.h>
#include <signal.h>
#include <pthread.h>
#include <sys/wait.h>
#include <unistd.h>

static int (*prepare_guard)(void);
static void (*guard_call)(void (*)(void *), void *, int (*)(uintptr_t), int (*)(uintptr_t));
struct escape_exception {};
static int allow(uintptr_t) { errno = EDOM; return 1; }
static int throws_enter_or_leave(uintptr_t) { throw escape_exception{}; }
static jmp_buf environment;
static void ordinary(void *value) { assert(errno == ERANGE); ++*static_cast<int *>(value); errno = EBUSY; }
static void throws_exception(void *) { throw escape_exception{}; }
static void noop(void *) {}
static void *foreign_jump(void *) {
    jmp_buf local;
    int result = _setjmp(local);
    if (!result) _longjmp(local, 11);
    return reinterpret_cast<void *>(static_cast<uintptr_t>(result));
}
static void foreign_during_scope(void *) {
    pthread_t thread;
    assert(!pthread_create(&thread, nullptr, foreign_jump, nullptr));
    void *result = nullptr;
    assert(!pthread_join(thread, &result) && reinterpret_cast<uintptr_t>(result) == 11);
}
static void jumps(void *) { _longjmp(environment, 7); }
static void jump_from_signal(int) { _longjmp(environment, 7); }
static void signal_jump(void *) {
    struct sigaction action = {};
    action.sa_handler = jump_from_signal;
    sigemptyset(&action.sa_mask);
    assert(!sigaction(SIGUSR1, &action, nullptr));
    assert(!raise(SIGUSR1));
}
static void expect_exit(void (*callback)(void *), bool use_jump,
                        int (*enter)(uintptr_t) = allow, int (*leave)(uintptr_t) = allow) {
    int output[2];
    assert(!pipe(output));
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        close(output[0]);
        assert(dup2(output[1], 2) == 2);
        close(output[1]);
        if (use_jump && _setjmp(environment)) _exit(99);
        try { guard_call(callback, nullptr, enter, leave); }
        catch (...) { _exit(98); }
        _exit(97);
    }
    close(output[1]);
    char message[128] = {};
    ssize_t count = read(output[0], message, sizeof message - 1);
    close(output[0]);
    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 223);
    const char *expected = use_jump ? "X4 UI OVERLAY FATAL reason=guarded_longjmp\n" :
                                      "X4 UI OVERLAY FATAL reason=caught_cpp\n";
    assert(count == static_cast<ssize_t>(strlen(expected)) && !strcmp(message, expected));
}
int main() {
    prepare_guard = reinterpret_cast<int (*)(void)>(dlsym(RTLD_DEFAULT, "player_ui_escape_guard_prepare"));
    guard_call = reinterpret_cast<void (*)(void (*)(void *), void *, int (*)(uintptr_t), int (*)(uintptr_t))>(dlsym(RTLD_DEFAULT, "player_ui_escape_guard_call"));
    assert(prepare_guard && guard_call);
    errno = EDOM;
    int result = _setjmp(environment);
    if (!result) _longjmp(environment, 7);
    assert(result == 7 && errno == EDOM);
    assert(prepare_guard());
    int calls = 0;
    errno = ERANGE;
    guard_call(ordinary, &calls, allow, allow);
    assert(calls == 1 && errno == EBUSY);
    guard_call(foreign_during_scope, nullptr, allow, allow);
    expect_exit(throws_exception, false);
    expect_exit(jumps, true);
    expect_exit(signal_jump, true);
    expect_exit(noop, false, throws_enter_or_leave, allow);
    expect_exit(noop, false, allow, throws_enter_or_leave);
}
