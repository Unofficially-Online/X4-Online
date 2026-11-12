#ifndef X4_PLAYER_CONTROLLER_CALLBACK_ADAPTER_H
#define X4_PLAYER_CONTROLLER_CALLBACK_ADAPTER_H

/* Unwired single-controller adapter. The owner validates the exact ELF, all
 * original entries and ABIs, object lifetime, worker thread, isolation and
 * quiescence. Operations must make bank, singleton, zone and getter-route
 * scope one transaction, retaining uncertain obligations. Neither this file
 * nor the policy supplies native exchange operations or installs a hook. */
#include <stdatomic.h>
#include <stdlib.h>
#include "player_controller_callback_scope.h"
#include "gate_player_controller_routes.h"

typedef struct {
    pcc_policy policy;
    player_controller_routes *routes;
    void (*fatal)(const char *reason);
} player_callback_adapter;

/* One resident controller binding per module, published before its vptr. */
static _Atomic(player_callback_adapter *) player_callback_active;

_Noreturn static void player_callback_fatal(player_callback_adapter *adapter, const char *reason) {
    if (adapter && adapter->fatal)
        adapter->fatal(reason);
    /* Native virtual functions cannot propagate an error to their caller. */
    _Exit(126);
}

static player_callback_adapter *player_callback_owner(void) {
    player_callback_adapter *adapter = atomic_load_explicit(&player_callback_active, memory_order_acquire);
    if (!adapter)
        player_callback_fatal(NULL, "callback_without_owner");
    return adapter;
}

static void player_callback_upkeep(void *controller, double elapsed_time) {
    player_callback_adapter *adapter = player_callback_owner();
    if (!pcc_upkeep(&adapter->policy, controller, elapsed_time))
        player_callback_fatal(adapter, "upkeep_failed");
}

static void player_callback_environment(void *controller, void *first, void *second) {
    player_callback_adapter *adapter = player_callback_owner();
    if (!pcc_environment(&adapter->policy, controller, first, second))
        player_callback_fatal(adapter, "environment_failed");
}

static void player_callback_change(void *controller, void *first, void *second, unsigned char mode) {
    player_callback_adapter *adapter = player_callback_owner();
    if (!pcc_change(&adapter->policy, controller, first, second, mode))
        player_callback_fatal(adapter, "change_failed");
}

/* stock entries and original targets must already be validated by the caller
 * against its native pins. On any failure after initialization, this binding is
 * not reusable. The caller supplies an optional independently checked slot42. */
static int player_callback_install(player_callback_adapter *adapter, player_controller_routes *routes,
                                   void *controller, void **stock, void *held_ship,
                                   player_route_ship_flag read_ship_flag, const player_route_motion agents[3],
                                   const player_route_motion players[3], player_route_snapshot initial,
                                   const pcc_operations *operations, const pcc_originals *originals,
                                   uint64_t owner_thread, void (*fatal)(const char *reason), void *slot42) {
    if (!adapter || !routes || !controller || !stock || !originals || !fatal ||
        stock[40] != (void *)originals->upkeep || stock[41] != (void *)originals->environment ||
        stock[43] != (void *)originals->change ||
        atomic_load_explicit(&player_callback_active, memory_order_acquire))
        return 0;
    if (!pcc_initialize(&adapter->policy, operations, originals, controller, owner_thread))
        return 0;
    adapter->routes = routes;
    adapter->fatal = fatal;
    player_callback_adapter *empty = NULL;
    if (!atomic_compare_exchange_strong_explicit(&player_callback_active, &empty, adapter,
                                                 memory_order_release, memory_order_relaxed))
        return 0;
    player_route_callbacks callbacks = {(void *)player_callback_upkeep, (void *)player_callback_environment,
                                        slot42, (void *)player_callback_change};
    if (!player_routes_install_with_callbacks(routes, controller, stock, agents, players, held_ship,
                                              read_ship_flag, initial, &callbacks)) {
        atomic_store_explicit(&player_callback_active, NULL, memory_order_release);
        return 0;
    }
    return 1;
}

/* Only with all possible vtable readers quiescent. The native release must
 * happen after this restores the original controller vptr. Adapter and code
 * remain resident, including if any restoration check fails. */
static int player_callback_restore(player_callback_adapter *adapter) {
    if (!adapter || atomic_load_explicit(&player_callback_active, memory_order_acquire) != adapter ||
        pcc_active || !adapter->routes || !player_routes_restore(adapter->routes))
        return 0;
    atomic_store_explicit(&player_callback_active, NULL, memory_order_release);
    return 1;
}
#endif
