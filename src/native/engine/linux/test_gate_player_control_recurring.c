#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

static _Thread_local uint64_t fake_thread = 1;
static uint64_t main_thread = 1, ui_thread = 2, stock_observer_worker_thread = 2;
static int boot_phase = 3, path_installed = 1;
static struct { int active; uint64_t issued; } stats;
static uint64_t GetCurrentThreadId(void) { return fake_thread; }
static unsigned long long GetTickCount64(void) {
    struct timespec time;
    assert(!clock_gettime(CLOCK_MONOTONIC, &time));
    return (unsigned long long)time.tv_sec * 1000 + (unsigned long long)time.tv_nsec / 1000000;
}
static int factory_probe_joined_boundary(void) { return fake_thread == 2; }
static int player_lifecycle_discard_requested(void) { return 0; }
static _Noreturn void player_lifecycle_discard_run(unsigned flags) { (void)flags; _exit(199); }
typedef struct { int ignored; } lua_State;
static int accept_ui(void) { return 1; }
static int stock_observer_worker_boundary(void) { return 1; }
static void fake_pushnumber(lua_State *state, double number) { (void)state; (void)number; }
static void fake_pushnil(lua_State *state) { (void)state; }
static void fake_pushlstring(lua_State *state, const char *text, size_t length) {
    (void)state; (void)text; (void)length;
}
static void (*p_lua_pushnumber)(lua_State *, double) = fake_pushnumber;
static void (*p_lua_pushnil)(lua_State *) = fake_pushnil;
static void (*p_lua_pushlstring)(lua_State *, const char *, size_t) = fake_pushlstring;
#define X4_GATE_PLAYER_RECURRING_PAUSE_H
#include "gate_player_recurring_receipt.h"
static void player_recurring_pause_sample(player_recurring_receipt *receipt) {
    receipt->pause_available = 1; receipt->paused = 0; receipt->pause_error = 0;
}
#define X4_GATE_PLAYER_CONTROL_WORKER_TAIL_H
#define LX(address) ((void *)(uintptr_t)(address))
typedef struct { int installed; } player_tail_installation;
static player_tail_installation player_tail_resident;
static void (*fake_tail_callback)(void *, unsigned);
static int fake_case;
static int player_tail_install(player_tail_installation *state, void (*callback)(void *, unsigned)) {
    assert(!state->installed); if (fake_case == 16) return 0;
    state->installed = 1; fake_tail_callback = callback; return 1;
}
static int player_tail_restore(player_tail_installation *state) {
    assert(state->installed); if (fake_case == 17) return 0;
    state->installed = 0; return 1;
}
static unsigned receipt_rows;
static char receipt_last[384];
static void lifecycle_discard_write(const char *message) {
    ++receipt_rows;
    snprintf(receipt_last, sizeof receipt_last, "%s", message);
}
static _Atomic unsigned player_physics_ready;
static void player_physics_discard_begin(void);
static _Noreturn void player_physics_discard_finish(void);
static void player_physics_frames_begin(uint64_t generation);
static void player_physics_frames_finish(uint64_t generation, unsigned final);
static void player_ui_camera_refill_tail(uint64_t generation) { (void)generation; }
static void player_stock_view_observer_close(uint64_t generation) { (void)generation; }
/* This boundary-only fixture loads no authenticated observer provider.
 * Refuse any provider admission; provider authentication has its own fixture. */
static int player_semaphore_pinned_provider(void *symbol, Dl_info *owner) {
    (void)symbol; (void)owner; return 0;
}
#include "gate_player_control_boundary.h"

static unsigned ancestry_stage;
static void player_view_ancestry_before(uint64_t generation) {
    assert(player_recurring_physics_entered() && generation >= 5 && ancestry_stage == 0);
    ancestry_stage = 1;
}
static void player_view_ancestry_after(uint64_t generation) {
    assert(player_recurring_physics_entered() && generation >= 5 && ancestry_stage == 2);
    ancestry_stage = 0;
}
static void player_view_ancestry_close(uint64_t generation) {
    assert(player_recurring_physics_entered() && generation == 7 && ancestry_stage == 0);
    ancestry_stage = 3;
}

static void player_physics_discard_begin(void) {
    assert(player_recurring_physics_entered());
    uint64_t state = atomic_load(&player_recurring_rendezvous.state);
    assert(fake_thread == 2 && fwrr_generation(state) == 5);
    assert((state & FWRR_FLAGS) ==
           (FWRR_ARMED | FWRR_MAIN_PARKED | FWRR_WORKER_ARRIVED | FWRR_ENTERED));
    atomic_store(&player_physics_ready, 1);
}
static _Noreturn void player_physics_discard_finish(void) {
    assert(player_recurring_physics_entered());
    assert(fake_thread == 2 && atomic_load(&player_physics_ready));
    assert(!(atomic_load(&player_recurring_rendezvous.state) & FWRR_RELEASED));
    _exit(223);
}

static unsigned fake_physics_frames;
static void player_physics_frames_begin(uint64_t generation) {
    assert(player_recurring_physics_entered());
    assert(generation == 5 + fake_physics_frames);
    assert(fwrr_generation(atomic_load(&player_recurring_rendezvous.state)) == generation);
    ++fake_physics_frames;
    atomic_store(&player_physics_ready, 1);
}
static void player_physics_frames_finish(uint64_t generation, unsigned final) {
    assert(player_recurring_physics_entered());
    assert(generation == 4 + fake_physics_frames);
    assert(!(atomic_load(&player_recurring_rendezvous.state) & FWRR_RELEASED));
    player_recurring_receipt saved = player_recurring_last_tail;
    assert(saved.available && saved.thread == 2 && fwrr_generation(saved.state) == generation);
    assert(saved.token_owner == (uintptr_t)&player_recurring_rendezvous &&
           saved.token_generation == generation && saved.token_thread == 2 && saved.busy == 1 &&
           saved.main_tick == generation && saved.worker_last == generation - 1);
    char short_buffer[384];
    volatile size_t short_capacity = 8;
    assert(!player_recurring_receipt_format(short_buffer, short_capacity, "tail", &saved));
    unsigned previous_rows = receipt_rows;
    player_recurring_diagnostic_failure();
    assert(receipt_rows == previous_rows + 2 && strstr(receipt_last, "stage=current") &&
           strstr(receipt_last, "pause_available=1 paused=0 pause_error=0"));
    if (final) {
        assert(generation == 7 && fake_physics_frames == 3);
        _exit(223);
    }
}

static void player_physics_network_begin(uint64_t generation) {
    assert(player_recurring_physics == 3 && ancestry_stage == 1);
    player_physics_frames_begin(generation);
    ancestry_stage = 2;
}

static void player_physics_network_finish(uint64_t generation, unsigned final) {
    assert(player_recurring_physics == 3 && ancestry_stage == (final ? 3u : 0u));
    player_physics_frames_finish(generation, final);
}

static void *worker_join(void *ignored) {
    (void)ignored; fake_thread = 2;
    player_control_boundary_after_join();
    if (fake_tail_callback && player_recurring_tail_token.owner) {
        uint64_t state = atomic_load(&player_recurring_rendezvous.state);
        assert((state & (FWRR_ENTERED | FWRR_RELEASED | FWRR_MAIN_AUTHORIZED)) == FWRR_ENTERED);
        assert(atomic_load(&player_recurring_worker_busy));
        if (fake_case == 25) atomic_fetch_and(&player_recurring_rendezvous.state, ~FWRR_WORKER_ARRIVED);
        fake_tail_callback(LX(fake_case == 14 ? 0x3daab21 : 0x3daab20), fake_case == 18 ? 2 : 1);
    }
    return NULL;
}
static void worker_once(void) {
    pthread_t worker; assert(!pthread_create(&worker, NULL, worker_join, NULL));
    assert(!pthread_join(worker, NULL));
}
static void frame(uint64_t tick) {
    stats.issued = tick;
    int armed = player_control_boundary_before_frame();
    assert(armed == 2);
    pthread_t worker; assert(!pthread_create(&worker, NULL, worker_join, NULL));
    player_control_boundary_after_frame(armed);
    assert(!pthread_join(worker, NULL));
}
static void run_case(int which) {
    fake_case = which; stats.active = 1; stats.issued = 4;
    unsetenv("X4MP_SEMAPHORE_OBSERVER");
    unsetenv("X4LAN_PLAYER_CONTROL_RECURRING");
    unsetenv("X4LAN_PLAYER_CONTROL_RECURRING_END");
    unsetenv("X4_DEV_PLAYER_LIFECYCLE");
    unsetenv("X4_DEV_PLAYER_PHYSICS");
    unsetenv("X4LAN_PLAYER_CONTROL_RECURRING_TAIL");
    unsetenv("X4_DEV_UI_SESSION_FRAMES");
    if (which == 0) {
        assert(player_control_boundary_before_frame() == 0);
        worker_once(); _exit(0);
    }
    if (which == 12) {
        stats.issued = 5;
        atomic_store(&player_control_boundary_requested, 1);
        int armed = player_control_boundary_before_frame(); assert(armed == 1);
        pthread_t worker; assert(!pthread_create(&worker, NULL, worker_join, NULL));
        player_control_boundary_after_frame(armed);
        assert(!pthread_join(worker, NULL));
        assert(atomic_load(&player_control_rendezvous.state) & FWR_MAIN_AUTHORIZED);
        _exit(0);
    }
    if (which >= 13) setenv("X4LAN_PLAYER_CONTROL_RECURRING_TAIL", "1", 1);
    if (which >= 20) {
        setenv("X4_DEV_PLAYER_PHYSICS", which == 23 ? "bad" : "single-frame-discard", 1);
        if (which == 21) unsetenv("X4LAN_PLAYER_CONTROL_RECURRING_TAIL");
        if (which == 24) { (void)player_control_boundary_before_frame(); _exit(98); }
    }
    if (which == 19) { (void)player_control_boundary_before_frame(); _exit(98); }
    setenv("X4LAN_PLAYER_CONTROL_RECURRING", which == 6 ? "bad" : "1", 1);
    setenv("X4LAN_PLAYER_CONTROL_RECURRING_END", which == 10 ? "4" : (which >= 20 && which != 22 ? "5" : "7"), 1);
    if (which >= 26) {
        setenv("X4_DEV_PLAYER_PHYSICS", "bounded-frames-discard", 1);
        setenv("X4LAN_PLAYER_CONTROL_RECURRING_END", which == 28 ? "5" : which == 29 ? "185" : "7", 1);
        if (which == 27) unsetenv("X4LAN_PLAYER_CONTROL_RECURRING_TAIL");
    }
    if (which >= 30) {
        setenv("X4_DEV_PLAYER_PHYSICS", "network-frames-discard", 1);
        if (which == 31) unsetenv("X4LAN_PLAYER_CONTROL_RECURRING_TAIL");
        if (which == 32) setenv("X4LAN_PLAYER_CONTROL_RECURRING_END", "5", 1);
    }
    if (which == 33 || which == 34) {
        setenv("X4LAN_PLAYER_CONTROL_RECURRING_END", which == 33 ? "604" : "605", 1);
        (void)player_control_boundary_before_frame();
        assert(which == 33 && player_recurring_end == 604 && player_recurring_physics == 3);
        _exit(0);
    }
    if (which >= 35 && which <= 39) {
        setenv("X4_DEV_UI_SESSION_FRAMES", which == 37 ? "18001" : "18000", 1);
        setenv("X4LAN_PLAYER_CONTROL_RECURRING_END", which == 36 ? "18003" : "18004", 1);
        if (which == 38) unsetenv("X4LAN_PLAYER_CONTROL_RECURRING_TAIL");
        if (which == 39) setenv("X4_DEV_PLAYER_PHYSICS", "bounded-frames-discard", 1);
        (void)player_control_boundary_before_frame();
        assert(which == 35 && player_recurring_end == 18004 && player_session_budget.last_capture == 18002);
        _exit(0);
    }
    if (which == 3) setenv("X4_DEV_PLAYER_LIFECYCLE", "take-release-input-discard", 1);
    if (which == 3 || which == 6 || which == 10 || (which >= 21 && which <= 23) ||
        (which >= 27 && which <= 29) || which >= 31) {
        (void)player_control_boundary_before_frame(); _exit(91);
    }
    assert(player_control_boundary_before_frame() == 0);
    worker_once(); /* pre-window: never touches the rendezvous */
    if (which == 7) {
        atomic_store(&player_recurring_main_tick, 5);
        worker_once(); _exit(92);
    }
    if (which == 8) {
        stats.issued = 5; assert(player_control_boundary_before_frame() == 2);
        player_control_boundary_after_join(); _exit(93);
    }
    if (which == 11) {
        atomic_store(&player_control_boundary_requested, 1);
        (void)player_control_boundary_before_frame(); _exit(94);
    }
    if (which == 15) {
        stats.issued = 5; assert(player_control_boundary_before_frame() == 2);
        fake_tail_callback(LX(0x3daab20), 1); _exit(99);
    }
    frame(5);
    if (which == 4) { worker_once(); _exit(95); }
    if (which == 5) { stats.issued = 7; (void)player_control_boundary_before_frame(); _exit(96); }
    if (which == 9) {
        atomic_store(&player_recurring_worker_busy, 1);
        stats.issued = 6; (void)player_control_boundary_before_frame(); _exit(97);
    }
    frame(6); frame(7);
    stats.issued = 8; assert(player_control_boundary_before_frame() == 0);
    worker_once();
    assert(atomic_load(&player_recurring_mode) == 3);
    _exit(0);
}
static unsigned count_text(const char *text, const char *needle) {
    unsigned count = 0; size_t step = strlen(needle);
    while ((text = strstr(text, needle))) { ++count; text += step; }
    return count;
}
static void scenario(int which, int expected_exit, const char *expected) {
    int fd[2]; assert(!pipe(fd)); pid_t pid = fork(); assert(pid >= 0);
    if (!pid) { close(fd[0]); assert(dup2(fd[1], STDERR_FILENO) >= 0); close(fd[1]); run_case(which); }
    close(fd[1]); char output[8192]; size_t used = 0; ssize_t n;
    while ((n = read(fd[0], output + used, sizeof(output) - used - 1)) > 0) {
        used += (size_t)n; assert(used < sizeof(output) - 1);
    }
    output[used] = 0; close(fd[0]); int status;
    assert(waitpid(pid, &status, 0) == pid);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != expected_exit || (expected && !strstr(output, expected))) {
        fprintf(stderr, "scenario %d status=%d output=%s\n", which, status, output); abort();
    }
    if (which == 13) {
        assert(count_text(output, "tail=1 hook_restored=1") == 1);
        assert(count_text(output, "tail=1 event=enter") == 3);
        assert(count_text(output, "tail=1 event=release") == 3);
        assert(count_text(output, "other_threads_excluded=0 tail=1") == 3);
        const char *enter = strstr(output, "tick=5 tail=1 event=enter");
        const char *release = strstr(output, "tick=5 tail=1 event=release");
        const char *complete = strstr(output, "tick=5 completed=1");
        assert(enter && release && complete && enter < release && release < complete);
    }
    if (which == 2) {
        assert(count_text(output, "completed=1 readiness_only=1") == 3);
        assert(count_text(output, "tick=5 completed=1") == 1);
        assert(count_text(output, "tick=6 completed=1") == 1);
        assert(count_text(output, "tick=7 completed=1") == 1);
        assert(count_text(output, "bank_writes=0 context_writes=0 reference_calls=0 other_threads_excluded=0") == 3);
    }
    if (which == 0) assert(!*output);
    if (which == 26 || which == 30) {
        assert(count_text(output, "readiness_only=0 bank_writes=1") == 2);
        assert(count_text(output, "event=enter") == 3);
        assert(count_text(output, "event=release") == 2);
        assert(!strstr(output, "hook_restored"));
        assert(!strstr(output, "readiness_only=1"));
    }
}
int main(void) {
    /* Keep the existing Lua entrypoints compiled too. */
    (void)&lua_player_control_boundary_request;
    (void)&lua_player_control_boundary_status;
    scenario(0, 0, NULL);
    scenario(2, 0, NULL);
    scenario(3, 222, "reason=one_shot_conflict");
    scenario(4, 222, "reason=worker_arrival");
    scenario(5, 222, "reason=arm_admission");
    scenario(6, 222, "reason=invalid_mode");
    scenario(7, 222, "reason=worker_before_arm");
    scenario(8, 222, "reason=worker_arrival");
    scenario(9, 222, "reason=arm_admission");
    scenario(10, 222, "reason=invalid_end");
    scenario(11, 222, "reason=one_shot_conflict");
    scenario(12, 0, NULL);
    scenario(13, 0, NULL);
    scenario(14, 222, "reason=tail_admission");
    scenario(15, 222, "reason=tail_admission");
    scenario(16, 222, "reason=tail_install");
    scenario(17, 222, "reason=tail_restore");
    scenario(18, 222, "reason=tail_admission");
    scenario(19, 222, "reason=tail_requires_recurring");
    scenario(20, 223, "tick=5 tail=1 event=enter");
    scenario(21, 222, "reason=physics_requires_single_tail");
    scenario(22, 222, "reason=physics_requires_single_tail");
    scenario(23, 222, "reason=invalid_physics_mode");
    scenario(24, 222, "reason=physics_requires_recurring");
    scenario(25, 222, "reason=physics_tail_admission");
    scenario(26, 223, "tick=7 tail=1 event=enter");
    scenario(27, 222, "reason=physics_requires_bounded_tail");
    scenario(28, 222, "reason=physics_requires_bounded_tail");
    scenario(29, 222, "reason=physics_requires_bounded_tail");
    scenario(30, 223, "tick=7 tail=1 event=enter");
    scenario(31, 222, "reason=physics_requires_bounded_tail");
    scenario(32, 222, "reason=physics_requires_bounded_tail");
    scenario(33, 0, NULL);
    scenario(34, 222, "reason=physics_requires_bounded_tail");
    scenario(35, 0, NULL);
    scenario(36, 222, "reason=session_config_mismatch");
    scenario(37, 222, "reason=invalid_session_budget");
    scenario(38, 222, "reason=session_config_mismatch");
    scenario(39, 222, "reason=session_config_mismatch");
    puts("recurring boundary adapter: ok");
}
