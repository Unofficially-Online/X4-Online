#ifndef X4_PLAYER_VIEW_RENDER_FOV_OBSERVER_H
#define X4_PLAYER_VIEW_RENDER_FOV_OBSERVER_H
#include <stdint.h>
/* The renderer site supplies camera but has no controller argument. The gate
 * must authenticate local presentation ownership before retaining a sample. */
typedef void (*player_view_render_fov_observer)(uintptr_t camera, float original);
typedef struct {
    uint32_t version, installed;
    uintptr_t page, dispatch, original_slot, frame;
    uint64_t original, patched;
} player_view_render_fov_info;
#endif
