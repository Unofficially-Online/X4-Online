/* Lua surface of the UI guard (lockstep_ui_guard.h, native_gate_ui_guard; tools/dev/native_gate_ui_replay_core.lua). */
/* gate.ui_guard_pins(): 1, or 0 and the first entry whose bytes differ from the pinned build. The probe asks where the
 * other pins are checked, before modules patch X4; gate.ui_guard answers from the cached check. */
static int lua_ui_guard_pins(lua_State *L) {
    char why[32];
    int ok=accept_ui() && uiguard_pins();
    p_lua_pushnumber(L,ok);
    if(ok) return 1;
    snprintf(why,sizeof(why),"pins_ui_guard:%x",pin_failed_rva);p_lua_pushlstring(L,why,strlen(why));return 2;
}
/* gate.ui_guard(groups): bit mask of the groups (lockstep_ui_guard.h order, all = 16383). After gate.ui_replay, before
 * the first admitted tick. 1 and the number of entries, or 0 and the reason. */
static int lua_ui_guard(lua_State *L) {
    double groups=p_lua_tonumber(L,1);
    uiguard_refuse("arguments");
    uiguard_lua_string=(const char *(*)(void*,int,size_t*))p_lua_tolstring;   /* the rows' arguments (UIGUARD_ARGS_*) */
    int ok=isfinite(groups) && floor(groups)==groups && groups>=1 && groups<=UIGUARD_ALL_GROUPS && accept_ui() && uiguard_arm((unsigned)groups);
    p_lua_pushnumber(L,ok);
    if(ok) {unsigned n=0;for(unsigned i=0;i<uiguard_count;i++) n+=uiguard_selected(&uiguard_tab[i]);p_lua_pushnumber(L,n);return 2;}
    p_lua_pushlstring(L,uiguard_arm_why,strlen(uiguard_arm_why));return 2;
}
/* gate.ui_compslot(): the interact menu's PerformCompSlotPlayerAction captured and replayed by the action's class
 * (lockstep_ui_compslot.h). After gate.ui_guard, before the first admitted tick. 1, or 0 and the reason. */
static int lua_ui_compslot(lua_State *L) {
    uirep_refuse("arguments");
    const int ok=accept_ui() && !stats.issued && uics_arm();
    p_lua_pushnumber(L,ok);
    if(ok) return 1;
    p_lua_pushlstring(L,uirep_arm_why,strlen(uirep_arm_why));return 2;
}
static int lua_ui_guard_status(lua_State *L) {
    static char out[4096];int n=accept_ui() && uiguard_armed?uiguard_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* gate.ui_guard_refusals(): "tick:name:group,..." since the last call, nil when none. */
static int lua_ui_guard_refusals(lua_State *L) {
    char out[4096];int n=accept_ui() && uiguard_armed?uiguard_refusals_text(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
