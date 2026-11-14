#ifndef X4_GATE_PLAYER_CONTROL_RECURRING_H
#define X4_GATE_PLAYER_CONTROL_RECURRING_H

/* Explicitly enabled, bounded readiness-only experiment. Main parses the
 * environment once, publishes the configuration, and arms before FrameTick.
 * Worker captures that published generation at the post-join hook and enters.
 * Normal mode releases there; optional tail mode holds until the movement
 * worker reaches its semaphore call. No input bank, context, or native
 * reference is changed. Main parks after FrameTick and records completions.
 * Tail hook mutation assumes the existing main frame gate has parked the
 * native worker outside that call site; its RX stub stays resident.
 *
 * Neither this adapter nor its generation proves which native worker iteration
 * reached the hook. In particular, an old invocation delayed until after the
 * next arm could impersonate the next pass. Concurrent/repeated invocations
 * observed while a generation is armed are fatal. The busy flag also refuses
 * a new arm while the preceding worker hook is finishing. The caller must not
 * use this diagnostic as player-state admission or proof of reader exclusion. */
#include "gate_player_startup_phases.h"
#include "frame_worker_recurring.h"
#include "player_ui_session_budget.h"
#include "gate_player_recurring_receipt.h"
#include "gate_player_recurring_pause.h"
#include "gate_player_control_worker_tail.h"
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#define PLAYER_RECURRING_DEFAULT_END UINT64_C(30)
#define PLAYER_RECURRING_MAX_END UINT64_C(1000)
#define PLAYER_RECURRING_TIMEOUT_MS 5000u
/* 0 unconfigured, 1 disabled, 2 active, 3 bounded window finished. */
static _Atomic unsigned player_recurring_mode;
static uint64_t player_recurring_end;
static unsigned player_recurring_tail;
static unsigned player_recurring_physics;
static player_ui_session_budget player_session_budget;
static void player_physics_network_begin(uint64_t generation);
static void player_physics_network_finish(uint64_t generation, unsigned final);
extern int x4mp_semaphore_observer_checkpoint(uint64_t) __attribute__((weak, visibility("default")));
static _Thread_local fwrr_token player_recurring_tail_token;
static frame_worker_recurring player_recurring_rendezvous = FRAME_WORKER_RECURRING_INIT;
static _Atomic unsigned player_recurring_worker_busy;
static _Atomic uint64_t player_recurring_worker_last = 4;
static _Atomic uint64_t player_recurring_main_tick;
static _Thread_local player_recurring_receipt player_recurring_last_tail;

static player_recurring_receipt player_recurring_sample(void) {
    fwrr_token token = player_recurring_tail_token;
    player_recurring_receipt receipt = {
        .available = 1,
        .thread = (uint64_t)GetCurrentThreadId(),
        .state = atomic_load_explicit(&player_recurring_rendezvous.state, memory_order_acquire),
        .token_owner = (uintptr_t)token.owner,
        .token_generation = token.generation,
        .token_thread = token.worker_thread,
        .busy = atomic_load_explicit(&player_recurring_worker_busy, memory_order_acquire),
        .main_tick = atomic_load_explicit(&player_recurring_main_tick, memory_order_acquire),
        .worker_last = atomic_load_explicit(&player_recurring_worker_last, memory_order_acquire),
    };
    player_recurring_pause_sample(&receipt);
    return receipt;
}

/* The last-tail sample belongs to this thread. Pause sampling never blocks. */
static inline void player_recurring_diagnostic_failure(void) {
    char message[384];
    player_recurring_receipt current = player_recurring_sample();
    if (player_recurring_receipt_format(message, sizeof message, "last_tail", &player_recurring_last_tail))
        lifecycle_discard_write(message);
    if (player_recurring_receipt_format(message, sizeof message, "current", &current))
        lifecycle_discard_write(message);
}


/* The physics diagnostic uses this only on its admitted worker. It does not
 * extend the rendezvous to renderer or unrelated engine threads. */
static int player_recurring_physics_entered(void) {
    uint64_t state = atomic_load_explicit(&player_recurring_rendezvous.state, memory_order_acquire);
    fwrr_token token = player_recurring_tail_token;
    return player_recurring_physics && player_recurring_tail &&
        GetCurrentThreadId() == stock_observer_worker_thread &&
        atomic_load_explicit(&player_recurring_worker_busy, memory_order_acquire) == 1 &&
        token.owner == &player_recurring_rendezvous && token.worker_thread == GetCurrentThreadId() &&
        token.generation >= player_startup()->take && token.generation <= player_recurring_end &&
        fwrr_generation(state) == token.generation &&
        (state & FWRR_FLAGS) == (FWRR_ARMED | FWRR_MAIN_PARKED | FWRR_WORKER_ARRIVED | FWRR_ENTERED);
}

static _Noreturn void player_recurring_fatal(const char *reason) {
    char message[256];
    int length = snprintf(message, sizeof(message),
        "X4LAN PLAYER_CONTROL_RECURRING_FATAL exit=222 reason=%s state=%llu failure=%u\n",
        reason, (unsigned long long)atomic_load_explicit(&player_recurring_rendezvous.state,
                                                        memory_order_acquire),
        atomic_load_explicit(&player_recurring_rendezvous.failure, memory_order_acquire));
    if (length > 0 && (size_t)length < sizeof(message)) (void)write(STDERR_FILENO, message, (size_t)length);
    _exit(222);
}
static void player_recurring_write_complete(uint64_t generation) {
    char message[256];
    int length = snprintf(message, sizeof(message),
        "X4LAN PLAYER_CONTROL_RECURRING tick=%llu completed=1 readiness_only=%u "
        "bank_writes=%u context_writes=%s reference_calls=%u other_threads_excluded=0%s\n",
        (unsigned long long)generation, !player_recurring_physics,
        !!player_recurring_physics, player_recurring_physics ? "unverified" : "0",
        !!player_recurring_physics, player_recurring_tail ? " tail=1" : "");
    if (length <= 0 || (size_t)length >= sizeof(message)) player_recurring_fatal("format");
    size_t written = 0;
    while (written < (size_t)length) {
        ssize_t count = write(STDERR_FILENO, message + written, (size_t)length - written);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) player_recurring_fatal("record_write");
        written += (size_t)count;
    }
}
/* Only the main frame caller configures; immutable data is release-published. */
static unsigned player_recurring_configure(void) {
    unsigned mode = atomic_load_explicit(&player_recurring_mode, memory_order_acquire);
    if (mode) return mode;
    const char *setting = getenv("X4LAN_PLAYER_CONTROL_RECURRING");
    const char *tail_setting = getenv("X4LAN_PLAYER_CONTROL_RECURRING_TAIL");
    const char *physics_setting = getenv("X4_DEV_PLAYER_PHYSICS");
    const char *session_text = getenv("X4_DEV_UI_SESSION_FRAMES");
    const player_startup_phases *phase = player_startup();
    if (session_text && !player_ui_session_parse_phased(session_text, phase, &player_session_budget))
        player_recurring_fatal("invalid_session_budget");
    if (physics_setting && *physics_setting && strcmp(physics_setting, "single-frame-discard") &&
        strcmp(physics_setting, "bounded-frames-discard") &&
        strcmp(physics_setting, "network-frames-discard"))
        player_recurring_fatal("invalid_physics_mode");
    if (tail_setting && *tail_setting && strcmp(tail_setting, "1"))
        player_recurring_fatal("invalid_tail_mode");
    if (!setting || !*setting) {
        if (session_text) player_recurring_fatal("session_requires_recurring");
        if (physics_setting && *physics_setting) player_recurring_fatal("physics_requires_recurring");
        if (tail_setting && *tail_setting) player_recurring_fatal("tail_requires_recurring");
        atomic_store_explicit(&player_recurring_mode, 1, memory_order_release);
        return 1;
    }
    if (strcmp(setting, "1")) player_recurring_fatal("invalid_mode");
    const char *lifecycle_mode = getenv("X4_DEV_PLAYER_LIFECYCLE");
    if ((lifecycle_mode && *lifecycle_mode) || player_lifecycle_discard_requested() ||
        atomic_load_explicit(&player_control_boundary_requested, memory_order_acquire))
        player_recurring_fatal("one_shot_conflict");
    uint64_t end = PLAYER_RECURRING_DEFAULT_END;
    const char *end_text = getenv("X4LAN_PLAYER_CONTROL_RECURRING_END");
    uint64_t maximum_end = session_text ? player_session_budget.recurring_end : PLAYER_RECURRING_MAX_END;
    if (end_text) {
        if (!*end_text) player_recurring_fatal("invalid_end");
        end = 0;
        for (const char *p = end_text; *p; ++p) {
            if (*p < '0' || *p > '9' || end > maximum_end / 10)
                player_recurring_fatal("invalid_end");
            end = end * 10 + (unsigned)(*p - '0');
            if (end > maximum_end) player_recurring_fatal("invalid_end");
        }
        if (end < player_startup()->take) player_recurring_fatal("invalid_end");
    }
    player_recurring_tail = tail_setting && *tail_setting;
    player_recurring_physics = !physics_setting || !*physics_setting ? 0 :
        !strcmp(physics_setting, "single-frame-discard") ? 1 :
        !strcmp(physics_setting, "bounded-frames-discard") ? 2 : 3;
    if (phase->version && !session_text) player_recurring_fatal("phases_require_session");
    if (session_text && (player_recurring_physics != 3 || !player_recurring_tail ||
                         !end_text || end != player_session_budget.recurring_end))
        player_recurring_fatal("session_config_mismatch");
    if (player_recurring_physics == 1 && (!player_recurring_tail || end != player_startup()->take))
        player_recurring_fatal("physics_requires_single_tail");
    if (player_recurring_physics >= 2 &&
        (!player_recurring_tail || end <= player_startup()->take || end > (session_text ? player_session_budget.recurring_end :
                 player_recurring_physics == 3 ? 604u : 184u)))
        player_recurring_fatal("physics_requires_bounded_tail");
    player_recurring_end = end;
    atomic_store_explicit(&player_recurring_worker_last, phase->take - 1, memory_order_relaxed);
    atomic_store_explicit(&player_recurring_mode, 2, memory_order_release);
    return 2;
}
static int player_recurring_selected(void) {
    unsigned mode = atomic_load_explicit(&player_recurring_mode, memory_order_acquire);
    return mode == 2 || mode == 3;
}
static void player_recurring_wait(uint64_t generation, unsigned long long started, int worker) {
    if (GetTickCount64() - started >= PLAYER_RECURRING_TIMEOUT_MS) {
        (void)fwrr_cancel(&player_recurring_rendezvous, generation, GetCurrentThreadId(), worker, 1);
        player_recurring_fatal(worker ? "worker_timeout" : "main_timeout");
    }
    const struct timespec pause = {0, 1000000};
    nanosleep(&pause, NULL);
}
/* Return 2 only for this call's arm; 0 means no recurring park this frame. */
static void player_ui_camera_refill_tail(uint64_t generation);
static void player_stock_view_observer_close(uint64_t generation);
static void player_recurring_tail_callback(void *semaphore, unsigned count);
static int player_recurring_before_frame(void) {
    unsigned mode = player_recurring_configure();
    if (mode == 1 || mode == 3) return 0;
    if (atomic_load_explicit(&player_control_boundary_requested, memory_order_acquire))
        player_recurring_fatal("one_shot_conflict");
    if (!stats.active) return 0;
    uint64_t generation = stats.issued;
    atomic_store_explicit(&player_recurring_main_tick, generation, memory_order_release);
    if (generation < player_startup()->take) return 0;
    if (generation > player_recurring_end) {
        uint64_t completed = atomic_load_explicit(&player_recurring_rendezvous.state,
                                                   memory_order_acquire);
        if (atomic_load_explicit(&player_recurring_worker_busy, memory_order_acquire) ||
            atomic_load_explicit(&player_recurring_worker_last, memory_order_acquire) != player_recurring_end ||
            fwrr_generation(completed) != player_recurring_end ||
            (completed & (FWRR_ARMED | FWRR_RELEASED | FWRR_MAIN_AUTHORIZED | FWRR_POISONED)) !=
                (FWRR_ARMED | FWRR_RELEASED | FWRR_MAIN_AUTHORIZED))
            player_recurring_fatal("incomplete_window");
        if (player_recurring_tail) {
            if (generation != player_recurring_end + 1) player_recurring_fatal("tail_restore_generation");
            if (GetCurrentThreadId() != main_thread || boot_phase != 3 || !path_installed ||
                !stock_observer_worker_thread || stock_observer_worker_thread != ui_thread)
                player_recurring_fatal("tail_restore_admission");
            if (!player_tail_restore(&player_tail_resident)) player_recurring_fatal("tail_restore");
            const char record[] = "X4LAN PLAYER_CONTROL_RECURRING tail=1 hook_restored=1\n";
            if (write(STDERR_FILENO, record, sizeof(record) - 1) != (ssize_t)(sizeof(record) - 1))
                player_recurring_fatal("restore_record_write");
        }
        atomic_store_explicit(&player_recurring_mode, 3, memory_order_release);
        return 0;
    }
    if (GetCurrentThreadId() != main_thread || boot_phase != 3 || !path_installed ||
        !stock_observer_worker_thread || stock_observer_worker_thread != ui_thread ||
        atomic_load_explicit(&player_recurring_worker_busy, memory_order_acquire) ||
        atomic_load_explicit(&player_recurring_worker_last, memory_order_acquire) != generation - 1)
        player_recurring_fatal("arm_admission");
    if (generation == player_startup()->take &&
        !fwrr_initialize_at(&player_recurring_rendezvous, main_thread, stock_observer_worker_thread,
                            player_startup()->take))
        player_recurring_fatal("registration");
    if (generation == player_startup()->take && player_recurring_tail &&
        !player_tail_install(&player_tail_resident, player_recurring_tail_callback))
        player_recurring_fatal("tail_install");
    if (fwrr_arm(&player_recurring_rendezvous, generation, GetCurrentThreadId()) != FWRR_OK)
        player_recurring_fatal("arm");
    return 2;
}
static void player_recurring_after_frame(void) {
    uint64_t state = atomic_load_explicit(&player_recurring_rendezvous.state, memory_order_acquire);
    uint64_t generation = fwrr_generation(state);
    if (fwrr_main_park(&player_recurring_rendezvous, generation, GetCurrentThreadId()) != FWRR_OK)
        player_recurring_fatal("main_arrival");
    unsigned long long started = GetTickCount64();
    for (;;) {
        fwrr_result result = fwrr_main_poll(&player_recurring_rendezvous, generation, GetCurrentThreadId());
        if (result == FWRR_MAIN_RETURN_ALLOWED) break;
        if (result != FWRR_WAIT) player_recurring_fatal("main_poll");
        player_recurring_wait(generation, started, 0);
    }
    player_recurring_write_complete(generation);
}
/* This marker is emitted before release, so the main completion record can
 * only follow it. Logging and the native movement are not input-bank writes. */
static void player_recurring_tail_record(uint64_t generation, const char *event) {
    char message[160];
    int length = snprintf(message, sizeof(message),
        "X4LAN PLAYER_CONTROL_RECURRING_WORKER tick=%llu tail=1 event=%s\n",
        (unsigned long long)generation, event);
    if (length <= 0 || (size_t)length >= sizeof(message)) player_recurring_fatal("tail_format");
    size_t written = 0;
    while (written < (size_t)length) {
        ssize_t count = write(STDERR_FILENO, message + written, (size_t)length - written);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) player_recurring_fatal("tail_record_write");
        written += (size_t)count;
    }
}
static void player_recurring_tail_callback(void *semaphore, unsigned count) {
    fwrr_token token = player_recurring_tail_token;
    uint64_t generation = token.generation;
    uint64_t state = atomic_load_explicit(&player_recurring_rendezvous.state, memory_order_acquire);
    if (!player_recurring_tail || GetCurrentThreadId() != stock_observer_worker_thread ||
        semaphore != LX(UINT64_C(0x3daab20)) || count != 1 ||
        !atomic_load_explicit(&player_recurring_worker_busy, memory_order_acquire) ||
        token.owner != &player_recurring_rendezvous || token.worker_thread != GetCurrentThreadId() ||
        generation < player_startup()->take || generation > player_recurring_end ||
        fwrr_generation(state) != generation ||
        (state & (FWRR_ENTERED | FWRR_RELEASED | FWRR_MAIN_AUTHORIZED | FWRR_POISONED)) != FWRR_ENTERED ||
        atomic_load_explicit(&player_recurring_worker_last, memory_order_acquire) != generation - 1)
        player_recurring_fatal("tail_admission");
    if (player_recurring_physics) {
        player_ui_camera_refill_tail(generation);
        if ((state & FWRR_FLAGS) !=
            (FWRR_ARMED | FWRR_MAIN_PARKED | FWRR_WORKER_ARRIVED | FWRR_ENTERED))
            player_recurring_fatal("physics_tail_admission");
        player_recurring_last_tail = player_recurring_sample();
        if (player_recurring_physics == 1) {
            /* One-pass mode never releases the native semaphore. */
            player_physics_discard_finish();
        }
        if (player_recurring_physics == 3) {
            if (generation == player_recurring_end) {
                player_view_ancestry_close(generation);
                player_stock_view_observer_close(generation);
                const char *observer = getenv("X4MP_SEMAPHORE_OBSERVER");
                if (observer) {
                    Dl_info checkpoint_owner = {0}, information_owner = {0};
                    void *information = dlsym(RTLD_DEFAULT, "x4mp_semaphore_observer_info");
                    if (strcmp(observer, "1") || !x4mp_semaphore_observer_checkpoint ||
                        !player_semaphore_pinned_provider((void *)x4mp_semaphore_observer_checkpoint, &checkpoint_owner) ||
                        !information || !dladdr(information, &information_owner) ||
                        checkpoint_owner.dli_fbase != information_owner.dli_fbase ||
                        !player_recurring_physics_entered()) player_recurring_fatal("semaphore_checkpoint_admission");
                    int complete = x4mp_semaphore_observer_checkpoint(generation);
                    char checkpoint[192];
                    int length = snprintf(checkpoint, sizeof checkpoint,
                        "X4LAN SEMAPHORE OBSERVER CHECKPOINT tick=%llu owner=%ld main_parked=1 complete=%d\n",
                        (unsigned long long)generation, (long)syscall(SYS_gettid), complete == 1);
                    if (length <= 0 || (size_t)length >= sizeof checkpoint) player_recurring_fatal("semaphore_checkpoint_format");
                    lifecycle_discard_write(checkpoint);
                }
            }
            player_physics_network_finish(generation, generation == player_recurring_end);
        } else {
            player_physics_frames_finish(generation, generation == player_recurring_end);
        }
    }
    player_recurring_tail_record(generation, "release");
    player_recurring_tail_token = (fwrr_token){0};
    atomic_store_explicit(&player_recurring_worker_last, generation, memory_order_release);
    if (fwrr_worker_release(&player_recurring_rendezvous, generation, GetCurrentThreadId(), &token) != FWRR_OK)
        player_recurring_fatal("tail_release");
    atomic_store_explicit(&player_recurring_worker_busy, 0, memory_order_release);
}
/* Return 1 when this mode owns the hook, including frames outside the window. */
static int player_recurring_after_join(void) {
    unsigned mode = atomic_load_explicit(&player_recurring_mode, memory_order_acquire);
    if (mode != 2 && mode != 3) return 0;
    if (mode == 3) return 1;
    uint64_t state = atomic_load_explicit(&player_recurring_rendezvous.state, memory_order_acquire);
    if (!state) {
        if (atomic_load_explicit(&player_recurring_main_tick, memory_order_acquire) >=
            player_startup()->take) player_recurring_fatal("worker_before_arm");
        return 1; /* before the bounded window */
    }
    unsigned idle = 0;
    if (!atomic_compare_exchange_strong_explicit(&player_recurring_worker_busy, &idle, 1,
                                                  memory_order_acq_rel, memory_order_acquire))
        player_recurring_fatal("worker_reentry");
    uint64_t generation = fwrr_generation(state);
    if (!factory_probe_joined_boundary() || generation < player_startup()->take ||
        generation > player_recurring_end ||
        atomic_load_explicit(&player_recurring_worker_last, memory_order_acquire) != generation - 1 ||
        fwrr_worker_arrive(&player_recurring_rendezvous, generation, GetCurrentThreadId()) != FWRR_OK)
        player_recurring_fatal("worker_arrival");
    fwrr_token token = {0};
    unsigned long long started = GetTickCount64();
    for (;;) {
        fwrr_result result = fwrr_worker_try_enter(&player_recurring_rendezvous, generation,
                                                   GetCurrentThreadId(), &token);
        if (result == FWRR_WORKER_ENTERED) break;
        if (result != FWRR_WAIT) player_recurring_fatal("worker_enter");
        player_recurring_wait(generation, started, 1);
    }
    if (!factory_probe_joined_boundary()) player_recurring_fatal("worker_boundary_changed");
    if (player_recurring_tail) {
        if (player_recurring_tail_token.owner) player_recurring_fatal("tail_token_reentry");
        player_recurring_tail_token = token;
        player_recurring_tail_record(generation, "enter");
        if (player_recurring_physics) {
            uint64_t entered = atomic_load_explicit(&player_recurring_rendezvous.state,
                                                     memory_order_acquire);
            if (fwrr_generation(entered) != generation ||
                (entered & FWRR_FLAGS) !=
                    (FWRR_ARMED | FWRR_MAIN_PARKED | FWRR_WORKER_ARRIVED | FWRR_ENTERED))
                player_recurring_fatal("physics_entry_admission");
            if (player_recurring_physics == 1) {
                player_physics_discard_begin();
            } else if (player_recurring_physics == 3) {
                player_view_ancestry_before(generation);
                player_physics_network_begin(generation);
                player_view_ancestry_after(generation);
            } else {
                player_physics_frames_begin(generation);
            }
        }
        return 1;
    }
    atomic_store_explicit(&player_recurring_worker_last, generation, memory_order_release);
    if (fwrr_worker_release(&player_recurring_rendezvous, generation, GetCurrentThreadId(), &token) != FWRR_OK)
        player_recurring_fatal("worker_release");
    /* Last operation in this adapter; the caller still returns through the
     * existing path hook, so this is not proof that the engine pass ended. */
    atomic_store_explicit(&player_recurring_worker_busy, 0, memory_order_release);
    return 1;
}
#endif
