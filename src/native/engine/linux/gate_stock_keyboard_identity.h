/* Read-only identity/context sample for the bounded canonical-ship keyboard probe.
 *
 * collect() does not establish a scheduling boundary. Its caller must prove the
 * admitted MoveWorker/TLS identity, no active InputUpdate/selected callback, and
 * stable engine ownership for this whole call (post-input Lua or post-join).
 * Only the two pinned no-argument getters call engine code. No engine state is
 * written or retained. The caller binds its first ready sample and compares
 * future identities; cached pointers must never become fallback read targets.
 *
 * Return 1 means every requested field was read, even when ready is false.
 * Return 0 means a read, pin, or canonical identity check failed. A nonnull
 * output always receives ready=0 and an explicit reason on that path.
 * ready describes this limited STATE2..5 context, not full stock input.
 * Key-bank bytes are reported unchanged; the replay caller checks the expected
 * held mask, including its initial neutral baseline.
 * Include after gate_world.h and gate_stock_keyboard_observer.h.
 */
#ifndef X4_GATE_STOCK_KEYBOARD_IDENTITY_H
#define X4_GATE_STOCK_KEYBOARD_IDENTITY_H

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

/* Fixed production operations, injectable only through the internal collector
 * used by the engine-free fixture. No runtime override or cached result exists.
 */
typedef struct stock_keyboard_identity_ops {
    int (*pins)(void);
    int (*read)(uintptr_t address, void *output, size_t length);
    uintptr_t (*address)(unsigned va);
    uintptr_t (*getter)(unsigned va);
    uint64_t (*saved)(uint64_t runtime);
    uintptr_t observer_owner;
} stock_keyboard_identity_ops;

static int stock_keyboard_identity_pins(void) {
    static const lx_site pins[] = {
        {"canonical-ship-getter", 0x1dc8d40, 91,
         "1d146c0784618df3db973fdc923a604dffab70849c2edbd32a4b5e8f5b4124a5"},
        {"canonical-controller-getter", 0x1dd19d0, 137,
         "3cab7b30231bb96a1276459079a28e655b922186495473633288e7ede5c9f837"},
        {"controller-chain-lookup", 0x1dd1980, 67,
         "9d4bce92abf2fd3814ea62e9cd809e066a9f1a4be6aa694f7bbc52f48aa8bae9"},
        {"controlled-component-getter", 0x1699f20, 107,
         "875a39c3fadc0608768592821830bf99016fb500cdc452f788f7e69fa2091196"},
        {"ancestor-lookup", 0x189d8f0, 55,
         "7c5c18f7f341ce22b68eefdc5479b7e8c7e4d55fa0d259ed3e708edd44b951db"},
        {"player-slot-1ce0", 0x136c820, 68,
         "38cb2d7e1fe206c4a214526bcfd0d4274ce8237d350f9711334de76d950a578c"},
        {"playercontroller-slot-50", 0xd586b0, 18,
         "fb947f662987500d1c5d54ce6e9d10fd734c720ada645fddc23dec4bf1db7787"},
        {"playercontroller-slot-d0", 0xa863a0, 3,
         "251447ee91a9067dcd6ab96703133f617565974cd6c4819021760c4688c91abf"},
        {"shipsmall-slot-11f0", 0xa389f0, 48,
         "6cc846478894fc1636f1416467c973ff24c09d48b3207c8f8b4fa37d6bfd6e42"},
        {"controller-authority", 0x1658d60, 86,
         "1a796356b0e1ca17860fbc4cff9fa7303ab57a43f136b622d1f5a6dfd160fbcf"},
        {"input-mouse-predicate", 0xce1290, 48,
         "3a78d090ea254b6cade8ab2de69cd13d4c49c54f73ab454a4e2d5c40a1985d82"},
        {"canonical-controller-update", 0x166d020, 15106,
         "f5fb3e3afe84b9c4f1ecd7c27088d49e6cf6e84971c2d8af3c05bdba15e4143f"},
        {"bank-setup", 0x165b070, 1473,
         "a0ccbcb5f7d9bc7da48f171e8f5d1c73a57f23bd70af536fe2267465e8ab80ee"},
        {"state-handler", 0x166c6f0, 2349,
         "369d1664b722dec42bee51f949b3c0c32c16bf1353e00412e271ffbedb6dd335"},
        {"context-active-update-loop", 0xcecd27, 105,
         "a01c5bc454d54e044f2dcd95e7a2f3bb0e5c93e5cef4eb732817ea43aeb8977b"},
        {"state2-to5-jumptable", 0x2d6ddc4, 16,
         "b1a4a64f7eac17554d555806cdbc1c61e4ae138a1f058073dc25bb3d621c0670"},
    };
    for (unsigned pin_index = 0; pin_index < sizeof(pins) / sizeof(pins[0]); ++pin_index) {
        if (!site_ok(&pins[pin_index])) {
            return 0;
        }
    }
    /* Vtable pointer words are relocated at load time: compare their resolved
     * values rather than hashing the file's unrelocated pointer bytes.
     */
    static const unsigned slots[][2] = {
        {0x3b61480, 0x136c820},
        {0x3b7c258, 0xd586b0},
        {0x3b7c2d8, 0xa863a0},
        {0x3d22938, 0xa389f0},
    };
    for (unsigned slot_index = 0; slot_index < sizeof(slots) / sizeof(slots[0]); ++slot_index) {
        uintptr_t target = 0;
        if (!read_local(LX(slots[slot_index][0]), &target, sizeof(target)) ||
            target != (uintptr_t)LX(slots[slot_index][1])) {
            return 0;
        }
    }
    return 1;
}

static int stock_keyboard_identity_read(uintptr_t address, void *output, size_t length) {
    return address && read_local((const void *)address, output, length);
}

static uintptr_t stock_keyboard_identity_address(unsigned va) {
    return (uintptr_t)LX(va);
}

static uintptr_t stock_keyboard_identity_getter(unsigned va) {
    return (uintptr_t)((void *(*)(void))LX(va))();
}

static int stock_keyboard_identity_reject(stock_keyboard_identity_sample *sample, const char *reason) {
    sample->reason = reason;
    return 0;
}

/* Cursor mode is only for the authenticated convergence consumer; 16 is a
 * conservative admission bound, not a claim about native device calibration. */
/* Explicit local UI and delayed simulation states. The caller supplies the
 * authenticated states for this boundary; differing states are expected during
 * network delay. This is not permission to accept arbitrary native bytes. */
static int stock_keyboard_identity_collect_states(stock_keyboard_identity_sample *sample,
                                                   const stock_keyboard_identity_ops *operations,
                                                   unsigned cursor_mode, unsigned local_active,
                                                   unsigned replay_active) {
    if (!sample || !operations || cursor_mode > 1 || local_active > 1 || replay_active > 1) {
        return 0;
    }
    memset(sample, 0, sizeof(*sample));
    sample->reason = "pins";
    if (!operations->pins()) {
        return 0;
    }
#define SKI_READ(address, field) operations->read((uintptr_t)(address), &(field), sizeof(field))
#define SKI_GLOBAL(va, field) SKI_READ(operations->address(va), field)
    if (!SKI_GLOBAL(0x3db6948, sample->universe) || !sample->universe ||
        !SKI_READ(sample->universe + 0x238, sample->player) || !sample->player ||
        !SKI_READ(sample->player, sample->player_vtable) ||
        sample->player_vtable != operations->address(0x3b5f7a0)) {
        return stock_keyboard_identity_reject(sample, "player_identity");
    }
    if (!SKI_READ(sample->player + 8, sample->player_runtime) ||
        !(sample->player_saved = operations->saved(sample->player_runtime))) {
        return stock_keyboard_identity_reject(sample, "player_saved_identity");
    }
    sample->ship = operations->getter(0x1dc8d40);
    if (!sample->ship || !SKI_READ(sample->ship, sample->ship_vtable) ||
        sample->ship_vtable != operations->address(0x3d21748)) {
        return stock_keyboard_identity_reject(sample, "ship_identity");
    }
    sample->controller = operations->getter(0x1dd19d0);
    if (!sample->controller || !SKI_READ(sample->controller, sample->controller_vtable) ||
        sample->controller_vtable != operations->address(0x3b7c208) ||
        !SKI_READ(sample->controller + 0x20, sample->controller_owner) ||
        sample->controller_owner != sample->ship) {
        return stock_keyboard_identity_reject(sample, "controller_identity");
    }
    if (!SKI_READ(sample->ship + 8, sample->runtime) ||
        !(sample->saved = operations->saved(sample->runtime))) {
        return stock_keyboard_identity_reject(sample, "ship_saved_identity");
    }
    if (!SKI_GLOBAL(0x3daaaf8, sample->input_owner) || !sample->input_owner ||
        sample->input_owner != operations->observer_owner) {
        return stock_keyboard_identity_reject(sample, "input_owner_identity");
    }
    if (!SKI_READ(sample->input_owner + 0xcf0, sample->context_cf0) ||
        !SKI_READ(sample->input_owner + 0x9f36, sample->mouse) ||
        !SKI_READ(sample->ship + 0xd1, sample->ship_d1) ||
        !SKI_READ(sample->controller + 0xd9, sample->controller_d9) ||
        !SKI_GLOBAL(0x3d8daa8, sample->flight) || !SKI_GLOBAL(0x3d8daa9, sample->requested_space) ||
        !SKI_GLOBAL(0x3d8da60, sample->enabled) || !SKI_GLOBAL(0x3d8da64, sample->count) ||
        !SKI_GLOBAL(0x3d8da58, sample->autopilot) || !SKI_GLOBAL(0x3e5b29a, sample->ignore) ||
        !SKI_GLOBAL(0x3d8da68, sample->keys) || !SKI_GLOBAL(0x3d8d9a0, sample->axes) ||
        !SKI_GLOBAL(0x3d8d9b8, sample->cursor)) {
        return stock_keyboard_identity_reject(sample, "context_read");
    }
#undef SKI_GLOBAL
#undef SKI_READ
    sample->reason = "ready";
    if (sample->context_cf0 != local_active || sample->requested_space != 1 ||
        sample->flight != replay_active) {
        sample->reason = "space_context";
    } else if (sample->enabled != 1 || sample->count != 0) {
        sample->reason = "controls_disabled";
    } else if (sample->autopilot != 0) {
        sample->reason = "autopilot";
    } else if (sample->ignore != 0) {
        sample->reason = "input_ignored";
    } else if (sample->ship_d1 != 1 || sample->controller_d9 != 1) {
        sample->reason = "controller_inactive";
    } else if ((!cursor_mode && (sample->mouse[0] || sample->mouse[1] || sample->mouse[2])) ||
               (cursor_mode && (sample->mouse[0] > 1 || sample->mouse[1] > 1 || sample->mouse[2] > 1))) {
        sample->reason = "mouse_mode";
    } else {
        for (unsigned axis_index = 0; axis_index < 3; ++axis_index) {
            if (!isfinite(sample->axes[axis_index]) || !isfinite(sample->cursor[axis_index])) {
                sample->reason = "nonfinite_axes";
                break;
            }
            if ((!cursor_mode && (sample->axes[axis_index] != 0 || sample->cursor[axis_index] != 0)) ||
                (cursor_mode && (fabsf(sample->axes[axis_index]) > 16.0f ||
                                 fabsf(sample->cursor[axis_index]) > 16.0f))) {
                sample->reason = cursor_mode ? "axes_out_of_range" : "nonzero_axes";
                break;
            }
        }
    }
    sample->ready = !strcmp(sample->reason, "ready");
    return 1;
}

/* Keep all older callers active-flight-only. SPACE callers must explicitly
 * supply both expected states through collect_states. */
static int stock_keyboard_identity_collect_with_mode(stock_keyboard_identity_sample *sample,
                                                     const stock_keyboard_identity_ops *operations,
                                                     unsigned cursor_mode) {
    return stock_keyboard_identity_collect_states(sample, operations, cursor_mode, 1, 1);
}

static int stock_keyboard_identity_collect_with(stock_keyboard_identity_sample *sample,
                                                const stock_keyboard_identity_ops *operations) {
    return stock_keyboard_identity_collect_with_mode(sample, operations, 0);
}

static int stock_keyboard_identity_collect(stock_keyboard_identity_sample *sample) {
    const stock_keyboard_identity_ops operations = {
        stock_keyboard_identity_pins,   stock_keyboard_identity_read, stock_keyboard_identity_address,
        stock_keyboard_identity_getter, world_identity_lookup,        sko_session.owner,
    };
    return stock_keyboard_identity_collect_with(sample, &operations);
}

/* Caller must first authenticate and bind the cursor consumer. */
static __attribute__((unused)) int stock_keyboard_identity_collect_cursor(stock_keyboard_identity_sample *sample) {
    const stock_keyboard_identity_ops operations = {
        stock_keyboard_identity_pins, stock_keyboard_identity_read, stock_keyboard_identity_address,
        stock_keyboard_identity_getter, world_identity_lookup, sko_session.owner,
    };
    return stock_keyboard_identity_collect_with_mode(sample, &operations, 1);
}

static int lua_stock_keyboard_identity(lua_State *L) {
    if (!stock_observer_worker_boundary() || !atomic_load(&sko_session.active)) {
        p_lua_pushnil(L);
        return 1;
    }
    stock_keyboard_identity_sample sample;
    int complete = stock_keyboard_identity_collect(&sample);
    char output[1536];
    int length =
        snprintf(output, sizeof(output),
                 "complete=%d ready=%d reason=%s universe=%llx player=%llx controller=%llx ship=%llx "
                 "input_owner=%llx controller_owner=%llx player_vtable=%llx controller_vtable=%llx "
                 "ship_vtable=%llx player_runtime=%llu player_saved=%016llx runtime=%llu saved=%016llx "
                 "context_cf0=%u requested_space=%u flight=%u enabled=%u count=%u autopilot=%llx "
                 "ignore=%u ship_d1=%u controller_d9=%u mouse=%u,%u,%u keys=%u,%u,%u,%u "
                 "axes=%.9g,%.9g,%.9g "
                 "cursor=%.9g,%.9g,%.9g",
                 complete, sample.ready, sample.reason, (unsigned long long)sample.universe,
                 (unsigned long long)sample.player, (unsigned long long)sample.controller,
                 (unsigned long long)sample.ship, (unsigned long long)sample.input_owner,
                 (unsigned long long)sample.controller_owner, (unsigned long long)sample.player_vtable,
                 (unsigned long long)sample.controller_vtable, (unsigned long long)sample.ship_vtable,
                 (unsigned long long)sample.player_runtime, (unsigned long long)sample.player_saved,
                 (unsigned long long)sample.runtime, (unsigned long long)sample.saved,
                 sample.context_cf0, sample.requested_space, sample.flight, sample.enabled, sample.count,
                 (unsigned long long)sample.autopilot, sample.ignore, sample.ship_d1,
                 sample.controller_d9, sample.mouse[0], sample.mouse[1], sample.mouse[2], sample.keys[0],
                 sample.keys[1], sample.keys[2], sample.keys[3], sample.axes[0], sample.axes[1],
                 sample.axes[2], sample.cursor[0], sample.cursor[1], sample.cursor[2]);
    if (length < 0 || (size_t)length >= sizeof(output)) {
        p_lua_pushnil(L);
    } else {
        p_lua_pushlstring(L, output, (size_t)length);
    }
    return 1;
}
#endif
