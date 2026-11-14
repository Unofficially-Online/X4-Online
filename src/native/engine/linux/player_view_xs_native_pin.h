#ifndef X4_PLAYER_VIEW_XS_NATIVE_PIN_H
#define X4_PLAYER_VIEW_XS_NATIVE_PIN_H

/* Read-only preflight for the exact Linux XS caller and callees. The embedding
 * gate must authenticate the whole executable image before invoking this; this
 * helper installs no patches and does not publish callable native pointers. */
#include <stddef.h>
#include <stdint.h>

typedef struct {
    void *context;
    int (*sealed_image)(void *);
    int (*site)(void *, uintptr_t, size_t, const char *);
    int (*read)(void *, uintptr_t, void *, size_t);
    uintptr_t (*address)(void *, uintptr_t);
} player_view_xs_pin_ops;

static inline int player_view_xs_native_preflight(const player_view_xs_pin_ops *ops) {
    if (!ops || !ops->sealed_image || !ops->site || !ops->read || !ops->address ||
        ops->sealed_image(ops->context) != 1) return 0;
    static const struct { uintptr_t address; size_t length; const char *sha256; } sites[] = {
        {0x129a680, 0x111, "e491a0bd3d0a73760d66ada4215e20b07b4af0dc6b894d232dfba2472bb9142b"},
        {0x1c90380, 0x51, "47a88f24f0a75dc1801d682abe6c5d83b1a6c52f2d125428bb1d8e17b83b4c00"},
        {0x1e07020, 0x33, "7fbd4e7bdb604524025c4a46cc791ae7137b7c1f19270213781e35e5b7890f51"},
        {0x1dccd30, 0x34, "6398d63d2afe8dbdfcc384682ea75da888787d96b67df91fba9519d42bec94d0"},
        {0x1c98690, 0x1f8, "598e8e3b893cd530a4c413082a2bcaee43c3bd0ec3e728b4afddb2ec97eaba4b"},
    };
    for (size_t i = 0; i < sizeof sites / sizeof sites[0]; ++i)
        if (!ops->site(ops->context, sites[i].address, sites[i].length, sites[i].sha256)) return 0;
    uintptr_t slot = 0;
    uint32_t threshold = 0;
    if (!ops->read(ops->context, ops->address(ops->context, 0x3d29e98), &slot, sizeof slot) ||
        slot != ops->address(ops->context, 0x129a680) ||
        !ops->read(ops->context, ops->address(ops->context, 0x2d25b20), &threshold, sizeof threshold) ||
        threshold != UINT32_C(0x4a742400)) return 0;
    return 1;
}
#endif
