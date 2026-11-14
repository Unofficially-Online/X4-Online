#ifndef X4_PLAYER_STOCK_VIEW_SHIP_PRELOAD_PIN_H
#define X4_PLAYER_STOCK_VIEW_SHIP_PRELOAD_PIN_H
#include "player_view_ancestry_preload_pin.h"

static int player_stock_view_ship_provider(const player_view_query_api *sealed,
                                           player_stock_view_ship_api *out) {
    const uint64_t entry_original = UINT64_C(0xfd89485554415541);
    uint64_t entry;
    if (!gate_read_local(LX(0x129bad0), &entry, sizeof entry)) return 0;
    if (entry == entry_original) return player_stock_view_ship_provider_core(sealed, out, NULL);
    player_view_ancestry_api ancestry;
    if (!player_view_ancestry_provider(sealed, &ancestry)) return 0;
    return player_stock_view_ship_provider_core(sealed, out, &ancestry.information);
}
#endif
