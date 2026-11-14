#ifndef X4_PLAYER_SELECTION_PARITY_H
#define X4_PLAYER_SELECTION_PARITY_H

/* Read-only comparison of Player selected targets and the process-local UI
 * soft target. These are deliberately different sources. No native resolver
 * is called: the stock resolver clears an invalid Player reference. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef enum {
    SELECTION_PARITY_ABSENT,
    SELECTION_PARITY_VALID,
    SELECTION_PARITY_UNREADABLE,
    SELECTION_PARITY_INVALID,
    SELECTION_PARITY_UNAVAILABLE,
    SELECTION_PARITY_DRIFT
} selection_parity_status;

typedef struct {
    selection_parity_status status;
    uintptr_t source, object;
    uint64_t raw, runtime, saved, portable;
    unsigned mapping_available;
} selection_parity_value;

typedef struct {
    void *context;
    int (*authenticate)(void *);
    int (*read)(void *, uintptr_t, void *, size_t);
    int (*map)(void *, uint64_t runtime, uint64_t *saved, uint64_t *portable);
    uintptr_t player1, player2, registry_global, ui_manager_global;
} selection_parity_ops;

typedef struct {
    selection_parity_value player1, player2, ui;
} selection_parity_snapshot;

static inline int selection_parity_value_equal(const selection_parity_value *a,
                                                const selection_parity_value *b) {
    return a->status == b->status && a->source == b->source && a->object == b->object &&
        a->raw == b->raw && a->runtime == b->runtime && a->saved == b->saved &&
        a->portable == b->portable && a->mapping_available == b->mapping_available;
}

static inline int selection_parity_read(const selection_parity_ops *ops, uintptr_t base,
                                        size_t offset, void *out, size_t size) {
    return base && offset <= UINTPTR_MAX - base && size <= UINTPTR_MAX - (base + offset) &&
        ops->read(ops->context, base + offset, out, size) == 1;
}

/* Registry layout is the pinned 0x14bcf00 / 0xa45060 implementation. All
 * object/registry reads are repeated; this detects drift, not ABA or a lock. */
static inline void selection_parity_resolve(const selection_parity_ops *ops,
                                            selection_parity_value *value) {
    if (!value->raw) {
        value->status = SELECTION_PARITY_ABSENT;
        return;
    }
    uint32_t low = (uint32_t)value->raw & 0x1ffffffu;
    if (!low) {
        value->status = SELECTION_PARITY_INVALID;
        return;
    }
    uint32_t index = low - 1, block_index = index >> 20;
    uint32_t group_index = (index & 0xfffffu) / 3u, slot = (index & 0xfffffu) % 3u;
    uintptr_t registry = 0, block = 0, registry_after = 0, block_after = 0;
    unsigned char group[32], after[32];
    uint64_t runtime_after = 0;
    value->status = SELECTION_PARITY_UNREADABLE;
    if (!selection_parity_read(ops, ops->registry_global, 0, &registry, 8)) return;
    if (!registry) { value->status = SELECTION_PARITY_UNAVAILABLE; return; }
    if (!selection_parity_read(ops, registry, 0x60u + (size_t)block_index * 8u, &block, 8)) return;
    if (!block) { value->status = SELECTION_PARITY_INVALID; return; }
    if (!selection_parity_read(ops, block, (size_t)group_index * 32u, group, sizeof group)) return;
    uint32_t flags = 0, generations = 0;
    memcpy(&value->object, group + slot * 8u, 8);
    memcpy(&flags, group + 28, 4);
    memcpy(&generations, group + 24u + (slot / 2u) * 4u, 4);
    unsigned nibble = (flags >> (16u + slot * 4u)) & 15u;
    unsigned generation = (generations >> ((slot & 1u) * 16u)) & 0xffffu;
    if (!value->object || (nibble & 5u) != 5u || generation != ((value->raw >> 25) & 0xffffu)) {
        value->status = SELECTION_PARITY_INVALID;
        return;
    }
    if (!selection_parity_read(ops, value->object, 8, &value->runtime, 8)) return;
    if (value->runtime != value->raw) { value->status = SELECTION_PARITY_INVALID; return; }
    value->mapping_available = ops->map(ops->context, value->runtime, &value->saved, &value->portable) == 1;
    if (!value->mapping_available) value->saved = value->portable = 0;
    if (!selection_parity_read(ops, block, (size_t)group_index * 32u, after, sizeof after) ||
        !selection_parity_read(ops, registry, 0x60u + (size_t)block_index * 8u, &block_after, 8) ||
        !selection_parity_read(ops, ops->registry_global, 0, &registry_after, 8) ||
        !selection_parity_read(ops, value->object, 8, &runtime_after, 8)) return;
    value->status = registry == registry_after && block == block_after &&
        value->runtime == runtime_after && !memcmp(group, after, sizeof group)
        ? SELECTION_PARITY_VALID : SELECTION_PARITY_DRIFT;
}

static inline selection_parity_value selection_parity_player(const selection_parity_ops *ops,
                                                              uintptr_t player) {
    selection_parity_value value = {.status = SELECTION_PARITY_UNREADABLE};
    uint64_t after = 0;
    if (!player || player > UINTPTR_MAX - 0x6dc0) return value;
    value.source = player + 0x6db8;
    if (!selection_parity_read(ops, value.source, 0, &value.raw, 8)) return value;
    selection_parity_resolve(ops, &value);
    if (!selection_parity_read(ops, value.source, 0, &after, 8)) value.status = SELECTION_PARITY_UNREADABLE;
    else if (after != value.raw) value.status = SELECTION_PARITY_DRIFT;
    return value;
}

static inline selection_parity_value selection_parity_ui(const selection_parity_ops *ops) {
    selection_parity_value value = {.status = SELECTION_PARITY_UNREADABLE};
    uintptr_t manager = 0, entry = 0, manager_after = 0, entry_after = 0;
    unsigned char active = 0, active_after = 0;
    uint64_t after = 0;
    if (!selection_parity_read(ops, ops->ui_manager_global, 0, &manager, 8)) return value;
    if (!manager) { value.status = SELECTION_PARITY_UNAVAILABLE; return value; }
    if (!selection_parity_read(ops, manager, 0x5c, &active, 1)) return value;
    if (active != 1) { value.status = SELECTION_PARITY_UNAVAILABLE; return value; }
    if (!selection_parity_read(ops, manager, 0x40, &entry, 8)) return value;
    if (entry) {
        if (entry > UINTPTR_MAX - 0x58) return value;
        value.source = entry + 0x50;
        if (!selection_parity_read(ops, value.source, 0, &value.raw, 8)) return value;
    }
    selection_parity_resolve(ops, &value);
    if (!selection_parity_read(ops, ops->ui_manager_global, 0, &manager_after, 8) ||
        !selection_parity_read(ops, manager, 0x40, &entry_after, 8) ||
        !selection_parity_read(ops, manager, 0x5c, &active_after, 1) ||
        (entry && !selection_parity_read(ops, value.source, 0, &after, 8))) {
        value.status = SELECTION_PARITY_UNREADABLE;
    } else if (manager != manager_after || entry != entry_after || active != active_after ||
               (entry && value.raw != after)) {
        value.status = SELECTION_PARITY_DRIFT;
    }
    return value;
}

/* Source failures are explicit values, not fabricated empty selections.
 * Authority failure leaves output unchanged and refuses the whole sample. */
static inline int player_selection_parity_collect(const selection_parity_ops *ops,
                                                  selection_parity_snapshot *out) {
    if (!ops || !out || !ops->authenticate || !ops->read || !ops->map ||
        !ops->player1 || !ops->player2 || ops->player1 == ops->player2 ||
        !ops->registry_global || !ops->ui_manager_global || ops->authenticate(ops->context) != 1) return 0;
    selection_parity_snapshot next;
    next.player1 = selection_parity_player(ops, ops->player1);
    next.player2 = selection_parity_player(ops, ops->player2);
    next.ui = selection_parity_ui(ops);
    /* Revisit the decisive sources after reading the other two, rather than
     * accepting a changed earlier source as part of one comparison epoch. */
    selection_parity_value after = selection_parity_player(ops, ops->player1);
    if (!selection_parity_value_equal(&next.player1, &after)) next.player1.status = SELECTION_PARITY_DRIFT;
    after = selection_parity_player(ops, ops->player2);
    if (!selection_parity_value_equal(&next.player2, &after)) next.player2.status = SELECTION_PARITY_DRIFT;
    after = selection_parity_ui(ops);
    if (!selection_parity_value_equal(&next.ui, &after)) next.ui.status = SELECTION_PARITY_DRIFT;
    if (ops->authenticate(ops->context) != 1) return 0;
    *out = next;
    return 1;
}

static inline const char *selection_parity_status_name(selection_parity_status status) {
    static const char *const names[] = {"absent", "valid", "unreadable", "invalid", "unavailable", "drift"};
    return (unsigned)status < sizeof names / sizeof names[0] ? names[status] : "invalid_status";
}
#endif
