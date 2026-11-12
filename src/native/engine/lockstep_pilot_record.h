#ifndef LOCKSTEP_PILOT_RECORD_H
#define LOCKSTEP_PILOT_RECORD_H

/* Pure record-v1 helpers extracted from lockstep_pilot_inputs.h. Windows and
 * Linux share these exact bytes and constraints. No controller, platform,
 * lifecycle, identity lookup or actuator belongs in this header.
 *
 * Wire layout (28 bytes, little endian): version at 0, mode at 1, capture tick
 * low 16 at 2..3, seven Q15 int16 values at 4..17, buttons at 18, reserved zero
 * at 19 and saved target uint64 at 20..27. Buttons are the four native trigger
 * bits. TARGET_UNMAPPED requires a zero saved target.
 * q[] is x/y/z/pitch/yaw/roll/throttle. -32768 is forbidden. Modes, buttons
 * and target are retained as encoded; knowing their format is not applying
 * those features. Quantization requires finite input and uses the caller's
 * floating-point rounding mode, as the original Windows helper did. */
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PILOT_RECORD_BYTES 28u
#define PILOT_RECORD_VERSION 1u
#define PILOT_SLOTS 8u
#define PILOT_TICK_MAX 8388607u
#define PILOT_RING 64u
#define PILOT_LOCAL_RING 32u
#define PILOT_DELAY_MIN 2u
#define PILOT_DELAY_MAX 30u
#define PILOT_SUPPRESS_LEAD 3u
#define PILOT_MODE_ASSIST 1u
#define PILOT_MODE_BOOST 2u
#define PILOT_MODE_TRAVEL 4u
#define PILOT_MODE_TARGET_UNMAPPED 8u
#define PILOT_MODE_REQUESTS 16u /* q0..q5 are native requests divided by Agent limits. */
#define PILOT_MODE_KNOWN 31u
#define PILOT_SOURCE_SCRIPT 1u
#define PILOT_SOURCE_WIRE 2u

typedef struct {
    unsigned char mode;
    unsigned char buttons;
    uint16_t capture16;
    int16_t q[7];
    uint64_t target;
} pilot_record;

static inline int16_t pilot_quantize(float value) {
    if (value > 1.0f) value = 1.0f;
    if (value < -1.0f) value = -1.0f;
    return (int16_t)lrintf(value * 32767.0f);
}

static inline float pilot_axis(int16_t q) {
    return (float)q / 32767.0f;
}

static inline void pilot_encode(const pilot_record *record, unsigned char out[PILOT_RECORD_BYTES]) {
    memset(out, 0, PILOT_RECORD_BYTES);
    out[0] = PILOT_RECORD_VERSION;
    out[1] = record->mode;
    out[2] = (unsigned char)record->capture16;
    out[3] = (unsigned char)(record->capture16 >> 8);
    for (unsigned i = 0; i < 7; i++) {
        uint16_t value = (uint16_t)record->q[i];
        out[4 + 2 * i] = (unsigned char)value;
        out[5 + 2 * i] = (unsigned char)(value >> 8);
    }
    out[18] = record->buttons;
    for (unsigned i = 0; i < 8; i++) out[20 + i] = (unsigned char)(record->target >> (8 * i));
}

/* As before extraction, a failed decode may have written a partial record.
 * Callers must decode to a temporary and publish only after success. */
static inline int pilot_decode(const unsigned char in[PILOT_RECORD_BYTES], pilot_record *record) {
    if (in[0] != PILOT_RECORD_VERSION || (in[1] & ~PILOT_MODE_KNOWN) || (in[18] & 0xf0) || in[19]) return 0;
    memset(record, 0, sizeof(*record));
    record->mode = in[1];
    record->buttons = in[18];
    record->capture16 = (uint16_t)(in[2] | in[3] << 8);
    for (unsigned i = 0; i < 7; i++) {
        record->q[i] = (int16_t)(uint16_t)(in[4 + 2 * i] | in[5 + 2 * i] << 8);
        if (record->q[i] == INT16_MIN) return 0;
    }
    for (unsigned i = 0; i < 8; i++) record->target |= (uint64_t)in[20 + i] << (8 * i);
    return !(record->target && (record->mode & PILOT_MODE_TARGET_UNMAPPED));
}

static inline void pilot_hex(const unsigned char *in, unsigned size, char *out) {
    static const char digits[] = "0123456789abcdef";
    for (unsigned i = 0; i < size; i++) {
        out[2 * i] = digits[in[i] >> 4];
        out[2 * i + 1] = digits[in[i] & 15];
    }
    out[2 * size] = 0;
}

static inline int pilot_unhex(const char *text, size_t length, unsigned char out[PILOT_RECORD_BYTES]) {
    if (!text || length != 2 * PILOT_RECORD_BYTES) return 0;
    for (unsigned i = 0; i < 2 * PILOT_RECORD_BYTES; i++) {
        char c = text[i];
        unsigned digit = c >= '0' && c <= '9' ? (unsigned)(c - '0') :
                         c >= 'a' && c <= 'f' ? (unsigned)(c - 'a' + 10) : 16;
        if (digit > 15) return 0;
        if (i & 1) out[i / 2] = (unsigned char)(out[i / 2] | digit);
        else out[i / 2] = (unsigned char)(digit << 4);
    }
    return 1;
}

/* Existing deterministic script: 120-tick segments of forward, forward/yaw,
 * half forward/pitch, strafe/roll, then neutral; odd/even slots turn oppositely. */
static inline void pilot_script(unsigned slot, uint64_t tick, uint64_t start, pilot_record *record) {
    memset(record, 0, sizeof(*record));
    record->mode = PILOT_MODE_ASSIST;
    record->capture16 = (uint16_t)tick;
    int16_t full = (slot & 1) ? 32767 : -32767;
    int16_t half = (slot & 1) ? 16384 : -16384;
    switch (tick < start ? 4u : (unsigned)((tick - start) / 120 % 5)) {
    case 0:
        record->q[2] = 32767;
        break;
    case 1:
        record->q[2] = 32767;
        record->q[4] = half;
        break;
    case 2:
        record->q[2] = 16384;
        record->q[3] = half;
        break;
    case 3:
        record->q[0] = full;
        record->q[5] = half;
        break;
    default:
        break;
    }
    record->q[6] = record->q[2];
}

static inline int pilot_parse_manifest(const char *text, uint64_t saved[PILOT_SLOTS], unsigned *count) {
    unsigned number = 0;
    size_t at = 0;
    if (!text) return 0;
    for (;;) {
        uint64_t value = 0;
        for (unsigned i = 0; i < 16; i++) {
            char c = text[at + i];
            unsigned digit = c >= '0' && c <= '9' ? (unsigned)(c - '0') :
                             c >= 'a' && c <= 'f' ? (unsigned)(c - 'a' + 10) : 16;
            if (digit > 15) return 0;
            value = (value << 4) | digit;
        }
        if (!value || number >= PILOT_SLOTS) return 0;
        for (unsigned j = 0; j < number; j++) if (saved[j] == value) return 0;
        saved[number++] = value;
        at += 16;
        if (!text[at]) break;
        if (text[at] != ',') return 0;
        ++at;
    }
    *count = number;
    return 1;
}

static inline int pilot_config_valid(unsigned count, unsigned source, unsigned delay,
                                     uint64_t start, uint64_t stop, unsigned suppress) {
    if (!count || count > PILOT_SLOTS || suppress > 1 || stop < start || stop > PILOT_TICK_MAX ||
        start < PILOT_SUPPRESS_LEAD + 2) return 0;
    if (source == PILOT_SOURCE_SCRIPT) return delay == 0;
    return source == PILOT_SOURCE_WIRE && delay >= PILOT_DELAY_MIN && delay <= PILOT_DELAY_MAX && start > delay;
}

static inline int pilot_delay_covers(unsigned source, unsigned delay, unsigned lookahead) {
    return source != PILOT_SOURCE_WIRE || delay >= PILOT_DELAY_MIN + lookahead;
}

#endif
