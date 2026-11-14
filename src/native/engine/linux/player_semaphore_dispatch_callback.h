#ifndef X4_PLAYER_SEMAPHORE_DISPATCH_CALLBACK_H
#define X4_PLAYER_SEMAPHORE_DISPATCH_CALLBACK_H
#include <stdint.h>
/* Resident provider callback; runs independently of observation ledger lifetime.
 * kind: ctor=0, post=1, wait=2, timed=3; returning=0/1. */
typedef void (*player_semaphore_dispatch_callback)(unsigned kind, unsigned returning,
    uintptr_t caller, uint64_t argument, uint64_t result, unsigned movement);
#endif
