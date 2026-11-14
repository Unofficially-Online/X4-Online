#ifndef X4_PLAYER_STOCK_VIEW_UI_SELECTION_H
#define X4_PLAYER_STOCK_VIEW_UI_SELECTION_H
#include "player_stock_view_capture.h"

/* GetSofttarget2's 24-byte SysV sret result. The name pointer is intentionally
 * not dereferenced. A nonzero raw handle with an unresolved result is unknown,
 * never an empty target. The caller must hold and authenticate a local UI lease. */
typedef struct { uint64_t id; const char *name; uint32_t message; } player_stock_view_softtarget;
_Static_assert(sizeof(player_stock_view_softtarget) == 24, "Linux softtarget ABI");
typedef struct {
    void *context;
    int (*authenticate)(void *, const player_stock_view_capture_identity *);
    int (*read)(void *, uintptr_t, void *, size_t);
    int (*get)(void *, player_stock_view_softtarget *);
    uintptr_t manager_global;
} player_stock_view_ui_ops;
typedef struct {
    player_stock_view_ui_ops ops;
    player_stock_view_capture_identity identity;
    uintptr_t manager, entry;
    uint64_t handle, runtime;
    unsigned ready;
} player_stock_view_ui_selection;

static inline int player_stock_view_ui_read_at(const player_stock_view_ui_ops *ops, uintptr_t base,
                                                size_t offset, void *out, size_t size) {
    if (!base || offset > UINTPTR_MAX - base || size > UINTPTR_MAX - (base + offset)) return 0;
    return ops->read(ops->context, base + offset, out, size) == 1;
}
static inline int player_stock_view_ui_snapshot(const player_stock_view_ui_ops *ops,
                                                 uintptr_t *manager, uintptr_t *entry, uint64_t *handle) {
    unsigned char active = 0;
    *manager = 0; *entry = 0; *handle = 0;
    return player_stock_view_ui_read_at(ops, ops->manager_global, 0, manager, sizeof *manager) && *manager &&
        player_stock_view_ui_read_at(ops, *manager, 0x5c, &active, sizeof active) && active == 1 &&
        player_stock_view_ui_read_at(ops, *manager, 0x40, entry, sizeof *entry) &&
        (!*entry || player_stock_view_ui_read_at(ops, *entry, 0x50, handle, sizeof *handle));
}
static inline int player_stock_view_ui_unchanged(const player_stock_view_ui_selection *selection) {
    uintptr_t manager = 0, entry = 0; uint64_t handle = 0;
    return selection && selection->ready &&
        selection->ops.authenticate(selection->ops.context, &selection->identity) == 1 &&
        player_stock_view_ui_snapshot(&selection->ops, &manager, &entry, &handle) &&
        manager == selection->manager && entry == selection->entry && handle == selection->handle &&
        selection->ops.authenticate(selection->ops.context, &selection->identity) == 1;
}
static inline int player_stock_view_ui_select(player_stock_view_ui_selection *out,
        const player_stock_view_ui_ops *ops, const player_stock_view_capture_identity *identity) {
    if (!out || !ops || !identity || !identity->generation || identity->local_slot < 1 ||
        identity->local_slot > 2 || !ops->authenticate || !ops->read || !ops->get || !ops->manager_global ||
        ops->authenticate(ops->context, identity) != 1) return 0;
    player_stock_view_ui_selection next = {.ops = *ops, .identity = *identity};
    if (!player_stock_view_ui_snapshot(ops, &next.manager, &next.entry, &next.handle)) return 0;
    player_stock_view_softtarget result = {0};
    if (ops->get(ops->context, &result) != 1 ||
        (next.handle && !result.id) || (!next.handle && result.id)) return 0;
    next.runtime = result.id; next.ready = 1;
    if (!player_stock_view_ui_unchanged(&next)) return 0;
    *out = next;
    return 1;
}
/* Re-run the pinned read-only registry lookup at the end of the complete
 * snapshot. An unchanged handle alone does not prove its object is still live. */
static inline int player_stock_view_ui_revalidate(const player_stock_view_ui_selection *selection) {
    player_stock_view_softtarget result = {0};
    return player_stock_view_ui_unchanged(selection) &&
        selection->ops.get(selection->ops.context, &result) == 1 &&
        result.id == selection->runtime && player_stock_view_ui_unchanged(selection);
}
static inline int player_stock_view_ui_target(void *context, const player_stock_view_capture_identity *identity,
                                               uint64_t *runtime) {
    const player_stock_view_ui_selection *selection = context;
    if (!identity || !runtime || !player_stock_view_ui_unchanged(selection) ||
        identity->generation != selection->identity.generation || identity->universe != selection->identity.universe ||
        identity->player != selection->identity.player || identity->controller != selection->identity.controller ||
        identity->local_slot != selection->identity.local_slot) return 0;
    *runtime = selection->runtime;
    return 1;
}
#endif
