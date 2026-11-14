/* Engine-free contract: one immutable event/cursor/VIEW record, two validated
 * slots before effects, and no stale presentation data during draining. */
#include "stock_keyboard_replay.h"
#include <assert.h>

static const float local_cursor[3] = {0.25f, -0.5f, 0.0f};
static const linux_stock_view_sample local_view = {3, 0.75f, UINT64_C(0x123456789abcdef0)};

static void open_capture(keyboard_replay *replay, unsigned enabled) {
    keyboard_replay_config config = {
        .own_slot = 1, .delay = 2, .first_capture = 38, .last_capture = 39,
        .saved_ship_ids = {111, 222}, .independent_players = 1,
        .cursor_active_only = 1, .view_capture = enabled
    };
    assert(keyboard_replay_init(replay));
    assert(keyboard_replay_configure(replay, &config) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_begin(replay, 38) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_publish_slots(replay, 38, NULL, NULL) == KEYBOARD_REPLAY_NOT_DUE);
}

static void close_replay(keyboard_replay *replay) {
    keyboard_replay_stop(replay);
    assert(keyboard_replay_destroy(replay));
}

static void seal_first(keyboard_replay *replay, unsigned char *bytes, unsigned *length) {
    assert(keyboard_replay_seal_presentation(replay, local_cursor, &local_view,
        bytes, STOCK_INPUT_MAX_BYTES, length) == KEYBOARD_REPLAY_OK);
}

static void duplicate_conflict(unsigned changed_field) {
    keyboard_replay replay;
    open_capture(&replay, 1);
    unsigned char bytes[STOCK_INPUT_MAX_BYTES];
    unsigned length = 0;
    seal_first(&replay, bytes, &length);
    linux_stock_view_sample changed = local_view;
    if (changed_field == 0) changed.bits ^= 1;
    if (changed_field == 1) changed.fov = 1.0f;
    if (changed_field == 2) changed.target ^= 1;
    unsigned char untouched[STOCK_INPUT_MAX_BYTES];
    memset(untouched, 0xa5, sizeof untouched);
    unsigned unchanged_length = 777;
    assert(keyboard_replay_seal_presentation(&replay, local_cursor, &changed,
        untouched, sizeof untouched, &unchanged_length) == KEYBOARD_REPLAY_QUEUE_FAILURE);
    assert(replay.queue_result == LINUX_STOCK_CONFLICT && unchanged_length == 777);
    for (size_t i = 0; i < sizeof untouched; ++i) assert(untouched[i] == 0xa5);
    close_replay(&replay);
}

static void rejected_local_sample(unsigned failure) {
    keyboard_replay replay;
    open_capture(&replay, failure != 0);
    linux_stock_view_sample invalid = local_view;
    if (failure == 1) invalid.bits = 4;
    if (failure == 2) invalid.fov = NAN;
    if (failure == 3) invalid.fov = INFINITY;
    unsigned char bytes[STOCK_INPUT_MAX_BYTES];
    unsigned length = 777;
    keyboard_replay_result result = keyboard_replay_seal_presentation(
        &replay, local_cursor, &invalid, bytes, sizeof bytes, &length);
    assert(result == (failure ? KEYBOARD_REPLAY_QUEUE_FAILURE : KEYBOARD_REPLAY_ARGUMENT));
    assert(length == 777 && !replay.queue.capture_sealed);
    close_replay(&replay);
}

typedef struct { unsigned prepared, applied; } effect_counts;

static int prepare_pair(void *context, uint64_t tick, const uint64_t saved[2],
                        const stock_input_record records[2]) {
    effect_counts *effects = context;
    assert(tick == 40 && saved[0] == 111 && saved[1] == 222);
    assert(!effects->prepared && !effects->applied);
    assert(records[0].view == local_view.bits && records[0].fov == local_view.fov);
    assert(records[0].target == local_view.target && records[0].cursor[0] == local_cursor[0]);
    assert(records[0].count == 1 && records[0].events[0].id == 2);
    assert(records[1].view == 1 && records[1].fov == 1.25f && records[1].target == 1);
    ++effects->prepared;
    return 1;
}

static int apply_slot(void *context, uint64_t tick, unsigned slot, uint64_t saved,
                      const stock_input_record *record) {
    effect_counts *effects = context;
    assert(effects->prepared == 1 && tick == 40 && slot == effects->applied + 1);
    assert(saved == (slot == 1 ? 111u : 222u) && record->capture16 == 38);
    ++effects->applied;
    return 1;
}

static void delayed_pair(unsigned failure) {
    keyboard_replay replay;
    open_capture(&replay, 1);
    assert(keyboard_replay_consume(&replay, STOCK_INPUT_STATE, 2, 1) == KEYBOARD_REPLAY_OK);
    unsigned char local[STOCK_INPUT_MAX_BYTES], remote[STOCK_INPUT_MAX_BYTES];
    unsigned local_length = 0;
    /* Short output must not seal or consume the authenticated sample. */
    assert(keyboard_replay_seal_presentation(&replay, local_cursor, &local_view,
        local, 1, &local_length) == KEYBOARD_REPLAY_ARGUMENT);
    assert(!replay.queue.capture_sealed && local_length == 0);
    seal_first(&replay, local, &local_length);
    unsigned char duplicate[STOCK_INPUT_MAX_BYTES];
    unsigned duplicate_length = 0;
    assert(keyboard_replay_seal_presentation(&replay, local_cursor, &local_view,
        duplicate, sizeof duplicate, &duplicate_length) == KEYBOARD_REPLAY_DUPLICATE);
    assert(local_length == duplicate_length && !memcmp(local, duplicate, local_length));
    stock_input_record remote_record = {
        .flags = STOCK_INPUT_ACTIVE | STOCK_INPUT_CURSOR | STOCK_INPUT_VIEW,
        .capture16 = 38, .view = 1, .fov = 1.25f, .target = 1,
        .cursor = {0, 0.5f, 0}
    };
    if (failure == 1) remote_record.flags &= ~STOCK_INPUT_ACTIVE;
    if (failure == 2) remote_record.flags |= STOCK_INPUT_FIRE;
    if (failure == 3) remote_record.view = 4;
    if (failure == 4) local[34] ^= 1; /* Own echo target differs after sealing. */
    unsigned remote_length = stock_input_encode(&remote_record, remote, sizeof remote);
    keyboard_replay_result result = keyboard_replay_stage(&replay, 1, local, local_length);
    if (failure == 4) {
        assert(result == KEYBOARD_REPLAY_QUEUE_FAILURE && replay.queue_result == LINUX_STOCK_ECHO_MISMATCH);
        close_replay(&replay);
        return;
    }
    assert(result == KEYBOARD_REPLAY_OK);
    result = keyboard_replay_stage(&replay, 2, remote, remote_length);
    if (failure == 2 || failure == 3) {
        assert(result == KEYBOARD_REPLAY_QUEUE_FAILURE);
        close_replay(&replay);
        return;
    }
    assert(result == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_begin(&replay, 39) == KEYBOARD_REPLAY_OK);
    assert(keyboard_replay_publish_slots(&replay, 39, NULL, NULL) == KEYBOARD_REPLAY_NOT_DUE);
    assert(keyboard_replay_seal_cursor(&replay, local_cursor, local, sizeof local, &local_length) == KEYBOARD_REPLAY_OK);
    stock_input_record absent;
    assert(stock_input_decode(local, local_length, &absent));
    assert(!(absent.flags & STOCK_INPUT_VIEW) && !absent.view && !absent.target &&
           stock_input_float_bits(absent.fov) == 0);
    assert(keyboard_replay_begin(&replay, 40) == KEYBOARD_REPLAY_OK);
    effect_counts effects = {0};
    result = keyboard_replay_publish_slots_prepared(&replay, 40, prepare_pair, apply_slot, &effects);
    if (failure) {
        assert(result == KEYBOARD_REPLAY_INVALID_FRAME && !effects.prepared && !effects.applied);
    } else {
        assert(result == KEYBOARD_REPLAY_OK && effects.prepared == 1 && effects.applied == 2);
    }
    close_replay(&replay);
}

static void space_and_view(void) {
    keyboard_replay_config config = {.independent_players = 1, .cursor_active_only = 1,
        .space_context8 = 1, .view_capture = 1};
    stock_input_record record = {.flags = STOCK_INPUT_VIEW, .view = 2, .fov = 1.0f,
        .target = 17, .count = 1, .events = {{.kind = STOCK_INPUT_CONTEXT, .id = 8, .value = 0}}};
    unsigned after = 99;
    assert(keyboard_replay_space_record_active(&config, &record, 1, &after) && after == 0);
    config.view_capture = 0;
    assert(!keyboard_replay_space_record_active(&config, &record, 1, &after));
    config.view_capture = 1;
    record.cursor[0] = -0.0f;
    assert(!keyboard_replay_space_record_active(&config, &record, 1, &after));
    record.cursor[0] = 0.0f;
    record.flags |= STOCK_INPUT_FIRE;
    assert(!keyboard_replay_space_record_active(&config, &record, 1, &after));
}

static void raw_queue_requires_matching_sample(void) {
    for (unsigned missing = 0; missing < 2; ++missing) {
        keyboard_replay replay;
        open_capture(&replay, 1);
        unsigned char bytes[STOCK_INPUT_MAX_BYTES];
        unsigned length = 777;
        unsigned flags = STOCK_INPUT_ACTIVE | STOCK_INPUT_CURSOR;
        if (missing) flags |= STOCK_INPUT_VIEW;
        assert(linux_stock_capture_with_presentation(&replay.queue, flags, local_cursor,
            missing ? NULL : &local_view, bytes, sizeof bytes, &length) == LINUX_STOCK_INVALID_RECORD);
        assert(length == 777 && !replay.queue.capture_sealed);
        close_replay(&replay);
    }
}

static void default_refuses_remote_view(void) {
    keyboard_replay replay;
    open_capture(&replay, 0);
    stock_input_record record = {
        .flags = STOCK_INPUT_ACTIVE | STOCK_INPUT_CURSOR | STOCK_INPUT_VIEW,
        .capture16 = 38, .view = 1, .fov = 1.0f
    };
    unsigned char bytes[STOCK_INPUT_MAX_BYTES];
    unsigned length = stock_input_encode(&record, bytes, sizeof bytes);
    assert(keyboard_replay_stage(&replay, 2, bytes, length) == KEYBOARD_REPLAY_QUEUE_FAILURE);
    assert(replay.queue_result == LINUX_STOCK_UNSUPPORTED_INPUT);
    close_replay(&replay);
}

int main(void) {
    for (unsigned failure = 0; failure < 4; ++failure) rejected_local_sample(failure);
    for (unsigned field = 0; field < 3; ++field) duplicate_conflict(field);
    for (unsigned failure = 0; failure < 5; ++failure) delayed_pair(failure);
    space_and_view();
    raw_queue_requires_matching_sample();
    default_refuses_remote_view();
    return 0;
}
