/* Lua surface of the UI replay's faction family (lockstep_ui_faction.h; native_gate_ui_replay_core.lua X4LAN_UI_FACTION). */
/* gate.ui_faction_pins(): 1, or 0 and the first pinned function that differs (the family's and the trade family's scope pins).
 * The probe asks where the other pins are checked, before modules patch X4. */
static int lua_ui_faction_pins(lua_State *L) {
    char why[40];
    int ok=accept_ui() && uifac_pins();
    if(ok && !uitrade_pins()) {snprintf(why,sizeof(why),"pins_ui_trade:%x",pin_failed_rva);p_lua_pushnumber(L,0);p_lua_pushlstring(L,why,strlen(why));return 2;}
    p_lua_pushnumber(L,ok);
    if(ok) return 1;
    snprintf(why,sizeof(why),"pins_ui_faction:%x",pin_failed_rva);p_lua_pushlstring(L,why,strlen(why));return 2;
}
/* gate.ui_faction(): after gate.ui_replay (with the family) and gate.ui_guard, before the first tick: 1 and the number of
 * claimed entries, or 0 and why. */
static int lua_ui_faction(lua_State *L) {
    int ok=accept_ui() && uifac_arm();
    p_lua_pushnumber(L,ok);
    if(ok) {p_lua_pushnumber(L,(double)UIFAC_CLAIMS);return 2;}
    p_lua_pushlstring(L,uifac_arm_why,strlen(uifac_arm_why));return 2;
}
/* gate.ui_faction_part(slot, hex): one part at its apply (every client): 0 more to come; 1, the claim's name and its subject
 * (a decimal runtime ID, '' when it names none, 'gone'); nil and why when dropped (torn). Malformed records and subjects
 * whose identities differ stop (164). */
static int lua_ui_faction_part(lua_State *L) {
    double slot=p_lua_tonumber(L,1);size_t length=0;const char *text=p_lua_tolstring(L,2,&length);
    unsigned char data[UIREP_EVENT_MAX];unsigned n=0;const char *why=NULL;int r=-2;
    if(accept_ui() && uifac_armed && isfinite(slot) && floor(slot)==slot && slot>=1 && slot<=PILOT_SLOTS && text && length<=2*UIREP_EVENT_MAX &&
       pstk_unhex(text,length,data,sizeof(data),&n))
        r=uifac_part((unsigned)slot,data,n,&why);
    if(r==0) {p_lua_pushnumber(L,0);return 1;}
    if(r==1) {
        const char *name="?";uint64_t runtime=0;int s=uifac_ready_subject((unsigned)slot,&name,&runtime);
        if(s<0) {uirep_async_fail("ui_ref_mismatch");p_lua_pushnil(L);p_lua_pushlstring(L,"mismatch",8);return 2;}
        char decimal[24]="";int m=0;
        if(s==1) m=snprintf(decimal,sizeof(decimal),"%llu",(unsigned long long)runtime);
        else if(s==2) m=snprintf(decimal,sizeof(decimal),"gone");
        p_lua_pushnumber(L,1);p_lua_pushlstring(L,name,strlen(name));p_lua_pushlstring(L,decimal,(size_t)m);return 3;
    }
    if(r==-2 && uifac_armed) uirep_async_fail(why?why:"ui_faction");
    why=why?why:"ui_faction";p_lua_pushnil(L);p_lua_pushlstring(L,why,strlen(why));return 2;
}
/* gate.ui_faction_run(slot, faction, refused): the slot's whole record (during the apply): faction = the slot's faction ID
 * (the owner of its ship), refused = the Lua half's verdict or nil. 1 and what ran, 0 and what was skipped (the same
 * everywhere); a stop (164) answers 0 and the why. */
static int lua_ui_faction_run(lua_State *L) {
    double slot=p_lua_tonumber(L,1);size_t a=0,b=0;const char *faction=p_lua_tolstring(L,2,&a),*refused=p_lua_tolstring(L,3,&b);
    char what[160]="ui_faction_run";int r=-1;
    static char self[UIFAC_STRING_MAX+1],verdict[48];
    if(accept_ui() && uifac_armed && InterlockedCompareExchange(&uirep_replaying,0,0) && isfinite(slot) && floor(slot)==slot && slot>=1 && slot<=PILOT_SLOTS) {
        self[0]=0;if(faction && a<=UIFAC_STRING_MAX) {memcpy(self,faction,a);self[a]=0;}
        verdict[0]=0;if(refused && b) {size_t k=b<sizeof(verdict)-1?b:sizeof(verdict)-1;memcpy(verdict,refused,k);verdict[k]=0;}
        r=uifac_run((unsigned)slot,self,verdict[0]?verdict:NULL,what,sizeof(what));
    }
    if(r<0 && uifac_armed) uirep_async_fail(!strcmp(what,"ui_ref_mismatch")?"ui_ref_mismatch":what);
    p_lua_pushnumber(L,r>0);p_lua_pushlstring(L,what,strlen(what));return 2;
}
/* gate.ui_faction_frame(): the receipt's part (UIFAC_1:...), nil when off. gate.ui_faction_state(): the same without the hash. */
static int lua_ui_faction_frame(lua_State *L) {
    char out[512];int n=accept_ui() && uifac_armed?uifac_frame_text(out,sizeof(out),1):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_ui_faction_state(lua_State *L) {
    char out[512];int n=accept_ui() && uifac_armed?uifac_frame_text(out,sizeof(out),0):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_ui_faction_status(lua_State *L) {
    char out[512];int n=accept_ui() && uifac_armed?uifac_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* gate.ui_faction_refusals(): "tick:what:why,..." of this client's capture refusals since the last call, nil when none. */
static int lua_ui_faction_refusals(lua_State *L) {
    char out[2048];int n=accept_ui() && uifac_armed?uifac_refusals_text(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* gate.ui_faction_view(1): open the faction view around a stock menu function (this client's UI, factions mode): 1 opened,
 * 2 open already (nested), 0 not needed, -1 refused. gate.ui_faction_view(0): close it; 0 when g_PlayerFaction changed inside
 * (the replay stops, 164). */
static int lua_ui_faction_view(lua_State *L) {
    double on=p_lua_tonumber(L,1);int r=-1;
    if(accept_ui() && uifac_armed) {
        if(on==1) r=uifac_view_enter();
        else if(on==0) {r=uifac_view_leave();if(!r) uirep_async_fail("ui_faction_view");}
    }
    p_lua_pushnumber(L,r);return 1;
}
