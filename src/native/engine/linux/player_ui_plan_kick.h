#ifndef X4_PLAYER_UI_PLAN_KICK_H
#define X4_PLAYER_UI_PLAN_KICK_H

#include <stdint.h>
#include <string.h>
#include "player_ui_viewstate.h"

/* This policy is only for the already authenticated, process-discard scope.
 * Failure is terminal to its caller; it does not undo native UI events. */
typedef struct {
    player_ui_viewstate view;
    uintptr_t manager, presenter, controller;
    unsigned context_count;
    char context_hash[65];
    uint64_t rng;
} player_ui_plan_kick_sample;
typedef struct {
    void *context;
    int (*capture)(void *, player_ui_plan_kick_sample *);
    void (*handler)(void *, int);
    void (*emit)(void *, const char *, const player_ui_plan_kick_sample *);
    int (*rng_valid)(void *, uint64_t *);
    uint64_t *rng;
} player_ui_plan_kick_ops;

static inline int player_ui_plan_kick_matches(const player_ui_plan_kick_sample *sample,
                                                const player_ui_plan_kick_sample *initial,
                                                int first_person) {
    const char *expected = first_person ? "firstperson" : "cockpit";
    size_t length = first_person ? 11 : 7;
    return sample->manager && sample->presenter && sample->controller &&
        (!initial || (sample->manager == initial->manager && sample->presenter == initial->presenter &&
                      sample->controller == initial->controller)) && sample->view.selector == 1 &&
        sample->view.first_person == first_person && sample->view.plan_length == length &&
        !memcmp(sample->view.plan, expected, length);
}

static inline int player_ui_plan_kick_run(const player_ui_plan_kick_ops *ops,
                                            player_ui_plan_kick_sample samples[3],
                                            uint64_t *rng_restored) {
    if (!ops || !ops->capture || !ops->handler || !ops->emit || !ops->rng_valid || !ops->rng || !samples || !rng_restored ||
        !ops->rng_valid(ops->context, ops->rng))
        return 0;
    uint64_t initial_rng = *ops->rng;
    if (!ops->capture(ops->context, &samples[0]) || samples[0].rng != initial_rng ||
        !player_ui_plan_kick_matches(&samples[0], NULL, 0)) return 0;
    ops->emit(ops->context, "before", &samples[0]);
    ops->handler(ops->context, 1);
    int ok = ops->capture(ops->context, &samples[1]) &&
             player_ui_plan_kick_matches(&samples[1], &samples[0], 1);
    if (ok) {
        ops->emit(ops->context, "opposite", &samples[1]);
        ops->handler(ops->context, 0);
        ok = ops->capture(ops->context, &samples[2]) &&
             player_ui_plan_kick_matches(&samples[2], &samples[0], 0);
        if (ok) ops->emit(ops->context, "restored", &samples[2]);
    }
    if (!ops->rng_valid(ops->context, ops->rng)) return 0;
    *ops->rng = initial_rng;
    *rng_restored = *ops->rng;
    return ok && *rng_restored == initial_rng;
}
#endif
