#ifndef X4_LINUX_GATE_STOCK_QUEUE_H
#define X4_LINUX_GATE_STOCK_QUEUE_H
/* Offline stock-v3 (wire section 0x53) capture/staging data, NOT a player port.
 * All configured slots are present throughout one fixed session. No X4 calls,
 * hooks, Lua, device sampling, suppression, absent/dead slots or steering.
 * The engine adapter must establish event phase, agreed tick, identity and
 * motion authority before using a returned immutable value as replay input.
 *
 * One mutex serializes every operation. begin_tick opens an agreed capture
 * tick; append_event retains dispatch order and repeated identical events.
 * capture seals the tick once. A complete delayed frame can be published only
 * when its own-slot echo exactly matches the remembered local capture.
 * Missing slots leave the previous publication untouched and permit retry.
 * Overflow, policy violations and conflicting records latch permanent failure.
 * STOP closes admission permanently; destroy only after all callers join.
 */
#include <pthread.h>
#include "../lockstep_stock_record.h"

enum {
    LINUX_STOCK_SLOTS = 8,
    LINUX_STOCK_RING = 32,
    LINUX_STOCK_DELAY_MAX = 30
};

#define LINUX_STOCK_TICK_MAX UINT64_C(8388607)

typedef enum {
    LINUX_STOCK_OK = 1,
    LINUX_STOCK_DUPLICATE = 2,
    LINUX_STOCK_CAPTURED = 3,
    LINUX_STOCK_STAGED = 4,
    LINUX_STOCK_FRAME_READY = 5,
    LINUX_STOCK_NOT_DUE = 6,
    LINUX_STOCK_NOT_CONFIGURED = -1,
    LINUX_STOCK_INVALID_CONFIG = -2,
    LINUX_STOCK_INVALID_TICK = -3,
    LINUX_STOCK_INVALID_SLOT = -4,
    LINUX_STOCK_INVALID_RECORD = -5,
    LINUX_STOCK_UNSUPPORTED_INPUT = -6,
    LINUX_STOCK_OVERFLOW = -7,
    LINUX_STOCK_CONFLICT = -8,
    LINUX_STOCK_ECHO_MISMATCH = -9,
    LINUX_STOCK_RING_COLLISION = -10,
    LINUX_STOCK_LATE_RECORD = -11,
    LINUX_STOCK_FUTURE_RECORD = -12,
    LINUX_STOCK_MISSING_SLOT = -13,
    LINUX_STOCK_MISSING_LOCAL = -14,
    LINUX_STOCK_CAPTURE_UNSEALED = -15,
    LINUX_STOCK_CAPTURE_SEALED = -16,
    LINUX_STOCK_BUFFER_SMALL = -17,
    LINUX_STOCK_NO_FRAME = -18,
    LINUX_STOCK_STOPPED = -19
} linux_stock_result;

/* Explicit whitelist, never a promise that X4 implements these controls.
 * This first adapter contract supports state events and optional stop/SPACE8;
 * ACTIVE is the only supported flag. Extending the wire codec is not enough
 * to extend this policy: ranges, mouse, aim, UI/view and fire are refused. */
typedef struct {
    uint64_t state_ids[8];
    unsigned allowed_flags;
    unsigned allow_stop_action;
    unsigned allow_space8;
} linux_stock_policy;

typedef struct {
    unsigned slots;
    unsigned own_slot;
    unsigned delay;
    unsigned lookahead;
    uint64_t first_capture;
    uint64_t last_capture;
    uint64_t saved_ships[LINUX_STOCK_SLOTS];
    linux_stock_policy policy;
} linux_stock_config;

typedef struct {
    uint64_t capture;
    uint64_t apply;
    unsigned present;
    unsigned lengths[LINUX_STOCK_SLOTS];
    unsigned char bytes[LINUX_STOCK_SLOTS][STOCK_INPUT_MAX_BYTES];
} linux_stock_pending;

typedef struct {
    uint64_t capture;
    unsigned present;
    unsigned length;
    unsigned char bytes[STOCK_INPUT_MAX_BYTES];
} linux_stock_local;

typedef struct {
    uint64_t tick;
    uint64_t capture;
    unsigned slots;
    uint64_t saved_ships[LINUX_STOCK_SLOTS];
    unsigned lengths[LINUX_STOCK_SLOTS];
    unsigned char bytes[LINUX_STOCK_SLOTS][STOCK_INPUT_MAX_BYTES];
} linux_stock_frame;

typedef struct {
    pthread_mutex_t mutex;
    unsigned configured;
    unsigned stopped;
    unsigned tick_open;
    unsigned capture_sealed;
    unsigned frame_ready;
    linux_stock_result failure;
    linux_stock_config config;
    uint64_t tick;
    stock_input_record capture_record;
    linux_stock_local local[LINUX_STOCK_RING];
    linux_stock_pending pending[LINUX_STOCK_RING];
    linux_stock_frame frame;
} linux_stock_queue;

static inline linux_stock_policy linux_stock_keyboard_policy(void) {
    linux_stock_policy policy = {0};
    policy.allowed_flags = STOCK_INPUT_ACTIVE;
    return policy;
}

static inline int linux_stock_allow_state(linux_stock_policy *policy, unsigned id) {
    if (!policy || id >= 512) {
        return 0;
    }
    policy->state_ids[id / 64] |= UINT64_C(1) << (id % 64);
    return 1;
}

static inline int linux_stock_event_supported(
    const linux_stock_policy *policy,
    const stock_input_event *event
) {
    if (event->id >= 512) {
        return 0;
    }
    if (event->kind == STOCK_INPUT_STATE) {
        return event->value <= 1 && ((policy->state_ids[event->id / 64] >> (event->id % 64)) & 1u);
    }
    if (event->kind == STOCK_INPUT_ACTION) {
        return policy->allow_stop_action && event->id == 0x7b && event->value == 0;
    }
    if (event->kind == STOCK_INPUT_CONTEXT) {
        return policy->allow_space8 && event->id == 8 && event->value <= 1;
    }
    return 0;
}

static inline int linux_stock_record_supported(
    const linux_stock_policy *policy,
    const stock_input_record *record
) {
    if (record->flags & ~policy->allowed_flags) {
        return 0;
    }
    for (unsigned event_index = 0; event_index < record->count; ++event_index) {
        if (!linux_stock_event_supported(policy, &record->events[event_index])) {
            return 0;
        }
    }
    return 1;
}

static inline int linux_stock_config_valid(const linux_stock_config *config) {
    if (!config || config->slots < 1 || config->slots > LINUX_STOCK_SLOTS || config->own_slot < 1
        || config->own_slot > config->slots || config->delay < 2 || config->delay > LINUX_STOCK_DELAY_MAX
        || config->lookahead > LINUX_STOCK_DELAY_MAX - 2 || config->delay < config->lookahead + 2
        || !config->first_capture || config->last_capture < config->first_capture || config->last_capture > LINUX_STOCK_TICK_MAX - config->delay
        || (config->policy.allowed_flags & ~STOCK_INPUT_ACTIVE) || config->policy.allow_stop_action > 1
        || config->policy.allow_space8 > 1) {
        return 0;
    }
    for (unsigned slot_index = 0; slot_index < config->slots; ++slot_index) {
        if (!config->saved_ships[slot_index]) {
            return 0;
        }
        for (unsigned previous_slot_index = 0; previous_slot_index < slot_index; ++previous_slot_index) {
            if (config->saved_ships[previous_slot_index] == config->saved_ships[slot_index]) {
                return 0;
            }
        }
    }
    return 1;
}

static inline int linux_stock_queue_init(linux_stock_queue *queue) {
    if (!queue) {
        return 0;
    }
    memset(queue, 0, sizeof(*queue));
    return pthread_mutex_init(&queue->mutex, NULL) == 0;
}

static inline int linux_stock_queue_destroy(linux_stock_queue *queue) {
    return queue && pthread_mutex_destroy(&queue->mutex) == 0;
}

/* Internal helpers: caller holds mutex. */
static inline linux_stock_result linux_stock_admission(const linux_stock_queue *queue) {
    if (queue->stopped) {
        return LINUX_STOCK_STOPPED;
    }
    if (queue->failure) {
        return queue->failure;
    }
    return queue->configured ? LINUX_STOCK_OK : LINUX_STOCK_NOT_CONFIGURED;
}

static inline linux_stock_result linux_stock_fail(
    linux_stock_queue *queue,
    linux_stock_result failure
) {
    if (!queue->failure) {
        queue->failure = failure;
    }
    return queue->failure;
}

static inline int linux_stock_same_config(
    const linux_stock_config *a,
    const linux_stock_config *b
) {
    return a->slots == b->slots && a->own_slot == b->own_slot && a->delay == b->delay && a->lookahead == b->lookahead
        && a->first_capture == b->first_capture && a->last_capture == b->last_capture && !memcmp(a->saved_ships,
        b->saved_ships, a->slots * sizeof(a->saved_ships[0])) && !memcmp(a->policy.state_ids,
        b->policy.state_ids, sizeof(a->policy.state_ids)) && a->policy.allowed_flags == b->policy.allowed_flags
        && a->policy.allow_stop_action == b->policy.allow_stop_action && a->policy.allow_space8 == b->policy.allow_space8;
}

static inline linux_stock_result linux_stock_configure(
    linux_stock_queue *queue,
    const linux_stock_config *config
) {
    pthread_mutex_lock(&queue->mutex);
    linux_stock_result result = linux_stock_admission(queue);
    if (result == LINUX_STOCK_NOT_CONFIGURED || result == LINUX_STOCK_OK) {
        if (!linux_stock_config_valid(config)) {
            result = LINUX_STOCK_INVALID_CONFIG;
        } else if (queue->configured) {
            result = linux_stock_same_config(&queue->config, config) ? LINUX_STOCK_DUPLICATE : LINUX_STOCK_INVALID_CONFIG;
        } else {
            queue->config = *config;
            queue->configured = 1;
            result = LINUX_STOCK_OK;
        }
    }
    pthread_mutex_unlock(&queue->mutex);
    return result;
}

/* No skipped tick or silent discard: finish capture and required publication
 * before advancing. A failed call leaves the current tick/buffer intact. */
static inline linux_stock_result linux_stock_begin_tick(linux_stock_queue *queue, uint64_t tick) {
    pthread_mutex_lock(&queue->mutex);
    linux_stock_result result = linux_stock_admission(queue);
    if (result != LINUX_STOCK_OK) {
        goto done;
    }
    if (queue->tick_open && tick == queue->tick) {
        result = LINUX_STOCK_DUPLICATE;
        goto done;
    }
    if (tick != (queue->tick_open ? queue->tick + 1 : queue->config.first_capture) || tick > queue->config.last_capture + queue->config.delay) {
        result = LINUX_STOCK_INVALID_TICK;
        goto done;
    }
    if (queue->tick_open && queue->tick <= queue->config.last_capture && !queue->capture_sealed) {
        result = LINUX_STOCK_CAPTURE_UNSEALED;
        goto done;
    }
    if (queue->tick_open && queue->tick >= queue->config.first_capture + queue->config.delay
        && (!queue->frame_ready || queue->frame.tick != queue->tick)) {
        result = LINUX_STOCK_MISSING_SLOT;
        goto done;
    }
    queue->tick = tick;
    queue->tick_open = 1;
    queue->capture_sealed = 0;
    memset(&queue->capture_record, 0, sizeof(queue->capture_record));
    queue->capture_record.capture16 = (uint16_t) tick;
done:
    pthread_mutex_unlock(&queue->mutex);
    return result;
}

static inline linux_stock_result linux_stock_append_event(
    linux_stock_queue *queue,
    stock_input_event event
) {
    pthread_mutex_lock(&queue->mutex);
    linux_stock_result result = linux_stock_admission(queue);
    if (result != LINUX_STOCK_OK) {
        goto done;
    }
    if (!queue->tick_open || queue->tick > queue->config.last_capture) {
        result = LINUX_STOCK_NOT_DUE;
        goto done;
    }
    if (queue->capture_sealed) {
        result = linux_stock_fail(queue, LINUX_STOCK_CAPTURE_SEALED);
        goto done;
    }
    if (!linux_stock_event_supported(&queue->config.policy, &event)) {
        result = linux_stock_fail(queue, LINUX_STOCK_UNSUPPORTED_INPUT);
        goto done;
    }
    if (queue->capture_record.count == STOCK_INPUT_MAX_EVENTS) {
        result = linux_stock_fail(queue, LINUX_STOCK_OVERFLOW);
        goto done;
    }
    queue->capture_record.events[queue->capture_record.count++] = event;
done:
    pthread_mutex_unlock(&queue->mutex);
    return result;
}

/* Length/output are untouched on refusal, including short capacity. */
static inline linux_stock_result linux_stock_capture(
    linux_stock_queue *queue,
    unsigned flags,
    unsigned char *output,
    size_t capacity,
    unsigned *length
) {
    pthread_mutex_lock(&queue->mutex);
    linux_stock_result result = linux_stock_admission(queue);
    if (result != LINUX_STOCK_OK) {
        goto done;
    }
    if (!queue->tick_open || queue->tick > queue->config.last_capture) {
        result = LINUX_STOCK_NOT_DUE;
        goto done;
    }
    if (flags & ~queue->config.policy.allowed_flags) {
        result = linux_stock_fail(queue, LINUX_STOCK_UNSUPPORTED_INPUT);
        goto done;
    }
    if (!output || !length || capacity < STOCK_INPUT_HEADER_BYTES + STOCK_INPUT_EVENT_BYTES * queue->capture_record.count) {
        result = LINUX_STOCK_BUFFER_SMALL;
        goto done;
    }
    linux_stock_local *local = &queue->local[queue->tick % LINUX_STOCK_RING];
    if (queue->capture_sealed) {
        if (queue->capture_record.flags != flags) {
            result = linux_stock_fail(queue, LINUX_STOCK_CONFLICT);
            goto done;
        }
        memcpy(output, local->bytes, local->length);
        * length = local->length;
        result = LINUX_STOCK_DUPLICATE;
        goto done;
    }
    if (local->present) {
        result = linux_stock_fail(queue, LINUX_STOCK_RING_COLLISION);
        goto done;
    }
    stock_input_record record = queue->capture_record;
    record.flags = (unsigned char) flags;
    unsigned char encoded[STOCK_INPUT_MAX_BYTES];
    unsigned encoded_length = stock_input_encode(&record, encoded, sizeof(encoded));
    const linux_stock_pending *pending = &queue->pending[(queue->tick + queue->config.delay) % LINUX_STOCK_RING];
    unsigned own = queue->config.own_slot - 1;
    if (pending->apply == queue->tick + queue->config.delay && (pending->present & (1u << own))
        && (pending->lengths[own] != encoded_length || memcmp(pending->bytes[own], encoded,
        encoded_length))) {
        result = linux_stock_fail(queue, LINUX_STOCK_ECHO_MISMATCH);
        goto done;
    }
    local->capture = queue->tick;
    local->length = encoded_length;
    local->present = 1;
    memcpy(local->bytes, encoded, encoded_length);
    queue->capture_record.flags = (unsigned char) flags;
    queue->capture_sealed = 1;
    memcpy(output, encoded, encoded_length);
    * length = encoded_length;
    result = LINUX_STOCK_CAPTURED;
done:
    pthread_mutex_unlock(&queue->mutex);
    return result;
}

/* now is always queue->tick, established by begin_tick. Low16 is unwrapped
 * backward within the bounded delay horizon; future/expired records refuse.
 * The current apply tick remains open until publication; after publication a
 * record for that tick is late. An engine adapter must publish before replay.
 * This operation stages data only, never invokes input handlers. */
static inline linux_stock_result linux_stock_stage(
    linux_stock_queue *queue,
    unsigned slot,
    const unsigned char *bytes,
    size_t length
) {
    pthread_mutex_lock(&queue->mutex);
    linux_stock_result result = linux_stock_admission(queue);
    if (result != LINUX_STOCK_OK) {
        goto done;
    }
    if (!queue->tick_open) {
        result = LINUX_STOCK_INVALID_TICK;
        goto done;
    }
    if (slot < 1 || slot > queue->config.slots) {
        result = LINUX_STOCK_INVALID_SLOT;
        goto done;
    }
    stock_input_record record;
    if (!stock_input_decode(bytes, length, &record)) {
        result = linux_stock_fail(queue, LINUX_STOCK_INVALID_RECORD);
        goto done;
    }
    if (!linux_stock_record_supported(&queue->config.policy, &record)) {
        result = linux_stock_fail(queue, LINUX_STOCK_UNSUPPORTED_INPUT);
        goto done;
    }
    uint64_t back = (uint16_t)(queue->tick - record.capture16);
    if (back > queue->tick || (back && back > UINT16_MAX / 2)) {
        result = LINUX_STOCK_FUTURE_RECORD;
        goto done;
    }
    uint64_t capture = queue->tick - back;
    if (back > queue->config.delay || capture < queue->config.first_capture) {
        result = LINUX_STOCK_LATE_RECORD;
        goto done;
    }
    if (capture > queue->config.last_capture) {
        result = LINUX_STOCK_FUTURE_RECORD;
        goto done;
    }
    uint64_t apply = capture + queue->config.delay;
    if (queue->frame_ready && apply <= queue->frame.tick) {
        result = LINUX_STOCK_LATE_RECORD;
        goto done;
    }
    unsigned index = slot - 1;
    const linux_stock_local *local = &queue->local[capture % LINUX_STOCK_RING];
    if (slot == queue->config.own_slot && local->present && local->capture == capture && (local->length != length
        || memcmp(local->bytes, bytes, length))) {
        result = linux_stock_fail(queue, LINUX_STOCK_ECHO_MISMATCH);
        goto done;
    }
    linux_stock_pending *pending = &queue->pending[apply % LINUX_STOCK_RING];
    if (pending->present && pending->apply != apply) {
        result = linux_stock_fail(queue, LINUX_STOCK_RING_COLLISION);
        goto done;
    }
    if (pending->present & (1u << index)) {
        if (pending->lengths[index] != length || memcmp(pending->bytes[index], bytes, length)) {
            result = linux_stock_fail(queue, slot == queue->config.own_slot ? LINUX_STOCK_ECHO_MISMATCH : LINUX_STOCK_CONFLICT);
        } else {
            result = LINUX_STOCK_DUPLICATE;
        }
        goto done;
    }
    if (!pending->present) {
        pending->capture = capture;
        pending->apply = apply;
    }
    pending->lengths[index] = (unsigned) length;
    memcpy(pending->bytes[index], bytes, length);
    pending->present |= 1u << index;
    result = LINUX_STOCK_STAGED;
done:
    pthread_mutex_unlock(&queue->mutex);
    return result;
}

static inline linux_stock_result linux_stock_publish(linux_stock_queue *queue) {
    pthread_mutex_lock(&queue->mutex);
    linux_stock_result result = linux_stock_admission(queue);
    if (result != LINUX_STOCK_OK) {
        goto done;
    }
    if (!queue->tick_open || queue->tick < queue->config.first_capture + queue->config.delay) {
        result = LINUX_STOCK_NOT_DUE;
        goto done;
    }
    if (queue->frame_ready && queue->frame.tick == queue->tick) {
        result = LINUX_STOCK_DUPLICATE;
        goto done;
    }
    linux_stock_pending *pending = &queue->pending[queue->tick % LINUX_STOCK_RING];
    if (pending->apply != queue->tick || pending->capture != queue->tick - queue->config.delay
        || pending->present != (1u << queue->config.slots) - 1) {
        result = LINUX_STOCK_MISSING_SLOT;
        goto done;
    }
    linux_stock_local *local = &queue->local[pending->capture % LINUX_STOCK_RING];
    unsigned own = queue->config.own_slot - 1;
    if (!local->present || local->capture != pending->capture) {
        result = LINUX_STOCK_MISSING_LOCAL;
        goto done;
    }
    if (local->length != pending->lengths[own] || memcmp(local->bytes, pending->bytes[own],
        local->length)) {
        result = linux_stock_fail(queue, LINUX_STOCK_ECHO_MISMATCH);
        goto done;
    }
    linux_stock_frame next = {0};
    next.tick = queue->tick;
    next.capture = pending->capture;
    next.slots = queue->config.slots;
    memcpy(next.saved_ships, queue->config.saved_ships, next.slots * sizeof(next.saved_ships[0]));
    for (unsigned slot_index = 0; slot_index < next.slots; ++slot_index) {
        next.lengths[slot_index] = pending->lengths[slot_index];
        memcpy(next.bytes[slot_index], pending->bytes[slot_index], next.lengths[slot_index]);
    }
    queue->frame = next;
    queue->frame_ready = 1;
    pending->present = 0;
    local->present = 0;
    result = LINUX_STOCK_FRAME_READY;
done:
    pthread_mutex_unlock(&queue->mutex);
    return result;
}

/* Value copy only; no internal storage pointer or steering permission escapes.
 * Failure/STOP prohibit new reads; previously returned values stay immutable. */
static inline linux_stock_result linux_stock_copy_frame(
    linux_stock_queue *queue,
    uint64_t tick,
    linux_stock_frame *output
) {
    pthread_mutex_lock(&queue->mutex);
    linux_stock_result result = linux_stock_admission(queue);
    if (result == LINUX_STOCK_OK) {
        if (!output || !queue->frame_ready || queue->frame.tick != tick) {
            result = LINUX_STOCK_NO_FRAME;
        } else {
            * output = queue->frame;
            result = LINUX_STOCK_FRAME_READY;
        }
    }
    pthread_mutex_unlock(&queue->mutex);
    return result;
}

typedef struct {
    unsigned configured;
    unsigned stopped;
    unsigned capture_sealed;
    unsigned captured_events;
    linux_stock_result failure;
    uint64_t admitted_tick;
    uint64_t published_tick;
} linux_stock_status;

/* Diagnostic snapshot under the same mutex; never inspect live fields directly. */
static inline int linux_stock_copy_status(linux_stock_queue *queue, linux_stock_status *output) {
    if (!output) {
        return 0;
    }
    pthread_mutex_lock(&queue->mutex);
    linux_stock_status status = {0};
    status.configured = queue->configured;
    status.stopped = queue->stopped;
    status.capture_sealed = queue->capture_sealed;
    status.captured_events = queue->capture_record.count;
    status.failure = queue->failure;
    status.admitted_tick = queue->tick;
    status.published_tick = queue->frame_ready ? queue->frame.tick : 0;
    * output = status;
    pthread_mutex_unlock(&queue->mutex);
    return 1;
}

static inline void linux_stock_stop(linux_stock_queue *queue) {
    pthread_mutex_lock(&queue->mutex);
    queue->stopped = 1;
    pthread_mutex_unlock(&queue->mutex);
}
#endif
