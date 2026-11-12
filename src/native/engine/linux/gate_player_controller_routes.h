#ifndef X4_LINUX_PLAYER_CONTROLLER_ROUTES_H
#define X4_LINUX_PLAYER_CONTROLLER_ROUTES_H

/* Synthetic-first, one P2 controller. Native integration must separately prove
 * exclusion, object lifetime, branch adapters and the pinned image. No hook or
 * native address is installed here. The owner must quiesce all readers before
 * restoring the vptr and before destroying this storage or the controller. */
#include <stdint.h>
#include <string.h>
#include <stdatomic.h>

#define PLAYER_ROUTE_VTABLE_SLOTS 79u
#define PLAYER_ROUTE_MOTION_COUNT 3u

typedef void (*player_route_motion)(void *, void *, unsigned char, double);
typedef unsigned char (*player_route_assist)(void *, unsigned char);
typedef unsigned char (*player_route_bool)(void *);
typedef float (*player_route_float)(void *);
/* Must compare controller+0x20 to held_ship and read held_ship+0xd1 safely.
 * Return zero on a mismatch or failed read. The owner keeps the ship alive. */
typedef int (*player_route_ship_flag)(void *controller, void *held_ship, unsigned char *flag);

typedef struct {
    unsigned char global_authority, assist_off, assist_on, agent_mode, no_highway;
    float throttle;
} player_route_snapshot;

typedef struct {
    /* Include Itanium ABI offset-to-top and RTTI words at [-2], [-1]. */
    void *table[PLAYER_ROUTE_VTABLE_SLOTS + 2];
    void **stock_table;
    void *controller;
    void *held_ship;
    player_route_ship_flag read_ship_flag;
    _Atomic unsigned access_failures;
    player_route_motion stock[3], agent[3], player[3];
    player_route_assist stock_assist;
    player_route_bool stock_agent_mode, stock_no_highway;
    player_route_float stock_throttle;
    _Atomic uint64_t snapshot;
} player_controller_routes;

/* One helper instance per module; no nesting or overlapping P2 route owners.
 * Scope is per-thread so foreign readers cannot observe the scoped bank. */
static _Atomic(player_controller_routes *) player_routes_active;
static _Thread_local player_controller_routes *player_routes_scope;
static const unsigned player_route_motion_slots[3] = {13, 14, 17};

static uint64_t player_routes_pack(player_route_snapshot snapshot) {
    uint32_t bits;
    memcpy(&bits, &snapshot.throttle, sizeof bits);
    return (uint64_t)bits << 32 | (uint64_t)!!snapshot.global_authority |
           (uint64_t)!!snapshot.assist_off << 1 | (uint64_t)!!snapshot.assist_on << 2 |
           (uint64_t)!!snapshot.agent_mode << 3 | (uint64_t)!!snapshot.no_highway << 4;
}
static player_route_snapshot player_routes_unpack(uint64_t word) {
    player_route_snapshot snapshot;
    uint32_t bits = (uint32_t)(word >> 32);
    snapshot.global_authority = word & 1;
    snapshot.assist_off = (word >> 1) & 1;
    snapshot.assist_on = (word >> 2) & 1;
    snapshot.agent_mode = (word >> 3) & 1;
    snapshot.no_highway = (word >> 4) & 1;
    memcpy(&snapshot.throttle, &bits, sizeof bits);
    return snapshot;
}
static player_controller_routes *player_routes_owner(void *controller) {
    player_controller_routes *routes = atomic_load_explicit(&player_routes_active, memory_order_acquire);
    return routes && routes->controller == controller ? routes : NULL;
}
static void player_routes_motion_dispatch(void *controller, void *output, unsigned char mode,
                                          double elapsed_time, unsigned index) {
    player_controller_routes *routes = player_routes_owner(controller);
    /* Only our controller receives this vtable; lifetime is an owner contract. */
    if (!routes)
        return;
    if (player_routes_scope == routes) {
        routes->stock[index](controller, output, mode, elapsed_time);
        return;
    }
    unsigned char ship_flag = 0;
    if (!routes->read_ship_flag(controller, routes->held_ship, &ship_flag) || ship_flag > 1) {
        /* A failed identity/read does not call either branch or modify output.
         * The owner must observe this sticky counter and stop the run. */
        atomic_fetch_add_explicit(&routes->access_failures, 1, memory_order_relaxed);
        return;
    }
    player_route_snapshot snapshot =
        player_routes_unpack(atomic_load_explicit(&routes->snapshot, memory_order_acquire));
    player_route_motion selected_route =
        (snapshot.global_authority || !ship_flag) ? routes->agent[index] : routes->player[index];
    selected_route(controller, output, mode, elapsed_time);
}
static void player_routes_transform(void *controller, void *output, unsigned char mode, double elapsed_time) {
    player_routes_motion_dispatch(controller, output, mode, elapsed_time, 0);
}
static void player_routes_velocity(void *controller, void *output, unsigned char mode, double elapsed_time) {
    player_routes_motion_dispatch(controller, output, mode, elapsed_time, 1);
}
static void player_routes_acceleration(void *controller, void *output, unsigned char mode,
                                       double elapsed_time) {
    player_routes_motion_dispatch(controller, output, mode, elapsed_time, 2);
}
static unsigned char player_routes_assist(void *controller, unsigned char arg) {
    player_controller_routes *routes = player_routes_owner(controller);
    if (!routes)
        return 0;
    if (player_routes_scope == routes)
        return routes->stock_assist(controller, arg);
    player_route_snapshot snapshot =
        player_routes_unpack(atomic_load_explicit(&routes->snapshot, memory_order_acquire));
    return arg ? snapshot.assist_on : snapshot.assist_off;
}
static unsigned char player_routes_agent_mode(void *controller) {
    player_controller_routes *routes = player_routes_owner(controller);
    if (!routes)
        return 0;
    return player_routes_scope == routes
               ? routes->stock_agent_mode(controller)
               : player_routes_unpack(atomic_load_explicit(&routes->snapshot, memory_order_acquire))
                     .agent_mode;
}
static unsigned char player_routes_no_highway(void *controller) {
    player_controller_routes *routes = player_routes_owner(controller);
    if (!routes)
        return 0;
    return player_routes_scope == routes
               ? routes->stock_no_highway(controller)
               : player_routes_unpack(atomic_load_explicit(&routes->snapshot, memory_order_acquire))
                     .no_highway;
}
static float player_routes_throttle(void *controller) {
    player_controller_routes *routes = player_routes_owner(controller);
    if (!routes)
        return 0;
    return player_routes_scope == routes
               ? routes->stock_throttle(controller)
               : player_routes_unpack(atomic_load_explicit(&routes->snapshot, memory_order_acquire)).throttle;
}
/* Caller supplies verified branch adapters with the exact motion ABI.
 * Native adapters are supplied by gate_player_controller_native_motion.h.
 * The getter-only API leaves slots 40..43 unchanged. Supplying callback
 * overrides additionally requires verified callback ABIs and scope ownership. */
/* Optional private entries are supplied before the vptr is published. Slot 42
 * remains caller-owned so the context-bind isolation adapter can be composed. */
typedef struct {
    void *slot40;
    void *slot41;
    void *slot42;
    void *slot43;
} player_route_callbacks;

static int player_routes_install_with_callbacks(player_controller_routes *routes, void *controller,
                                                void **stock, const player_route_motion agent[3],
                                                const player_route_motion player[3], void *held_ship,
                                                player_route_ship_flag read_ship_flag,
                                                player_route_snapshot initial,
                                                const player_route_callbacks *callbacks) {
    if (!routes || !controller || !stock || !agent || !player || !held_ship || !read_ship_flag ||
        *(void ***)controller != stock)
        return 0;
    for (unsigned index = 0; index < 3; index++)
        if (!stock[player_route_motion_slots[index]] || !agent[index] || !player[index])
            return 0;
    if (!stock[21] || !stock[30] || !stock[56] || !stock[57])
        return 0;
    if (atomic_load_explicit(&player_routes_active, memory_order_acquire))
        return 0;
    memset(routes, 0, sizeof *routes);
    routes->controller = controller;
    routes->stock_table = stock;
    routes->held_ship = held_ship;
    routes->read_ship_flag = read_ship_flag;
    atomic_init(&routes->access_failures, 0);
    memcpy(routes->table, stock - 2, sizeof routes->table);
    for (unsigned index = 0; index < 3; index++) {
        routes->stock[index] = (player_route_motion)stock[player_route_motion_slots[index]];
        routes->agent[index] = agent[index];
        routes->player[index] = player[index];
    }
    routes->stock_throttle = (player_route_float)stock[21];
    routes->stock_no_highway = (player_route_bool)stock[30];
    routes->stock_assist = (player_route_assist)stock[56];
    routes->stock_agent_mode = (player_route_bool)stock[57];
    routes->table[2 + 13] = (void *)player_routes_transform;
    routes->table[2 + 14] = (void *)player_routes_velocity;
    routes->table[2 + 17] = (void *)player_routes_acceleration;
    routes->table[2 + 21] = (void *)player_routes_throttle;
    routes->table[2 + 30] = (void *)player_routes_no_highway;
    routes->table[2 + 56] = (void *)player_routes_assist;
    routes->table[2 + 57] = (void *)player_routes_agent_mode;
    if (callbacks) {
        if (callbacks->slot40)
            routes->table[2 + 40] = callbacks->slot40;
        if (callbacks->slot41)
            routes->table[2 + 41] = callbacks->slot41;
        if (callbacks->slot42)
            routes->table[2 + 42] = callbacks->slot42;
        if (callbacks->slot43)
            routes->table[2 + 43] = callbacks->slot43;
    }
    atomic_init(&routes->snapshot, player_routes_pack(initial));
    player_controller_routes *empty = NULL;
    if (!atomic_compare_exchange_strong_explicit(&player_routes_active, &empty, routes, memory_order_release,
                                                 memory_order_relaxed))
        return 0;
    void **expected = stock;
    if (!__atomic_compare_exchange_n((void ***)controller, &expected, &routes->table[2], 0, __ATOMIC_RELEASE,
                                     __ATOMIC_RELAXED)) {
        atomic_store_explicit(&player_routes_active, NULL, memory_order_release);
        return 0;
    }
    return 1;
}
/* Existing getter-only callers retain the original installation behavior. */
static inline int player_routes_install(player_controller_routes *routes, void *controller, void **stock,
                                        const player_route_motion agent[3],
                                        const player_route_motion player[3], void *held_ship,
                                        player_route_ship_flag read_ship_flag,
                                        player_route_snapshot initial) {
    return player_routes_install_with_callbacks(routes, controller, stock, agent, player, held_ship,
                                                read_ship_flag, initial, NULL);
}
static int player_routes_enter(player_controller_routes *routes) {
    if (!routes || player_routes_scope ||
        atomic_load_explicit(&player_routes_active, memory_order_acquire) != routes)
        return 0;
    player_routes_scope = routes;
    return 1;
}
/* Capture stock answers while bank belongs to P2; global_authority must be provided by the owning scope,
 * excluding ship flag. Readers use the verified accessor and held ship lifetime contract for that flag. */
static int player_routes_leave(player_controller_routes *routes, player_route_snapshot snapshot) {
    if (!routes || player_routes_scope != routes)
        return 0;
    atomic_store_explicit(&routes->snapshot, player_routes_pack(snapshot), memory_order_release);
    player_routes_scope = NULL;
    return 1;
}
/* Caller guarantees no other thread is executing or starting a table call. */
static int player_routes_restore(player_controller_routes *routes) {
    if (!routes || player_routes_scope ||
        atomic_load_explicit(&player_routes_active, memory_order_acquire) != routes)
        return 0;
    void **expected = &routes->table[2];
    if (!__atomic_compare_exchange_n((void ***)routes->controller, &expected, routes->stock_table, 0,
                                     __ATOMIC_RELEASE, __ATOMIC_RELAXED))
        return 0;
    atomic_store_explicit(&player_routes_active, NULL, memory_order_release);
    return 1;
}
#endif
