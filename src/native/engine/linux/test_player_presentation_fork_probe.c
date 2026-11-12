#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
static int receipt_mode, delay_reap, wait_tracking, observed_eof, delayed_wait_complete;
static int fault_map_fd = -1, fault_map_reads;
static volatile sig_atomic_t atfork_prepared, atfork_parent, atfork_child;
static int test_atfork;
static volatile sig_atomic_t inherited_signal_called;
static int signal_fd = -1;
static void inherited_signal_handler(int signal_number) {
    (void)signal_number;
    inherited_signal_called = 1;
    (void)write(signal_fd, "x", 1);
}
static ssize_t test_read(int fd, void *data, size_t length);
static pid_t test_waitpid(pid_t pid, int *status, int options);
static ssize_t test_receipt_write(int fd, const void *data, size_t length);
#define write test_receipt_write
#define read test_read
#define waitpid test_waitpid
#include "player_presentation_fork_probe.h"
#undef write
#undef read
#undef waitpid
static ssize_t test_read(int fd, void *data, size_t length) {
    if (fd == fault_map_fd) {
        if (fault_map_reads++ > 0) { errno = EIO; return -1; }
        if (length > 17) length = 17;
    }
    ssize_t count = syscall(SYS_read, fd, data, length);
    if (delay_reap && wait_tracking && count == 0) observed_eof = 1;
    return count;
}
static pid_t test_waitpid(pid_t pid, int *status, int options) {
    if (delay_reap && options == WNOHANG && !delayed_wait_complete) {
        wait_tracking = 1;
        if (observed_eof) {
            struct timespec delay = {.tv_nsec = 80000000};
            nanosleep(&delay, NULL);
            delayed_wait_complete = 1;
        }
        return 0;
    }
    return waitpid(pid, status, options);
}
static void atfork_prepare(void) { ++atfork_prepared; }
static void atfork_parent_done(void) { ++atfork_parent; }
static void atfork_child_done(void) { ++atfork_child; }
static ssize_t test_receipt_write(int fd, const void *data, size_t length) {
    if (receipt_mode == 1) {
        (void)syscall(SYS_write, fd, data, length / 2);
        _exit(0);
    }
    if (receipt_mode == 3) {
        assert(length == sizeof(presentation_fork_receipt));
        presentation_fork_receipt changed;
        memcpy(&changed, data, sizeof changed);
        changed.child_pid += 1;
        return syscall(SYS_write, fd, &changed, sizeof changed);
    }
    ssize_t count = syscall(SYS_write, fd, data, length);
    if (receipt_mode == 2 && count == (ssize_t)length) {
        char extra = 0;
        (void)syscall(SYS_write, fd, &extra, 1);
    }
    return count;
}
typedef struct {
    unsigned char *p1, *p2, *room1, *room2;
    int quiet, quiet_refused, private_refused, graph_refused, child_mode, fail_restore;
    int cached_mode, cache_calls, release_calls;
    int inherited_fd, child_private_ok, leaves, parent_change, parent_reads, parent_unrelated_change, parent_full_reads;
    pid_t parent_pid;
} fixture;
static int quiet_enter(void *context) { fixture *f = context; if (f->quiet_refused) return 0; f->quiet = 1; return 1; }
static void quiet_leave(void *context) { fixture *f = context; assert(f->quiet); f->quiet = 0; ++f->leaves; }
static int private_range(void *context, uintptr_t address, size_t length) {
    fixture *f = context; return f->quiet && address && length && !f->private_refused;
}
static int private_graph(void *context, const presentation_fork_request *request, const presentation_fork_maps *maps) {
    fixture *f = context; assert(maps->length); return f->quiet && request->room == (uintptr_t)f->room2 && !f->graph_refused;
}
static int child_setup(void *context) {
    fixture *f = context;
    if (test_atfork) {
        sigset_t pending;
        if (kill(getpid(), SIGUSR1) || sigpending(&pending) ||
            sigismember(&pending, SIGUSR1) != 1 || inherited_signal_called) return 0;
    }
    return f->quiet && (!test_atfork || (!atfork_prepared && !atfork_parent && !atfork_child)) &&
        fcntl(f->inherited_fd, F_GETFD) < 0 && errno == EBADF;
}
static int owner(void *context, uintptr_t p1, uintptr_t p2, uintptr_t room, uint32_t bank) {
    fixture *f = context; return f->quiet && p1 == (uintptr_t)f->p1 && p2 == (uintptr_t)f->p2 &&
        room == (uintptr_t)f->room2 && bank == 0;
}
static int read_bytes(void *context, uintptr_t address, void *out, size_t size) {
    fixture *f = context;
    if (getpid() != f->parent_pid && f->cached_mode == 1 &&
        address == (uintptr_t)f->room2 + 0x40 && *(uint32_t *)address == 6) return 0;
    if (getpid() != f->parent_pid && f->cached_mode == 5 && f->release_calls &&
        address == (uintptr_t)f->room2 + 0x40) return 0;
    if (getpid() == f->parent_pid && address == (uintptr_t)f->p1 + 0x70 && size == 8 &&
        ++f->parent_reads == 2 && f->parent_change) {
        uintptr_t changed = (uintptr_t)f->room2;
        memcpy((void *)address, &changed, sizeof changed);
    }
    if (getpid() == f->parent_pid && address == (uintptr_t)f->p1 && size == PRESENTATION_PLAYER_EXTENT &&
        ++f->parent_full_reads == 2 && f->parent_unrelated_change) f->p1[0x500] = 1;
    memcpy(out, (void *)address, size); return 1;
}
static int write_bytes(void *context, uintptr_t address, const void *input, size_t size) {
    fixture *f = context;
    if (f->fail_restore && address == (uintptr_t)f->p1 + 0x70 && size == 8 &&
        *(const uintptr_t *)input == (uintptr_t)f->room1) return 0;
    memcpy((void *)address, input, size); return 1;
}
static uint64_t occupied(void *context) {
    fixture *f = context;
    if (f->child_mode == 1) raise(SIGSEGV);
    if (f->child_mode == 2) for (;;) pause();
    uintptr_t parent; memcpy(&parent, f->p1 + 0x70, 8);
    return parent == (uintptr_t)f->room1 ? 11 : parent == (uintptr_t)f->room2 ? 22 : 0;
}
static uintptr_t live_room(void *context, uintptr_t player) {
    (void)context; uintptr_t room; memcpy(&room, (void *)(player + 0x70), 8); return room;
}
static uintptr_t cached_room(void *context, uintptr_t player) {
    fixture *f = context;
    assert(++f->cache_calls == 1);
    if (f->cached_mode == 3) return (uintptr_t)1; /* Must never be dereferenced. */
    uintptr_t room; memcpy(&room, (void *)(player + 0xe0), 8);
    if (room) *(uint32_t *)(room + 0x40) += f->cached_mode == 2 ? 2 : 1;
    return room;
}
static int release_room(void *context, uintptr_t room) {
    fixture *f = context;
    assert(f->cached_mode != 3 && room == (uintptr_t)f->room2 && ++f->release_calls == 1);
    if (f->cached_mode == 4) return 0; /* May have failed after a side effect. */
    --*(uint32_t *)(room + 0x40);
    return 1;
}
static unsigned char *page(size_t length) {
    void *result = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(result != MAP_FAILED); return result;
}
static void set_pointer(unsigned char *base, size_t offset, uintptr_t value) { memcpy(base + offset, &value, 8); }
/* A parent difference is terminal. Isolate each fake parent so the test never
 * releases a lease that production must retain until process exit. */
static void test_terminal_parent_change(fixture *f, presentation_fork_request *request,
                                        presentation_fork_ops *ops, int unrelated) {
    pid_t process = fork();
    assert(process >= 0);
    if (!process) {
        f->parent_pid = getpid();
        f->parent_reads = f->parent_full_reads = 0;
        f->parent_change = !unrelated;
        f->parent_unrelated_change = unrelated;
        presentation_fork_result result;
        assert(!player_presentation_fork_run(request, ops, &result));
        assert(!strcmp(result.reason, "parent_changed") && result.lease_held && f->quiet);
        _exit(0);
    }
    int status;
    assert(waitpid(process, &status, 0) == process && WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
static void test_maps_reader(void) {
    static presentation_fork_maps maps;
    presentation_fork_maps_diagnostic diagnostic;
    char name[] = "/tmp/x4-map-test-XXXXXX";
    int fd = mkstemp(name);
    assert(fd >= 0 && !unlink(name));
    static char block[4096];
    memset(block, 'a', sizeof block);
    for (size_t i = 0; i < PRESENTATION_FORK_MAP_BYTES / sizeof block; ++i)
        assert(syscall(SYS_write, fd, block, sizeof block) == sizeof block);
    assert(!presentation_fork_maps_read_detail(fd, &maps, &diagnostic));
    assert(!strcmp(diagnostic.stage, "truncated") && diagnostic.length == PRESENTATION_FORK_MAP_BYTES);
    assert(pwrite(fd, "\n", 1, PRESENTATION_FORK_MAP_BYTES - 1) == 1);
    assert(presentation_fork_maps_read_detail(fd, &maps, &diagnostic));
    assert(!strcmp(diagnostic.stage, "ok") && diagnostic.length == PRESENTATION_FORK_MAP_BYTES);
    assert(syscall(SYS_write, fd, "x", 1) == 1);
    assert(!presentation_fork_maps_read_detail(fd, &maps, &diagnostic));
    assert(!strcmp(diagnostic.stage, "overflow") && diagnostic.length == PRESENTATION_FORK_MAP_BYTES && !diagnostic.error);
    fault_map_fd = fd; fault_map_reads = 0;
    assert(!presentation_fork_maps_read_detail(fd, &maps, &diagnostic));
    assert(!strcmp(diagnostic.stage, "read") && diagnostic.length == 17 && diagnostic.error == EIO);
    fault_map_fd = -1;
    assert(!presentation_fork_maps_read_detail(-1, &maps, &diagnostic));
    assert(!strcmp(diagnostic.stage, "seek") && diagnostic.length == 0 && diagnostic.error == EBADF);
    memcpy(maps.bytes, "1000-2000 rw-p\n", 15); maps.length = 15;
    assert(presentation_fork_mapping_private(&maps, 0x1000, 1, 1));
    memcpy(maps.bytes, "1000-2000 rw-p", 14); maps.length = 14;
    assert(!presentation_fork_mapping_private(&maps, 0x1000, 1, 1));
    memcpy(maps.bytes, "nope-2000 rw-p\n", 15); maps.length = 15;
    assert(!presentation_fork_mapping_private(&maps, 0x1000, 1, 1));
    close(fd);
}
int main(void) {
    test_maps_reader();
    fixture f = {.p1 = page(0x7000), .p2 = page(0x7000), .room1 = page(4096), .room2 = page(4096)};
    uintptr_t vector1[1] = {(uintptr_t)f.room1}, vector2[1] = {(uintptr_t)f.room2};
    set_pointer(f.p1, 0x70, (uintptr_t)f.room1); set_pointer(f.p1, 0xe0, (uintptr_t)f.room1);
    set_pointer(f.p2, 0x70, (uintptr_t)f.room2); set_pointer(f.p2, 0xe0, (uintptr_t)f.room2);
    set_pointer(f.p1, 0x308, (uintptr_t)vector1); set_pointer(f.p1, 0x310, (uintptr_t)(vector1 + 1));
    set_pointer(f.p1, 0x318, (uintptr_t)(vector1 + 1));
    set_pointer(f.p2, 0x308, (uintptr_t)vector2); set_pointer(f.p2, 0x310, (uintptr_t)(vector2 + 1));
    set_pointer(f.p2, 0x318, (uintptr_t)(vector2 + 1));
    *(uint32_t *)(f.room2 + 0x40) = 5; *(uint32_t *)(f.room2 + 0x44) = 2;
    f.parent_pid = getpid();
    f.inherited_fd = open("/dev/null", O_RDONLY | O_CLOEXEC); assert(f.inherited_fd >= 3);
    presentation_fork_request request = {(uintptr_t)f.p1, (uintptr_t)f.p2, (uintptr_t)f.room2, 11, 22, 0, 500};
    presentation_fork_ops ops = {&f, quiet_enter, quiet_leave, private_range, private_graph, child_setup,
        owner, read_bytes, write_bytes, occupied, live_room, cached_room, release_room};
    presentation_fork_result result;
    assert(player_presentation_fork_run(&request, &ops, &result));
    assert(!strcmp(result.reason, "ok") && result.receipt.overlay_ship == 22 && !f.quiet && f.leaves == 1);
    assert(occupied(&f) == 11 && *(uint32_t *)(f.room2 + 0x40) == 5);
    test_terminal_parent_change(&f, &request, &ops, 0);
    test_terminal_parent_change(&f, &request, &ops, 1);
    f.quiet_refused = 1; assert(!player_presentation_fork_run(&request, &ops, &result));
    assert(!strcmp(result.reason, "quiet") && f.leaves == 1); f.quiet_refused = 0;
    f.private_refused = 1; assert(!player_presentation_fork_run(&request, &ops, &result));
    assert(!strcmp(result.reason, "private_mapping") && !f.quiet); f.private_refused = 0;
    unsigned char *shared = mmap(NULL, 0x7000, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    assert(shared != MAP_FAILED);
    memcpy(shared, f.p2, 0x7000);
    unsigned char *private_p2 = f.p2;
    f.p2 = shared; request.player2 = (uintptr_t)shared;
    assert(!player_presentation_fork_run(&request, &ops, &result));
    assert(!strcmp(result.reason, "private_mapping") && !f.quiet);
    f.p2 = private_p2; request.player2 = (uintptr_t)private_p2;
    assert(!munmap(shared, 0x7000));
    f.graph_refused = 1; assert(!player_presentation_fork_run(&request, &ops, &result));
    assert(!strcmp(result.reason, "private_mapping") && !f.quiet); f.graph_refused = 0;
    for (int mode = 1; mode <= 5; ++mode) {
        f.cached_mode = mode;
        assert(!player_presentation_fork_run(&request, &ops, &result));
        assert(!strcmp(result.reason, "child_failed") && !result.lease_held);
        assert(result.receipt.reference_state == PRESENTATION_FORK_REF_UNCERTAIN);
        assert(result.receipt.release_attempts == (mode == 3 ? 0u : 1u));
        assert(occupied(&f) == 11 && *(uint32_t *)(f.room2 + 0x40) == 5);
    }
    f.cached_mode = 0;
    f.fail_restore = 1; assert(!player_presentation_fork_run(&request, &ops, &result));
    assert(!strcmp(result.reason, "child_failed") && occupied(&f) == 11 && !f.quiet); f.fail_restore = 0;
    f.child_mode = 1; assert(!player_presentation_fork_run(&request, &ops, &result));
    assert(!strcmp(result.reason, "child_signaled") && !f.quiet); f.child_mode = 2;
    request.timeout_ms = 40; assert(!player_presentation_fork_run(&request, &ops, &result));
    assert(!strcmp(result.reason, "timeout") && !f.quiet); f.child_mode = 0; request.timeout_ms = 500;
    receipt_mode = 1; assert(!player_presentation_fork_run(&request, &ops, &result));
    assert(!strcmp(result.reason, "receipt_length") && !f.quiet);
    receipt_mode = 2; assert(!player_presentation_fork_run(&request, &ops, &result));
    assert(!strcmp(result.reason, "receipt_extra") && !f.quiet);
    receipt_mode = 3; assert(!player_presentation_fork_run(&request, &ops, &result));
    assert(!strcmp(result.reason, "receipt_invalid") && !f.quiet);
    receipt_mode = 0;
    /* Wait until a complete receipt reached EOF, then deliberately defer
     * reaping past the deadline. A successful late reap must not pass. */
    delay_reap = 1; wait_tracking = observed_eof = delayed_wait_complete = 0;
    request.timeout_ms = 50;
    assert(!player_presentation_fork_run(&request, &ops, &result));
    assert(observed_eof && delayed_wait_complete && !strcmp(result.reason, "timeout") && !f.quiet);
    delay_reap = 0; request.timeout_ms = 500;
    assert(!pthread_atfork(atfork_prepare, atfork_parent_done, atfork_child_done));
    struct sigaction action = {0}, previous_action;
    action.sa_handler = inherited_signal_handler;
    sigemptyset(&action.sa_mask);
    assert(!sigaction(SIGUSR1, &action, &previous_action));
    sigset_t previous_mask, current_mask;
    assert(!sigprocmask(SIG_SETMASK, NULL, &previous_mask));
    signal_fd = f.inherited_fd;
    test_atfork = 1;
    assert(player_presentation_fork_run(&request, &ops, &result));
    assert(!atfork_prepared && !atfork_parent && !atfork_child && !inherited_signal_called);
    assert(!sigprocmask(SIG_SETMASK, NULL, &current_mask));
    assert(sigismember(&previous_mask, SIGUSR1) == sigismember(&current_mask, SIGUSR1));
    assert(!sigaction(SIGUSR1, &previous_action, NULL));
    presentation_fork_receipt invalid = {0};
    assert(!presentation_fork_receipt_valid(&invalid, 123, &request));
    close(f.inherited_fd);
    puts("forked presentation getter isolation PASS");
}
