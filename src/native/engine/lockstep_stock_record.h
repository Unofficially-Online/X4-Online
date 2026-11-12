#ifndef X4_LOCKSTEP_STOCK_RECORD_H
#define X4_LOCKSTEP_STOCK_RECORD_H

/* Platform-independent version-3 stock input records (wire section 0x53).
 *
 * This codec validates representation only. The caller must separately check
 * that each event, context and feature is supported by its replay adapter and
 * that the record belongs to the admitted slot and capture tick. Decoding a
 * record never grants permission to call an engine handler.
 *
 * All integer and float bits travel little-endian. Fields whose presence flag
 * is absent must contain positive-zero bits, including cursor/aim/FOV values.
 */
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    STOCK_INPUT_VERSION = 3,
    STOCK_INPUT_HEADER_BYTES = 43,
    STOCK_INPUT_EVENT_BYTES = 6,
    STOCK_INPUT_MAX_EVENTS = 31,
    STOCK_INPUT_MAX_BYTES = 229,
    STOCK_INPUT_ACTIVE = 1,
    STOCK_INPUT_CURSOR = 2,
    STOCK_INPUT_AIM = 4,
    STOCK_INPUT_VIEW = 8,
    STOCK_INPUT_FIRE = 16,
    STOCK_INPUT_SECONDARY_FIRE = 32,
    STOCK_INPUT_KNOWN_FLAGS = 63,
    STOCK_INPUT_ACTION = 0,
    STOCK_INPUT_STATE = 1,
    STOCK_INPUT_RANGE = 2,
    STOCK_INPUT_CONTEXT = 3
};

typedef struct {
    unsigned char kind;
    uint16_t id;
    uint32_t value;
} stock_input_event;

/* Member names retain the existing Windows adapter's source contract.
 * capture16 is the low 16 bits of the capture tick, never an apply tick.
 * steer carries mouse-steering/manual-aim/aim-assist flags; view carries the
 * cockpit and floating-camera bits. target is a portable component identity.
 */
typedef struct {
    unsigned char flags;
    uint16_t capture16;
    float cursor[3];
    float aim[3];
    unsigned char steer;
    unsigned char view;
    float fov;
    uint64_t target;
    unsigned count;
    stock_input_event events[STOCK_INPUT_MAX_EVENTS];
} stock_input_record;

static inline uint32_t stock_input_read_u32(const unsigned char *bytes) {
    uint32_t value = 0;
    for (unsigned byte = 0; byte < 4; ++byte) {
        value |= (uint32_t)bytes[byte] << (8 * byte);
    }
    return value;
}

static inline void stock_input_write_u32(unsigned char *bytes, uint32_t value) {
    for (unsigned byte = 0; byte < 4; ++byte) {
        bytes[byte] = (unsigned char)(value >> (8 * byte));
    }
}

static inline uint32_t stock_input_float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

/* Reject malformed values before publishing any record to the caller. */
static inline int stock_input_decode(
    const unsigned char *bytes, size_t length, stock_input_record *output) {
    if (!bytes || !output || length < STOCK_INPUT_HEADER_BYTES ||
        bytes[0] != STOCK_INPUT_VERSION ||
        (bytes[1] & ~STOCK_INPUT_KNOWN_FLAGS) ||
        bytes[42] > STOCK_INPUT_MAX_EVENTS ||
        length != (size_t)STOCK_INPUT_HEADER_BYTES + STOCK_INPUT_EVENT_BYTES * bytes[42]) {
        return 0;
    }

    stock_input_record record = {0};
    record.flags = bytes[1];
    record.capture16 = (uint16_t)(bytes[2] | (uint16_t)bytes[3] << 8);
    record.count = bytes[42];
    record.steer = bytes[28];
    record.view = bytes[29];
    uint32_t fov_bits = stock_input_read_u32(bytes + 30);
    memcpy(&record.fov, &fov_bits, sizeof(fov_bits));
    for (unsigned byte = 0; byte < 8; ++byte) {
        record.target |= (uint64_t)bytes[34 + byte] << (8 * byte);
    }
    if (record.flags & STOCK_INPUT_VIEW) {
        if (record.view > 3 || !isfinite(record.fov)) {
            return 0;
        }
    } else if (record.view || record.target || fov_bits) {
        return 0;
    }

    for (unsigned axis = 0; axis < 3; ++axis) {
        uint32_t cursor_bits = stock_input_read_u32(bytes + 4 + 4 * axis);
        uint32_t aim_bits = stock_input_read_u32(bytes + 16 + 4 * axis);
        memcpy(&record.cursor[axis], &cursor_bits, sizeof(cursor_bits));
        memcpy(&record.aim[axis], &aim_bits, sizeof(aim_bits));
        if (!isfinite(record.cursor[axis]) || !isfinite(record.aim[axis]) ||
            (!(record.flags & STOCK_INPUT_CURSOR) && cursor_bits) ||
            (!(record.flags & STOCK_INPUT_AIM) && aim_bits)) {
            return 0;
        }
    }
    if (record.steer > 7 || (!(record.flags & STOCK_INPUT_AIM) && record.steer)) {
        return 0;
    }

    for (unsigned index = 0; index < record.count; ++index) {
        const unsigned char *encoded = bytes + STOCK_INPUT_HEADER_BYTES +
            STOCK_INPUT_EVENT_BYTES * index;
        uint16_t header = (uint16_t)(encoded[0] | (uint16_t)encoded[1] << 8);
        stock_input_event *event = &record.events[index];
        event->kind = (unsigned char)(header >> 14);
        event->id = header & 0x3fff;
        event->value = stock_input_read_u32(encoded + 2);
        if (event->id >= 512 ||
            (event->kind == STOCK_INPUT_ACTION && event->value) ||
            ((event->kind == STOCK_INPUT_STATE || event->kind == STOCK_INPUT_CONTEXT) &&
             event->value > 1)) {
            return 0;
        }
        if (event->kind == STOCK_INPUT_RANGE) {
            float range;
            memcpy(&range, &event->value, sizeof(range));
            if (!isfinite(range)) {
                return 0;
            }
        }
    }
    *output = record;
    return 1;
}

/* The caller supplies a locally constructed record. Semantic event admission
 * is deliberately separate; Windows also uses the encoder to build negative
 * test packets. Capacity/count checks prevent writes outside the output span.
 */
static inline unsigned stock_input_encode(
    const stock_input_record *record, unsigned char *output, size_t capacity) {
    if (!record || !output || record->count > STOCK_INPUT_MAX_EVENTS) {
        return 0;
    }
    unsigned length = STOCK_INPUT_HEADER_BYTES + STOCK_INPUT_EVENT_BYTES * record->count;
    if (capacity < length) {
        return 0;
    }
    output[0] = STOCK_INPUT_VERSION;
    output[1] = record->flags;
    output[2] = (unsigned char)record->capture16;
    output[3] = (unsigned char)(record->capture16 >> 8);
    for (unsigned axis = 0; axis < 3; ++axis) {
        stock_input_write_u32(output + 4 + 4 * axis, stock_input_float_bits(record->cursor[axis]));
        stock_input_write_u32(output + 16 + 4 * axis, stock_input_float_bits(record->aim[axis]));
    }
    output[28] = record->steer;
    output[29] = record->view;
    stock_input_write_u32(output + 30, stock_input_float_bits(record->fov));
    for (unsigned byte = 0; byte < 8; ++byte) {
        output[34 + byte] = (unsigned char)(record->target >> (8 * byte));
    }
    output[42] = (unsigned char)record->count;
    for (unsigned index = 0; index < record->count; ++index) {
        const stock_input_event *event = &record->events[index];
        unsigned char *encoded = output + STOCK_INPUT_HEADER_BYTES +
            STOCK_INPUT_EVENT_BYTES * index;
        uint16_t header = (uint16_t)((uint16_t)event->kind << 14 | event->id);
        encoded[0] = (unsigned char)header;
        encoded[1] = (unsigned char)(header >> 8);
        stock_input_write_u32(encoded + 2, event->value);
    }
    return length;
}

#endif
