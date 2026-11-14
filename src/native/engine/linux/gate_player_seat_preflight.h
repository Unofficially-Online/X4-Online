#ifndef X4_GATE_PLAYER_SEAT_PREFLIGHT_H
#define X4_GATE_PLAYER_SEAT_PREFLIGHT_H

/* Default-off, one-shot READ-ONLY seat evidence. Include after
 * gate_player_factory_probe.h, before gate_world_created.h is fine: resolution
 * uses the already declared lxw_find_fn, not a new engine ABI.
 * Request on accepted worker tick2; capture once at joined worker tick4 while
 * factory owns its retained P2 result. No writes, seating, virtual class calls,
 * native traversal calls or reference calls. Existing canonical getters and
 * existing registry find(map,runtime,4,0) are the only engine calls.
 *
 * The adapter's joined boundary must exclude target/room/controller tree and
 * registry writers throughout BOTH collections. Fresh resolution/double reads
 * detect changes; they do not acquire references or prove lifetime exclusion.
 * P2 has the factory's real owned hold. Target/room have no independent hold.
 * Cached observations are never later read destinations.
 *
 * Ship+410/+418/+420 are the pinned post-record vector begin/end/capacity.
 * Inspect one 16-byte record only after validating its live vector extent.
 * Its definition and actor fields remain raw pointers and are never followed.
 * An unknown controller-chain head does not prove that later polymorphic nodes
 * lack PlayerController; the exact pinned terminal NavigationController is the
 * only additional nonnull absence case supported here.
 * Registration payloads are exact16-byte observations; priority, enabled and
 * opaque tail policy remain unapproved. Complete means readable/stable diagnostic,
 * never safe-to-seat permission.
 */
_Static_assert(sizeof(uintptr_t) == 8, "seat preflight requires the pinned x86-64 layout");

#include "input_context_census.h"

#define SEAT_PREFLIGHT_TARGET_SAVED UINT64_C(0x175310)
#define SEAT_PREFLIGHT_CAPTURE_TICK UINT64_C(4)
#define SEAT_PREFLIGHT_SHIP_VTABLE 0x3d21748u
#define SEAT_PREFLIGHT_PLAYER_VTABLE 0x3b5f7a0u
#define SEAT_PREFLIGHT_CONTROLLER_VTABLE 0x3b7c208u
#define SEAT_PREFLIGHT_ROOM_VTABLE 0x3d4dad0u
#define SEAT_PREFLIGHT_CONTROL_ROOM_VTABLE 0x3cb4e88u
#define SEAT_PREFLIGHT_COCKPIT_VTABLE 0x3ca5fc0u
#define SEAT_PREFLIGHT_NAVIGATION_CONTROLLER_VTABLE 0x3b697e8u
#define SEAT_PREFLIGHT_PILOT_POST_GLOBAL 0x3e79f00u
#define SEAT_PREFLIGHT_PILOT_POSITION_GLOBAL 0x3e35e9cu
#define SEAT_PREFLIGHT_INVALID_POSITION_GLOBAL 0x3daa6f0u
#define SEAT_PREFLIGHT_ANIMATION_HANDLE_A 0x3ed1140u
#define SEAT_PREFLIGHT_ANIMATION_HANDLE_B 0x3ee2dc0u
#define SEAT_PREFLIGHT_CONTROLS_ENABLED 0x3d8da60u
#define SEAT_PREFLIGHT_DEACTIVATION_COUNT 0x3d8da64u
#define SEAT_PREFLIGHT_CONTEXTS_REQUESTED 0x3d8daa9u
#define SEAT_PREFLIGHT_RUNTIME_OFFSET 0x8u
#define SEAT_PREFLIGHT_PARENT_OFFSET 0x70u
#define SEAT_PREFLIGHT_CONTROLLER_HEAD_OFFSET 0x200u
#define SEAT_PREFLIGHT_CONTROLLED_BEGIN_OFFSET 0x308u
#define SEAT_PREFLIGHT_CONTROLLED_END_OFFSET 0x310u
#define SEAT_PREFLIGHT_CONTROLLED_CAPACITY_OFFSET 0x318u
#define SEAT_PREFLIGHT_PLAYER_POST_OFFSET 0x3c8u
#define SEAT_PREFLIGHT_SHIP_ROOM_OFFSET 0x408u
#define SEAT_PREFLIGHT_SHIP_POST_BASE_OFFSET 0x410u
#define SEAT_PREFLIGHT_SHIP_POST_END_OFFSET 0x418u
#define SEAT_PREFLIGHT_SHIP_POST_CAPACITY_OFFSET 0x420u
#define SEAT_PREFLIGHT_POST_RECORD_STRIDE 16u
#define SEAT_PREFLIGHT_PLAYER_ASSIST_OFFSET 0x6a9cu
#define SEAT_PREFLIGHT_PLAYER_POSITION_OFFSET 0x6aa8u
#define SEAT_PREFLIGHT_POST_PLAYER_FLAG_OFFSET 0x110u
#define SEAT_PREFLIGHT_POST_INDEX_OFFSET 0x1b0u
#define SEAT_PREFLIGHT_CLASS_GATE_OFFSET 0x68u
#define SEAT_PREFLIGHT_INPUT_MANAGER_OFFSET 0x10u
#define SEAT_PREFLIGHT_ANCESTRY_BUDGET 16u /* Diagnostic budget, not native extent. */

#define SEAT_PREFLIGHT_POST_INDEX_OUTSIDE 1u
#define SEAT_PREFLIGHT_CONTROLLER_CHAIN_UNRESOLVED 2u
#define SEAT_PREFLIGHT_EXISTING_PLAYER_CONTROLLER 4u
#define SEAT_PREFLIGHT_ROOM_CLASS_UNSUITABLE 8u
#define SEAT_PREFLIGHT_ROOM_ANCESTRY_UNRESOLVED 16u
#define SEAT_PREFLIGHT_PILOT_SYMBOL_INVALID 32u
#define SEAT_PREFLIGHT_POST_NOT_PLAYER 64u
#define SEAT_PREFLIGHT_P2_ALREADY_ATTACHED 128u
#define SEAT_PREFLIGHT_ANIMATION_HANDLES_PRESENT 256u
#define SEAT_PREFLIGHT_INPUT_CONTEXT_UNSAFE 512u
#define SEAT_PREFLIGHT_TARGET_HOLD_UNPROVEN 1024u
#define SEAT_PREFLIGHT_CONTEXT_REGISTRATIONS_MISSING 2048u
#define SEAT_PREFLIGHT_ROOM_CLASS_UNKNOWN 4096u

typedef struct {
    uintptr_t begin;
    uintptr_t end;
    uintptr_t capacity;
    uint64_t length;
    uint64_t capacity_entries;
    uintptr_t pilot_entry;
    unsigned pilot_entry_read;
} seat_preflight_vector;

typedef struct {
    uintptr_t address;
    uintptr_t vtable;
    uint64_t runtime;
    uintptr_t parent;
    uintptr_t post_definition;
    seat_preflight_vector controlled;
    unsigned char assist[4];
    uint32_t position;
} seat_preflight_player;

typedef struct {
    unsigned complete;
    unsigned stable;
    unsigned blockers;
    const char *reason;
    stock_keyboard_identity_sample canonical;
    input_context_census_sample input_contexts;
    seat_preflight_player player1;
    seat_preflight_player player2;
    uint32_t player2_reference_count;
    uint32_t player2_reference_marker;
    uintptr_t target;
    uint64_t target_runtime;
    uintptr_t target_vtable;
    uintptr_t room;
    uintptr_t room_vtable;
    uint32_t room_class_gate;
    unsigned room_class19;
    unsigned room_class19_known;
    unsigned room_ancestry_reaches_target;
    unsigned room_ancestry_steps;
    uintptr_t room_ancestry[SEAT_PREFLIGHT_ANCESTRY_BUDGET];
    uintptr_t target_controller_head;
    uintptr_t target_controller_vtable;
    unsigned controller_absence_known;
    unsigned controller_head_is_player;
    unsigned controller_terminal_navigation;
    uintptr_t pilot_post;
    uint64_t pilot_post_index;
    unsigned char pilot_post_player_flag;
    uintptr_t target_post_base;
    uintptr_t target_post_end;
    uintptr_t target_post_capacity;
    uint64_t post_length;
    uint64_t post_capacity_entries;
    uintptr_t post_record_definition;
    uintptr_t post_record_actor;
    unsigned post_bounds_known;
    unsigned post_index_in_bounds;
    unsigned post_record_read;
    unsigned post_index_arithmetic_safe;
    uint32_t pilot_position;
    uint32_t invalid_position;
    unsigned animation_handle_a_nonnull;
    unsigned animation_handle_b_nonnull;
    unsigned char controls_enabled;
    uint32_t deactivation_count;
    unsigned char contexts_requested;
} seat_preflight_sample;

typedef struct {
    void *context;
    uintptr_t expected_input_owner; /* Comparison only, never a read target. */
    int (*boundary)(void *context);
    int (*pins)(void *context);
    uintptr_t (*address)(void *context, unsigned va);
    int (*read)(void *context, uintptr_t address, void *output, size_t length);
    int (*canonical)(void *context, stock_keyboard_identity_sample *sample);
    int (*owned_player2)(void *context, uintptr_t *player, uint64_t *runtime, uint32_t *count,
                         uint32_t *marker);
    int (*resolve)(void *context, uint64_t saved, uintptr_t *object, uint64_t *runtime);
} seat_preflight_operations;

static int seat_preflight_read(const seat_preflight_operations *operations, uintptr_t base,
                               unsigned offset, void *output, size_t length) {
    return base && base <= UINTPTR_MAX - offset && length <= UINTPTR_MAX - (base + offset) &&
           operations->read(operations->context, base + offset, output, length);
}

static int seat_preflight_player_read(const seat_preflight_operations *operations, uintptr_t player,
                                      uint64_t pilot_index, seat_preflight_player *sample) {
    sample->address = player;
#define SEAT_PLAYER_READ(offset, field)                                                            \
    seat_preflight_read(operations, player, offset, &(field), sizeof(field))
    if (!SEAT_PLAYER_READ(0, sample->vtable) ||
        sample->vtable != operations->address(operations->context, SEAT_PREFLIGHT_PLAYER_VTABLE) ||
        !SEAT_PLAYER_READ(SEAT_PREFLIGHT_RUNTIME_OFFSET, sample->runtime) || !sample->runtime ||
        !SEAT_PLAYER_READ(SEAT_PREFLIGHT_PARENT_OFFSET, sample->parent) ||
        !SEAT_PLAYER_READ(SEAT_PREFLIGHT_CONTROLLED_BEGIN_OFFSET, sample->controlled.begin) ||
        !SEAT_PLAYER_READ(SEAT_PREFLIGHT_CONTROLLED_END_OFFSET, sample->controlled.end) ||
        !SEAT_PLAYER_READ(SEAT_PREFLIGHT_CONTROLLED_CAPACITY_OFFSET, sample->controlled.capacity) ||
        !SEAT_PLAYER_READ(SEAT_PREFLIGHT_PLAYER_POST_OFFSET, sample->post_definition) ||
        !SEAT_PLAYER_READ(SEAT_PREFLIGHT_PLAYER_ASSIST_OFFSET, sample->assist) ||
        !SEAT_PLAYER_READ(SEAT_PREFLIGHT_PLAYER_POSITION_OFFSET, sample->position)) {
        return 0;
    }
#undef SEAT_PLAYER_READ
    seat_preflight_vector *vector = &sample->controlled;
    if (vector->begin > vector->end || vector->end > vector->capacity ||
        ((vector->begin | vector->end | vector->capacity) & 7) ||
        (!vector->begin && (vector->end || vector->capacity))) {
        return 0;
    }
    vector->length = (vector->end - vector->begin) / 8;
    vector->capacity_entries = (vector->capacity - vector->begin) / 8;
    /* Actual observed vector end bounds this ONE read. No invented maximum or
     * assumption that pilot index0 exists. Allocation ownership stays native. */
    if (pilot_index < vector->length) {
        uintptr_t entry = vector->begin + (uintptr_t)pilot_index * 8;
        if (!operations->read(operations->context, entry, &vector->pilot_entry, 8)) {
            return 0;
        }
        vector->pilot_entry_read = 1;
    }
    return 1;
}

static int seat_preflight_once(seat_preflight_sample *sample,
                               const seat_preflight_operations *operations) {
    memset(sample, 0, sizeof(*sample));
    sample->reason = "boundary";
    if (!operations->boundary(operations->context)) {
        return 0;
    }
    sample->reason = "canonical_identity";
    if (!operations->canonical(operations->context, &sample->canonical) ||
        !sample->canonical.input_owner ||
        sample->canonical.input_owner != operations->expected_input_owner) {
        return 0;
    }
    sample->reason = "retained_player2";
    uintptr_t player2 = 0;
    uint64_t player2_runtime = 0;
    if (!operations->owned_player2(operations->context, &player2, &player2_runtime,
                                   &sample->player2_reference_count,
                                   &sample->player2_reference_marker) ||
        !player2 || !player2_runtime || player2 == sample->canonical.player ||
        !sample->player2_reference_count || sample->player2_reference_count >= INT32_MAX ||
        sample->player2_reference_marker != 2) {
        return 0;
    }
    sample->reason = "fresh_target_resolution";
    if (!operations->resolve(operations->context, SEAT_PREFLIGHT_TARGET_SAVED, &sample->target,
                             &sample->target_runtime) ||
        !sample->target || sample->target == sample->canonical.ship) {
        return 0;
    }
#define SEAT_GLOBAL(va, field)                                                                     \
    operations->read(operations->context, operations->address(operations->context, va), &(field),  \
                     sizeof(field))
    sample->reason = "pilot_definition";
    if (!SEAT_GLOBAL(SEAT_PREFLIGHT_PILOT_POST_GLOBAL, sample->pilot_post) || !sample->pilot_post ||
        !seat_preflight_read(operations, sample->pilot_post, SEAT_PREFLIGHT_POST_PLAYER_FLAG_OFFSET,
                             &sample->pilot_post_player_flag, 1) ||
        !seat_preflight_read(operations, sample->pilot_post, SEAT_PREFLIGHT_POST_INDEX_OFFSET,
                             &sample->pilot_post_index, 8) ||
        !SEAT_GLOBAL(SEAT_PREFLIGHT_PILOT_POSITION_GLOBAL, sample->pilot_position) ||
        !SEAT_GLOBAL(SEAT_PREFLIGHT_INVALID_POSITION_GLOBAL, sample->invalid_position)) {
        return 0;
    }
    sample->reason = "player_fields";
    if (!seat_preflight_player_read(operations, sample->canonical.player, sample->pilot_post_index,
                                    &sample->player1) ||
        sample->player1.runtime != sample->canonical.player_runtime ||
        !seat_preflight_player_read(operations, player2, sample->pilot_post_index,
                                    &sample->player2) ||
        sample->player2.runtime != player2_runtime) {
        return 0;
    }
    sample->reason = "target_fields";
    uint64_t observed_runtime = 0;
    if (!seat_preflight_read(operations, sample->target, 0, &sample->target_vtable, 8) ||
        sample->target_vtable !=
            operations->address(operations->context, SEAT_PREFLIGHT_SHIP_VTABLE) ||
        !seat_preflight_read(operations, sample->target, SEAT_PREFLIGHT_RUNTIME_OFFSET,
                             &observed_runtime, 8) ||
        observed_runtime != sample->target_runtime ||
        !seat_preflight_read(operations, sample->target, SEAT_PREFLIGHT_SHIP_ROOM_OFFSET,
                             &sample->room, 8) ||
        !seat_preflight_read(operations, sample->target, SEAT_PREFLIGHT_SHIP_POST_BASE_OFFSET,
                             &sample->target_post_base, 8) ||
        !seat_preflight_read(operations, sample->target, SEAT_PREFLIGHT_SHIP_POST_END_OFFSET,
                             &sample->target_post_end, 8) ||
        !seat_preflight_read(operations, sample->target, SEAT_PREFLIGHT_SHIP_POST_CAPACITY_OFFSET,
                             &sample->target_post_capacity, 8) ||
        !seat_preflight_read(operations, sample->target, SEAT_PREFLIGHT_CONTROLLER_HEAD_OFFSET,
                             &sample->target_controller_head, 8)) {
        return 0;
    }
    sample->blockers = SEAT_PREFLIGHT_TARGET_HOLD_UNPROVEN;
    sample->reason = "post_vector_shape";
    if ((!sample->target_post_base && (sample->target_post_end || sample->target_post_capacity)) ||
        sample->target_post_base > sample->target_post_end ||
        sample->target_post_end > sample->target_post_capacity ||
        (sample->target_post_base & (sizeof(uintptr_t) - 1)) ||
        (sample->target_post_end - sample->target_post_base) % SEAT_PREFLIGHT_POST_RECORD_STRIDE ||
        (sample->target_post_capacity - sample->target_post_base) %
            SEAT_PREFLIGHT_POST_RECORD_STRIDE) {
        return 0;
    }
    sample->post_length =
        (sample->target_post_end - sample->target_post_base) / SEAT_PREFLIGHT_POST_RECORD_STRIDE;
    sample->post_capacity_entries = (sample->target_post_capacity - sample->target_post_base) /
                                    SEAT_PREFLIGHT_POST_RECORD_STRIDE;
    sample->post_bounds_known = 1;
    sample->post_index_arithmetic_safe =
        sample->target_post_base &&
        sample->target_post_base <= UINTPTR_MAX - SEAT_PREFLIGHT_POST_RECORD_STRIDE &&
        sample->pilot_post_index <=
            (UINTPTR_MAX - sample->target_post_base - SEAT_PREFLIGHT_POST_RECORD_STRIDE) /
                SEAT_PREFLIGHT_POST_RECORD_STRIDE;
    sample->post_index_in_bounds =
        sample->post_index_arithmetic_safe && sample->pilot_post_index < sample->post_length;
    if (sample->post_index_in_bounds) {
        uintptr_t record[2] = {0};
        sample->reason = "post_record";
        if (!operations->read(operations->context,
                              sample->target_post_base +
                                  sample->pilot_post_index * SEAT_PREFLIGHT_POST_RECORD_STRIDE,
                              record, sizeof(record))) {
            return 0;
        }
        /* First word comes from macro post configuration, not necessarily the
         * global PostDef. Neither word conveys an owned lifetime reference. */
        sample->post_record_definition = record[0];
        sample->post_record_actor = record[1];
        sample->post_record_read = 1;
    } else {
        sample->blockers |= SEAT_PREFLIGHT_POST_INDEX_OUTSIDE;
    }
    sample->reason = "controller_head";
    if (!sample->target_controller_head) {
        sample->controller_absence_known = 1;
    } else {
        if (!seat_preflight_read(operations, sample->target_controller_head, 0,
                                 &sample->target_controller_vtable, 8)) {
            return 0;
        }
        if (sample->target_controller_vtable ==
            operations->address(operations->context, SEAT_PREFLIGHT_CONTROLLER_VTABLE)) {
            sample->controller_head_is_player = 1;
            sample->blockers |= SEAT_PREFLIGHT_EXISTING_PLAYER_CONTROLLER;
        } else if (sample->target_controller_vtable ==
                   operations->address(operations->context,
                                       SEAT_PREFLIGHT_NAVIGATION_CONTROLLER_VTABLE)) {
            /* Pinned vslot+50 predicate accepts classes0x1a/0x2e, not0x2f;
             * pinned vslot+d0 is xor EAX,EAX;ret. No next-node field is guessed
             * and no virtual code is called to establish this terminal case. */
            sample->controller_terminal_navigation = 1;
            sample->controller_absence_known = 1;
        } else {
            sample->blockers |= SEAT_PREFLIGHT_CONTROLLER_CHAIN_UNRESOLVED;
        }
    }
    sample->reason = "room_fields";
    if (sample->room) {
        if (!seat_preflight_read(operations, sample->room, 0, &sample->room_vtable, 8) ||
            !seat_preflight_read(operations, sample->room, SEAT_PREFLIGHT_CLASS_GATE_OFFSET,
                                 &sample->room_class_gate, 4)) {
            return 0;
        }
        /* Pinned class0x19 bits: Room=0; ControlRoom/Cockpit=1 gated on+68==0.
         * No arbitrary virtual class predicate or ancestor getter is called. */
        sample->room_class19_known =
            sample->room_vtable ==
                operations->address(operations->context, SEAT_PREFLIGHT_ROOM_VTABLE) ||
            sample->room_vtable ==
                operations->address(operations->context, SEAT_PREFLIGHT_CONTROL_ROOM_VTABLE) ||
            sample->room_vtable ==
                operations->address(operations->context, SEAT_PREFLIGHT_COCKPIT_VTABLE);
        sample->room_class19 =
            (sample->room_vtable ==
                 operations->address(operations->context, SEAT_PREFLIGHT_CONTROL_ROOM_VTABLE) ||
             sample->room_vtable ==
                 operations->address(operations->context, SEAT_PREFLIGHT_COCKPIT_VTABLE)) &&
            sample->room_class_gate == 0;
        uintptr_t ancestor = sample->room;
        for (unsigned depth = 0; depth < SEAT_PREFLIGHT_ANCESTRY_BUDGET; ++depth) {
            if (ancestor == sample->target) {
                sample->room_ancestry_reaches_target = 1;
                break;
            }
            if (!ancestor) {
                break;
            }
            unsigned repeated = 0;
            for (unsigned previous = 0; previous < depth; ++previous) {
                if (sample->room_ancestry[previous] == ancestor) {
                    repeated = 1;
                }
            }
            if (repeated) {
                break;
            }
            sample->room_ancestry[depth] = ancestor;
            ++sample->room_ancestry_steps;
            if (!seat_preflight_read(operations, ancestor, SEAT_PREFLIGHT_PARENT_OFFSET, &ancestor,
                                     8)) {
                return 0;
            }
        }
    }
    if (!sample->room_class19_known) {
        sample->blockers |= SEAT_PREFLIGHT_ROOM_CLASS_UNKNOWN;
    } else if (!sample->room_class19) {
        sample->blockers |= SEAT_PREFLIGHT_ROOM_CLASS_UNSUITABLE;
    }
    if (!sample->room_ancestry_reaches_target) {
        sample->blockers |= SEAT_PREFLIGHT_ROOM_ANCESTRY_UNRESOLVED;
    }
    if (!sample->pilot_position || sample->pilot_position == sample->invalid_position) {
        sample->blockers |= SEAT_PREFLIGHT_PILOT_SYMBOL_INVALID;
    }
    if (sample->pilot_post_player_flag != 1) {
        sample->blockers |= SEAT_PREFLIGHT_POST_NOT_PLAYER;
    }
    if (sample->player2.parent || sample->player2.controlled.pilot_entry) {
        sample->blockers |= SEAT_PREFLIGHT_P2_ALREADY_ATTACHED;
    }
    uintptr_t animation_handle_a = 0;
    uintptr_t animation_handle_b = 0;
    sample->reason = "shared_scalar_fields";
    if (!SEAT_GLOBAL(SEAT_PREFLIGHT_ANIMATION_HANDLE_A, animation_handle_a) ||
        !SEAT_GLOBAL(SEAT_PREFLIGHT_ANIMATION_HANDLE_B, animation_handle_b) ||
        !SEAT_GLOBAL(SEAT_PREFLIGHT_CONTROLS_ENABLED, sample->controls_enabled) ||
        !SEAT_GLOBAL(SEAT_PREFLIGHT_DEACTIVATION_COUNT, sample->deactivation_count) ||
        !SEAT_GLOBAL(SEAT_PREFLIGHT_CONTEXTS_REQUESTED, sample->contexts_requested)) {
        return 0;
    }
#undef SEAT_GLOBAL
    sample->animation_handle_a_nonnull = animation_handle_a != 0;
    sample->animation_handle_b_nonnull = animation_handle_b != 0;
    if (animation_handle_a || animation_handle_b) {
        sample->blockers |= SEAT_PREFLIGHT_ANIMATION_HANDLES_PRESENT;
    }
    if (!sample->canonical.ready || sample->controls_enabled != 1 ||
        sample->deactivation_count != 0 || sample->contexts_requested != 1) {
        sample->blockers |= SEAT_PREFLIGHT_INPUT_CONTEXT_UNSAFE;
    }
    sample->reason = "input_context_census";
    if (sample->canonical.input_owner > UINTPTR_MAX - SEAT_PREFLIGHT_INPUT_MANAGER_OFFSET) {
        return 0;
    }
    const input_context_census_operations context_operations = {
        operations->context,
        operations->read,
    };
    int contexts_complete = input_context_census_collect(
        &context_operations, sample->canonical.input_owner + SEAT_PREFLIGHT_INPUT_MANAGER_OFFSET,
        &sample->input_contexts);
    if (!contexts_complete) {
        if (!sample->input_contexts.tree_valid) {
            return 0;
        }
        sample->blockers |= SEAT_PREFLIGHT_CONTEXT_REGISTRATIONS_MISSING;
    }
    sample->reason = "observed_owned_target_hold_unproven";
    return 1;
}

static int seat_preflight_collect(seat_preflight_sample *sample,
                                  const seat_preflight_operations *operations) {
    if (!sample) {
        return 0;
    }
    memset(sample, 0, sizeof(*sample));
    sample->reason = "operations";
    if (!operations || !operations->expected_input_owner || !operations->boundary ||
        !operations->pins || !operations->address || !operations->read || !operations->canonical ||
        !operations->owned_player2 || !operations->resolve) {
        return 0;
    }
    sample->reason = "boundary";
    if (!operations->boundary(operations->context)) {
        return 0;
    }
    sample->reason = "pins";
    if (!operations->pins(operations->context)) {
        return 0;
    }
    if (!seat_preflight_once(sample, operations)) {
        return 0;
    }
    seat_preflight_sample after;
    if (!seat_preflight_once(&after, operations)) {
        sample->reason = after.reason;
        return 0;
    }
    seat_preflight_sample before;
    memcpy(&before, sample, sizeof(before));
    before.reason = after.reason = NULL;
    before.canonical.reason = after.canonical.reason = NULL;
    before.input_contexts.reason = after.input_contexts.reason = NULL;
    sample->reason = "observation_changed";
    if (memcmp(&before, &after, sizeof(before))) {
        return 0;
    }
    sample->reason = "boundary_changed";
    if (!operations->boundary(operations->context)) {
        return 0;
    }
    sample->complete = sample->stable = 1;
    sample->reason = "observed_owned_target_hold_unproven";
    return 1;
}

static int player_seat_preflight_pins(void *context) {
    (void)context;
    static const lx_site pins[] = {
        {"animation-handle-a-load", 0x164c307, 7,
         "b3e54520e2424d8323db4602d3843a509ef119171e926a0d30241219ce23f228"},
        {"animation-handle-b-load", 0x164c340, 7,
         "e5d6f7afc49ded0d9134f9ba83ea30712cbb6245291ae1f5de90925b55ec04d0"},
        {"context-request-refresh", 0x165d860, 235,
         "239c4ec0e606c95d16d15966608a361b2fc25f4680cee6242936314735602233"},
        {"context-request-remove", 0x104f220, 273,
         "08e38ecf256ed1dfa8732e311a26eee8d97ea7a28a1d306053355c1601a61dc9"},
        {"context-request-insert", 0x104d350, 1168,
         "43f1b8869d7f2cfb32e248026512624fd1b7f363e4d27530667f6232d75ded32"},
        {"context-request-unlink", 0x104bf60, 1076,
         "1e403b0b35a50d53847d444fb278a5171cf10cceb6377c108144a7d2ab7adf0c"},
        {"component-find", 0xa68df0, 147,
         "6177d6aa24635956c7ed938eb80e67b6175ce95a1fb87bc1d0ef261eb9ef1684"},
        {"pilot-post-binding", 0x14f2de7, 60,
         "075843a4c13013674a13e654ec4bd1030a14112e5b752b66c62258af87121753"},
        {"pilot-position-wrapper", 0x13969e0, 118,
         "c6628b874e67daf2f185718cd60708d5b1b881166a37a7021467225e86827ead"},
        {"post-record-indexing", 0x186ba70, 125,
         "df179e3f0e1727ec2f513f63727a3b438d4c86a2b7b061b4edabed0ec3d489ae"},
        {"post-vector-constructor", 0x17c8d00, 623,
         "3553c51842895280156aeb990f7ccbd64c6957a01a27bd190813881141416ced"},
        {"post-vector-resize", 0x1874a70, 824,
         "2a4f2edcf7ad290cc7598cf43c6e974657267d09b1bd7316959e20f59a07e5ee"},
        {"post-vector-destructor", 0x118ad40, 615,
         "c8c07e5c1d5b4f3bd21fbfc078d6b77b9e7c93565bbb49ba433bc1b4193e9d87"},
        {"controlled-vector-indexing", 0x1ec2380, 89,
         "f7950cb339c5fdeb93d0d652db1a316bce1d2c73063c880b71dbe3b0a3bf4e66"},
        {"assist-field-read", 0xa456d0, 27,
         "63f474fbc3baa605a29bc4aece92f1c0be33ffe240165b1c7114bbef4f15ecb9"},
        {"controller-chain-lookup", 0x1dd1980, 67,
         "9d4bce92abf2fd3814ea62e9cd809e066a9f1a4be6aa694f7bbc52f48aa8bae9"},
        {"Room-class-predicate", 0xa37db0, 48,
         "1d4969302746d557792a8db4c473519dba88ce670674c55e9ca88c4d32a55510"},
        {"ControlRoom-class-predicate", 0xa398e0, 48,
         "2074c11d6aa602fe94e95bf83d1f851227d00088bcefb4850d328bc7041dc429"},
        {"Room-class19-table-word", 0x2c82584, 4,
         "5e4af92d1b61923dfb73d2bb1f11c68b0be4b47ac2e0ed8390962c74df7aa6c1"},
        {"ControlRoom-class19-table-word", 0x2c81d04, 4,
         "d1913da76f7a74ccd5f945d67a2e55caaf1af7f77dea6ed2e93f15f6a848a5b1"},
        {"Cockpit-class-predicate", 0xa39930, 48,
         "8bf9e521822333a1f5b82954f8081829f44d80b19b641fd7e96a19ed85b416f9"},
        {"Cockpit-class19-table-word", 0x2c81ce4, 4,
         "52bdb0935fa7bf70e447a27904a02c550e7ff5115cf66ef3e5d93e668304923c"},
        {"NavigationController-class-predicate", 0xd58680, 15,
         "59e75159a711489b5886c979ff434844270d033af2429b796ebcaf93cc14fc53"},
        {"NavigationController-terminal-next", 0xa863a0, 3,
         "251447ee91a9067dcd6ab96703133f617565974cd6c4819021760c4688c91abf"},
    };
    for (unsigned index = 0; index < sizeof(pins) / sizeof(pins[0]); ++index) {
        if (!site_ok(&pins[index])) {
            return 0;
        }
    }
    static const unsigned typed_method_slots[][2] = {
        {SEAT_PREFLIGHT_ROOM_VTABLE + 0x11f0, 0xa37db0},
        {SEAT_PREFLIGHT_CONTROL_ROOM_VTABLE + 0x11f0, 0xa398e0},
        {SEAT_PREFLIGHT_COCKPIT_VTABLE + 0x11f0, 0xa39930},
        {SEAT_PREFLIGHT_NAVIGATION_CONTROLLER_VTABLE + 0x50, 0xd58680},
        {SEAT_PREFLIGHT_NAVIGATION_CONTROLLER_VTABLE + 0xd0, 0xa863a0},
    };
    for (unsigned index = 0; index < sizeof(typed_method_slots) / sizeof(typed_method_slots[0]);
         ++index) {
        uintptr_t target = 0;
        if (!read_local(LX(typed_method_slots[index][0]), &target, 8) ||
            target != (uintptr_t)LX(typed_method_slots[index][1])) {
            return 0;
        }
    }
    return 1;
}

static uintptr_t player_seat_preflight_input_owner;
static unsigned player_seat_preflight_requested;
static unsigned player_seat_preflight_attempted;
static uint64_t player_seat_preflight_tick;
static seat_preflight_sample player_seat_preflight_snapshot;

static int player_seat_preflight_retained(void) {
    return factory_probe_state.phase == PLAYER_FACTORY_RETAINED && factory_probe_state.player2 &&
           factory_probe_state.player2_owned && !factory_probe_state.player2_uncertain &&
           !factory_probe_state.problem && !factory_probe_state.player1_owned &&
           !factory_probe_state.player1_uncertain && !factory_probe_state.forward_pending &&
           !factory_probe_state.interceptor_active && !factory_probe_state.constructor_pending &&
           !factory_probe_state.assignment_pending && !factory_probe_state.singleton_uncertain &&
           !factory_probe_state.release1_pending && !factory_probe_state.release2_pending &&
           !factory_probe_inflight;
}

static int player_seat_preflight_boundary(void *context) {
    (void)context;
    uintptr_t fresh_owner = 0;
    return player_seat_preflight_requested && stats.issued == player_startup()->seat_preflight &&
           factory_probe_joined_boundary() && read_local(LX(SKO_OWNER_VA), &fresh_owner, 8) &&
           fresh_owner && fresh_owner == player_seat_preflight_input_owner &&
           fresh_owner == sko_session.owner && player_seat_preflight_retained() &&
           !factory_probe_patch_owned && !factory_probe_code_permission_pending &&
           !factory_probe_thunk_permission_pending && !atomic_load(&factory_probe_scope) &&
           !atomic_load(&factory_probe_wrapper_depth) &&
           factory_probe_release_tick > player_startup()->seat_preflight;
}

static uintptr_t player_seat_preflight_address(void *context, unsigned va) {
    (void)context;
    return (uintptr_t)LX(va);
}

static int player_seat_preflight_read(void *context, uintptr_t address, void *output,
                                      size_t length) {
    (void)context;
    return address && read_local((const void *)address, output, length);
}

static int player_seat_preflight_canonical(void *context, stock_keyboard_identity_sample *sample) {
    (void)context;
    return stock_keyboard_identity_collect(sample);
}

static int player_seat_preflight_owned_player2(void *context, uintptr_t *player, uint64_t *runtime,
                                               uint32_t *count, uint32_t *marker) {
    (void)context;
    if (!player_seat_preflight_boundary(NULL) || !factory_probe_state.player2 ||
        !factory_probe_object(factory_probe_state.player2, factory_probe_macro, runtime, count,
                              marker) ||
        *runtime != factory_probe_player2_runtime) {
        return 0;
    }
    *player = factory_probe_state.player2;
    return 1;
}

/* One fixed saved target, one immutable seal. This caches identity metadata,
 * never a native object. A stale admission is refused rather than rebuilt. */
static struct {
    unsigned initialized;
    world_identity_entry *table;
    int64_t count;
    uint64_t generation, runtime;
    unsigned bucket;
} player_seat_target_cache;

static int player_seat_preflight_target_runtime(uint64_t *runtime) {
    int accepted = 0;
    *runtime = 0;
    pthread_mutex_lock(&lxw_insert_lock);
    if (!world_identity_seal_admitted || !world_identity_installed ||
        !world_identity_sealed || !world_identities || !world_identity_seal_generation ||
        world_identity_seal_generation_exhausted ||
        __atomic_load_n(&world_identity_failures, __ATOMIC_SEQ_CST) || world_identity_count <= 0 ||
        world_identity_count > WORLD_IDENTITY_CAPACITY / 2) {
        goto done;
    }
    if (!player_seat_target_cache.initialized) {
        uint64_t unique_runtime = 0;
        unsigned unique_bucket = 0, matches = 0;
        int64_t population = 0;
        for (unsigned bucket = 0; bucket < WORLD_IDENTITY_CAPACITY; ++bucket) {
            const world_identity_entry *entry = &world_identities[bucket];
            if (entry->runtime) ++population;
            if (entry->saved == SEAT_PREFLIGHT_TARGET_SAVED) {
                ++matches;
                unique_runtime = entry->runtime;
                unique_bucket = bucket;
            }
        }
        player_seat_target_cache.initialized = 1;
        player_seat_target_cache.table = world_identities;
        player_seat_target_cache.count = world_identity_count;
        player_seat_target_cache.generation = world_identity_seal_generation;
        if (matches == 1 && unique_runtime && population == world_identity_count) {
            player_seat_target_cache.runtime = unique_runtime;
            player_seat_target_cache.bucket = unique_bucket;
        }
    }
    if (player_seat_target_cache.table != world_identities ||
        player_seat_target_cache.count != world_identity_count ||
        player_seat_target_cache.generation != world_identity_seal_generation ||
        !player_seat_target_cache.runtime ||
        player_seat_target_cache.bucket >= WORLD_IDENTITY_CAPACITY) {
        goto done;
    }
    const world_identity_entry *entry = &world_identities[player_seat_target_cache.bucket];
    if (entry->runtime != player_seat_target_cache.runtime ||
        entry->saved != SEAT_PREFLIGHT_TARGET_SAVED ||
        world_identity_lookup(entry->runtime) != SEAT_PREFLIGHT_TARGET_SAVED) {
        goto done;
    }
    *runtime = entry->runtime;
    accepted = 1;
done:
    pthread_mutex_unlock(&lxw_insert_lock);
    return accepted;
}

/* Every collection still resolves a fresh native object through the existing
 * pinned registry ABI. Immutable imported IDs do not hold engine objects alive. */
static int player_seat_preflight_resolve(void *context, uint64_t saved, uintptr_t *object,
                                         uint64_t *runtime) {
    (void)context;
    *object = 0;
    *runtime = 0;
    if (saved != SEAT_PREFLIGHT_TARGET_SAVED || !player_seat_preflight_target_runtime(runtime)) {
        return 0;
    }
    void *map = NULL;
    if (!*runtime || world_identity_lookup(*runtime) != saved ||
        !read_local(world_component_map ? world_component_map : (void **)LX(LX_COMPONENT_MAP_VA),
                    &map, sizeof(map)) ||
        !map) {
        return 0;
    }
    lxw_find_fn find = world_find ? world_find : (lxw_find_fn)(void *)LX(LX_FIND_VA);
    *object = (uintptr_t)find(map, *runtime, 4, 0);
    uint64_t confirmed_runtime = 0;
    if (!*object || !player_seat_preflight_target_runtime(&confirmed_runtime) ||
        confirmed_runtime != *runtime) {
        *object = 0;
        *runtime = 0;
        return 0;
    }
    return 1;
}

static int player_seat_preflight_after_join(void) {
    if (!player_seat_preflight_requested) {
        return 1;
    }
    if (player_seat_preflight_attempted) {
        return player_seat_preflight_snapshot.complete;
    }
    if (stats.issued < player_startup()->seat_preflight) {
        return 1;
    }
    player_seat_preflight_attempted = 1;
    player_seat_preflight_tick = stats.issued;
    const seat_preflight_operations operations = {
        NULL,
        player_seat_preflight_input_owner,
        player_seat_preflight_boundary,
        player_seat_preflight_pins,
        player_seat_preflight_address,
        player_seat_preflight_read,
        player_seat_preflight_canonical,
        player_seat_preflight_owned_player2,
        player_seat_preflight_resolve,
    };
    return seat_preflight_collect(&player_seat_preflight_snapshot, &operations);
}

static int lua_player_seat_preflight_request(lua_State *state) {
    uintptr_t fresh_owner = 0;
    int accepted = accept_ui() && stats.active && stats.issued == player_startup()->seat_request &&
                   !player_seat_preflight_requested && !player_seat_preflight_attempted &&
                   stock_observer_worker_boundary() && player_seat_preflight_retained() &&
                   factory_probe_release_tick > player_startup()->seat_preflight &&
                   read_local(LX(SKO_OWNER_VA), &fresh_owner, 8) && fresh_owner &&
                   fresh_owner == sko_session.owner;
    if (accepted) {
        player_seat_preflight_input_owner = fresh_owner;
        player_seat_preflight_requested = 1;
    }
    p_lua_pushnumber(state, accepted);
    return 1;
}

static int lua_player_seat_preflight(lua_State *state) {
    if (!stock_observer_worker_boundary()) {
        p_lua_pushnil(state);
        return 1;
    }
    const seat_preflight_sample *sample = &player_seat_preflight_snapshot;
    char context_payloads[INPUT_CONTEXT_CENSUS_TARGETS][INPUT_CONTEXT_CENSUS_PAYLOAD_BYTES * 2 + 1];
    static const char digits[] = "0123456789abcdef";
    for (unsigned context_index = 0; context_index < INPUT_CONTEXT_CENSUS_TARGETS;
         ++context_index) {
        if (!(sample->input_contexts.target_present & (1u << context_index))) {
            memcpy(context_payloads[context_index], "not_observed", sizeof("not_observed"));
            continue;
        }
        for (unsigned byte_index = 0; byte_index < INPUT_CONTEXT_CENSUS_PAYLOAD_BYTES;
             ++byte_index) {
            unsigned char byte = sample->input_contexts.payloads[context_index][byte_index];
            context_payloads[context_index][byte_index * 2] = digits[byte >> 4];
            context_payloads[context_index][byte_index * 2 + 1] = digits[byte & 15];
        }
        context_payloads[context_index][INPUT_CONTEXT_CENSUS_PAYLOAD_BYTES * 2] = 0;
    }
    char output[3200];
    int length = snprintf(
        output, sizeof(output),
        "requested=%u attempted=%u captured_tick=%llu complete=%u stable=%u safe_to_seat=0 "
        "reason=%s "
        "blockers=%u target_saved=%016llx target_runtime=%llu target=%llx target_vtable=%llx "
        "canonical_player=%llx canonical_ship=%llx player2=%llx player2_runtime=%llu "
        "player2_reference_count=%u player2_reference_marker=%u player1_parent=%llx "
        "player2_parent=%llx "
        "player1_post_definition=%llx player2_post_definition=%llx "
        "player1_vector=%llx,%llx,%llx player1_vector_length=%llu player1_pilot_entry_read=%u "
        "player1_pilot_entry=%llx player2_vector=%llx,%llx,%llx player2_vector_length=%llu "
        "player2_pilot_entry_read=%u player2_pilot_entry=%llx player1_position=%u "
        "player2_position=%u "
        "pilot_position=%u invalid_position=%u player1_assist=%u,%u,%u,%u "
        "player2_assist=%u,%u,%u,%u "
        "pilot_post=%llx pilot_post_player_flag=%u pilot_post_index=%llu target_post_base=%llx "
        "target_post_end=%llx target_post_capacity=%llx post_length=%llu "
        "post_capacity_entries=%llu "
        "post_index_arithmetic_safe=%u post_bounds_known=%u post_index_in_bounds=%u "
        "post_record_read=%u post_record_definition=%llx post_record_actor=%llx "
        "post_actor_dereferenced=0 "
        "room=%llx room_vtable=%llx room_class19_known=%u room_class19=%u room_class_gate=%u "
        "room_class19_status=%s "
        "room_ancestry_reaches_target=%u room_ancestry_steps=%u controller_head=%llx "
        "controller_vtable=%llx controller_absence_known=%u controller_head_is_player=%u "
        "controller_terminal_navigation=%u "
        "animation_handle_a_nonnull=%u animation_handle_b_nonnull=%u controls_enabled=%u "
        "deactivation_count=%u contexts_requested=%u target_owned_hold=0 "
        "input_owner=%llx context_manager=%llx context_tree_valid=%u context_tree_complete=%u "
        "context_node_count=%u context_target_mask=%u context_reason=%s "
        "context8_payload=%s context15_payload=%s context16_payload=%s",
        player_seat_preflight_requested, player_seat_preflight_attempted,
        (unsigned long long)player_seat_preflight_tick, sample->complete, sample->stable,
        sample->reason ? sample->reason : "off", sample->blockers,
        (unsigned long long)SEAT_PREFLIGHT_TARGET_SAVED, (unsigned long long)sample->target_runtime,
        (unsigned long long)sample->target, (unsigned long long)sample->target_vtable,
        (unsigned long long)sample->canonical.player, (unsigned long long)sample->canonical.ship,
        (unsigned long long)sample->player2.address, (unsigned long long)sample->player2.runtime,
        sample->player2_reference_count, sample->player2_reference_marker,
        (unsigned long long)sample->player1.parent, (unsigned long long)sample->player2.parent,
        (unsigned long long)sample->player1.post_definition,
        (unsigned long long)sample->player2.post_definition,
        (unsigned long long)sample->player1.controlled.begin,
        (unsigned long long)sample->player1.controlled.end,
        (unsigned long long)sample->player1.controlled.capacity,
        (unsigned long long)sample->player1.controlled.length,
        sample->player1.controlled.pilot_entry_read,
        (unsigned long long)sample->player1.controlled.pilot_entry,
        (unsigned long long)sample->player2.controlled.begin,
        (unsigned long long)sample->player2.controlled.end,
        (unsigned long long)sample->player2.controlled.capacity,
        (unsigned long long)sample->player2.controlled.length,
        sample->player2.controlled.pilot_entry_read,
        (unsigned long long)sample->player2.controlled.pilot_entry, sample->player1.position,
        sample->player2.position, sample->pilot_position, sample->invalid_position,
        sample->player1.assist[0], sample->player1.assist[1], sample->player1.assist[2],
        sample->player1.assist[3], sample->player2.assist[0], sample->player2.assist[1],
        sample->player2.assist[2], sample->player2.assist[3],
        (unsigned long long)sample->pilot_post, sample->pilot_post_player_flag,
        (unsigned long long)sample->pilot_post_index, (unsigned long long)sample->target_post_base,
        (unsigned long long)sample->target_post_end,
        (unsigned long long)sample->target_post_capacity, (unsigned long long)sample->post_length,
        (unsigned long long)sample->post_capacity_entries, sample->post_index_arithmetic_safe,
        sample->post_bounds_known, sample->post_index_in_bounds, sample->post_record_read,
        (unsigned long long)sample->post_record_definition,
        (unsigned long long)sample->post_record_actor, (unsigned long long)sample->room,
        (unsigned long long)sample->room_vtable, sample->room_class19_known, sample->room_class19,
        sample->room_class_gate,
        !sample->room_class19_known ? "unknown"
        : sample->room_class19      ? "suitable"
                                    : "unsuitable",
        sample->room_ancestry_reaches_target, sample->room_ancestry_steps,
        (unsigned long long)sample->target_controller_head,
        (unsigned long long)sample->target_controller_vtable, sample->controller_absence_known,
        sample->controller_head_is_player, sample->controller_terminal_navigation,
        sample->animation_handle_a_nonnull, sample->animation_handle_b_nonnull,
        sample->controls_enabled, sample->deactivation_count, sample->contexts_requested,
        (unsigned long long)sample->canonical.input_owner,
        (unsigned long long)sample->input_contexts.manager, sample->input_contexts.tree_valid,
        sample->input_contexts.complete, sample->input_contexts.node_count,
        sample->input_contexts.target_present,
        sample->input_contexts.reason ? sample->input_contexts.reason : "not_sampled",
        context_payloads[0], context_payloads[1], context_payloads[2]);
    if (length < 0 || (size_t)length >= sizeof(output)) {
        p_lua_pushnil(state);
    } else {
        p_lua_pushlstring(state, output, (size_t)length);
    }
    return 1;
}
#endif
