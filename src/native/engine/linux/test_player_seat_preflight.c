/* Fake owned Player objects and registry only; never launches/calls X4. */
#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "player_startup_phases.h"

/* Match the real default startup schedule, rather than a fixture-only epoch. */
static const player_startup_phases *player_startup(void) {
    static player_startup_phases phase;
    static int initialized;
    if (!initialized) {
        assert(player_startup_phases_parse(NULL, &phase));
        initialized = 1;
    }
    return &phase;
}

#define WORLD_IDENTITY_CAPACITY 8u
#define LX_COMPONENT_MAP_VA 0x3daa860u
#define LX_FIND_VA 0xa68df0u
#define SKO_OWNER_VA 0x3daaaf8u
#define LX(va) ((void *)(uintptr_t)(va))

typedef struct {
    int ready;
    const char *reason;
    uintptr_t universe, player, controller, ship, input_owner, controller_owner;
    uintptr_t player_vtable, controller_vtable, ship_vtable;
    uint64_t player_runtime, player_saved, runtime, saved, autopilot;
    uint32_t count;
    unsigned char context_cf0, requested_space, flight, enabled, ignore, ship_d1, controller_d9;
    unsigned char mouse[3], keys[4];
    float axes[3], cursor[3];
} stock_keyboard_identity_sample;
typedef struct {
    const char *name;
    unsigned va, length;
    const char *sha256;
} lx_site;
typedef struct {
    unsigned number, nil;
    char text[3400];
} lua_State;
typedef struct {
    uint64_t runtime, saved;
} world_identity_entry;
typedef void *(*lxw_find_fn)(void *, uint64_t, unsigned, unsigned);
static world_identity_entry import_entries[WORLD_IDENTITY_CAPACITY];
static world_identity_entry *world_identities = import_entries;
static unsigned world_identity_sealed;
static int world_identity_installed = 1, world_identity_seal_generation_exhausted;
static int world_identity_seal_admitted = 1;
static int64_t world_identity_count = 1, world_identity_failures;
static uint64_t world_identity_seal_generation = 2;
static pthread_mutex_t lxw_insert_lock = PTHREAD_MUTEX_INITIALIZER;
static void **world_component_map;
static lxw_find_fn world_find;
static struct {
    unsigned active;
    uint64_t issued;
} stats;
#define PLAYER_FACTORY_RETAINED 6u
static struct {
    unsigned phase, player2_owned, player2_uncertain, release2_pending;
    unsigned player1_owned, player1_uncertain, forward_pending, interceptor_active;
    unsigned constructor_pending, assignment_pending, singleton_uncertain, release1_pending;
    const char *problem;
    uintptr_t player2;
} factory_probe_state;
static uintptr_t factory_probe_macro;
static uint64_t factory_probe_player2_runtime, factory_probe_release_tick;
static unsigned factory_probe_patch_owned, factory_probe_code_permission_pending,
    factory_probe_inflight;
static unsigned factory_probe_thunk_permission_pending;
static atomic_uint factory_probe_scope, factory_probe_wrapper_depth;
static unsigned joined_boundary, worker_boundary, accepted_ui, valid_pins, valid_identity;
static unsigned canonical_calls, resolve_calls, change_target, change_canonical, change_position;
static unsigned change_seal_during_find;
static unsigned reads, forbidden_reads, pin_calls;
static uintptr_t unreadable;
static unsigned char player1[0x6ac0], player2[0x6ac0];
static unsigned char target[0x430], target_replaced[0x430], room[0x80], pilot_post[0x1b8];
static unsigned char controller[0x30];
static uintptr_t vector1[3], vector2[3];
_Alignas(16) static uintptr_t post_records[4][2];
static unsigned change_post_record, change_post_vector, post_record_reads;
static uintptr_t navigation_predicate, navigation_next;
static uintptr_t post_global, animation_a, animation_b;
static uint32_t pilot_position, invalid_position, deactivation_count;
static unsigned char controls_enabled, contexts_requested;
static unsigned char registry;
static unsigned char input_owner[0x4cb0];
static unsigned char context_nodes[3][0x30];
static struct {
    uintptr_t owner;
} sko_session;
static stock_keyboard_identity_sample canonical_identity;

static int read_local(const void *address, void *output, size_t length);
static int accept_ui(void) {
    return accepted_ui;
}
static int stock_observer_worker_boundary(void) {
    return worker_boundary;
}
static int factory_probe_joined_boundary(void) {
    return joined_boundary;
}
static int site_ok(const lx_site *pin) {
    ++pin_calls;
    assert(pin->length && strlen(pin->sha256) == 64);
    return valid_pins;
}
static int stock_keyboard_identity_collect(stock_keyboard_identity_sample *sample) {
    ++canonical_calls;
    memcpy(sample, &canonical_identity, sizeof(*sample));
    if (change_canonical && canonical_calls == 2) {
        sample->input_owner += 8;
    }
    return valid_identity;
}
static int factory_probe_object(uintptr_t player, uintptr_t macro, uint64_t *runtime,
                                uint32_t *count, uint32_t *marker) {
    if (player != (uintptr_t)player2 || macro != factory_probe_macro) {
        return 0;
    }
    *count = 12;
    *marker = 2;
    return read_local((void *)(player + 8), runtime, 8);
}
static uint64_t world_identity_lookup(uint64_t runtime) {
    for (unsigned index = 0; index < WORLD_IDENTITY_CAPACITY; ++index) {
        if (world_identities[index].runtime == runtime) {
            return world_identities[index].saved;
        }
    }
    return 0;
}
static void *fake_find(void *map, uint64_t runtime, unsigned mode, unsigned flags) {
    assert(map == &registry && runtime == 9001 && mode == 4 && flags == 0);
    ++resolve_calls;
    if (change_seal_during_find) ++world_identity_seal_generation;
    return change_target && resolve_calls == 2 ? target_replaced : target;
}
static void p_lua_pushnil(lua_State *state) {
    state->nil = 1;
}
static void p_lua_pushnumber(lua_State *state, double value) {
    state->number = (unsigned)value;
}
static void p_lua_pushlstring(lua_State *state, const char *text, size_t length) {
    assert(length < sizeof(state->text));
    memcpy(state->text, text, length);
    state->text[length] = 0;
}
#include "gate_player_seat_preflight.h"

static int read_local(const void *address, void *output, size_t length) {
    uintptr_t source = (uintptr_t)address;
    ++reads;
    assert(source != animation_a || !animation_a);
    assert(source != animation_b || !animation_b);
    if (source >= 0x60000000 && source < 0x60010000) {
        ++forbidden_reads;
        assert(!"unproved post-array dereference");
    }
    if (source == unreadable) {
        return 0;
    }
#define FAKE_GLOBAL(va, field)                                                                     \
    if (source == (va)) {                                                                          \
        assert(length == sizeof(field));                                                           \
        memcpy(output, &(field), length);                                                          \
        return 1;                                                                                  \
    }
    FAKE_GLOBAL(SEAT_PREFLIGHT_PILOT_POST_GLOBAL, post_global)
    FAKE_GLOBAL(SEAT_PREFLIGHT_PILOT_POSITION_GLOBAL, pilot_position)
    FAKE_GLOBAL(SEAT_PREFLIGHT_INVALID_POSITION_GLOBAL, invalid_position)
    FAKE_GLOBAL(SEAT_PREFLIGHT_ANIMATION_HANDLE_A, animation_a)
    FAKE_GLOBAL(SEAT_PREFLIGHT_ANIMATION_HANDLE_B, animation_b)
    FAKE_GLOBAL(SEAT_PREFLIGHT_CONTROLS_ENABLED, controls_enabled)
    FAKE_GLOBAL(SEAT_PREFLIGHT_DEACTIVATION_COUNT, deactivation_count)
    FAKE_GLOBAL(SEAT_PREFLIGHT_CONTEXTS_REQUESTED, contexts_requested)
#undef FAKE_GLOBAL
    if (source == SKO_OWNER_VA) {
        memcpy(output, &canonical_identity.input_owner, 8);
        return 1;
    }
    if (source == LX_COMPONENT_MAP_VA) {
        void *map = &registry;
        memcpy(output, &map, length);
        return 1;
    }
    if (source == SEAT_PREFLIGHT_ROOM_VTABLE + 0x11f0 ||
        source == SEAT_PREFLIGHT_CONTROL_ROOM_VTABLE + 0x11f0 ||
        source == SEAT_PREFLIGHT_COCKPIT_VTABLE + 0x11f0) {
        uintptr_t function = source == SEAT_PREFLIGHT_ROOM_VTABLE + 0x11f0      ? 0xa37db0
                             : source == SEAT_PREFLIGHT_COCKPIT_VTABLE + 0x11f0 ? 0xa39930
                                                                                : 0xa398e0;
        memcpy(output, &function, length);
        return 1;
    }
    if (source == SEAT_PREFLIGHT_NAVIGATION_CONTROLLER_VTABLE + 0x50 ||
        source == SEAT_PREFLIGHT_NAVIGATION_CONTROLLER_VTABLE + 0xd0) {
        uintptr_t function = source == SEAT_PREFLIGHT_NAVIGATION_CONTROLLER_VTABLE + 0x50
                                 ? navigation_predicate
                                 : navigation_next;
        memcpy(output, &function, length);
        return 1;
    }
    struct {
        const void *address;
        size_t length;
    } regions[] = {
        {player1, sizeof(player1)},
        {player2, sizeof(player2)},
        {target, sizeof(target)},
        {target_replaced, sizeof(target_replaced)},
        {room, sizeof(room)},
        {pilot_post, sizeof(pilot_post)},
        {controller, sizeof(controller)},
        {vector1, sizeof(vector1)},
        {vector2, sizeof(vector2)},
        {post_records, sizeof(post_records)},
        {input_owner, sizeof(input_owner)},
        {context_nodes, sizeof(context_nodes)},
    };
    for (unsigned index = 0; index < sizeof(regions) / sizeof(regions[0]); ++index) {
        uintptr_t begin = (uintptr_t)regions[index].address;
        if (source >= begin && source - begin <= regions[index].length &&
            length <= regions[index].length - (source - begin)) {
            if (change_position && source == (uintptr_t)player2 + 0x6aa8 && canonical_calls == 2) {
                uint32_t replacement = 99;
                memcpy(player2 + 0x6aa8, &replacement, 4);
            }
            if (change_post_vector && source == (uintptr_t)target + 0x418 && canonical_calls == 2) {
                uintptr_t shortened = (uintptr_t)(post_records + 2);
                memcpy(target + 0x418, &shortened, 8);
            }
            if (source == (uintptr_t)post_records[2]) {
                ++post_record_reads;
                if (change_post_record && canonical_calls == 2) {
                    post_records[2][1] += 8;
                }
            }
            memcpy(output, address, length);
            return 1;
        }
    }
    return 0;
}
static void set_pointer(void *object, unsigned offset, uintptr_t value) {
    memcpy((unsigned char *)object + offset, &value, 8);
}
static void set_u32(void *object, unsigned offset, uint32_t value) {
    memcpy((unsigned char *)object + offset, &value, 4);
}
static void setup(void) {
    stats.active = stats.issued = 1;
    accepted_ui = worker_boundary = joined_boundary = valid_pins = valid_identity = 1;
    controls_enabled = contexts_requested = 1;
    navigation_predicate = 0xd58680;
    navigation_next = 0xa863a0;
    factory_probe_state.phase = PLAYER_FACTORY_RETAINED;
    factory_probe_state.player2_owned = 1;
    factory_probe_state.player2 = (uintptr_t)player2;
    factory_probe_macro = 0x778899;
    factory_probe_player2_runtime = 777;
    factory_probe_release_tick = 60;
    post_global = (uintptr_t)pilot_post;
    pilot_position = 11;
    invalid_position = UINT32_MAX;
    for (unsigned player_index = 0; player_index < 2; ++player_index) {
        unsigned char *player = player_index ? player2 : player1;
        uintptr_t *vector = player_index ? vector2 : vector1;
        set_pointer(player, 0, SEAT_PREFLIGHT_PLAYER_VTABLE);
        set_pointer(player, 8, player_index ? 777 : 666);
        set_pointer(player, 0x308, (uintptr_t)vector);
        set_pointer(player, 0x310, (uintptr_t)(vector + 3));
        set_pointer(player, 0x318, (uintptr_t)(vector + 3));
    }
    set_u32(player1, 0x6aa8, pilot_position);
    set_u32(player2, 0x6aa8, invalid_position);
    vector1[2] = 0x123456;
    pilot_post[0x110] = 1;
    set_pointer(pilot_post, 0x1b0, 2); /* Index is deliberately not0. */
    set_pointer(target, 0, SEAT_PREFLIGHT_SHIP_VTABLE);
    set_pointer(target, 8, 9001);
    set_pointer(target, 0x408, (uintptr_t)room);
    set_pointer(target, 0x410, (uintptr_t)post_records);
    set_pointer(target, 0x418, (uintptr_t)(post_records + 3));
    set_pointer(target, 0x420, (uintptr_t)(post_records + 4));
    post_records[2][0] = 0x60000000; /* Raw definition and actor: never followed. */
    post_records[2][1] = 0x60000008;
    memcpy(target_replaced, target, sizeof(target));
    set_pointer(room, 0, SEAT_PREFLIGHT_CONTROL_ROOM_VTABLE);
    set_pointer(room, 0x70, (uintptr_t)target);
    memset(&canonical_identity, 0, sizeof(canonical_identity));
    canonical_identity.ready = 1;
    canonical_identity.reason = "ready";
    canonical_identity.player = (uintptr_t)player1;
    canonical_identity.player_runtime = 666;
    canonical_identity.ship = 0x776655;
    canonical_identity.input_owner = (uintptr_t)input_owner;
    sko_session.owner = (uintptr_t)input_owner;
    player_seat_preflight_input_owner = (uintptr_t)input_owner;
    unsigned char *context_header = input_owner + 0x10 + 0x4c50;
    set_pointer(context_header, 8, (uintptr_t)context_nodes[1]);
    set_pointer(context_header, 0x20, (uintptr_t)context_nodes[0]);
    set_pointer(context_header, 0x28, 3);
    set_pointer(context_header, 0x30, 3);
    set_pointer(context_nodes[1], 0, (uintptr_t)context_header);
    set_pointer(context_nodes[1], 8, (uintptr_t)context_nodes[0]);
    set_pointer(context_nodes[1], 0x10, (uintptr_t)context_nodes[2]);
    set_u32(context_nodes[1], 0x18, 1);
    set_pointer(context_nodes[0], 0, (uintptr_t)context_nodes[1]);
    set_pointer(context_nodes[2], 0, (uintptr_t)context_nodes[1]);
    const unsigned contexts[3] = {8, 15, 16};
    for (unsigned index = 0; index < 3; ++index) {
        set_u32(context_nodes[index], 0x1c, contexts[index]);
        set_u32(context_nodes[index], 0x20, 6);
        context_nodes[index][0x28] = 1;
        context_nodes[index][0x29] = (unsigned char)(0x80 + index);
        context_nodes[index][0x2a] = 0xa5;
        context_nodes[index][0x2b] = 0x5a;
    }
    world_identity_sealed = 1;
    import_entries[3] = (world_identity_entry){9001, SEAT_PREFLIGHT_TARGET_SAVED};
    world_find = fake_find;
    stats.issued = 4;
    player_seat_preflight_requested = 1;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    setup();
    const char *scenario = argv[1];
    if (!strncmp(scenario, "cache_", 6)) {
        uintptr_t object = 0;
        uint64_t runtime = 0;
        assert(player_seat_preflight_resolve(NULL, SEAT_PREFLIGHT_TARGET_SAVED, &object, &runtime));
        assert(object == (uintptr_t)target && runtime == 9001 && resolve_calls == 1);
        assert(player_seat_target_cache.initialized && player_seat_target_cache.runtime == 9001);
        world_identity_entry replacement[WORLD_IDENTITY_CAPACITY];
        memcpy(replacement, import_entries, sizeof(replacement));
        if (!strcmp(scenario, "cache_fresh_object")) {
            change_target = 1;
            assert(player_seat_preflight_resolve(NULL, SEAT_PREFLIGHT_TARGET_SAVED, &object, &runtime));
            assert(object == (uintptr_t)target_replaced && runtime == 9001 && resolve_calls == 2);
            return 0;
        } else if (!strcmp(scenario, "cache_generation")) {
            ++world_identity_seal_generation;
        } else if (!strcmp(scenario, "cache_unsealed")) {
            world_identity_sealed = 0;
        } else if (!strcmp(scenario, "cache_replaced_table")) {
            world_identities = replacement;
        } else if (!strcmp(scenario, "cache_count")) {
            ++world_identity_count;
        } else if (!strcmp(scenario, "cache_failures")) {
            world_identity_failures = 1;
        } else if (!strcmp(scenario, "cache_restored")) {
            world_identity_installed = 0;
        } else if (!strcmp(scenario, "cache_restore_started")) {
            world_identity_seal_admitted = 0;
        } else if (!strcmp(scenario, "cache_entry_drift")) {
            import_entries[3].runtime = 9002;
        } else if (!strcmp(scenario, "cache_saved_drift")) {
            import_entries[3].saved = 0x123;
        } else if (!strcmp(scenario, "cache_exhausted")) {
            world_identity_seal_generation_exhausted = 1;
        } else {
            assert(!"unknown cache scenario");
        }
        assert(!player_seat_preflight_resolve(NULL, SEAT_PREFLIGHT_TARGET_SAVED, &object, &runtime));
        assert(!object && !runtime && resolve_calls == 1);
        return 0;
    }
    if (!strcmp(scenario, "off")) {
        player_seat_preflight_requested = 0;
        assert(player_seat_preflight_after_join());
        assert(!reads && !pin_calls && !resolve_calls);
        return 0;
    }
    if (!strcmp(scenario, "request")) {
        player_seat_preflight_requested = 0;
        stats.issued = 2;
        lua_State state = {0};
        lua_player_seat_preflight_request(&state);
        assert(state.number && player_seat_preflight_requested);
        assert(reads == 1 && !resolve_calls);
        assert(player_seat_preflight_after_join() && reads == 1);
        stats.issued = 4;
    } else if (!strcmp(scenario, "request_wrong_worker")) {
        player_seat_preflight_requested = 0;
        stats.issued = 2;
        worker_boundary = 0;
        lua_State state = {0};
        lua_player_seat_preflight_request(&state);
        assert(!state.number && !player_seat_preflight_requested && !reads);
        return 0;
    } else if (!strcmp(scenario, "request_wrong_tick")) {
        player_seat_preflight_requested = 0;
        lua_State state = {0};
        lua_player_seat_preflight_request(&state);
        assert(!state.number && !reads);
        return 0;
    } else if (!strcmp(scenario, "request_owner_mismatch")) {
        player_seat_preflight_requested = 0;
        stats.issued = 2;
        sko_session.owner += 8;
        lua_State state = {0};
        lua_player_seat_preflight_request(&state);
        assert(!state.number && !player_seat_preflight_requested);
        return 0;
    } else if (!strcmp(scenario, "context_missing")) {
        set_u32(context_nodes[2], 0x20, 7);
    } else if (!strcmp(scenario, "context_invalid")) {
        set_pointer(context_nodes[2], 0, (uintptr_t)input_owner);
    } else if (!strcmp(scenario, "context_unreadable")) {
        unreadable = (uintptr_t)input_owner + 0x10 + 0x4c50;
    } else if (!strcmp(scenario, "owner_change_before_collect")) {
        canonical_identity.input_owner += 8;
    } else if (!strcmp(scenario, "inflight")) {
        factory_probe_inflight = 1;
    } else if (!strcmp(scenario, "singleton_uncertain")) {
        factory_probe_state.singleton_uncertain = 1;
    } else if (!strcmp(scenario, "room_plain")) {
        set_pointer(room, 0, SEAT_PREFLIGHT_ROOM_VTABLE);
    } else if (!strcmp(scenario, "room_gated")) {
        set_u32(room, 0x68, 1);
    } else if (!strcmp(scenario, "room_unknown")) {
        set_pointer(room, 0, 0x445566);
    } else if (!strcmp(scenario, "cockpit") || !strcmp(scenario, "cockpit_gated") ||
               !strcmp(scenario, "live_cockpit_navigation")) {
        set_pointer(room, 0, SEAT_PREFLIGHT_COCKPIT_VTABLE);
        if (!strcmp(scenario, "cockpit_gated")) {
            set_u32(room, 0x68, 1);
        }
        if (!strcmp(scenario, "live_cockpit_navigation")) {
            set_pointer(target, 0x200, (uintptr_t)controller);
            set_pointer(controller, 0, SEAT_PREFLIGHT_NAVIGATION_CONTROLLER_VTABLE);
        }
    } else if (!strcmp(scenario, "room_cycle")) {
        set_pointer(room, 0x70, (uintptr_t)room);
    } else if (!strcmp(scenario, "controller_unknown")) {
        set_pointer(target, 0x200, (uintptr_t)controller);
        set_pointer(controller, 0, 0x112244);
    } else if (!strcmp(scenario, "controller_player")) {
        set_pointer(target, 0x200, (uintptr_t)controller);
        set_pointer(controller, 0, SEAT_PREFLIGHT_CONTROLLER_VTABLE);
    } else if (!strcmp(scenario, "controller_navigation") ||
               !strcmp(scenario, "navigation_unexpected_next") ||
               !strcmp(scenario, "navigation_unexpected_predicate")) {
        set_pointer(target, 0x200, (uintptr_t)controller);
        set_pointer(controller, 0, SEAT_PREFLIGHT_NAVIGATION_CONTROLLER_VTABLE);
        if (!strcmp(scenario, "navigation_unexpected_next")) {
            navigation_next += 16;
        }
        if (!strcmp(scenario, "navigation_unexpected_predicate")) {
            navigation_predicate += 16;
        }
    } else if (!strcmp(scenario, "animation_handles")) {
        animation_a = 0xdead0000;
        animation_b = 0xbeef0000;
    } else if (!strcmp(scenario, "context_disabled")) {
        deactivation_count = 2;
        contexts_requested = 0;
    } else if (!strcmp(scenario, "large_post_index")) {
        set_pointer(pilot_post, 0x1b0, UINTPTR_MAX);
    } else if (!strcmp(scenario, "post_short")) {
        set_pointer(target, 0x418, (uintptr_t)(post_records + 2));
    } else if (!strcmp(scenario, "post_index_outside")) {
        set_pointer(pilot_post, 0x1b0, 4);
    } else if (!strcmp(scenario, "post_empty")) {
        set_pointer(target, 0x410, 0);
        set_pointer(target, 0x418, 0);
        set_pointer(target, 0x420, 0);
    } else if (!strcmp(scenario, "post_capacity_before_end")) {
        set_pointer(target, 0x420, (uintptr_t)(post_records + 2));
    } else if (!strcmp(scenario, "post_begin_after_end")) {
        set_pointer(target, 0x410, (uintptr_t)(post_records + 4));
    } else if (!strcmp(scenario, "post_stride")) {
        set_pointer(target, 0x418, (uintptr_t)(post_records + 3) - 1);
    } else if (!strcmp(scenario, "post_unaligned")) {
        set_pointer(target, 0x410, (uintptr_t)post_records + 1);
        set_pointer(target, 0x418, (uintptr_t)(post_records + 3) + 1);
        set_pointer(target, 0x420, (uintptr_t)(post_records + 4) + 1);
    } else if (!strcmp(scenario, "post_null_partial")) {
        set_pointer(target, 0x410, 0);
    } else if (!strcmp(scenario, "post_unreadable")) {
        unreadable = (uintptr_t)post_records[2];
    } else if (!strcmp(scenario, "post_header_unreadable")) {
        unreadable = (uintptr_t)target + 0x418;
    } else if (!strcmp(scenario, "post_record_change")) {
        change_post_record = 1;
    } else if (!strcmp(scenario, "post_vector_change")) {
        change_post_vector = 1;
    } else if (!strcmp(scenario, "p2_attached")) {
        set_pointer(player2, 0x70, (uintptr_t)room);
    } else if (!strcmp(scenario, "pins")) {
        valid_pins = 0;
    } else if (!strcmp(scenario, "joined_boundary")) {
        joined_boundary = 0;
    } else if (!strcmp(scenario, "lost_hold")) {
        factory_probe_state.player2_owned = 0;
    } else if (!strcmp(scenario, "uncertain_hold")) {
        factory_probe_state.player2_uncertain = 1;
    } else if (!strcmp(scenario, "release_pending")) {
        factory_probe_state.release2_pending = 1;
    } else if (!strcmp(scenario, "wrong_phase")) {
        factory_probe_state.phase = 4;
    } else if (!strcmp(scenario, "missed_tick")) {
        stats.issued = 5;
    } else if (!strcmp(scenario, "target_missing")) {
        import_entries[3].saved = 0;
    } else if (!strcmp(scenario, "target_duplicate")) {
        import_entries[4] = import_entries[3];
        world_identity_count = 2;
    } else if (!strcmp(scenario, "target_duplicate_different_runtime")) {
        import_entries[4] = (world_identity_entry){9002, SEAT_PREFLIGHT_TARGET_SAVED};
        world_identity_count = 2;
    } else if (!strcmp(scenario, "target_count_invalid")) {
        world_identity_count = WORLD_IDENTITY_CAPACITY;
    } else if (!strcmp(scenario, "target_count_zero")) {
        world_identity_count = 0;
    } else if (!strcmp(scenario, "target_count_negative")) {
        world_identity_count = -1;
    } else if (!strcmp(scenario, "target_restore_started")) {
        world_identity_seal_admitted = 0;
    } else if (!strcmp(scenario, "target_generation_during_find")) {
        change_seal_during_find = 1;
    } else if (!strcmp(scenario, "target_population_mismatch")) {
        world_identity_count = 2;
    } else if (!strcmp(scenario, "target_zero_runtime")) {
        import_entries[3].runtime = 0;
    } else if (!strcmp(scenario, "target_wrong_vtable")) {
        set_pointer(target, 0, 0x111111);
    } else if (!strcmp(scenario, "target_wrong_runtime")) {
        set_pointer(target, 8, 1234);
    } else if (!strcmp(scenario, "target_change")) {
        change_target = 1;
    } else if (!strcmp(scenario, "canonical_change")) {
        change_canonical = 1;
    } else if (!strcmp(scenario, "p2_field_change")) {
        change_position = 1;
    } else if (!strcmp(scenario, "p2_unreadable")) {
        unreadable = (uintptr_t)player2 + 0x6aa8;
    } else if (!strcmp(scenario, "vector_invalid")) {
        set_pointer(player2, 0x310, (uintptr_t)vector2 - 8);
    } else if (!strcmp(scenario, "observe") || !strcmp(scenario, "request")) {
    } else {
        assert(!"unknown scenario");
    }
    int success = player_seat_preflight_after_join();
    seat_preflight_sample *sample = &player_seat_preflight_snapshot;
    int expected =
        !strcmp(scenario, "observe") || !strcmp(scenario, "request") ||
        !strcmp(scenario, "room_plain") || !strcmp(scenario, "room_gated") ||
        !strcmp(scenario, "room_unknown") || !strcmp(scenario, "room_cycle") ||
        !strcmp(scenario, "cockpit") || !strcmp(scenario, "cockpit_gated") ||
        !strcmp(scenario, "live_cockpit_navigation") ||
        !strcmp(scenario, "controller_navigation") || !strcmp(scenario, "controller_unknown") ||
        !strcmp(scenario, "controller_player") || !strcmp(scenario, "animation_handles") ||
        !strcmp(scenario, "context_disabled") || !strcmp(scenario, "large_post_index") ||
        !strcmp(scenario, "post_short") || !strcmp(scenario, "post_index_outside") ||
        !strcmp(scenario, "post_empty") || !strcmp(scenario, "p2_attached") ||
        !strcmp(scenario, "context_missing");
    assert(success == expected);
    assert(!forbidden_reads);
    if (expected) {
        assert(sample->complete && sample->stable);
        assert(sample->post_bounds_known);
        assert(sample->blockers & SEAT_PREFLIGHT_TARGET_HOLD_UNPROVEN);
        int post_outside =
            !strcmp(scenario, "large_post_index") || !strcmp(scenario, "post_short") ||
            !strcmp(scenario, "post_index_outside") || !strcmp(scenario, "post_empty");
        assert(sample->post_index_in_bounds == !post_outside);
        assert(sample->post_record_read == !post_outside);
        assert(!!(sample->blockers & SEAT_PREFLIGHT_POST_INDEX_OUTSIDE) == post_outside);
        if (post_outside) {
            assert(!post_record_reads && !sample->post_record_definition &&
                   !sample->post_record_actor);
        } else {
            assert(post_record_reads == 2);
            assert(sample->post_length == 3 && sample->post_capacity_entries == 4);
            assert(sample->post_record_definition == 0x60000000);
            assert(sample->post_record_actor == 0x60000008);
        }
        assert(resolve_calls == 2);
        if (!strcmp(scenario, "context_missing")) {
            assert(sample->input_contexts.tree_valid && sample->input_contexts.target_present == 3);
            assert(!sample->input_contexts.complete);
            assert(sample->blockers & SEAT_PREFLIGHT_CONTEXT_REGISTRATIONS_MISSING);
        } else {
            assert(sample->input_contexts.complete && sample->input_contexts.target_present == 7);
            assert(sample->input_contexts.node_count == 3);
            for (unsigned index = 0; index < 3; ++index) {
                assert(!memcmp(sample->input_contexts.payloads[index], context_nodes[index] + 0x1c,
                               16));
            }
        }
        if (!strcmp(scenario, "observe")) {
            assert(sample->pilot_post_index == 2 &&
                   sample->player1.controlled.pilot_entry == vector1[2]);
            assert(sample->room_class19_known && sample->room_class19);
            assert(sample->room_ancestry_reaches_target && sample->controller_absence_known);
        }
        if (!strcmp(scenario, "room_plain") || !strcmp(scenario, "room_gated")) {
            assert(sample->room_class19_known && !sample->room_class19);
            assert(sample->blockers & SEAT_PREFLIGHT_ROOM_CLASS_UNSUITABLE);
            assert(!(sample->blockers & SEAT_PREFLIGHT_ROOM_CLASS_UNKNOWN));
        }
        if (!strcmp(scenario, "room_unknown")) {
            assert(!sample->room_class19_known);
            assert(sample->blockers & SEAT_PREFLIGHT_ROOM_CLASS_UNKNOWN);
            assert(!(sample->blockers & SEAT_PREFLIGHT_ROOM_CLASS_UNSUITABLE));
        }
        if (!strcmp(scenario, "cockpit") || !strcmp(scenario, "live_cockpit_navigation")) {
            assert(sample->room_class19_known && sample->room_class19);
            assert(!(sample->blockers &
                     (SEAT_PREFLIGHT_ROOM_CLASS_UNSUITABLE | SEAT_PREFLIGHT_ROOM_CLASS_UNKNOWN)));
        }
        if (!strcmp(scenario, "cockpit_gated")) {
            assert(sample->room_class19_known && !sample->room_class19);
            assert(sample->blockers & SEAT_PREFLIGHT_ROOM_CLASS_UNSUITABLE);
        }
        if (!strcmp(scenario, "controller_navigation") ||
            !strcmp(scenario, "live_cockpit_navigation")) {
            assert(sample->controller_terminal_navigation && sample->controller_absence_known);
            assert(!sample->controller_head_is_player);
            assert(!(sample->blockers & (SEAT_PREFLIGHT_EXISTING_PLAYER_CONTROLLER |
                                         SEAT_PREFLIGHT_CONTROLLER_CHAIN_UNRESOLVED)));
        }
        if (!strcmp(scenario, "live_cockpit_navigation")) {
            assert(sample->blockers == SEAT_PREFLIGHT_TARGET_HOLD_UNPROVEN);
        }
        if (!strcmp(scenario, "room_cycle")) {
            assert(!sample->room_ancestry_reaches_target);
        }
        if (!strcmp(scenario, "controller_unknown")) {
            assert(!sample->controller_absence_known);
            assert(sample->blockers & SEAT_PREFLIGHT_CONTROLLER_CHAIN_UNRESOLVED);
        }
        if (!strcmp(scenario, "controller_player")) {
            assert(sample->controller_head_is_player);
        }
        if (!strcmp(scenario, "animation_handles")) {
            assert(sample->animation_handle_a_nonnull && sample->animation_handle_b_nonnull);
            assert(sample->blockers & SEAT_PREFLIGHT_ANIMATION_HANDLES_PRESENT);
        }
        if (!strcmp(scenario, "context_disabled")) {
            assert(sample->blockers & SEAT_PREFLIGHT_INPUT_CONTEXT_UNSAFE);
        }
        if (!strcmp(scenario, "large_post_index")) {
            assert(!sample->post_index_arithmetic_safe &&
                   !sample->player2.controlled.pilot_entry_read);
        }
        if (!strcmp(scenario, "p2_attached")) {
            assert(sample->blockers & SEAT_PREFLIGHT_P2_ALREADY_ATTACHED);
        }
        unsigned prior_reads = reads;
        assert(player_seat_preflight_after_join() && reads == prior_reads);
        lua_State state = {0};
        lua_player_seat_preflight(&state);
        assert(!state.nil && strstr(state.text, "safe_to_seat=0"));
        assert(strstr(state.text, "captured_tick=4"));
        assert(!strstr(state.text, " tick="));
        assert(strstr(state.text, "post_bounds_known=1"));
        assert(strstr(state.text, "post_actor_dereferenced=0"));
        if (!strcmp(scenario, "room_unknown")) {
            assert(strstr(state.text, "room_class19_status=unknown"));
        }
        if (!strcmp(scenario, "cockpit_gated") || !strcmp(scenario, "room_plain")) {
            assert(strstr(state.text, "room_class19_status=unsuitable"));
        }
        if (!strcmp(scenario, "live_cockpit_navigation")) {
            assert(strstr(state.text, "room_class19_status=suitable"));
            assert(strstr(state.text, "controller_terminal_navigation=1"));
        }
        assert(strstr(state.text, "context8_payload=0800000006000000000000000180a55a"));
        if (!strcmp(scenario, "context_missing")) {
            assert(strstr(state.text, "context16_payload=not_observed"));
        }
    } else {
        assert(!sample->complete && !sample->stable);
        if (!strcmp(scenario, "navigation_unexpected_next") ||
            !strcmp(scenario, "navigation_unexpected_predicate")) {
            assert(!strcmp(sample->reason, "pins") && !resolve_calls);
        }
        if (!strcmp(scenario, "post_record_change") || !strcmp(scenario, "post_vector_change")) {
            assert(!strcmp(sample->reason, "observation_changed"));
        }
        unsigned prior_reads = reads;
        assert(!player_seat_preflight_after_join() && reads == prior_reads);
    }
    return 0;
}
