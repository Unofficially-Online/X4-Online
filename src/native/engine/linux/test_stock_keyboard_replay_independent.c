#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "stock_keyboard_replay.h"

typedef struct {
    keyboard_replay *replay;
    unsigned calls;
    unsigned prepared;
    unsigned prepare_fail;
    unsigned prepare_reenter;
    unsigned expect_prepared;
    unsigned fail_slot;
    unsigned reenter;
    unsigned stop;
    uint64_t tick[2];
    unsigned slot[2];
    uint64_t saved_ship[2];
    stock_input_record records[2];
} fixture_calls;

static int apply_slot(void *context, uint64_t tick, unsigned slot, uint64_t ship,
                      const stock_input_record *record) {
    fixture_calls *calls = context;
    assert(calls->calls < 2);
    assert(!calls->expect_prepared || calls->prepared == 1);
    unsigned index = calls->calls++;
    calls->tick[index] = tick;
    calls->slot[index] = slot;
    calls->saved_ship[index] = ship;
    calls->records[index] = *record;
    if (slot == 1 && calls->reenter) {
        assert(keyboard_replay_publish_slots(calls->replay, tick, apply_slot, calls) == KEYBOARD_REPLAY_PHASE);
    }
    if (slot == 1 && calls->stop) {
        keyboard_replay_stop(calls->replay);
        assert(!keyboard_replay_destroy(calls->replay));
    }
    return slot != calls->fail_slot;
}

static int prepare_slots(void *context, uint64_t tick, const uint64_t saved[2],
                         const stock_input_record records[2]) {
    fixture_calls *calls = context;
    assert(!calls->calls && !calls->prepared && tick == 5);
    assert(saved[0] == 111 && saved[1] == 222);
    assert(records[0].capture16 == 3 && records[1].capture16 == 3);
    ++calls->prepared;
    if (calls->prepare_reenter)
        assert(keyboard_replay_publish_slots(calls->replay, tick, apply_slot, calls) == KEYBOARD_REPLAY_PHASE);
    return !calls->prepare_fail;
}

static int apply_event(void *context, unsigned id, unsigned char state) {
    (void)context;
    (void)id;
    (void)state;
    return 1;
}

static void setup(keyboard_replay *replay, unsigned own_slot, uint64_t first_tick) {
    keyboard_replay_config config = {
        .own_slot = own_slot,
        .delay = 2,
        .first_capture = first_tick,
        .last_capture = first_tick + 1,
        .saved_ship_ids = {111, 222},
        .independent_players = 1,
    };
    assert(keyboard_replay_init(replay));
    assert(keyboard_replay_configure(replay, &config) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_begin(replay, first_tick) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_publish(replay, first_tick, apply_event, NULL) == KEYBOARD_REPLAY_PHASE);
    assert(keyboard_replay_publish_slots(replay, first_tick, NULL, NULL) == KEYBOARD_REPLAY_NOT_DUE);
}

static unsigned make_record(unsigned char *bytes, uint64_t tick, unsigned id, unsigned count) {
    stock_input_record record = {0};
    record.capture16 = (uint16_t)tick;
    record.count = count;
    for (unsigned index = 0; index < count; ++index) {
        record.events[index] = (stock_input_event){STOCK_INPUT_STATE, (uint16_t)id, index & 1u};
    }
    unsigned size = stock_input_encode(&record, bytes, STOCK_INPUT_MAX_BYTES);
    assert(size);
    return size;
}

static void close_tick(keyboard_replay *replay, uint64_t first_tick, unsigned own_slot,
                       const unsigned char *foreign, unsigned foreign_length) {
    unsigned char local[STOCK_INPUT_MAX_BYTES];
    unsigned local_length = 0;
    assert(keyboard_replay_seal(replay, local, sizeof local, &local_length) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_stage(replay, own_slot, local, local_length) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_stage(replay, own_slot == 1 ? 2 : 1, foreign, foreign_length) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_begin(replay, first_tick + 1) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_publish_slots(replay, first_tick + 1, NULL, NULL) == KEYBOARD_REPLAY_NOT_DUE);
    assert(keyboard_replay_seal(replay, local, sizeof local, &local_length) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_begin(replay, first_tick + 2) == KEYBOARD_REPLAY_OK);
}

static void finish(keyboard_replay *replay) {
    keyboard_replay_stop(replay);
    assert(keyboard_replay_destroy(replay));
}

static void test_slot_two_capture_and_full_width_tick(void) {
    for (uint64_t capture = 65535; capture <= 65536; ++capture) {
        keyboard_replay replay;
        setup(&replay, 2, capture);
        assert(keyboard_replay_consume(&replay, STOCK_INPUT_STATE, 3, 1) == KEYBOARD_REPLAY_OK);
        assert(keyboard_replay_consume(&replay, STOCK_INPUT_STATE, 3, 0) == KEYBOARD_REPLAY_OK);
        unsigned char foreign[STOCK_INPUT_MAX_BYTES];
        unsigned length = make_record(foreign, capture, 5, 1);
        close_tick(&replay, capture, 2, foreign, length);
        fixture_calls calls = {.replay = &replay};
        assert(keyboard_replay_publish_slots(&replay, capture + 3, apply_slot, &calls) == KEYBOARD_REPLAY_PHASE);
        assert(calls.calls == 0);
        assert(keyboard_replay_publish_slots(&replay, capture + 2, apply_slot, &calls) == KEYBOARD_REPLAY_OK);
        assert(calls.calls == 2 && calls.slot[0] == 1 && calls.slot[1] == 2);
        assert(calls.tick[0] == capture + 2 && calls.tick[1] == capture + 2);
        assert(calls.saved_ship[0] == 111 && calls.saved_ship[1] == 222);
        assert(calls.records[0].capture16 == (uint16_t)capture &&
               calls.records[1].capture16 == (uint16_t)capture);
        assert(calls.records[0].count == 1 && calls.records[0].events[0].id == 5);
        assert(calls.records[1].count == 2 && calls.records[1].events[0].id == 3 &&
               calls.records[1].events[0].value == 1 && calls.records[1].events[1].value == 0);
        assert(keyboard_replay_publish_slots(&replay, capture + 2, apply_slot, &calls) == KEYBOARD_REPLAY_DUPLICATE);
        assert(calls.calls == 2);
        finish(&replay);
    }
}

static void test_empty_records_and_wrong_mode(void) {
    keyboard_replay replay;
    setup(&replay, 1, 3);
    unsigned char foreign[STOCK_INPUT_MAX_BYTES];
    unsigned length = make_record(foreign, 3, 2, 0);
    close_tick(&replay, 3, 1, foreign, length);
    fixture_calls calls = {.replay = &replay};
    assert(keyboard_replay_publish(&replay, 5, apply_event, NULL) == KEYBOARD_REPLAY_PHASE);
    assert(keyboard_replay_publish_slots(&replay, 5, apply_slot, &calls) == KEYBOARD_REPLAY_OK);
    assert(calls.calls == 2 && calls.records[0].count == 0 && calls.records[1].count == 0);
    finish(&replay);

    setup(&replay, 1, 3);
    assert(keyboard_replay_consume(&replay, STOCK_INPUT_STATE, 2, 1) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_consume(&replay, STOCK_INPUT_STATE, 5, 0) == KEYBOARD_REPLAY_OK);
    length = make_record(foreign, 3, 4, 1);
    close_tick(&replay, 3, 1, foreign, length);
    memset(&calls, 0, sizeof calls);
    calls.replay = &replay;
    assert(keyboard_replay_publish_slots(&replay, 5, apply_slot, &calls) == KEYBOARD_REPLAY_OK);
    assert(calls.calls == 2 && calls.records[0].count == 2 && calls.records[0].events[0].id == 2 &&
           calls.records[0].events[0].value == 1 && calls.records[0].events[1].id == 5 &&
           calls.records[0].events[1].value == 0 && calls.records[1].events[0].id == 4);
    finish(&replay);

    assert(keyboard_replay_init(&replay));
    keyboard_replay_config config = {.own_slot = 1, .delay = 2, .first_capture = 3,
                                     .last_capture = 3, .saved_ship_ids = {111, 222},
                                     .independent_players = 2};
    assert(keyboard_replay_configure(&replay, &config) == KEYBOARD_REPLAY_ARGUMENT);
    config.independent_players = 0;
    assert(keyboard_replay_configure(&replay, &config) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_begin(&replay, 3) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_publish_slots(&replay, 3, apply_slot, &calls) == KEYBOARD_REPLAY_PHASE);
    assert(keyboard_replay_publish(&replay, 3, apply_event, NULL) == KEYBOARD_REPLAY_NOT_DUE);
    finish(&replay);
}

static void test_invalid_second_record_before_effect(void) {
    for (unsigned scenario = 0; scenario < 4; ++scenario) {
        keyboard_replay replay;
        setup(&replay, 1, 3);
        assert(keyboard_replay_consume(&replay, STOCK_INPUT_STATE, 2, 1) == KEYBOARD_REPLAY_OK);
        unsigned char foreign[STOCK_INPUT_MAX_BYTES];
        unsigned length = make_record(foreign, 3, 3, 1);
        close_tick(&replay, 3, 1, foreign, length);
        /* Inject faults after staging to exercise the coordinator's own
         * second-record checks as well as the queue's wire admission. */
        unsigned char *bytes = replay.queue.pending[5 % LINUX_STOCK_RING].bytes[1];
        if (scenario == 0) {
            bytes[1] = STOCK_INPUT_ACTIVE;
        } else if (scenario == 1) {
            bytes[2] ^= 1; /* capture16 no longer matches the full capture tick */
        } else if (scenario == 2) {
            bytes[43] = 10; /* unsupported STATE10 after queue validation */
        } else {
            bytes[45] = 2; /* invalid boolean payload after queue validation */
        }
        fixture_calls calls = {.replay = &replay};
        assert(keyboard_replay_publish_slots(&replay, 5, apply_slot, &calls) == KEYBOARD_REPLAY_INVALID_FRAME);
        assert(calls.calls == 0);
        assert(keyboard_replay_publish_slots(&replay, 5, apply_slot, &calls) == KEYBOARD_REPLAY_INVALID_FRAME);
        finish(&replay);
    }
}

static void test_missing_second_record_waits_without_effects(void) {
    keyboard_replay replay;
    setup(&replay, 1, 3);
    unsigned char local[STOCK_INPUT_MAX_BYTES];
    unsigned local_length = 0;
    assert(keyboard_replay_seal(&replay, local, sizeof local, &local_length) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_stage(&replay, 1, local, local_length) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_begin(&replay, 4) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_publish_slots(&replay, 4, NULL, NULL) == KEYBOARD_REPLAY_NOT_DUE);
    assert(keyboard_replay_seal(&replay, local, sizeof local, &local_length) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_begin(&replay, 5) == KEYBOARD_REPLAY_OK);
    fixture_calls calls = {.replay = &replay};
    assert(keyboard_replay_publish_slots(&replay, 5, apply_slot, &calls) == KEYBOARD_REPLAY_WAITING);
    assert(calls.calls == 0);
    unsigned char foreign[STOCK_INPUT_MAX_BYTES];
    unsigned length = make_record(foreign, 3, 4, 1);
    assert(keyboard_replay_stage(&replay, 2, foreign, length) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_publish_slots(&replay, 5, apply_slot, &calls) == KEYBOARD_REPLAY_OK);
    assert(calls.calls == 2 && calls.records[0].count == 0 && calls.records[1].events[0].id == 4);
    finish(&replay);
}

static void test_identity_and_second_slot_admission(void) {
    keyboard_replay replay;
    setup(&replay, 1, 3);
    unsigned char foreign[STOCK_INPUT_MAX_BYTES];
    unsigned length = make_record(foreign, 3, 2, 0);
    close_tick(&replay, 3, 1, foreign, length);
    /* Internal fault injection proves the copied frame's identity is checked
     * before the first callback, even after staging succeeded. */
    replay.config.saved_ship_ids[1] = 333;
    fixture_calls calls = {.replay = &replay};
    assert(keyboard_replay_publish_slots(&replay, 5, apply_slot, &calls) == KEYBOARD_REPLAY_INVALID_FRAME);
    assert(calls.calls == 0);
    finish(&replay);

    setup(&replay, 1, 3);
    unsigned char local[STOCK_INPUT_MAX_BYTES];
    unsigned local_length = 0;
    assert(keyboard_replay_seal(&replay, local, sizeof local, &local_length) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_stage(&replay, 1, local, local_length) == KEYBOARD_REPLAY_OK);
    length = make_record(foreign, 3, 10, 1);
    assert(keyboard_replay_stage(&replay, 2, foreign, length) == KEYBOARD_REPLAY_QUEUE_FAILURE);
    assert(keyboard_replay_publish_slots(&replay, 3, apply_slot, &calls) == KEYBOARD_REPLAY_QUEUE_FAILURE);
    assert(calls.calls == 0);
    finish(&replay);
}

static void test_failure_reentry_and_stop(void) {
    for (unsigned scenario = 0; scenario < 5; ++scenario) {
        keyboard_replay replay;
        setup(&replay, 1, 3);
        unsigned char foreign[STOCK_INPUT_MAX_BYTES];
        unsigned length = make_record(foreign, 3, 2, 0);
        close_tick(&replay, 3, 1, foreign, length);
        fixture_calls calls = {.replay = &replay};
        if (scenario == 0) {
            calls.fail_slot = 1;
        } else if (scenario == 1) {
            calls.fail_slot = 2;
        } else if (scenario == 2) {
            calls.reenter = 1;
        } else {
            calls.stop = 1;
            if (scenario == 4) {
                calls.fail_slot = 1;
            }
        }
        keyboard_replay_result result = keyboard_replay_publish_slots(&replay, 5, apply_slot, &calls);
        assert(result == (scenario >= 3 ? KEYBOARD_REPLAY_STOPPED : KEYBOARD_REPLAY_CALLBACK_FAILURE));
        assert(calls.calls == (scenario == 1 ? 2u : 1u));
        keyboard_replay_status status;
        assert(keyboard_replay_get_status(&replay, &status) &&
               status.failure == KEYBOARD_REPLAY_CALLBACK_FAILURE);
        assert(keyboard_replay_publish_slots(&replay, 5, apply_slot, &calls) ==
               (scenario >= 3 ? KEYBOARD_REPLAY_STOPPED : KEYBOARD_REPLAY_CALLBACK_FAILURE));
        assert(calls.calls == (scenario == 1 ? 2u : 1u));
        finish(&replay);
    }
}

static void test_wrong_echo_and_unsupported(void) {
    keyboard_replay replay;
    setup(&replay, 2, 3);
    assert(keyboard_replay_consume(&replay, STOCK_INPUT_STATE, 4, 1) == KEYBOARD_REPLAY_OK);
    unsigned char local[STOCK_INPUT_MAX_BYTES], wrong[STOCK_INPUT_MAX_BYTES];
    unsigned local_length = 0;
    assert(keyboard_replay_seal(&replay, local, sizeof local, &local_length) == KEYBOARD_REPLAY_OK);
    unsigned wrong_length = make_record(wrong, 3, 2, 0);
    assert(keyboard_replay_stage(&replay, 2, wrong, wrong_length) == KEYBOARD_REPLAY_QUEUE_FAILURE);
    fixture_calls calls = {.replay = &replay};
    assert(keyboard_replay_publish_slots(&replay, 3, apply_slot, &calls) == KEYBOARD_REPLAY_QUEUE_FAILURE);
    assert(calls.calls == 0);
    finish(&replay);

    setup(&replay, 1, 3);
    assert(keyboard_replay_consume(&replay, STOCK_INPUT_STATE, 10, 1) == KEYBOARD_REPLAY_UNSUPPORTED);
    assert(keyboard_replay_publish_slots(&replay, 3, apply_slot, &calls) == KEYBOARD_REPLAY_UNSUPPORTED);
    assert(calls.calls == 0);
    finish(&replay);
}

static void test_throttle_policy(void) {
    keyboard_replay replay;
    setup(&replay, 1, 3);
    assert(keyboard_replay_consume(&replay, STOCK_INPUT_STATE, 13, 1) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_consume(&replay, STOCK_INPUT_STATE, 14, 0) == KEYBOARD_REPLAY_OK);
    unsigned char foreign[STOCK_INPUT_MAX_BYTES];
    unsigned length = make_record(foreign, 3, 14, 1);
    close_tick(&replay, 3, 1, foreign, length);
    fixture_calls calls = {.replay = &replay};
    assert(keyboard_replay_publish_slots(&replay, 5, apply_slot, &calls) == KEYBOARD_REPLAY_OK);
    assert(calls.calls == 2 && calls.records[0].events[0].id == 13 &&
           calls.records[0].events[1].id == 14 && calls.records[1].events[0].id == 14);
    finish(&replay);

    for (unsigned id = 10; id <= 15; ++id) {
        if (id == 13 || id == 14) continue;
        setup(&replay, 1, 3);
        assert(keyboard_replay_consume(&replay, STOCK_INPUT_STATE, id, 1) == KEYBOARD_REPLAY_UNSUPPORTED);
        finish(&replay);
    }
    keyboard_replay_config config = {.own_slot=1, .delay=2, .first_capture=3, .last_capture=4,
        .saved_ship_ids={111,222}, .independent_players=1, .scripted_throttle=1};
    assert(keyboard_replay_init(&replay));
    assert(keyboard_replay_configure(&replay, &config) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_begin(&replay, 3) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_consume(&replay, STOCK_INPUT_STATE, 13, 0) == KEYBOARD_REPLAY_UNSUPPORTED);
    finish(&replay);

    assert(keyboard_replay_init(&replay));
    assert(keyboard_replay_configure(&replay, &config) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_begin(&replay, 3) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_publish_slots(&replay, 3, NULL, NULL) == KEYBOARD_REPLAY_NOT_DUE);
    assert(keyboard_replay_consume(&replay, STOCK_INPUT_STATE, 2, 1) == KEYBOARD_REPLAY_OK);
    unsigned char local[STOCK_INPUT_MAX_BYTES];
    unsigned local_length = 0;
    assert(keyboard_replay_seal(&replay, local, sizeof local, &local_length) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_stage(&replay, 1, local, local_length) == KEYBOARD_REPLAY_OK);
    length = make_record(foreign, 3, 14, 1);
    assert(keyboard_replay_stage(&replay, 2, foreign, length) == KEYBOARD_REPLAY_QUEUE_FAILURE);
    calls = (fixture_calls){.replay = &replay};
    assert(keyboard_replay_publish_slots(&replay, 5, apply_slot, &calls) == KEYBOARD_REPLAY_QUEUE_FAILURE);
    assert(calls.calls == 0);
    finish(&replay);
}

static void test_strafe_capture_order(void) {
    keyboard_replay replay;
    setup(&replay, 1, 3);
    for (unsigned state = 1; state <= 2; ++state) {
        for (unsigned id = 6; id <= 9; ++id) {
            assert(keyboard_replay_consume(&replay, STOCK_INPUT_STATE, id, state == 1) == KEYBOARD_REPLAY_OK);
        }
    }
    unsigned char foreign[STOCK_INPUT_MAX_BYTES];
    unsigned length = make_record(foreign, 3, 9, 2);
    close_tick(&replay, 3, 1, foreign, length);
    fixture_calls calls = {.replay = &replay};
    assert(keyboard_replay_publish_slots(&replay, 5, apply_slot, &calls) == KEYBOARD_REPLAY_OK);
    assert(calls.calls == 2 && calls.records[0].count == 8 && calls.records[1].count == 2);
    for (unsigned index = 0; index < 8; ++index) {
        assert(calls.records[0].events[index].id == 6 + index % 4);
        assert(calls.records[0].events[index].value == (index < 4));
    }
    finish(&replay);
    for (unsigned invalid = 0; invalid < 3; ++invalid) {
        setup(&replay, 1, 3);
        assert(keyboard_replay_consume(&replay, STOCK_INPUT_STATE,
            invalid == 0 ? 10 : invalid == 1 ? 65535 : 6, invalid == 2 ? 2 : 1) ==
            KEYBOARD_REPLAY_UNSUPPORTED);
        finish(&replay);
    }
}

static void test_prepared_slots(void) {
    for (unsigned failure = 0; failure < 3; ++failure) {
        keyboard_replay replay;
        setup(&replay, 1, 3);
        unsigned char foreign[STOCK_INPUT_MAX_BYTES];
        unsigned length = make_record(foreign, 3, 2, 0);
        close_tick(&replay, 3, 1, foreign, length);
        fixture_calls calls = {.replay = &replay, .expect_prepared = 1,
                               .prepare_fail = failure == 1, .prepare_reenter = failure == 2};
        keyboard_replay_result result = keyboard_replay_publish_slots_prepared(
            &replay, 5, prepare_slots, apply_slot, &calls);
        assert(calls.prepared == 1);
        if (!failure) assert(result == KEYBOARD_REPLAY_OK && calls.calls == 2);
        else {
            assert(result == KEYBOARD_REPLAY_CALLBACK_FAILURE && calls.calls == 0);
            assert(keyboard_replay_publish_slots_prepared(&replay, 5, prepare_slots, apply_slot, &calls) ==
                   KEYBOARD_REPLAY_CALLBACK_FAILURE);
        }
        finish(&replay);
    }
}

int main(void) {
    test_prepared_slots();
    test_throttle_policy();
    test_strafe_capture_order();
    test_slot_two_capture_and_full_width_tick();
    test_empty_records_and_wrong_mode();
    test_invalid_second_record_before_effect();
    test_missing_second_record_waits_without_effects();
    test_identity_and_second_slot_admission();
    test_failure_reentry_and_stop();
    test_wrong_echo_and_unsupported();
    puts("independent keyboard replay: atomic validation, slot order and no retry PASS");
}
