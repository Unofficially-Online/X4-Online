#define _GNU_SOURCE
#include <assert.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "player_stock_view_capture_handoff.h"
#include "player_view_render_fov_observer.h"
#define X4_GATE_PLAYER_STOCK_VIEW_CONTROL_PROOF_H
#define X4_PLAYER_VIEW_RENDER_FOV_OBSERVER_PIN_H
enum { PLAYER_UI_OVERLAY_ACTIVE = 2 };
typedef struct { uintptr_t player1, player2, room; unsigned bank; } presentation_fork_request;
static struct { presentation_fork_request request; uintptr_t universe; } presentation_fork;
static struct { struct { uint64_t first_capture,last_capture; unsigned delay; } config; } keyboard_probe_queue;
static struct { unsigned slot, active; uint64_t issued; } stats;
static struct { uint64_t generation; } player_recurring_tail_token;
static struct { struct { long owner_tid; unsigned state; } lifecycle; } ui_overlay_probe;
static struct { unsigned version; uint64_t presentation_first; } startup;
static struct { unsigned frames, presentation_count; } player_session_budget;
static int ui_game_render_active, ui_camera_refill_active, ui_projection_scope_active;
static void *player_routes_scope;
static uintptr_t lifecycle_discard_controller;
static player_view_render_fov_observer bound;
typedef struct { int dummy; } player_view_query_api;
typedef struct { int (*bind)(player_view_render_fov_observer); } player_view_render_fov_api;
static int fake_bind(player_view_render_fov_observer cb) { if (bound) return 0; bound=cb; return 1; }
static int view_query_expected_library(const char **sha,uint64_t *size) {*sha="x";*size=1;return 1;}
static int player_view_query_provider(const char *sha,uint64_t size,player_view_query_api *out) {(void)sha;(void)size;(void)out;return 1;}
static int player_view_render_fov_provider(const player_view_query_api *s,player_view_render_fov_api *out) {(void)s;out->bind=fake_bind;return 1;}
static int player_cursor_mode(void) {return 1;}
static unsigned keyboard_probe_requested, keyboard_probe_closed, boot_phase, stock_observer_thread_failure;
static uint64_t stock_observer_worker_thread, ui_thread;
static uint64_t GetCurrentThreadId(void) {return (uint64_t)syscall(SYS_gettid);}
static void *gate_tls_self(void) {return (void *)1;}
static int gate_tls_worker(void *p) {return p != NULL;}
#define player_startup() (&startup)
static int player_recurring_physics_entered(void) {return 1;}
static int read_local(const void *p,void *out,size_t n) {memcpy(out,p,n);return 1;}
static int presentation_fork_owner(void *p, uintptr_t p1, uintptr_t p2, uintptr_t room, unsigned bank) {
    uintptr_t canonical = 0;
    (void)p; (void)room; (void)bank;
    memcpy(&canonical, (void *)(presentation_fork.universe + 0x238), sizeof canonical);
    return p1 == 0xbbbb && p2 == 0xdddd && canonical == p1;
}
static int stock_view_ui_lease_auth(void *context, const player_stock_view_capture_identity *id) {
    (void)context;
    return id && id->local_slot == 2 && !ui_game_render_active && !ui_camera_refill_active &&
        ui_overlay_probe.lifecycle.state == PLAYER_UI_OVERLAY_ACTIVE;
}
static int player_stock_view_ui_verified_capture_observed(const player_stock_view_capture_identity *id,
        uintptr_t camera, float fov, player_stock_view_capture_sample *out) {
    (void)id; (void)camera;
    *out = (player_stock_view_capture_sample){1, fov, 55};
    return 1;
}
#include "gate_player_view_render_capture.h"
static void *foreign_thread(void *unused) { (void)unused; bound(0xaaaa, 4.0f); return NULL; }
int main(void) {
    unsigned char universe[0x400]={0};uintptr_t camera=0xaaaa, player=0xbbbb;
    memcpy(universe+0x3e8,&camera,8);memcpy(universe+0x238,&player,8);
    presentation_fork.universe=(uintptr_t)universe;presentation_fork.request.player1=player;
    presentation_fork.request.player2=0xdddd;
    lifecycle_discard_controller=0xcccc; stats.slot=2; stats.active=1; boot_phase=3;
    stock_observer_worker_thread=ui_thread=GetCurrentThreadId();
    startup.version=1;startup.presentation_first=4;
    player_session_budget.frames=1;player_session_budget.presentation_count=8;
    keyboard_probe_queue.config.first_capture=3;keyboard_probe_queue.config.last_capture=11;keyboard_probe_queue.config.delay=2;
    ui_overlay_probe.lifecycle.owner_tid=syscall(SYS_gettid);ui_overlay_probe.lifecycle.state=PLAYER_UI_OVERLAY_ACTIVE;
    assert(!setenv("X4_DEV_RENDER_FOV_CAPTURE","1",1));assert(!setenv("X4_DEV_RENDER_FOV_OBSERVE","1",1));assert(!setenv("X4_DEV_UI_RENDER_LEASE","1",1));
    assert(!keyboard_probe_requested && player_view_render_capture_start() && bound);
    for (uint64_t t=3;t<10;++t) assert(player_view_render_capture_seal(t));
    stats.issued=player_recurring_tail_token.generation=9;ui_game_render_active=1;
    pthread_t thread; assert(!pthread_create(&thread, NULL, foreign_thread, NULL)); assert(!pthread_join(thread, NULL));
    bound(camera+8,1.25f); /* unrelated camera is ignored */
    bound(camera,1.25f);
    assert(atomic_load(&render_view_callbacks) == 3 && atomic_load(&render_view_owner_callbacks) == 2);
    assert(render_view_eligible_callbacks == 2 && render_view_published_callbacks == 1);
    ui_game_render_active = 0;
    ui_overlay_probe.lifecycle.state = 0;
    bound(camera, 9.0f); /* unleased canonical query must not replace the sample */
    assert(render_view_published_callbacks == 1);
    ui_overlay_probe.lifecycle.state = PLAYER_UI_OVERLAY_ACTIVE;
    bound(camera, 1.5f); /* the owned Move lease is a valid local projection source */
    assert(render_view_published_callbacks == 2);
    ui_camera_refill_active = 1;
    bound(camera, 8.0f);
    ui_camera_refill_active = 0;
    assert(render_view_published_callbacks == 2);
    ui_projection_scope_active = 1;
    bound(camera, 1.75f); /* The authenticated post-refill projection publishes locally. */
    ui_projection_scope_active = 0;
    assert(render_view_published_callbacks == 3);
    uintptr_t wrong_canonical = presentation_fork.request.player2;
    memcpy(universe + 0x238, &wrong_canonical, sizeof wrong_canonical);
    ui_projection_scope_active = 1;
    bound(camera, 2.0f); /* Remote identity is not the canonical singleton. */
    ui_projection_scope_active = 0;
    assert(render_view_published_callbacks == 3);
    memcpy(universe + 0x238, &player, sizeof player);
    stats.issued=player_recurring_tail_token.generation=10;ui_game_render_active=0;
    assert(player_view_render_capture_ui());
    assert(player_view_render_capture_seal(10) && render_view_sealed_present && render_view_sealed.fov==1.75f);
    stats.issued=player_recurring_tail_token.generation=11;
    assert(!player_view_render_capture_ui()); /* expired sample cannot be reused */
    assert(!player_view_render_capture_seal(11));
    return 0;
}
