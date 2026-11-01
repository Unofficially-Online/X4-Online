/* Lua surface of the build and assets families (lockstep_ui_build.h, tools/dev/native_gate_ui_build_core.lua). */
/* gate.ui_build(factions[, faction_ids]): after gate.ui_replay (with build and/or assets) and gate.ui_guard, before the first
 * tick. factions 1 = factions mode (slots 2+ only the station construction, in the trade family's faction scope);
 * faction_ids "player,x4lan_player2,..." the identity plan's faction of each slot (a plot's faction). 1, or 0 and why. */
static int lua_ui_build(lua_State *L) {
    double factions=p_lua_tonumber(L,1);size_t length=0;const char *ids=p_lua_tolstring(L,2,&length);
    uibuild_refuse("arguments");
    int ok=(factions==0 || factions==1) && accept_ui() && (!ids || (length<PILOT_SLOTS*(UIBUILD_FACTION_ID+1) && uibuild_faction_ids(ids))) && uibuild_arm(factions==1);
    p_lua_pushnumber(L,ok);
    if(ok) return 1;
    p_lua_pushlstring(L,uibuild_arm_why,strlen(uibuild_arm_why));return 2;
}
/* gate.ui_build_add(id, hex[, scoped]): a Lua record (the replay codec's bytes) into this client's open batch; scoped 1 =
 * made inside a station construction scope (budget, plot, builder fee). 1, or 0 and why. */
static int lua_ui_build_add(lua_State *L) {
    double id=p_lua_tonumber(L,1),scoped=p_lua_tonumber(L,3);size_t length=0;const char *text=p_lua_tolstring(L,2,&length);
    static unsigned char data[UIBUILD_BATCH_MAX];unsigned n=0;const char *why="arguments";
    int ok=accept_ui() && uibuild_armed && isfinite(id) && floor(id)==id && id>=1 && id<=UIBUILD_LUA_MAX && text &&
        pstk_unhex(text,length,data,sizeof(data),&n) && uibuild_add_lua((unsigned)id,data,n,scoped==1,&why);
    if(!ok && uibuild_armed) uibuild_note(id==1?"TransferPlayerMoneyTo":id==2?"TransferMoneyToPlayer":id==3?"SetComponentName":"SignalObject",why);
    p_lua_pushnumber(L,ok);
    if(ok) return 1;
    p_lua_pushlstring(L,why,strlen(why));return 2;
}
/* gate.ui_build_flush(): the open batch goes out now (a commit scope's end, the probe's frame). 1 sent, 0 nothing or dropped. */
static int lua_ui_build_flush(lua_State *L) {
    p_lua_pushnumber(L,accept_ui() && uibuild_armed?uibuild_flush("lua"):0);return 1;
}
/* gate.ui_build_part(slot, hex): one part at its apply (every client): 1 the batch is whole, 0 more to come, nil and why
 * when dropped (torn); a malformed batch stops (164). */
static int lua_ui_build_part(lua_State *L) {
    double slot=p_lua_tonumber(L,1);size_t length=0;const char *text=p_lua_tolstring(L,2,&length);
    unsigned char data[UIREP_EVENT_MAX];unsigned n=0;const char *why=NULL;int r=-2;
    if(accept_ui() && uibuild_armed && isfinite(slot) && floor(slot)==slot && slot>=1 && slot<=PILOT_SLOTS && text && pstk_unhex(text,length,data,sizeof(data),&n))
        r=uibuild_part((unsigned)slot,data,n,&why);
    if(r>=0) {p_lua_pushnumber(L,r);return 1;}
    if(r==-2 && uibuild_armed) uirep_async_fail(why?why:"ui_build");
    p_lua_pushnil(L);why=why?why:"ui_build";p_lua_pushlstring(L,why,strlen(why));return 2;
}
/* gate.ui_build_step(slot, tick): the next record of the slot's whole batch. 'ran',what (a claimed call ran or was
 * skipped) | 'lua',id,hex,scope (the Lua core runs it, inside gate.ui_trade_scope(slot) when scope is 1, then
 * gate.ui_build_lua_done) | 'done',what | nil,why (stopped, 164). */
static int lua_ui_build_step(lua_State *L) {
    double slot=p_lua_tonumber(L,1),tick=p_lua_tonumber(L,2);char what[128]="";unsigned lua=0,length=0;const unsigned char *payload=NULL;const char *why="ui_build_step";
    static char hex[2*UIBUILD_BATCH_MAX+1];
    int r=-1,scope=0;
    if(accept_ui() && uibuild_armed && InterlockedCompareExchange(&uirep_replaying,0,0) && isfinite(slot) && floor(slot)==slot && slot>=1 && slot<=PILOT_SLOTS &&
       isfinite(tick) && tick>=1)
        r=uibuild_step((unsigned)slot,(uint64_t)tick,what,sizeof(what),&lua,&payload,&length,&why,&scope);
    if(r==1) {p_lua_pushlstring(L,"ran",3);p_lua_pushlstring(L,what,strlen(what));return 2;}
    if(r==2) {pilot_hex(payload,length,hex);p_lua_pushlstring(L,"lua",3);p_lua_pushnumber(L,lua);p_lua_pushlstring(L,hex,2*length);p_lua_pushnumber(L,scope);return 4;}
    if(r==0) {p_lua_pushlstring(L,"done",4);p_lua_pushlstring(L,what,strlen(what));return 2;}
    if(uibuild_armed) uirep_async_fail(why);
    p_lua_pushnil(L);p_lua_pushlstring(L,why,strlen(why));return 2;
}
/* gate.ui_build_lua_done(slot, ok): what the stock Lua function made of the Lua record (joins the batch's hash). */
static int lua_ui_build_lua_done(lua_State *L) {
    double slot=p_lua_tonumber(L,1),ok=p_lua_tonumber(L,2);
    int r=accept_ui() && uibuild_armed && isfinite(slot) && floor(slot)==slot && slot>=1 && slot<=PILOT_SLOTS && uibuild_lua_done((unsigned)slot,ok==1);
    if(!r && uibuild_armed) uirep_async_fail("ui_build_lua");
    p_lua_pushnumber(L,r);return 1;
}
static int lua_ui_build_frame(lua_State *L) {
    double tick=p_lua_tonumber(L,1);char out[96];
    int n=accept_ui() && uibuild_armed && isfinite(tick) && tick>=0?uibuild_frame_text((uint64_t)tick,out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_ui_build_status(lua_State *L) {
    char out[512];int n=accept_ui() && uibuild_armed?uibuild_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* gate.ui_build_plan(station, spec): a scripted station plan commit of this client's slot (native_gate_ui_build_script
 * 'plan'; station a decimal runtime ID, spec "macro,connection,pred,predconnection,x,y,z;..."), in a local phase. 1, or 0
 * and why. */
static int lua_ui_build_plan(lua_State *L) {
    size_t length=0,slen=0;const char *station=p_lua_tolstring(L,1,&length),*spec=p_lua_tolstring(L,2,&slen);const char *why="arguments";
    uint64_t runtime=0;int ok=0;
    if(accept_ui() && station && spec && length && length<21 && slen && slen<2048) {
        ok=1;for(size_t i=0;i<length;i++) {if(station[i]<'0' || station[i]>'9') ok=0;else runtime=runtime*10+(uint64_t)(station[i]-'0');}
        ok=ok && runtime && uicons_script_plan(runtime,spec,&why);
    }
    p_lua_pushnumber(L,ok);
    if(ok) return 1;
    p_lua_pushlstring(L,why,strlen(why));return 2;
}
/* gate.ui_build_refusals(): "tick:what:why,..." since the last call, nil when none. */
static int lua_ui_build_refusals(lua_State *L) {
    char out[2048];int n=accept_ui() && uibuild_armed?uibuild_refusals_text(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
