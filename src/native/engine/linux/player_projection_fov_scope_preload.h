#ifndef X4_PLAYER_PROJECTION_FOV_SCOPE_PRELOAD_H
#define X4_PLAYER_PROJECTION_FOV_SCOPE_PRELOAD_H
#include <stdint.h>

typedef int (*player_projection_fov_scope_callback)(uintptr_t camera);
typedef struct {
    uint32_t version, installed;
    uintptr_t site, stub, dispatcher, original_slot, frame;
    uint64_t original_word, patched_word;
    uint32_t stub_size, frame_size, page_size;
} player_projection_fov_scope_info;
#endif
