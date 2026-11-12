#ifndef X4_PLAYER_PRESENTATION_FORK_PROBE_H
#define X4_PLAYER_PRESENTATION_FORK_PROBE_H

/* Default-unused diagnostic. The caller supplies the actual fs_quiet/fs_release
 * lease, authenticated object graph and exact pinned, leaf-only native calls.
 * Every callback after fork must be async-signal-safe and use no allocation,
 * UI, renderer, Lua, save, inherited locks or writable external descriptors.
 * private_graph must cover EVERY mapping a native callback can write, not just
 * the four overlay fields. This tests a private fork copy, not live display. */
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "player_presentation_overlay.h"

#define PRESENTATION_FORK_MAGIC UINT64_C(0x58345052464f524b)
#define PRESENTATION_FORK_VERSION 2u
#define PRESENTATION_FORK_MAX_MS 2000u
#define PRESENTATION_FORK_VECTOR_MAX 8192u
#ifndef SYS_close_range
#define SYS_close_range 436
#endif

typedef struct {
    uintptr_t player1, player2, room;
    uint64_t expected_p1_ship, expected_p2_ship;
    uint32_t bank, timeout_ms;
} presentation_fork_request;
typedef struct presentation_fork_maps presentation_fork_maps;
typedef struct {
    void *context;
    int (*quiet_enter)(void *);          /* failure leaves no held obligations */
    void (*quiet_leave)(void *);         /* parent only, after reap and comparison */
    int (*private_range)(void *, uintptr_t, size_t);
    int (*private_graph)(void *, const presentation_fork_request *, const presentation_fork_maps *);
    int (*child_setup)(void *);          /* copy worker TLS; no locks/allocations */
    int (*owner)(void *, uintptr_t, uintptr_t, uintptr_t, uint32_t);
    int (*read)(void *, uintptr_t, void *, size_t);
    int (*write)(void *, uintptr_t, const void *, size_t);
    uint64_t (*occupied)(void *);
    uintptr_t (*live_room)(void *, uintptr_t);
    uintptr_t (*cached_room)(void *, uintptr_t); /* returns owned native ref */
    int (*release_room)(void *, uintptr_t);       /* nonfinal, exactly once */
} presentation_fork_ops;
enum {
    PRESENTATION_FORK_REF_NONE,
    PRESENTATION_FORK_REF_UNCERTAIN,
    PRESENTATION_FORK_REF_RELEASED
};
typedef struct {
    uint64_t magic;
    uint32_t version, size;
    int64_t child_pid;
    uint32_t stage, pass;
    uint64_t before_ship, overlay_ship, restored_ship;
    uintptr_t live_room, cached_room;
    uint32_t refs_before, refs_acquired, refs_after, marker_before, marker_after;
    uint32_t reference_state, release_attempts;
} presentation_fork_receipt;
typedef struct { const char *stage; size_t length; int error; } presentation_fork_maps_diagnostic;
typedef struct {
    const char *reason;
    presentation_fork_maps_diagnostic maps_diagnostic;
    presentation_fork_receipt receipt;
    int child_status, lease_held;
} presentation_fork_result;

typedef struct { unsigned char parent[8], vector[24], position[4], cached_parent[8]; } presentation_fork_p1;
static int presentation_fork_capture(const presentation_fork_request *request, const presentation_fork_ops *ops,
                                     presentation_fork_p1 *value) {
    uintptr_t p = request->player1;
    return ops->read(ops->context, p + 0x70, value->parent, 8) &&
        ops->read(ops->context, p + 0x308, value->vector, 24) &&
        ops->read(ops->context, p + 0x6aa8, value->position, 4) &&
        ops->read(ops->context, p + 0xe0 + request->bank * 0x80, value->cached_parent, 8);
}
/* Build a bounded descriptor/permission inventory in the parent. A child
 * close_range also closes descriptors opened after this snapshot. */
typedef struct { int fd, status_flags; } presentation_fork_fd;
typedef struct {
    int null_fd, maps_fd;
    unsigned count;
    presentation_fork_fd entries[4096];
} presentation_fork_fd_plan;
#define PRESENTATION_FORK_MAP_BYTES (2u * 1024u * 1024u)
struct presentation_fork_maps { char bytes[PRESENTATION_FORK_MAP_BYTES]; size_t length; };

static int presentation_fork_fd_prepare(presentation_fork_fd_plan *plan) {
    memset(plan, 0, sizeof *plan);
    plan->null_fd = open("/dev/null", O_RDWR | O_CLOEXEC);
    plan->maps_fd = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    if (plan->null_fd < 3 || plan->maps_fd < 3) return 0;
    DIR *directory = opendir("/proc/self/fd");
    if (!directory) return 0;
    int own_fd = dirfd(directory), valid = 1;
    struct dirent *entry;
    errno = 0;
    while ((entry = readdir(directory))) {
        if (entry->d_name[0] == '.') continue;
        char *end = NULL;
        long value = strtol(entry->d_name, &end, 10);
        if (!end || *end || value < 0 || value > INT_MAX) { valid = 0; break; }
        if (value == own_fd) continue;
        if (plan->count == sizeof plan->entries / sizeof plan->entries[0]) { valid = 0; break; }
        int flags = fcntl((int)value, F_GETFL);
        if (flags < 0) { valid = 0; break; }
        plan->entries[plan->count++] = (presentation_fork_fd){(int)value, flags};
        errno = 0;
    }
    int read_error = errno;
    int close_error = closedir(directory);
    return valid && !read_error && !close_error;
}
/* Refresh through the preopened fd: no stdio, parsing allocation or mmap. */
static int presentation_fork_maps_read_detail(int fd, presentation_fork_maps *maps,
                                               presentation_fork_maps_diagnostic *diagnostic) {
    presentation_fork_maps_diagnostic ignored;
    if (!diagnostic) diagnostic = &ignored;
    *diagnostic = (presentation_fork_maps_diagnostic){"seek", 0, 0};
    maps->length = 0;
    if (lseek(fd, 0, SEEK_SET) < 0) { diagnostic->error = errno; return 0; }
    diagnostic->stage = "read";
    while (maps->length < sizeof maps->bytes) {
        ssize_t count = read(fd, maps->bytes + maps->length, sizeof maps->bytes - maps->length);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) { diagnostic->length = maps->length; diagnostic->error = errno; return 0; }
        if (!count) {
            diagnostic->length = maps->length;
            if (maps->length && maps->bytes[maps->length - 1] != '\n') {
                diagnostic->stage = "truncated";
                return 0;
            }
            diagnostic->stage = "ok";
            return 1;
        }
        maps->length += (size_t)count;
    }
    /* A full buffer is valid only if the next read proves EOF. */
    char extra;
    ssize_t count;
    do { count = read(fd, &extra, 1); } while (count < 0 && errno == EINTR);
    diagnostic->length = maps->length;
    if (count < 0) { diagnostic->error = errno; return 0; }
    if (count) { diagnostic->stage = "overflow"; return 0; }
    if (maps->length && maps->bytes[maps->length - 1] != '\n') {
        diagnostic->stage = "truncated";
        return 0;
    }
    diagnostic->stage = "ok";
    return 1;
}
static inline int presentation_fork_maps_read(int fd, presentation_fork_maps *maps) {
    return presentation_fork_maps_read_detail(fd, maps, NULL);
}
static int presentation_fork_hex(const char **at, const char *end, uintptr_t *value) {
    uintptr_t result = 0;
    unsigned digits = 0;
    while (*at < end) {
        unsigned char c = (unsigned char)**at;
        unsigned digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else break;
        if (result > (UINTPTR_MAX - digit) / 16) return 0;
        result = result * 16 + digit; ++*at; ++digits;
    }
    *value = result;
    return digits != 0;
}
static int presentation_fork_mapping_private(const presentation_fork_maps *maps,
                                               uintptr_t address, size_t length, int writable) {
    if (!length || address > UINTPTR_MAX - length) return 0;
    uintptr_t finish = address + length, cursor = address;
    const char *at = maps->bytes, *end = maps->bytes + maps->length;
    while (at < end && cursor < finish) {
        const char *line_end = memchr(at, '\n', (size_t)(end - at));
        if (!line_end) return 0;
        uintptr_t start, stop;
        if (!presentation_fork_hex(&at, line_end, &start) || at == line_end || *at++ != '-' ||
            !presentation_fork_hex(&at, line_end, &stop) || line_end - at < 5 || *at++ != ' ') return 0;
        if (cursor >= start && cursor < stop) {
            if (at[0] != 'r' || (writable && at[1] != 'w') || at[3] != 'p') return 0;
            cursor = stop < finish ? stop : finish;
        } else if (start > cursor) return 0;
        at = line_end + 1;
    }
    return cursor == finish;
}
static int presentation_fork_vectors_private(const presentation_fork_request *request,
                                               const presentation_fork_ops *ops, const presentation_fork_maps *maps) {
    uintptr_t players[2] = {request->player1, request->player2};
    for (unsigned i = 0; i < 2; ++i) {
        player_presentation_vector vector;
        if (!ops->read(ops->context, players[i] + 0x308, &vector, sizeof vector) ||
            !presentation_overlay_vector_valid(&vector) || vector.end - vector.begin > PRESENTATION_FORK_VECTOR_MAX ||
            (vector.end != vector.begin && (!presentation_fork_mapping_private(maps, vector.begin, vector.end - vector.begin, 0) ||
             !ops->private_range(ops->context, vector.begin, vector.end - vector.begin))))
            return 0;
        for (uintptr_t entry = vector.begin; entry < vector.end; entry += sizeof(uintptr_t)) {
            uintptr_t object = 0;
            if (!ops->read(ops->context, entry, &object, sizeof object)) return 0;
            if (object && (!presentation_fork_mapping_private(maps, object, 0x80, 1) ||
                           !ops->private_range(ops->context, object, 0x80))) return 0;
        }
    }
    return 1;
}
static int presentation_fork_quarantine(int output, int null_fd) {
    if (output <= 2 || null_fd <= 2 || output == null_fd) return 0;
    for (int fd = 0; fd < 3; ++fd) if (dup2(null_fd, fd) < 0) return 0;
    if (output > 3 && syscall(SYS_close_range, 3u, (unsigned)output - 1u, 0u)) return 0;
    return syscall(SYS_close_range, (unsigned)output + 1u, UINT_MAX, 0u) == 0;
}
static int presentation_fork_receipt_valid(const presentation_fork_receipt *receipt, pid_t child,
                                            const presentation_fork_request *request) {
    return receipt && receipt->magic == PRESENTATION_FORK_MAGIC && receipt->version == PRESENTATION_FORK_VERSION &&
        receipt->size == sizeof *receipt && receipt->child_pid == child && receipt->stage == 5 && receipt->pass == 1 &&
        receipt->before_ship == request->expected_p1_ship && receipt->overlay_ship == request->expected_p2_ship &&
        receipt->restored_ship == request->expected_p1_ship && receipt->live_room == request->room &&
        receipt->cached_room == request->room && receipt->refs_before > 1 && receipt->refs_before < UINT32_MAX &&
        receipt->refs_acquired == receipt->refs_before + 1 && receipt->refs_after == receipt->refs_before &&
        receipt->marker_before == 2 && receipt->marker_after == 2 &&
        receipt->reference_state == PRESENTATION_FORK_REF_RELEASED && receipt->release_attempts == 1;
}
static void presentation_fork_write_receipt(int fd, const presentation_fork_receipt *receipt) {
    const unsigned char *at = (const unsigned char *)receipt;
    size_t remaining = sizeof *receipt;
    while (remaining) {
        ssize_t count = write(fd, at, remaining);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) _exit(225);
        at += count;
        remaining -= (size_t)count;
    }
}
/* Never invoke inherited engine signal handlers or write a core file in the
 * disposable child. All calls here are async-signal-safe. */
static int presentation_fork_child_signals(void) {
    struct sigaction action = {0};
    action.sa_handler = SIG_DFL;
    if (sigemptyset(&action.sa_mask)) return 0;
    static const int signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGPIPE, SIGALRM, SIGTERM};
    for (unsigned i = 0; i < sizeof signals / sizeof signals[0]; ++i)
        if (sigaction(signals[i], &action, NULL)) return 0;
    /* Leave unrelated inherited handlers blocked for the entire child. */
    sigset_t fatal;
    if (sigemptyset(&fatal) || sigaddset(&fatal, SIGSEGV) || sigaddset(&fatal, SIGBUS) ||
        sigaddset(&fatal, SIGILL) || sigaddset(&fatal, SIGFPE) || sigaddset(&fatal, SIGABRT) ||
        sigprocmask(SIG_UNBLOCK, &fatal, NULL)) return 0;
    struct rlimit no_core = {0, 0};
    return syscall(SYS_prlimit64, 0, RLIMIT_CORE, &no_core, NULL) == 0;
}
static _Noreturn void presentation_fork_child(int output, int null_fd, const presentation_fork_request *request,
                                                const presentation_fork_ops *ops) {
    presentation_fork_receipt receipt = {.magic = PRESENTATION_FORK_MAGIC, .version = PRESENTATION_FORK_VERSION,
        .size = sizeof receipt, .child_pid = (int64_t)getpid()};
    if (!presentation_fork_quarantine(output, null_fd) || !presentation_fork_child_signals()) _exit(225);
    if (!ops->child_setup(ops->context)) goto finish;
    receipt.stage = 1;
    receipt.before_ship = ops->occupied(ops->context);
    if (receipt.before_ship != request->expected_p1_ship) goto finish;
    player_presentation_overlay overlay = {0};
    player_presentation_overlay_ops overlay_ops = {.context = ops->context, .owner = ops->owner,
        .read = ops->read, .write = ops->write};
    if (!player_presentation_overlay_begin(&overlay, &overlay_ops, request->player1, request->player2,
                                           request->room, request->bank)) goto finish;
    receipt.stage = 2;
    receipt.overlay_ship = ops->occupied(ops->context);
    receipt.live_room = ops->live_room(ops->context, request->player1);
    if (receipt.overlay_ship != request->expected_p2_ship || receipt.live_room != request->room ||
        !ops->read(ops->context, request->room + 0x40, &receipt.refs_before, 4) ||
        !ops->read(ops->context, request->room + 0x44, &receipt.marker_before, 4) ||
        receipt.refs_before <= 1 || receipt.refs_before == UINT32_MAX || receipt.marker_before != 2) goto finish;
    receipt.cached_room = ops->cached_room(ops->context, request->player1);
    if (!receipt.cached_room) goto finish;
    /* The pinned stock getter grants one reference on success. An unexpected
     * object cannot be safely inspected or released; the child will exit. */
    receipt.reference_state = PRESENTATION_FORK_REF_UNCERTAIN;
    if (receipt.cached_room != request->room) goto finish;
    int acquired_read = ops->read(ops->context, request->room + 0x40, &receipt.refs_acquired, 4);
    /* The exact held room had >1 references before the pinned acquisition.
     * Release that known owned reference exactly once, even on read failure. */
    receipt.release_attempts = 1;
    int released = ops->release_room(ops->context, request->room);
    if (!released || !acquired_read || receipt.refs_acquired != receipt.refs_before + 1 ||
        !ops->read(ops->context, request->room + 0x40, &receipt.refs_after, 4) ||
        !ops->read(ops->context, request->room + 0x44, &receipt.marker_after, 4) ||
        receipt.refs_after != receipt.refs_before || receipt.marker_after != receipt.marker_before) goto finish;
    receipt.reference_state = PRESENTATION_FORK_REF_RELEASED;
    receipt.stage = 3;
    if (!player_presentation_overlay_end(&overlay, request->player1, request->player2, request->room, request->bank))
        goto finish;
    receipt.stage = 4;
    receipt.restored_ship = ops->occupied(ops->context);
    if (receipt.restored_ship != request->expected_p1_ship) goto finish;
    receipt.stage = 5;
    receipt.pass = 1;
finish:
    presentation_fork_write_receipt(output, &receipt);
    _exit(receipt.pass ? 0 : 1);
}
static uint64_t presentation_fork_now_ms(void) {
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value)) return 0;
    return (uint64_t)value.tv_sec * 1000u + (uint64_t)value.tv_nsec / 1000000u;
}
/* Caller must terminate rather than resume if result.lease_held is set,
 * including an unexpected parent difference or an unreaped child. */
static int player_presentation_fork_run(const presentation_fork_request *request, const presentation_fork_ops *ops,
                                         presentation_fork_result *result) {
    if (!result) return 0;
    memset(result, 0, sizeof *result);
    result->reason = "arguments";
    uintptr_t end;
    if (!request || !ops || !ops->quiet_enter || !ops->quiet_leave || !ops->private_range ||
        !ops->private_graph || !ops->child_setup || !ops->owner || !ops->read || !ops->write ||
        !ops->occupied || !ops->live_room || !ops->cached_room || !ops->release_room ||
        !request->expected_p1_ship || !request->expected_p2_ship || request->expected_p1_ship == request->expected_p2_ship ||
        request->bank > 1 || !request->timeout_ms || request->timeout_ms > PRESENTATION_FORK_MAX_MS ||
        !presentation_overlay_address(request->player1, 0, PRESENTATION_PLAYER_EXTENT, &end) ||
        !presentation_overlay_address(request->player2, 0, PRESENTATION_PLAYER_EXTENT, &end) ||
        !presentation_overlay_address(request->room, 0, 0x80, &end)) return 0;
    /* Resolve before quiet: _Fork omits pthread_atfork handlers, unlike fork.
     * It is absent on older glibc, where this diagnostic simply refuses. */
    result->reason = "fork_unavailable";
    void *libc = dlopen("libc.so.6", RTLD_NOW | RTLD_LOCAL | RTLD_NOLOAD);
    if (!libc) return 0;
    pid_t (*fork_without_handlers)(void) = (pid_t (*)(void))dlsym(libc, "_Fork");
    Dl_info fork_module = {0};
    const char *basename = NULL;
    if (fork_without_handlers && dladdr((void *)fork_without_handlers, &fork_module) && fork_module.dli_fname) {
        basename = strrchr(fork_module.dli_fname, '/');
        basename = basename ? basename + 1 : fork_module.dli_fname;
    }
    if (!basename || strcmp(basename, "libc.so.6")) {
        dlclose(libc);
        return 0;
    }
    result->reason = "fd_plan";
    presentation_fork_fd_plan plan;
    presentation_fork_maps *maps = mmap(NULL, sizeof *maps, PROT_READ | PROT_WRITE,
                                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (maps == MAP_FAILED) { dlclose(libc); result->reason = "maps_alloc"; return 0; }
    int pipes[2] = {-1, -1};
    if (!presentation_fork_fd_prepare(&plan)) goto close_fds;
    if (pipe2(pipes, O_CLOEXEC | O_NONBLOCK) || pipes[0] < 3 || pipes[1] < 3 ||
        !presentation_fork_maps_read_detail(plan.maps_fd, maps, &result->maps_diagnostic)) goto close_fds;
    result->reason = "quiet";
    if (!ops->quiet_enter(ops->context)) goto close_fds;
    result->lease_held = 1;
    result->reason = "private_mapping";
    presentation_fork_p1 before, after;
    unsigned char p1_before[PRESENTATION_PLAYER_EXTENT], p1_after[PRESENTATION_PLAYER_EXTENT];
    if (!presentation_fork_maps_read_detail(plan.maps_fd, maps, &result->maps_diagnostic) ||
        !presentation_fork_mapping_private(maps, request->player1, PRESENTATION_PLAYER_EXTENT, 1) ||
        !presentation_fork_mapping_private(maps, request->player2, PRESENTATION_PLAYER_EXTENT, 1) ||
        !presentation_fork_mapping_private(maps, request->room, 0x80, 1) ||
        !ops->private_range(ops->context, request->player1, PRESENTATION_PLAYER_EXTENT) ||
        !ops->private_range(ops->context, request->player2, PRESENTATION_PLAYER_EXTENT) ||
        !ops->private_range(ops->context, request->room, 0x80) || !presentation_fork_vectors_private(request, ops, maps) ||
        !ops->private_graph(ops->context, request, maps) || !ops->owner(ops->context, request->player1, request->player2,
                                                                 request->room, request->bank) ||
        !presentation_fork_capture(request, ops, &before) ||
        !ops->read(ops->context, request->player1, p1_before, sizeof p1_before)) goto release;
    uintptr_t original_parent = 0, original_cached_parent = 0;
    memcpy(&original_parent, before.parent, sizeof original_parent);
    memcpy(&original_cached_parent, before.cached_parent, sizeof original_cached_parent);
    if (!original_parent || original_parent != original_cached_parent ||
        !presentation_fork_mapping_private(maps, original_parent, 0x80, 1) ||
        !ops->private_range(ops->context, original_parent, 0x80)) goto release;
    result->reason = "clock";
    uint64_t now = presentation_fork_now_ms();
    if (!now || now > UINT64_MAX - request->timeout_ms) goto release;
    uint64_t deadline = now + request->timeout_ms;
    result->reason = "signal_mask";
    sigset_t all_signals, previous_signals;
    if (sigfillset(&all_signals) || sigprocmask(SIG_SETMASK, &all_signals, &previous_signals)) goto release;
    pid_t child = fork_without_handlers();
    if (!child) {
        close(pipes[0]);
        presentation_fork_child(pipes[1], plan.null_fd, request, ops);
    }
    if (sigprocmask(SIG_SETMASK, &previous_signals, NULL)) {
        if (child > 0) {
            kill(child, SIGKILL);
            (void)waitpid(child, NULL, WNOHANG);
        }
        result->reason = "signal_restore";
        goto close_fds; /* Keep the lease: the parent signal state is uncertain. */
    }
    if (child < 0) { result->reason = "fork"; goto release; }
    close(pipes[1]); pipes[1] = -1;
    unsigned char bytes[sizeof(presentation_fork_receipt) + 1];
    size_t count = 0;
    int reaped = 0, ended = 0, status = 0, timed_out = 0;
    result->reason = "timeout";
    while (!reaped || !ended) {
        now = presentation_fork_now_ms();
        if (!now || now >= deadline) { timed_out = 1; break; }
        struct pollfd poll_fd = {.fd = pipes[0], .events = POLLIN | POLLHUP};
        int wait_ms = (int)(deadline - now);
        if (wait_ms > 10) wait_ms = 10;
        int polled = poll(&poll_fd, 1, wait_ms);
        if (polled < 0 && errno != EINTR) { result->reason = "poll"; break; }
        if (polled > 0) {
            ssize_t received = read(pipes[0], bytes + count, sizeof bytes - count);
            if (received > 0) {
                count += (size_t)received;
                if (count == sizeof bytes) { result->reason = "receipt_extra"; break; }
            } else if (!received) ended = 1;
            else if (errno != EAGAIN && errno != EINTR) { result->reason = "pipe_read"; break; }
        }
        if (!reaped) {
            pid_t waited = waitpid(child, &status, WNOHANG);
            if (waited == child) reaped = 1;
            else if (waited < 0 && errno != EINTR) { result->reason = "waitpid"; break; }
        }
    }
    now = presentation_fork_now_ms();
    if (!now || now >= deadline) timed_out = 1;
    if (!reaped) {
        /* The PID is ours until waitpid reports it reaped. Do not risk an
         * unbounded wait: failure to reap leaves the quiet lease held. */
        pid_t current = waitpid(child, &status, WNOHANG);
        if (current == child) reaped = 1;
        else if (!current) {
            (void)kill(child, SIGKILL);
            uint64_t reap_deadline = presentation_fork_now_ms() + 1000;
            do {
                current = waitpid(child, &status, WNOHANG);
                if (current == child) { reaped = 1; break; }
                if (current < 0 && errno != EINTR) break;
                struct timespec pause = {.tv_nsec = 1000000};
                nanosleep(&pause, NULL);
                now = presentation_fork_now_ms();
            } while (now && now < reap_deadline);
        }
    }
    if (!reaped) { result->reason = "reap_uncertain"; goto close_fds; }
    result->child_status = status;
    if (!presentation_fork_capture(request, ops, &after) || memcmp(&before, &after, sizeof before) ||
        !ops->read(ops->context, request->player1, p1_after, sizeof p1_after) ||
        memcmp(p1_before, p1_after, sizeof p1_before) ||
        !ops->owner(ops->context, request->player1, request->player2, request->room, request->bank)) {
        /* Unexpected parent mutation or failed revalidation is terminal.
         * Keep the lease held; caller must end the process without resuming. */
        result->reason = "parent_changed";
        goto close_fds;
    }
    if (timed_out) { result->reason = "timeout"; goto release; }
    if (reaped && ended && !strcmp(result->reason, "timeout")) result->reason = "completed";
    if (WIFSIGNALED(status) && strcmp(result->reason, "timeout")) {
        result->reason = "child_signaled";
        goto release;
    }
    if (!ended || count != sizeof result->receipt) {
        if (reaped && ended && strcmp(result->reason, "receipt_extra")) result->reason = "receipt_length";
        goto release;
    }
    memcpy(&result->receipt, bytes, sizeof result->receipt);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) { result->reason = "child_failed"; goto release; }
    if (!presentation_fork_receipt_valid(&result->receipt, child, request)) { result->reason = "receipt_invalid"; goto release; }
    result->reason = "ok";
release:
    ops->quiet_leave(ops->context);
    result->lease_held = 0;
close_fds:
    if (!result->lease_held) { munmap(maps, sizeof *maps); dlclose(libc); }
    if (plan.null_fd >= 0) close(plan.null_fd);
    if (plan.maps_fd >= 0) close(plan.maps_fd);
    if (pipes[0] >= 0) close(pipes[0]);
    if (pipes[1] >= 0) close(pipes[1]);
    return !strcmp(result->reason, "ok");
}
#endif
