#include "stock_keyboard_replay.h"
#include <assert.h>
#include <stdio.h>

typedef struct {
    keyboard_replay *queue;
    unsigned prepared, applied, contexts, states;
    unsigned fail_slot;
} calls;
static keyboard_replay_config config(void) {
    return (keyboard_replay_config){.own_slot = 1, .delay = 2, .first_capture = 3,
        .last_capture = 5, .saved_ship_ids = {111, 222}, .independent_players = 1,
        .cursor_active_only = 1, .space_context8 = 1};
}
static void close_queue(keyboard_replay *queue) {
    keyboard_replay_stop(queue); assert(keyboard_replay_destroy(queue));
}
static void begin_capture(keyboard_replay *queue, uint64_t tick) {
    assert(keyboard_replay_begin(queue, tick) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_publish_slots(queue, tick, NULL, NULL) == KEYBOARD_REPLAY_NOT_DUE);
}
static void stage(keyboard_replay *queue, uint64_t tick, unsigned remote_active,
                  const stock_input_event *events, unsigned count) {
    unsigned active;
    assert(keyboard_replay_space_local_state(queue, &active));
    float cursor[3] = {0.25f, 0, 0};
    unsigned char bytes[STOCK_INPUT_MAX_BYTES]; unsigned length;
    assert(keyboard_replay_seal_cursor(queue, active ? cursor : NULL, bytes, sizeof bytes, &length) == KEYBOARD_REPLAY_OK);
    stock_input_record local;
    assert(stock_input_decode(bytes, length, &local));
    assert(local.flags == (active ? STOCK_INPUT_ACTIVE | STOCK_INPUT_CURSOR : 0));
    assert(keyboard_replay_stage(queue, 1, bytes, length) == KEYBOARD_REPLAY_OK);
    stock_input_record remote = {.flags = remote_active ? STOCK_INPUT_ACTIVE | STOCK_INPUT_CURSOR : 0,
                                .capture16 = (uint16_t)tick, .count = count};
    if (count) memcpy(remote.events, events, count * sizeof *events);
    if (remote_active) remote.cursor[1] = -0.5f;
    length = stock_input_encode(&remote, bytes, sizeof bytes);
    assert(length && keyboard_replay_stage(queue, 2, bytes, length) == KEYBOARD_REPLAY_OK);
}
static int prepare(void *context, uint64_t tick, const uint64_t saved[2], const stock_input_record rows[2]) {
    calls *state = context;
    assert(tick >= 5 && saved[0] == 111 && saved[1] == 222);
    assert(state->applied == state->prepared * 2);
    for (unsigned slot = 0; slot < 2; ++slot) {
        unsigned before, after;
        assert(keyboard_replay_space_slot_state(state->queue, slot + 1, &before));
        assert(keyboard_replay_space_record_active(&state->queue->config, &rows[slot], before, &after));
        assert(after == !!(rows[slot].flags & STOCK_INPUT_ACTIVE));
    }
    ++state->prepared;
    return 1;
}
static int apply(void *context, uint64_t tick, unsigned slot, uint64_t saved, const stock_input_record *row) {
    calls *state = context;
    assert(tick >= 5 && saved == (slot == 1 ? 111u : 222u));
    assert(state->prepared == state->applied / 2 + 1 && slot == state->applied % 2 + 1);
    for (unsigned index = 0; index < row->count; ++index) {
        const stock_input_event *event = &row->events[index];
        if (keyboard_replay_is_space_event(&state->queue->config, event)) ++state->contexts;
        else { assert(event->kind == STOCK_INPUT_STATE); ++state->states; }
    }
    ++state->applied;
    return state->fail_slot != slot;
}
static void state_is(keyboard_replay *queue, unsigned first, unsigned second) {
    unsigned active;
    assert(keyboard_replay_space_slot_state(queue, 1, &active) && active == first);
    assert(keyboard_replay_space_slot_state(queue, 2, &active) && active == second);
}
static void valid_delayed_stream(void) {
    keyboard_replay queue; keyboard_replay_config cfg = config();
    assert(keyboard_replay_init(&queue) && keyboard_replay_configure(&queue, &cfg) == KEYBOARD_REPLAY_OK);
    state_is(&queue, 1, 1);
    begin_capture(&queue, 3);
    assert(keyboard_replay_consume(&queue, STOCK_INPUT_CONTEXT, 8, 0) == KEYBOARD_REPLAY_OK);
    float cursor[3] = {0, 0, 0}; unsigned char bytes[STOCK_INPUT_MAX_BYTES]; unsigned length;
    assert(keyboard_replay_seal_cursor(&queue, cursor, bytes, sizeof bytes, &length) == KEYBOARD_REPLAY_ARGUMENT);
    stage(&queue, 3, 1, NULL, 0);
    state_is(&queue, 1, 1); /* local UI edge cannot publish delayed simulation state */
    begin_capture(&queue, 4);
    assert(keyboard_replay_consume(&queue, STOCK_INPUT_CONTEXT, 8, 1) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_consume(&queue, STOCK_INPUT_STATE, 8, 1) == KEYBOARD_REPLAY_OK);
    stock_input_event off = {STOCK_INPUT_CONTEXT, 8, 0};
    stage(&queue, 4, 0, &off, 1);
    assert(keyboard_replay_begin(&queue, 5) == KEYBOARD_REPLAY_OK);
    calls state = {.queue = &queue};
    assert(keyboard_replay_publish_slots_prepared(&queue, 5, prepare, apply, &state) == KEYBOARD_REPLAY_OK);
    state_is(&queue, 0, 1);
    stock_input_event on = {STOCK_INPUT_CONTEXT, 8, 1};
    stage(&queue, 5, 1, &on, 1);
    assert(keyboard_replay_begin(&queue, 6) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_publish_slots_prepared(&queue, 6, prepare, apply, &state) == KEYBOARD_REPLAY_OK);
    state_is(&queue, 1, 0);
    assert(keyboard_replay_begin(&queue, 7) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_publish_slots_prepared(&queue, 7, prepare, apply, &state) == KEYBOARD_REPLAY_OK);
    state_is(&queue, 1, 1);
    assert(state.prepared == 3 && state.applied == 6 && state.contexts == 4 && state.states == 1);
    close_queue(&queue);
}
static void remote_refusal(unsigned variant) {
    keyboard_replay queue; keyboard_replay_config cfg = config();
    assert(keyboard_replay_init(&queue) && keyboard_replay_configure(&queue, &cfg) == KEYBOARD_REPLAY_OK);
    begin_capture(&queue, 3);
    float cursor[3] = {0}; unsigned char local[STOCK_INPUT_MAX_BYTES], bytes[STOCK_INPUT_MAX_BYTES]; unsigned length;
    assert(keyboard_replay_seal_cursor(&queue, cursor, local, sizeof local, &length) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_stage(&queue, 1, local, length) == KEYBOARD_REPLAY_OK);
    stock_input_record remote = {.flags = STOCK_INPUT_ACTIVE | STOCK_INPUT_CURSOR, .capture16 = 3};
    if (variant == 0) { remote.count = 1; remote.events[0] = (stock_input_event){STOCK_INPUT_CONTEXT, 8, 1}; }
    if (variant == 1) { remote.flags = 0; } /* inactive without an edge */
    if (variant == 2) { remote.count = 1; remote.events[0] = (stock_input_event){STOCK_INPUT_CONTEXT, 8, 0}; }
    if (variant == 3) {
        remote.flags = 0; remote.count = 2;
        remote.events[0] = remote.events[1] = (stock_input_event){STOCK_INPUT_CONTEXT, 8, 0};
    }
    if (variant == 4) {
        remote.flags = STOCK_INPUT_CURSOR; remote.count = 1;
        remote.events[0] = (stock_input_event){STOCK_INPUT_CONTEXT, 8, 0};
    }
    if (variant == 5) { remote.count = 1; remote.events[0] = (stock_input_event){STOCK_INPUT_CONTEXT, 7, 0}; }
    if (variant >= 6 && variant <= 8) remote.flags |= variant == 6 ? STOCK_INPUT_AIM : variant == 7 ? STOCK_INPUT_VIEW : STOCK_INPUT_FIRE;
    length = stock_input_encode(&remote, bytes, sizeof bytes); assert(length);
    keyboard_replay_result staged = keyboard_replay_stage(&queue, 2, bytes, length);
    if (variant >= 5) {
        assert(staged == KEYBOARD_REPLAY_QUEUE_FAILURE); close_queue(&queue); return;
    }
    assert(staged == KEYBOARD_REPLAY_OK);
    begin_capture(&queue, 4);
    assert(keyboard_replay_seal_cursor(&queue, cursor, local, sizeof local, &length) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_begin(&queue, 5) == KEYBOARD_REPLAY_OK);
    calls state = {.queue = &queue};
    assert(keyboard_replay_publish_slots_prepared(&queue, 5, prepare, apply, &state) == KEYBOARD_REPLAY_INVALID_FRAME);
    assert(!state.prepared && !state.applied && queue.space_active[0] == 1 && queue.space_active[1] == 1);
    close_queue(&queue);
}
static void callback_failure_never_commits_or_retries(void) {
    keyboard_replay queue; keyboard_replay_config cfg = config();
    assert(keyboard_replay_init(&queue) && keyboard_replay_configure(&queue, &cfg) == KEYBOARD_REPLAY_OK);
    begin_capture(&queue, 3);
    assert(keyboard_replay_consume(&queue, STOCK_INPUT_CONTEXT, 8, 0) == KEYBOARD_REPLAY_OK);
    stage(&queue, 3, 1, NULL, 0);
    begin_capture(&queue, 4);
    stage(&queue, 4, 1, NULL, 0);
    assert(keyboard_replay_begin(&queue, 5) == KEYBOARD_REPLAY_OK);
    calls state = {.queue = &queue, .fail_slot = 2};
    assert(keyboard_replay_publish_slots_prepared(&queue, 5, prepare, apply, &state) == KEYBOARD_REPLAY_CALLBACK_FAILURE);
    assert(state.prepared == 1 && state.applied == 2 && queue.space_active[0] == 1 && queue.space_active[1] == 1);
    /* Native slot1 may already have effects. A stale coordinator state is not
     * permission to retry or claim rollback after the permanent failure. */
    assert(keyboard_replay_publish_slots_prepared(&queue, 5, prepare, apply, &state) == KEYBOARD_REPLAY_CALLBACK_FAILURE);
    assert(state.applied == 2);
    close_queue(&queue);
}
static void strict_configuration_and_local_edges(void) {
    for (unsigned variant = 0; variant < 3; ++variant) {
        keyboard_replay queue; keyboard_replay_config cfg = config();
        if (variant == 0) cfg.independent_players = 0;
        if (variant == 1) cfg.cursor_active_only = 0;
        if (variant == 2) cfg.space_context8 = 2;
        assert(keyboard_replay_init(&queue));
        assert(keyboard_replay_configure(&queue, &cfg) == KEYBOARD_REPLAY_ARGUMENT);
        close_queue(&queue);
    }
    for (unsigned variant = 0; variant < 5; ++variant) {
        keyboard_replay queue; keyboard_replay_config cfg = config();
        if (variant == 0) cfg.space_context8 = 0;
        assert(keyboard_replay_init(&queue) && keyboard_replay_configure(&queue, &cfg) == KEYBOARD_REPLAY_OK);
        begin_capture(&queue, 3);
        unsigned kind = STOCK_INPUT_CONTEXT, id = 8, value = 0;
        if (variant == 1) value = 1;
        if (variant == 2) value = 2;
        if (variant == 3) id = 7;
        if (variant == 4) kind = STOCK_INPUT_ACTION;
        assert(keyboard_replay_consume(&queue, kind, id, value) == KEYBOARD_REPLAY_UNSUPPORTED);
        close_queue(&queue);
    }
    keyboard_replay_config cfg = config(); unsigned after = 99;
    stock_input_record neutral = {.capture16 = 3};
    assert(keyboard_replay_space_record_active(&cfg, &neutral, 0, &after) && after == 0);
    neutral.cursor[0] = -0.0f;
    assert(!keyboard_replay_space_record_active(&cfg, &neutral, 0, &after));
    neutral.cursor[0] = 0;
    stock_input_record double_edge = {.flags = STOCK_INPUT_ACTIVE, .count = 2};
    double_edge.events[0] = (stock_input_event){STOCK_INPUT_CONTEXT, 8, 0};
    double_edge.events[1] = (stock_input_event){STOCK_INPUT_CONTEXT, 8, 1};
    assert(keyboard_replay_space_record_active(&cfg, &double_edge, 1, &after) && after == 1);
    neutral.aim[0] = 1;
    assert(!keyboard_replay_space_record_active(&cfg, &neutral, 0, &after));
}
int main(void) {
    valid_delayed_stream(); strict_configuration_and_local_edges(); callback_failure_never_commits_or_retries();
    for (unsigned variant = 0; variant < 9; ++variant) remote_refusal(variant);
    puts("SPACE context8 ordered edges, inactive cursor and pre-effect pair validation PASS");
    return 0;
}
