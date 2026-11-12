#ifndef X4_GATE_PLAYER_CAMERA_ID_SCOPE_H
#define X4_GATE_PLAYER_CAMERA_ID_SCOPE_H

#include <stddef.h>
#include <stdint.h>

/* Only the synchronous camera setter's allocating thread owns this scope.
 * A stale, foreign or nested token cannot alter the current thread's scope. */
typedef struct { unsigned active; uint64_t generation; } player_camera_id_state;
typedef struct { player_camera_id_state *owner; uint64_t generation; } player_camera_id_token;
static _Thread_local player_camera_id_state player_camera_id_thread;

static inline int player_camera_id_scope_enter(player_camera_id_token *token) {
    player_camera_id_state *state = &player_camera_id_thread;
    if (!token || state->active || state->generation == UINT64_MAX) return 0;
    state->active = 1;
    token->owner = state;
    token->generation = ++state->generation;
    return 1;
}
static inline int player_camera_id_scope_leave(player_camera_id_token *token) {
    player_camera_id_state *state = &player_camera_id_thread;
    if (!token || !state->active || token->owner != state || token->generation != state->generation)
        return 0;
    state->active = 0;
    token->owner = NULL;
    token->generation = 0;
    return 1;
}
static inline int player_camera_id_scope_active(void) { return player_camera_id_thread.active == 1; }
#endif
