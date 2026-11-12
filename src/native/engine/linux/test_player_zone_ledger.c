#include "player_zone_ledger.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    int value[3], refs[8];
    uint64_t thread;
    int boundary, fail_read, result, calls, reads, fail_at, fail_effect;
    int invalidate_after_assign, invalidate_after_read, invalidate_thread;
} fixture;
static uint64_t thread(void *context) { return ((fixture *)context)->thread; }
static int boundary(void *context) { return ((fixture *)context)->boundary; }
static int inspect(void *context, pz_location location) {
    fixture *fixture_state = context;
    ++fixture_state->reads;
    (void)location;
    if (fixture_state->reads == fixture_state->invalidate_after_read) {
        fixture_state->boundary = 0;
    }
    if (fixture_state->fail_read) {
        fixture_state->fail_read = 0;
        return 0;
    }
    return 1;
}
static pz_effect assign(void *context, pz_location destination, pz_location source) {
    fixture *fixture_state = context;
    ++fixture_state->calls;
    int effect = fixture_state->calls == fixture_state->fail_at
        ? fixture_state->fail_effect : fixture_state->result;
    fixture_state->result = PZ_DONE;
    if (effect == PZ_NO_EFFECT) {
        return PZ_NO_EFFECT;
    }
    int old = fixture_state->value[destination];
    int value = source == PZ_EMPTY ? 0 : fixture_state->value[source];
    if (value) {
        ++fixture_state->refs[value];
    }
    if (old) {
        --fixture_state->refs[old];
    }
    fixture_state->value[destination] = value;
    if (fixture_state->calls == fixture_state->invalidate_after_assign) {
        if (fixture_state->invalidate_thread) {
            fixture_state->thread = 8;
        } else {
            fixture_state->boundary = 0;
        }
    }
    return (pz_effect)effect;
}
static void setup(fixture *fixture_state, pz_ledger *ledger, pcb_slot *key) {
    memset(fixture_state, 0, sizeof(*fixture_state));
    memset(ledger, 0, sizeof(*ledger));
    fixture_state->thread = 9;
    fixture_state->boundary = 1;
    fixture_state->result = PZ_DONE;
    fixture_state->value[PZ_LIVE] = 1;
    fixture_state->refs[1] = 1;
    pz_operations operations = {fixture_state, thread, boundary, inspect, assign};
    assert(pz_initialize(ledger, key, &operations, 9));
    assert(pz_initialize_slot(ledger));
    assert(fixture_state->refs[1] == 2);
}
static void native_change(fixture *fixture_state, int value) {
    int old = fixture_state->value[PZ_LIVE];
    if (old) {
        --fixture_state->refs[old];
    }
    fixture_state->value[PZ_LIVE] = value;
    if (value) {
        ++fixture_state->refs[value];
    }
}
static void normal(void) {
    fixture fixture_state;
    pz_ledger ledger;
    pcb_slot slot = {0};
    setup(&fixture_state, &ledger, &slot);
    native_change(&fixture_state, 2);
    assert(pz_enter(&ledger, &slot) == PCB_ZONE_ACTIVE);
    assert(fixture_state.value[PZ_LIVE] == 1 && fixture_state.refs[1] == 2 && fixture_state.refs[2] == 1);
    assert(pz_enter(&ledger, &slot) == PCB_ZONE_UNCERTAIN);
    native_change(&fixture_state, 3);
    assert(pz_restore(&ledger, &slot));
    assert(fixture_state.value[PZ_LIVE] == 2 && fixture_state.value[PZ_SLOT] == 3);
    assert(fixture_state.refs[1] == 0 && fixture_state.refs[2] == 1 && fixture_state.refs[3] == 1);
    assert(!pz_restore(&ledger, &slot));
    assert(pz_destroy(&ledger));
    assert(fixture_state.refs[3] == 0 && fixture_state.refs[2] == 1 && !pz_destroy(&ledger));
}
static void failures(void) {
    fixture fixture_state;
    pz_ledger ledger;
    pcb_slot slot = {0}, other = {0};
    setup(&fixture_state, &ledger, &slot);
    fixture_state.fail_read = 1;
    assert(pz_enter(&ledger, &slot) == PCB_ZONE_INACTIVE);
    fixture_state.result = PZ_NO_EFFECT;
    assert(pz_enter(&ledger, &slot) == PCB_ZONE_INACTIVE);
    assert(pz_enter(&ledger, &other) == PCB_ZONE_INACTIVE);
    assert(pz_enter(&ledger, &slot) == PCB_ZONE_ACTIVE);
    native_change(&fixture_state, 2);
    fixture_state.fail_read = 1;
    assert(!pz_restore(&ledger, &slot) && ledger.phase == PZ_ACTIVE && fixture_state.value[PZ_SLOT] == 1);
    fixture_state.result = PZ_NO_EFFECT;
    assert(!pz_restore(&ledger, &slot) && ledger.phase == PZ_ACTIVE);
    fixture_state.thread = 8;
    int calls = fixture_state.calls;
    assert(!pz_restore(&ledger, &slot));
    assert(!pz_destroy(&ledger) && fixture_state.calls == calls);
    fixture_state.thread = 9;
    fixture_state.boundary = 0;
    assert(!pz_restore(&ledger, &slot) && fixture_state.calls == calls);
    fixture_state.boundary = 1;
    assert(pz_restore(&ledger, &slot));
    fixture_state.result = PZ_NO_EFFECT;
    assert(!pz_destroy(&ledger) && ledger.phase == PZ_DESTROYING && fixture_state.refs[2] == 1);
    assert(pz_destroy(&ledger) && fixture_state.refs[2] == 0);
}
static void unknown(void) {
    fixture fixture_state;
    pz_ledger ledger;
    pcb_slot slot = {0};
    setup(&fixture_state, &ledger, &slot);
    fixture_state.result = PZ_UNKNOWN;
    assert(pz_enter(&ledger, &slot) == PCB_ZONE_UNCERTAIN);
    assert(ledger.phase == PZ_POISONED && ledger.unknown_destination == PZ_CANONICAL);
    int calls = fixture_state.calls;
    assert(!pz_restore(&ledger, &slot));
    assert(!pz_destroy(&ledger));
    assert(pz_enter(&ledger, &slot) == PCB_ZONE_UNCERTAIN && fixture_state.calls == calls);
    setup(&fixture_state, &ledger, &slot);
    assert(pz_enter(&ledger, &slot) == PCB_ZONE_ACTIVE);
    fixture_state.result = PZ_UNKNOWN;
    assert(!pz_restore(&ledger, &slot));
    assert(ledger.phase == PZ_POISONED && ledger.unknown_destination == PZ_SLOT);
    calls = fixture_state.calls;
    assert(!pz_restore(&ledger, &slot) && !pz_destroy(&ledger) && calls == fixture_state.calls);
}
static void every_step(void) {
    /* The five assignments after initialization: preserve, enter, capture,
     * restore, release. A refused operation is retryable; unknown is not. */
    for (int step = 1; step <= 5; ++step) {
        for (int effect = PZ_NO_EFFECT; effect <= PZ_UNKNOWN; effect += 2) {
            fixture fixture_state;
            pz_ledger ledger;
            pcb_slot slot = {0};
            setup(&fixture_state, &ledger, &slot);
            fixture_state.fail_at = fixture_state.calls + step;
            fixture_state.fail_effect = effect;
            pcb_zone_result entered = pz_enter(&ledger, &slot);
            if (step == 1 && effect == PZ_NO_EFFECT) {
                assert(entered == PCB_ZONE_INACTIVE && ledger.phase == PZ_IDLE);
                assert(pz_destroy(&ledger));
                continue;
            }
            if (effect == PZ_UNKNOWN && step <= 2) {
                assert(entered == PCB_ZONE_UNCERTAIN && ledger.phase == PZ_POISONED);
            } else {
                assert(entered == (step == 2 ? PCB_ZONE_UNCERTAIN : PCB_ZONE_ACTIVE));
                if (step >= 3) {
                    assert(!pz_restore(&ledger, &slot));
                }
                if (effect == PZ_NO_EFFECT) {
                    assert(pz_restore(&ledger, &slot));
                    assert(pz_destroy(&ledger));
                    continue;
                }
                assert(ledger.phase == PZ_POISONED);
            }
            int calls = fixture_state.calls;
            assert(!pz_restore(&ledger, &slot) && !pz_destroy(&ledger) && fixture_state.calls == calls);
        }
    }
}
static void lifecycle_unknown(void) {
    fixture fixture_state;
    pz_ledger ledger = {0};
    pcb_slot slot = {0};
    memset(&fixture_state, 0, sizeof(fixture_state));
    fixture_state.thread = 9;
    fixture_state.boundary = 1;
    fixture_state.value[PZ_LIVE] = 1;
    fixture_state.refs[1] = 1;
    fixture_state.result = PZ_UNKNOWN;
    pz_operations operations = {&fixture_state, thread, boundary, inspect, assign};
    assert(pz_initialize(&ledger, &slot, &operations, 9));
    assert(!pz_initialize_slot(&ledger));
    assert(ledger.phase == PZ_POISONED && ledger.unknown_destination == PZ_SLOT);
    int calls = fixture_state.calls;
    assert(!pz_initialize_slot(&ledger) && !pz_destroy(&ledger) && fixture_state.calls == calls);
    setup(&fixture_state, &ledger, &slot);
    fixture_state.result = PZ_UNKNOWN;
    assert(!pz_destroy(&ledger) && ledger.phase == PZ_POISONED);
    calls = fixture_state.calls;
    assert(!pz_destroy(&ledger) && fixture_state.calls == calls);
}
static void boundary_changes_during_operations(void) {
    for (int wrong_thread = 0; wrong_thread <= 1; ++wrong_thread) {
        fixture fixture_state;
        pz_ledger ledger;
        pcb_slot slot = {0};
        setup(&fixture_state, &ledger, &slot);
        fixture_state.invalidate_thread = wrong_thread;
        fixture_state.invalidate_after_assign = fixture_state.calls + 1;
        assert(pz_enter(&ledger, &slot) == PCB_ZONE_UNCERTAIN);
        assert(ledger.phase == PZ_CANON_HELD);
        assert(fixture_state.calls == fixture_state.invalidate_after_assign);
        assert(fixture_state.value[PZ_LIVE] == 1 && fixture_state.refs[1] == 3);
        assert(!pz_restore(&ledger, &slot));
        fixture_state.thread = 9;
        fixture_state.boundary = 1;
        assert(pz_restore(&ledger, &slot));
        assert(pz_destroy(&ledger));
    }
    for (int read_number = 1; read_number <= 2; ++read_number) {
        fixture fixture_state;
        pz_ledger ledger;
        pcb_slot slot = {0};
        setup(&fixture_state, &ledger, &slot);
        fixture_state.invalidate_after_read = fixture_state.reads + read_number;
        int calls = fixture_state.calls;
        int reads = fixture_state.reads;
        assert(pz_enter(&ledger, &slot) == PCB_ZONE_INACTIVE);
        assert(ledger.phase == PZ_IDLE && fixture_state.calls == calls);
        assert(fixture_state.reads == reads + read_number);
        fixture_state.boundary = 1;
        assert(pz_destroy(&ledger));
    }
}
int main(void) {
    normal();
    failures();
    unknown();
    every_step();
    lifecycle_unknown();
    boundary_changes_during_operations();
    puts("6 scenario groups passed");
}
