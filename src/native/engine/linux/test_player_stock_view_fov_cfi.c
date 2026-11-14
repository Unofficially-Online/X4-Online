#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <stdint.h>
#include <string.h>
#include "player_stock_view_fov_cfi.h"

typedef struct { void *tbase, *dbase, *function; } unwind_bases;
int main(void) {
    static unsigned char frame[PLAYER_STOCK_VIEW_FOV_FRAME_SIZE] __attribute__((aligned(8)));
    const uintptr_t start = 0x50000000;
    player_stock_view_fov_frame(frame, start);
    void *library = dlopen("libgcc_s.so.1", RTLD_NOW | RTLD_LOCAL);
    assert(library);
    void (*register_frame)(void *) = dlsym(library, "__register_frame");
    void (*deregister_frame)(void *) = dlsym(library, "__deregister_frame");
    void *(*find)(void *, unwind_bases *) = dlsym(library, "_Unwind_Find_FDE");
    assert(register_frame && deregister_frame && find);
    register_frame(frame);
    unwind_bases bases = {0};
    assert(find((void *)start, &bases) == frame + 28 && bases.function == (void *)start);
    assert(find((void *)(start + 14), &bases) == NULL);
    deregister_frame(frame);
    player_stock_view_ship_frame(frame, start);
    assert(frame[15] == 64);
    register_frame(frame);
    memset(&bases, 0, sizeof bases);
    assert(find((void *)start, &bases) == frame + 28 && bases.function == (void *)start);
    deregister_frame(frame);
    dlclose(library);
    return 0;
}
