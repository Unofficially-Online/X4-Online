#ifndef X4_GATE_PLAYER_TARGET_HOLD_H
#define X4_GATE_PLAYER_TARGET_HOLD_H

/* UNUSED native adapter. Include after gate_player_seat_preflight.h.
 * No request, Lua method, hook or automatic cleanup is installed here.
 *
 * The existing joined worker boundary and the caller's serialized lifecycle
 * must exclude final-reference release, registry removal, unobserved ABA,
 * cached-room rebinding and parent changes throughout each operation. Reads
 * and count deltas cannot establish that exclusion. Acquiring a Component
 * reference prevents its zero-count destructor; it does not freeze its room,
 * post vector, controller or other state. Revalidate those separately.
 *
 * Each native call has a resident pending obligation BEFORE entry. A failed
 * readback leaves that obligation uncertain and permanently prevents retries
 * in this isolated process. Do not reset this storage on STOP. A known partial
 * acquisition can be cleaned up, but no call releases an unowned reference.
 * Cleanup requires another live reference (count>1), allowing readable identity
 * checks after release without following an object that may have died.
 */

#define PLAYER_TARGET_HOLD_SHIP_REFERENCE_VTABLE 0x3d23c58u
#define PLAYER_TARGET_HOLD_COCKPIT_REFERENCE_VTABLE 0x3ca7de0u
#define PLAYER_TARGET_HOLD_ACQUIRE_VA 0xa238b0u
#define PLAYER_TARGET_HOLD_RELEASE_VA 0xa14540u
#define PLAYER_TARGET_HOLD_ZERO_THUNK_VA 0x18a7df0u
#define PLAYER_TARGET_HOLD_RUNTIME_OFFSET 0x8u
#define PLAYER_TARGET_HOLD_REFERENCE_OFFSET 0x38u
#define PLAYER_TARGET_HOLD_COUNT_OFFSET 0x40u
#define PLAYER_TARGET_HOLD_MARKER_OFFSET 0x44u
#define PLAYER_TARGET_HOLD_CLASS_GATE_OFFSET 0x68u
#define PLAYER_TARGET_HOLD_PARENT_OFFSET 0x70u
#define PLAYER_TARGET_HOLD_ROOM_OFFSET 0x408u

typedef enum {
    PLAYER_TARGET_HOLD_NO_CALL,
    PLAYER_TARGET_HOLD_ACQUIRE_TARGET,
    PLAYER_TARGET_HOLD_ACQUIRE_ROOM,
    PLAYER_TARGET_HOLD_RELEASE_ROOM,
    PLAYER_TARGET_HOLD_RELEASE_TARGET
} player_target_hold_call;

typedef struct {
    uintptr_t target;
    uint64_t target_runtime;
    uintptr_t target_vtable;
    uintptr_t target_reference_vtable;
    uint32_t target_count;
    uint32_t target_marker;
    uintptr_t room;
    uint64_t room_runtime;
    uintptr_t room_vtable;
    uintptr_t room_reference_vtable;
    uint32_t room_count;
    uint32_t room_marker;
    uint32_t room_class_gate;
    uintptr_t room_parent;
} player_target_hold_snapshot;

typedef struct {
    player_target_hold_snapshot identity;
    uintptr_t target_reference;
    uintptr_t room_reference;
    uint64_t worker_thread;
    unsigned target_owned;
    unsigned room_owned;
    unsigned busy;
    unsigned uncertain;
    unsigned returned;
    player_target_hold_call pending;
    const char *reason;
} player_target_hold;

/* Production has no operations override or externally supplied raw pointer. */
static player_target_hold player_target_hold_resident;

static int player_target_hold_pins(void) {
    static const lx_site pins[] = {
        {"target-hold-acquire", PLAYER_TARGET_HOLD_ACQUIRE_VA, 21,
         "0e7c126afb520730e79452b02c5cedd696ecdc918f11b71aab1501dead392f84"},
        {"target-hold-release", PLAYER_TARGET_HOLD_RELEASE_VA, 37,
         "7368cef95d1efcc8cda06dfd53df92ca026f1da024481b82f3c7a86b8db75587"},
        {"target-hold-find", 0xa68df0, 147,
         "6177d6aa24635956c7ed938eb80e67b6175ce95a1fb87bc1d0ef261eb9ef1684"},
        {"target-hold-zero-thunk", PLAYER_TARGET_HOLD_ZERO_THUNK_VA, 6,
         "6b79cd8bbfcde38ac5dc19c8484ad016c7c5dd0d77087043ca7767f2dcd2305c"},
        {"target-hold-zero-enqueue", 0x18a7d80, 112,
         "7b2e9ec6193bcbb295089fda312dd6921b47aae5323761c668d9569c509dff4f"},
        {"target-hold-Cockpit-predicate", 0xa39930, 48,
         "8bf9e521822333a1f5b82954f8081829f44d80b19b641fd7e96a19ed85b416f9"},
        {"target-hold-Cockpit-class19", 0x2c81ce4, 4,
         "52bdb0935fa7bf70e447a27904a02c550e7ff5115cf66ef3e5d93e668304923c"},
    };
    for (unsigned pin_index = 0; pin_index < sizeof(pins) / sizeof(pins[0]); ++pin_index) {
        if (!site_ok(&pins[pin_index])) {
            return 0;
        }
    }
    const unsigned reference_vtables[] = {
        PLAYER_TARGET_HOLD_SHIP_REFERENCE_VTABLE,
        PLAYER_TARGET_HOLD_COCKPIT_REFERENCE_VTABLE,
    };
    for (unsigned table_index = 0;
         table_index < sizeof(reference_vtables) / sizeof(reference_vtables[0]); ++table_index) {
        uintptr_t zero_dispatch = 0;
        if (!read_local(LX(reference_vtables[table_index]), &zero_dispatch,
                        sizeof(zero_dispatch)) ||
            zero_dispatch != (uintptr_t)LX(PLAYER_TARGET_HOLD_ZERO_THUNK_VA)) {
            return 0;
        }
    }
    uintptr_t class_predicate = 0;
    return read_local(LX(SEAT_PREFLIGHT_COCKPIT_VTABLE + 0x11f0), &class_predicate,
                      sizeof(class_predicate)) &&
           class_predicate == (uintptr_t)LX(0xa39930);
}

static int player_target_hold_boundary(void) {
    player_target_hold *hold = &player_target_hold_resident;
    return (!hold->worker_thread || hold->worker_thread == (uint64_t)GetCurrentThreadId()) &&
           factory_probe_joined_boundary() && player_target_hold_pins();
}

static int player_target_hold_read(uintptr_t object, unsigned offset, void *output, size_t length) {
    return object && object <= UINTPTR_MAX - offset && object + offset <= UINTPTR_MAX - length &&
           read_local((const void *)(object + offset), output, length);
}

/* The reused resolver scans the sealed import map and does a fresh registry
 * find(map,runtime,4,0). It has no preflight request or tick4 dependency. */
static int player_target_hold_snapshot_read(player_target_hold_snapshot *snapshot) {
    memset(snapshot, 0, sizeof(*snapshot));
    if (!player_target_hold_boundary() ||
        !player_seat_preflight_resolve(NULL, SEAT_PREFLIGHT_TARGET_SAVED, &snapshot->target,
                                       &snapshot->target_runtime) ||
        !snapshot->target_runtime || (snapshot->target & (sizeof(uintptr_t) - 1))) {
        return 0;
    }
    uint64_t runtime = 0;
    if (!player_target_hold_read(snapshot->target, 0, &snapshot->target_vtable, 8) ||
        snapshot->target_vtable != (uintptr_t)LX(SEAT_PREFLIGHT_SHIP_VTABLE) ||
        !player_target_hold_read(snapshot->target, PLAYER_TARGET_HOLD_RUNTIME_OFFSET, &runtime,
                                 8) ||
        runtime != snapshot->target_runtime ||
        !player_target_hold_read(snapshot->target, PLAYER_TARGET_HOLD_REFERENCE_OFFSET,
                                 &snapshot->target_reference_vtable, 8) ||
        snapshot->target_reference_vtable !=
            (uintptr_t)LX(PLAYER_TARGET_HOLD_SHIP_REFERENCE_VTABLE) ||
        !player_target_hold_read(snapshot->target, PLAYER_TARGET_HOLD_COUNT_OFFSET,
                                 &snapshot->target_count, 4) ||
        !snapshot->target_count || snapshot->target_count >= INT32_MAX ||
        !player_target_hold_read(snapshot->target, PLAYER_TARGET_HOLD_MARKER_OFFSET,
                                 &snapshot->target_marker, 4) ||
        snapshot->target_marker != 2 ||
        !player_target_hold_read(snapshot->target, PLAYER_TARGET_HOLD_ROOM_OFFSET, &snapshot->room,
                                 8) ||
        !snapshot->room || snapshot->room == snapshot->target ||
        (snapshot->room & (sizeof(uintptr_t) - 1)) ||
        !player_target_hold_read(snapshot->room, 0, &snapshot->room_vtable, 8) ||
        snapshot->room_vtable != (uintptr_t)LX(SEAT_PREFLIGHT_COCKPIT_VTABLE) ||
        !player_target_hold_read(snapshot->room, PLAYER_TARGET_HOLD_RUNTIME_OFFSET,
                                 &snapshot->room_runtime, 8) ||
        !snapshot->room_runtime || snapshot->room_runtime == snapshot->target_runtime ||
        !player_target_hold_read(snapshot->room, PLAYER_TARGET_HOLD_REFERENCE_OFFSET,
                                 &snapshot->room_reference_vtable, 8) ||
        snapshot->room_reference_vtable !=
            (uintptr_t)LX(PLAYER_TARGET_HOLD_COCKPIT_REFERENCE_VTABLE) ||
        !player_target_hold_read(snapshot->room, PLAYER_TARGET_HOLD_COUNT_OFFSET,
                                 &snapshot->room_count, 4) ||
        !snapshot->room_count || snapshot->room_count >= INT32_MAX ||
        !player_target_hold_read(snapshot->room, PLAYER_TARGET_HOLD_MARKER_OFFSET,
                                 &snapshot->room_marker, 4) ||
        snapshot->room_marker != 2 ||
        !player_target_hold_read(snapshot->room, PLAYER_TARGET_HOLD_CLASS_GATE_OFFSET,
                                 &snapshot->room_class_gate, 4) ||
        snapshot->room_class_gate ||
        !player_target_hold_read(snapshot->room, PLAYER_TARGET_HOLD_PARENT_OFFSET,
                                 &snapshot->room_parent, 8) ||
        snapshot->room_parent != snapshot->target || !player_target_hold_boundary()) {
        return 0;
    }
    return 1;
}

static int player_target_hold_identity_equal(const player_target_hold_snapshot *first,
                                             const player_target_hold_snapshot *second) {
    player_target_hold_snapshot before = *first;
    player_target_hold_snapshot after = *second;
    before.target_count = after.target_count = 0;
    before.room_count = after.room_count = 0;
    return !memcmp(&before, &after, sizeof(before));
}

static int player_target_hold_owned_slots(void) {
    player_target_hold *hold = &player_target_hold_resident;
    return hold->target_owned <= 1 && hold->room_owned <= 1 &&
           hold->target_reference == (hold->target_owned ? hold->identity.target : 0) &&
           hold->room_reference == (hold->room_owned ? hold->identity.room : 0) &&
           (!hold->room_owned || hold->target_owned);
}

static int player_target_hold_inspect(player_target_hold_snapshot *snapshot) {
    player_target_hold *hold = &player_target_hold_resident;
    return player_target_hold_owned_slots() && player_target_hold_snapshot_read(snapshot) &&
           player_target_hold_identity_equal(&hold->identity, snapshot);
}

/* busy is held by the outer operation. No exception or signal recovery is
 * attempted around native calls; pending/returned record the last known point. */
static int player_target_hold_apply(player_target_hold_call call) {
    player_target_hold *hold = &player_target_hold_resident;
    player_target_hold_snapshot before;
    player_target_hold_snapshot checked;
    unsigned target_call =
        call == PLAYER_TARGET_HOLD_ACQUIRE_TARGET || call == PLAYER_TARGET_HOLD_RELEASE_TARGET;
    unsigned acquiring =
        call == PLAYER_TARGET_HOLD_ACQUIRE_TARGET || call == PLAYER_TARGET_HOLD_ACQUIRE_ROOM;
    unsigned owned = target_call ? hold->target_owned : hold->room_owned;
    if (!hold->busy || hold->pending || hold->uncertain || owned == acquiring ||
        (call == PLAYER_TARGET_HOLD_ACQUIRE_ROOM && !hold->target_owned) ||
        (call == PLAYER_TARGET_HOLD_RELEASE_TARGET && hold->room_owned) ||
        !player_target_hold_inspect(&before)) {
        hold->reason = "before_call";
        return 0;
    }
    uint32_t count = target_call ? before.target_count : before.room_count;
    if ((acquiring && count >= INT32_MAX - 1) || (!acquiring && count <= 1) ||
        !player_target_hold_inspect(&checked) || memcmp(&before, &checked, sizeof(before)) ||
        !player_target_hold_boundary()) {
        hold->reason = "before_call_changed_or_last_reference";
        return 0;
    }
    uintptr_t object = target_call ? before.target : before.room;
    hold->pending = call;
    hold->returned = 0;
    if (acquiring) {
        ((void (*)(void *))LX(PLAYER_TARGET_HOLD_ACQUIRE_VA))((void *)object);
    } else {
        ((void (*)(void *))LX(PLAYER_TARGET_HOLD_RELEASE_VA))(
            (void *)(object + PLAYER_TARGET_HOLD_REFERENCE_OFFSET));
    }
    hold->returned = 1;
    player_target_hold_snapshot after;
    uint32_t expected_target_count = before.target_count;
    uint32_t expected_room_count = before.room_count;
    if (target_call) {
        expected_target_count = acquiring ? expected_target_count + 1 : expected_target_count - 1;
    } else {
        expected_room_count = acquiring ? expected_room_count + 1 : expected_room_count - 1;
    }
    if (!player_target_hold_inspect(&after) || after.target_count != expected_target_count ||
        after.room_count != expected_room_count || !player_target_hold_boundary()) {
        hold->uncertain = 1;
        hold->reason = "after_call_uncertain";
        return 0;
    }
    if (target_call) {
        hold->target_owned = acquiring;
        hold->target_reference = acquiring ? object : 0;
    } else {
        hold->room_owned = acquiring;
        hold->room_reference = acquiring ? object : 0;
    }
    hold->pending = PLAYER_TARGET_HOLD_NO_CALL;
    hold->returned = 0;
    return 1;
}

static int player_target_hold_acquire(void) {
    player_target_hold *hold = &player_target_hold_resident;
    if (hold->busy || hold->uncertain || hold->pending || hold->target_owned || hold->room_owned ||
        hold->target_reference || hold->room_reference) {
        return 0;
    }
    memset(hold, 0, sizeof(*hold));
    hold->busy = 1;
    hold->reason = "initial_snapshot";
    player_target_hold_snapshot checked;
    int success = player_target_hold_snapshot_read(&hold->identity) &&
                  player_target_hold_snapshot_read(&checked) &&
                  !memcmp(&hold->identity, &checked, sizeof(checked));
    if (success) {
        hold->worker_thread = (uint64_t)GetCurrentThreadId();
        success = player_target_hold_apply(PLAYER_TARGET_HOLD_ACQUIRE_TARGET) &&
                  player_target_hold_apply(PLAYER_TARGET_HOLD_ACQUIRE_ROOM);
    }
    if (success) {
        hold->reason = "held";
    }
    hold->busy = 0;
    return success;
}

static int player_target_hold_check(void) {
    player_target_hold *hold = &player_target_hold_resident;
    if (hold->busy || hold->uncertain || hold->pending || !hold->target_owned ||
        !hold->room_owned) {
        return 0;
    }
    hold->busy = 1;
    player_target_hold_snapshot snapshot;
    int success = player_target_hold_inspect(&snapshot) && snapshot.room_count > 1;
    hold->reason = success ? "held" : "held_identity_changed";
    hold->busy = 0;
    return success;
}

static __attribute__((unused)) int player_target_hold_release(void) {
    player_target_hold *hold = &player_target_hold_resident;
    if (hold->busy || hold->uncertain || hold->pending || !player_target_hold_owned_slots()) {
        return 0;
    }
    if (!hold->target_owned && !hold->room_owned) {
        return 1;
    }
    hold->busy = 1;
    int success =
        (!hold->room_owned || player_target_hold_apply(PLAYER_TARGET_HOLD_RELEASE_ROOM)) &&
        (!hold->target_owned || player_target_hold_apply(PLAYER_TARGET_HOLD_RELEASE_TARGET));
    if (success) {
        hold->reason = "released";
    }
    hold->busy = 0;
    return success;
}
#endif
