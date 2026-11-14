/* Execute only finish/stop routes from the real gate, with a synthetic image.
 * The larger fixture supplies stand-ins; its main and network tests never run. */
#define main unused_gate_fixture_main
#include "test_gate_linux.c"
#undef main
static struct { unsigned forwarded, returned; } *stop_markers;
ssize_t __real_write(int fd, const void *buffer, size_t length);
ssize_t __wrap_write(int fd, const void *buffer, size_t length) {
    const char prefix[] = "X4LAN WORKER_DISPATCH_TERMINAL ";
    if (length >= sizeof prefix - 1 && !memcmp(buffer, prefix, sizeof prefix - 1)) {
        /* The first cleanup operation must not have run, regardless of caller. */
        if (!memmem(buffer, length, "source=takeover-after-stop", 26)) assert(!keyboard_probe_closed);
        assert(!stop_markers->forwarded && !stop_markers->returned);
    }
    return __real_write(fd, buffer, length);
}
static void stop_forward_marker(void *frame, unsigned char loading) {
    (void)frame; (void)loading; ++stop_markers->forwarded;
}
static unsigned stop_route;
static void *stop_worker(void *unused) {
    (void)unused;
    ui_thread = GetCurrentThreadId();
    if (stop_route == 1) (void)x4gate_stop();
    else if (stop_route == 2) (void)lua_stop(NULL);
    else finish(7);
    ++stop_markers->returned;
    gate_original_once((void *)0x1234, 0);
    return NULL;
}
static void terminal_child(unsigned route, unsigned phase, unsigned published, unsigned active) {
    reset_session();
    gate_frame_tick = stop_forward_marker;
    keyboard_probe_requested = 1; keyboard_probe_closed = 0;
    stats.active = (int)active;
    atomic_store(&player_dispatch.phase, phase);
    assert(player_dispatch_stop_claim());
    if (published) player_dispatch_stop_published();
    stop_route = route;
    if (route == 3) {
        /* Real validation-failure path that would otherwise forward FrameTick. */
        stats.active = 1;
        main_thread = GetCurrentThreadId();
        gate_tick((void *)0x1234, 1);
    } else if (route == 4) finish(8); /* main peer/permit failure */
    else {
        pthread_t worker; assert(!pthread_create(&worker, NULL, stop_worker, NULL));
        assert(!pthread_join(worker, NULL));
    }
    ++stop_markers->returned;
    gate_original_once((void *)0x1234, 0);
    _exit(99);
}
static pthread_barrier_t stop_claim_barrier;
static void *simultaneous_takeover(void *unused) {
    (void)unused;
    int result = pthread_barrier_wait(&stop_claim_barrier);
    assert(!result || result == PTHREAD_BARRIER_SERIAL_THREAD);
    /* Request arbitration must either claim before finish, or terminate before
     * any provider lookup/publication when legacy cleanup won. */
    if (!player_dispatch_stop_claim())
        player_dispatch_stop_terminate(0, atomic_load(&player_dispatch_stop_ownership), "takeover-after-stop");
    /* Finish must see REQUESTED even without provider registration completing. */
    for (;;) sched_yield();
}
static void simultaneous_child(void) {
    reset_session(); keyboard_probe_requested = 1; keyboard_probe_closed = 0;
    gate_frame_tick = stop_forward_marker;
    assert(!pthread_barrier_init(&stop_claim_barrier, NULL, 2));
    pthread_t contender; assert(!pthread_create(&contender, NULL, simultaneous_takeover, NULL));
    int result = pthread_barrier_wait(&stop_claim_barrier);
    assert(!result || result == PTHREAD_BARRIER_SERIAL_THREAD);
    finish(1);
    /* Legacy stop won. Keep this thread here until late takeover terminates;
     * actual request cannot publish callbacks after the stop latch. */
    for (;;) sched_yield();
}
int main(void) {
    synth_image();
    stop_markers = mmap(NULL, sizeof *stop_markers, PROT_READ | PROT_WRITE,
                        MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    assert(stop_markers != MAP_FAILED);
    const unsigned phases[] = {PWDA_OFF, PWDA_REQUESTED, PWDA_STARTUP_POST, PWDA_PARKED, PWDA_ARMED};
    for (unsigned index = 0; index < sizeof phases / sizeof *phases; ++index) {
        for (unsigned route = 0; route < 5; ++route) {
            pid_t child = fork(); assert(child >= 0);
            if (!child) terminal_child(route, phases[index], index % 2, index % 2);
            int status; assert(waitpid(child, &status, 0) == child);
            assert(WIFEXITED(status) && WEXITSTATUS(status) == 225);
            assert(!stop_markers->returned && !stop_markers->forwarded);
        }
    }
    for (unsigned race = 0; race < 20; ++race) {
        pid_t child = fork(); assert(child >= 0);
        if (!child) simultaneous_child();
        int status; assert(waitpid(child, &status, 0) == child);
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 225);
        assert(!stop_markers->returned && !stop_markers->forwarded);
    }
    /* Never-taken-over retains cleanup and native forwarding, even when an
     * opt-in configuration was selected but takeover has not been requested. */
    reset_session();
    player_dispatch_enabled = 1;
    keyboard_probe_requested = 1; keyboard_probe_closed = 0;
    stats.active = 1;
    gate_frame_tick = stop_forward_marker;
    finish(7);
    assert(keyboard_probe_closed && !stats.active && stats.reason == 7);
    gate_original_once((void *)0x1234, 0);
    assert(stop_markers->forwarded == 1);
    assert(atomic_load(&player_dispatch_stop_ownership) == PLAYER_DISPATCH_LEGACY_STOP);
    assert(!player_dispatch_stop_claim());
    /* Another legacy stop still performs the old cleanup. */
    keyboard_probe_closed = 0; finish(8); assert(keyboard_probe_closed && stats.reason == 8);
    puts("dispatch terminate-only stop: 25 real finish/public/Lua/failure routes, 20 claim races and default cleanup ok");
    return 0;
}
