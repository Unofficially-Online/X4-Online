#include <assert.h>
#include "player_view_render_fov_sample.h"
int main(void) {
    player_view_render_fov_sample s = {.mutex = PTHREAD_MUTEX_INITIALIZER}; float out = 9;
    assert(!player_view_render_fov_read(&s, 50, 1, 2, &out) && out == 9);
    assert(!player_view_render_fov_publish(&s, 49, 1, 2, NAN));
    assert(player_view_render_fov_publish(&s, 49, 1, 2, 1.25f));
    assert(player_view_render_fov_read(&s, 50, 1, 2, &out) && out == 1.25f);
    assert(!player_view_render_fov_read(&s, 51, 1, 2, &out));
    assert(!player_view_render_fov_read(&s, 48, 1, 2, &out));
    assert(!player_view_render_fov_read(&s, 50, 9, 2, &out));
    assert(!player_view_render_fov_read(&s, 50, 1, 9, &out));
    assert(!player_view_render_fov_publish(&s, 48, 1, 2, 2));
    assert(!pthread_mutex_destroy(&s.mutex)); return 0;
}
