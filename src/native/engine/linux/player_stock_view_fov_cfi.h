#ifndef X4_PLAYER_STOCK_VIEW_FOV_CFI_H
#define X4_PLAYER_STOCK_VIEW_FOV_CFI_H

#include <stdint.h>
#include <string.h>

/* Each stack-neutral near jump enters the middle of 0x16666b0, whose FDE at
 * both sites specifies CFA=rsp+112, r13/r12/rbp/rbx at CFA-16/-24/-32/-40.
 * The caller retains this frame and the registered page for process lifetime. */
#define PLAYER_STOCK_VIEW_FOV_FRAME_SIZE 56u
static inline void player_stock_view_fov_frame(unsigned char frame[PLAYER_STOCK_VIEW_FOV_FRAME_SIZE],
                                               uintptr_t address) {
    static const unsigned char cie[28] = {
        24,0,0,0, 0,0,0,0, 1,0,1,0x78,16,
        0x0c,7,112, 0x90,1, 0x8d,2, 0x8c,3, 0x86,4, 0x83,5, 0,0
    };
    const uint32_t fde_length = 20, cie_distance = 32;
    const uint64_t start = address, extent = 14;
    memset(frame, 0, PLAYER_STOCK_VIEW_FOV_FRAME_SIZE);
    memcpy(frame, cie, sizeof cie);
    memcpy(frame + 28, &fde_length, 4);
    memcpy(frame + 32, &cie_distance, 4);
    memcpy(frame + 36, &start, 8);
    memcpy(frame + 44, &extent, 8);
}
/* The ship mode site has the same saved registers but a 64-byte CFA. */
static inline void player_stock_view_ship_frame(unsigned char frame[PLAYER_STOCK_VIEW_FOV_FRAME_SIZE],
                                                uintptr_t address) {
    player_stock_view_fov_frame(frame, address);
    frame[15] = 64;
}
#endif
