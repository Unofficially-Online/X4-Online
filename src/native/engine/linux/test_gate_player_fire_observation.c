#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static unsigned char gun[0x320], ship[0x660];
static _Alignas(16) unsigned char controller[0xa0], alternate_controller[0xa0];
static void *registry = (void *)1;
static unsigned reads, forbidden_calls, drift_variant, drift_applied;

static void forbidden_mutation(void) {
    ++forbidden_calls;
    assert(!"FIRE observation called a mutation provider");
}
static void *find_component(void *, uint64_t, unsigned, unsigned);
static void *fixture_address(uintptr_t address) {
    switch (address) {
    case 0xa68df0: return find_component;
    case 0x3daa860: return &registry;
    case 0x1a448b0: case 0x11339b0: case 0x110f900: case 0x189fa50: case 0x16c5fe0: case 0x124f720:
        return forbidden_mutation;
    default: return (void *)address;
    }
}
#define LX(address) fixture_address(address)

static int hash_ok(const void *address, size_t size, const char *hash) {
    (void)address;
    (void)size;
    (void)hash;
    return 1;
}
static int gate_read_local(const void *source, void *output, size_t size) {
    if (!source) return 0;
    ++reads;
    memcpy(output, source, size);
    if (!drift_applied && source == controller + 0x91) {
        drift_applied = 1;
        if (drift_variant == 1) gun[0x2fd] ^= 1;
        if (drift_variant == 2) {
            uintptr_t other = (uintptr_t)alternate_controller;
            memcpy(gun + 0x308, &other, sizeof other);
        }
        if (drift_variant == 3) {
            uintptr_t foreign_vtable = 0x123456;
            memcpy(controller, &foreign_vtable, sizeof foreign_vtable);
        }
        if (drift_variant == 4) controller[0x91] ^= 1;
        if (drift_variant == 5) {
            uint64_t other_runtime = 124;
            memcpy(gun + 8, &other_runtime, sizeof other_runtime);
        }
        if (drift_variant == 6) {
            int32_t other_group = 1;
            memcpy(ship + 0x658, &other_group, sizeof other_group);
        }
    }
    return 1;
}

typedef void *(*lxw_find_fn)(void *, uint64_t, unsigned, unsigned);
static void *find_component(void *map, uint64_t runtime, unsigned mode, unsigned extra) {
    return map == registry && runtime == 123 && mode == 4 && !extra ? gun : NULL;
}
static lxw_find_fn world_find = find_component;
static void **world_component_map;
#define LX_COMPONENT_MAP_VA 0x3daa860
#define LX_FIND_VA 0xa68df0
static uintptr_t lifecycle_discard_ship;
static struct { uint64_t issued; } stats;
static struct { uint64_t generation; } player_recurring_tail_token;
static int player_physics_replay_busy = 1, player_network_busy = 1;
static uint64_t player_recurring_end = 219;
static struct { unsigned version; uint64_t apply; } startup = {1, 40};
static void *gate_tls_self(void) { return &startup; }
static int gate_tls_worker(void *value) { return value == &startup; }
static int player_recurring_physics_entered(void) { return 1; }
static int player_physics_identity_for(int scoped) { return scoped == 1; }
static __typeof__(startup) *player_startup(void) { return &startup; }
static unsigned long GetCurrentThreadId(void) { return 42; }
static void lifecycle_discard_require(int condition, const char *reason) {
    if (!condition) {
        fprintf(stderr, "FIRE_OBSERVATION_TEST refusal=%s\n", reason);
        _exit(223);
    }
}
static _Noreturn void player_recurring_fatal(const char *reason) {
    (void)reason;
    _exit(223);
}
#include "gate_player_fire_observation.h"

static void setup(void) {
    memset(gun, 0, sizeof gun);
    memset(ship, 0, sizeof ship);
    memset(controller, 0, sizeof controller);
    memset(alternate_controller, 0, sizeof alternate_controller);
    memset(&player_fire_observation, 0, sizeof player_fire_observation);
    player_fire_observation_done = 0;
    reads = forbidden_calls = drift_variant = drift_applied = 0;
    uintptr_t gun_vtable = 0x3c6a440, parent = (uintptr_t)ship;
    uintptr_t stock_vtable = 0x3b2fc38, current_controller = (uintptr_t)controller;
    uint64_t runtime = 123;
    memcpy(gun, &gun_vtable, sizeof gun_vtable);
    memcpy(gun + 8, &runtime, sizeof runtime);
    memcpy(gun + 0x70, &parent, sizeof parent);
    memcpy(gun + 0x308, &current_controller, sizeof current_controller);
    memcpy(controller, &stock_vtable, sizeof stock_vtable);
    memcpy(alternate_controller, &stock_vtable, sizeof stock_vtable);
    static uintptr_t ids[] = {123};
    static uintptr_t group[3];
    group[0] = (uintptr_t)ids;
    group[1] = group[2] = (uintptr_t)(ids + 1);
    uintptr_t groups[] = {(uintptr_t)group, (uintptr_t)(group + 3)};
    memcpy(ship + 0x628, groups, sizeof groups);
    lifecycle_discard_ship = (uintptr_t)ship;
    stats.issued = player_recurring_tail_token.generation = 40;
}

static void expect_refusal(void) {
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        player_fire_observation_scope(40);
        _exit(0);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 223);
}

int main(void) {
    setup();
    unsetenv("X4_DEV_FIRE_DIAG");
    player_fire_observation_scope(40);
    assert(!reads && !forbidden_calls);
    assert(!setenv("X4_DEV_FIRE_DIAG", "1", 1));

    /* Preserve actual bytes, including raw91 values outside bool. */
    gun[0x2fd] = 1;
    controller[0x91] = 7;
    unsigned char gun_before[sizeof gun], controller_before[sizeof controller];
    memcpy(gun_before, gun, sizeof gun);
    memcpy(controller_before, controller, sizeof controller);
    player_fire_observation_scope(40);
    assert(player_fire_observation_done && !forbidden_calls);
    assert(!memcmp(gun, gun_before, sizeof gun) && !memcmp(controller, controller_before, sizeof controller));
    unsigned once_reads = reads;
    player_fire_observation_scope(40);
    assert(reads == once_reads);

    setup();
    uintptr_t empty = 0;
    memcpy(gun + 0x308, &empty, sizeof empty);
    player_fire_observation_scope(40);
    assert(player_fire_observation_done && !forbidden_calls && !memcmp(gun + 0x308, &empty, sizeof empty));

    setup();
    uintptr_t group_address = 0, ids_address = 0;
    memcpy(&group_address, ship + 0x628, sizeof group_address);
    memcpy(&ids_address, (void *)group_address, sizeof ids_address);
    uint64_t saved_runtime = 0;
    memcpy(&saved_runtime, (void *)ids_address, sizeof saved_runtime);
    memset((void *)ids_address, 0, sizeof saved_runtime);
    player_fire_observation_scope(40);
    assert(player_fire_observation_done && !forbidden_calls && !player_fire_observation.count);
    memcpy((void *)ids_address, &saved_runtime, sizeof saved_runtime);

    for (unsigned variant = 1; variant <= 6; ++variant) {
        setup();
        drift_variant = variant;
        expect_refusal();
    }
    setup();
    uintptr_t foreign_vtable = 0x123456;
    memcpy(controller, &foreign_vtable, sizeof foreign_vtable);
    expect_refusal();
    puts("FIRE observation actual fields, no mutation and six identity-drift refusals PASS");
    return 0;
}
