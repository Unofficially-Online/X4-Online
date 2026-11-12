#define _GNU_SOURCE
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static uint64_t fake_autopilot;
static unsigned char fake_flight_control = 1;
static unsigned char fake_highway;
static float fake_throttle = 0.5f;
static void *fake_address(uintptr_t address) {
    switch (address) {
    case 0x3d8da58u:
        return &fake_autopilot;
    case 0x3d8daa8u:
        return &fake_flight_control;
    case 0x3e4cdc0u:
        return &fake_highway;
    case 0x3d8d9e4u:
        return &fake_throttle;
    default:
        return (void *)address;
    }
}
#define LX(va) fake_address((uintptr_t)(va))
static int read_local(const void *source, void *target, size_t length) {
    memcpy(target, source, length);
    return 1;
}
static int hash_ok(const void *source, size_t length, const char *hash) {
    (void)source;
    (void)length;
    (void)hash;
    return 0;
}

#define GATE_PAGE 4096u
static int gate_read_local(const void *source, void *target, size_t length) {
    return read_local(source, target, length);
}
static int gate_maps_find(uintptr_t address, char permissions[5], char *path, size_t capacity) {
    (void)address;
    (void)permissions;
    (void)path;
    (void)capacity;
    return 0;
}
#include "gate_player_route_compare.h"

typedef struct {
    void **table;
    unsigned char reserved[0x18];
    void *ship;
} fake_controller;
static int overflow;
static int change_authority;
static int change_ship_flag;
static void transform(void *controller, void *output, unsigned char mode, double elapsed_time) {
    (void)elapsed_time;
    if (change_authority) {
        fake_autopilot = 1;
        change_authority = 0;
    }
    if (change_ship_flag) {
        fake_controller *object = controller;
        ((unsigned char *)object->ship)[0xd1] = 0;
        change_ship_flag = 0;
    }
    memset(output, mode ? 1 : 2, overflow ? 65 : 64);
}
static void different_transform(void *controller, void *output, unsigned char mode, double elapsed_time) {
    transform(controller, output, mode, elapsed_time);
    ((unsigned char *)output)[0] ^= 1;
}
static void vector(void *controller, void *output, unsigned char mode, double elapsed_time) {
    (void)controller;
    (void)elapsed_time;
    float *components = output;
    for (unsigned index = 0; index < 8; ++index) {
        if (mode)
            components[index] = 0.5f;
        else
            components[index] += 0.5f;
    }
}
static void wrong_overwrite_vector(void *controller, void *output, unsigned char mode, double elapsed_time) {
    (void)controller;
    (void)mode;
    (void)elapsed_time;
    float *components = output;
    for (unsigned index = 0; index < 8; ++index)
        components[index] = 0.5f;
}
static unsigned char assist(void *controller, unsigned char argument) {
    (void)controller;
    return !!argument;
}
static unsigned char no_highway(void *controller) {
    (void)controller;
    return 1;
}
static float throttle(void *controller) {
    (void)controller;
    return 0.5f;
}
static void unused(void) {
}
static void callback40(void) {
}
static void callback41(void) {
}
static void callback42(void) {
}
static void callback43(void) {
}
int main(void) {
    /* Check bounds and comparison using a synthetic controller only. Native
     * preparation is intentionally not executed by this fixture. */
    (void)factory_probe_factory_pin;
    (void)route_compare_prepare;
    (void)route_compare_leave;
    (void)route_compare_detach_for_discard;
    void *raw_table[81];
    for (unsigned index = 0; index < 81; ++index)
        raw_table[index] = (void *)unused;
    void **stock_table = raw_table + 2;
    stock_table[13] = (void *)transform;
    stock_table[14] = stock_table[17] = (void *)vector;
    stock_table[21] = (void *)throttle;
    stock_table[30] = stock_table[57] = (void *)no_highway;
    stock_table[56] = (void *)assist;
    unsigned char ship[0xd2] = {0};
    ship[0xd1] = 1;
    fake_controller controller = {stock_table, {0}, ship};
    route_compare_experiment experiment = {0};
    player_route_motion branches[3] = {transform, vector, vector};
    player_route_snapshot snapshot = {0, 0, 1, 1, 1, 0.5f};
    experiment.controller = &controller;
    experiment.held_ship = ship;
    experiment.stock_table = stock_table;
    experiment.elapsed_time = 9.25;
    player_route_callbacks callbacks = {(void *)callback40, (void *)callback41, (void *)callback42,
                                        (void *)callback43};
    assert(player_routes_install_with_callbacks(&experiment.routes, &controller, stock_table, branches,
                                                branches, ship, route_compare_ship_flag, snapshot,
                                                &callbacks));
    assert(controller.table == &experiment.routes.table[2]);
    assert(controller.table[40] == callbacks.slot40 && controller.table[41] == callbacks.slot41 &&
           controller.table[42] == callbacks.slot42 && controller.table[43] == callbacks.slot43);
    assert(stock_table[40] == (void *)unused && stock_table[41] == (void *)unused &&
           stock_table[42] == (void *)unused && stock_table[43] == (void *)unused);
    assert(controller.table[13] == (void *)player_routes_transform &&
           controller.table[14] == (void *)player_routes_velocity &&
           controller.table[17] == (void *)player_routes_acceleration);
    experiment.installed = 1;
    assert(player_routes_enter(&experiment.routes));
    overflow = 1;
    assert(!route_compare_read_motion(&experiment, experiment.scoped));
    overflow = 0;
    change_authority = 1;
    assert(!route_compare_capture_scoped(&experiment));
    fake_autopilot = 0;
    change_ship_flag = 1;
    assert(!route_compare_capture_scoped(&experiment));
    ship[0xd1] = 1;
    assert(route_compare_capture_scoped(&experiment));
    assert(player_routes_leave(&experiment.routes, snapshot));
    experiment.snapshot = snapshot;
    experiment.left = 1;
    experiment.routes.agent[0] = different_transform;
    experiment.routes.player[0] = different_transform;
    experiment.routes.agent[1] = experiment.routes.player[1] = wrong_overwrite_vector;
    experiment.routes.agent[2] = experiment.routes.player[2] = wrong_overwrite_vector;
    assert(route_compare_offscope(&experiment));
    assert(experiment.raw_mismatch_mask == (3u | (1u << 2) | (1u << 4)) &&
           experiment.scalar_mismatch_mask == 0);
    assert(experiment.scoped_authority_before == 0 && experiment.scoped_authority_after == 0);
    assert(experiment.scoped_ship_before == 1 && experiment.scoped_ship_after == 1);
    assert(experiment.offscope_ship_before == 1 && experiment.offscope_ship_after == 1);
    assert(experiment.selected_agent == 0);
    float expected_velocity = 0;
    memcpy(&expected_velocity, experiment.scoped[1][0].data, sizeof expected_velocity);
    assert(expected_velocity == 1.5f);
    assert(player_routes_restore(&experiment.routes));
    assert(controller.table == stock_table);
    player_route_callbacks partial = {NULL, (void *)callback41, NULL, NULL};
    assert(player_routes_install_with_callbacks(&experiment.routes, &controller, stock_table, branches,
                                                branches, ship, route_compare_ship_flag, snapshot,
                                                &partial));
    assert(controller.table[40] == stock_table[40] && controller.table[41] == partial.slot41 &&
           controller.table[42] == stock_table[42] && controller.table[43] == stock_table[43]);
    assert(player_routes_restore(&experiment.routes));
    assert(player_routes_install(&experiment.routes, &controller, stock_table, branches, branches, ship,
                                 route_compare_ship_flag, snapshot));
    for (unsigned index = 40; index <= 43; ++index)
        assert(controller.table[index] == stock_table[index]);
    assert(player_routes_restore(&experiment.routes));
    puts("synthetic getter comparison and output canaries PASS");
}
