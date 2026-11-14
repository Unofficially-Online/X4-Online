#ifndef X4_PLAYER_VIEW_XS_PRELOAD_H
#define X4_PLAYER_VIEW_XS_PRELOAD_H
#include <stdint.h>

/* Target policy returns 1 for a rewritten output, 2 for stock, 0 on error. */
typedef int (*player_view_xs_target_policy)(uintptr_t, uintptr_t, uintptr_t *);
typedef int (*player_view_xs_external_policy)(uint64_t, uint64_t *);
typedef int (*player_view_xs_distance_policy)(uintptr_t, uintptr_t, unsigned, float, float *);
typedef struct {
    uint32_t version;
    uint32_t installed;
    uintptr_t page;
    uintptr_t bridge[3];
    uintptr_t original_slot[3];
    uintptr_t frame[3];
    unsigned char original[3][5];
    unsigned char patched[3][5];
} player_view_xs_preload_info;
#endif
