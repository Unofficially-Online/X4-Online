#ifndef X4_PLAYER_STOCK_VIEW_COMPONENT_POLICY_H
#define X4_PLAYER_STOCK_VIEW_COMPONENT_POLICY_H

/* Read-only component-to-ship attribution for a sealed two-slot VIEW epoch.
 * The caller supplies retained ship identities and authenticates the worker
 * before any owned-slot state is consumed. A non-owned direct parent is foreign
 * only if the embedding gate verifies its exact ship class and registry identity.
 * Failed, intermediate or changing identities fail closed. */
#include <stdint.h>
#include "player_stock_view_state.h"

typedef struct {
    void *context;
    int (*read)(void *context, uintptr_t address, void *out, unsigned size);
    int (*authenticate)(void *context, unsigned slot, uint64_t *generation);
    int (*foreign_identity)(void *context, uintptr_t component, uint64_t component_runtime,
                            uintptr_t parent);
    uintptr_t ships[2];
    uintptr_t classes[2];
    const player_stock_view_state *state;
} player_stock_view_component_context;

static inline int player_stock_view_component_resolve(
        const player_stock_view_component_context *context, unsigned site,
        uintptr_t component, unsigned *resolved_slot, uintptr_t *resolved_parent,
        player_stock_view *resolved_view) {
    if (!context || !context->read || !context->authenticate || !context->foreign_identity || !context->state ||
        !resolved_slot || !resolved_parent || !resolved_view || site > 1 || !component || component > UINTPTR_MAX - 0x78 ||
        !context->ships[0] || !context->ships[1] || context->ships[0] == context->ships[1] ||
        !context->classes[site]) return 0;
    struct component_identity { uintptr_t class_pointer; uint64_t runtime; } before, after;
    uintptr_t parent = 0, parent_after = 0;
    if (!context->read(context->context, component, &before, sizeof before) ||
        !context->read(context->context, component + 0x70, &parent, sizeof parent) ||
        !parent || parent == component) return 0;
    unsigned slot = parent == context->ships[0] ? 1u : parent == context->ships[1] ? 2u : 0u;
    if (before.class_pointer != context->classes[site] || !before.runtime) return 0;
    player_stock_view view = {0};
    uint64_t generation = 0;
    if (!slot && !context->foreign_identity(context->context, component, before.runtime, parent)) return 0;
    if (slot && (
        !context->authenticate(context->context, slot, &generation) ||
        !player_stock_view_read(context->state, generation, slot,
                                context->state->saved[slot - 1], &view) ||
        view.cockpit > 1 || view.floating > 1)) return 0;
    if (!context->read(context->context, component, &after, sizeof after) ||
        !context->read(context->context, component + 0x70, &parent_after, sizeof parent_after) ||
        before.class_pointer != after.class_pointer || before.runtime != after.runtime ||
        parent != parent_after) return 0;
    *resolved_slot = slot;
    *resolved_parent = parent;
    *resolved_view = view;
    return 1;
}

static inline int player_stock_view_component_route(
        const player_stock_view_component_context *context, unsigned site,
        uintptr_t component, unsigned char original, unsigned char *result) {
    unsigned slot = 0;
    uintptr_t parent = 0;
    player_stock_view view = {0};
    if (!result || !player_stock_view_component_resolve(context, site, component,
                                                         &slot, &parent, &view)) return 0;
    /* 0x1d6b1c0 is the floating predicate; 0x1e07020 is external view. */
    *result = slot ? (unsigned char)(site == 0 ? view.floating : !view.cockpit) : original;
    return 1;
}

/* The pinned instruction compares the getter's full pointer to RBX, the
 * immediate engine parent. Only an authenticated owned parent may be selected.
 * Foreign engines retain the exact original getter result. */
static inline int player_stock_view_precompare_route(
        const player_stock_view_component_context *context, uintptr_t component,
        uintptr_t ancestor, uintptr_t original, uintptr_t *result) {
    unsigned slot = 0;
    uintptr_t parent = 0;
    player_stock_view view = {0};
    if (!result || !player_stock_view_component_resolve(context, 1, component,
                                                         &slot, &parent, &view) ||
        ancestor != parent) return 0;
    if (!slot) {
        *result = original;
        return 1;
    }
    if (original != context->ships[0] && original != context->ships[1]) return 0;
    *result = ancestor;
    return 1;
}
#endif
