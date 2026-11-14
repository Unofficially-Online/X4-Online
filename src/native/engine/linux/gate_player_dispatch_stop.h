#ifndef X4_GATE_PLAYER_DISPATCH_STOP_H
#define X4_GATE_PLAYER_DISPATCH_STOP_H
#include <stdatomic.h>
#include <stdio.h>
#include <unistd.h>
enum { PLAYER_DISPATCH_UNOWNED, PLAYER_DISPATCH_REQUESTED,
       PLAYER_DISPATCH_PUBLISHED, PLAYER_DISPATCH_LEGACY_STOP };
/* One-way arbitration with finish: no reset or callback unregistration. A
 * legacy stop may retain legacy cleanup, but takeover after it is unsupported. */
static _Atomic unsigned player_dispatch_stop_ownership;
static inline int player_dispatch_stop_claim(void) {
    unsigned expected = PLAYER_DISPATCH_UNOWNED;
    return atomic_compare_exchange_strong_explicit(&player_dispatch_stop_ownership, &expected,
        PLAYER_DISPATCH_REQUESTED, memory_order_acq_rel, memory_order_acquire);
}
static _Noreturn void player_dispatch_stop_terminate(unsigned reason, unsigned ownership, const char *source) {
    char message[192];
    int length = snprintf(message, sizeof message,
        "X4LAN WORKER_DISPATCH_TERMINAL source=%s reason=%u state=%u policy=terminate-only exit=225\n",
        source, reason, ownership);
    if (length > 0 && (size_t)length < sizeof message)
        (void)write(STDERR_FILENO, message, (size_t)length);
    _exit(225); /* Terminal session stop, distinct from finite-test exit 223. */
}
static inline void player_dispatch_stop_published(void) {
    unsigned expected = PLAYER_DISPATCH_REQUESTED;
    if (!atomic_compare_exchange_strong_explicit(&player_dispatch_stop_ownership, &expected,
        PLAYER_DISPATCH_PUBLISHED, memory_order_acq_rel, memory_order_acquire))
        player_dispatch_stop_terminate(0, expected, "publication-after-stop");
}
static inline void player_dispatch_stop_before_cleanup(unsigned reason) {
    unsigned ownership = PLAYER_DISPATCH_UNOWNED;
    if (atomic_compare_exchange_strong_explicit(&player_dispatch_stop_ownership, &ownership,
        PLAYER_DISPATCH_LEGACY_STOP, memory_order_acq_rel, memory_order_acquire) ||
        ownership == PLAYER_DISPATCH_LEGACY_STOP) return;
    player_dispatch_stop_terminate(reason, ownership, "finish");
}
#endif
