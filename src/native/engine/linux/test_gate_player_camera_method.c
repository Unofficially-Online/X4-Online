#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "gate_player_camera_id_scope.h"
static int camera_scope_ready = 1, camera_scope_begin_ok = 1, camera_scope_end_ok = 1;
static int player_camera_rid_ready(void) { return camera_scope_ready && !player_camera_id_scope_active(); }
static int player_camera_rid_begin(player_camera_id_token *token) {
    return camera_scope_begin_ok && player_camera_rid_ready() && player_camera_id_scope_enter(token);
}
static int player_camera_rid_end(player_camera_id_token *token) {
    int closed = player_camera_id_scope_leave(token);
    return closed && camera_scope_end_ok;
}

typedef struct { int ignored; } lua_State;
typedef struct { const char *name; uintptr_t address; size_t size; const char *hash; } lx_site;
#include "input_context_census.h"
static struct { const char *exe; } gate_env;
static struct { int active, issued, slot; } stats = {1, 6, 2};
static struct { int independent_players; } config = {1};
static struct { struct { int independent_players; } config; } keyboard_probe_queue = {{1}};
static struct { uintptr_t owner; _Atomic int wrapper_depth; } sko_session = {0x1000, 0};
static struct { struct { uintptr_t target; uint64_t target_runtime; } identity; } player_target_hold_resident = {{0x1234, 42}};
static int boot_phase = 3, wire_clients = 2, keyboard_probe_requested = 1, player_recurring_physics = 3;
static int keyboard_probe_inflight, player_network_busy, player_physics_replay_busy, player_physics_upkeep_busy;
static _Atomic int player_physics_ready = 1, stock_observer_input_depth;
static void *player_routes_scope;
static int lifecycle_discard_log = 1, identity_ok = 1, pins_ok = 1, possible_result = 1, mismatch;
static int possible_calls, set_calls, reported, getter_calls, damage_identity, damage_bank, context_before_rows, context_after_rows, anchor_receipts;
static uintptr_t lifecycle_discard_ship = 0x1234, lifecycle_discard_controller = 0x5678;
static unsigned long GetCurrentThreadId(void) { return 77; }
static uint64_t rng_word = 0x55;
static unsigned char bank[16], idmap[96];
#define LX_FILE_SIZE 1
#define PCB_VALUE_BYTES 16
static int gate_file_sha256(const char *p, size_t n, unsigned char *d) { (void)p;(void)n;memset(d, 0, 32);return pins_ok; }
static void hex32(const unsigned char *d, char *s) { (void)d;strcpy(s,"113f73d74570084f6ce18a659b365009eccf579fb66dbabb5dad2b1718045f20"); }
static int site_ok(const lx_site *s) { (void)s;return pins_ok; }
static int accept_ui(void) { return 1; }
static int stock_observer_worker_boundary(void) { return 1; }
static unsigned char *gate_tls_self(void) { return (unsigned char *)&rng_word; }
static int gate_tls_worker(unsigned char *p) { return p != NULL; }
static uint64_t *gate_tls_rng(unsigned char *p) { (void)p; return &rng_word; }
static int player_physics_identity(void) { return identity_ok; }
static int player_seat_preflight_resolve(void *p, uint64_t id, uintptr_t *t, uint64_t *r) { (void)p;(void)id;*t=0x1234;*r=42;return 1; }
static uintptr_t lifecycle_discard_pointer(uintptr_t p) { (void)p;return 0x3d21748; }
static int read_local(const void *p, void *out, size_t n) { if ((uintptr_t)p==0x3daa860) { uintptr_t a=(uintptr_t)idmap;memcpy(out,&a,n); } else memcpy(out,p,n);return 1; }
static int player_seat_preflight_read(void *context, uintptr_t address, void *output, size_t length) { (void)context; (void)address; (void)output; (void)length; return 1; }
static int test_collect_entries(const input_context_census_operations *o, uintptr_t manager,
                                input_context_census_sample *s, input_context_census_entries *e) {
    (void)o; memset(s, 0, sizeof *s); memset(e, 0, sizeof *e);
    static const unsigned old_keys[] = {0,1,2,3,4,5,7,8,9,10,12,15,16};
    static const unsigned new_keys[] = {0,1,2,3,4,5,7,8,9,11,12,13,14,15,16};
    const unsigned *keys = set_calls ? new_keys : old_keys;
    e->count = set_calls ? 15 : 13;
    s->complete = s->tree_valid = 1; s->target_present = 7; s->node_count = e->count; s->manager = manager;
    for (unsigned i = 0; i < e->count; ++i) {
        unsigned context = keys[i], requester = context == 13 || context == 14 ? 20 : 6;
        e->payloads[i][0] = (unsigned char)context; e->payloads[i][4] = (unsigned char)requester;
        e->payloads[i][8] = requester == 20 ? 5 : 0; e->payloads[i][12] = requester == 20 ? 0 : 1;
        for (unsigned j = 0; j < 3; ++j) if (context == (unsigned[]){8,15,16}[j])
            memcpy(s->payloads[j], e->payloads[i], 16);
    }
    return 1;
}
#define input_context_census_collect_entries test_collect_entries
static void lifecycle_routes_read_bank(unsigned char *p) { memcpy(p,bank,sizeof bank); }
static void lockstep_sha256(const void *p,size_t n,unsigned char *d) { (void)p;(void)n;memset(d,0,32); }
static void lifecycle_discard_write(const char *s) {
    if (strstr(s, "CONTEXT_ANCHOR v=1 tick=6 owner=1000 manager=1010 target=1234 controller=5678 digest=")) ++anchor_receipts;
    if (strstr(s, "CAMERA_CONTEXT v=2 stage=before")) ++context_before_rows;
    if (strstr(s, "CAMERA_CONTEXT v=2 stage=after")) ++context_after_rows;
}
static void lifecycle_discard_require(int ok, const char *why) { (void)why; if (!ok) _exit(223); }
static _Noreturn void lifecycle_discard_exit(int passed,const char *s) { (void)passed;(void)s;_exit(223); }
static void p_lua_pushnumber(lua_State *s,int v) { (void)s;reported=v; }
static uint64_t current(void) { ++getter_calls; return set_calls && !mismatch ? 42 : 7; }
static unsigned char possible(uint64_t id,unsigned char force) { assert(!player_camera_id_scope_active());assert(id==42 && force==1);++possible_calls;rng_word=0x66;return possible_result; }
static void set(uint64_t id,unsigned char force) { assert(player_camera_id_scope_active());assert(id==42 && force==1);++set_calls;if (damage_bank) bank[0]=9;if (damage_identity) identity_ok=0;rng_word=0x77; }
static void *mock_lx(uintptr_t address) { if (address==0xc030e0) return (void *)possible; if(address==0xc03b30) return (void *)set; if(address==0xa41a20) return (void *)current; return (void *)address; }
#define LX(address) mock_lx(address)
static void player_context_adapter_camera_rebase(const input_context_census_sample *before,
        const input_context_census_entries *old, const input_context_census_sample *after,
        const input_context_census_entries *current) {
    assert(anchor_receipts == 1 && before->node_count == 13 && old->count == 13);
    assert(after->node_count == 15 && current->count == 15);
    assert(before->manager == after->manager && stats.issued == 6);
}
#include "gate_player_camera_probe.h"

static void reset(void) { memset(&player_camera_anchor, 0, sizeof player_camera_anchor); player_camera_probe_attempted=0; possible_calls=set_calls=getter_calls=reported=context_before_rows=context_after_rows=anchor_receipts=0; rng_word=0x55; }
int main(void) {
    lua_State state = {0};
    (void)config;
    reset(); assert(lua_player_external_camera_probe(&state)==1); assert(reported && possible_calls==1 && set_calls==1 && getter_calls==2 && rng_word==0x55 && !bank[0] && context_before_rows==13 && context_after_rows==15 && anchor_receipts==1 && atomic_load(&player_camera_anchor.active)==1);
    assert(lua_player_external_camera_probe(&state)==1 && !reported && set_calls==1);
    reset(); possible_result=0; assert(lua_player_external_camera_probe(&state)==1 && !reported && possible_calls==1 && !set_calls && rng_word==0x55 && anchor_receipts==0);
    reset(); possible_result=1; mismatch=1; assert(lua_player_external_camera_probe(&state)==1 && !reported && set_calls==1 && rng_word==0x55);
    reset(); mismatch=0; camera_scope_ready=0;
    assert(lua_player_external_camera_probe(&state)==1 && !reported && !possible_calls && !set_calls && rng_word==0x55);
    reset(); camera_scope_ready=1; camera_scope_begin_ok=0;
    assert(lua_player_external_camera_probe(&state)==1 && !reported && possible_calls==1 && !set_calls && rng_word==0x55);
    reset(); camera_scope_begin_ok=1; camera_scope_end_ok=0;
    pid_t scope_child = fork();
    assert(scope_child >= 0);
    if (!scope_child) { lua_player_external_camera_probe(&state); _exit(0); }
    int scope_status = 0;
    assert(waitpid(scope_child, &scope_status, 0) == scope_child && WIFEXITED(scope_status) && WEXITSTATUS(scope_status)==223);
    camera_scope_end_ok=1;
    reset(); damage_bank=1;
    pid_t bad_bank_child = fork();
    assert(bad_bank_child >= 0);
    if (!bad_bank_child) { lua_player_external_camera_probe(&state); _exit(0); }
    int bad_bank_status = 0;
    assert(waitpid(bad_bank_child, &bad_bank_status, 0) == bad_bank_child && WIFEXITED(bad_bank_status) && WEXITSTATUS(bad_bank_status)==223);
    damage_bank=0;
    reset(); mismatch=0; pins_ok=0; assert(lua_player_external_camera_probe(&state)==1 && !reported && !possible_calls && !set_calls);
    reset(); pins_ok=1; stats.slot=1; assert(lua_player_external_camera_probe(&state)==1 && !reported && !possible_calls);
    reset(); stats.slot=2; stats.issued=7; assert(lua_player_external_camera_probe(&state)==1 && !reported && !possible_calls);
    reset(); stats.issued=6; player_routes_scope = &state;
    assert(lua_player_external_camera_probe(&state)==1 && !reported && !possible_calls);
    reset(); player_routes_scope = NULL; keyboard_probe_queue.config.independent_players = 0;
    assert(lua_player_external_camera_probe(&state)==1 && !reported && !possible_calls);
    keyboard_probe_queue.config.independent_players = 1;
    reset(); identity_ok = 0;
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        lua_player_external_camera_probe(&state);
        _exit(0);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child && (WIFEXITED(status) && WEXITSTATUS(status) == 223));
    reset(); identity_ok = 1; damage_identity = 1;
    child = fork();
    assert(child >= 0);
    if (!child) {
        lua_player_external_camera_probe(&state);
        _exit(0);
    }
    assert(waitpid(child, &status, 0) == child && (WIFEXITED(status) && WEXITSTATUS(status) == 223));
    return 0;
}
