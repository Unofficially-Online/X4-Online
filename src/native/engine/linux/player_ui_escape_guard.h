#ifndef X4_PLAYER_UI_ESCAPE_GUARD_H
#define X4_PLAYER_UI_ESCAPE_GUARD_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Resolve the next _longjmp before arming. Must succeed on the owner thread. */
int player_ui_escape_guard_prepare(void);
/* Only for the authenticated, armed callback. Never returns after an escape. */
void player_ui_escape_guard_call(void (*original)(void *), void *object,
                                 int (*enter)(uintptr_t), int (*leave)(uintptr_t));
#ifdef __cplusplus
}
#endif
#endif
