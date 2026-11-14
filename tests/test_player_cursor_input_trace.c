#include "../src/native/engine/linux/player_cursor_input_trace.h"
#include <assert.h>
#include <stdio.h>

typedef struct {
    unsigned char input[0xa020], owner[16], context[0x1e8];
    unsigned char context8, ignored, gate;
    uintptr_t input_ptr, gate_ptr, owner_ptr;
    uintptr_t fail_address;
    unsigned auth_calls, fail_auth_call, emissions;
    int fail_emit, mutate_owner;
    cursor_input_trace_sample observed;
} fixture;

static int read_memory(void *opaque, uintptr_t address, void *out, size_t size) {
    fixture *f = opaque;
    if (address == f->fail_address) return 0;
    /* Every address in this fixture is part of f. */
    if (address < (uintptr_t)f || size > sizeof *f || address - (uintptr_t)f > sizeof *f - size) return 0;
    memcpy(out, (const void *)address, size);
    if (f->mutate_owner && address == (uintptr_t)&f->owner_ptr) {
        f->mutate_owner = 0;
        f->owner_ptr = 0;
    }
    return 1;
}
static int authenticate(void *opaque, uint64_t generation, uintptr_t input) {
    fixture *f = opaque;
    ++f->auth_calls;
    return generation && input == (uintptr_t)f->input && f->auth_calls != f->fail_auth_call;
}
static int emit(void *opaque, uint64_t generation, unsigned index, const cursor_input_trace_sample *sample) {
    fixture *f = opaque;
    assert(generation && index == f->emissions);
    if (f->fail_emit) return 0;
    f->observed = *sample;
    ++f->emissions;
    return 1;
}
int main(void) {
    fixture f = {0}; cursor_input_trace trace; uintptr_t context = (uintptr_t)f.context;
    f.input_ptr = (uintptr_t)f.input; f.gate_ptr = (uintptr_t)&f.gate; f.owner_ptr = (uintptr_t)f.owner;
    memcpy(f.owner + 8, &context, sizeof context);
    f.input[0x9f33] = 1; f.input[0x9f42] = 1; f.gate = 1; f.context[0x1e7] = 2;
    cursor_input_trace_api api = { &f, read_memory, authenticate, emit, (uintptr_t)&f.input_ptr,
        (uintptr_t)&f.context8, (uintptr_t)&f.ignored, (uintptr_t)&f.gate_ptr, (uintptr_t)&f.owner_ptr };
    assert(cursor_input_trace_option(NULL) == 0 && cursor_input_trace_option("1") == 1);
    assert(cursor_input_trace_option("0") == -1 && cursor_input_trace_option("") == -1);
    assert(cursor_input_trace_init(&trace, 0));
    assert(cursor_input_trace_capture(&trace, NULL, 0, 0));
    assert(cursor_input_trace_init(&trace, 1));
    assert(cursor_input_trace_capture(&trace, &api, 1, f.input_ptr));
    assert(f.emissions == 1 && f.observed.mode33 == 1 && f.observed.gate42 == 1);
    assert(f.observed.window_gate_present && f.observed.window_gate_value == 1);
    assert(f.observed.window_context_present && f.observed.window_modal == 2);
    assert(cursor_input_trace_capture(&trace, &api, 2, f.input_ptr) && f.emissions == 1);
    f.input[0x9f36] = 1;
    f.fail_address = (uintptr_t)f.input + 0x9f36;
    assert(!cursor_input_trace_capture(&trace, &api, 3, f.input_ptr) && f.emissions == 1);
    f.fail_address = 0; f.fail_auth_call = f.auth_calls + 2;
    assert(!cursor_input_trace_capture(&trace, &api, 3, f.input_ptr) && f.emissions == 1);
    f.fail_auth_call = 0; f.fail_emit = 1;
    assert(!cursor_input_trace_capture(&trace, &api, 3, f.input_ptr) && f.emissions == 1);
    f.fail_emit = 0;
    assert(cursor_input_trace_capture(&trace, &api, 3, f.input_ptr) && f.emissions == 2);
    assert(f.observed.steer36 == 1);
    assert(!cursor_input_trace_capture(&trace, &api, 2, f.input_ptr));
    f.mutate_owner = 1;
    assert(!cursor_input_trace_capture(&trace, &api, 4, f.input_ptr) && f.emissions == 2);
    f.gate_ptr = 0;
    assert(cursor_input_trace_capture(&trace, &api, 4, f.input_ptr));
    assert(!f.observed.window_gate_present && !f.observed.window_context_present);
    trace.emitted = CURSOR_INPUT_TRACE_LIMIT; f.input[0x9f38] = 1;
    assert(!cursor_input_trace_capture(&trace, &api, 5, f.input_ptr) && f.emissions == 3);
    puts("cursor input trace: PASS");
    return 0;
}
