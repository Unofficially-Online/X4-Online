#ifndef X4_PLAYER_STOCK_VIEW_CAPTURE_HANDOFF_H
#define X4_PLAYER_STOCK_VIEW_CAPTURE_HANDOFF_H
#include "player_stock_view_mailbox.h"
#include "player_view_render_fov_sample.h"
/* The capture callback must validate its local identity and camera while
 * holding the appropriate presentation lease. The renderer sample is already
 * authenticated at publication; matching camera/controller and age are
 * checked again here before the callback is allowed to form the snapshot. */
typedef int (*player_stock_view_observed_capture_fn)(void *context,
        const player_stock_view_capture_identity *, uintptr_t camera, float fov,
        player_stock_view_capture_sample *out);
static inline int player_stock_view_capture_handoff(player_stock_view_mailbox *mailbox,
        player_view_render_fov_sample *render, const player_stock_view_capture_identity *identity,
        uintptr_t camera, player_stock_view_observed_capture_fn capture, void *context) {
    if (!mailbox || !render || !identity || !camera || !capture) return 0;
    float fov;
    player_stock_view_capture_sample value;
    if (!player_view_render_fov_read(render, identity->generation, camera, identity->controller, &fov) ||
        capture(context, identity, camera, fov, &value) != 1) return 0;
    return player_stock_view_mailbox_stage(mailbox, identity->generation, identity->local_slot, &value);
}
#endif
