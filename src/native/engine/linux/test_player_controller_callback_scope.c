#include "player_controller_callback_scope.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static _Thread_local uint64_t thread_id = 7;
typedef struct {
    pcc_policy policy;
    int controller, other, first, second;
    unsigned open_calls, close_calls, reports, actual40, actual41, actual43;
    int in_scope, fail_open, fail_close, invalidate, nested, nested_result, scope_error;
    double last_time;
    void *first_seen, *second_seen;
    unsigned char mode_seen;
    const char *last_failure;
} fixture;
static fixture *active;
static uint64_t get_thread(void *context) { (void)context; return thread_id; }
static int identity(void *context, void *controller) {
    fixture *f = context;
    return !f->invalidate && controller == &f->controller;
}
static pcc_scope_state get_scope(void *context, void *controller) {
    fixture *f = context; (void)controller;
    return f->scope_error ? PCC_SCOPE_ERROR : f->in_scope ? PCC_SCOPE_OWN : PCC_SCOPE_OUTSIDE;
}
static int open_scope(void *context, void *controller) {
    fixture *f = context; (void)controller; ++f->open_calls;
    if (f->fail_open) {
        if (f->fail_open == 2) f->in_scope = 1; /* unknown partial effect */
        return 0;
    }
    if (f->nested == 3)
        f->nested_result = pcc_environment(&f->policy, &f->controller, NULL, NULL);
    f->in_scope = 1;
    return 1;
}
static int close_scope(void *context, void *controller) {
    fixture *f = context; (void)controller; ++f->close_calls;
    if (f->fail_close == 1) return 0;
    if (f->fail_close == 2) return 1; /* false success: still in scope */
    f->in_scope = 0;
    return 1;
}
static void report(void *context, const char *reason) {
    fixture *f = context; ++f->reports; f->last_failure = reason;
}
static void original40(void *controller, double now) {
    assert(controller == &active->controller && active->in_scope);
    ++active->actual40; active->last_time = now;
}
static void original41(void *controller, void *first, void *second) {
    assert(controller == &active->controller && active->in_scope);
    ++active->actual41; active->first_seen = first; active->second_seen = second;
    if (active->nested == 4) active->in_scope = 0;
    if (active->nested == 5) active->invalidate = 1;
    if (active->nested == 6) thread_id = 8;
}
static void original43(void *controller, void *first, void *second, unsigned char mode) {
    assert(controller == &active->controller && active->in_scope);
    ++active->actual43; active->first_seen = first; active->second_seen = second;
    active->mode_seen = mode;
    if (active->nested == 1)
        active->nested_result = pcc_environment(&active->policy, controller, first, second);
    if (active->nested == 2)
        active->nested_result = pcc_upkeep(&active->policy, controller, 2.5);
}
/* Stand-in vtable entries use the native SysV signatures; the result is
 * checked because a production failure reporter must not return to X4. */
static void virtual40(void *controller, double now) {
    assert(pcc_upkeep(&active->policy, controller, now));
}
static void virtual41(void *controller, void *first, void *second) {
    assert(pcc_environment(&active->policy, controller, first, second));
}
static void virtual43(void *controller, void *first, void *second, unsigned char mode) {
    assert(pcc_change(&active->policy, controller, first, second, mode));
}
static void initialize(fixture *f) {
    memset(f, 0, sizeof(*f)); active = f;
    pcc_operations operations = {f, get_thread, identity, get_scope, open_scope, close_scope, report};
    pcc_originals originals = {original40, original41, original43};
    assert(pcc_initialize(&f->policy, &operations, &originals, &f->controller, 7));
    assert(!pcc_initialize(&f->policy, &operations, &originals, &f->controller, 7));
}
static void *wrong_thread(void *context) {
    fixture *f = context; thread_id = 8;
    assert(!pcc_upkeep(&f->policy, &f->controller, 1.25));
    return NULL;
}
int main(void) {
    fixture f;
    initialize(&f);
    void (*v40)(void *, double) = virtual40;
    void (*v41)(void *, void *, void *) = virtual41;
    void (*v43)(void *, void *, void *, unsigned char) = virtual43;
    v40(&f.controller, 123.125);
    assert(f.actual40 == 1 && f.last_time == 123.125 && f.open_calls == 1 && f.close_calls == 1);
    assert(atomic_load(&f.policy.calls[0]) == 1 && !f.in_scope);
    f.in_scope = 1;
    v41(&f.controller, &f.first, &f.second);
    assert(f.actual41 == 1 && f.first_seen == &f.first && f.second_seen == &f.second);
    assert(f.open_calls == 1 && f.close_calls == 1 && f.in_scope);
    f.in_scope = 0; f.nested = 1;
    v43(&f.controller, &f.first, &f.second, 0xa5);
    assert(f.actual43 == 1 && f.actual41 == 2 && f.mode_seen == 0xa5 && f.nested_result);
    assert(f.open_calls == 2 && f.close_calls == 2 && !f.in_scope);
    assert(atomic_load(&f.policy.calls[1]) == 2 && atomic_load(&f.policy.calls[2]) == 1);
    initialize(&f); f.nested = 2;
    assert(!pcc_change(&f.policy, &f.controller, &f.first, &f.second, 1));
    assert(!f.nested_result && !f.actual40 && f.actual43 == 1 && f.reports && f.close_calls == 1);
    assert(atomic_load(&f.policy.failure));
    initialize(&f);
    assert(!pcc_environment(&f.policy, &f.other, NULL, NULL));
    assert(!f.actual41 && !f.open_calls && f.reports && !strcmp(f.last_failure, "wrong_controller"));
    assert(!pcc_environment(&f.policy, &f.controller, NULL, NULL));
    assert(!f.actual41);
    initialize(&f); pthread_t worker;
    assert(!pthread_create(&worker, NULL, wrong_thread, &f));
    assert(!pthread_join(worker, NULL));
    assert(!f.actual40 && !f.open_calls && !strcmp(f.last_failure, "wrong_thread"));
    initialize(&f); f.fail_open = 1;
    assert(!pcc_change(&f.policy, &f.controller, NULL, NULL, 0));
    assert(!f.actual43 && f.open_calls == 1 && !f.close_calls && !strcmp(f.last_failure, "scope_open"));
    initialize(&f); f.fail_open = 2;
    assert(!pcc_change(&f.policy, &f.controller, NULL, NULL, 0));
    assert(!f.actual43 && f.in_scope && !f.close_calls && atomic_load(&f.policy.failure));
    initialize(&f); f.nested = 3;
    assert(!pcc_change(&f.policy, &f.controller, NULL, NULL, 0));
    assert(!f.nested_result && !f.actual43 && !f.actual41 && f.close_calls == 1 && !f.in_scope);
    initialize(&f); f.scope_error = 1;
    assert(!pcc_upkeep(&f.policy, &f.controller, 1.0));
    assert(!f.open_calls && !f.actual40 && atomic_load(&f.policy.failure));
    initialize(&f); f.fail_close = 1;
    assert(!pcc_environment(&f.policy, &f.controller, &f.first, &f.second));
    assert(f.actual41 == 1 && f.close_calls == 1 && f.in_scope && atomic_load(&f.policy.failure));
    assert(!pcc_change(&f.policy, &f.controller, NULL, NULL, 0) && !f.actual43);
    initialize(&f); f.fail_close = 2;
    assert(!pcc_environment(&f.policy, &f.controller, NULL, NULL));
    assert(f.actual41 == 1 && f.close_calls == 1 && f.in_scope && atomic_load(&f.policy.failure));
    assert(!strcmp(f.last_failure, "scope_close"));
    initialize(&f); f.nested = 4;
    assert(!pcc_environment(&f.policy, &f.controller, NULL, NULL));
    assert(f.actual41 == 1 && f.close_calls == 1 && atomic_load(&f.policy.failure));
    initialize(&f); f.in_scope = 1; f.nested = 4;
    assert(!pcc_environment(&f.policy, &f.controller, NULL, NULL));
    assert(f.actual41 == 1 && !f.close_calls && atomic_load(&f.policy.failure));
    initialize(&f); f.nested = 5;
    assert(!pcc_environment(&f.policy, &f.controller, NULL, NULL));
    assert(f.actual41 == 1 && !f.close_calls && f.in_scope && atomic_load(&f.policy.failure));
    assert(!strcmp(f.last_failure, "post_callback_identity"));
    initialize(&f); f.nested = 6;
    assert(!pcc_environment(&f.policy, &f.controller, NULL, NULL));
    assert(f.actual41 == 1 && !f.close_calls && f.in_scope && atomic_load(&f.policy.failure));
    assert(!strcmp(f.last_failure, "post_callback_identity"));
    thread_id = 7;
    initialize(&f);
    assert(!pcc_upkeep(&f.policy, &f.controller, NAN));
    assert(!f.actual40 && !f.open_calls && !strcmp(f.last_failure, "invalid_time"));
    initialize(&f); atomic_store(&f.policy.calls[0], UINT64_MAX);
    assert(!pcc_upkeep(&f.policy, &f.controller, 1.0));
    assert(!f.actual40 && f.open_calls == 1 && f.close_calls == 1 && atomic_load(&f.policy.failure));
    puts("player controller callback scopes: ok");
}
