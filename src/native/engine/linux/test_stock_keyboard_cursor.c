#include "stock_keyboard_replay.h"
#include <assert.h>
#include <math.h>
#include <string.h>

typedef struct { unsigned prepared, applied; } calls;
static int prepare(void *context, uint64_t tick, const uint64_t saved[2], const stock_input_record rows[2]) {
    calls *state = context;
    assert(!state->prepared && !state->applied && tick == 40 && saved[0] == 111 && saved[1] == 222);
    assert(rows[0].flags == (STOCK_INPUT_ACTIVE | STOCK_INPUT_CURSOR));
    assert(rows[0].cursor[0] == 0.25f && rows[1].cursor[1] == -0.5f);
    ++state->prepared;
    return 1;
}
static int apply(void *context, uint64_t tick, unsigned slot, uint64_t saved, const stock_input_record *row) {
    calls *state = context;
    assert(state->prepared == 1 && tick == 40 && slot == state->applied + 1 &&
           saved == (slot == 1 ? 111u : 222u) && row->capture16 == 38);
    ++state->applied;
    return 1;
}
static void run(unsigned failure) {
    keyboard_replay replay;
    keyboard_replay_config config = {.own_slot = 1, .delay = 2, .first_capture = 38,
        .last_capture = 39, .saved_ship_ids = {111, 222}, .independent_players = 1, .cursor_active_only = 1};
    assert(keyboard_replay_init(&replay));
    assert(keyboard_replay_configure(&replay, &config) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_begin(&replay, 38) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_publish_slots(&replay, 38, NULL, NULL) == KEYBOARD_REPLAY_NOT_DUE);
    unsigned char local[STOCK_INPUT_MAX_BYTES], remote[STOCK_INPUT_MAX_BYTES];
    unsigned local_length = 0;
    float cursor[3] = {0.25f, 0, 0};
    assert(keyboard_replay_seal(&replay, local, sizeof local, &local_length) == KEYBOARD_REPLAY_ARGUMENT);
    assert(keyboard_replay_seal_cursor(&replay, cursor, local, sizeof local, &local_length) == KEYBOARD_REPLAY_OK);
    if (!failure) {
        unsigned duplicate_length = 0;
        unsigned char duplicate[STOCK_INPUT_MAX_BYTES];
        assert(keyboard_replay_seal_cursor(&replay, cursor, duplicate, sizeof duplicate, &duplicate_length) == KEYBOARD_REPLAY_DUPLICATE);
        assert(duplicate_length == local_length && !memcmp(duplicate, local, local_length));
    }
    stock_input_record row = {.flags = STOCK_INPUT_ACTIVE | STOCK_INPUT_CURSOR, .capture16 = 38,
                              .cursor = {0, -0.5f, 0}};
    if (failure == 1) row.flags &= ~STOCK_INPUT_ACTIVE;
    if (failure == 2) row.flags |= STOCK_INPUT_FIRE;
    unsigned remote_length = stock_input_encode(&row, remote, sizeof remote);
    assert(remote_length && keyboard_replay_stage(&replay, 1, local, local_length) == KEYBOARD_REPLAY_OK);
    keyboard_replay_result remote_result = keyboard_replay_stage(&replay, 2, remote, remote_length);
    if (failure == 2) {
        assert(remote_result == KEYBOARD_REPLAY_QUEUE_FAILURE);
        keyboard_replay_stop(&replay); assert(keyboard_replay_destroy(&replay)); return;
    }
    assert(remote_result == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_begin(&replay, 39) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_publish_slots(&replay, 39, NULL, NULL) == KEYBOARD_REPLAY_NOT_DUE);
    assert(keyboard_replay_seal_cursor(&replay, cursor, local, sizeof local, &local_length) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_begin(&replay, 40) == KEYBOARD_REPLAY_OK);
    calls state = {0};
    keyboard_replay_result result = keyboard_replay_publish_slots_prepared(&replay, 40, prepare, apply, &state);
    if (failure) assert(result == KEYBOARD_REPLAY_INVALID_FRAME && !state.prepared && !state.applied);
    else assert(result == KEYBOARD_REPLAY_OK && state.prepared == 1 && state.applied == 2);
    keyboard_replay_stop(&replay); assert(keyboard_replay_destroy(&replay));
}
int main(void) { run(0); run(1); run(2); return 0; }
