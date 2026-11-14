#include <assert.h>
#include "player_stock_view_capture_handoff.h"
static int capture(void *context, const player_stock_view_capture_identity *id, uintptr_t camera,
                   float fov, player_stock_view_capture_sample *out) {
    unsigned *calls = context; ++*calls;
    assert(id->generation == 50 && camera == 10 && fov == 1.25f);
    *out = (player_stock_view_capture_sample){3, fov, 55}; return 1;
}
int main(void) {
    player_stock_view_mailbox box = {0};
    player_view_render_fov_sample render = {.mutex = PTHREAD_MUTEX_INITIALIZER};
    player_stock_view_capture_identity id = {50, 100, 200, 300, 2};
    assert(player_stock_view_mailbox_configure(&box, 49, 51, 50, 50, 2, 2));
    unsigned present = 0, calls = 0; player_stock_view_capture_sample out = {0};
    assert(player_stock_view_mailbox_seal(&box, 49, 2, &present, &out) && !present);
    assert(!player_stock_view_capture_handoff(&box, &render, &id, 10, capture, &calls) && !calls);
    assert(player_view_render_fov_publish(&render, 49, 10, 300, 1.25f));
    assert(!player_stock_view_capture_handoff(&box, &render, &id, 11, capture, &calls) && !calls);
    assert(player_stock_view_capture_handoff(&box, &render, &id, 10, capture, &calls));
    assert(player_stock_view_mailbox_seal(&box, 50, 2, &present, &out) && present && out.target == 55 && out.fov == 1.25f);
    assert(!player_stock_view_mailbox_seal(&box, 50, 2, &present, &out));
    assert(player_stock_view_mailbox_seal(&box, 51, 2, &present, &out) && !present);
    assert(!pthread_mutex_destroy(&render.mutex)); return 0;
}
