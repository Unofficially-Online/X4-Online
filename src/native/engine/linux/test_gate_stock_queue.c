/* Offline queue data-path tests. Never loads or calls X4. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "gate_stock_queue.h"

static linux_stock_config config_for(
    uint64_t first,
    uint64_t last,
    unsigned slots,
    unsigned delay
) {
    linux_stock_config config = {0};
    config.slots = slots;
    config.own_slot = 1;
    config.delay = delay;
    config.first_capture = first;
    config.last_capture = last;
    config.policy = linux_stock_keyboard_policy();
    assert(linux_stock_allow_state(&config.policy, 0x11));
    assert(linux_stock_allow_state(&config.policy, 0x12));
    config.policy.allow_space8 = 1;
    config.policy.allow_stop_action = 1;
    for (unsigned slot_index = 0; slot_index < slots; ++slot_index) {
        config.saved_ships[slot_index] = 0x1001 + slot_index;
    }
    return config;
}

static linux_stock_queue *make_queue(linux_stock_config config) {
    linux_stock_queue *queue = malloc(sizeof(*queue));
    assert(queue);
    assert(linux_stock_queue_init(queue));
    assert(linux_stock_configure(queue, &config) == LINUX_STOCK_OK);
    return queue;
}

static void free_queue(linux_stock_queue *queue) {
    assert(linux_stock_queue_destroy(queue));
    free(queue);
}

static unsigned packet(uint64_t tick, unsigned state, unsigned char *bytes) {
    stock_input_record record = {0};
    record.capture16 = (uint16_t) tick;
    record.flags = STOCK_INPUT_ACTIVE;
    record.count = 1;
    record.events[0] = (stock_input_event) { STOCK_INPUT_STATE, 0x11, state };
    return stock_input_encode(&record, bytes, STOCK_INPUT_MAX_BYTES);
}

static void capture_state(
    linux_stock_queue *queue,
    unsigned state,
    unsigned char *bytes,
    unsigned *length
) {
    assert(linux_stock_append_event(queue, (stock_input_event) { STOCK_INPUT_STATE, 0x11, state }) == LINUX_STOCK_OK);
    assert(linux_stock_capture(queue, STOCK_INPUT_ACTIVE, bytes, STOCK_INPUT_MAX_BYTES, length) == LINUX_STOCK_CAPTURED);
}

static void test_configuration(void) {
    linux_stock_config config = config_for(1, 10, 2, 3), bad;
    linux_stock_queue *queue = malloc(sizeof(*queue));
    assert(queue && linux_stock_queue_init(queue));
    assert(linux_stock_begin_tick(queue, 1) == LINUX_STOCK_NOT_CONFIGURED);
    for (unsigned field = 0; field < 12; ++field) {
        bad = config;
        switch (field) {
            case 0:
                bad.slots = 0;
                break;
            case 1:
                bad.slots = 9;
                break;
            case 2:
                bad.own_slot = 0;
                break;
            case 3:
                bad.own_slot = 3;
                break;
            case 4:
                bad.delay = 1;
                break;
            case 5:
                bad.delay = 31;
                break;
            case 6:
                bad.lookahead = 2;
                break;
            case 7:
                bad.first_capture = 0;
                break;
            case 8:
                bad.last_capture = LINUX_STOCK_TICK_MAX;
                break;
            case 9:
                bad.saved_ships[1] = bad.saved_ships[0];
                break;
            case 10:
                bad.policy.allowed_flags = STOCK_INPUT_FIRE;
                break;
            case 11:
                bad.policy.allow_space8 = 2;
                break;
        }
        assert(linux_stock_configure(queue, &bad) == LINUX_STOCK_INVALID_CONFIG);
    }
    assert(linux_stock_configure(queue, &config) == LINUX_STOCK_OK);
    assert(linux_stock_configure(queue, &config) == LINUX_STOCK_DUPLICATE);
    bad = config;
    bad.policy.allow_stop_action = 0;
    assert(linux_stock_configure(queue, &bad) == LINUX_STOCK_INVALID_CONFIG);
    assert(linux_stock_begin_tick(queue, 2) == LINUX_STOCK_INVALID_TICK);
    assert(linux_stock_begin_tick(queue, 1) == LINUX_STOCK_OK);
    assert(linux_stock_begin_tick(queue, 1) == LINUX_STOCK_DUPLICATE);
    assert(linux_stock_begin_tick(queue, 2) == LINUX_STOCK_CAPTURE_UNSEALED);
    free_queue(queue);
}

static void test_capture_order_and_overflow(void) {
    linux_stock_queue *queue = make_queue(config_for(1, 10, 2, 3));
    unsigned char bytes[229], again[229], before[229];
    unsigned length = 999;
    memset(bytes, 0xa5, sizeof(bytes));
    memcpy(before, bytes, sizeof(bytes));
    assert(linux_stock_append_event(queue, (stock_input_event) { 1, 0x11, 1 }) == LINUX_STOCK_NOT_DUE);
    assert(linux_stock_begin_tick(queue, 1) == LINUX_STOCK_OK);
    for (unsigned event_index = 0; event_index < 31; ++event_index) {
        assert(linux_stock_append_event(queue, (stock_input_event) { 1, 0x11, event_index % 3 == 0 ? 0 : 1 }) == LINUX_STOCK_OK);
    }
    assert(linux_stock_capture(queue, 1, bytes, 228, &length) == LINUX_STOCK_BUFFER_SMALL);
    assert(length == 999 && !memcmp(bytes, before, sizeof(bytes)));
    assert(linux_stock_capture(queue, 1, bytes, sizeof(bytes), &length) == LINUX_STOCK_CAPTURED
        && length == 229);
    stock_input_record decoded;
    assert(stock_input_decode(bytes, length, &decoded) && decoded.count == 31);
    for (unsigned event_index = 0; event_index < 31; ++event_index) {
        assert(decoded.events[event_index].value == (event_index % 3 == 0 ? 0u : 1u));
    }
    unsigned second = 0;
    assert(linux_stock_capture(queue, 1, again, sizeof(again), &second) == LINUX_STOCK_DUPLICATE);
    assert(second == length && !memcmp(bytes, again, length));
    assert(linux_stock_append_event(queue, (stock_input_event) { 1, 0x11, 0 }) == LINUX_STOCK_CAPTURE_SEALED);
    assert(linux_stock_publish(queue) == LINUX_STOCK_CAPTURE_SEALED);
    free_queue(queue);
    queue = make_queue(config_for(1, 10, 2, 3));
    assert(linux_stock_begin_tick(queue, 1) == LINUX_STOCK_OK);
    for (unsigned event_index = 0; event_index < 31; ++event_index) {
        assert(linux_stock_append_event(queue, (stock_input_event) { 1, 0x11, 1 }) == LINUX_STOCK_OK);
    }
    assert(linux_stock_append_event(queue, (stock_input_event) { 1, 0x11, 1 }) == LINUX_STOCK_OVERFLOW);
    assert(queue->capture_record.count == 31);
    assert(linux_stock_capture(queue, 1, bytes, sizeof(bytes), &length) == LINUX_STOCK_OVERFLOW);
    assert(linux_stock_begin_tick(queue, 2) == LINUX_STOCK_OVERFLOW);
    linux_stock_status status;
    assert(linux_stock_copy_status(queue, &status));
    assert(status.failure == LINUX_STOCK_OVERFLOW && status.captured_events == 31 && !status.published_tick);
    linux_stock_stop(queue);
    assert(linux_stock_copy_status(queue, &status));
    assert(status.stopped && status.failure == LINUX_STOCK_OVERFLOW);
    assert(linux_stock_configure(queue, &queue->config) == LINUX_STOCK_STOPPED);
    free_queue(queue);
    queue = make_queue(config_for(1, 1, 1, 2));
    assert(linux_stock_begin_tick(queue, 1) == LINUX_STOCK_OK);
    capture_state(queue, 1, bytes, &length);
    assert(linux_stock_capture(queue, 0, again, sizeof(again), &second) == LINUX_STOCK_CONFLICT);
    free_queue(queue);
}

static void test_policy(void) {
    const stock_input_event denied[] = {
        { 1, 0x13, 1 },
        { 1, 0x11, 2 },
        { 0, 0x130, 0 },
        { 0, 0x7b, 1 },
        { 2, 4, 0 },
        { 3, 13,
            1 },
        { 3, 8, 2 },
        { 4, 0, 0 },
        { 1, 512, 0 }
    };
    for (unsigned event_index = 0; event_index < sizeof(denied) / sizeof(denied[0]); ++event_index) {
        linux_stock_queue *queue = make_queue(config_for(1, 10, 2, 3));
        assert(linux_stock_begin_tick(queue, 1) == LINUX_STOCK_OK);
        assert(linux_stock_append_event(queue, denied[event_index]) == LINUX_STOCK_UNSUPPORTED_INPUT);
        assert(linux_stock_publish(queue) == LINUX_STOCK_UNSUPPORTED_INPUT);
        free_queue(queue);
    }
    for (unsigned flag = STOCK_INPUT_CURSOR; flag <= STOCK_INPUT_SECONDARY_FIRE; flag <<= 1) {
        linux_stock_queue *queue = make_queue(config_for(1, 10, 2, 3));
        assert(linux_stock_begin_tick(queue, 1) == LINUX_STOCK_OK);
        unsigned char bytes[229];
        unsigned length = 0;
        assert(linux_stock_capture(queue, flag, bytes, sizeof(bytes), &length) == LINUX_STOCK_UNSUPPORTED_INPUT);
        free_queue(queue);
    }
    linux_stock_config config = config_for(1, 10, 2, 3);
    config.policy.allow_stop_action = 0;
    config.policy.allow_space8 = 0;
    linux_stock_queue *queue = make_queue(config);
    assert(linux_stock_begin_tick(queue, 1) == LINUX_STOCK_OK);
    assert(linux_stock_append_event(queue, (stock_input_event) { 3, 8, 1 }) == LINUX_STOCK_UNSUPPORTED_INPUT);
    free_queue(queue);
    queue = make_queue(config_for(1, 10, 2, 3));
    assert(linux_stock_begin_tick(queue, 1) == LINUX_STOCK_OK);
    assert(linux_stock_append_event(queue, (stock_input_event) { 3, 8, 1 }) == LINUX_STOCK_OK);
    assert(linux_stock_append_event(queue, (stock_input_event) { 0, 0x7b, 0 }) == LINUX_STOCK_OK);
    unsigned char bytes[229];
    unsigned length = 0;
    assert(linux_stock_capture(queue, 0, bytes, sizeof(bytes), &length) == LINUX_STOCK_CAPTURED);
    assert(linux_stock_stage(queue, 1, bytes, length) == LINUX_STOCK_STAGED);
    free_queue(queue);
}

static void test_echo_ordering(void) {
    for (unsigned receive_first = 0; receive_first < 2; ++receive_first) {
        for (unsigned mismatch = 0; mismatch < 2; ++mismatch) {
            linux_stock_queue *queue = make_queue(config_for(1, 10, 2, 3));
            assert(linux_stock_begin_tick(queue, 1) == LINUX_STOCK_OK);
            unsigned char local[229], echo[229];
            unsigned length = packet(1, mismatch ? 0 : 1, echo), local_length = 0;
            if (receive_first) {
                assert(linux_stock_stage(queue, 1, echo, length) == LINUX_STOCK_STAGED);
            }
            assert(linux_stock_append_event(queue, (stock_input_event) { 1, 0x11, 1 }) == LINUX_STOCK_OK);
            linux_stock_result result = linux_stock_capture(queue, 1, local, sizeof(local),
                &local_length);
            if (receive_first && mismatch) {
                assert(result == LINUX_STOCK_ECHO_MISMATCH);
            } else {
                assert(result == LINUX_STOCK_CAPTURED);
                if (!receive_first) {
                    assert(linux_stock_stage(queue, 1, echo, length) == (mismatch ? LINUX_STOCK_ECHO_MISMATCH : LINUX_STOCK_STAGED));
                }
            }
            if (mismatch) {
                assert(!queue->frame_ready);
                assert(linux_stock_begin_tick(queue, 2) == LINUX_STOCK_ECHO_MISMATCH);
                assert(linux_stock_configure(queue, &queue->config) == LINUX_STOCK_ECHO_MISMATCH);
            } else {
                assert(linux_stock_stage(queue, 1, echo, length) == LINUX_STOCK_DUPLICATE);
            }
            free_queue(queue);
        }
    }
}

static void test_nonfirst_own_slot(void) {
    linux_stock_config config = config_for(1, 1, 8, 2);
    config.own_slot = 8;
    linux_stock_queue *queue = make_queue(config);
    unsigned char local[229], remote[229];
    unsigned length = 0;
    assert(linux_stock_begin_tick(queue, 1) == LINUX_STOCK_OK);
    capture_state(queue, 1, local, &length);
    unsigned other_length = packet(1, 0, remote);
    for (unsigned slot = 1; slot <= 8; ++slot) {
        assert(linux_stock_stage(queue, slot, slot == 8 ? local : remote, slot == 8 ? length : other_length) == LINUX_STOCK_STAGED);
    }
    assert(linux_stock_begin_tick(queue, 2) == LINUX_STOCK_OK);
    assert(linux_stock_begin_tick(queue, 3) == LINUX_STOCK_OK);
    assert(linux_stock_publish(queue) == LINUX_STOCK_FRAME_READY);
    linux_stock_frame frame;
    assert(linux_stock_copy_frame(queue, 3, &frame) == LINUX_STOCK_FRAME_READY);
    for (unsigned slot = 0; slot < 8; ++slot) {
        stock_input_record decoded;
        assert(stock_input_decode(frame.bytes[slot], frame.lengths[slot], &decoded));
        assert(decoded.events[0].value == (slot == 7 ? 1u : 0u));
    }
    free_queue(queue);
}

static void test_staging_refusals(void) {
    linux_stock_queue *queue = make_queue(config_for(1, 10, 2, 3));
    unsigned char bytes[229], changed[229];
    unsigned length = packet(1, 1, bytes);
    assert(linux_stock_stage(queue, 1, bytes, length) == LINUX_STOCK_INVALID_TICK);
    assert(linux_stock_begin_tick(queue, 1) == LINUX_STOCK_OK);
    assert(linux_stock_stage(queue, 0, bytes, length) == LINUX_STOCK_INVALID_SLOT);
    assert(linux_stock_stage(queue, 3, bytes, length) == LINUX_STOCK_INVALID_SLOT);
    unsigned future = packet(2, 1, changed);
    assert(linux_stock_stage(queue, 2, changed, future) == LINUX_STOCK_FUTURE_RECORD);
    assert(linux_stock_stage(queue, 2, bytes, length) == LINUX_STOCK_STAGED);
    assert(linux_stock_stage(queue, 2, bytes, length) == LINUX_STOCK_DUPLICATE);
    packet(1, 0, changed);
    assert(linux_stock_stage(queue, 2, changed, length) == LINUX_STOCK_CONFLICT);
    assert(!memcmp(queue->pending[4 % 32].bytes[1], bytes, length));
    free_queue(queue);
    queue = make_queue(config_for(1, 10, 2, 3));
    assert(linux_stock_begin_tick(queue, 1) == LINUX_STOCK_OK);
    assert(linux_stock_stage(queue, 2, bytes, length - 1) == LINUX_STOCK_INVALID_RECORD);
    free_queue(queue);
    queue = make_queue(config_for(1, 10, 2, 3));
    assert(linux_stock_begin_tick(queue, 1) == LINUX_STOCK_OK);
    stock_input_record bad = {0};
    bad.capture16 = 1;
    bad.flags = STOCK_INPUT_FIRE;
    length = stock_input_encode(&bad, bytes, sizeof(bytes));
    assert(linux_stock_stage(queue, 2, bytes, length) == LINUX_STOCK_UNSUPPORTED_INPUT);
    free_queue(queue);
    /* Defensive collision check: an occupied live bucket is never overwritten. */
    queue = make_queue(config_for(1, 10, 2, 3));
    assert(linux_stock_begin_tick(queue, 1) == LINUX_STOCK_OK);
    length = packet(1, 1, bytes);
    queue->pending[4].apply = 36;
    queue->pending[4].present = 2;
    assert(linux_stock_stage(queue, 2, bytes, length) == LINUX_STOCK_RING_COLLISION);
    assert(queue->pending[4].apply == 36 && queue->pending[4].present == 2);
    free_queue(queue);
}

static void run_window(uint64_t first, uint64_t last, unsigned slots, unsigned delay) {
    linux_stock_queue *queue = make_queue(config_for(first, last, slots, delay));
    linux_stock_frame frame, retained;
    int have_retained = 0;
    for (uint64_t tick = first; tick <= last + delay; ++tick) {
        assert(linux_stock_begin_tick(queue, tick) == LINUX_STOCK_OK);
        if (tick <= last) {
            unsigned char local[229], echo[229];
            unsigned length = 0, echo_length = packet(tick, (unsigned)(tick & 1), echo);
            if (tick & 1) {
                assert(linux_stock_stage(queue, 1, echo, echo_length) == LINUX_STOCK_STAGED);
            }
            capture_state(queue, (unsigned)(tick & 1), local, &length);
            assert(length == echo_length && !memcmp(local, echo, length));
            assert(linux_stock_stage(queue, 1, local, length) == (tick & 1 ? LINUX_STOCK_DUPLICATE : LINUX_STOCK_STAGED));
            for (unsigned slot = 2; slot <= slots; ++slot) {
                assert(linux_stock_stage(queue, slot, local, length) == LINUX_STOCK_STAGED);
            }
        }
        if (tick < first + delay) {
            assert(linux_stock_publish(queue) == LINUX_STOCK_NOT_DUE);
            continue;
        }
        assert(linux_stock_publish(queue) == LINUX_STOCK_FRAME_READY);
        assert(linux_stock_publish(queue) == LINUX_STOCK_DUPLICATE);
        assert(linux_stock_copy_frame(queue, tick, &frame) == LINUX_STOCK_FRAME_READY);
        assert(frame.tick == tick && frame.capture == tick - delay && frame.slots == slots);
        for (unsigned slot = 0; slot < slots; ++slot) {
            stock_input_record record;
            assert(stock_input_decode(frame.bytes[slot], frame.lengths[slot], &record));
            assert(record.capture16 == (uint16_t)(tick - delay) && record.events[0].value == ((tick - delay) & 1));
            assert(frame.saved_ships[slot] == 0x1001 + slot);
        }
        if (!have_retained) {
            retained = frame;
            have_retained = 1;
        }
        assert(retained.tick == first + delay && retained.capture == first);
        unsigned char late[229];
        unsigned length = packet(tick - delay, 1, late);
        assert(linux_stock_stage(queue, 1, late, length) == LINUX_STOCK_LATE_RECORD);
    }
    assert(linux_stock_begin_tick(queue, last + delay + 1) == LINUX_STOCK_INVALID_TICK);
    linux_stock_stop(queue);
    linux_stock_frame unchanged = frame;
    assert(linux_stock_copy_frame(queue, frame.tick, &frame) == LINUX_STOCK_STOPPED);
    assert(!memcmp(&frame, &unchanged, sizeof(frame)));
    free_queue(queue);
}

static void test_empty_capture(void) {
    for (unsigned flags = 0; flags <= STOCK_INPUT_ACTIVE; ++flags) {
        linux_stock_queue *queue = make_queue(config_for(1, 1, 2, 2));
        assert(linux_stock_begin_tick(queue, 1) == LINUX_STOCK_OK);
        stock_input_record record = {0};
        record.capture16 = 1;
        record.flags = (unsigned char) flags;
        unsigned char expected[229], captured[229];
        unsigned expected_length = stock_input_encode(&record, expected, sizeof(expected)),
            length = 0;
        assert(expected_length == 43);
        assert(linux_stock_stage(queue, 1, expected, expected_length) == LINUX_STOCK_STAGED);
        assert(linux_stock_capture(queue, flags, captured, sizeof(captured), &length) == LINUX_STOCK_CAPTURED);
        assert(length == expected_length && !memcmp(captured, expected, length));
        assert(linux_stock_capture(queue, flags, captured, sizeof(captured), &length) == LINUX_STOCK_DUPLICATE);
        assert(linux_stock_stage(queue, 1, captured, length) == LINUX_STOCK_DUPLICATE);
        assert(linux_stock_stage(queue, 2, captured, length) == LINUX_STOCK_STAGED);
        assert(linux_stock_begin_tick(queue, 2) == LINUX_STOCK_OK);
        assert(linux_stock_begin_tick(queue, 3) == LINUX_STOCK_OK);
        assert(linux_stock_publish(queue) == LINUX_STOCK_FRAME_READY);
        linux_stock_frame frame;
        assert(linux_stock_copy_frame(queue, 3, &frame) == LINUX_STOCK_FRAME_READY);
        for (unsigned slot = 0; slot < 2; ++slot) {
            stock_input_record decoded;
            assert(stock_input_decode(frame.bytes[slot], frame.lengths[slot], &decoded));
            assert(!decoded.count && decoded.flags == flags && decoded.capture16 == 1);
        }
        free_queue(queue);
    }
}

static void test_missing_transaction(void) {
    linux_stock_queue *queue = make_queue(config_for(1, 4, 2, 2));
    unsigned char bytes[229];
    unsigned length = 0;
    linux_stock_frame output, before;
    for (uint64_t tick = 1; tick <= 3; ++tick) {
        assert(linux_stock_begin_tick(queue, tick) == LINUX_STOCK_OK);
        capture_state(queue, 1, bytes, &length);
        assert(linux_stock_stage(queue, 1, bytes, length) == LINUX_STOCK_STAGED);
        if (tick == 3) {
            assert(linux_stock_stage(queue, 2, bytes, length) == LINUX_STOCK_STAGED);
        }
    }
    assert(linux_stock_publish(queue) == LINUX_STOCK_MISSING_SLOT && !queue->frame_ready);
    memset(&output, 0xa5, sizeof(output));
    memcpy(&before, &output, sizeof(output));
    assert(linux_stock_copy_frame(queue, 3, &output) == LINUX_STOCK_NO_FRAME && !memcmp(&output,
        &before, sizeof(output)));
    assert(linux_stock_begin_tick(queue, 4) == LINUX_STOCK_MISSING_SLOT);
    length = packet(1, 1, bytes);
    assert(linux_stock_stage(queue, 2, bytes, length) == LINUX_STOCK_STAGED);
    assert(linux_stock_publish(queue) == LINUX_STOCK_FRAME_READY);
    assert(linux_stock_copy_frame(queue, 3, &output) == LINUX_STOCK_FRAME_READY);
    before = output;
    assert(linux_stock_begin_tick(queue, 4) == LINUX_STOCK_OK);
    assert(linux_stock_publish(queue) == LINUX_STOCK_MISSING_SLOT);
    assert(linux_stock_copy_frame(queue, 3, &output) == LINUX_STOCK_FRAME_READY && !memcmp(&output,
        &before, sizeof(output)));
    assert(linux_stock_copy_frame(queue, 4, &output) == LINUX_STOCK_NO_FRAME);
    length = packet(2, 1, bytes);
    assert(linux_stock_stage(queue, 2, bytes, length) == LINUX_STOCK_STAGED);
    /* Test the missing-local defense explicitly; valid capture sequencing normally prevents it. */
    queue->local[2].present = 0;
    assert(linux_stock_publish(queue) == LINUX_STOCK_MISSING_LOCAL);
    assert(linux_stock_copy_frame(queue, 3, &output) == LINUX_STOCK_FRAME_READY && !memcmp(&output,
        &before, sizeof(output)));
    free_queue(queue);
}

typedef struct {
    linux_stock_queue *queue;
    pthread_barrier_t *barrier;
    linux_stock_result result;
    unsigned kind;
} race;

static void * race_operation(void * argument) {
    race *operation = argument;
    pthread_barrier_wait(operation->barrier);
    if (operation->kind == 0) {
        operation->result = linux_stock_publish(operation->queue);
    } else if (operation->kind == 1) {
        linux_stock_stop(operation->queue);
        operation->result = LINUX_STOCK_STOPPED;
    } else if (operation->kind == 2) {
        operation->result = linux_stock_append_event(operation->queue, (stock_input_event) { 1,
            0x11, 1 });
    } else if (operation->kind == 3) {
        unsigned char bytes[229];
        unsigned length = 0;
        operation->result = linux_stock_capture(operation->queue, 1, bytes, sizeof(bytes), &length);
    } else {
        unsigned char bytes[229];
        unsigned length = packet(1, 0, bytes);
        operation->result = linux_stock_stage(operation->queue, 1, bytes, length);
    }
    return NULL;
}

static void test_races(void) {
    for (unsigned iteration = 0; iteration < 100; ++iteration) {
        linux_stock_queue *queue = make_queue(config_for(1, 1, 2, 2));
        unsigned char bytes[229];
        unsigned length = 0;
        assert(linux_stock_begin_tick(queue, 1) == LINUX_STOCK_OK);
        capture_state(queue, 1, bytes, &length);
        for (unsigned slot = 1; slot <= 2; ++slot) {
            assert(linux_stock_stage(queue, slot, bytes, length) == LINUX_STOCK_STAGED);
        }
        assert(linux_stock_begin_tick(queue, 2) == LINUX_STOCK_OK && linux_stock_begin_tick(queue,
            3) == LINUX_STOCK_OK);
        pthread_barrier_t barrier;
        assert(!pthread_barrier_init(&barrier, NULL, 3));
        race publish = { queue, &barrier, 0, 0 }, stop = { queue, &barrier, 0, 1 };
        pthread_t first_thread, second_thread;
        assert(!pthread_create(&first_thread, NULL, race_operation, &publish));
        assert(!pthread_create(&second_thread, NULL, race_operation, &stop));
        pthread_barrier_wait(&barrier);
        assert(!pthread_join(first_thread, NULL));
        assert(!pthread_join(second_thread, NULL));
        assert(publish.result == LINUX_STOCK_FRAME_READY || publish.result == LINUX_STOCK_STOPPED);
        assert(linux_stock_publish(queue) == LINUX_STOCK_STOPPED);
        assert(linux_stock_begin_tick(queue, 4) == LINUX_STOCK_STOPPED);
        assert(!pthread_barrier_destroy(&barrier));
        free_queue(queue);
    }
    for (unsigned iteration = 0; iteration < 100; ++iteration) {
        linux_stock_queue *queue = make_queue(config_for(1, 1, 1, 2));
        assert(linux_stock_begin_tick(queue, 1) == LINUX_STOCK_OK);
        assert(linux_stock_append_event(queue, (stock_input_event) { 1, 0x11, 1 }) == LINUX_STOCK_OK);
        pthread_barrier_t barrier;
        assert(!pthread_barrier_init(&barrier, NULL, 3));
        race capture = { queue, &barrier, 0, 3 }, receive = { queue, &barrier, 0, 4 };
        pthread_t first_thread, second_thread;
        assert(!pthread_create(&first_thread, NULL, race_operation, &capture));
        assert(!pthread_create(&second_thread, NULL, race_operation, &receive));
        pthread_barrier_wait(&barrier);
        assert(!pthread_join(first_thread, NULL));
        assert(!pthread_join(second_thread, NULL));
        assert((capture.result == LINUX_STOCK_CAPTURED && receive.result == LINUX_STOCK_ECHO_MISMATCH)
            || (receive.result == LINUX_STOCK_STAGED && capture.result == LINUX_STOCK_ECHO_MISMATCH));
        assert(linux_stock_publish(queue) == LINUX_STOCK_ECHO_MISMATCH && !queue->frame_ready);
        assert(!pthread_barrier_destroy(&barrier));
        free_queue(queue);
    }
    linux_stock_queue *queue = make_queue(config_for(1, 1, 1, 2));
    assert(linux_stock_begin_tick(queue, 1) == LINUX_STOCK_OK);
    pthread_barrier_t barrier;
    assert(!pthread_barrier_init(&barrier, NULL, 17));
    race workers[16];
    pthread_t threads[16];
    for (unsigned worker_index = 0; worker_index < 16; ++worker_index) {
        workers[worker_index] = (race) { queue, &barrier, 0, 2 };
        assert(!pthread_create(&threads[worker_index], NULL, race_operation, &workers[worker_index]));
    }
    pthread_barrier_wait(&barrier);
    for (unsigned worker_index = 0; worker_index < 16; ++worker_index) {
        assert(!pthread_join(threads[worker_index], NULL));
        assert(workers[worker_index].result == LINUX_STOCK_OK);
    }
    unsigned char bytes[229];
    unsigned length = 0;
    assert(linux_stock_capture(queue, 0, bytes, sizeof(bytes), &length) == LINUX_STOCK_CAPTURED);
    stock_input_record record;
    assert(stock_input_decode(bytes, length, &record) && record.count == 16);
    for (unsigned worker_index = 0; worker_index < 16; ++worker_index) {
        assert(record.events[worker_index].kind == 1 && record.events[worker_index].id == 0x11
            && record.events[worker_index].value == 1);
    }
    assert(!pthread_barrier_destroy(&barrier));
    free_queue(queue);
}

int main(void) {
    test_configuration();
    test_capture_order_and_overflow();
    test_policy();
    test_echo_ordering();
    test_nonfirst_own_slot();
    test_staging_refusals();
    run_window(65532, 65548, 2, 3);
    run_window(1, 80, 8, 30);
    test_empty_capture();
    test_missing_transaction();
    test_races();
    puts("stock queue: 11 offline groups passed; data only, no native replay");
    return 0;
}
