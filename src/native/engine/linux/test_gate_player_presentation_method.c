#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "input_context_census.h"

typedef struct { int unused; } lua_State;
typedef struct { const char *name; uintptr_t address; size_t size; const char *hash; } lx_site;
static struct { const char *exe; } gate_env;
static struct { int active, issued, slot; } stats = {1,7,1};
static struct { struct { int independent_players; } config; } keyboard_probe_queue = {{1}};
static struct { uintptr_t owner; _Atomic int wrapper_depth; } sko_session = {0x1000,0};
static struct { struct { uintptr_t room; } identity; } player_target_hold_resident;
static struct { uintptr_t player2; unsigned player2_owned, player2_uncertain; } factory_probe_state;
static uintptr_t factory_probe_player1;
static int boot_phase=3, wire_clients=2, keyboard_probe_requested=1, player_recurring_physics=3;
static int player_network_busy, player_physics_replay_busy, player_physics_upkeep_busy, keyboard_probe_inflight;
static _Atomic int player_physics_ready=1, stock_observer_input_depth;
static void *player_routes_scope;
static int worker_ok=1, tls_ok=1, identity_ok=1, damage_bank, damage_context, damage_identity;
static int reported, calls, releases, census_calls, bad_hash, bad_plan_pin, damage_ui;
static unsigned char ui_manager[16], ui_presenter[0x70], ui_controller[16];
static uintptr_t ui_manager_pointer, ui_cockpit_slot, ui_firstperson_slot, ui_plan_slot;
static int lifecycle_discard_log = 1;
static unsigned char players[2][0x6b00], rooms[2][0x80], bank[16], tls;
static uint32_t view_bank;
static char log_text[4096];
#define PCB_VALUE_BYTES 16
#define LX_FILE_SIZE 1
#define PLAYER_BODY_IMAGE_SHA "113f73d74570084f6ce18a659b365009eccf579fb66dbabb5dad2b1718045f20"
#define SEAT_PREFLIGHT_CONTROL_ROOM_VTABLE 0x3cb4e88u
#define SEAT_PREFLIGHT_COCKPIT_VTABLE 0x3ca5fc0u
static int gate_file_sha256(const char *p,size_t n,unsigned char *d) { (void)p;(void)n;memset(d,0,32);return 1; }
static void hex32(const unsigned char *d,char *s) { (void)d;strcpy(s,bad_hash ? "bad" : PLAYER_BODY_IMAGE_SHA); }
static int site_ok(const lx_site *site) { return !bad_plan_pin || strcmp(site->name,"ui-plan-change"); }
static int view_query_occupied_original_pin(void) { return 0; }
static int player_seat_preflight_pins(void *context) { (void)context;return 1; }
static int accept_ui(void) { return 1; }
static int stock_observer_worker_boundary(void) { return worker_ok; }
static unsigned char *gate_tls_self(void) { return &tls; }
static int gate_tls_worker(unsigned char *p) { return p && tls_ok; }
static int player_physics_identity(void) { return identity_ok; }
static int read_local(const void *p,void *out,size_t n) { if ((uintptr_t)p < 0x100000) return 0; memcpy(out,p,n);return 1; }
static int player_seat_preflight_read(void *c,uintptr_t p,void *out,size_t n) { (void)c;(void)p;(void)out;(void)n;return 1; }
static int fixture_census(const input_context_census_operations *o, uintptr_t manager,
                          input_context_census_sample *sample, input_context_census_entries *entries) {
    (void)o; memset(sample,0,sizeof *sample);memset(entries,0,sizeof *entries);
    sample->manager=manager;sample->tree_valid=sample->complete=1;sample->node_count=1;
    entries->count=1; entries->payloads[0][0]=(unsigned char)(damage_context && census_calls > 0);++census_calls;return 1;
}
#define input_context_census_collect_entries fixture_census
static void lifecycle_routes_read_bank(unsigned char *p) { memcpy(p,bank,sizeof bank); }
static void lifecycle_discard_write(const char *s) { assert(strlen(log_text)+strlen(s)<sizeof log_text);strcat(log_text,s); }
static void lifecycle_discard_require(int ok,const char *reason) { (void)reason;assert(ok); }
static void p_lua_pushnumber(lua_State *state,int value) { (void)state;reported=value; }
static void *live_mock(void *p,int cls,unsigned char include) {
    assert(cls==0x19 && !include);++calls;return p==players[0]?rooms[0]:rooms[1];
}
static void **cached_mock(void **out,void *p,int cls,unsigned char include) {
    assert(cls==0x19 && !include);++calls;*out=p==players[0]?rooms[0]:rooms[1];++*(uint32_t *)((unsigned char *)*out+0x40);return out;
}
static void release_mock(void *p) { ++releases;--*(uint32_t *)((unsigned char *)p+8); }
static uint64_t occupied_mock(void) { ++calls;if(damage_bank) bank[0]=1;if(damage_identity) identity_ok=0;if(damage_ui) ui_presenter[0x6c]=1;return 123; }
static void *mock_lx(uintptr_t a) {
    if(a==0x3e77430) return &view_bank;
    if(a==0x3db6e48) return &ui_manager_pointer;
    if(a==0x3ab4610) return &ui_cockpit_slot;
    if(a==0x3ab4618) return &ui_firstperson_slot;
    if(a==0x3ab4070) return &ui_plan_slot;
    if(a==0x189d8f0) return (void *)live_mock;
    if(a==0x189f8d0) return (void *)cached_mock;
    if(a==0xa14540) return (void *)release_mock;
    if(a==0xa44e40) return (void *)occupied_mock;
    return (void *)a;
}
#define LX(a) mock_lx(a)
#include "gate_player_presentation_probe.h"

static void reset(void) {
    memset(players,0,sizeof players);memset(rooms,0,sizeof rooms);memset(bank,0,sizeof bank);log_text[0]=0;
    for (unsigned i=0;i<2;++i) {
        uintptr_t room=(uintptr_t)rooms[i], table=SEAT_PREFLIGHT_COCKPIT_VTABLE;
        memcpy(players[i]+0x70,&room,8);memcpy(players[i]+0xe0,&room,8);
        memcpy(rooms[i],&table,8);*(uint32_t *)(rooms[i]+0x40)=8;*(uint32_t *)(rooms[i]+0x44)=2;
    }
    memset(ui_manager,0,sizeof ui_manager);memset(ui_presenter,0,sizeof ui_presenter);memset(ui_controller,0,sizeof ui_controller);
    ui_manager_pointer=(uintptr_t)ui_manager;
    uintptr_t presenter=(uintptr_t)ui_presenter, controller=(uintptr_t)ui_controller;
    uintptr_t presenter_vtable=0x3ab4600, controller_vtable=0x3ab4060;
    memcpy(ui_manager+8,&presenter,8);memcpy(ui_presenter,&presenter_vtable,8);
    memcpy(ui_presenter+0x18,&controller,8);memcpy(ui_controller,&controller_vtable,8);
    ui_cockpit_slot=0xa78130;ui_firstperson_slot=0xa781a0;ui_plan_slot=0xa2b0a0;
    bad_hash=bad_plan_pin=damage_ui=0;
    factory_probe_player1=(uintptr_t)players[0];factory_probe_state.player2=(uintptr_t)players[1];
    factory_probe_state.player2_owned=1;factory_probe_state.player2_uncertain=0;
    player_target_hold_resident.identity.room=(uintptr_t)rooms[1];
    atomic_store(&presentation_probe_attempted,0);stats.issued=7;stats.slot=1;
    worker_ok=tls_ok=identity_ok=1;damage_bank=damage_context=damage_identity=0;
    calls=releases=census_calls=reported=0;
}
static void must_refuse(lua_State *state) {
    assert(lua_player_presentation_probe(state)==1 && !reported && calls==0 && releases==0 && census_calls==0);
    assert(strstr(log_text,"stage=result tick=7") && strstr(log_text,"success=0"));
}
int main(void) {
    lua_State state={0};
    reset();stats.issued=6;must_refuse(&state);
    reset();stats.slot=3;must_refuse(&state);
    reset();worker_ok=0;must_refuse(&state);
    reset();tls_ok=0;must_refuse(&state);
    reset();player_physics_upkeep_busy=1;must_refuse(&state);player_physics_upkeep_busy=0;
    reset();player_routes_scope=&state;must_refuse(&state);player_routes_scope=NULL;
    reset();assert(lua_player_presentation_probe(&state)==1 && reported && calls==5 && releases==2);
    char *p1=strstr(log_text,"stage=p1"), *p2=strstr(log_text,"stage=p2");
    char *plan=strstr(log_text,"stage=ui_plan"), *occupied=strstr(log_text,"stage=occupied"), *result=strstr(log_text,"stage=result");
    assert(p1 && p2 && plan && occupied && result && p1<p2 && p2<plan && plan<occupied && occupied<result);
    assert(strstr(plan,"flag=0 authenticated=1"));
    assert(strstr(result,"success=1") && strstr(occupied,"observational=1"));
    int prior_calls=calls;assert(lua_player_presentation_probe(&state)==1 && !reported && calls==prior_calls);
    reset();stats.slot=2;assert(lua_player_presentation_probe(&state)==1 && reported && calls==5 && releases==2);
    reset();damage_bank=1;assert(lua_player_presentation_probe(&state)==1 && !reported && releases==2);
    reset();damage_context=1;assert(lua_player_presentation_probe(&state)==1 && !reported && releases==2);
    reset();damage_identity=1;assert(lua_player_presentation_probe(&state)==1 && !reported && releases==2);
    reset();bad_hash=1;must_refuse(&state);
    reset();bad_plan_pin=1;must_refuse(&state);
    reset();ui_manager_pointer=0;assert(lua_player_presentation_probe(&state)==1 && !reported && releases==2);
    reset();ui_manager_pointer=0x10000;assert(lua_player_presentation_probe(&state)==1 && !reported && releases==2);
    reset();ui_presenter[0]^=1;assert(lua_player_presentation_probe(&state)==1 && !reported && releases==2);
    reset();ui_controller[0]^=1;assert(lua_player_presentation_probe(&state)==1 && !reported && releases==2);
    reset();ui_presenter[0x6c]=2;assert(lua_player_presentation_probe(&state)==1 && !reported && releases==2);
    reset();ui_plan_slot^=1;assert(lua_player_presentation_probe(&state)==1 && !reported && releases==2);
    reset();damage_ui=1;assert(lua_player_presentation_probe(&state)==1 && !reported && releases==2);
    reset();ui_presenter[0x6c]=1;assert(lua_player_presentation_probe(&state)==1 && reported);
    assert(strstr(log_text,"flag=1 authenticated=1"));
    puts("presentation native adapter admission and ownership PASS");
}
