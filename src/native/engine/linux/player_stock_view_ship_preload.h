#ifndef X4_PLAYER_STOCK_VIEW_SHIP_PRELOAD_H
#define X4_PLAYER_STOCK_VIEW_SHIP_PRELOAD_H
#include <stdint.h>
typedef int (*player_stock_view_ship_policy)(uintptr_t ship, int original, int *result);
typedef struct {
    uint32_t version, installed;
    uintptr_t page, bridge, dispatch, frame;
    uint64_t original, patched;
} player_stock_view_ship_info;
#endif
