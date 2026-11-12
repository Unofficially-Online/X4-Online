#include "player_ui_plan_kick.h"
#include <assert.h>
#include <stdio.h>

typedef struct {
    uint64_t rng;
    unsigned captures, handlers, emitted;
    int mode, bad_initial, bad_after_first, changed_owner, invalidate_rng;
} fixture;
static int capture(void *context, player_ui_plan_kick_sample *out) {
    fixture *f = context;
    memset(out, 0, sizeof *out);
    ++f->captures;
    out->manager = 1; out->presenter = 2; out->controller = 3;
    if (f->changed_owner && f->captures == 2) out->controller = 4;
    out->view.selector = f->bad_initial && f->captures == 1 ? 2 : 1;
    out->view.first_person = f->mode;
    const char *plan = f->mode ? "firstperson" : "cockpit";
    out->view.plan_length = strlen(plan);
    memcpy(out->view.plan, plan, out->view.plan_length);
    out->rng = f->rng;
    return !(f->bad_after_first && f->captures == 2);
}
static void handler(void *context, int first_person) {
    fixture *f = context;
    assert(first_person == (f->handlers == 0));
    f->mode = first_person;
    ++f->handlers;
    ++f->rng;
}
static void emit(void *context, const char *stage, const player_ui_plan_kick_sample *sample) {
    fixture *f = context;
    static const char *stages[] = {"before", "opposite", "restored"};
    assert(f->emitted < 3 && !strcmp(stage, stages[f->emitted]));
    assert(sample->rng == 100 + f->emitted);
    ++f->emitted;
}
static int rng_valid(void *context, uint64_t *word) {
    fixture *f = context;
    return word == &f->rng && !(f->invalidate_rng && f->handlers);
}
int main(void) {
    fixture f = {.rng = 100};
    player_ui_plan_kick_ops ops = {&f, capture, handler, emit, rng_valid, &f.rng};
    player_ui_plan_kick_sample samples[3];
    uint64_t restored = 0;
    assert(player_ui_plan_kick_run(&ops, samples, &restored));
    assert(f.handlers == 2 && f.captures == 3 && f.emitted == 3 && restored == 100 && f.rng == 100);
    f = (fixture){.rng = 100, .bad_initial = 1};
    assert(!player_ui_plan_kick_run(&ops, samples, &restored) && !f.handlers && f.rng == 100);
    f = (fixture){.rng = 100, .bad_after_first = 1};
    assert(!player_ui_plan_kick_run(&ops, samples, &restored) && f.handlers == 1 && f.rng == 100);
    f = (fixture){.rng = 100, .changed_owner = 1};
    assert(!player_ui_plan_kick_run(&ops, samples, &restored) && f.handlers == 1 && f.rng == 100);
    f = (fixture){.rng = 100, .bad_after_first = 1, .invalidate_rng = 1};
    assert(!player_ui_plan_kick_run(&ops, samples, &restored) && f.handlers == 1 && f.rng == 101);
    puts("UI plan kick sequence PASS");
    return 0;
}
