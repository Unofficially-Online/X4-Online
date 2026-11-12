#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "player_factory_probe.h"

typedef struct {
    player_factory_probe *probe;
    const char *fault;
    char order[64];
    unsigned count;
    uintptr_t singleton;
    uintptr_t forwarded[4];
    unsigned forward_count;
} fixture;

static void record(fixture *test, char operation) {
    assert(test->count + 1 < sizeof(test->order));
    test->order[test->count++] = operation;
    test->order[test->count] = 0;
}

static int fault_is(fixture *test, const char *name) {
    return test->fault && !strcmp(test->fault, name);
}

static int validate(void *context, player_factory_validation phase,
                    uintptr_t universe, uintptr_t player1, uintptr_t macro,
                    uintptr_t player2) {
    fixture *test = context;
    assert(universe == 100);
    assert(player1 == 200);
    assert(macro == 300);
    record(test, 'V');
    if (phase == PLAYER_FACTORY_BEFORE) {
        return !fault_is(test, "before") && test->singleton == player1 && !player2;
    }
    if (phase == PLAYER_FACTORY_CONSTRUCTED) {
        return !fault_is(test, "constructed") && test->singleton == player2 && player2 == 400;
    }
    assert(phase == PLAYER_FACTORY_RESTORED);
    return !fault_is(test, "restored") && test->singleton == player1 && player2 == 400;
}

static int acquire(void *context, uintptr_t player1) {
    fixture *test = context;
    assert(player1 == 200);
    assert(test->probe->player1_uncertain);
    record(test, 'A');
    if (fault_is(test, "nested_acquire")) {
        player_factory_intercept_release(test->probe, 200, 900);
    }
    return !fault_is(test, "acquire");
}

static int forward(void *context, uintptr_t native_reference) {
    fixture *test = context;
    assert(test->probe->forward_pending);
    assert(test->forward_count < 4);
    test->forwarded[test->forward_count++] = native_reference;
    record(test, 'F');
    return !fault_is(test, "forward");
}

static int construct(void *context, player_factory_probe *probe, uintptr_t *output) {
    fixture *test = context;
    assert(*output == 0);
    assert(probe->constructor_pending);
    assert(probe->singleton_uncertain);
    assert(probe->player2_uncertain);
    record(test, 'C');
    if (fault_is(test, "constructor_early")) {
        return 0;
    }
    test->singleton = 400;
    if (!fault_is(test, "missing_intercept")) {
        uintptr_t old_player = fault_is(test, "foreign_old") ? 201 : 200;
        player_factory_intercept_release(probe, old_player, 888);
    }
    if (fault_is(test, "duplicate")) {
        player_factory_intercept_release(probe, 200, 889);
    }
    if (fault_is(test, "null_output")) {
        return 1;
    }
    if (fault_is(test, "alias_output")) {
        *output = 200;
        return 1;
    }
    *output = 400;
    return !fault_is(test, "constructor_late");
}

static int assign(void *context, uintptr_t universe, uintptr_t player1, uintptr_t player2) {
    fixture *test = context;
    assert(universe == 100);
    assert(player1 == 200);
    assert(player2 == 400);
    assert(test->probe->assignment_pending);
    record(test, 'S');
    test->singleton = player1;
    if (fault_is(test, "nested_assign")) {
        player_factory_intercept_release(test->probe, 200, 901);
    }
    return !fault_is(test, "assign");
}

static int release1(void *context, uintptr_t player1) {
    fixture *test = context;
    assert(player1 == 200);
    assert(test->probe->release1_pending);
    record(test, '1');
    return !fault_is(test, "release1");
}

static int release2(void *context, uintptr_t player2) {
    fixture *test = context;
    assert(player2 == 400);
    assert(test->probe->release2_pending);
    record(test, '2');
    if (fault_is(test, "nested_release2")) {
        assert(!player_factory_release_retained(test->probe));
        assert(test->probe->release2_pending && test->probe->player2_uncertain);
    }
    return !fault_is(test, "release2");
}

static void test_success(void) {
    fixture test = {.singleton = 200};
    player_factory_probe probe = {0};
    test.probe = &probe;
    player_factory_operations operations = {
        .context = &test,
        .validate = validate,
        .construct = construct,
        .acquire_player1 = acquire,
        .forward_release = forward,
        .assign_player1 = assign,
        .release_player1 = release1,
        .release_player2 = release2,
    };
    assert(player_factory_run(&probe, &operations, 100, 200, 300));
    assert(!strcmp(test.order, "VCAFVSV12"));
    assert(test.forward_count == 1);
    assert(test.forwarded[0] == 888);
    assert(test.singleton == 200);
    assert(probe.ownership_clean);
    assert(!probe.player1_owned);
    assert(!probe.player2_owned);
    assert(probe.phase == PLAYER_FACTORY_FINISHED);
    assert(!player_factory_release_retained(&probe));
    assert(!player_factory_run(&probe, &operations, 100, 200, 300));
    assert(!strcmp(test.order, "VCAFVSV12"));
    assert(!player_factory_intercept_release(&probe, 200, 999));
    assert(test.forward_count == 2);
    assert(test.forwarded[1] == 999);
    assert(!probe.ownership_clean);
}

static void test_failure(const char *name, const char *order, int retained) {
    fixture test = {.fault = name, .singleton = 200};
    player_factory_probe probe = {0};
    test.probe = &probe;
    player_factory_operations operations = {
        .context = &test,
        .validate = validate,
        .construct = construct,
        .acquire_player1 = acquire,
        .forward_release = forward,
        .assign_player1 = assign,
        .release_player1 = release1,
        .release_player2 = release2,
    };
    int result = retained ?
        player_factory_run_retained(&probe, &operations, 100, 200, 300) :
        player_factory_run(&probe, &operations, 100, 200, 300);
    assert(!result);
    assert(!player_factory_release_retained(&probe));
    assert(!strcmp(test.order, order));
    assert(probe.phase == PLAYER_FACTORY_FAILED);
    assert(probe.problem);
    assert(probe.ownership_clean == !strcmp(name, "before"));
    if (fault_is(&test, "foreign_old")) {
        assert(test.forward_count == 1);
        assert(test.forwarded[0] == 888);
        assert(!probe.capture_attempted);
    }
    if (fault_is(&test, "duplicate")) {
        assert(test.forward_count == 2);
        assert(test.forwarded[0] == 888);
        assert(test.forwarded[1] == 889);
        assert(probe.player1_owned);
    }
    if (fault_is(&test, "nested_acquire")) {
        assert(test.forward_count == 2);
        assert(test.forwarded[0] == 900);
        assert(test.forwarded[1] == 888);
        assert(probe.player1_owned);
    }
    if (fault_is(&test, "acquire")) {
        assert(probe.player1_uncertain);
        assert(test.forward_count == 1);
    }
    if (fault_is(&test, "forward")) {
        assert(probe.forward_pending == 1);
        assert(probe.player1_owned);
    }
    if (fault_is(&test, "constructor_early") || fault_is(&test, "constructor_late")) {
        assert(probe.constructor_pending);
        assert(probe.singleton_uncertain);
        assert(probe.player2_uncertain);
    }
    if (fault_is(&test, "assign") || fault_is(&test, "nested_assign")) {
        assert(probe.assignment_pending);
        assert(probe.player1_owned);
        assert(probe.player2_owned);
    }
    if (fault_is(&test, "release1")) {
        assert(probe.release1_pending);
        assert(probe.player1_owned);
        assert(probe.player2_owned);
    }
    if (fault_is(&test, "release2")) {
        assert(probe.release2_pending);
        assert(!probe.player1_owned);
        assert(probe.player2_owned);
    }
    assert(!player_factory_run(&probe, &operations, 100, 200, 300));
}

static void test_retained(const char *release_fault) {
    fixture test = {.singleton = 200};
    player_factory_probe probe = {0};
    test.probe = &probe;
    player_factory_operations operations = {
        .context = &test,
        .validate = validate,
        .construct = construct,
        .acquire_player1 = acquire,
        .forward_release = forward,
        .assign_player1 = assign,
        .release_player1 = release1,
        .release_player2 = release2,
    };
    assert(player_factory_run_retained(&probe, &operations, 100, 200, 300));
    assert(!strcmp(test.order, "VCAFVSV1"));
    assert(test.singleton == 200);
    assert(probe.phase == PLAYER_FACTORY_RETAINED);
    assert(probe.player2 == 400 && probe.player2_owned);
    assert(!probe.player1_owned && !probe.player1_uncertain);
    assert(!probe.player2_uncertain && !probe.singleton_uncertain);
    assert(!probe.ownership_clean);
    assert(!player_factory_run(&probe, &operations, 100, 200, 300));
    assert(!player_factory_run_retained(&probe, &operations, 100, 200, 300));
    assert(!strcmp(test.order, "VCAFVSV1"));
    test.fault = release_fault;
    if (fault_is(&test, "unexpected_release")) {
        assert(!player_factory_intercept_release(&probe, 200, 999));
        assert(!player_factory_release_retained(&probe));
        assert(!strcmp(test.order, "VCAFVSV1F"));
        assert(probe.player2_owned && !probe.ownership_clean);
        return;
    }
    int released = player_factory_release_retained(&probe);
    assert(!strcmp(test.order, "VCAFVSV12"));
    if (fault_is(&test, "release2")) {
        assert(!released);
        assert(probe.phase == PLAYER_FACTORY_FAILED);
        assert(probe.player2_owned && probe.player2_uncertain);
        assert(probe.release2_pending && !probe.ownership_clean);
        assert(!strcmp(probe.problem, "release_retained_player2"));
    } else {
        assert(released);
        assert(probe.phase == PLAYER_FACTORY_FINISHED);
        assert(!probe.player2_owned && !probe.player2_uncertain);
        assert(!probe.release2_pending && probe.ownership_clean);
    }
    assert(!player_factory_release_retained(&probe));
    assert(!strcmp(test.order, "VCAFVSV12"));
}

static void test_retained_release_guards(void) {
    assert(!player_factory_release_retained(NULL));
    player_factory_probe fresh = {0};
    assert(!player_factory_release_retained(&fresh));
    /* Every unresolved reference/call obligation blocks release without a call. */
    for (unsigned obligation = 0; obligation < 12; ++obligation) {
        fixture test = {0};
        player_factory_probe probe = {
            .phase = PLAYER_FACTORY_RETAINED,
            .player2 = 400,
            .player2_owned = 1,
            .operations = {.context = &test, .release_player2 = release2},
        };
        test.probe = &probe;
        unsigned *blocked[] = {
            &probe.player2_uncertain, &probe.player1_owned,
            &probe.player1_uncertain, &probe.forward_pending,
            &probe.interceptor_active, &probe.constructor_pending,
            &probe.assignment_pending, &probe.singleton_uncertain,
            &probe.release1_pending, &probe.release2_pending,
        };
        if (obligation < sizeof(blocked) / sizeof(blocked[0])) {
            *blocked[obligation] = 1;
        } else if (obligation == 10) {
            probe.problem = "late_failure";
        } else {
            probe.operations.release_player2 = NULL;
        }
        assert(!player_factory_release_retained(&probe));
        assert(!test.count && probe.player2_owned && !probe.ownership_clean);
        assert(probe.phase == PLAYER_FACTORY_RETAINED);
    }
}

int main(void) {
    struct failure_case {
        const char *name;
        const char *order;
    };
    static const struct failure_case cases[] = {
        {"before", "V"},
        {"constructor_early", "VC"},
        {"constructor_late", "VCAF"},
        {"missing_intercept", "VC"},
        {"foreign_old", "VCF"},
        {"duplicate", "VCAFF"},
        {"nested_acquire", "VCAFF"},
        {"acquire", "VCAF"},
        {"forward", "VCAF"},
        {"null_output", "VCAF"},
        {"alias_output", "VCAF"},
        {"constructed", "VCAFV"},
        {"assign", "VCAFVS"},
        {"nested_assign", "VCAFVSF"},
        {"restored", "VCAFVSV"},
        {"release1", "VCAFVSV1"},
        {"release2", "VCAFVSV12"},
    };
    test_success();
    for (unsigned index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        test_failure(cases[index].name, cases[index].order, 0);
        if (strcmp(cases[index].name, "release2")) {
            test_failure(cases[index].name, cases[index].order, 1);
        }
    }
    test_retained(NULL);
    test_retained("release2");
    test_retained("nested_release2");
    test_retained("unexpected_release");
    test_retained_release_guards();
    puts("player factory transaction: 39 scenarios passed");
    return 0;
}
