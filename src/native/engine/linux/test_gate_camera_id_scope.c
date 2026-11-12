/* Exercise the real Linux route with a fake image, without running the suite. */
#define main gate_fixture_original_main
#include "test_gate_linux.c"
#undef main
#include <pthread.h>

static player_camera_id_token shared_token;
static void *foreign_thread(void *unused) {
    (void)unused;
    uint32_t shared_stack[] = {LXR(0x700000u)};
    assert(!player_camera_id_scope_active());
    assert(lxr_route_of(shared_stack, 1) == RID_ROUTE_SHARED);
    assert(!player_camera_id_scope_leave(&shared_token));
    return NULL;
}
int main(void) {
    synth_image();
    rid_mode = RID_SET;
    rid_sealed = rid_local_built = rid_installed = rid_assign_installed = created_installed = 1;
    rid_patched_word = rd64(LX(LX_ID_REMOVE_VA));
    rid_assign_patched_word = rd64(LX(LX_ID_ASSIGN_VA));
    InterlockedExchange(&rid_local_on, 1);
    InterlockedExchange(&rid_active, 1);
    uint32_t shared_stack[] = {LXR(0x700000u)}, view_stack[] = {LXR(0x1796d53u)};
    assert(player_camera_rid_ready());
    assert(lxr_route_of(shared_stack, 1) == RID_ROUTE_SHARED);
    assert(lxr_route_of(view_stack, 1) == RID_ROUTE_VIEW);
    /* The deferred camera event must use local IDs after the setter returns.
     * The common sphere allocator and neighboring calls remain shared. */
    uint32_t camera_query[] = {LXR(0xeedc83u), LXR(0xeeddfcu), LXR(0x136ffbdu)};
    assert(lxr_route_of(camera_query, 3) == RID_ROUTE_VIEW);
    assert(lxr_route_of(camera_query, 2) == RID_ROUTE_SHARED);
    camera_query[2]--;
    assert(lxr_route_of(camera_query, 3) == RID_ROUTE_SHARED);
    camera_query[2] += 2;
    assert(lxr_route_of(camera_query, 3) == RID_ROUTE_SHARED);
    assert(player_camera_rid_begin(&shared_token));
    assert(lxr_route_of(shared_stack, 1) == RID_ROUTE_UI);
    assert(lxr_route_of(view_stack, 1) == RID_ROUTE_UI);
    player_camera_id_token nested = {0}, stale = shared_token;
    assert(!player_camera_rid_begin(&nested));
    pthread_t worker;
    assert(!pthread_create(&worker, NULL, foreign_thread, NULL));
    assert(!pthread_join(worker, NULL));
    assert(player_camera_id_scope_active());
    assert(player_camera_rid_end(&shared_token));
    assert(!player_camera_id_scope_active());
    assert(lxr_route_of(shared_stack, 1) == RID_ROUTE_SHARED);
    assert(lxr_route_of(view_stack, 1) == RID_ROUTE_VIEW);
    assert(!player_camera_id_scope_leave(&stale));
    assert(player_camera_rid_begin(&shared_token));
    assert(!player_camera_rid_end(&stale));
    assert(player_camera_id_scope_active());
    rid_mode = RID_OBSERVE;
    assert(!player_camera_rid_end(&shared_token));
    assert(!player_camera_id_scope_active());
    assert(!player_camera_rid_begin(&shared_token));
    rid_mode = RID_SET;
    rid_installed = 0;
    assert(!player_camera_rid_begin(&shared_token));
    rid_installed = 1;
    rid_assign_installed = 0;
    assert(!player_camera_rid_begin(&shared_token));
    rid_assign_installed = 1;
    rid_local_built = 0;
    assert(!player_camera_rid_begin(&shared_token));
    rid_local_built = 1;
    rid_sealed = 0;
    assert(!player_camera_rid_begin(&shared_token));
    rid_sealed = 1;
    rid_patched_word ^= 1;
    assert(!player_camera_rid_begin(&shared_token));
    rid_patched_word ^= 1;
    return 0;
}
