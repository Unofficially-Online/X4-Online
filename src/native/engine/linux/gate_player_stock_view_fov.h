#ifndef X4_GATE_PLAYER_STOCK_VIEW_FOV_H
#define X4_GATE_PLAYER_STOCK_VIEW_FOV_H

/* Disposable, opt-in coefficient routing diagnostic. The keyboard coordinator
 * admits VIEW only through its explicit finite wire configuration. No acceptance
 * of Windows records, independent-reader exclusion or normal session resume is implied. */
#include <stdio.h>
#include "player_stock_view_state.h"
#include "player_stock_view_fov_preload_pin.h"
#include "player_stock_view_ship_preload_pin.h"
#include "player_stock_view_component_preload_pin.h"
#include "player_stock_view_precompare_preload_pin.h"
#include "player_view_xs_preload_pin.h"

static unsigned stock_view_gate_mode; /* 0 unchecked, 1 off, 2 rewrite, 3 observe */
static uint32_t stock_view_gate_tid;
static uintptr_t stock_view_gate_controllers[2];
static uintptr_t stock_view_gate_ships[2];
static player_stock_view_state stock_view_gate_state;
static player_view_query_api stock_view_gate_sealed;
static player_stock_view_fov_api stock_view_gate_provider;
static player_stock_view_ship_api stock_view_gate_ship_provider;
static unsigned stock_view_gate_bound;
static unsigned stock_view_gate_component_enabled;
static unsigned stock_view_gate_xs_enabled;
static int player_view_xs_gate_target(uintptr_t, uintptr_t, uintptr_t *);
static int player_view_xs_gate_external(uint64_t, uint64_t *);
static int player_view_xs_gate_distance(uintptr_t, uintptr_t, unsigned, float, float *);
static _Atomic uint64_t stock_view_gate_observations[3][3][4];

/* Pointer-value observation only. Foreign threads do not read mutable gate or
 * TLS state; their counts remain a lower bound at the final sampled snapshot. */
static void stock_view_gate_observe(unsigned site, uintptr_t controller) {
    unsigned slot = controller == stock_view_gate_controllers[0] ? 0u :
                    controller == stock_view_gate_controllers[1] ? 1u : 2u;
    long tid = syscall(SYS_gettid);
    unsigned phase = 3; /* foreign or an unidentifiable thread */
    if (tid > 0 && (uint64_t)tid == stock_view_gate_tid) {
        phase = !player_recurring_physics_entered() ? 0u : player_routes_scope ? 2u : 1u;
    }
    _Atomic uint64_t *counter = &stock_view_gate_observations[site][slot][phase];
    uint64_t value = atomic_load_explicit(counter, memory_order_relaxed);
    while (value != UINT64_MAX && !atomic_compare_exchange_weak_explicit(
        counter, &value, value + 1, memory_order_relaxed, memory_order_relaxed)) {}
}

static int stock_view_gate_owner(uintptr_t controller, unsigned *slot) {
    if (controller != stock_view_gate_controllers[0] && controller != stock_view_gate_controllers[1]) {
        *slot = 0;
        return 1;
    }
    *slot = controller == stock_view_gate_controllers[0] ? 1u : 2u;
    long tid = syscall(SYS_gettid);
    if (tid <= 0 || (uint64_t)tid != stock_view_gate_tid ||
        !player_recurring_physics_entered() ||
        stats.issued != stock_view_gate_state.generation ||
        player_recurring_tail_token.generation != stock_view_gate_state.generation ||
        !atomic_load_explicit(&player_physics_ready, memory_order_acquire) ||
        !player_physics_identity_for(player_routes_scope != NULL) ||
        lifecycle_discard_controller != stock_view_gate_controllers[1] ||
        factory_probe_identity.controller != stock_view_gate_controllers[0]) return 0;
    return 1;
}

static int stock_view_gate_mode_policy(uintptr_t controller, int original, unsigned site, int *result) {
    unsigned slot;
    if (!result || site > 1) return 0;
    if (stock_view_gate_mode == 3) {
        stock_view_gate_observe(site, controller);
        *result = original;
        return 1;
    }
    if (!stock_view_gate_owner(controller, &slot)) return 0;
    if (!slot) { *result = original; return 1; }
    player_stock_view view;
    if (!player_stock_view_read(&stock_view_gate_state, stats.issued, slot,
                                stock_view_gate_state.saved[slot - 1], &view)) return 0;
    *result = view.cockpit ? 0 : 1;
    return 1;
}

static int stock_view_gate_fov_policy(uintptr_t controller, float original, float *result) {
    unsigned slot;
    if (!result) return 0;
    if (stock_view_gate_mode == 3) {
        stock_view_gate_observe(2, controller);
        *result = original;
        return 1;
    }
    if (!stock_view_gate_owner(controller, &slot)) return 0;
    if (!slot) { *result = original; return 1; }
    player_stock_view view;
    if (!player_stock_view_read(&stock_view_gate_state, stats.issued, slot,
                                stock_view_gate_state.saved[slot - 1], &view)) return 0;
    *result = player_stock_view_fov_factor(&view);
    return 1;
}

/* The ship site has an actual ship argument. Unknown ship pointers retain the
 * stock branch; known players require the same authenticated worker generation
 * as the controller routes. This is not a lifetime or reader-exclusion proof. */
static int stock_view_gate_ship_policy(uintptr_t ship, int original, int *result) {
    if (!result) return 0;
    unsigned index = ship == stock_view_gate_ships[0] ? 0u :
                     ship == stock_view_gate_ships[1] ? 1u : 2u;
    if (index == 2) {
        *result = original;
        return 1;
    }
    unsigned slot = 0;
    if (!stock_view_gate_owner(stock_view_gate_controllers[index], &slot) ||
        slot != index + 1 || factory_probe_identity.ship != stock_view_gate_ships[0] ||
        lifecycle_discard_ship != stock_view_gate_ships[1]) return 0;
    player_stock_view view;
    if (!player_stock_view_read(&stock_view_gate_state, stats.issued, slot,
                                stock_view_gate_state.saved[index], &view)) return 0;
    *result = view.cockpit ? 0 : 1;
    return 1;
}

#include "gate_player_stock_view_component_policy.h"

typedef struct {
    unsigned enabled, initial, component_enabled, xs_enabled;
    uint32_t tid;
    uintptr_t controllers[2], ships[2];
    uint64_t previous_generation;
    player_stock_view_state next;
    player_view_query_api sealed;
    player_stock_view_fov_api provider;
    player_stock_view_ship_api ship_provider;
    player_stock_view_component_api component_provider;
    player_stock_view_precompare_api precompare_provider;
    player_view_xs_preload_api xs_provider;
} player_stock_view_transaction;

/* Preparation performs all validation into caller-owned temporary storage.
 * A combined input coordinator can prepare cursor and VIEW before either is
 * published. The caller serializes prepare/commit at the worker boundary. */
static int player_stock_view_prepare_transaction(uint64_t generation, const uint64_t saved[2],
                                                  const stock_input_record records[2],
                                                  player_stock_view_transaction *transaction) {
    if (!transaction || !saved || !records) return 0;
    memset(transaction, 0, sizeof *transaction);
    const char *component_setting = getenv("X4_DEV_STOCK_VIEW_COMPONENT_POLICY");
    const char *xs_setting = getenv("X4_DEV_VIEW_XS");
    if (xs_setting && (strcmp(xs_setting, "1") || !component_setting)) return 0;
    if (component_setting && strcmp(component_setting, "1")) return 0;
    /* The XS transform path is not yet approved for external views. Check
     * both records before changing coordinator state or binding providers. */
    if (xs_setting) {
        for (unsigned i = 0; i < 2; ++i) {
            if ((records[i].flags & STOCK_INPUT_VIEW) && !(records[i].view & 1u)) return 0;
        }
    }
    if (!stock_view_gate_mode) {
        const char *setting = getenv("X4_DEV_STOCK_VIEW_FOV");
        const char *observe = getenv("X4_DEV_STOCK_VIEW_FOV_OBSERVE");
        if (!setting && !observe) {
            if (component_setting) return 0;
            stock_view_gate_mode = 1;
            return 1;
        }
        if ((setting && observe) || (setting && strcmp(setting, "1")) ||
            (observe && strcmp(observe, "1")) || player_recurring_physics != 3 ||
            !player_recurring_tail || player_startup()->version != 1) return 0;
        stock_view_gate_mode = observe ? 3 : 2;
    }
    if (stock_view_gate_mode == 1) return component_setting == NULL;
    if (component_setting && stock_view_gate_mode != 2) return 0;
    unsigned component_enabled = component_setting != NULL;
    unsigned xs_enabled = xs_setting != NULL;
    if (stock_view_gate_bound && (component_enabled != stock_view_gate_component_enabled ||
                                  xs_enabled != stock_view_gate_xs_enabled)) return 0;
    long tid = syscall(SYS_gettid);
    if (tid <= 0 || (unsigned long)tid > UINT32_MAX || !keyboard_probe_worker() ||
        !player_recurring_physics_entered() || generation != stats.issued ||
        generation != player_recurring_tail_token.generation || !player_physics_identity() ||
        !keyboard_probe_identity() || !factory_probe_identity.controller || !lifecycle_discard_controller ||
        factory_probe_identity.controller == lifecycle_discard_controller) return 0;
    if (stock_view_gate_bound && ((uint32_t)tid != stock_view_gate_tid ||
        factory_probe_identity.controller != stock_view_gate_controllers[0] ||
        lifecycle_discard_controller != stock_view_gate_controllers[1])) return 0;
    unsigned char first[STOCK_INPUT_MAX_BYTES], second[STOCK_INPUT_MAX_BYTES];
    unsigned first_length = stock_input_encode(&records[0], first, sizeof first);
    unsigned second_length = stock_input_encode(&records[1], second, sizeof second);
    transaction->next = stock_view_gate_state;
    if (!first_length || !second_length || !player_stock_view_stage(&transaction->next,
        generation, saved, first, first_length, second, second_length)) return 0;
    transaction->enabled = 1;
    transaction->initial = !stock_view_gate_bound;
    transaction->component_enabled = component_enabled;
    transaction->xs_enabled = xs_enabled;
    transaction->tid = (uint32_t)tid;
    transaction->controllers[0] = factory_probe_identity.controller;
    transaction->controllers[1] = lifecycle_discard_controller;
    transaction->ships[0] = factory_probe_identity.ship;
    transaction->ships[1] = lifecycle_discard_ship;
    if (stock_view_gate_mode == 2 && (!transaction->ships[0] || !transaction->ships[1] ||
        transaction->ships[0] == transaction->ships[1] ||
        (stock_view_gate_bound &&
         (transaction->ships[0] != stock_view_gate_ships[0] ||
          transaction->ships[1] != stock_view_gate_ships[1])))) return 0;
    transaction->previous_generation = stock_view_gate_state.generation;
    if (transaction->initial) {
        const char *sha = NULL; uint64_t size = 0;
        if (!view_query_expected_library(&sha, &size) ||
            !player_view_query_provider(sha, size, &transaction->sealed) ||
            !player_stock_view_fov_provider(&transaction->sealed, &transaction->provider) ||
            (stock_view_gate_mode == 2 && !player_stock_view_ship_provider(
                &transaction->sealed, &transaction->ship_provider)) ||
            (component_enabled && (!player_stock_view_component_provider(
                &transaction->sealed, &transaction->component_provider) ||
                !player_stock_view_precompare_provider(
                &transaction->sealed, &transaction->precompare_provider))) ||
            (xs_enabled && !player_view_xs_preload_provider(&transaction->sealed, &transaction->xs_provider)) ||
            (xs_enabled && !site_ok(&(lx_site){"xs-transform", 0x18a0090, 0x3e0,
                "8489683612cc942bd00c117405c4b2ba6e02307198a653f05b574d2e62b08ccd"}))) return 0;
    }
    return 1;
}

static int player_stock_view_commit_transaction(const player_stock_view_transaction *transaction) {
    if (!transaction) return 0;
    if (!transaction->enabled) return stock_view_gate_mode == 1;
    if (syscall(SYS_gettid) != (long)transaction->tid || !player_recurring_physics_entered() ||
        stats.issued != transaction->next.generation ||
        player_recurring_tail_token.generation != transaction->next.generation ||
        stock_view_gate_state.generation != transaction->previous_generation ||
        factory_probe_identity.controller != transaction->controllers[0] ||
        lifecycle_discard_controller != transaction->controllers[1] ||
        (stock_view_gate_mode == 2 &&
         (factory_probe_identity.ship != transaction->ships[0] ||
          lifecycle_discard_ship != transaction->ships[1])) ||
        transaction->initial != !stock_view_gate_bound ||
        (stock_view_gate_bound && (transaction->component_enabled != stock_view_gate_component_enabled ||
                                   transaction->xs_enabled != stock_view_gate_xs_enabled))) return 0;
    player_stock_view_state previous = stock_view_gate_state;
    stock_view_gate_state = transaction->next;
    if (transaction->initial) {
        stock_view_gate_tid = transaction->tid;
        stock_view_gate_controllers[0] = transaction->controllers[0];
        stock_view_gate_controllers[1] = transaction->controllers[1];
        stock_view_gate_ships[0] = transaction->ships[0];
        stock_view_gate_ships[1] = transaction->ships[1];
        stock_view_gate_sealed = transaction->sealed;
        stock_view_gate_provider = transaction->provider;
        if (!stock_view_gate_provider.bind(stock_view_gate_mode_policy, stock_view_gate_fov_policy)) {
            stock_view_gate_state = previous;
            stock_view_gate_tid = 0;
            stock_view_gate_controllers[0] = stock_view_gate_controllers[1] = 0;
            stock_view_gate_ships[0] = stock_view_gate_ships[1] = 0;
            return 0;
        }
        if (stock_view_gate_mode == 2) {
            stock_view_gate_ship_provider = transaction->ship_provider;
            /* A failed second bind leaves the first resident binding installed.
             * The coordinator must stop the disposable process on failure. */
            if (!stock_view_gate_ship_provider.bind(stock_view_gate_ship_policy)) return 0;
        }
        stock_view_gate_component_enabled = transaction->component_enabled;
        stock_view_gate_xs_enabled = transaction->xs_enabled;
        stock_view_gate_bound = 1;
        /* Publish immutable identities only after the first transaction has
         * committed. A failed resident bind must stop the coordinator. */
        if (transaction->component_enabled &&
            (!transaction->component_provider.bind(stock_view_component_gate_policy) ||
             !transaction->precompare_provider.bind(stock_view_precompare_gate_policy))) return 0;
        if (transaction->xs_enabled && !transaction->xs_provider.bind(
                player_view_xs_gate_target, player_view_xs_gate_external, player_view_xs_gate_distance)) return 0;
    }
    return 1;
}

static __attribute__((unused)) int player_stock_view_prepare_slots(void *context, uint64_t generation,
                                           const uint64_t saved[2],
                                           const stock_input_record records[2]) {
    (void)context;
    player_stock_view_transaction transaction;
    return player_stock_view_prepare_transaction(generation, saved, records, &transaction) &&
           player_stock_view_commit_transaction(&transaction);
}

/* Called at the final admitted worker tail. Snapshot counts are observations,
 * not a drained epoch: concurrent foreign calls may occur after the loads. */
static void player_stock_view_observer_close(uint64_t generation) {
    if (stock_view_gate_mode != 3) return;
    if (!stock_view_gate_bound || generation != stock_view_gate_state.generation ||
        syscall(SYS_gettid) != (long)stock_view_gate_tid) _exit(223);
    for (unsigned site = 0; site < 3; ++site) {
        for (unsigned slot = 0; slot < 3; ++slot) {
            uint64_t counts[4];
            for (unsigned phase = 0; phase < 4; ++phase)
                counts[phase] = atomic_load_explicit(&stock_view_gate_observations[site][slot][phase], memory_order_relaxed);
            dprintf(2, "X4 STOCK_VIEW_FOV_OBSERVE v=1 site=%u slot=%u owner_outside=%llu owner_parked=%llu owner_scoped=%llu foreign_lower=%llu\n",
                    site, slot + 1, (unsigned long long)counts[0], (unsigned long long)counts[1],
                    (unsigned long long)counts[2], (unsigned long long)counts[3]);
        }
    }
    dprintf(2, "X4 STOCK_VIEW_FOV_OBSERVE v=1 stage=close tick=%llu coverage=bounded foreign=lower_bound unpatched=0\n",
            (unsigned long long)generation);
}
#endif
