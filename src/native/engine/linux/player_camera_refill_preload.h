#ifndef X4_PLAYER_CAMERA_REFILL_PRELOAD_H
#define X4_PLAYER_CAMERA_REFILL_PRELOAD_H
#include <stdint.h>

typedef int (*player_camera_refill_callback)(uintptr_t camera, uintptr_t output);
typedef struct {
    uint32_t version, installed;
    uintptr_t entry, stub, trampoline, dispatcher, original_slot, frame;
    uint64_t original_word, patched_word;
    uint32_t stub_size, trampoline_size, frame_size, page_size;
} player_camera_refill_info;
#endif
