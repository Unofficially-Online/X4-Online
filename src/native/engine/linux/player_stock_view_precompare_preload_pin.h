#ifndef X4_PLAYER_STOCK_VIEW_PRECOMPARE_PRELOAD_PIN_H
#define X4_PLAYER_STOCK_VIEW_PRECOMPARE_PRELOAD_PIN_H
#include "player_stock_view_precompare_preload.h"
#include "player_view_ancestry_preload_pin.h"

typedef struct {
    int (*bind)(player_stock_view_precompare_policy);
    player_view_ancestry_api ancestry;
} player_stock_view_precompare_api;

/* Authentication delegates to the composite ancestry provider, which checks
 * the full reconstructed engine hash, both exact callwords, the getter, sealed
 * stub page, module and unwind information. This function never binds. */
static int player_stock_view_precompare_provider(const player_view_query_api *sealed,
                                                  player_stock_view_precompare_api *out) {
    if (!sealed || !out) return 0;
    memset(out, 0, sizeof *out);
    player_view_ancestry_api ancestry;
    Dl_info module;
    if (!player_view_ancestry_provider(sealed, &ancestry) ||
        !ancestry.information.pre_enabled ||
        !dladdr((void *)sealed->finish, &module) || !module.dli_fbase) return 0;
    void *bind = dlsym(RTLD_DEFAULT, "x4mp_stock_view_precompare_bind");
    if (!player_view_pin_same_module((uintptr_t)bind, module.dli_fbase, "r-xp")) return 0;
    out->bind = (int (*)(player_stock_view_precompare_policy))bind;
    out->ancestry = ancestry;
    return 1;
}
#endif
