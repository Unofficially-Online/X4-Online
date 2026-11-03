/* Lua surface of the missions and conversation families (lockstep_ui_missions.h, tools/dev/native_gate_ui_replay_core.lua). */
/* gate.ui_missions(): after gate.ui_replay (with missions or conversation) and gate.ui_guard (with their groups), before the
 * first admitted tick. 1, or 0 and the reason. */
static double uimis_lua_tonumber(void *L,int index) {return (double)p_lua_tonumber((lua_State*)L,index);}
static int lua_ui_missions(lua_State *L) {
    int ok=accept_ui() && uimis_arm(uimis_lua_tonumber);
    p_lua_pushnumber(L,ok);
    if(ok) return 1;
    p_lua_pushlstring(L,uimis_why,strlen(uimis_why));return 2;
}
/* gate.ui_missions_push(op, hex): a conversation call the Lua core captured (op 0x49, slot 1 only). 1 or 0. */
static int lua_ui_missions_push(lua_State *L) {
    double op=p_lua_tonumber(L,1);size_t length=0;const char *text=p_lua_tolstring(L,2,&length);
    unsigned char data[UIREP_EVENT_MAX];unsigned n=0;const char *why="ui_conversation";
    int ok=accept_ui() && uirep_armed && uimis_armed && op==UIMIS_OP_CONVERSATION && pstk_capture_slot==1 && text && length<=2*UIREP_EVENT_MAX &&
        pstk_unhex(text,length,data,sizeof(data),&n) && uimis_event_ok(1,UIMIS_OP_CONVERSATION,data,n,&why);
    if(!ok) {if(uirep_armed) uirep_note_refusal("conversation",why);p_lua_pushnumber(L,0);return 1;}
    p_lua_pushnumber(L,uirep_push(UIMIS_OP_CONVERSATION,data,n));return 1;
}
/* gate.ui_missions_run(slot, hex): one mission event in the probe's frame (every client, under the replay's bypass).
 * 1 and X4's answer, 0 and why nothing ran (the same everywhere); a reference that differs or a decode stops (164). */
static int lua_ui_missions_run(lua_State *L) {
    double slot=p_lua_tonumber(L,1);size_t length=0;const char *text=p_lua_tolstring(L,2,&length);
    unsigned char data[UIREP_EVENT_MAX];unsigned n=0;char what[128]="refused";int r=-1;
    if(accept_ui() && uirep_armed && uimis_armed && InterlockedCompareExchange(&uirep_replaying,0,0) && isfinite(slot) && floor(slot)==slot &&
       slot>=1 && slot<=PILOT_SLOTS && text && pstk_unhex(text,length,data,sizeof(data),&n))
        r=uimis_run((unsigned)slot,data,n,what,sizeof(what));
    if(r<0) uirep_async_fail(!strcmp(what,"ref_mismatch")?"ui_ref_mismatch":"ui_missions");
    p_lua_pushnumber(L,r>0);p_lua_pushlstring(L,what,strlen(what));return 2;
}
/* gate.ui_missions_cue_ref(decimal ID): an MD cue's reference (hex: ID and check), nil when the ID names no cue. */
static int lua_ui_missions_cue_ref(lua_State *L) {
    size_t length=0;const char *text=p_lua_tolstring(L,1,&length);unsigned char ref[UIMIS_REF];char hex[2*UIMIS_REF+1];
    uint64_t id=text && length && length<21?strtoull(text,NULL,10):0;
    if(!accept_ui() || !uirep_armed || !uimis_armed || !id || !uimis_put_cue(ref,id)) {p_lua_pushnil(L);return 1;}
    pilot_hex(ref,UIMIS_REF,hex);p_lua_pushlstring(L,hex,2*UIMIS_REF);return 1;
}
/* gate.ui_missions_cue_resolve(hex): the decimal ID, nil,'gone' (the same everywhere) or nil,'mismatch' (stops). */
static int lua_ui_missions_cue_resolve(lua_State *L) {
    size_t length=0;const char *text=p_lua_tolstring(L,1,&length);unsigned char ref[UIMIS_REF];unsigned n=0;uint64_t id=0;
    if(!accept_ui() || !uirep_armed || !uimis_armed || !pstk_unhex(text,length,ref,sizeof(ref),&n) || n!=UIMIS_REF) {
        p_lua_pushnil(L);p_lua_pushlstring(L,"encoding",8);return 2;
    }
    int resolved=uimis_resolve(ref,&id);
    if(resolved<0) {uirep_async_fail("ui_ref_mismatch");p_lua_pushnil(L);p_lua_pushlstring(L,"mismatch",8);return 2;}
    if(!resolved) {InterlockedIncrement64(&uirep_stale);p_lua_pushnil(L);p_lua_pushlstring(L,"gone",4);return 2;}
    char decimal[24];int m=snprintf(decimal,sizeof(decimal),"%llu",(unsigned long long)id);
    p_lua_pushlstring(L,decimal,(size_t)m);return 1;
}
static int lua_ui_missions_frame(lua_State *L) {
    char out[96];int n=accept_ui() && uimis_armed?uimis_frame_text(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_ui_missions_state(lua_State *L) {
    char out[128];int n=accept_ui() && uimis_armed?uimis_state_text(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_ui_missions_status(lua_State *L) {
    char out[512];int n=accept_ui()?uimis_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* gate.ui_missions_rows(): "tick:op:name:id,..." of what the guard's hook captured since the last call, nil when none. */
static int lua_ui_missions_rows(lua_State *L) {
    char out[2048];int n=accept_ui() && uimis_armed?uimis_rows_text(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
