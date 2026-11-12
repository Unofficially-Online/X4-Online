#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "player_reference_probe.h"

#define TEST_PLAYER 0x10000000u
#define TEST_IMAGE 0x400000u

typedef struct {
    p1_reference_probe probe;
    p1_reference_state state;
    unsigned boundary_calls;
    unsigned sample_calls;
    unsigned reference_reads;
    unsigned acquire_calls;
    unsigned release_calls;
    unsigned fail_boundary_at;
    unsigned fail_pins_at;
    unsigned fail_sample_at;
    unsigned stale_sample_at;
    unsigned fail_read_at;
    unsigned change_identity_at;
    unsigned change_player_at;
    unsigned change_count_at;
    unsigned change_marker_at;
    unsigned change_vtable_at;
    int short_sample;
    int no_increment;
    int no_decrement;
    p1_reference_call_result acquire_result;
    p1_reference_call_result release_result;
} fixture;

static int boundary(void *context) {
    fixture *test = context;
    ++test->boundary_calls;
    return test->boundary_calls != test->fail_boundary_at;
}

static int pins(void *context, const p1_reference_pin *expected, unsigned count) {
    fixture *test = context;
    assert(count == 2 && expected[0].va == 0xa238b0 && expected[0].length == 21);
    assert(expected[1].va == 0xa14540 && expected[1].length == 37);
    assert(!strcmp(expected[0].sha256,
                   "0e7c126afb520730e79452b02c5cedd696ecdc918f11b71aab1501dead392f84"));
    assert(!strcmp(expected[1].sha256,
                   "7368cef95d1efcc8cda06dfd53df92ca026f1da024481b82f3c7a86b8db75587"));
    return test->boundary_calls != test->fail_pins_at;
}

static int sample(void *context, p1_reference_identity *identity) {
    fixture *test = context;
    ++test->sample_calls;
    if (test->sample_calls == test->fail_sample_at) {
        return 0;
    }
    if (test->sample_calls == test->stale_sample_at) {
        return 1; /* Success without fresh output must not reuse an old sample. */
    }
    identity->ready = 1;
    identity->player = test->sample_calls == test->change_player_at ? TEST_PLAYER + 0x100 : TEST_PLAYER;
    identity->length = test->short_sample ? 7 : 8;
    memcpy(identity->bytes, "identity", 8);
    if (test->sample_calls == test->change_identity_at) {
        identity->bytes[7] ^= 1; /* Same pointer, different nonpointer identity field. */
    }
    return 1;
}

static int read_reference(void *context, uintptr_t player, p1_reference_state *state) {
    fixture *test = context;
    ++test->reference_reads;
    assert(player == TEST_PLAYER); /* Changed identity must fail before a stale read. */
    if (test->reference_reads == test->fail_read_at) {
        return 0;
    }
    *state = test->state;
    if (test->reference_reads == test->change_count_at) {
        --state->count;
    }
    if (test->reference_reads == test->change_marker_at) {
        state->marker = 3;
    }
    if (test->reference_reads == test->change_vtable_at) {
        state->reference_vtable ^= 8;
    }
    return 1;
}

static p1_reference_call_result acquire(void *context, uintptr_t function, uintptr_t player) {
    fixture *test = context;
    ++test->acquire_calls;
    assert(function == P1_REFERENCE_ACQUIRE_VA && player == TEST_PLAYER);
    assert(test->probe.obligation == P1_REFERENCE_ACQUIRE_PENDING);
    assert(!p1_reference_run(&test->probe)); /* Reentry cannot acquire twice. */
    if (test->acquire_result != P1_REFERENCE_NOT_CALLED && !test->no_increment) {
        ++test->state.count;
    }
    return test->acquire_result;
}

static p1_reference_call_result release(void *context, uintptr_t function, uintptr_t subobject) {
    fixture *test = context;
    ++test->release_calls;
    assert(function == P1_REFERENCE_RELEASE_VA && subobject == TEST_PLAYER + 0x38);
    assert(test->probe.obligation == P1_REFERENCE_RELEASE_PENDING);
    assert(test->state.count >= 3 && test->state.marker == 2);
    assert(!p1_reference_run(&test->probe)); /* Reentry cannot release twice. */
    if (test->release_result != P1_REFERENCE_NOT_CALLED && !test->no_decrement) {
        --test->state.count;
    }
    return test->release_result;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    fixture test = {0};
    test.state = (p1_reference_state){P1_REFERENCE_PLAYER_VTABLE_VA,
                                    P1_REFERENCE_SUBOBJECT_VTABLE_VA,
                                    P1_REFERENCE_ZERO_TARGET_VA, 4, 2};
    test.acquire_result = P1_REFERENCE_RETURNED;
    test.release_result = P1_REFERENCE_RETURNED;
    p1_reference_operations operations = {&test, boundary, pins, sample, read_reference, acquire, release};
    if (!strcmp(argv[1], "off")) {
        assert(!p1_reference_run(&test.probe));
        assert(!p1_reference_status(&test.probe).requested);
        assert(test.acquire_calls == 0 && test.release_calls == 0);
        return 0;
    }
    if (!strcmp(argv[1], "request_guards")) {
        assert(!p1_reference_request(&test.probe, &operations, 0, 8));
        assert(!p1_reference_request(&test.probe, &operations, UINTPTR_MAX, 8));
        assert(!p1_reference_request(&test.probe, &operations, TEST_IMAGE, 0));
        assert(!p1_reference_request(&test.probe, &operations, TEST_IMAGE, P1_REFERENCE_IDENTITY_MAX + 1));
        operations.release = NULL;
        assert(!p1_reference_request(&test.probe, &operations, TEST_IMAGE, 8));
        return 0;
    }
    unsigned phase = 0;
    if (sscanf(argv[1], "boundary_%u", &phase) == 1) {
        test.fail_boundary_at = phase;
    } else if (sscanf(argv[1], "pins_%u", &phase) == 1) {
        test.fail_pins_at = phase;
    } else if (sscanf(argv[1], "sample_%u", &phase) == 1) {
        test.fail_sample_at = phase;
    } else if (sscanf(argv[1], "read_%u", &phase) == 1) {
        test.fail_read_at = phase;
    } else if (sscanf(argv[1], "identity_%u", &phase) == 1) {
        test.change_identity_at = phase;
    } else if (sscanf(argv[1], "player_%u", &phase) == 1) {
        test.change_player_at = phase;
    } else if (sscanf(argv[1], "count_%u", &phase) == 1) {
        test.change_count_at = phase;
    } else if (sscanf(argv[1], "marker_%u", &phase) == 1) {
        test.change_marker_at = phase;
    } else if (sscanf(argv[1], "vtable_%u", &phase) == 1) {
        test.change_vtable_at = phase;
    } else if (!strcmp(argv[1], "stale_sample")) {
        test.stale_sample_at = 2;
        phase = 2;
    } else if (!strcmp(argv[1], "short_sample")) {
        test.short_sample = 1;
        phase = 1;
    } else if (!strcmp(argv[1], "initial_marker1")) {
        test.state.marker = 1;
        phase = 1;
    } else if (!strcmp(argv[1], "initial_count1")) {
        test.state.count = 1;
        phase = 1;
    } else if (!strcmp(argv[1], "initial_overflow")) {
        test.state.count = INT32_MAX;
        phase = 1;
    } else if (!strcmp(argv[1], "primary_vtable")) {
        test.state.player_vtable ^= 8;
        phase = 1;
    } else if (!strcmp(argv[1], "zero_target")) {
        test.state.zero_target ^= 8;
        phase = 1;
    } else if (!strcmp(argv[1], "acquire_refused")) {
        test.acquire_result = P1_REFERENCE_NOT_CALLED;
    } else if (!strcmp(argv[1], "acquire_uncertain")) {
        test.acquire_result = P1_REFERENCE_UNCERTAIN;
    } else if (!strcmp(argv[1], "acquire_uncertain_no_write")) {
        test.acquire_result = P1_REFERENCE_UNCERTAIN;
        test.no_increment = 1;
    } else if (!strcmp(argv[1], "acquire_no_write")) {
        test.no_increment = 1;
        phase = 2;
    } else if (!strcmp(argv[1], "release_refused")) {
        test.release_result = P1_REFERENCE_NOT_CALLED;
    } else if (!strcmp(argv[1], "release_uncertain")) {
        test.release_result = P1_REFERENCE_UNCERTAIN;
    } else if (!strcmp(argv[1], "release_uncertain_no_write")) {
        test.release_result = P1_REFERENCE_UNCERTAIN;
        test.no_decrement = 1;
    } else if (!strcmp(argv[1], "release_no_write")) {
        test.no_decrement = 1;
        phase = 4;
    } else {
        assert(!strcmp(argv[1], "normal"));
    }
    assert(p1_reference_request(&test.probe, &operations, TEST_IMAGE, 8));
    assert(!p1_reference_request(&test.probe, &operations, TEST_IMAGE, 8));
    int passed = p1_reference_run(&test.probe);
    p1_reference_probe_status status = p1_reference_status(&test.probe);
    assert(status.requested && status.attempted);
    if (!strcmp(argv[1], "normal")) {
        assert(passed && status.passed && !status.failed && status.clean);
        assert(test.acquire_calls == 1 && test.release_calls == 1);
        assert(test.state.count == 4 && test.state.marker == 2);
        assert(test.reference_reads == 4 && status.reference_samples == 4);
        assert(status.initial_count == 4 && status.last_observed_count == 4);
        assert(status.last_observed_marker == 2);
    } else {
        assert(!passed && !status.passed && status.failed);
        if (phase == 1 || !strcmp(argv[1], "acquire_refused")) {
            assert(status.clean && status.obligation == P1_REFERENCE_NO_OBLIGATION);
            assert(test.release_calls == 0);
        } else {
            assert(!status.clean && status.obligation != P1_REFERENCE_NO_OBLIGATION);
            assert(test.acquire_calls == 1);
            assert(test.release_calls == (phase == 4 || !strncmp(argv[1], "release_", 8) ? 1u : 0u));
        }
    }
    unsigned acquisitions = test.acquire_calls;
    unsigned releases = test.release_calls;
    assert(!p1_reference_run(&test.probe));
    assert(!p1_reference_request(&test.probe, &operations, TEST_IMAGE, 8));
    assert(test.acquire_calls == acquisitions && test.release_calls == releases);
    assert(p1_reference_status(&test.probe).obligation == status.obligation);
    return 0;
}
