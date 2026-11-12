#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "stock_keyboard_replay.h"

typedef struct {
    unsigned count;
    unsigned fail_at;
    unsigned ids[40];
    unsigned char states[40];
    keyboard_replay *reenter;
} replay_calls;
static int record_call(void *context, unsigned id, unsigned char state) {
    replay_calls *calls = context;
    calls->ids[calls->count] = id;
    calls->states[calls->count] = state;
    ++calls->count;
    if (calls->reenter) {
        assert(keyboard_replay_publish(calls->reenter, 5, record_call, calls) == KEYBOARD_REPLAY_PHASE);
    }
    return calls->count != calls->fail_at;
}
static void setup(keyboard_replay *replay, unsigned own_slot) {
    assert(keyboard_replay_init(replay));
    keyboard_replay_config config = {own_slot, 2, 0, 3, 5, {111, 222}, 0, 0};
    assert(keyboard_replay_configure(replay, &config) == KEYBOARD_REPLAY_OK);
}
static unsigned encode_row(unsigned char *out, unsigned capture, unsigned count) {
    stock_input_record record = {0};
    record.capture16 = (uint16_t)capture;
    record.count = count;
    for (unsigned i = 0; i < count; ++i) {
        record.events[i] = (stock_input_event){STOCK_INPUT_STATE, 2, i & 1u};
    }
    return stock_input_encode(&record, out, STOCK_INPUT_MAX_BYTES);
}
static void seal(keyboard_replay *replay, unsigned char *out, unsigned *length) {
    assert(keyboard_replay_seal(replay, out, STOCK_INPUT_MAX_BYTES, length) == KEYBOARD_REPLAY_OK);
}
static void start_frame_three(keyboard_replay *replay) {
    assert(keyboard_replay_begin(replay, 3) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_publish(replay, 3, record_call, NULL) == KEYBOARD_REPLAY_NOT_DUE);
}
static void stage_and_advance(keyboard_replay *replay, const unsigned char *first, unsigned first_len,
                              const unsigned char *second, unsigned second_len) {
    assert(keyboard_replay_stage(replay, 1, first, first_len) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_stage(replay, 2, second, second_len) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_begin(replay, 4) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_publish(replay, 4, record_call, NULL) == KEYBOARD_REPLAY_NOT_DUE);
    unsigned char capture[STOCK_INPUT_MAX_BYTES];
    unsigned length = 0;
    seal(replay, capture, &length);
    assert(keyboard_replay_begin(replay, 5) == KEYBOARD_REPLAY_OK);
}
static void test_order_and_dedup(void) {
    keyboard_replay replay;
    setup(&replay, 1);
    start_frame_three(&replay);
    assert(keyboard_replay_consume(&replay, STOCK_INPUT_STATE, 2, 1) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_consume(&replay, STOCK_INPUT_STATE, 2, 1) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_consume(&replay, STOCK_INPUT_STATE, 2, 0) == KEYBOARD_REPLAY_OK);
    unsigned char local[STOCK_INPUT_MAX_BYTES], neutral[STOCK_INPUT_MAX_BYTES];
    unsigned local_len = 0;
    seal(&replay, local, &local_len);
    unsigned repeat_len = 0;
    unsigned char repeat[STOCK_INPUT_MAX_BYTES];
    assert(keyboard_replay_seal(&replay, repeat, sizeof(repeat), &repeat_len) == KEYBOARD_REPLAY_DUPLICATE);
    assert(local_len == repeat_len && !memcmp(local, repeat, local_len));
    unsigned neutral_len = encode_row(neutral, 3, 0);
    stage_and_advance(&replay, local, local_len, neutral, neutral_len);
    assert(keyboard_replay_begin(&replay, 6) == KEYBOARD_REPLAY_PHASE);
    replay_calls calls = {.reenter = &replay};
    assert(keyboard_replay_publish(&replay, 5, record_call, &calls) == KEYBOARD_REPLAY_OK);
    assert(calls.count == 3 && calls.ids[0] == 2 && calls.states[0] == 1 &&
           calls.states[1] == 1 && calls.states[2] == 0);
    assert(keyboard_replay_publish(&replay, 5, record_call, &calls) == KEYBOARD_REPLAY_DUPLICATE);
    assert(calls.count == 3);
    keyboard_replay_stop(&replay);
    assert(keyboard_replay_consume(&replay, STOCK_INPUT_STATE, 2, 0) == KEYBOARD_REPLAY_STOPPED);
    assert(keyboard_replay_destroy(&replay));
}
static void test_second_slot_neutral(void) {
    keyboard_replay replay;
    setup(&replay, 2);
    start_frame_three(&replay);
    assert(keyboard_replay_consume(&replay, STOCK_INPUT_STATE, 3, 1) == KEYBOARD_REPLAY_OK);
    unsigned char local[STOCK_INPUT_MAX_BYTES], first[STOCK_INPUT_MAX_BYTES];
    unsigned local_len = 0;
    seal(&replay, local, &local_len);
    stock_input_record decoded;
    assert(stock_input_decode(local, local_len, &decoded) && !decoded.flags && !decoded.count);
    unsigned first_len = encode_row(first, 3, 2);
    stage_and_advance(&replay, first, first_len, local, local_len);
    replay_calls calls = {0};
    assert(keyboard_replay_publish(&replay, 5, record_call, &calls) == KEYBOARD_REPLAY_OK);
    assert(calls.count == 2 && calls.states[0] == 0 && calls.states[1] == 1);
    keyboard_replay_stop(&replay);
    assert(keyboard_replay_destroy(&replay));
}
static void test_invalid_neutral_before_effect(void) {
    keyboard_replay replay;
    setup(&replay, 1);
    start_frame_three(&replay);
    assert(keyboard_replay_consume(&replay, STOCK_INPUT_STATE, 2, 1) == KEYBOARD_REPLAY_OK);
    unsigned char local[STOCK_INPUT_MAX_BYTES], foreign[STOCK_INPUT_MAX_BYTES];
    unsigned local_len = 0;
    seal(&replay, local, &local_len);
    unsigned foreign_len = encode_row(foreign, 3, 1);
    stage_and_advance(&replay, local, local_len, foreign, foreign_len);
    replay_calls calls = {0};
    assert(keyboard_replay_publish(&replay, 5, record_call, &calls) == KEYBOARD_REPLAY_INVALID_FRAME);
    assert(!calls.count);
    assert(keyboard_replay_publish(&replay, 5, record_call, &calls) == KEYBOARD_REPLAY_INVALID_FRAME);
    keyboard_replay_stop(&replay);
    assert(keyboard_replay_destroy(&replay));
}
static void test_callback_failure(void) {
    keyboard_replay replay;
    setup(&replay, 1);
    start_frame_three(&replay);
    assert(keyboard_replay_consume(&replay, STOCK_INPUT_STATE, 2, 1) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_consume(&replay, STOCK_INPUT_STATE, 3, 0) == KEYBOARD_REPLAY_OK);
    unsigned char local[STOCK_INPUT_MAX_BYTES], neutral[STOCK_INPUT_MAX_BYTES];
    unsigned local_len = 0;
    seal(&replay, local, &local_len);
    unsigned neutral_len = encode_row(neutral, 3, 0);
    stage_and_advance(&replay, local, local_len, neutral, neutral_len);
    replay_calls calls = {.fail_at = 2};
    assert(keyboard_replay_publish(&replay, 5, record_call, &calls) == KEYBOARD_REPLAY_CALLBACK_FAILURE);
    assert(calls.count == 2);
    assert(keyboard_replay_publish(&replay, 5, record_call, &calls) == KEYBOARD_REPLAY_CALLBACK_FAILURE);
    assert(keyboard_replay_begin(&replay, 6) == KEYBOARD_REPLAY_CALLBACK_FAILURE);
    assert(calls.count == 2);
    keyboard_replay_stop(&replay);
    assert(keyboard_replay_destroy(&replay));
}
static void test_unsupported_and_overflow(void) {
    for (unsigned own_slot = 1; own_slot <= 2; ++own_slot) {
        keyboard_replay replay;
        setup(&replay, own_slot);
        start_frame_three(&replay);
        assert(keyboard_replay_consume(&replay, STOCK_INPUT_STATE, 6, 1) == KEYBOARD_REPLAY_UNSUPPORTED);
        assert(keyboard_replay_consume(&replay, STOCK_INPUT_STATE, 2, 1) == KEYBOARD_REPLAY_UNSUPPORTED);
        keyboard_replay_stop(&replay);
        assert(keyboard_replay_destroy(&replay));
    }
    keyboard_replay replay;
    setup(&replay, 1);
    start_frame_three(&replay);
    for (unsigned i = 0; i < STOCK_INPUT_MAX_EVENTS; ++i) {
        assert(keyboard_replay_consume(&replay, STOCK_INPUT_STATE, 2, 1) == KEYBOARD_REPLAY_OK);
    }
    assert(keyboard_replay_consume(&replay, STOCK_INPUT_STATE, 2, 0) == KEYBOARD_REPLAY_QUEUE_FAILURE);
    keyboard_replay_status status;
    assert(keyboard_replay_get_status(&replay, &status));
    assert(status.queue_result == LINUX_STOCK_OVERFLOW);
    keyboard_replay_stop(&replay);
    assert(keyboard_replay_destroy(&replay));
}
static void test_missing_row_and_retry(void) {
    keyboard_replay replay;
    setup(&replay, 1);
    start_frame_three(&replay);
    unsigned char local[STOCK_INPUT_MAX_BYTES];
    unsigned char neutral[STOCK_INPUT_MAX_BYTES];
    unsigned local_len = 0;
    seal(&replay, local, &local_len);
    assert(keyboard_replay_stage(&replay, 1, local, local_len) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_stage(&replay, 1, local, local_len) == KEYBOARD_REPLAY_DUPLICATE);
    assert(keyboard_replay_begin(&replay, 4) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_publish(&replay, 4, record_call, NULL) == KEYBOARD_REPLAY_NOT_DUE);
    unsigned char later[STOCK_INPUT_MAX_BYTES];
    unsigned later_len = 0;
    seal(&replay, later, &later_len);
    assert(keyboard_replay_begin(&replay, 5) == KEYBOARD_REPLAY_OK);
    replay_calls calls = {0};
    assert(keyboard_replay_publish(&replay, 5, record_call, &calls) == KEYBOARD_REPLAY_WAITING);
    assert(!calls.count);
    unsigned neutral_len = encode_row(neutral, 3, 0);
    assert(keyboard_replay_stage(&replay, 2, neutral, neutral_len) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_publish(&replay, 5, record_call, &calls) == KEYBOARD_REPLAY_OK);
    assert(!calls.count);
    keyboard_replay_stop(&replay);
    assert(keyboard_replay_destroy(&replay));
}
static void test_phase_and_invalid_wire(void) {
    keyboard_replay replay;
    setup(&replay, 2);
    assert(keyboard_replay_begin(&replay, 3) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_consume(&replay, STOCK_INPUT_ACTION, 0x7b, 0) ==
           KEYBOARD_REPLAY_UNSUPPORTED);
    keyboard_replay_stop(&replay);
    assert(keyboard_replay_destroy(&replay));

    setup(&replay, 1);
    start_frame_three(&replay);
    unsigned char already_sealed[STOCK_INPUT_MAX_BYTES];
    unsigned sealed_length = 0;
    seal(&replay, already_sealed, &sealed_length);
    assert(keyboard_replay_consume(&replay, STOCK_INPUT_STATE, 2, 1) == KEYBOARD_REPLAY_PHASE);
    assert(keyboard_replay_begin(&replay, 4) == KEYBOARD_REPLAY_PHASE);
    keyboard_replay_stop(&replay);
    assert(keyboard_replay_destroy(&replay));

    setup(&replay, 1);
    start_frame_three(&replay);
    unsigned char unsupported[STOCK_INPUT_MAX_BYTES];
    stock_input_record invalid = {0};
    invalid.capture16 = 3;
    invalid.count = 2;
    invalid.events[0] = (stock_input_event){STOCK_INPUT_STATE, 2, 1};
    invalid.events[1] = (stock_input_event){STOCK_INPUT_STATE, 6, 1};
    unsigned length = stock_input_encode(&invalid, unsupported, sizeof(unsupported));
    assert(keyboard_replay_stage(&replay, 2, unsupported, length) == KEYBOARD_REPLAY_QUEUE_FAILURE);
    keyboard_replay_status status;
    assert(keyboard_replay_get_status(&replay, &status));
    assert(status.queue_result == LINUX_STOCK_UNSUPPORTED_INPUT);
    replay_calls calls = {0};
    assert(keyboard_replay_publish(&replay, 3, record_call, &calls) == KEYBOARD_REPLAY_QUEUE_FAILURE);
    assert(!calls.count);
    keyboard_replay_stop(&replay);
    assert(keyboard_replay_destroy(&replay));
}
static void test_short_capture_window(void) {
    keyboard_replay replay;
    assert(keyboard_replay_init(&replay));
    keyboard_replay_config config = {2, 2, 0, 3, 3, {111, 222}, 0, 0};
    assert(keyboard_replay_configure(&replay, &config) == KEYBOARD_REPLAY_OK);
    start_frame_three(&replay);
    unsigned char neutral[STOCK_INPUT_MAX_BYTES];
    unsigned length = 0;
    seal(&replay, neutral, &length);
    assert(keyboard_replay_begin(&replay, 4) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_publish(&replay, 4, record_call, NULL) == KEYBOARD_REPLAY_NOT_DUE);
    keyboard_replay_status status;
    assert(keyboard_replay_get_status(&replay, &status));
    assert(status.phase == KEYBOARD_REPLAY_DRAIN);
    keyboard_replay_stop(&replay);
    assert(keyboard_replay_destroy(&replay));
}
static void test_configuration_constraints(void) {
    keyboard_replay replay;
    assert(keyboard_replay_init(&replay));
    keyboard_replay_config config = {1, 2, 0, 2, 5, {111, 222}, 0, 0};
    assert(keyboard_replay_configure(&replay, &config) == KEYBOARD_REPLAY_ARGUMENT);
    config.first_capture = 3;
    config.saved_ship_ids[1] = 111;
    assert(keyboard_replay_configure(&replay, &config) == KEYBOARD_REPLAY_ARGUMENT);
    config.saved_ship_ids[1] = 222;
    config.lookahead = 1;
    assert(keyboard_replay_configure(&replay, &config) == KEYBOARD_REPLAY_ARGUMENT);
    config.delay = 3;
    assert(keyboard_replay_configure(&replay, &config) == KEYBOARD_REPLAY_OK);
    keyboard_replay_stop(&replay);
    assert(keyboard_replay_begin(&replay, 3) == KEYBOARD_REPLAY_STOPPED);
    assert(keyboard_replay_configure(&replay, &config) == KEYBOARD_REPLAY_STOPPED);
    assert(keyboard_replay_destroy(&replay));
}
static void test_echo_and_output_refusal(void) {
    keyboard_replay replay;
    setup(&replay, 1);
    start_frame_three(&replay);
    assert(keyboard_replay_consume(&replay, STOCK_INPUT_STATE, 2, 1) == KEYBOARD_REPLAY_OK);
    unsigned char output[STOCK_INPUT_MAX_BYTES];
    memset(output, 0xa5, sizeof(output));
    unsigned length = 999;
    assert(keyboard_replay_seal(&replay, output, 1, &length) == KEYBOARD_REPLAY_ARGUMENT);
    assert(length == 999 && output[0] == 0xa5);
    seal(&replay, output, &length);
    unsigned char wrong_echo[STOCK_INPUT_MAX_BYTES];
    unsigned wrong_length = encode_row(wrong_echo, 3, 0);
    assert(keyboard_replay_stage(&replay, 1, wrong_echo, wrong_length) == KEYBOARD_REPLAY_QUEUE_FAILURE);
    keyboard_replay_status status;
    assert(keyboard_replay_get_status(&replay, &status));
    assert(status.queue_result == LINUX_STOCK_ECHO_MISMATCH);
    keyboard_replay_stop(&replay);
    assert(keyboard_replay_destroy(&replay));
}
int main(void) {
    test_order_and_dedup();
    test_second_slot_neutral();
    test_invalid_neutral_before_effect();
    test_callback_failure();
    test_unsupported_and_overflow();
    test_missing_row_and_retry();
    test_phase_and_invalid_wire();
    test_short_capture_window();
    test_configuration_constraints();
    test_echo_and_output_refusal();
    puts("PASS: keyboard replay phases, neutral peer, validation, errors and no retry");
    return 0;
}
