#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/wait.h>

typedef struct { const char *name; uintptr_t va; unsigned length; const char *sha256; } lx_site;
static unsigned sdl_calls, pin_calls;
static int main_thread = 1, pin_valid = 1, fail_read;
static uintptr_t game_window = 0x123400, keyboard_window = 0x123400;
static uint32_t stored_id = 7, sdl_id = 7;
static int site_ok(const lx_site *site) {
    ++pin_calls;
    assert(site->va == 0x9da904 && site->length == 42);
    return pin_valid;
}
static void *fake_keyboard_focus(void) { ++sdl_calls; return (void *)keyboard_window; }
static uint32_t fake_window_id(void *window) {
    ++sdl_calls;
    assert((uintptr_t)window == game_window);
    return sdl_id;
}
static void *fake_dlopen(const char *name, int flags) {
    assert(!strcmp(name, "libSDL3.so.0") && flags == (RTLD_NOW | RTLD_NOLOAD));
    return (void *)0x1000;
}
static void *fake_dlsym(void *library, const char *name) {
    assert(library == (void *)0x1000);
    if (!strcmp(name, "SDL_GetKeyboardFocus")) return (void *)fake_keyboard_focus;
    assert(!strcmp(name, "SDL_GetWindowID"));
    return (void *)fake_window_id;
}
static int fake_dladdr(const void *address, Dl_info *info) {
    assert(address == (void *)fake_keyboard_focus || address == (void *)fake_window_id);
    info->dli_fbase = (void *)0x1000;
    return 1;
}
static int fake_dlclose(void *library) { assert(library == (void *)0x1000); return 0; }
static long fake_syscall(long number) { assert(number == SYS_gettid); return main_thread ? getpid() : getpid() + 1; }
static int gate_read_local(const void *source, void *dest, size_t size) {
    if (fail_read) return 0;
    if ((uintptr_t)source == 0x3daaa88) {
        assert(size == sizeof game_window);
        memcpy(dest, &game_window, size);
    } else {
        assert((uintptr_t)source == 0x3daaa90 && size == sizeof stored_id);
        memcpy(dest, &stored_id, size);
    }
    return 1;
}
#define LX(address) ((void *)(uintptr_t)(address))
#define dlopen fake_dlopen
#define dlsym fake_dlsym
#define dladdr fake_dladdr
#define dlclose fake_dlclose
#define syscall fake_syscall
#include "gate_player_stock_focus.h"

static void reset(void) {
    memset(&player_focus_state, 0, sizeof player_focus_state);
    player_focus_sdl_handle = NULL;
    player_focus_requested = -1;
    sdl_calls = pin_calls = 0;
    main_thread = pin_valid = 1;
    fail_read = 0;
    game_window = keyboard_window = 0x123400;
    stored_id = sdl_id = 7;
    assert(setenv("X4_DEV_PLAYER_FOCUS", "1", 1) == 0);
}

int main(void) {
    unsigned focused = 99;
    reset();
    assert(unsetenv("X4_DEV_PLAYER_FOCUS") == 0);
    assert(player_stock_focus_capture(1) && !sdl_calls && !pin_calls);
    assert(!player_stock_focus_read(1, &focused) && focused == 99);

    reset();
    assert(player_stock_focus_capture(1));
    assert(player_stock_focus_read(1, &focused) && focused == 1);
    assert(!player_stock_focus_read(2, &focused));
    keyboard_window = 0; /* Losing focus is valid neutral input. */
    assert(player_stock_focus_capture(2));
    assert(player_stock_focus_read(2, &focused) && focused == 0);
    keyboard_window = 0x567800; /* The loading window is not the game window. */
    assert(player_stock_focus_capture(3));
    assert(player_stock_focus_read(3, &focused) && focused == 0);
    assert(!player_stock_focus_read(2, &focused));
    assert(pin_calls == 1 && sdl_calls == 6);
    assert(!player_stock_focus_capture(3)); /* No duplicate generation. */
    assert(!player_stock_focus_read(3, &focused));
    assert(!player_stock_focus_capture(4)); /* A refusal cannot recover silently. */

    reset();
    main_thread = 0;
    assert(!player_stock_focus_capture(1) && !sdl_calls && !pin_calls);
    reset();
    pin_valid = 0;
    assert(!player_stock_focus_capture(1) && !sdl_calls);
    reset();
    fail_read = 1;
    assert(!player_stock_focus_capture(1) && !sdl_calls);
    reset();
    sdl_id = 8;
    assert(!player_stock_focus_capture(1));
    reset();
    assert(player_stock_focus_capture(1));
    game_window += 16;
    assert(!player_stock_focus_capture(2));
    reset();
    assert(!player_stock_focus_capture(2)); /* A missing first sample is not usable. */
    reset();
    assert(!player_stock_focus_capture(UINT64_MAX));
    pid_t child = fork();
    assert(child >= 0);
    if (!child) player_stock_focus_fatal();
    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 216);
    return 0;
}
