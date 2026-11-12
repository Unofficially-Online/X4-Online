#ifndef X4_GATE_PLAYER_STOCK_FOCUS_H
#define X4_GATE_PLAYER_STOCK_FOCUS_H

#include "player_stock_focus.h"
#include <errno.h>

typedef void *(*player_sdl_keyboard_focus_fn)(void);
typedef uint32_t (*player_sdl_window_id_fn)(void *);

static player_stock_focus_state player_focus_state;
static player_sdl_keyboard_focus_fn player_focus_query;
static player_sdl_window_id_fn player_focus_window_id;
static void *player_focus_sdl_handle;
static int player_focus_requested = -1;

static int player_stock_focus_wanted(void) {
    if (player_focus_requested < 0) {
        const char *value = getenv("X4_DEV_PLAYER_FOCUS");
        player_focus_requested = value && !strcmp(value, "1");
    }
    return player_focus_requested;
}

static int player_stock_focus_bind(void) {
    static const lx_site creation = {"SDL-main-window", 0x9da904, 42,
        "937b196d38f4ef2d0f02631a092d81020cd0b1a353d03cce7f0da18063ff821b"};
    if (!site_ok(&creation)) return 0;
    /* X4 already depends on this library. Never load a second SDL instance.
     * Keep this reference while workers can consume its published samples. */
    void *library = dlopen("libSDL3.so.0", RTLD_NOW | RTLD_NOLOAD);
    if (!library) return 0;
    void *query = dlsym(library, "SDL_GetKeyboardFocus");
    void *window_id = dlsym(library, "SDL_GetWindowID");
    Dl_info query_module = {0}, id_module = {0};
    if (!query || !window_id || !dladdr(query, &query_module) ||
        !dladdr(window_id, &id_module) || !query_module.dli_fbase ||
        query_module.dli_fbase != id_module.dli_fbase) {
        dlclose(library);
        return 0;
    }
    player_focus_query = (player_sdl_keyboard_focus_fn)query;
    player_focus_window_id = (player_sdl_window_id_fn)window_id;
    player_focus_sdl_handle = library;
    return 1;
}

/* Called immediately before the admitted FrameTick, after any pacing wait.
 * The sample is local presentation/input state, never authoritative world data. */
static int player_stock_focus_capture_inner(uint64_t generation) {
    if (!player_stock_focus_wanted()) return 1;
    if ((pid_t)syscall(SYS_gettid) != getpid() ||
        __atomic_load_n(&player_focus_state.failed, __ATOMIC_ACQUIRE)) {
        return player_stock_focus_refuse(&player_focus_state);
    }
    if (!player_focus_sdl_handle && !player_stock_focus_bind())
        return player_stock_focus_refuse(&player_focus_state);
    uintptr_t window = 0, checked_window = 0;
    uint32_t stored_id = 0, checked_id = 0;
    if (!gate_read_local(LX(0x3daaa88), &window, sizeof window) || !window ||
        !gate_read_local(LX(0x3daaa90), &stored_id, sizeof stored_id) || !stored_id)
        return player_stock_focus_refuse(&player_focus_state);
    uint32_t observed_id = player_focus_window_id((void *)window);
    uintptr_t focused_window = (uintptr_t)player_focus_query();
    if (!gate_read_local(LX(0x3daaa88), &checked_window, sizeof checked_window) ||
        !gate_read_local(LX(0x3daaa90), &checked_id, sizeof checked_id) ||
        checked_window != window || checked_id != stored_id)
        return player_stock_focus_refuse(&player_focus_state);
    return player_stock_focus_publish(&player_focus_state, generation, 1,
                                      window, stored_id, observed_id, focused_window);
}

static int player_stock_focus_capture(uint64_t generation) {
    int saved_errno = errno;
    int result = player_stock_focus_capture_inner(generation);
    errno = saved_errno;
    return result;
}

static _Noreturn void player_stock_focus_fatal(void) {
    static const char message[] = "X4 PLAYER FOCUS FATAL invalid main-window sample\n";
    (void)write(2, message, sizeof message - 1);
    /* Disposable input sessions must never resume an uncoordinated frame after
     * failing to capture its input. The runner owns external process cleanup. */
    _exit(216);
}

static __attribute__((unused)) int player_stock_focus_read(uint64_t generation,
                                                          unsigned *focused) {
    return player_stock_focus_state_read(&player_focus_state, generation, focused);
}

#endif
