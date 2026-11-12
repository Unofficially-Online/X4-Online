#ifndef X4_LINUX_PLAYER_FACTORY_PROBE_H
#define X4_LINUX_PLAYER_FACTORY_PROBE_H

/* Engine-free transaction coordinator for a private-profile constructor probe.
 * No engine address, lifetime guarantee, thread safety, seating or rollback is
 * supplied here. The adapter must prove canonical identity and the callsite,
 * serialize all operations, and forward every intercepted native release once.
 * Zero-initialize the resident probe before its single explicit attempt.
 * A failed callback may already have changed engine state; the ledger then
 * retains uncertain obligations and the isolated process must be discarded.
 */
#include <stdint.h>
#include <string.h>

typedef struct player_factory_probe player_factory_probe;

typedef enum {
    PLAYER_FACTORY_BEFORE,
    PLAYER_FACTORY_CONSTRUCTED,
    PLAYER_FACTORY_RESTORED
} player_factory_validation;

typedef enum {
    PLAYER_FACTORY_IDLE,
    PLAYER_FACTORY_CONSTRUCTING,
    PLAYER_FACTORY_RESTORING,
    PLAYER_FACTORY_RELEASING,
    PLAYER_FACTORY_FINISHED,
    PLAYER_FACTORY_FAILED,
    PLAYER_FACTORY_RETAINED
} player_factory_phase;

typedef struct {
    void *context;
    int (*validate)(void *context, player_factory_validation phase,
                    uintptr_t universe, uintptr_t player1, uintptr_t macro,
                    uintptr_t player2);
    int (*construct)(void *context, player_factory_probe *probe, uintptr_t *output);
    int (*acquire_player1)(void *context, uintptr_t player1);
    int (*forward_release)(void *context, uintptr_t native_reference);
    int (*assign_player1)(void *context, uintptr_t universe, uintptr_t player1,
                          uintptr_t player2);
    int (*release_player1)(void *context, uintptr_t player1);
    int (*release_player2)(void *context, uintptr_t player2);
} player_factory_operations;

struct player_factory_probe {
    player_factory_operations operations;
    uintptr_t universe;
    uintptr_t player1;
    uintptr_t macro;
    uintptr_t player2;
    player_factory_phase phase;
    const char *problem;
    unsigned attempted;
    unsigned interceptor_calls;
    unsigned interceptor_active;
    unsigned forward_pending;
    unsigned capture_attempted;
    unsigned player1_owned;
    unsigned player1_uncertain;
    unsigned constructor_pending;
    unsigned player2_owned;
    unsigned player2_uncertain;
    unsigned assignment_pending;
    unsigned singleton_uncertain;
    unsigned release1_pending;
    unsigned release2_pending;
    /* Only the known reference ledger is clear; engine side effects may remain. */
    unsigned ownership_clean;
};

static int player_factory_fail(player_factory_probe *probe, const char *reason) {
    if (!probe->problem) {
        probe->problem = reason;
    }
    return 0;
}

/* Called only by the scoped native release interceptor. native_reference is
 * passed through untouched even on mismatch, duplicates or nested capture.
 * The adapter establishes the relationship of old_player and native_reference. */
static int player_factory_intercept_release(player_factory_probe *probe,
                                            uintptr_t old_player,
                                            uintptr_t native_reference) {
    int eligible = probe->phase == PLAYER_FACTORY_CONSTRUCTING &&
        !probe->problem && !probe->interceptor_calls &&
        !probe->interceptor_active && old_player == probe->player1;
    ++probe->interceptor_calls;
    /* This native release must still be forwarded if capture is interrupted. */
    ++probe->forward_pending;
    probe->ownership_clean = 0;
    if (eligible) {
        probe->interceptor_active = 1;
        probe->capture_attempted = 1;
        probe->player1_uncertain = 1;
        if (probe->operations.acquire_player1(probe->operations.context, old_player)) {
            probe->player1_owned = 1;
            probe->player1_uncertain = 0;
        } else {
            player_factory_fail(probe, "acquire_player1");
        }
        probe->interceptor_active = 0;
    } else {
        player_factory_fail(probe, "unexpected_release");
    }
    if (probe->operations.forward_release(probe->operations.context, native_reference)) {
        --probe->forward_pending;
    } else {
        player_factory_fail(probe, "forward_release");
    }
    return probe->problem == NULL;
}

/* One explicit attempt only. The output storage is initially zero. Callback
 * failure means unknown effects, never permission to guess cleanup or retry. */
static int player_factory_run_mode(player_factory_probe *probe,
                                   const player_factory_operations *operations,
                                   uintptr_t universe, uintptr_t player1, uintptr_t macro,
                                   int retain_player2) {
    if (!probe || !operations || !operations->validate || !operations->construct ||
        !operations->acquire_player1 || !operations->forward_release ||
        !operations->assign_player1 || !operations->release_player1 ||
        !operations->release_player2 || !universe || !player1 || !macro ||
        probe->attempted) {
        return 0;
    }
    memset(probe, 0, sizeof(*probe));
    probe->operations = *operations;
    probe->universe = universe;
    probe->player1 = player1;
    probe->macro = macro;
    probe->attempted = 1;
    probe->ownership_clean = 1;
    if (!operations->validate(operations->context, PLAYER_FACTORY_BEFORE,
                              universe, player1, macro, 0) || probe->problem) {
        probe->phase = PLAYER_FACTORY_FAILED;
        return player_factory_fail(probe, "initial_identity");
    }

    probe->phase = PLAYER_FACTORY_CONSTRUCTING;
    probe->ownership_clean = 0;
    probe->constructor_pending = 1;
    probe->singleton_uncertain = 1;
    probe->player2_uncertain = 1;
    uintptr_t output = 0;
    int constructed = operations->construct(operations->context, probe, &output);
    probe->player2 = output;
    if (!constructed) {
        probe->phase = PLAYER_FACTORY_FAILED;
        return player_factory_fail(probe, "constructor");
    }
    probe->constructor_pending = 0;
    if (probe->problem || probe->interceptor_calls != 1 || probe->forward_pending ||
        !probe->player1_owned || probe->player1_uncertain || !output || output == player1) {
        probe->phase = PLAYER_FACTORY_FAILED;
        return player_factory_fail(probe, "constructor_ledger");
    }
    if (!operations->validate(operations->context, PLAYER_FACTORY_CONSTRUCTED,
                              universe, player1, macro, output) || probe->problem) {
        probe->phase = PLAYER_FACTORY_FAILED;
        return player_factory_fail(probe, "constructed_identity");
    }
    probe->player2_owned = 1;
    probe->player2_uncertain = 0;

    probe->phase = PLAYER_FACTORY_RESTORING;
    probe->assignment_pending = 1;
    if (!operations->assign_player1(operations->context, universe, player1, output) ||
        probe->problem) {
        probe->phase = PLAYER_FACTORY_FAILED;
        return player_factory_fail(probe, "restore_assignment");
    }
    probe->assignment_pending = 0;
    if (!operations->validate(operations->context, PLAYER_FACTORY_RESTORED,
                              universe, player1, macro, output) || probe->problem) {
        probe->phase = PLAYER_FACTORY_FAILED;
        return player_factory_fail(probe, "restored_identity");
    }
    probe->singleton_uncertain = 0;
    probe->phase = PLAYER_FACTORY_RELEASING;
    probe->release1_pending = 1;
    if (!operations->release_player1(operations->context, player1) || probe->problem) {
        probe->phase = PLAYER_FACTORY_FAILED;
        return player_factory_fail(probe, "release_player1");
    }
    probe->release1_pending = 0;
    probe->player1_owned = 0;
    if (retain_player2) {
        probe->phase = PLAYER_FACTORY_RETAINED;
        /* This reference remains ours. Canonical restoration is not cleanup. */
        return 1;
    }
    probe->release2_pending = 1;
    if (!operations->release_player2(operations->context, output) || probe->problem) {
        probe->phase = PLAYER_FACTORY_FAILED;
        return player_factory_fail(probe, "release_player2");
    }
    probe->release2_pending = 0;
    probe->player2_owned = 0;
    probe->phase = PLAYER_FACTORY_FINISHED;
    probe->ownership_clean = 1;
    return 1;
}

/* Existing roundtrip: restore P1, then release both references owned here. */
static int player_factory_run(player_factory_probe *probe,
                              const player_factory_operations *operations,
                              uintptr_t universe, uintptr_t player1, uintptr_t macro) {
    return player_factory_run_mode(probe, operations, universe, player1, macro, 0);
}

/* Restore P1 and release our temporary P1 hold, retaining exactly the factory
 * result reference. The adapter must keep its context and callbacks alive and
 * serialize later use/release. No seating or lifetime permission is supplied. */
static int player_factory_run_retained(player_factory_probe *probe,
                                      const player_factory_operations *operations,
                                      uintptr_t universe, uintptr_t player1,
                                      uintptr_t macro) {
    return player_factory_run_mode(probe, operations, universe, player1, macro, 1);
}

/* Release the retained result once, only after the adapter has undone all uses
 * of it (including borrowed seating). The callback must freshly validate its
 * own native boundary and reference. Failure may mean the release happened:
 * retain the uncertain ledger and never retry or dereference the result. */
static int player_factory_release_retained(player_factory_probe *probe) {
    if (!probe || probe->phase != PLAYER_FACTORY_RETAINED || probe->problem ||
        !probe->player2 || !probe->player2_owned || probe->player2_uncertain ||
        probe->player1_owned || probe->player1_uncertain || probe->forward_pending ||
        probe->interceptor_active || probe->constructor_pending ||
        probe->assignment_pending || probe->singleton_uncertain ||
        probe->release1_pending || probe->release2_pending ||
        !probe->operations.release_player2) {
        return 0;
    }
    probe->phase = PLAYER_FACTORY_RELEASING;
    probe->ownership_clean = 0;
    probe->release2_pending = 1;
    probe->player2_uncertain = 1;
    if (!probe->operations.release_player2(probe->operations.context, probe->player2) ||
        probe->problem) {
        probe->phase = PLAYER_FACTORY_FAILED;
        return player_factory_fail(probe, "release_retained_player2");
    }
    probe->release2_pending = 0;
    probe->player2_uncertain = 0;
    probe->player2_owned = 0;
    probe->phase = PLAYER_FACTORY_FINISHED;
    probe->ownership_clean = 1;
    return 1;
}
#endif
