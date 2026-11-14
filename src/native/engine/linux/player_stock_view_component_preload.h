#ifndef X4_PLAYER_STOCK_VIEW_COMPONENT_PRELOAD_H
#define X4_PLAYER_STOCK_VIEW_COMPONENT_PRELOAD_H
#include <stdint.h>
typedef int (*player_stock_view_component_policy)(unsigned site, uintptr_t component,
                                                  unsigned char original, unsigned char *result);
typedef struct {
    uint32_t version, installed;
    uintptr_t page, bridge[2], dispatch[2], original_slot[2], frame[2];
    uint64_t original[2], patched[2];
} player_stock_view_component_info;
#endif
