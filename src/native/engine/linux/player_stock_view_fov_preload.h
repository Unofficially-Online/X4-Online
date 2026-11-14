#ifndef X4_PLAYER_STOCK_VIEW_FOV_PRELOAD_H
#define X4_PLAYER_STOCK_VIEW_FOV_PRELOAD_H
#include <stdint.h>

typedef int (*player_stock_view_mode_policy)(uintptr_t controller, int original, unsigned site, int *result);
typedef void (*player_stock_view_fov_observer)(uintptr_t controller, uintptr_t camera, float original);
typedef int (*player_stock_view_fov_policy)(uintptr_t controller, float original, float *result);

typedef struct {
    uint32_t version, installed;
    uintptr_t page, mode_bridge[2], fov_bridge, mode_dispatch, fov_dispatch;
    uintptr_t mode_frame[2], call_frame, original_fov_slot;
    uint64_t original[3], patched[3];
} player_stock_view_fov_info;
#endif
