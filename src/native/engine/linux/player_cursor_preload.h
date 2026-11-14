#ifndef X4_PLAYER_CURSOR_PRELOAD_H
#define X4_PLAYER_CURSOR_PRELOAD_H
#include <stdint.h>

typedef struct {
    uint32_t version, installed;
    uintptr_t site, stub, bridge, dispatcher, resume_slot, frame;
    unsigned char original[7], patch[7];
    uint32_t stub_size, bridge_size, frame_size, page_size;
} player_cursor_preload_info;
#endif
