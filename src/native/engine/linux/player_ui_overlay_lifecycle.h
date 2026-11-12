#ifndef X4_PLAYER_UI_OVERLAY_LIFECYCLE_H
#define X4_PLAYER_UI_OVERLAY_LIFECYCLE_H

#include <stdint.h>

/* Disposable one-shot experiment. The caller must terminate if fatal is called
 * or any operation returns false. An escaped callback is detected at the main
 * frame interlock; this helper does not provide C++ or longjmp cleanup. */
typedef struct {
    void *context;
    uint64_t (*current_tid)(void *context);
    int (*apply)(void *context);
    int (*restore)(void *context);
    void (*fatal)(void *context, const char *reason);
} player_ui_overlay_ops;

typedef enum {
    PLAYER_UI_OVERLAY_NEW,
    PLAYER_UI_OVERLAY_CHANGING,
    PLAYER_UI_OVERLAY_ACTIVE,
    PLAYER_UI_OVERLAY_PAUSED,
    PLAYER_UI_OVERLAY_CLOSED,
    PLAYER_UI_OVERLAY_FAILED
} player_ui_overlay_state;

typedef struct {
    player_ui_overlay_ops ops;
    uint64_t owner_tid;
    uintptr_t object;
    unsigned state;
} player_ui_overlay_lifecycle;

static inline int player_ui_overlay_fail(player_ui_overlay_lifecycle *scope, const char *reason) {
    __atomic_store_n(&scope->state, PLAYER_UI_OVERLAY_FAILED, __ATOMIC_RELEASE);
    scope->ops.fatal(scope->ops.context, reason);
    return 0;
}

/* Initialize before publishing the token to either thread. */
static inline int player_ui_overlay_init(player_ui_overlay_lifecycle *scope,
                                         const player_ui_overlay_ops *ops,
                                         uint64_t owner_tid, uintptr_t object) {
    if (!scope || !ops || !ops->current_tid || !ops->apply || !ops->restore ||
        !ops->fatal || !owner_tid || !object) return 0;
    scope->ops = *ops;
    scope->owner_tid = owner_tid;
    scope->object = object;
    __atomic_store_n(&scope->state, PLAYER_UI_OVERLAY_NEW, __ATOMIC_RELEASE);
    return 1;
}

static inline int player_ui_overlay_step(player_ui_overlay_lifecycle *scope, uintptr_t object,
                                         unsigned expected, unsigned next, int apply) {
    if (!scope) return 0;
    if (object != scope->object || scope->ops.current_tid(scope->ops.context) != scope->owner_tid)
        return player_ui_overlay_fail(scope, "owner_or_object");
    if (!__atomic_compare_exchange_n(&scope->state, &expected, PLAYER_UI_OVERLAY_CHANGING,
                                     0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return player_ui_overlay_fail(scope, "state");
    int ok = apply ? scope->ops.apply(scope->ops.context) : scope->ops.restore(scope->ops.context);
    if (!ok) return player_ui_overlay_fail(scope, apply ? "apply" : "restore");
    expected = PLAYER_UI_OVERLAY_CHANGING;
    if (!__atomic_compare_exchange_n(&scope->state, &expected, next, 0,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return player_ui_overlay_fail(scope, "interrupted");
    return 1;
}

static inline int player_ui_overlay_enter(player_ui_overlay_lifecycle *scope, uintptr_t object) {
    return player_ui_overlay_step(scope, object, PLAYER_UI_OVERLAY_NEW, PLAYER_UI_OVERLAY_ACTIVE, 1);
}
static inline int player_ui_overlay_pause(player_ui_overlay_lifecycle *scope, uintptr_t object) {
    return player_ui_overlay_step(scope, object, PLAYER_UI_OVERLAY_ACTIVE, PLAYER_UI_OVERLAY_PAUSED, 0);
}
static inline int player_ui_overlay_resume(player_ui_overlay_lifecycle *scope, uintptr_t object) {
    return player_ui_overlay_step(scope, object, PLAYER_UI_OVERLAY_PAUSED, PLAYER_UI_OVERLAY_ACTIVE, 1);
}
static inline int player_ui_overlay_return(player_ui_overlay_lifecycle *scope, uintptr_t object) {
    return player_ui_overlay_step(scope, object, PLAYER_UI_OVERLAY_ACTIVE, PLAYER_UI_OVERLAY_CLOSED, 0);
}

/* Called on the main thread before any next-frame simulation. The integration
 * must terminate on false, including if fatal unexpectedly returns. */
static inline int player_ui_overlay_frame_interlock(player_ui_overlay_lifecycle *scope) {
    if (!scope) return 0;
    unsigned state = __atomic_load_n(&scope->state, __ATOMIC_ACQUIRE);
    if (state == PLAYER_UI_OVERLAY_CLOSED) return 1;
    return player_ui_overlay_fail(scope, "abandoned_scope");
}
#endif
