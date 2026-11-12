#ifndef LINUX_GATE_PILOT_RECEIVER_H
#define LINUX_GATE_PILOT_RECEIVER_H

/* Internal preparation backend, not a Lua capability or an active pilot port.
 *
 * Configuration/staging/publication prepare record-v1 data for all-present,
 * non-stock slots. Absent, dropped, rejoin, death, stock and UI semantics
 * are not implemented here. No function calls X4, takes motion authority, suppresses local
 * controls or installs a hook. All known mode/button/target fields survive
 * exactly; none is acted on. apply() always returns APPLY_NOT_IMPLEMENTED.
 *
 * One mutex serializes staging, STOP and frame publication. A frame is built
 * completely in a temporary before publication; readers receive a value copy,
 * never a pointer into mutable staging/current storage. A retained copy stays
 * immutable across later publication and STOP, but is NOT permission to steer.
 * Unlike the Windows caller's capture-before-echo ordering assumption, this
 * internal API accepts either order and latches contradictory own echoes.
 * Once latched, staging, publication and frame reads refuse for the object's
 * lifetime, including after STOP. Previously returned copies cannot be revoked.
 * Publication without a remembered local record does not validate our echo;
 * runtime integration must capture and validate it before any actuation.
 * Runtime integration must supply its own session/UI/identity admission and
 * before-release ordering, and refuse unsupported session features explicitly.
 */
#include <pthread.h>
#include <stdio.h>
#include "../lockstep_pilot_record.h"

typedef enum {
    LINUX_PILOT_RECEIVER_CONFIGURED = 1,
    LINUX_PILOT_RECEIVER_STAGED,
    LINUX_PILOT_RECEIVER_DUPLICATE,
    LINUX_PILOT_RECEIVER_FRAME_READY,
    LINUX_PILOT_RECEIVER_LOCAL_REMEMBERED,
    LINUX_PILOT_RECEIVER_NOT_CONFIGURED = -1,
    LINUX_PILOT_RECEIVER_INVALID_CONFIG = -2,
    LINUX_PILOT_RECEIVER_INVALID_RECORD = -3,
    LINUX_PILOT_RECEIVER_INVALID_SLOT = -4,
    LINUX_PILOT_RECEIVER_INVALID_TICK = -5,
    LINUX_PILOT_RECEIVER_LATE_RECORD = -6,
    LINUX_PILOT_RECEIVER_CONFLICT = -7,
    LINUX_PILOT_RECEIVER_ECHO_MISMATCH = -8,
    LINUX_PILOT_RECEIVER_RING_CONFLICT = -9,
    LINUX_PILOT_RECEIVER_MISSING_RECORD = -10,
    LINUX_PILOT_RECEIVER_DELAY_MISMATCH = -11,
    LINUX_PILOT_RECEIVER_LOCAL_RING_FULL = -12,
    LINUX_PILOT_RECEIVER_WRONG_SOURCE = -13,
    LINUX_PILOT_RECEIVER_NO_FRAME = -14,
    LINUX_PILOT_RECEIVER_STOPPED = -15,
    LINUX_PILOT_RECEIVER_APPLY_NOT_IMPLEMENTED = -16
} linux_pilot_receiver_result;

typedef struct {
    uint64_t tick;
    unsigned present;
    unsigned char bytes[PILOT_SLOTS][PILOT_RECORD_BYTES];
} linux_pilot_receiver_staged;

typedef struct {
    uint64_t capture;
    uint64_t apply;
    unsigned used;
    unsigned char bytes[PILOT_RECORD_BYTES];
} linux_pilot_receiver_local;

typedef struct {
    uint64_t tick;
    unsigned count;
    unsigned present;
    unsigned in_window;
    unsigned steering_active; /* Always zero in this backend. */
    uint64_t saved[PILOT_SLOTS];
    pilot_record records[PILOT_SLOTS];
    unsigned char bytes[PILOT_SLOTS][PILOT_RECORD_BYTES];
} linux_pilot_receiver_frame;

typedef struct {
    pthread_mutex_t lock;
    unsigned configured;
    unsigned stopped;
    unsigned echo_failure;
    unsigned count;
    unsigned own_slot;
    unsigned source;
    unsigned delay;
    unsigned lookahead;
    uint64_t start;
    uint64_t stop;
    uint64_t saved[PILOT_SLOTS];
    linux_pilot_receiver_staged ring[PILOT_RING];
    linux_pilot_receiver_local locals[PILOT_LOCAL_RING];
    linux_pilot_receiver_frame frame;
    unsigned frame_ready;
    uint64_t staged;
    uint64_t duplicates;
    uint64_t conflicts;
    uint64_t missing;
    uint64_t late;
    uint64_t publications;
    uint64_t apply_refusals;
    linux_pilot_receiver_result last_result;
} linux_pilot_receiver;

static inline int linux_pilot_receiver_init(linux_pilot_receiver *receiver) {
    memset(receiver, 0, sizeof(*receiver));
    receiver->last_result = LINUX_PILOT_RECEIVER_NOT_CONFIGURED;
    return pthread_mutex_init(&receiver->lock, NULL) == 0;
}

/* Ordinary object lifetime: destroy only after every caller has joined. There
 * is no reset/rearm after STOP; create a new object for a new session. */
static inline int linux_pilot_receiver_destroy(linux_pilot_receiver *receiver) {
    return pthread_mutex_destroy(&receiver->lock) == 0;
}

/* Caller holds receiver->lock. */
static inline linux_pilot_receiver_result linux_pilot_receiver_admission_locked(linux_pilot_receiver *receiver) {
    if (receiver->stopped) return LINUX_PILOT_RECEIVER_STOPPED;
    if (!receiver->configured) return LINUX_PILOT_RECEIVER_NOT_CONFIGURED;
    if (receiver->echo_failure) return LINUX_PILOT_RECEIVER_ECHO_MISMATCH;
    return LINUX_PILOT_RECEIVER_CONFIGURED;
}

static inline linux_pilot_receiver_result linux_pilot_receiver_configure(
    linux_pilot_receiver *receiver, const char *manifest, unsigned source, unsigned delay,
    uint64_t start, uint64_t stop, unsigned lookahead, unsigned own_slot) {
    uint64_t saved[PILOT_SLOTS] = {0};
    unsigned count = 0;
    int valid = pilot_parse_manifest(manifest, saved, &count) &&
        pilot_config_valid(count, source, delay, start, stop, 0) &&
        lookahead <= PILOT_DELAY_MAX && pilot_delay_covers(source, delay, lookahead) &&
        own_slot >= 1 && own_slot <= count;
    pthread_mutex_lock(&receiver->lock);
    linux_pilot_receiver_result result = LINUX_PILOT_RECEIVER_INVALID_CONFIG;
    if (receiver->stopped) result = LINUX_PILOT_RECEIVER_STOPPED;
    else if (receiver->echo_failure) result = LINUX_PILOT_RECEIVER_ECHO_MISMATCH;
    else if (valid && receiver->configured) {
        int same = receiver->count == count && receiver->source == source && receiver->delay == delay &&
            receiver->start == start && receiver->stop == stop && receiver->lookahead == lookahead &&
            receiver->own_slot == own_slot && !memcmp(receiver->saved, saved, sizeof(saved));
        if (same) result = LINUX_PILOT_RECEIVER_CONFIGURED;
    } else if (valid) {
        receiver->count = count;
        receiver->source = source;
        receiver->delay = delay;
        receiver->start = start;
        receiver->stop = stop;
        receiver->lookahead = lookahead;
        receiver->own_slot = own_slot;
        memcpy(receiver->saved, saved, sizeof(saved));
        receiver->configured = 1;
        result = LINUX_PILOT_RECEIVER_CONFIGURED;
    }
    receiver->last_result = result;
    pthread_mutex_unlock(&receiver->lock);
    return result;
}

/* Complete local wire records only; capture itself is outside this module.
 * This is Windows' pending-local ring rule, needed for exact own-slot echoes. */
static inline linux_pilot_receiver_result linux_pilot_receiver_remember_local(
    linux_pilot_receiver *receiver, uint64_t capture, const unsigned char *bytes, size_t length) {
    pilot_record decoded;
    int valid = bytes && length == PILOT_RECORD_BYTES && pilot_decode(bytes, &decoded);
    pthread_mutex_lock(&receiver->lock);
    linux_pilot_receiver_result result = linux_pilot_receiver_admission_locked(receiver);
    if (result == LINUX_PILOT_RECEIVER_CONFIGURED) {
        if (receiver->source != PILOT_SOURCE_WIRE) result = LINUX_PILOT_RECEIVER_WRONG_SOURCE;
        else if (!valid) result = LINUX_PILOT_RECEIVER_INVALID_RECORD;
        else if (!capture || capture > PILOT_TICK_MAX || decoded.capture16 != (uint16_t)capture)
            result = LINUX_PILOT_RECEIVER_INVALID_TICK;
        else {
            linux_pilot_receiver_local *local = &receiver->locals[capture % PILOT_LOCAL_RING];
            uint64_t frame_tick = receiver->frame_ready ? receiver->frame.tick : 0;
            uint64_t apply = capture + receiver->delay;
            const linux_pilot_receiver_staged *entry = &receiver->ring[apply % PILOT_RING];
            unsigned own_bit = 1u << (receiver->own_slot - 1);
            int staged_conflict = entry->tick == apply && (entry->present & own_bit) &&
                memcmp(entry->bytes[receiver->own_slot - 1], bytes, PILOT_RECORD_BYTES);
            int published_conflict = receiver->frame_ready && receiver->frame.tick == apply &&
                (receiver->frame.present & own_bit) &&
                memcmp(receiver->frame.bytes[receiver->own_slot - 1], bytes, PILOT_RECORD_BYTES);
            /* The API does not assume capture-before-echo ordering. Once two
             * claims of our own record disagree, no later frame is admissible.
             * A previously returned value copy is data only, not actuation. */
            if (staged_conflict || published_conflict) {
                receiver->echo_failure = 1;
                result = LINUX_PILOT_RECEIVER_ECHO_MISMATCH;
            } else if (capture < frame_tick) result = LINUX_PILOT_RECEIVER_INVALID_TICK;
            else if (local->used && local->apply > frame_tick) result = LINUX_PILOT_RECEIVER_LOCAL_RING_FULL;
            else {
                local->capture = capture;
                local->apply = capture + receiver->delay;
                local->used = 1;
                memcpy(local->bytes, bytes, PILOT_RECORD_BYTES);
                result = LINUX_PILOT_RECEIVER_LOCAL_REMEMBERED;
            }
        }
    }
    receiver->last_result = result;
    pthread_mutex_unlock(&receiver->lock);
    return result;
}

/* Windows pilot_stage_record semantics: low16 capture is unwrapped backward
 * from now; apply=capture+delay must be strictly future and within the ring.
 * No payload is changed or defaulted. Identical duplicates are idempotent. */
static inline linux_pilot_receiver_result linux_pilot_receiver_stage(
    linux_pilot_receiver *receiver, unsigned slot, const unsigned char *bytes, size_t length, uint64_t now) {
    pilot_record decoded;
    int valid = bytes && length == PILOT_RECORD_BYTES && pilot_decode(bytes, &decoded);
    pthread_mutex_lock(&receiver->lock);
    linux_pilot_receiver_result result = linux_pilot_receiver_admission_locked(receiver);
    if (result != LINUX_PILOT_RECEIVER_CONFIGURED) goto done;
    if (receiver->source != PILOT_SOURCE_WIRE) {
        result = LINUX_PILOT_RECEIVER_WRONG_SOURCE;
        goto done;
    }
    if (slot < 1 || slot > receiver->count) {
        result = LINUX_PILOT_RECEIVER_INVALID_SLOT;
        goto done;
    }
    if (!valid) {
        result = LINUX_PILOT_RECEIVER_INVALID_RECORD;
        goto done;
    }
    if (now > PILOT_TICK_MAX || (receiver->frame_ready && now < receiver->frame.tick)) {
        result = LINUX_PILOT_RECEIVER_INVALID_TICK;
        goto done;
    }
    uint64_t back = (uint16_t)(now - decoded.capture16);
    if (back > now) {
        result = LINUX_PILOT_RECEIVER_INVALID_TICK;
        goto done;
    }
    uint64_t capture = now - back;
    uint64_t apply = capture + receiver->delay;
    if (!capture || apply <= now || apply >= now + PILOT_RING) {
        ++receiver->late;
        result = LINUX_PILOT_RECEIVER_LATE_RECORD;
        goto done;
    }
    if (slot == receiver->own_slot) {
        for (unsigned i = 0; i < PILOT_LOCAL_RING; i++) {
            const linux_pilot_receiver_local *local = &receiver->locals[i];
            if (local->used && local->capture == capture && memcmp(local->bytes, bytes, PILOT_RECORD_BYTES)) {
                receiver->echo_failure = 1;
                result = LINUX_PILOT_RECEIVER_ECHO_MISMATCH;
                goto done;
            }
        }
    }
    linux_pilot_receiver_staged *entry = &receiver->ring[apply % PILOT_RING];
    if (entry->tick != apply) {
        if (entry->present && entry->tick > now) {
            result = LINUX_PILOT_RECEIVER_RING_CONFLICT;
            goto done;
        }
        memset(entry, 0, sizeof(*entry));
        entry->tick = apply;
    }
    unsigned bit = 1u << (slot - 1);
    if (entry->present & bit) {
        if (memcmp(entry->bytes[slot - 1], bytes, PILOT_RECORD_BYTES)) {
            ++receiver->conflicts;
            result = LINUX_PILOT_RECEIVER_CONFLICT;
        } else {
            ++receiver->duplicates;
            result = LINUX_PILOT_RECEIVER_DUPLICATE;
        }
        goto done;
    }
    memcpy(entry->bytes[slot - 1], bytes, PILOT_RECORD_BYTES);
    entry->present |= bit;
    ++receiver->staged;
    result = LINUX_PILOT_RECEIVER_STAGED;
done:
    receiver->last_result = result;
    pthread_mutex_unlock(&receiver->lock);
    return result;
}

/* Build all slots transactionally. Missing/invalid/delay-mismatched records
 * leave the last published frame untouched; its tick cannot satisfy a new
 * reader. Within the window every configured slot is required. No absent,
 * dropped or stock slot is synthesized. Outside the window present is zero. */
static inline linux_pilot_receiver_result linux_pilot_receiver_publish(linux_pilot_receiver *receiver, uint64_t tick) {
    pthread_mutex_lock(&receiver->lock);
    linux_pilot_receiver_result result = linux_pilot_receiver_admission_locked(receiver);
    if (result != LINUX_PILOT_RECEIVER_CONFIGURED) goto done;
    if (!tick || tick > PILOT_TICK_MAX || (receiver->frame_ready && tick < receiver->frame.tick)) {
        result = LINUX_PILOT_RECEIVER_INVALID_TICK;
        goto done;
    }
    if (receiver->frame_ready && tick == receiver->frame.tick) {
        result = LINUX_PILOT_RECEIVER_FRAME_READY;
        goto done;
    }
    linux_pilot_receiver_frame next;
    memset(&next, 0, sizeof(next));
    next.tick = tick;
    next.count = receiver->count;
    next.in_window = tick >= receiver->start && tick <= receiver->stop;
    memcpy(next.saved, receiver->saved, sizeof(next.saved));
    const linux_pilot_receiver_staged *entry = &receiver->ring[tick % PILOT_RING];
    if (next.in_window) {
        for (unsigned i = 0; i < receiver->count; i++) {
            if (receiver->source == PILOT_SOURCE_SCRIPT) {
                pilot_script(i + 1, tick, receiver->start, &next.records[i]);
                pilot_encode(&next.records[i], next.bytes[i]);
            } else {
                if (entry->tick != tick || !(entry->present & (1u << i))) {
                    ++receiver->missing;
                    result = LINUX_PILOT_RECEIVER_MISSING_RECORD;
                    goto done;
                }
                if (!pilot_decode(entry->bytes[i], &next.records[i]) ||
                    next.records[i].capture16 != (uint16_t)(tick - receiver->delay)) {
                    result = LINUX_PILOT_RECEIVER_DELAY_MISMATCH;
                    goto done;
                }
                memcpy(next.bytes[i], entry->bytes[i], PILOT_RECORD_BYTES);
            }
            next.present |= 1u << i;
        }
    }
    receiver->frame = next;
    receiver->frame_ready = 1;
    ++receiver->publications;
    for (unsigned i = 0; i < PILOT_LOCAL_RING; i++) {
        if (receiver->locals[i].used && receiver->locals[i].apply <= tick) receiver->locals[i].used = 0;
    }
    result = LINUX_PILOT_RECEIVER_FRAME_READY;
done:
    receiver->last_result = result;
    pthread_mutex_unlock(&receiver->lock);
    return result;
}

/* Copy under the publication mutex. Never return an internal pointer. */
static inline linux_pilot_receiver_result linux_pilot_receiver_copy_frame(
    linux_pilot_receiver *receiver, uint64_t tick, linux_pilot_receiver_frame *out) {
    pthread_mutex_lock(&receiver->lock);
    linux_pilot_receiver_result result = linux_pilot_receiver_admission_locked(receiver);
    if (result == LINUX_PILOT_RECEIVER_CONFIGURED) {
        if (!out || !receiver->frame_ready || receiver->frame.tick != tick) result = LINUX_PILOT_RECEIVER_NO_FRAME;
        else {
            *out = receiver->frame;
            result = LINUX_PILOT_RECEIVER_FRAME_READY;
        }
    }
    pthread_mutex_unlock(&receiver->lock);
    return result;
}

static inline void linux_pilot_receiver_stop(linux_pilot_receiver *receiver) {
    pthread_mutex_lock(&receiver->lock);
    receiver->stopped = 1;
    receiver->last_result = LINUX_PILOT_RECEIVER_STOPPED;
    pthread_mutex_unlock(&receiver->lock);
}

static inline linux_pilot_receiver_result linux_pilot_receiver_apply(linux_pilot_receiver *receiver) {
    pthread_mutex_lock(&receiver->lock);
    ++receiver->apply_refusals;
    pthread_mutex_unlock(&receiver->lock);
    return LINUX_PILOT_RECEIVER_APPLY_NOT_IMPLEMENTED;
}

static inline int linux_pilot_receiver_status(linux_pilot_receiver *receiver, char *out, size_t capacity) {
    if (!out || !capacity) return 0;
    char text[768];
    pthread_mutex_lock(&receiver->lock);
    int length = snprintf(text, sizeof(text),
        "configured=%u stopped=%u echo_failure=%u source=%s slots=%u delay=%u first=%llu last=%llu frame=%llu "
        "staged=%llu duplicates=%llu conflicts=%llu missing=%llu late=%llu publications=%llu apply_refusals=%llu "
        "steering_active=0 apply=not_implemented scope=all_present_record_slots last_result=%d",
        receiver->configured, receiver->stopped, receiver->echo_failure,
        receiver->source == PILOT_SOURCE_WIRE ? "wire" : receiver->source == PILOT_SOURCE_SCRIPT ? "script" : "none",
        receiver->count, receiver->delay, (unsigned long long)receiver->start, (unsigned long long)receiver->stop,
        (unsigned long long)(receiver->frame_ready ? receiver->frame.tick : 0),
        (unsigned long long)receiver->staged, (unsigned long long)receiver->duplicates,
        (unsigned long long)receiver->conflicts, (unsigned long long)receiver->missing,
        (unsigned long long)receiver->late, (unsigned long long)receiver->publications,
        (unsigned long long)receiver->apply_refusals, receiver->last_result);
    pthread_mutex_unlock(&receiver->lock);
    if (length <= 0 || (size_t)length >= sizeof(text) || (size_t)length >= capacity) return 0;
    memcpy(out, text, (size_t)length + 1);
    return length;
}

#endif
