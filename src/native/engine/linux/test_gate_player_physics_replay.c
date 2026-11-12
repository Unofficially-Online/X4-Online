#define _GNU_SOURCE
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define FWRR_FIRST_GENERATION UINT64_C(5)
#define PCB_VALUE_BYTES 16
static _Atomic unsigned player_physics_ready = 1, stock_observer_input_depth;
static struct { _Atomic unsigned wrapper_depth; uintptr_t owner; } sko_session;
static struct { uint64_t issued; } stats;
static struct { uint64_t generation; } player_recurring_tail_token;
static struct { uintptr_t player2; } factory_probe_state;
static uintptr_t factory_probe_player1 = 1, player_physics_zone = 99;
static unsigned player_physics_upkeep_busy, player_physics_upkeep_calls = 1;
static unsigned char player_physics_bank[PCB_VALUE_BYTES], live_bank[PCB_VALUE_BYTES];
static unsigned char keys[4], flight=1, enabled=1, ignored;
static uint64_t autopilot;
static float digital;
static int scoped, singleton=1, update_count, last_update, motion_count, body_count, snapshots;
static int context_changes, bad_zone, in_progress;
typedef struct { unsigned tree_valid, node_count, target_present; unsigned char payloads[3][16]; } input_context_census_sample;
typedef struct { void *context; int (*read)(void *, uintptr_t, void *, size_t); } input_context_census_operations;
typedef struct { int value; } player_route_snapshot;
typedef struct { struct { int unused; } routes; } route_compare_experiment;
static route_compare_experiment player_physics_routes;
static int player_seat_preflight_read(void *c, uintptr_t a, void *p, size_t n) { (void)c;(void)a;(void)p;(void)n;return 1; }
static int lifecycle_discard_context_collect(const input_context_census_operations *o, input_context_census_sample *out) {
    (void)o; memset(out, 0, sizeof *out); out->tree_valid=1;out->node_count=13+context_changes;out->target_present=7; return 1;
}
static void lifecycle_discard_require(int condition, const char *reason) { if (!condition) { dprintf(2, "%s\n",reason); _exit(222); } }
static int player_recurring_physics_entered(void) { return 1; }
static int player_physics_identity(void) { return singleton==1 && !scoped; }
static int player_physics_identity_for(unsigned s) { return s && singleton==2 && scoped; }
static int gate_tls_worker(void *t) { return t!=NULL; }
static void *gate_tls_self(void) { return (void*)1; }
static int lifecycle_input_pins(void) { return 1; }
static uintptr_t fake_current_zone(void) { return bad_zone ? 100 : 99; }
static void fake_state(int id, unsigned char state) { assert(singleton==2 && scoped && id>=2 && id<=3); keys[id-2]=state; }
static void *fake_lx(uintptr_t address) {
    switch(address) {
    case 0x3d8daa8: return &flight; case 0x3d8da60: return &enabled; case 0x3e5b29a: return &ignored;
    case 0x3d8da58: return &autopilot; case 0x3d8da68: return keys; case 0x3d8d9ac: return &digital;
    case 0x1dd0270: return (void*)fake_current_zone; case 0x166c6f0: return (void*)fake_state;
    default: abort();
    }
}
#define LX(a) fake_lx(a)
static void lifecycle_discard_read(uintptr_t address, void *out, size_t size) { memcpy(out,(void*)address,size); }
static void lifecycle_routes_read_bank(unsigned char *p) { memcpy(p,live_bank,sizeof live_bank); }
static void lifecycle_routes_write_bank(const unsigned char *p) { memcpy(live_bank,p,sizeof live_bank); }
static void lifecycle_routes_assign_player(uintptr_t player) { singleton=(int)player; }
static void lifecycle_discard_snapshot(const char *name,int initial) { (void)name;(void)initial;assert(singleton==1&&!scoped); ++snapshots; }
static void player_physics_require_bank(const unsigned char *p) { assert(singleton==1&&!scoped&&!memcmp(p,live_bank,sizeof live_bank)); }
static int player_routes_enter(void *r) { (void)r; assert(singleton==2&&!scoped); scoped=1;return 1; }
static int player_routes_leave(void *r, player_route_snapshot s) { (void)r;(void)s;assert(scoped);scoped=0;return 1; }
static int route_compare_snapshot(void *r, player_route_snapshot *s) { (void)r;(void)s;return scoped; }
static void lifecycle_discard_update_once(int input) { assert(scoped&&singleton==2&&!in_progress); ++update_count;last_update=input; digital=keys[0]?-1:keys[1]?1:0;live_bank[0]++;if(context_changes==2) context_changes=1; }
static void lifecycle_discard_write(const char *text) { (void)text; }
static void player_physics_motion(const char *stage) { assert(!strcmp(stage,"before")&&player_physics_identity());++motion_count; }
static void player_physics_observe_body(const char *stage) { assert(!strcmp(stage,"before")&&player_physics_identity());++body_count; }
enum { PLAYER_CONTEXT_FRAME_BEGIN = 1 };
static int context_scope_open;
static void player_context_adapter_outside(uint64_t generation, int boundary) {
    assert(generation == stats.issued && boundary == PLAYER_CONTEXT_FRAME_BEGIN && !context_scope_open);
}
static void player_context_adapter_scope_begin(uint64_t generation) {
    assert(generation == stats.issued && player_physics_identity() && !context_scope_open);
    context_scope_open = 1;
}
static void player_context_adapter_scope_end(uint64_t generation) {
    assert(generation == stats.issued && player_physics_identity() && context_scope_open);
    context_scope_open = 0;
}
#include "gate_player_physics_replay.h"

static void run_success(void) {
    factory_probe_state.player2=2; memset(live_bank,0x5a,sizeof live_bank);
    for (unsigned g=6; g<=40; ++g) {
        stats.issued=g; player_recurring_tail_token.generation=g; player_physics_replay_begin(g);
        assert(update_count==(int)g-5 && !scoped && singleton==1 && !keys[0] && !keys[1]);
        assert(live_bank[0]==0x5a && player_physics_bank[0]==g-5);
        assert(last_update==(g<=14?1:g<=24?0:g<=34?2:0));
        assert(player_physics_upkeep_calls==0 && !context_scope_open);
        player_physics_upkeep_calls=1;
    }
    assert(motion_count==35 && body_count==35 && snapshots==70);
}
static void refuse_case(int which) {
    factory_probe_state.player2=2;stats.issued=6;player_recurring_tail_token.generation=6;
    if(which==1) keys[0]=1;
    if(which==2) ignored=1;
    if(which==3) player_physics_upkeep_calls=0;
    if(which==4) bad_zone=1;
    if(which==5) context_changes=2;
    if(which==6) stats.issued=7;
    player_physics_replay_begin(stats.issued); _exit(1);
}
int main(void) {
    for(int i=1;i<=6;i++){pid_t p=fork();assert(p>=0);if(!p)refuse_case(i);int status;assert(waitpid(p,&status,0)==p&&WIFEXITED(status)&&WEXITSTATUS(status)==222);}
    run_success();puts("player physics replay scripted scopes and refusal: ok");
}
