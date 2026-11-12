#ifndef X4_LINUX_PLAYER_ZONE_LEDGER_H
#define X4_LINUX_PLAYER_ZONE_LEDGER_H

#include <stdint.h>
#include "player_control_banks.h"

/* One ledger per pcb_slot, initialized from zero and bound for its lifetime.
 * The adapter owns three distinct strong-reference locations: LIVE (the native
 * global), SLOT (persistent) and CANONICAL (temporary); EMPTY is a source
 * representing a null strong reference. Initially SLOT and CANONICAL are empty.
 * Null references are permitted. No raw component pointer enters this API.
 *
 * The caller serializes all operations, prevents reentry and keeps the ledger,
 * slot key, callbacks, locations and referenced objects alive through cleanup.
 * thread/boundary prove the admitted worker and exclusive native boundary on
 * EVERY call, including initialization and cleanup. Between active enter and
 * restore native Update may change LIVE by its own valid ownership protocol;
 * otherwise no other actor may change these locations. In particular there is
 * no safe concurrent deletion or mutation during the transaction.
 *
 * inspect(location) is read-only and proves the location is a valid owned
 * strong-reference slot (or null); failure has no effects. assign(destination, source)
 * accepts only those owned locations or EMPTY. DONE guarantees the destination
 * now independently owns the source value, and its previous ownership is fully
 * released. The source and other ledger reference locations are unchanged;
 * unrelated reference ownership is unchanged. Legitimate native lifetime
 * effects of the verified release, such as deletion, are permitted. NO_EFFECT
 * guarantees no assignment or reference ownership effect. UNKNOWN includes partial refcount effects, even when pointer
 * bytes look correct. UNKNOWN permanently poisons the ledger: neither readback
 * nor retry can prove refcounts. The caller must preserve all locations and stop
 * simulation; recovery needs external proof beyond this API. The adapter must
 * establish these exact semantics, including same-value and null assignment.
 */
typedef enum { PZ_LIVE, PZ_SLOT, PZ_CANONICAL, PZ_EMPTY } pz_location;
typedef enum { PZ_NO_EFFECT, PZ_DONE, PZ_UNKNOWN } pz_effect;
typedef struct {
    void *context;
    uint64_t (*thread)(void *);
    int (*boundary)(void *);
    int (*inspect)(void *, pz_location);
    pz_effect (*assign)(void *, pz_location, pz_location);
} pz_operations;
typedef enum {
    PZ_ZERO, PZ_INITIALIZING, PZ_IDLE, PZ_CANON_HELD, PZ_ACTIVE,
    PZ_CAPTURED, PZ_LIVE_RESTORED, PZ_DESTROYING, PZ_DEAD, PZ_POISONED
} pz_phase;
typedef struct {
    unsigned initialized;
    uint64_t owner_thread;
    const pcb_slot *key;
    pz_operations ops;
    pz_phase phase;
    pz_location unknown_destination;
    pz_location unknown_source;
} pz_ledger;

static inline int pz_boundary(pz_ledger *ledger) {
    return ledger && ledger->initialized &&
           ledger->ops.thread(ledger->ops.context) == ledger->owner_thread &&
           ledger->ops.boundary(ledger->ops.context);
}

/* -1 means permanently unknown, 0 means no effect, 1 means completed. */
static inline int pz_assign(pz_ledger *ledger, pz_location destination,
                            pz_location source) {
    pz_operations *operations = &ledger->ops;
    if (!pz_boundary(ledger) ||
        !operations->inspect(operations->context, destination)) {
        return 0;
    }
    if (source != PZ_EMPTY) {
        if (!pz_boundary(ledger) ||
            !operations->inspect(operations->context, source)) {
            return 0;
        }
    }
    if (!pz_boundary(ledger)) {
        return 0;
    }
    /* Record before invoking a callback which may return an uncertain effect. */
    ledger->unknown_destination = destination;
    ledger->unknown_source = source;
    pz_effect effect = operations->assign(operations->context, destination, source);
    if (effect == PZ_DONE) {
        return 1;
    }
    if (effect == PZ_NO_EFFECT) {
        return 0;
    }
    ledger->phase = PZ_POISONED;
    return -1;
}

/* The first call binds the key and callback lifetime even on a failed read.
 * A failed/no-effect initialization may be retried; unknown is never retried. */
static inline int pz_initialize(pz_ledger *ledger, const pcb_slot *slot,
                                const pz_operations *operations, uint64_t owner_thread) {
    if (!ledger || ledger->initialized || !slot || !operations || !owner_thread ||
        !operations->thread || !operations->boundary || !operations->inspect ||
        !operations->assign || operations->thread(operations->context) != owner_thread ||
        !operations->boundary(operations->context)) {
        return 0;
    }
    ledger->initialized = 1;
    ledger->key = slot;
    ledger->ops = *operations;
    ledger->owner_thread = owner_thread;
    ledger->phase = PZ_INITIALIZING;
    return 1;
}

static inline int pz_initialize_slot(pz_ledger *ledger) {
    if (!pz_boundary(ledger) || ledger->phase != PZ_INITIALIZING) {
        return 0;
    }
    if (pz_assign(ledger, PZ_SLOT, PZ_LIVE) != 1) {
        return 0;
    }
    ledger->phase = PZ_IDLE;
    return 1;
}

/* A bank adapter forwards its context and slot key to these functions. */
static inline pcb_zone_result pz_enter(void *context, const pcb_slot *slot) {
    pz_ledger *ledger = (pz_ledger *)context;
    if (!ledger || !ledger->initialized) {
        return PCB_ZONE_INACTIVE;
    }
    if (!pz_boundary(ledger) || ledger->key != slot || ledger->phase != PZ_IDLE) {
        return ledger->phase == PZ_IDLE ? PCB_ZONE_INACTIVE : PCB_ZONE_UNCERTAIN;
    }
    int result = pz_assign(ledger, PZ_CANONICAL, PZ_LIVE);
    if (result == 0) {
        return PCB_ZONE_INACTIVE;
    }
    if (result < 0) {
        return PCB_ZONE_UNCERTAIN;
    }
    ledger->phase = PZ_CANON_HELD;
    result = pz_assign(ledger, PZ_LIVE, PZ_SLOT);
    if (result != 1) {
        return PCB_ZONE_UNCERTAIN;
    }
    ledger->phase = PZ_ACTIVE;
    return PCB_ZONE_ACTIVE;
}

/* Restore resumes only a proven unfinished step. An ACTIVE entry always
 * captures LIVE first, even if bank value entry failed after zone entry. */
static inline int pz_restore(void *context, const pcb_slot *slot) {
    pz_ledger *ledger = (pz_ledger *)context;
    if (!pz_boundary(ledger) || ledger->key != slot) {
        return 0;
    }
    if (ledger->phase == PZ_ACTIVE) {
        if (pz_assign(ledger, PZ_SLOT, PZ_LIVE) != 1) {
            return 0;
        }
        ledger->phase = PZ_CAPTURED;
    }
    if (ledger->phase == PZ_CAPTURED) {
        if (pz_assign(ledger, PZ_LIVE, PZ_CANONICAL) != 1) {
            return 0;
        }
        ledger->phase = PZ_LIVE_RESTORED;
    }
    if (ledger->phase == PZ_CANON_HELD || ledger->phase == PZ_LIVE_RESTORED) {
        if (pz_assign(ledger, PZ_CANONICAL, PZ_EMPTY) != 1) {
            return 0;
        }
        ledger->phase = PZ_IDLE;
        return 1;
    }
    return 0;
}

/* Final cleanup can only release a quiescent persistent slot. The caller must
 * first restore any active transaction and retain this ledger on failure. */
static inline int pz_destroy(pz_ledger *ledger) {
    if (!pz_boundary(ledger)) {
        return 0;
    }
    if (ledger->phase == PZ_IDLE) {
        ledger->phase = PZ_DESTROYING;
    }
    if (ledger->phase != PZ_DESTROYING) {
        return 0;
    }
    if (pz_assign(ledger, PZ_SLOT, PZ_EMPTY) != 1) {
        return 0;
    }
    ledger->phase = PZ_DEAD;
    return 1;
}
#endif
