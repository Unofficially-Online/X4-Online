#ifndef X4_PLAYER_CURSOR_INPUT_TRACE_H
#define X4_PLAYER_CURSOR_INPUT_TRACE_H
/* Optional read-only observation. The embedding cursor gate must provide an
 * authenticated P1 worker/generation callback, exact native globals and a
 * checked read operation; no engine getter or input mutation is performed. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define CURSOR_INPUT_TRACE_LIMIT 256u
#define CURSOR_INPUT_TRACE_ENV "X4_DEV_CURSOR_INPUT_TRACE"

typedef int (*cursor_input_trace_read_fn)(void *, uintptr_t, void *, size_t);
typedef int (*cursor_input_trace_auth_fn)(void *, uint64_t, uintptr_t);
typedef struct {
    uintptr_t input, mouse_device, window_gate, window_owner, window_context;
    unsigned char mode33, steer36, transient37, direct38, block39, gate42, override_a010;
    unsigned char context8, ignored, window_gate_value, window_modal;
    unsigned char window_gate_present, window_context_present;
} cursor_input_trace_sample;
typedef int (*cursor_input_trace_emit_fn)(void *, uint64_t, unsigned, const cursor_input_trace_sample *);
typedef struct {
    void *context;
    cursor_input_trace_read_fn read;
    cursor_input_trace_auth_fn authenticate;
    cursor_input_trace_emit_fn emit;
    uintptr_t input_global, context8_global, ignored_global, window_gate_global, window_owner_global;
} cursor_input_trace_api;
typedef struct {
    uint64_t last_generation;
    unsigned enabled, initialized, emitted;
    cursor_input_trace_sample last;
} cursor_input_trace;

/* Null means disabled, exact '1' means enabled, any other value is refused. */
static inline int cursor_input_trace_option(const char *value) {
    return !value ? 0 : !strcmp(value, "1") ? 1 : -1;
}
static inline int cursor_input_trace_init(cursor_input_trace *trace, int enabled) {
    if (!trace || (enabled != 0 && enabled != 1)) return 0;
    memset(trace, 0, sizeof *trace);
    trace->enabled = (unsigned)enabled;
    return 1;
}
static inline int cursor_input_trace_equal(const cursor_input_trace_sample *a, const cursor_input_trace_sample *b) {
    return a->input == b->input && a->mouse_device == b->mouse_device &&
        a->window_gate == b->window_gate && a->window_owner == b->window_owner &&
        a->window_context == b->window_context && a->mode33 == b->mode33 &&
        a->steer36 == b->steer36 && a->transient37 == b->transient37 &&
        a->direct38 == b->direct38 && a->block39 == b->block39 && a->gate42 == b->gate42 &&
        a->override_a010 == b->override_a010 && a->context8 == b->context8 &&
        a->ignored == b->ignored && a->window_gate_value == b->window_gate_value &&
        a->window_modal == b->window_modal && a->window_gate_present == b->window_gate_present &&
        a->window_context_present == b->window_context_present;
}
static inline int cursor_input_trace_capture(cursor_input_trace *trace, const cursor_input_trace_api *api,
                                             uint64_t generation, uintptr_t expected_input) {
    if (!trace) return 0;
    if (!trace->enabled) return 1;
    if (!api || !generation || !expected_input || !api->read || !api->authenticate || !api->emit ||
        !api->input_global || !api->context8_global || !api->ignored_global ||
        !api->window_gate_global || !api->window_owner_global) return 0;
    if (expected_input > UINTPTR_MAX - 0xa011 || generation < trace->last_generation ||
        !api->authenticate(api->context, generation, expected_input)) return 0;
    cursor_input_trace_sample s = {0};
    uintptr_t input_after = 0, gate_after = 0, owner_after = 0, context_after = 0;
#define CURSOR_TRACE_READ(address, destination) \
    do { if (!api->read(api->context, (address), &(destination), sizeof(destination))) return 0; } while (0)
    CURSOR_TRACE_READ(api->input_global, s.input);
    if (s.input != expected_input) return 0;
    CURSOR_TRACE_READ(s.input + 0x9f33, s.mode33);
    CURSOR_TRACE_READ(s.input + 0x9f36, s.steer36);
    CURSOR_TRACE_READ(s.input + 0x9f37, s.transient37);
    CURSOR_TRACE_READ(s.input + 0x9f38, s.direct38);
    CURSOR_TRACE_READ(s.input + 0x9f39, s.block39);
    CURSOR_TRACE_READ(s.input + 0x9f42, s.gate42);
    CURSOR_TRACE_READ(s.input + 0xa010, s.override_a010);
    CURSOR_TRACE_READ(s.input + 0x64b0, s.mouse_device);
    CURSOR_TRACE_READ(api->context8_global, s.context8);
    CURSOR_TRACE_READ(api->ignored_global, s.ignored);
    CURSOR_TRACE_READ(api->window_gate_global, s.window_gate);
    if (s.window_gate) {
        s.window_gate_present = 1;
        CURSOR_TRACE_READ(s.window_gate, s.window_gate_value);
    }
    CURSOR_TRACE_READ(api->window_owner_global, s.window_owner);
    if (s.window_owner) {
        if (s.window_owner > UINTPTR_MAX - 16) return 0;
        CURSOR_TRACE_READ(s.window_owner + 8, s.window_context);
        if (s.window_context) {
            if (s.window_context > UINTPTR_MAX - 0x1e8) return 0;
            s.window_context_present = 1;
            CURSOR_TRACE_READ(s.window_context + 0x1e7, s.window_modal);
        }
    }
    CURSOR_TRACE_READ(api->window_gate_global, gate_after);
    CURSOR_TRACE_READ(api->window_owner_global, owner_after);
    if (s.window_owner) CURSOR_TRACE_READ(s.window_owner + 8, context_after);
    CURSOR_TRACE_READ(api->input_global, input_after);
#undef CURSOR_TRACE_READ
    if (input_after != expected_input || gate_after != s.window_gate || owner_after != s.window_owner ||
        context_after != s.window_context || !api->authenticate(api->context, generation, expected_input)) return 0;
    if (!trace->initialized || !cursor_input_trace_equal(&trace->last, &s)) {
        if (trace->emitted >= CURSOR_INPUT_TRACE_LIMIT || !api->emit(api->context, generation, trace->emitted, &s)) return 0;
        trace->last = s;
        ++trace->emitted;
        trace->initialized = 1;
    }
    trace->last_generation = generation;
    return 1;
}
#endif
