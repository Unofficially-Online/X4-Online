#ifndef X4_LINUX_PLAYER_ROUTE_COMPARE_H
#define X4_LINUX_PLAYER_ROUTE_COMPARE_H

/* Default-unused disposable-process experiment. The caller owns the joined
 * boundary, the held ship/controller, the active P2 bank and singleton, all
 * reader exclusion, and the restoration of globals. Include after the gate's
 * LX(), read_local() and hash_ok() definitions. No engine hook is installed. */
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include "gate_player_controller_native_motion.h"
#include "player_getter_preload_pin.h"

#define ROUTE_COMPARE_PAGE_SIZE 4096u
#define ROUTE_COMPARE_BUFFER_SIZE 128u
#define ROUTE_COMPARE_CANARY 0xa5u

static const unsigned route_compare_slots[7] = {13, 14, 17, 21, 30, 56, 57};
static const uintptr_t route_compare_targets[7] = {0x1659010u, 0x165a190u, 0x165a910u, 0xd586e0u,
                                                   0xd586f0u,  0x1653f40u, 0x164cd40u};
static const size_t route_compare_output_sizes[3] = {64, 32, 32};

typedef struct {
    _Alignas(16) unsigned char prefix[16];
    _Alignas(16) unsigned char data[ROUTE_COMPARE_BUFFER_SIZE];
    unsigned char suffix[16];
} route_compare_output;

typedef struct {
    player_controller_routes routes;
    unsigned char *stub_page;
    void *controller;
    void *held_ship;
    void **stock_table;
    double elapsed_time;
    route_compare_output scoped[3][2];
    route_compare_output offscope[3][2];
    player_route_snapshot snapshot;
    unsigned char scoped_authority_before;
    unsigned char scoped_authority_after;
    unsigned char scoped_ship_before;
    unsigned char scoped_ship_after;
    unsigned char offscope_ship_before;
    unsigned char offscope_ship_after;
    unsigned char selected_agent;
    unsigned raw_mismatch_mask;
    unsigned scalar_mismatch_mask;
    int installed;
    int captured;
    int left;
    int compared;
} route_compare_experiment;

/* Caller holds the actual ship through every virtual call and ensures the
 * controller/ship cannot die or be rebound while this function is running. */
static int route_compare_ship_flag(void *controller, void *held_ship, unsigned char *flag) {
    void *current_ship = NULL;
    return controller && held_ship && flag &&
           read_local((unsigned char *)controller + 0x20, &current_ship, sizeof current_ship) &&
           current_ship == held_ship && read_local((unsigned char *)held_ship + 0xd1, flag, sizeof *flag) &&
           *flag <= 1;
}

static int route_compare_snapshot(route_compare_experiment *experiment, player_route_snapshot *snapshot) {
    uint64_t autopilot = 0;
    unsigned char flight_control = 0;
    unsigned char highway = 0;
    unsigned char ship_flag = 0;
    if (!route_compare_ship_flag(experiment->controller, experiment->held_ship, &ship_flag) ||
        !read_local(LX(0x3d8da58u), &autopilot, sizeof autopilot) ||
        !read_local(LX(0x3d8daa8u), &flight_control, sizeof flight_control) ||
        !read_local(LX(0x3e4cdc0u), &highway, sizeof highway) ||
        !read_local(LX(0x3d8d9e4u), &snapshot->throttle, sizeof snapshot->throttle))
        return 0;
    player_route_assist assist = (player_route_assist)experiment->stock_table[56];
    player_route_bool agent_mode = (player_route_bool)experiment->stock_table[57];
    snapshot->assist_off = assist(experiment->controller, 0);
    snapshot->assist_on = assist(experiment->controller, 1);
    snapshot->agent_mode = agent_mode(experiment->controller);
    snapshot->no_highway = !highway;
    snapshot->global_authority = !!autopilot || (!flight_control && snapshot->assist_off && !highway);
    return 1;
}

static int route_compare_prepare_with_callbacks(route_compare_experiment *experiment, void *controller,
                                                void *held_ship, double elapsed_time,
                                                const player_route_callbacks *callbacks) {
    if (!experiment || !controller || !held_ship || experiment->installed || experiment->stub_page)
        return 0;
    for (unsigned index = 0; index < sizeof player_native_fde_pins / sizeof player_native_fde_pins[0];
         ++index) {
        const player_native_pin *pin = &player_native_fde_pins[index];
        if (index == 0 ? !player_transform_getter_pin() : !hash_ok(LX(pin->va), pin->size, pin->sha256))
            return 0;
    }
    void **stock_table = NULL;
    if (!read_local(controller, &stock_table, sizeof stock_table) || stock_table != (void **)LX(0x3b7c208u))
        return 0;
    for (unsigned index = 0; index < 7; ++index) {
        void *actual = NULL;
        if (!read_local(stock_table + route_compare_slots[index], &actual, sizeof actual) ||
            actual != LX(route_compare_targets[index]))
            return 0;
    }
    experiment->controller = controller;
    experiment->held_ship = held_ship;
    experiment->stock_table = stock_table;
    experiment->elapsed_time = elapsed_time;
    if (!route_compare_snapshot(experiment, &experiment->snapshot))
        return 0;
    experiment->stub_page =
        mmap(NULL, ROUTE_COMPARE_PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (experiment->stub_page == MAP_FAILED) {
        experiment->stub_page = NULL;
        return 0;
    }
    player_route_motion agents[3], players[3];
    uintptr_t bias = (uintptr_t)LX(0x400000u) - 0x400000u;
    if (!player_native_bind(bias, experiment->stub_page, ROUTE_COMPARE_PAGE_SIZE, agents, players) ||
        mprotect(experiment->stub_page, ROUTE_COMPARE_PAGE_SIZE, PROT_READ | PROT_EXEC)) {
        munmap(experiment->stub_page, ROUTE_COMPARE_PAGE_SIZE);
        experiment->stub_page = NULL;
        return 0;
    }
    __builtin___clear_cache((char *)experiment->stub_page,
                            (char *)experiment->stub_page + ROUTE_COMPARE_PAGE_SIZE);
    if (!player_routes_install_with_callbacks(&experiment->routes, controller, stock_table, agents, players,
                                              held_ship, route_compare_ship_flag, experiment->snapshot,
                                              callbacks)) {
        munmap(experiment->stub_page, ROUTE_COMPARE_PAGE_SIZE);
        experiment->stub_page = NULL;
        return 0;
    }
    experiment->installed = 1;
    if (!player_routes_enter(&experiment->routes))
        return 0; /* Owner must restore the installed table before release. */
    return 1;
}

static inline int route_compare_prepare(route_compare_experiment *experiment, void *controller,
                                        void *held_ship, double elapsed_time) {
    return route_compare_prepare_with_callbacks(experiment, controller, held_ship, elapsed_time, NULL);
}

/* ELF 16592a0 composes a float32 affine transform: translation first,
 * then three basis vectors. This non-identity rotation and translation are a
 * valid rigid transform, so mode 0 distinguishes composition from overwrite.
 * ELF 165a271 and 165a9c0 use float32 addps on two four-float vectors; the
 * fourth lanes are zero padding, with nonzero finite xyz components. */
static void route_compare_initialize_output(route_compare_output *output, unsigned index, unsigned mode) {
    static const float transform_seed[16] = {1.0f,  2.0f, 3.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                             -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    static const float vector_seed[8] = {1.0f, 2.0f, 3.0f, 0.0f, 4.0f, 5.0f, 6.0f, 0.0f};
    memset(output, ROUTE_COMPARE_CANARY, sizeof *output);
    memset(output->data, 0, route_compare_output_sizes[index]);
    if (mode == 0) {
        if (index == 0)
            memcpy(output->data, transform_seed, sizeof transform_seed);
        else
            memcpy(output->data, vector_seed, sizeof vector_seed);
    }
}

static int route_compare_read_motion(route_compare_experiment *experiment,
                                     route_compare_output output[3][2]) {
    void **table = NULL;
    if (!read_local(experiment->controller, &table, sizeof table) || table != &experiment->routes.table[2])
        return 0;
    for (unsigned index = 0; index < 3; ++index) {
        for (unsigned mode = 0; mode < 2; ++mode) {
            route_compare_output *row = &output[index][mode];
            route_compare_initialize_output(row, index, mode);
            ((player_route_motion)table[route_compare_slots[index]])(
                experiment->controller, row->data, (unsigned char)mode, experiment->elapsed_time);
            for (unsigned position = 0; position < sizeof row->prefix; ++position)
                if (row->prefix[position] != ROUTE_COMPARE_CANARY ||
                    row->suffix[position] != ROUTE_COMPARE_CANARY)
                    return 0;
            for (size_t position = route_compare_output_sizes[index]; position < sizeof row->data; ++position)
                if (row->data[position] != ROUTE_COMPARE_CANARY)
                    return 0;
        }
    }
    return !atomic_load_explicit(&experiment->routes.access_failures, memory_order_relaxed);
}

static int route_compare_capture_scoped(route_compare_experiment *experiment) {
    if (!experiment || !experiment->installed || experiment->captured ||
        player_routes_scope != &experiment->routes)
        return 0;
    player_route_snapshot before = {0}, after = {0};
    if (!route_compare_ship_flag(experiment->controller, experiment->held_ship,
                                 &experiment->scoped_ship_before) ||
        !route_compare_snapshot(experiment, &before) ||
        !route_compare_read_motion(experiment, experiment->scoped) ||
        !route_compare_snapshot(experiment, &after) ||
        !route_compare_ship_flag(experiment->controller, experiment->held_ship,
                                 &experiment->scoped_ship_after))
        return 0;
    experiment->scoped_authority_before = before.global_authority;
    experiment->scoped_authority_after = after.global_authority;
    if (experiment->scoped_authority_before != experiment->scoped_authority_after ||
        experiment->scoped_ship_before != experiment->scoped_ship_after)
        return 0;
    experiment->captured = 1;
    return 1;
}

/* The caller still owns P2 globals here; restore them only after this returns. */
static int route_compare_leave(route_compare_experiment *experiment) {
    if (!experiment || !experiment->captured || experiment->left ||
        !route_compare_snapshot(experiment, &experiment->snapshot))
        return 0;
    unsigned char ship_flag = 0;
    if (!route_compare_ship_flag(experiment->controller, experiment->held_ship, &ship_flag) ||
        ship_flag != experiment->scoped_ship_after ||
        experiment->snapshot.global_authority != experiment->scoped_authority_after ||
        !player_routes_leave(&experiment->routes, experiment->snapshot))
        return 0;
    experiment->left = 1;
    return 1;
}

/* Caller must have restored and checked the canonical bank/singleton/zone. */
static int route_compare_offscope(route_compare_experiment *experiment) {
    if (!experiment || !experiment->left || experiment->compared ||
        !route_compare_ship_flag(experiment->controller, experiment->held_ship,
                                 &experiment->offscope_ship_before) ||
        experiment->offscope_ship_before != experiment->scoped_ship_after ||
        !route_compare_read_motion(experiment, experiment->offscope) ||
        !route_compare_ship_flag(experiment->controller, experiment->held_ship,
                                 &experiment->offscope_ship_after) ||
        experiment->offscope_ship_before != experiment->offscope_ship_after)
        return 0;
    experiment->selected_agent = experiment->snapshot.global_authority || !experiment->offscope_ship_before;
    for (unsigned index = 0; index < 3; ++index)
        for (unsigned mode = 0; mode < 2; ++mode)
            if (memcmp(experiment->scoped[index][mode].data, experiment->offscope[index][mode].data,
                       route_compare_output_sizes[index]))
                experiment->raw_mismatch_mask |= 1u << (index * 2 + mode);
    void **table = &experiment->routes.table[2];
    player_route_snapshot *snapshot = &experiment->snapshot;
    if (((player_route_assist)table[56])(experiment->controller, 0) != !!snapshot->assist_off ||
        ((player_route_assist)table[56])(experiment->controller, 1) != !!snapshot->assist_on)
        experiment->scalar_mismatch_mask |= 1u;
    if (((player_route_bool)table[57])(experiment->controller) != !!snapshot->agent_mode)
        experiment->scalar_mismatch_mask |= 2u;
    if (((player_route_bool)table[30])(experiment->controller) != !!snapshot->no_highway)
        experiment->scalar_mismatch_mask |= 4u;
    float throttle = ((player_route_float)table[21])(experiment->controller);
    if (memcmp(&throttle, &snapshot->throttle, sizeof throttle))
        experiment->scalar_mismatch_mask |= 8u;
    experiment->compared = 1;
    return 1;
}

/* Call only after every table/stub reader is quiescent, before native release.
 * Keep the RX page resident if table restoration fails. */
static inline int route_compare_restore(route_compare_experiment *experiment) {
    if (!experiment || !experiment->installed)
        return 0;
    /* On a failed scoped capture, close the helper scope before restoring its
     * vtable. The caller has already excluded all readers. */
    if (player_routes_scope == &experiment->routes &&
        !player_routes_leave(&experiment->routes, experiment->snapshot))
        return 0;
    if (!player_routes_restore(&experiment->routes))
        return 0;
    experiment->installed = 0;
    if (munmap(experiment->stub_page, ROUTE_COMPARE_PAGE_SIZE))
        return 0;
    experiment->stub_page = NULL;
    return 1;
}

/* Disposable-process variant: restore the published vptr, but do not reclaim
 * the old table, route owner or executable stubs. A reader which already took
 * the old table may still be in flight. Keeping storage alive prevents this
 * probe from introducing an avoidable unmap race; it does not prove reader
 * exclusion or make subsequent controller destruction safe for live gameplay.
 * The caller must terminate this isolated process without resuming simulation. */
static int route_compare_detach_for_discard(route_compare_experiment *experiment) {
    if (!experiment || !experiment->installed || player_routes_scope ||
        atomic_load_explicit(&player_routes_active, memory_order_acquire) != &experiment->routes)
        return 0;
    void **expected = &experiment->routes.table[2];
    if (!__atomic_compare_exchange_n((void ***)experiment->controller, &expected, experiment->stock_table, 0,
                                     __ATOMIC_RELEASE, __ATOMIC_RELAXED))
        return 0;
    experiment->installed = 0;
    return 1;
}
#endif
