#ifndef X4_GATE_PLAYER_FIRE_OBSERVATION_H
#define X4_GATE_PLAYER_FIRE_OBSERVATION_H

#include "player_weapon_groups.h"
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

/* Once-only observation in the authenticated P2 scope. Never invokes a fire
 * getter, constructs/replaces a controller, writes a trigger or posts events.
 * Equal snapshots are point observations, not a lifetime/exclusion lease. */
typedef struct {
    uintptr_t groups[2];
    uintptr_t ids[2];
    int32_t selected;
    unsigned count;
    uint64_t values[PLAYER_WEAPON_IDS_MAX];
} player_fire_observation_group;

typedef struct {
    player_weapon_item weapons[PLAYER_WEAPON_MAX];
    player_fire_observation_group group;
    unsigned count;
    uint64_t tick;
} player_fire_observation_state;

typedef struct {
    uintptr_t gun;
    uintptr_t controller;
    uintptr_t vtable;
    uint64_t runtime;
    unsigned char firing;
    unsigned char raw91;
    unsigned char raw91_present;
} player_fire_observation_sample;

static player_fire_observation_state player_fire_observation;
static unsigned player_fire_observation_done;
static unsigned player_fire_observation_resolver_failed;

static int player_fire_observation_read(void *context, uintptr_t address, void *output, size_t size) {
    (void)context;
    return gate_read_local((void *)address, output, size);
}

static uintptr_t player_fire_observation_resolve(void *context, uint64_t runtime) {
    (void)context;
    void *registry = NULL;
    uint64_t observed_runtime = 0;
    const void *registry_slot = world_component_map ? (void *)world_component_map : LX(LX_COMPONENT_MAP_VA);
    if (!runtime || !gate_read_local(registry_slot, &registry, sizeof registry) || !registry) {
        player_fire_observation_resolver_failed = 1;
        return 0;
    }
    lxw_find_fn expected = (lxw_find_fn)(void *)LX(LX_FIND_VA);
    if ((world_find && world_find != expected) ||
        !hash_ok(LX(LX_FIND_VA), 0x93, "6177d6aa24635956c7ed938eb80e67b6175ce95a1fb87bc1d0ef261eb9ef1684")) {
        player_fire_observation_resolver_failed = 1;
        return 0;
    }
    uintptr_t object = (uintptr_t)expected(registry, runtime, 4, 0);
    if (!object || object > UINTPTR_MAX - 16 ||
        !gate_read_local((void *)(object + 8), &observed_runtime, sizeof observed_runtime) ||
        observed_runtime != runtime) {
        player_fire_observation_resolver_failed = 1;
        return 0;
    }
    return object;
}

static int player_fire_observation_under_ship(void *context, uintptr_t gun, uintptr_t ship) {
    (void)context;
    uintptr_t vtable = 0;
    if (!gate_read_local((void *)gun, &vtable, sizeof vtable)) return -1;
    if (vtable != (uintptr_t)LX(0x3c6a440)) return 0;
    uintptr_t current = gun;
    for (unsigned depth = 0; depth < 16; ++depth) {
        if (current == ship) return 1;
        if (!current || current > UINTPTR_MAX - 0x78) return -1;
        uintptr_t parent = 0;
        if (!gate_read_local((void *)(current + 0x70), &parent, sizeof parent) || parent == current) return -1;
        current = parent;
    }
    return -1;
}

/* Retain the selector and every ID, including non-weapons: two selections can
 * resolve to the same gun list without being the same native group. */
static int player_fire_observation_group_read(player_fire_observation_group *group) {
    if (!group || !lifecycle_discard_ship || lifecycle_discard_ship > UINTPTR_MAX - 0x65c) return 0;
    memset(group, 0, sizeof *group);
    if (!gate_read_local((void *)(lifecycle_discard_ship + 0x628), group->groups, sizeof group->groups) ||
        !gate_read_local((void *)(lifecycle_discard_ship + 0x658), &group->selected, sizeof group->selected) ||
        group->groups[1] < group->groups[0] || (group->groups[1] - group->groups[0]) % 24 ||
        (group->groups[1] - group->groups[0]) / 24 > PLAYER_WEAPON_GROUP_MAX || group->selected < 0 ||
        (size_t)group->selected >= (group->groups[1] - group->groups[0]) / 24 ||
        group->groups[0] > UINTPTR_MAX - (size_t)group->selected * 24) return 0;
    uintptr_t selected = group->groups[0] + (size_t)group->selected * 24;
    if (selected > UINTPTR_MAX - sizeof group->ids ||
        !gate_read_local((void *)selected, group->ids, sizeof group->ids) || group->ids[1] < group->ids[0] ||
        (group->ids[1] - group->ids[0]) % 8 || (group->ids[1] - group->ids[0]) / 8 > PLAYER_WEAPON_IDS_MAX) return 0;
    group->count = (unsigned)((group->ids[1] - group->ids[0]) / 8);
    return !group->count || gate_read_local((void *)group->ids[0], group->values, (size_t)group->count * 8);
}

static int player_fire_observation_collect(player_weapon_item output[PLAYER_WEAPON_MAX], unsigned *count) {
    player_weapon_reader reader = {player_fire_observation_read, player_fire_observation_resolve,
                                  player_fire_observation_under_ship, NULL};
    player_fire_observation_resolver_failed = 0;
    return player_weapon_collect(&reader, lifecycle_discard_ship, 0, output, count) &&
           !player_fire_observation_resolver_failed;
}

static int player_fire_observation_valid(player_fire_observation_state *state, uintptr_t gun, uint64_t runtime) {
    if (!state || state->tick != stats.issued || state->tick != player_recurring_tail_token.generation ||
        !player_recurring_physics_entered() || !player_physics_replay_busy || !player_network_busy ||
        !gate_tls_worker(gate_tls_self()) || !player_physics_identity_for(1) || !state->count ||
        gun != state->weapons[0].object || runtime != state->weapons[0].runtime) return 0;
    player_fire_observation_group current_group;
    player_weapon_item current[PLAYER_WEAPON_MAX] = {{0}};
    unsigned count = 0;
    if (!player_fire_observation_group_read(&current_group) ||
        memcmp(&current_group, &state->group, sizeof current_group) ||
        !player_fire_observation_collect(current, &count) || count != state->count) return 0;
    for (unsigned index = 0; index < count; ++index) {
        if (current[index].object != state->weapons[index].object ||
            current[index].runtime != state->weapons[index].runtime) return 0;
    }
    return 1;
}

static int player_fire_observation_sample_read(player_fire_observation_state *state, uintptr_t gun,
                                               uint64_t runtime, player_fire_observation_sample *sample) {
    uintptr_t gun_vtable = 0;
    if (!state || !sample || gun > UINTPTR_MAX - 0x310 || !player_fire_observation_valid(state, gun, runtime)) return 0;
    memset(sample, 0, sizeof *sample);
    sample->gun = gun;
    if (!gate_read_local((void *)gun, &gun_vtable, sizeof gun_vtable) || gun_vtable != (uintptr_t)LX(0x3c6a440) ||
        !gate_read_local((void *)(gun + 8), &sample->runtime, sizeof sample->runtime) || sample->runtime != runtime ||
        !gate_read_local((void *)(gun + 0x2fd), &sample->firing, sizeof sample->firing) || sample->firing > 1 ||
        !gate_read_local((void *)(gun + 0x308), &sample->controller, sizeof sample->controller)) return 0;
    if (sample->controller) {
        if (!gate_read_local((void *)sample->controller, &sample->vtable, sizeof sample->vtable)) return 0;
        if (sample->vtable == (uintptr_t)LX(0x3b2fc38)) {
            if (sample->controller > UINTPTR_MAX - 0x92 ||
                !gate_read_local((void *)(sample->controller + 0x91), &sample->raw91, sizeof sample->raw91)) return 0;
            sample->raw91_present = 1; /* Preserve the raw byte, without trigger/bool semantics. */
        }
    }
    return player_fire_observation_valid(state, gun, runtime);
}

static void player_fire_observation_receipt(const char *outcome, const player_fire_observation_sample *sample) {
    int saved_errno = errno;
    char message[512];
    int length = snprintf(message, sizeof message,
        "X4LAN FIRE_DIAG v=2 stage=observe tick=%llu worker=%llu ship=%llx group=%d ids=%u weapons=%u "
        "gun=%llx id=%llx controller=%llx vtable=%llx firing=%u raw91=%u raw91_present=%u mutation=0 outcome=%s\n",
        (unsigned long long)stats.issued, (unsigned long long)GetCurrentThreadId(),
        (unsigned long long)lifecycle_discard_ship, player_fire_observation.group.selected,
        player_fire_observation.group.count, player_fire_observation.count,
        (unsigned long long)sample->gun, (unsigned long long)sample->runtime,
        (unsigned long long)sample->controller, (unsigned long long)sample->vtable,
        sample->firing, sample->raw91, sample->raw91_present, outcome);
    lifecycle_discard_require(length > 0 && (size_t)length < sizeof message, "fire_observation_receipt_format");
    size_t remaining = (size_t)length;
    const char *next = message;
    while (remaining) {
        ssize_t written = write(2, next, remaining);
        if (written < 0 && errno == EINTR) continue;
        lifecycle_discard_require(written > 0, "fire_observation_receipt_write");
        next += written;
        remaining -= (size_t)written;
    }
    errno = saved_errno;
}

static void player_fire_observation_scope(uint64_t generation) {
    const char *setting = getenv("X4_DEV_FIRE_DIAG");
    if (!setting) return;
    lifecycle_discard_require(!strcmp(setting, "1") && player_startup()->version == 1 &&
        player_recurring_end >= player_startup()->apply + 179 &&
        player_recurring_end <= player_startup()->apply + 899 && generation == stats.issued &&
        generation == player_recurring_tail_token.generation && gate_tls_worker(gate_tls_self()) &&
        player_recurring_physics_entered() &&
        player_physics_replay_busy && player_network_busy && player_physics_identity_for(1), "fire_observation_admission");
    if (player_fire_observation_done || generation != player_startup()->apply) return;

    player_fire_observation_state *state = &player_fire_observation;
    state->tick = generation;
    lifecycle_discard_require(player_fire_observation_group_read(&state->group) &&
        player_fire_observation_collect(state->weapons, &state->count), "fire_observation_group");
    if (!state->count) {
        player_fire_observation_group fresh_group;
        player_weapon_item current[PLAYER_WEAPON_MAX] = {{0}};
        unsigned count = 0;
        lifecycle_discard_require(player_fire_observation_group_read(&fresh_group) &&
            !memcmp(&fresh_group, &state->group, sizeof fresh_group) &&
            player_fire_observation_collect(current, &count) && !count && player_physics_identity_for(1),
            "fire_observation_empty_drift");
        player_fire_observation_receipt("no_gun", &(player_fire_observation_sample){0});
        player_fire_observation_done = 1;
        return;
    }

    uintptr_t gun = state->weapons[0].object;
    uint64_t runtime = state->weapons[0].runtime;
    player_fire_observation_sample before, after;
    lifecycle_discard_require(player_fire_observation_sample_read(state, gun, runtime, &before) &&
        player_fire_observation_sample_read(state, gun, runtime, &after) && !memcmp(&before, &after, sizeof before),
        "fire_observation_drift");
    const char *outcome = !after.controller ? "empty_observed" :
        after.raw91_present ? "stock_player_observed" : "incompatible_owner";
    player_fire_observation_receipt(outcome, &after);
    if (after.controller && !after.raw91_present) player_recurring_fatal("fire_observation_incompatible_owner");
    player_fire_observation_done = 1;
}
#endif
