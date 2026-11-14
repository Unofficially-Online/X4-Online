#ifndef X4_GATE_PLAYER_PRESENTATION_QUIET_H
#define X4_GATE_PLAYER_PRESENTATION_QUIET_H
#define PRESENTATION_FORK_QUIET_BUDGET_US UINT64_C(50000)
/* This bounds retries and admission, not preemption of an in-flight /proc
 * census syscall. A success completed after the deadline is released/refused. */
static int presentation_fork_quiet_now(uint64_t *now) {
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) || value.tv_sec < 0 || value.tv_nsec < 0 ||
        value.tv_nsec >= 1000000000) {
        return 0;
    }
    *now = (uint64_t)value.tv_sec * UINT64_C(1000000) + (uint64_t)value.tv_nsec / 1000;
    return 1;
}

static int presentation_fork_quiet_owned(native_presentation_fork *probe) {
    const char *reason = NULL;
    if (probe != &presentation_fork || GetCurrentThreadId() != main_thread) {
        reason = "quiet_owner";
    } else if (atomic_load_explicit(&presentation_fork_reservation, memory_order_acquire) != 1) {
        reason = "reservation";
    } else if (fs.state != FS_OFF || atomic_load_explicit(&presentation_fork_state, memory_order_acquire) != 1) {
        reason = "quiet_state";
    }
    if (!reason) return 1;
    snprintf(probe->quiet_reason, sizeof probe->quiet_reason, "%s", reason);
    probe->quiet_blocker[0] = 0;
    probe->other_running = 0;
    return 0;
}

static int presentation_fork_quiet_retryable(void) {
    if (!strcmp(fs.reason, "lock") || !strcmp(fs.reason, "rpmalloc")) return 1;
    if (strcmp(fs.reason, "census")) return 0;
    size_t length = strlen(fs.blocker);
    const char suffix[] = ":running";
    return length >= sizeof suffix - 1 && !strcmp(fs.blocker + length - (sizeof suffix - 1), suffix);
}

static int presentation_fork_quiet_enter(void *context) {
    native_presentation_fork *probe = context;
    if (!presentation_fork_quiet_owned(probe)) return 0;
    probe->quiet_reason[0] = probe->quiet_blocker[0] = 0;
    probe->other_running = 0;
    for (unsigned i = 0; i < FS_LOCKS; ++i) {
        if (fs_locks[i].held) {
            snprintf(probe->quiet_reason, sizeof probe->quiet_reason, "quiet_locks");
            return 0;
        }
    }
    uint64_t started, now;
    if (!presentation_fork_quiet_now(&started)) {
        snprintf(probe->quiet_reason, sizeof probe->quiet_reason, "quiet_clock");
        return 0;
    }
    probe->saved_fs = fs;
    memcpy(probe->saved_locks, fs_locks, sizeof fs_locks);
    fs.worker_tid = probe->worker_tid;
    fs.worker_tls = (unsigned char *)probe->worker_tls;
    for (;;) {
        if (!presentation_fork_quiet_owned(probe)) break;
        if (!presentation_fork_quiet_now(&now) || now < started) {
            snprintf(probe->quiet_reason, sizeof probe->quiet_reason, "quiet_clock");
            break;
        }
        if (now - started >= PRESENTATION_FORK_QUIET_BUDGET_US) break;
        /* A previous transient reason must never authorize retrying a later
         * hard failure. fs_quiet owns cleanup of every unsuccessful attempt. */
        fs.reason[0] = fs.blocker[0] = 0;
        fs.other_running = 0;
        probe->held_since = 0;
        int quiet = fs_quiet(&probe->held_since);
        int retry = presentation_fork_quiet_retryable();
        snprintf(probe->quiet_reason, sizeof probe->quiet_reason, "%s", fs.reason);
        snprintf(probe->quiet_blocker, sizeof probe->quiet_blocker, "%s", fs.blocker);
        probe->other_running = fs.other_running;
        if (quiet && fs.other_running) {
            snprintf(probe->quiet_reason, sizeof probe->quiet_reason, "other_running");
            retry = 1;
        }
        int clock_ok = presentation_fork_quiet_now(&now) && now >= started;
        int owned = presentation_fork_quiet_owned(probe);
        if (quiet && !fs.other_running && clock_ok && owned &&
            now - started < PRESENTATION_FORK_QUIET_BUDGET_US) {
            return 1;
        }
        if (quiet) fs_release(FS_RP_WORDS);
        /* Defensive refusal: no wait is allowed with any acquired lock. */
        int locks_held = 0;
        for (unsigned i = 0; i < FS_LOCKS; ++i) {
            locks_held |= fs_locks[i].held;
        }
        if (locks_held) {
            fs_release(0);
            snprintf(probe->quiet_reason, sizeof probe->quiet_reason, "quiet_locks");
            break;
        }
        if (!clock_ok) {
            snprintf(probe->quiet_reason, sizeof probe->quiet_reason, "quiet_clock");
            break;
        }
        if (!owned) break;
        if (now - started >= PRESENTATION_FORK_QUIET_BUDGET_US) {
            if (quiet) snprintf(probe->quiet_reason, sizeof probe->quiet_reason, "quiet_budget");
            break;
        }
        if (!retry) break;
        uint64_t remaining = PRESENTATION_FORK_QUIET_BUDGET_US - (now - started);
        struct timespec pause = {0, (long)(remaining < 200 ? remaining : 200) * 1000};
        if (nanosleep(&pause, NULL) && errno != EINTR) {
            snprintf(probe->quiet_reason, sizeof probe->quiet_reason, "quiet_pause");
            break;
        }
    }
    fs = probe->saved_fs;
    memcpy(fs_locks, probe->saved_locks, sizeof fs_locks);
    probe->held_since = 0;
    return 0;
}

static void presentation_fork_quiet_leave(void *context) {
    native_presentation_fork *probe = context;
    fs_release(FS_RP_WORDS);
    fs = probe->saved_fs;
    memcpy(fs_locks, probe->saved_locks, sizeof fs_locks);
}
#endif
