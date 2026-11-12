#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "stock_keyboard_observer.h"
typedef struct stock_keyboard_identity_sample {
    int ready;
    const char *reason;
    uintptr_t universe;
    uintptr_t player;
    uintptr_t controller;
    uintptr_t ship;
    uintptr_t input_owner;
    uintptr_t controller_owner;
    uintptr_t player_vtable;
    uintptr_t controller_vtable;
    uintptr_t ship_vtable;
    uint64_t player_runtime;
    uint64_t player_saved;
    uint64_t runtime;
    uint64_t saved;
    uint64_t autopilot;
    uint32_t count;
    unsigned char context_cf0;
    unsigned char requested_space;
    unsigned char flight;
    unsigned char enabled;
    unsigned char ignore;
    unsigned char ship_d1;
    unsigned char controller_d9;
    unsigned char mouse[3];
    unsigned char keys[4];
    float axes[3];
    float cursor[3];
} stock_keyboard_identity_sample;

typedef struct {
    const char *name;
    uint32_t va;
    uint32_t length;
    const char *sha256;
} lx_site;
typedef struct lua_State {
    double result;
    int nil;
    char text[500];
} lua_State;
static struct {
    unsigned active;
    uint64_t issued;
} stats = {1, 1};
static unsigned boot_phase = 3;
static unsigned main_thread = 1;
static unsigned ui_thread = 2;
static unsigned stock_observer_worker_thread = 2;
static unsigned stock_observer_post_join_boundary = 1;
static unsigned stock_observer_install_tick = 2;
static unsigned path_installed = 1;
static unsigned stock_observer_failed;
static atomic_uint stock_observer_requested = 1;
static atomic_uint stock_observer_input_depth;
static atomic_uint stock_observer_thread_failure;
static unsigned char *image = (unsigned char *)0x400000;
#define LX(value) ((void *)(uintptr_t)(value))
#define STOCK_OBSERVER_NATIVE_FLAGS 0x3daab04u
static unsigned thread_id = 2;
static unsigned tls_worker = 1;
static unsigned slot_valid = 1;
static unsigned accepted = 1;
static unsigned valid_pins = 1;
static unsigned valid_identity = 1;
static unsigned fail_read;
static unsigned native_flags;
static unsigned char loading;
static unsigned samples;
static unsigned reenter;
static int player_reference_after_join(void);
static unsigned change_sample_at;
static unsigned change_pointer_at;
static unsigned reference_reads;
static unsigned change_reference_count;
static uint32_t reference_count = 4;
static uint32_t reference_marker = 2;
static uintptr_t primary_vtable = 0x3b5f7a0;
static uintptr_t reference_vtable = 0x3b61a10;
static uintptr_t zero_target = 0x18a7df0;

static unsigned GetCurrentThreadId(void) {
    return thread_id;
}
static unsigned gate_tls_self(void) {
    return tls_worker;
}
static unsigned gate_tls_worker(unsigned value) {
    return value;
}
static int stock_observer_slot_valid(void) {
    return slot_valid;
}
static int stock_observer_wanted(void) {
    return atomic_load(&stock_observer_requested) != 0;
}
static int accept_ui(void) {
    return accepted;
}
static void p_lua_pushnumber(lua_State *state, double value) {
    state->result = value;
}
static void p_lua_pushnil(lua_State *state) {
    state->nil = 1;
}
static void p_lua_pushlstring(lua_State *state, const char *text, size_t length) {
    assert(length < sizeof(state->text));
    memcpy(state->text, text, length);
    state->text[length] = 0;
}
static int site_ok(const lx_site *pin) {
    assert((pin->va == 0xa238b0 && pin->length == 21) ||
    (pin->va == 0xa14540 && pin->length == 37));
    assert(strlen(pin->sha256) == 64);
    return valid_pins;
}
static int stock_keyboard_identity_collect(stock_keyboard_identity_sample *output) {
    ++samples;
    if (reenter) {
        assert(!player_reference_after_join());
    }
    memset(output, 0, sizeof(*output));
    output->ready = valid_identity;
    output->reason = "ready";
    output->player = samples == change_pointer_at ? 0x20000000 : 0x10000000;
    output->universe = 0x30000000;
    output->controller = 0x40000000;
    output->player_vtable = primary_vtable;
    output->runtime = 123;
    output->saved = 456;
    if (samples == change_sample_at) {
        output->cursor[2] = 1; /* A nonpointer field must also invalidate identity. */
    }
    return valid_identity;
}
static int read_local(const void *address, void *output, size_t length) {
    uintptr_t value = (uintptr_t)address;
    if (fail_read) {
        return 0;
    }
#define COPY_VALUE(expected, field) \
    if (value == (expected)) { \
        assert(length == sizeof(field)); \
        memcpy(output, &(field), length); \
        return 1; \
    }
    COPY_VALUE(0x3daab04u, native_flags)
    COPY_VALUE(0x3daab00u, loading)
    COPY_VALUE(0x3b61a10u, zero_target)
    COPY_VALUE(0x10000000u, primary_vtable)
    COPY_VALUE(0x10000038u, reference_vtable)
    COPY_VALUE(0x10000044u, reference_marker)
    if (value == 0x10000040u) {
        assert(length == sizeof(reference_count));
        uint32_t current = reference_count + (change_reference_count && reference_reads != 0);
        ++reference_reads;
        memcpy(output, &current, length);
        return 1;
    }
#undef COPY_VALUE
    assert(!"unexpected or stale native read destination");
    return 0;
}
#include "gate_player_reference_probe.h"

int main(int argc, char **argv) {
    assert(argc == 2);
    /* This fixture shares the observer types, not its installation machinery. */
    (void)sko_begin;
    (void)sko_install;
    (void)sko_validate;
    (void)sko_drain;
    (void)sko_restored;
    atomic_store(&sko_session.active, 1);
    sko_session.owned = (1u << SKO_SELECTED) - 1;
    lua_State state = {0};
    if (!strcmp(argv[1], "off")) {
        assert(player_reference_after_join());
        assert(player_reference_clean() && samples == 0);
        lua_player_reference_status(&state);
        assert(strstr(state.text, "requested=0") && strstr(state.text, "native_calls_enabled=0"));
        return 0;
    }
    if (!strncmp(argv[1], "request_", 8)) {
        if (!strcmp(argv[1], "request_tick")) {
            stats.issued = 2;
        } else if (!strcmp(argv[1], "request_owner")) {
            thread_id = 3;
        } else if (!strcmp(argv[1], "request_tls")) {
            tls_worker = 0;
        } else if (!strcmp(argv[1], "request_observer")) {
            atomic_store(&stock_observer_requested, 0);
        } else if (!strcmp(argv[1], "request_depth")) {
            atomic_store(&stock_observer_input_depth, 1);
        } else if (!strcmp(argv[1], "request_accept")) {
            accepted = 0;
        } else {
            assert(!"unknown request scenario");
        }
        lua_player_reference_request(&state);
        assert(state.result == 0 && samples == 0 && !player_reference_probe_state.requested);
        return 0;
    }
    lua_player_reference_request(&state);
    assert(state.result == 1 && player_reference_probe_state.requested);
    lua_player_reference_request(&state);
    assert(state.result == 0);
    assert(player_reference_after_join() && samples == 0); /* Tick one does not inspect engine refs. */
    stats.issued = 2;
    int expected = 0;
    if (!strcmp(argv[1], "observe")) {
        expected = 1;
    } else if (!strcmp(argv[1], "marker1")) {
        reference_marker = 1;
        expected = 1;
    } else if (!strcmp(argv[1], "count1")) {
        reference_count = 1;
        expected = 1;
    } else if (!strcmp(argv[1], "count_drift")) {
        change_reference_count = 1;
        expected = 1;
    } else if (!strcmp(argv[1], "no_join")) {
        stock_observer_post_join_boundary = 0;
    } else if (!strcmp(argv[1], "worker")) {
        thread_id = 3;
    } else if (!strcmp(argv[1], "tls")) {
        tls_worker = 0;
    } else if (!strcmp(argv[1], "inactive")) {
        stats.active = 0;
    } else if (!strcmp(argv[1], "boot")) {
        boot_phase = 2;
    } else if (!strcmp(argv[1], "path")) {
        path_installed = 0;
    } else if (!strcmp(argv[1], "depth")) {
        atomic_store(&stock_observer_input_depth, 1);
    } else if (!strcmp(argv[1], "wrapper")) {
        atomic_store(&sko_session.wrapper_depth, 1);
    } else if (!strcmp(argv[1], "observer_inactive")) {
        atomic_store(&sko_session.active, 0);
    } else if (!strcmp(argv[1], "owned")) {
        sko_session.owned &= ~1u;
    } else if (!strcmp(argv[1], "uncertain")) {
        sko_session.uncertain = 1;
    } else if (!strcmp(argv[1], "pending")) {
        sko_session.pending_readback = 1;
    } else if (!strcmp(argv[1], "overflow")) {
        atomic_store(&sko_session.overflow, 1);
    } else if (!strcmp(argv[1], "thread_failure")) {
        atomic_store(&stock_observer_thread_failure, 1);
    } else if (!strcmp(argv[1], "slot")) {
        slot_valid = 0;
    } else if (!strcmp(argv[1], "flags")) {
        native_flags = 2;
    } else if (!strcmp(argv[1], "loading")) {
        loading = 1;
    } else if (!strcmp(argv[1], "pins")) {
        valid_pins = 0;
    } else if (!strcmp(argv[1], "read")) {
        fail_read = 1;
    } else if (!strcmp(argv[1], "identity")) {
        valid_identity = 0;
    } else if (!strcmp(argv[1], "reentry")) {
        reenter = 1;
    } else if (!strcmp(argv[1], "identity_change")) {
        change_sample_at = 2;
    } else if (!strcmp(argv[1], "pointer_change")) {
        change_pointer_at = 2;
    } else if (!strcmp(argv[1], "primary_vtable")) {
        primary_vtable ^= 8;
    } else if (!strcmp(argv[1], "reference_vtable")) {
        reference_vtable ^= 8;
    } else if (!strcmp(argv[1], "zero_target")) {
        zero_target ^= 8;
    } else {
        assert(!"unknown scenario");
    }
    assert(player_reference_after_join() == expected);
    assert(player_reference_clean() && !player_reference_probe_state.attempted);
    assert(!player_reference_probe_state.passed);
    assert(player_reference_acquire_calls == 0 && player_reference_release_calls == 0);
    assert(reference_marker == (!strcmp(argv[1], "marker1") ? 1u : 2u));
    lua_player_reference_status(&state);
    if (expected) {
        assert(player_reference_observed && strstr(state.text, "reason=lifetime_contract_pending"));
        assert(strstr(state.text, "acquire_calls=0 release_calls=0"));
        assert(player_reference_probe_state.reference_samples == 2);
        assert(player_reference_preconditions == (!strcmp(argv[1], "observe") ? 1u : 0u));
    } else {
        assert(player_reference_probe_state.failed && !player_reference_observed);
    }
    unsigned previous_samples = samples;
    assert(player_reference_after_join() == expected && samples == previous_samples);
    assert(player_reference_acquire(NULL, P1_REFERENCE_ACQUIRE_VA, 0x10000000) == P1_REFERENCE_NOT_CALLED);
    assert(player_reference_release(NULL, P1_REFERENCE_RELEASE_VA, 0x10000038) == P1_REFERENCE_NOT_CALLED);
    assert(player_reference_acquire_calls == 0 && player_reference_release_calls == 0);
    accepted = 0;
    lua_player_reference_status(&state);
    assert(state.nil);
    return 0;
}
