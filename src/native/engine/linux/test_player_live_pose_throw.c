#define PLAYER_VIEW_QUERY_TEST
#include "player_view_query_preload.c"
#include <assert.h>
extern uintptr_t live_pose_throw_original(void *, void *, unsigned, double);
void live_pose_test_throw(void) {
    static unsigned char output[64];
    live_pose_information.installed = 1;
    live_pose_original = live_pose_throw_original;
    atomic_store(&owner_tid, current_thread());
    ui_depth[0] = 1;
    assert(x4mp_live_pose_bind(current_thread(), 0x1234));
    (void)live_pose_dispatch((void *)0x1234, output, 1, 1.25);
    assert(0);
}
int live_pose_test_refuses_after_unwind(void) {
    player_live_pose_snapshot result;
    return live_pose_capture.started == 1 && live_pose_capture.returned == 0 &&
        !x4mp_live_pose_finish(&result);
}
