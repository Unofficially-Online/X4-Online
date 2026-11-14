#ifndef X4_GATE_PLAYER_WORKER_DISPATCH_H
#define X4_GATE_PLAYER_WORKER_DISPATCH_H
#include "player_worker_dispatch_adapter.h"
#include "player_semaphore_dispatch_callback.h"
#include "gate_player_physics_frame_prefix.h"
#include "gate_player_fork_worker_park.h"
#include "gate_player_dispatch_stop.h"
#include <pthread.h>
#include "../lockstep_sha256.h"
static player_worker_dispatch_adapter player_dispatch;
static unsigned player_dispatch_enabled;
static long player_dispatch_main_os_tid;
static _Atomic long player_dispatch_worker_os_tid;
static _Thread_local uint64_t player_dispatch_ticket;
extern int x4mp_semaphore_dispatch_register(player_semaphore_dispatch_callback)
    __attribute__((weak, visibility("default")));
static uint64_t player_dispatch_clock(void *unused) {
    (void)unused;
    return GetTickCount64();
}
static void player_dispatch_pause(void *unused) {
    (void)unused;
    struct timespec pause = {0, 1000000};
    nanosleep(&pause, NULL);
}
static int player_dispatch_snapshot(void *unused, unsigned *count, unsigned *maximum) {
    (void)unused;
    unsigned char *sem = LX(UINT64_C(0x3e59e20));
    if (pthread_mutex_lock((pthread_mutex_t *)sem)) return 0;
    memcpy(count, sem + 0x58, sizeof *count);
    memcpy(maximum, sem + 0x5c, sizeof *maximum);
    return !pthread_mutex_unlock((pthread_mutex_t *)sem);
}
static void player_dispatch_callback(unsigned kind, unsigned returning, uintptr_t caller,
                                     uint64_t argument, uint64_t result, unsigned movement) {
    (void)result;
    uint64_t thread = GetCurrentThreadId();
    long os_tid = (long)syscall(SYS_gettid);
    if (thread == main_thread) {
        if (os_tid != player_dispatch_main_os_tid) player_recurring_fatal("dispatch_main_identity");
    } else if (thread == stock_observer_worker_thread) {
        long expected = 0;
        (void)atomic_compare_exchange_strong_explicit(&player_dispatch_worker_os_tid, &expected, os_tid,
                                                     memory_order_acq_rel, memory_order_acquire);
        if (atomic_load_explicit(&player_dispatch_worker_os_tid, memory_order_acquire) != os_tid)
            player_recurring_fatal("dispatch_worker_identity");
    }
    int ok = 0;
    if (kind == 1 && argument == 1 && thread == main_thread &&
        (caller == (uintptr_t)LX(UINT64_C(0x20251b2)) || caller == (uintptr_t)LX(UINT64_C(0x20251fb))))
        ok = returning ? pwda_post_return(&player_dispatch, thread) : pwda_post_begin(&player_dispatch, thread);
    else if (kind == 2 && movement == 1 && thread == stock_observer_worker_thread)
        ok = returning ? pwda_wait_return(&player_dispatch, thread, player_dispatch_ticket) :
                         pwda_pre_wait(&player_dispatch, thread, &player_dispatch_ticket);
    if (!ok) {
        (void)pwda_fail(&player_dispatch);
        player_recurring_fatal("dispatch_native_activity");
    }
}
/* No native locks: fork census invokes this again while holding its eleven
 * locks. PARKED cannot resume native work until this same main arms/posts.
 * Its resident loop may execute only deadline/pause bookkeeping meanwhile;
 * timeout or foreign activity terminates instead of releasing that park. */
static int player_dispatch_parked_for_fork(long worker_os_tid) {
    uint64_t generation = atomic_load_explicit(&player_dispatch.generation, memory_order_acquire);
    uint64_t frame = atomic_load_explicit(&player_recurring_rendezvous.state, memory_order_acquire);
    uint64_t dispatch = atomic_load_explicit(&player_dispatch.contract.state, memory_order_acquire);
    const uint64_t completed_flags = FWRR_ARMED | FWRR_MAIN_PARKED | FWRR_WORKER_ARRIVED |
                                     FWRR_ENTERED | FWRR_RELEASED | FWRR_MAIN_AUTHORIZED;
    const uint64_t dispatch_flags = PWDC_POST_STARTED | PWDC_POST_RETURNED | PWDC_WAIT_CONSUMED | PWDC_JOINED;
    if (!player_dispatch_enabled || !stats.active ||
        atomic_load_explicit(&player_recurring_mode, memory_order_acquire) != 2 ||
        GetCurrentThreadId() != main_thread || player_dispatch.main_thread != main_thread ||
        player_recurring_rendezvous.main_thread != main_thread ||
        (long)syscall(SYS_gettid) != player_dispatch_main_os_tid ||
        worker_os_tid <= 0 || worker_os_tid == player_dispatch_main_os_tid ||
        worker_os_tid != atomic_load_explicit(&player_dispatch_worker_os_tid, memory_order_acquire) ||
        player_dispatch.worker_thread != stock_observer_worker_thread ||
        player_recurring_rendezvous.worker_thread != stock_observer_worker_thread ||
        stock_observer_worker_thread != ui_thread || !stock_observer_worker_thread ||
        player_dispatch.contract.rendezvous != &player_recurring_rendezvous ||
        pwda_phase(&player_dispatch) != PWDA_PARKED || !generation ||
        generation != stats.completed || generation != stats.issued ||
        generation != atomic_load_explicit(&player_recurring_worker_last, memory_order_acquire) ||
        atomic_load_explicit(&player_recurring_worker_busy, memory_order_acquire) ||
        frame != fwrr_pack(generation, completed_flags) || dispatch != fwrr_pack(generation, dispatch_flags) ||
        atomic_load_explicit(&player_recurring_rendezvous.failure, memory_order_acquire)) return 0;
    return pwda_phase(&player_dispatch) == PWDA_PARKED;
}
static uint64_t player_dispatch_last_receipt;
static void player_dispatch_frame_prefix(uint64_t generation) {
    uint64_t first = player_startup()->take;
    if (!player_dispatch_enabled || !player_recurring_physics_entered() ||
        pwda_phase(&player_dispatch) != PWDA_JOINED || player_dispatch_ticket != generation ||
        atomic_load_explicit(&player_dispatch.generation, memory_order_acquire) != generation ||
        player_recurring_tail_token.generation != generation ||
        generation != (player_dispatch_last_receipt ? player_dispatch_last_receipt + 1 : first))
        player_recurring_fatal("dispatch_frame_receipt");
    if (generation == first)
        lifecycle_discard_write("X4LAN WORKER_DISPATCH bootstrap=1 parked=1 count=0 maximum=1\n");
    char message[128];
    int length = snprintf(message, sizeof message,
        "X4LAN WORKER_DISPATCH tick=%llu ticket=%llu consumed=1 post_returned=1\n",
        (unsigned long long)generation, (unsigned long long)player_dispatch_ticket);
    if (length <= 0 || (size_t)length >= sizeof message) player_recurring_fatal("dispatch_record");
    lifecycle_discard_write(message);
    player_dispatch_last_receipt = generation;
}
static int player_dispatch_startup_join(void) {
    unsigned phase = pwda_phase(&player_dispatch);
    if (GetCurrentThreadId() != player_dispatch.worker_thread ||
        !factory_probe_joined_boundary() || player_dispatch_ticket ||
        atomic_load_explicit(&player_dispatch.generation, memory_order_acquire) ||
        phase < PWDA_REQUESTED || phase > PWDA_DRAIN) return 0;
    long os_tid = (long)syscall(SYS_gettid), expected = 0;
    (void)atomic_compare_exchange_strong_explicit(&player_dispatch_worker_os_tid, &expected, os_tid,
                                                 memory_order_acq_rel, memory_order_acquire);
    return atomic_load_explicit(&player_dispatch_worker_os_tid, memory_order_acquire) == os_tid;
}
static int player_dispatch_post_sites_pinned(void) {
    static const char expected[] = "ebb291194d37371d844c06649077d2ece2d62bd4734e7925190c1abd9bf404ba";
    static const char hex[] = "0123456789abcdef";
    unsigned char digest[32];
    /* The branch and both native callsites are unchanged; provider owns only
     * the independently authenticated callee entry. */
    lockstep_sha256(LX(UINT64_C(0x2025195)), 0x66, digest);
    for (unsigned i = 0; i < sizeof digest; ++i)
        if (expected[2*i] != hex[digest[i] >> 4] || expected[2*i+1] != hex[digest[i] & 15]) return 0;
    return 1;
}
static void player_dispatch_request(void) {
    if (!player_dispatch_stop_claim())
        player_dispatch_stop_terminate(0, atomic_load_explicit(&player_dispatch_stop_ownership,
                                      memory_order_acquire), "takeover-after-stop");
    Dl_info callback_owner = {0}, information_owner = {0};
    void *information = dlsym(RTLD_DEFAULT, "x4mp_semaphore_observer_info");
    if (!x4mp_semaphore_dispatch_register ||
        !player_semaphore_pinned_provider((void *)x4mp_semaphore_dispatch_register, &callback_owner) ||
        !information || !dladdr(information, &information_owner) ||
        callback_owner.dli_fbase != information_owner.dli_fbase)
        player_recurring_fatal("dispatch_provider");
    if (!player_dispatch_post_sites_pinned()) player_recurring_fatal("dispatch_post_pins");
    player_dispatch_main_os_tid = (long)syscall(SYS_gettid);
    player_dispatch.snapshot = player_dispatch_snapshot;
    player_dispatch.clock_ms = player_dispatch_clock;
    player_dispatch.pause = player_dispatch_pause;
    player_dispatch.timeout_ms = PLAYER_RECURRING_TIMEOUT_MS;
    if (!pwda_request(&player_dispatch, main_thread, stock_observer_worker_thread,
                      player_startup()->take, &player_recurring_rendezvous) ||
        !x4mp_semaphore_dispatch_register(player_dispatch_callback))
        player_recurring_fatal("dispatch_registration");
    player_dispatch_stop_published();
}
#endif
