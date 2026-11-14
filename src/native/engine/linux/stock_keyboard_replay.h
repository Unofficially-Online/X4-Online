#ifndef X4_LINUX_STOCK_KEYBOARD_REPLAY_H
#define X4_LINUX_STOCK_KEYBOARD_REPLAY_H

/* Engine-free coordinator for canonical-ship and opt-in two-slot diagnostics. The caller
 * must serialize every operation and prove thread, phase, identity, quiescence,
 * input suppression and callback safety. This does not grant motion authority,
 * call X4 or establish that either ship descriptor is live. In particular, the
 * caller must suppress all 15 selected native handlers on both peers, including
 * events rejected here, and must never forward a rejected event to the engine.
 * Canonical mode records STATE 2..5 for slot 1 and drops them for slot 2.
 * Independent mode additionally admits STATE6..9 and captures the local slot.
 * It validates both complete records before either callback. Default modes emit flags=0;
 * separate opt-ins support active cursor and ordered SPACE context8 transitions. This is not general stock
 * input, native motion authority, or a multi-player controller implementation.
 * Treat the embedded queue and coordinator fields as private; use the functions
 * below, and never reinitialize while any caller or callback is still active. */
#include "gate_stock_queue.h"

typedef enum {
    KEYBOARD_REPLAY_OK = 1,
    KEYBOARD_REPLAY_DUPLICATE = 2,
    KEYBOARD_REPLAY_NOT_DUE = 3,
    KEYBOARD_REPLAY_WAITING = 4,
    KEYBOARD_REPLAY_ARGUMENT = -1,
    KEYBOARD_REPLAY_PHASE = -2,
    KEYBOARD_REPLAY_UNSUPPORTED = -3,
    KEYBOARD_REPLAY_QUEUE_FAILURE = -4,
    KEYBOARD_REPLAY_INVALID_FRAME = -5,
    KEYBOARD_REPLAY_CALLBACK_FAILURE = -6,
    KEYBOARD_REPLAY_STOPPED = -7
} keyboard_replay_result;

typedef enum {
    KEYBOARD_REPLAY_UNCONFIGURED,
    KEYBOARD_REPLAY_NEEDS_PUBLISH,
    KEYBOARD_REPLAY_APPLYING,
    KEYBOARD_REPLAY_CAPTURE,
    KEYBOARD_REPLAY_SEALED,
    KEYBOARD_REPLAY_DRAIN
} keyboard_replay_phase;

typedef struct {
    unsigned own_slot;
    unsigned delay;
    unsigned lookahead;
    uint64_t first_capture;
    uint64_t last_capture;
    uint64_t saved_ship_ids[2];
    unsigned independent_players; /* 0 = canonical (default), 1 = two slots */
    unsigned scripted_throttle; /* Reserve STATE13/14 for the diagnostic. */
    unsigned cursor_active_only; /* Strict ACTIVE-only unless separate SPACE opt-in is set. */
    unsigned space_context8; /* Requires independent + cursor; authenticated startup ACTIVE=1. */
    unsigned view_capture; /* Separate opt-in for authenticated local VIEW samples. */
} keyboard_replay_config;

typedef struct {
    linux_stock_queue queue;
    keyboard_replay_config config;
    keyboard_replay_phase phase;
    keyboard_replay_result failure;
    linux_stock_result queue_result;
    uint64_t tick;
    uint64_t last_applied_tick;
    unsigned initialized;
    unsigned configured;
    unsigned stopped;
    unsigned space_active[2]; /* Delayed simulation states, separate from local window. */
    unsigned space_local_active;
} keyboard_replay;

typedef struct {
    keyboard_replay_phase phase;
    keyboard_replay_result failure;
    linux_stock_result queue_result;
    uint64_t tick;
    uint64_t last_applied_tick;
    unsigned configured;
    unsigned stopped;
} keyboard_replay_status;

/* Return nonzero only if this one already-validated event was applied. An
 * unsuccessful callback may already have effects, so no callback is retried. */
typedef int (*keyboard_replay_apply_fn)(void *context, unsigned id, unsigned char state);
/* The record is an immutable callback-lifetime copy. A slot callback receives
 * one whole record even when it contains no events, so the owner can update
 * that player exactly once. A failed callback may already have effects. */
typedef int (*keyboard_replay_apply_slot_fn)(void *context, uint64_t apply_tick, unsigned slot,
                                             uint64_t saved_ship_id, const stock_input_record *record);
/* Optional pre-application stage: both immutable records have passed all queue,
 * echo and tick checks. Any failure poisons this frame before either slot runs. */
typedef int (*keyboard_replay_prepare_slots_fn)(void *context, uint64_t apply_tick,
                                                const uint64_t saved_ship_ids[2],
                                                const stock_input_record records[2]);

static inline int keyboard_replay_init(keyboard_replay *replay) {
    if (!replay) {
        return 0;
    }
    memset(replay, 0, sizeof(*replay));
    if (!linux_stock_queue_init(&replay->queue)) {
        return 0;
    }
    replay->initialized = 1;
    return 1;
}

/* Destroy only after STOP and after the owner joins all callers. */
static inline int keyboard_replay_destroy(keyboard_replay *replay) {
    if (!replay || !replay->initialized || !replay->stopped ||
        (replay->config.independent_players && replay->phase == KEYBOARD_REPLAY_APPLYING)) {
        return 0;
    }
    if (!linux_stock_queue_destroy(&replay->queue)) {
        return 0;
    }
    replay->initialized = 0;
    return 1;
}

static inline keyboard_replay_result keyboard_replay_latch(
        keyboard_replay *replay, keyboard_replay_result failure) {
    if (!replay->failure) {
        replay->failure = failure;
    }
    return replay->failure;
}

static inline keyboard_replay_result keyboard_replay_ready(const keyboard_replay *replay) {
    if (!replay || !replay->initialized) {
        return KEYBOARD_REPLAY_ARGUMENT;
    }
    if (replay->stopped) {
        return KEYBOARD_REPLAY_STOPPED;
    }
    if (replay->failure) {
        return replay->failure;
    }
    if (!replay->configured) {
        return KEYBOARD_REPLAY_PHASE;
    }
    return KEYBOARD_REPLAY_OK;
}

/* STATE10..12 and 15 onward are never admitted. */
static inline int keyboard_replay_allowed_state(const keyboard_replay_config *config, unsigned id) {
    return (id >= 2 && id <= (config->independent_players ? 9u : 5u)) ||
        (config->independent_players && !config->scripted_throttle && (id == 13 || id == 14));
}

/* CONTEXT8 is never a STATE8 key. Only an explicit SPACE configuration
 * admits it, and only the whole-record native owner may apply its effects. */
static inline int keyboard_replay_is_space_event(const keyboard_replay_config *config,
                                                  const stock_input_event *event) {
    return config && config->space_context8 && event && event->kind == STOCK_INPUT_CONTEXT &&
        event->id == 8 && event->value <= 1;
}

static inline int keyboard_replay_space_local_state(const keyboard_replay *replay, unsigned *active) {
    if (!active || keyboard_replay_ready(replay) != KEYBOARD_REPLAY_OK || !replay->config.space_context8) return 0;
    *active = replay->space_local_active;
    return 1;
}
static inline int keyboard_replay_space_slot_state(const keyboard_replay *replay, unsigned slot, unsigned *active) {
    if (!active || slot < 1 || slot > 2 || keyboard_replay_ready(replay) != KEYBOARD_REPLAY_OK ||
        !replay->config.space_context8) return 0;
    *active = replay->space_active[slot - 1];
    return 1;
}

/* Pure semantic validation after wire decode. A record describes the final
 * active state; its ordered CONTEXT8 edges must reach that state from before.
 * STATE events keep their own namespace and never alter active state. */
static inline int keyboard_replay_space_record_active(const keyboard_replay_config *config,
        const stock_input_record *record, unsigned before, unsigned *after) {
    if (!config || !config->space_context8 || !record || !after || before > 1 ||
        record->count > STOCK_INPUT_MAX_EVENTS ||
        (record->flags & ~(STOCK_INPUT_ACTIVE | STOCK_INPUT_CURSOR |
            (config->view_capture ? STOCK_INPUT_VIEW : 0)))) return 0;
    if (record->steer) return 0;
    if (record->flags & STOCK_INPUT_VIEW) {
        if (record->view > 3 || !isfinite(record->fov)) return 0;
    } else if (record->view || record->target || stock_input_float_bits(record->fov)) return 0;
    for (unsigned axis = 0; axis < 3; ++axis)
        if (stock_input_float_bits(record->aim[axis])) return 0;
    unsigned active = before;
    for (unsigned index = 0; index < record->count; ++index) {
        const stock_input_event *event = &record->events[index];
        if (keyboard_replay_is_space_event(config, event)) {
            if (event->value == active) return 0;
            active = event->value;
        } else if (event->kind != STOCK_INPUT_STATE ||
                   !keyboard_replay_allowed_state(config, event->id) || event->value > 1) return 0;
    }
    if (!!(record->flags & STOCK_INPUT_ACTIVE) != active || (!active && (record->flags & STOCK_INPUT_CURSOR))) return 0;
    for (unsigned axis = 0; axis < 3; ++axis) {
        if (!isfinite(record->cursor[axis]) || fabsf(record->cursor[axis]) > 16.0f ||
            ((!active || !(record->flags & STOCK_INPUT_CURSOR)) && stock_input_float_bits(record->cursor[axis]))) return 0;
    }
    *after = active;
    return 1;
}

/* Shared whole-record admission for both coordinator and native P2 adapter.
 * Re-decode the wire representation so absent fields, float bits and all
 * event values receive the same canonical checks in both paths. */
static inline int keyboard_replay_record_valid(const keyboard_replay *replay,
        const stock_input_record *record, unsigned slot) {
    if (!replay || !record || slot < 1 || slot > 2 || record->count > STOCK_INPUT_MAX_EVENTS)
        return 0;
    for (unsigned index = 0; index < record->count; ++index)
        if (record->events[index].kind > STOCK_INPUT_CONTEXT || record->events[index].id >= 512)
            return 0;
    unsigned char encoded[STOCK_INPUT_MAX_BYTES];
    stock_input_record decoded;
    unsigned length = stock_input_encode(record, encoded, sizeof encoded);
    if (!length || !stock_input_decode(encoded, length, &decoded) ||
        !linux_stock_record_supported(&replay->queue.config.policy, &decoded)) return 0;
    const keyboard_replay_config *config = &replay->config;
    if (config->space_context8) {
        unsigned after;
        return keyboard_replay_space_record_active(config, &decoded, replay->space_active[slot - 1], &after);
    }
    unsigned view_flags = config->view_capture ? STOCK_INPUT_VIEW : 0;
    if (config->cursor_active_only ?
            (!(decoded.flags & STOCK_INPUT_ACTIVE) ||
             (decoded.flags & ~(STOCK_INPUT_ACTIVE | STOCK_INPUT_CURSOR | view_flags))) :
            (decoded.flags & ~view_flags) != 0) return 0;
    for (unsigned index = 0; index < decoded.count; ++index) {
        const stock_input_event *event = &decoded.events[index];
        if (event->kind != STOCK_INPUT_STATE ||
            !keyboard_replay_allowed_state(config, event->id) || event->value > 1) return 0;
    }
    return 1;
}

/* A callback cannot invoke a second mutation while its effects are in flight.
 * The independent mode latches the failure so the outer publish cannot proceed
 * to the next slot even when the callback ignores the returned phase error. */
static inline keyboard_replay_result keyboard_replay_reentrant(keyboard_replay *replay) {
    if (replay->phase != KEYBOARD_REPLAY_APPLYING) {
        return KEYBOARD_REPLAY_OK;
    }
    if (replay->config.independent_players) {
        keyboard_replay_latch(replay, KEYBOARD_REPLAY_CALLBACK_FAILURE);
    }
    return KEYBOARD_REPLAY_PHASE;
}

static inline keyboard_replay_result keyboard_replay_configure(
        keyboard_replay *replay, const keyboard_replay_config *config) {
    if (!replay || !replay->initialized || !config) {
        return KEYBOARD_REPLAY_ARGUMENT;
    }
    if (replay->stopped) {
        return KEYBOARD_REPLAY_STOPPED;
    }
    if (replay->failure) {
        return replay->failure;
    }
    if (replay->config.independent_players && keyboard_replay_reentrant(replay) != KEYBOARD_REPLAY_OK) {
        return KEYBOARD_REPLAY_PHASE;
    }
    if (replay->configured || config->first_capture < 3 || config->independent_players > 1 ||
        config->scripted_throttle > 1 || (config->scripted_throttle && !config->independent_players) ||
        config->cursor_active_only > 1 || (config->cursor_active_only && !config->independent_players) ||
        config->view_capture > 1 || (config->view_capture && !config->independent_players) ||
        config->space_context8 > 1 || (config->space_context8 && (!config->independent_players || !config->cursor_active_only))) {
        return KEYBOARD_REPLAY_ARGUMENT;
    }
    linux_stock_config queue_config = {0};
    queue_config.slots = 2;
    queue_config.own_slot = config->own_slot;
    queue_config.delay = config->delay;
    queue_config.lookahead = config->lookahead;
    queue_config.first_capture = config->first_capture;
    queue_config.last_capture = config->last_capture;
    queue_config.saved_ships[0] = config->saved_ship_ids[0];
    queue_config.saved_ships[1] = config->saved_ship_ids[1];
    queue_config.policy = linux_stock_keyboard_policy();
    queue_config.policy.allowed_flags = config->cursor_active_only ? STOCK_INPUT_ACTIVE | STOCK_INPUT_CURSOR : 0;
    if (config->view_capture) queue_config.policy.allowed_flags |= STOCK_INPUT_VIEW;
    queue_config.policy.allow_space8 = config->space_context8;
    for (unsigned id = 2; id <= (config->independent_players ? 9u : 5u); ++id) {
        (void)linux_stock_allow_state(&queue_config.policy, id);
    }
    if (config->independent_players && !config->scripted_throttle) {
        (void)linux_stock_allow_state(&queue_config.policy, 13);
        (void)linux_stock_allow_state(&queue_config.policy, 14);
    }
    replay->queue_result = linux_stock_configure(&replay->queue, &queue_config);
    if (replay->queue_result != LINUX_STOCK_OK) {
        return KEYBOARD_REPLAY_ARGUMENT;
    }
    replay->config = *config;
    replay->space_local_active = replay->space_active[0] = replay->space_active[1] = 1;
    replay->configured = 1;
    return KEYBOARD_REPLAY_OK;
}

/* Called at admitted tick before the post-join publish point and input pass. */
static inline keyboard_replay_result keyboard_replay_begin(keyboard_replay *replay, uint64_t tick) {
    keyboard_replay_result ready = keyboard_replay_ready(replay);
    if (ready != KEYBOARD_REPLAY_OK) {
        return ready;
    }
    if (keyboard_replay_reentrant(replay) != KEYBOARD_REPLAY_OK) {
        return KEYBOARD_REPLAY_PHASE;
    }
    if (tick == replay->tick && replay->phase != KEYBOARD_REPLAY_UNCONFIGURED) {
        return KEYBOARD_REPLAY_DUPLICATE;
    }
    if (replay->tick && replay->tick >= replay->config.first_capture + replay->config.delay &&
        replay->last_applied_tick != replay->tick) {
        return KEYBOARD_REPLAY_PHASE;
    }
    replay->queue_result = linux_stock_begin_tick(&replay->queue, tick);
    if (replay->queue_result != LINUX_STOCK_OK) {
        return KEYBOARD_REPLAY_PHASE;
    }
    replay->tick = tick;
    replay->phase = KEYBOARD_REPLAY_NEEDS_PUBLISH;
    return KEYBOARD_REPLAY_OK;
}

/* Selected native calls are intercepted on both peers. A caller must keep them
 * suppressed even when this function fails or the event is unsupported. */
static inline keyboard_replay_result keyboard_replay_consume(
        keyboard_replay *replay, unsigned kind, unsigned id, unsigned state) {
    keyboard_replay_result ready = keyboard_replay_ready(replay);
    if (ready != KEYBOARD_REPLAY_OK) {
        return ready;
    }
    if (replay->config.independent_players && keyboard_replay_reentrant(replay) != KEYBOARD_REPLAY_OK) {
        return KEYBOARD_REPLAY_PHASE;
    }
    int space = replay->config.space_context8 && kind == STOCK_INPUT_CONTEXT && id == 8;
    if (state > 1 || (space ? state == replay->space_local_active :
            kind != STOCK_INPUT_STATE || !keyboard_replay_allowed_state(&replay->config, id))) {
        return keyboard_replay_latch(replay, KEYBOARD_REPLAY_UNSUPPORTED);
    }
    if (replay->phase != KEYBOARD_REPLAY_CAPTURE) {
        return keyboard_replay_latch(replay, KEYBOARD_REPLAY_PHASE);
    }
    if (!replay->config.independent_players && replay->config.own_slot == 2) {
        return KEYBOARD_REPLAY_OK;
    }
    stock_input_event event = {(unsigned char)kind, (uint16_t)id, state};
    replay->queue_result = linux_stock_append_event(&replay->queue, event);
    if (replay->queue_result != LINUX_STOCK_OK) {
        return keyboard_replay_latch(replay, KEYBOARD_REPLAY_QUEUE_FAILURE);
    }
    if (space) replay->space_local_active = state;
    return KEYBOARD_REPLAY_OK;
}

/* Seal events, cursor and VIEW together. NULL VIEW explicitly means absent
 * for this capture, never reuse of the previous generation's selection/FOV.
 * Native admission must decide where absence is expected or a fatal error. */
static inline keyboard_replay_result keyboard_replay_seal_presentation(
        keyboard_replay *replay, const float cursor[3], const linux_stock_view_sample *view,
        unsigned char *output, size_t capacity, unsigned *length) {
    keyboard_replay_result ready = keyboard_replay_ready(replay);
    if (ready != KEYBOARD_REPLAY_OK) {
        return ready;
    }
    if (keyboard_replay_reentrant(replay) != KEYBOARD_REPLAY_OK) {
        return KEYBOARD_REPLAY_PHASE;
    }
    if (replay->phase != KEYBOARD_REPLAY_CAPTURE && replay->phase != KEYBOARD_REPLAY_SEALED) {
        return KEYBOARD_REPLAY_PHASE;
    }
    unsigned cursor_required = replay->config.cursor_active_only &&
        (!replay->config.space_context8 || replay->space_local_active);
    if (!!cursor != !!cursor_required) return KEYBOARD_REPLAY_ARGUMENT;
    if (view && !replay->config.view_capture) return KEYBOARD_REPLAY_ARGUMENT;
    unsigned flags = cursor_required ? STOCK_INPUT_ACTIVE | STOCK_INPUT_CURSOR : 0;
    if (view) flags |= STOCK_INPUT_VIEW;
    replay->queue_result = linux_stock_capture_with_presentation(
        &replay->queue, flags, cursor, view, output, capacity, length);
    if (replay->queue_result == LINUX_STOCK_BUFFER_SMALL) {
        return KEYBOARD_REPLAY_ARGUMENT;
    }
    if (replay->queue_result == LINUX_STOCK_DUPLICATE) {
        return KEYBOARD_REPLAY_DUPLICATE;
    }
    if (replay->queue_result != LINUX_STOCK_CAPTURED) {
        return keyboard_replay_latch(replay, KEYBOARD_REPLAY_QUEUE_FAILURE);
    }
    replay->phase = KEYBOARD_REPLAY_SEALED;
    return KEYBOARD_REPLAY_OK;
}

static inline keyboard_replay_result keyboard_replay_seal_cursor(
        keyboard_replay *replay, const float cursor[3], unsigned char *output, size_t capacity, unsigned *length) {
    return keyboard_replay_seal_presentation(replay, cursor, NULL, output, capacity, length);
}

static inline keyboard_replay_result keyboard_replay_seal(
        keyboard_replay *replay, unsigned char *output, size_t capacity, unsigned *length) {
    return keyboard_replay_seal_cursor(replay, NULL, output, capacity, length);
}

static inline keyboard_replay_result keyboard_replay_stage(
        keyboard_replay *replay, unsigned slot, const unsigned char *bytes, size_t length) {
    keyboard_replay_result ready = keyboard_replay_ready(replay);
    if (ready != KEYBOARD_REPLAY_OK) {
        return ready;
    }
    if (keyboard_replay_reentrant(replay) != KEYBOARD_REPLAY_OK ||
        replay->phase == KEYBOARD_REPLAY_UNCONFIGURED) {
        return KEYBOARD_REPLAY_PHASE;
    }
    replay->queue_result = linux_stock_stage(&replay->queue, slot, bytes, length);
    if (replay->queue_result == LINUX_STOCK_DUPLICATE) {
        return KEYBOARD_REPLAY_DUPLICATE;
    }
    if (replay->queue_result != LINUX_STOCK_STAGED) {
        return keyboard_replay_latch(replay, KEYBOARD_REPLAY_QUEUE_FAILURE);
    }
    return KEYBOARD_REPLAY_OK;
}

/* Shared admission, queue/echo validation and full two-record validation.
 * No application callback may run until this function succeeds. */
static inline keyboard_replay_result keyboard_replay_prepare_frame(
        keyboard_replay *replay, uint64_t tick, int has_callback, stock_input_record records[2]) {
    if (!tick || tick != replay->tick) {
        return KEYBOARD_REPLAY_PHASE;
    }
    if (replay->phase != KEYBOARD_REPLAY_NEEDS_PUBLISH) {
        return KEYBOARD_REPLAY_DUPLICATE;
    }
    if (tick < replay->config.first_capture + replay->config.delay) {
        replay->phase = tick <= replay->config.last_capture
            ? KEYBOARD_REPLAY_CAPTURE : KEYBOARD_REPLAY_DRAIN;
        return KEYBOARD_REPLAY_NOT_DUE;
    }
    if (!has_callback) {
        return KEYBOARD_REPLAY_ARGUMENT;
    }
    replay->queue_result = linux_stock_publish(&replay->queue);
    if (replay->queue_result == LINUX_STOCK_MISSING_SLOT) {
        return KEYBOARD_REPLAY_WAITING;
    }
    if (replay->queue_result != LINUX_STOCK_FRAME_READY) {
        return keyboard_replay_latch(replay, KEYBOARD_REPLAY_QUEUE_FAILURE);
    }
    linux_stock_frame frame;
    replay->queue_result = linux_stock_copy_frame(&replay->queue, tick, &frame);
    if (replay->queue_result != LINUX_STOCK_FRAME_READY ||
        frame.tick != tick || frame.capture != tick - replay->config.delay || frame.slots != 2 ||
        frame.saved_ships[0] != replay->config.saved_ship_ids[0] ||
        frame.saved_ships[1] != replay->config.saved_ship_ids[1]) {
        return keyboard_replay_latch(replay, KEYBOARD_REPLAY_INVALID_FRAME);
    }
    for (unsigned slot = 0; slot < 2; ++slot) {
        if (!stock_input_decode(frame.bytes[slot], frame.lengths[slot], &records[slot]) ||
            records[slot].capture16 != (uint16_t)frame.capture) {
            return keyboard_replay_latch(replay, KEYBOARD_REPLAY_INVALID_FRAME);
        }
        if (!keyboard_replay_record_valid(replay, &records[slot], slot + 1))
            return keyboard_replay_latch(replay, KEYBOARD_REPLAY_INVALID_FRAME);
    }
    if (!replay->config.independent_players && records[1].count != 0) {
        return keyboard_replay_latch(replay, KEYBOARD_REPLAY_INVALID_FRAME);
    }
    return KEYBOARD_REPLAY_OK;
}

/* The legacy entry point continues to dispatch slot 1 events individually. */
static inline keyboard_replay_result keyboard_replay_publish(
        keyboard_replay *replay, uint64_t tick, keyboard_replay_apply_fn apply, void *context) {
    keyboard_replay_result ready = keyboard_replay_ready(replay);
    if (ready != KEYBOARD_REPLAY_OK) {
        return ready;
    }
    if (keyboard_replay_reentrant(replay) != KEYBOARD_REPLAY_OK || replay->config.independent_players) {
        return KEYBOARD_REPLAY_PHASE;
    }
    stock_input_record records[2];
    keyboard_replay_result prepared = keyboard_replay_prepare_frame(replay, tick, apply != NULL, records);
    if (prepared != KEYBOARD_REPLAY_OK) {
        return prepared;
    }
    replay->phase = KEYBOARD_REPLAY_APPLYING;
    for (unsigned index = 0; index < records[0].count; ++index) {
        const stock_input_event *event = &records[0].events[index];
        if (!apply(context, event->id, (unsigned char)event->value)) {
            return keyboard_replay_latch(replay, KEYBOARD_REPLAY_CALLBACK_FAILURE);
        }
        if (replay->stopped || replay->failure) {
            return replay->stopped ? KEYBOARD_REPLAY_STOPPED : replay->failure;
        }
    }
    replay->last_applied_tick = tick;
    replay->phase = tick <= replay->config.last_capture ? KEYBOARD_REPLAY_CAPTURE : KEYBOARD_REPLAY_DRAIN;
    return KEYBOARD_REPLAY_OK;
}

/* Independent mode always calls slot 1 then slot 2, including empty records.
 * No callback is retried after failure, reentry, or STOP during application. */
static inline keyboard_replay_result keyboard_replay_publish_slots_prepared(
        keyboard_replay *replay, uint64_t tick, keyboard_replay_prepare_slots_fn prepare,
        keyboard_replay_apply_slot_fn apply, void *context) {
    keyboard_replay_result ready = keyboard_replay_ready(replay);
    if (ready != KEYBOARD_REPLAY_OK) {
        return ready;
    }
    if (keyboard_replay_reentrant(replay) != KEYBOARD_REPLAY_OK || !replay->config.independent_players) {
        return KEYBOARD_REPLAY_PHASE;
    }
    stock_input_record records[2];
    keyboard_replay_result prepared = keyboard_replay_prepare_frame(replay, tick, apply != NULL, records);
    if (prepared != KEYBOARD_REPLAY_OK) {
        return prepared;
    }
    replay->phase = KEYBOARD_REPLAY_APPLYING;
    if (prepare && (!prepare(context, tick, replay->config.saved_ship_ids, records) ||
                    replay->stopped || replay->failure)) {
        keyboard_replay_latch(replay, KEYBOARD_REPLAY_CALLBACK_FAILURE);
        replay->phase = KEYBOARD_REPLAY_DRAIN;
        return replay->stopped ? KEYBOARD_REPLAY_STOPPED : replay->failure;
    }
    for (unsigned index = 0; index < 2; ++index) {
        int applied = apply(context, tick, index + 1, replay->config.saved_ship_ids[index], &records[index]);
        if (!applied || replay->stopped || replay->failure) {
            keyboard_replay_latch(replay, KEYBOARD_REPLAY_CALLBACK_FAILURE);
            replay->phase = KEYBOARD_REPLAY_DRAIN;
            return replay->stopped ? KEYBOARD_REPLAY_STOPPED : replay->failure;
        }
    }
    if (replay->config.space_context8) {
        /* Commit only after both callbacks succeeded. Failures already latch
         * permanent failure; this is not a rollback of native side effects. */
        replay->space_active[0] = !!(records[0].flags & STOCK_INPUT_ACTIVE);
        replay->space_active[1] = !!(records[1].flags & STOCK_INPUT_ACTIVE);
    }
    replay->last_applied_tick = tick;
    replay->phase = tick <= replay->config.last_capture ? KEYBOARD_REPLAY_CAPTURE : KEYBOARD_REPLAY_DRAIN;
    return KEYBOARD_REPLAY_OK;
}

/* Existing callers retain their original two-callback behavior. */
static inline keyboard_replay_result keyboard_replay_publish_slots(
        keyboard_replay *replay, uint64_t tick, keyboard_replay_apply_slot_fn apply, void *context) {
    return keyboard_replay_publish_slots_prepared(replay, tick, NULL, apply, context);
}

/* STOP closes the data coordinator; held-key release and hook cleanup belong to
 * the owner, which must not claim they happened merely because this succeeded. */
static inline void keyboard_replay_stop(keyboard_replay *replay) {
    if (!replay || !replay->initialized || replay->stopped) {
        return;
    }
    if (replay->configured && replay->config.independent_players &&
        replay->phase == KEYBOARD_REPLAY_APPLYING) {
        keyboard_replay_latch(replay, KEYBOARD_REPLAY_CALLBACK_FAILURE);
    }
    replay->stopped = 1;
    linux_stock_stop(&replay->queue);
}

static inline int keyboard_replay_get_status(const keyboard_replay *replay, keyboard_replay_status *out) {
    if (!replay || !replay->initialized || !out) {
        return 0;
    }
    *out = (keyboard_replay_status){replay->phase, replay->failure, replay->queue_result,
        replay->tick, replay->last_applied_tick, replay->configured, replay->stopped};
    return 1;
}
#endif
