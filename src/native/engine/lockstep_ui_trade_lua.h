/* Lua surface of the UI replay's trade family (lockstep_ui_trade.h; tools/dev/native_gate_ui_replay_core.lua X4LAN_UI_TRADE). */
/* gate.ui_trade_pins(): 1, or 0 and the first entry or pinned function that differs from the pinned build. The probe asks
 * where the other pins are checked, before modules patch X4; the first pass checks the cached answer again. */
static int lua_ui_trade_pins(lua_State *L) {
    char why[32];
    int ok=accept_ui() && uitrade_pins();
    p_lua_pushnumber(L,ok);
    if(ok) return 1;
    snprintf(why,sizeof(why),"pins_ui_trade:%x",pin_failed_rva);p_lua_pushlstring(L,why,strlen(why));return 2;
}
/* gate.ui_trade_run(slot, hex): one FFI event in the probe's frame (every client, during the apply): 1 and what ran,
 * 0 and why nothing ran (the same everywhere); decode errors and identities that differ stop (164). */
static int lua_ui_trade_run(lua_State *L) {
    double slot=p_lua_tonumber(L,1);size_t length=0;const char *text=p_lua_tolstring(L,2,&length);
    unsigned char data[UIREP_EVENT_MAX];unsigned n=0;char what[96]="refused";int r=-1;
    if(accept_ui() && uitrade_wanted() && InterlockedCompareExchange(&uirep_replaying,0,0) && isfinite(slot) && floor(slot)==slot && slot>=1 &&
       slot<=PILOT_SLOTS && text && pstk_unhex(text,length,data,sizeof(data),&n))
        r=uitrade_run((unsigned)slot,data,n,what,sizeof(what));
    if(r<0) uirep_async_fail(!strcmp(what,"ref_mismatch")?"ui_ref_mismatch":"ui_trade");
    p_lua_pushnumber(L,r>0);p_lua_pushlstring(L,what,strlen(what));return 2;
}
/* gate.ui_trade_subject(hex): an FFI event's subject (decimal runtime ID), nil,'gone' (the same everywhere) or nil,'mismatch'
 * (stops) or nil,'decode' (stops). */
static int lua_ui_trade_subject(lua_State *L) {
    size_t length=0;const char *text=p_lua_tolstring(L,1,&length);unsigned char data[UIREP_EVENT_MAX];unsigned n=0;uint64_t runtime=0;
    if(!accept_ui() || !uitrade_wanted()) {p_lua_pushnil(L);p_lua_pushlstring(L,"off",3);return 2;}
    if(!text || !pstk_unhex(text,length,data,sizeof(data),&n) || uitrade_length(data,n)!=n) {
        uirep_async_fail("ui_trade");p_lua_pushnil(L);p_lua_pushlstring(L,"decode",6);return 2;
    }
    int r=uitrade_subject(data,n,&runtime);
    if(r==1) {char decimal[24];int m=snprintf(decimal,sizeof(decimal),"%llu",(unsigned long long)runtime);p_lua_pushlstring(L,decimal,(size_t)m);return 1;}
    p_lua_pushnil(L);
    if(!r) {InterlockedIncrement64(&uitrade_skipped);p_lua_pushlstring(L,"gone",4);return 2;}
    uirep_async_fail("ui_ref_mismatch");p_lua_pushlstring(L,"mismatch",8);return 2;
}
/* gate.ui_trade_scope(slot): open slot k's faction scope around a Lua-run trade call (during the apply): 1 opened, 0 not
 * needed (the slot's faction is the player faction already), -1 refused (no faction, not an apply, a scope open). */
static int lua_ui_trade_scope(lua_State *L) {
    double slot=p_lua_tonumber(L,1);
    int r=accept_ui() && uitrade_wanted() && isfinite(slot) && floor(slot)==slot && slot>=1 && slot<=PILOT_SLOTS?uitrade_scope_enter((unsigned)slot):-1;
    p_lua_pushnumber(L,r);return 1;
}
/* gate.ui_trade_scope_end(): close it; 0 when g_PlayerFaction changed inside (the replay stops, 164). */
static int lua_ui_trade_scope_end(lua_State *L) {
    int ok=!accept_ui() || uitrade_scope_leave();
    if(!ok) uirep_async_fail("ui_trade_scope");
    p_lua_pushnumber(L,ok);return 1;
}
/* gate.ui_trade_money(slot): the balance of slot k's faction account in cents (a number), nil without a faction or account. A read. */
static int lua_ui_trade_money(lua_State *L) {
    double slot=p_lua_tonumber(L,1);int64_t cents=0;
    void *faction=accept_ui() && uitrade_wanted() && isfinite(slot) && floor(slot)==slot && slot>=1 && slot<=PILOT_SLOTS?uitrade_slot_faction((unsigned)slot):NULL;
    if(!faction || uitrade_balance(faction,&cents)<=0) {p_lua_pushnil(L);return 1;}
    p_lua_pushnumber(L,(double)cents);return 1;
}
/* gate.ui_trade_account(slot): give slot k's faction its own account when it has none (pool factions): 1 made, 0 it had one,
 * nil and a reason when refused. Called once per pool slot by the Lua core, before any of its money moves. */
static int lua_ui_trade_account(lua_State *L) {
    double slot=p_lua_tonumber(L,1);
    int r=accept_ui() && uitrade_wanted() && isfinite(slot) && floor(slot)==slot && slot>=1 && slot<=PILOT_SLOTS?uitrade_account_ensure((unsigned)slot):-1;
    if(r<0) {p_lua_pushnil(L);p_lua_pushlstring(L,"refused",7);return 2;}
    p_lua_pushnumber(L,r);return 1;
}
static int lua_ui_trade_frame(lua_State *L) {
    char out[512];int n=accept_ui() && uitrade_wanted()?uitrade_frame_text(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_ui_trade_status(lua_State *L) {
    char out[512];int n=accept_ui() && uitrade_wanted()?uitrade_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
