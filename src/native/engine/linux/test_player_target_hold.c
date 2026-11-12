/* Compile-time fake native dependencies; this never launches or calls X4. */
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SEAT_PREFLIGHT_TARGET_SAVED UINT64_C(0x175310)
#define SEAT_PREFLIGHT_SHIP_VTABLE 0x3d21748u
#define SEAT_PREFLIGHT_COCKPIT_VTABLE 0x3ca5fc0u

typedef struct {
    const char *name;
    unsigned va;
    unsigned length;
    const char *sha256;
} lx_site;

_Alignas(16) static unsigned char target[0x430];
_Alignas(16) static unsigned char cockpit[0x80];
_Alignas(16) static unsigned char other_target[0x430];
_Alignas(16) static unsigned char other_cockpit[0x80];
static uint64_t thread_id = 7;
static unsigned joined = 1;
static unsigned pins_valid = 1;
static unsigned sealed = 1;
static unsigned duplicate_saved;
static unsigned target_missing;
static unsigned resolve_calls;
static unsigned native_calls;
static unsigned reads;
static unsigned stop_after_native_call;
static unsigned bad_delta_call;
static unsigned change_identity_call;
static unsigned change_resolve_call;
static unsigned change_parent_call;
static unsigned fail_boundary_call;
static unsigned fail_pin_call;
static unsigned change_worker_call;
static unsigned nested_call;
static unsigned block_room_acquisition;
static unsigned block_target_cleanup;
static uintptr_t unreadable;
static uintptr_t ship_zero_dispatch = 0x18a7df0;
static uintptr_t room_zero_dispatch = 0x18a7df0;
static uintptr_t cockpit_predicate = 0xa39930;
static char call_order[16];

static void fake_acquire(void *object);
static void fake_release(void *reference);
static void *fake_lx(unsigned va) {
    if (va == 0xa238b0) {
        return (void *)fake_acquire;
    }
    if (va == 0xa14540) {
        return (void *)fake_release;
    }
    return (void *)(uintptr_t)va;
}
#define LX(va) fake_lx(va)

static int read_local(const void *address, void *output, size_t length);
static uint64_t GetCurrentThreadId(void) {
    return thread_id;
}
static int factory_probe_joined_boundary(void) {
    return joined;
}
static int site_ok(const lx_site *pin) {
    assert(pin->length && strlen(pin->sha256) == 64);
    return pins_valid;
}
static int player_seat_preflight_resolve(void *context, uint64_t saved, uintptr_t *object,
                                         uint64_t *runtime) {
    assert(!context && saved == SEAT_PREFLIGHT_TARGET_SAVED);
    ++resolve_calls;
    if (!sealed || duplicate_saved || target_missing) {
        return 0;
    }
    /* Fresh registry identity can change independently of the cached object. */
    *object = (change_identity_call && native_calls >= change_identity_call) ||
                      (change_resolve_call && resolve_calls >= change_resolve_call)
                  ? (uintptr_t)other_target
                  : (uintptr_t)target;
    *runtime = 100;
    return 1;
}

#include "gate_player_target_hold.h"

static void set_pointer(void *object, unsigned offset, uintptr_t value) {
    memcpy((unsigned char *)object + offset, &value, sizeof(value));
}
static void set_count(void *object, uint32_t value) {
    memcpy((unsigned char *)object + 0x40, &value, sizeof(value));
}
static uint32_t get_count(const void *object) {
    uint32_t count = 0;
    memcpy(&count, (const unsigned char *)object + 0x40, sizeof(count));
    return count;
}
static void set_marker(void *object, uint32_t value) {
    memcpy((unsigned char *)object + 0x44, &value, sizeof(value));
}

static int read_local(const void *address, void *output, size_t length) {
    uintptr_t source = (uintptr_t)address;
    ++reads;
    if (source == unreadable ||
        (stop_after_native_call && native_calls >= stop_after_native_call)) {
        return 0;
    }
    if (block_room_acquisition && player_target_hold_resident.target_owned &&
        !player_target_hold_resident.room_owned && !player_target_hold_resident.pending) {
        return 0;
    }
    if (block_target_cleanup && native_calls >= 3 && !player_target_hold_resident.room_owned &&
        player_target_hold_resident.target_owned) {
        return 0;
    }
    if (source == PLAYER_TARGET_HOLD_SHIP_REFERENCE_VTABLE ||
        source == PLAYER_TARGET_HOLD_COCKPIT_REFERENCE_VTABLE ||
        source == SEAT_PREFLIGHT_COCKPIT_VTABLE + 0x11f0) {
        assert(length == sizeof(uintptr_t));
        uintptr_t value = source == PLAYER_TARGET_HOLD_SHIP_REFERENCE_VTABLE ? ship_zero_dispatch
                          : source == PLAYER_TARGET_HOLD_COCKPIT_REFERENCE_VTABLE
                              ? room_zero_dispatch
                              : cockpit_predicate;
        memcpy(output, &value, length);
        return 1;
    }
    const struct {
        const void *base;
        size_t length;
    } regions[] = {
        {target, sizeof(target)},
        {cockpit, sizeof(cockpit)},
        {other_target, sizeof(other_target)},
        {other_cockpit, sizeof(other_cockpit)},
    };
    for (unsigned region_index = 0; region_index < sizeof(regions) / sizeof(regions[0]);
         ++region_index) {
        uintptr_t begin = (uintptr_t)regions[region_index].base;
        if (source >= begin && source - begin <= regions[region_index].length &&
            length <= regions[region_index].length - (source - begin)) {
            memcpy(output, address, length);
            return 1;
        }
    }
    assert(!"unexpected native address");
    return 0;
}

static void after_native_call(void) {
    if (fail_boundary_call == native_calls) {
        joined = 0;
    }
    if (fail_pin_call == native_calls) {
        pins_valid = 0;
    }
    if (change_worker_call == native_calls) {
        ++thread_id;
    }
    if (change_parent_call && native_calls == change_parent_call) {
        set_pointer(cockpit, 0x70, (uintptr_t)other_target);
    }
    if (nested_call) {
        unsigned before = native_calls;
        assert(!player_target_hold_acquire());
        assert(!player_target_hold_release());
        assert(!player_target_hold_check());
        assert(native_calls == before && player_target_hold_resident.busy);
    }
}
static void fake_acquire(void *object) {
    assert(player_target_hold_resident.busy && player_target_hold_resident.pending);
    assert(!player_target_hold_resident.returned);
    assert(object == target || object == cockpit);
    unsigned target_call = object == target;
    assert(player_target_hold_resident.pending ==
           (target_call ? PLAYER_TARGET_HOLD_ACQUIRE_TARGET : PLAYER_TARGET_HOLD_ACQUIRE_ROOM));
    call_order[native_calls++] = target_call ? 'T' : 'R';
    uint32_t count = get_count(object);
    assert(count && count < INT32_MAX - 1);
    set_count(object, count + (bad_delta_call == native_calls ? 2 : 1));
    after_native_call();
}
static void fake_release(void *reference) {
    unsigned char *object = (unsigned char *)reference - 0x38;
    assert(player_target_hold_resident.busy && player_target_hold_resident.pending);
    assert(!player_target_hold_resident.returned);
    assert(object == target || object == cockpit);
    unsigned target_call = object == target;
    assert(player_target_hold_resident.pending ==
           (target_call ? PLAYER_TARGET_HOLD_RELEASE_TARGET : PLAYER_TARGET_HOLD_RELEASE_ROOM));
    assert(target_call ? player_target_hold_resident.target_owned
                       : player_target_hold_resident.room_owned);
    call_order[native_calls++] = target_call ? 't' : 'r';
    uint32_t count = get_count(object);
    assert(count > 1);
    set_count(object, count - (bad_delta_call == native_calls ? 0 : 1));
    after_native_call();
}

static void setup(void) {
    set_pointer(target, 0, SEAT_PREFLIGHT_SHIP_VTABLE);
    set_pointer(target, 8, 100);
    set_pointer(target, 0x38, PLAYER_TARGET_HOLD_SHIP_REFERENCE_VTABLE);
    set_count(target, 8);
    set_marker(target, 2);
    set_pointer(target, 0x408, (uintptr_t)cockpit);
    set_pointer(cockpit, 0, SEAT_PREFLIGHT_COCKPIT_VTABLE);
    set_pointer(cockpit, 8, 200);
    set_pointer(cockpit, 0x38, PLAYER_TARGET_HOLD_COCKPIT_REFERENCE_VTABLE);
    set_count(cockpit, 3);
    set_marker(cockpit, 2);
    set_pointer(cockpit, 0x70, (uintptr_t)target);
    memcpy(other_target, target, sizeof(target));
    memcpy(other_cockpit, cockpit, sizeof(cockpit));
}

static void assert_uncertain(unsigned call) {
    player_target_hold *hold = &player_target_hold_resident;
    assert(hold->uncertain && hold->pending == call && hold->returned && !hold->busy);
    unsigned calls = native_calls;
    unsigned prior_reads = reads;
    assert(!player_target_hold_release() && !player_target_hold_acquire());
    assert(!player_target_hold_check());
    assert(native_calls == calls && reads == prior_reads);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    setup();
    const char *scenario = argv[1];
    player_target_hold *hold = &player_target_hold_resident;
    if (!strcmp(scenario, "unused")) {
        assert(!native_calls && !resolve_calls && !reads);
        assert(player_target_hold_release());
        assert(!native_calls && !resolve_calls && !reads);
        return 0;
    }
    int refuse_initial = 1;
    if (!strcmp(scenario, "wrong_boundary")) {
        joined = 0;
    } else if (!strcmp(scenario, "pins")) {
        pins_valid = 0;
    } else if (!strcmp(scenario, "unsealed")) {
        sealed = 0;
    } else if (!strcmp(scenario, "duplicate_saved")) {
        duplicate_saved = 1;
    } else if (!strcmp(scenario, "missing_target")) {
        target_missing = 1;
    } else if (!strcmp(scenario, "wrong_ship")) {
        set_pointer(target, 0, SEAT_PREFLIGHT_SHIP_VTABLE + 8);
    } else if (!strcmp(scenario, "wrong_runtime")) {
        set_pointer(target, 8, 999);
    } else if (!strcmp(scenario, "wrong_room")) {
        set_pointer(cockpit, 0, SEAT_PREFLIGHT_COCKPIT_VTABLE + 8);
    } else if (!strcmp(scenario, "room_runtime_zero")) {
        set_pointer(cockpit, 8, 0);
    } else if (!strcmp(scenario, "wrong_parent")) {
        set_pointer(cockpit, 0x70, (uintptr_t)other_target);
    } else if (!strcmp(scenario, "wrong_ship_reference")) {
        set_pointer(target, 0x38, PLAYER_TARGET_HOLD_COCKPIT_REFERENCE_VTABLE);
    } else if (!strcmp(scenario, "wrong_room_reference")) {
        set_pointer(cockpit, 0x38, PLAYER_TARGET_HOLD_SHIP_REFERENCE_VTABLE);
    } else if (!strcmp(scenario, "wrong_ship_dispatch")) {
        ship_zero_dispatch += 8;
    } else if (!strcmp(scenario, "wrong_room_dispatch")) {
        room_zero_dispatch += 8;
    } else if (!strcmp(scenario, "wrong_room_predicate")) {
        cockpit_predicate += 8;
    } else if (!strcmp(scenario, "zero_count")) {
        set_count(target, 0);
    } else if (!strcmp(scenario, "destroyed_marker")) {
        set_marker(target, 3);
    } else if (!strcmp(scenario, "room_zero_count")) {
        set_count(cockpit, 0);
    } else if (!strcmp(scenario, "room_destroyed_marker")) {
        set_marker(cockpit, 3);
    } else if (!strcmp(scenario, "room_gated")) {
        set_pointer(cockpit, 0x68, 1);
    } else if (!strcmp(scenario, "unreadable")) {
        unreadable = (uintptr_t)target + 0x40;
    } else if (!strcmp(scenario, "count_overflow")) {
        set_count(target, INT32_MAX - 1);
    } else if (!strcmp(scenario, "initial_identity_change")) {
        change_resolve_call = 2;
    } else if (!strcmp(scenario, "unaligned_room")) {
        set_pointer(target, 0x408, (uintptr_t)cockpit + 1);
    } else {
        refuse_initial = 0;
    }
    if (refuse_initial) {
        assert(!player_target_hold_acquire());
        assert(!native_calls && !hold->target_owned && !hold->room_owned);
        assert(!hold->pending && !hold->uncertain && !hold->busy);
        return 0;
    }
    if (!strcmp(scenario, "acquire_target_unreadable")) {
        stop_after_native_call = 1;
    } else if (!strcmp(scenario, "acquire_room_unreadable")) {
        stop_after_native_call = 2;
    } else if (!strcmp(scenario, "acquire_target_bad_delta")) {
        bad_delta_call = 1;
    } else if (!strcmp(scenario, "acquire_room_bad_delta")) {
        bad_delta_call = 2;
    } else if (!strcmp(scenario, "acquire_identity_changed")) {
        change_identity_call = 1;
    } else if (!strcmp(scenario, "acquire_parent_changed")) {
        change_parent_call = 2;
    } else if (!strcmp(scenario, "acquire_pin_loss")) {
        fail_pin_call = 1;
    } else if (!strcmp(scenario, "acquire_boundary_loss")) {
        fail_boundary_call = 2;
    } else if (!strcmp(scenario, "acquire_worker_change")) {
        change_worker_call = 1;
    } else if (!strcmp(scenario, "partial_acquisition")) {
        block_room_acquisition = 1;
    } else if (!strcmp(scenario, "room_count_overflow")) {
        set_count(cockpit, INT32_MAX - 1);
    } else if (!strcmp(scenario, "nested")) {
        nested_call = 1;
    } else if (!strcmp(scenario, "initial_refusal_retry")) {
        joined = 0;
        assert(!player_target_hold_acquire() && !native_calls && !hold->pending);
        joined = 1;
    }
    int acquired = player_target_hold_acquire();
    if (hold->uncertain) {
        assert(!acquired && native_calls >= 1 && native_calls <= 2);
        assert_uncertain(native_calls == 1 ? PLAYER_TARGET_HOLD_ACQUIRE_TARGET
                                           : PLAYER_TARGET_HOLD_ACQUIRE_ROOM);
        assert(hold->target_owned == (native_calls == 2));
        return 0;
    }
    if (!strcmp(scenario, "partial_acquisition") || !strcmp(scenario, "room_count_overflow")) {
        assert(!acquired && native_calls == 1 && hold->target_owned && !hold->room_owned);
        block_room_acquisition = 0;
        assert(player_target_hold_release());
        assert(native_calls == 2 && !strcmp(call_order, "Tt"));
        assert(get_count(target) == 8);
        assert(get_count(cockpit) ==
               (!strcmp(scenario, "room_count_overflow") ? INT32_MAX - 1u : 3));
        return 0;
    }
    assert(acquired && hold->target_owned && hold->room_owned && native_calls == 2);
    assert(get_count(target) == 9 && get_count(cockpit) == 4);
    assert(player_target_hold_check());
    unsigned prior_calls = native_calls;
    assert(!player_target_hold_acquire() && native_calls == prior_calls);
    if (!strcmp(scenario, "wrong_thread")) {
        thread_id += 1;
        assert(!player_target_hold_check() && !player_target_hold_release());
        assert(hold->target_owned && hold->room_owned && native_calls == 2);
        --thread_id;
    } else if (!strcmp(scenario, "foreign_reference_slot")) {
        hold->room_reference = (uintptr_t)other_cockpit;
        assert(!player_target_hold_check() && !player_target_hold_release());
        assert(native_calls == 2 && hold->room_owned);
        return 0;
    } else if (!strcmp(scenario, "live_room_replaced")) {
        set_pointer(target, 0x408, (uintptr_t)other_cockpit);
        assert(!player_target_hold_check() && !player_target_hold_release());
        assert(native_calls == 2 && hold->room_owned && hold->target_owned);
        return 0;
    } else if (!strcmp(scenario, "last_room_reference")) {
        set_count(cockpit, 1);
        assert(!player_target_hold_check());
        assert(!player_target_hold_release() && native_calls == 2);
        assert(hold->target_owned && hold->room_owned && !hold->uncertain);
        return 0;
    } else if (!strcmp(scenario, "release_room_unreadable")) {
        stop_after_native_call = 3;
    } else if (!strcmp(scenario, "release_target_unreadable")) {
        stop_after_native_call = 4;
    } else if (!strcmp(scenario, "release_room_bad_delta")) {
        bad_delta_call = 3;
    } else if (!strcmp(scenario, "release_target_bad_delta")) {
        bad_delta_call = 4;
    } else if (!strcmp(scenario, "release_parent_changed")) {
        change_parent_call = 3;
    } else if (!strcmp(scenario, "release_pin_loss")) {
        fail_pin_call = 3;
    } else if (!strcmp(scenario, "release_boundary_loss")) {
        fail_boundary_call = 4;
    } else if (!strcmp(scenario, "release_worker_change")) {
        change_worker_call = 3;
    } else if (!strcmp(scenario, "release_identity_changed")) {
        change_identity_call = 4;
    } else if (!strcmp(scenario, "partial_cleanup")) {
        block_target_cleanup = 1;
    } else if (!strcmp(scenario, "last_target_reference")) {
        set_count(target, 1);
    } else {
        assert(!strcmp(scenario, "success") || !strcmp(scenario, "nested") ||
               !strcmp(scenario, "initial_refusal_retry"));
    }
    int released = player_target_hold_release();
    if (hold->uncertain) {
        assert(!released && native_calls >= 3 && native_calls <= 4);
        assert_uncertain(native_calls == 3 ? PLAYER_TARGET_HOLD_RELEASE_ROOM
                                           : PLAYER_TARGET_HOLD_RELEASE_TARGET);
        assert(hold->target_owned && hold->room_owned == (native_calls == 3));
        return 0;
    }
    if (!strcmp(scenario, "partial_cleanup") || !strcmp(scenario, "last_target_reference")) {
        assert(!released && native_calls == 3 && !hold->room_owned && hold->target_owned);
        if (!strcmp(scenario, "last_target_reference")) {
            return 0;
        }
        block_target_cleanup = 0;
        assert(player_target_hold_release());
    } else {
        assert(released);
    }
    assert(native_calls == 4 && !strcmp(call_order, "TRrt"));
    assert(!hold->target_owned && !hold->room_owned && !hold->pending && !hold->uncertain);
    assert(get_count(target) == 8 && get_count(cockpit) == 3);
    unsigned prior_reads = reads;
    assert(player_target_hold_release() && reads == prior_reads && native_calls == 4);
    assert(!player_target_hold_check() && reads == prior_reads);
    return 0;
}
