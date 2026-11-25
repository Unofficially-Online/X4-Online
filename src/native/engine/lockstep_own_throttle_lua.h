/* Lua surface of the own ship's throttle, boost and travel drive at once (lockstep_own_throttle.h).
 * gate.own_throttle_arm(rows, motion): after gate.own_predict('present', ...); rows 0..3600 (an OWN_THROTTLE row every N
 *   ticks while the throttle moves or the HUD shows what the simulation does not yet, 0 none), motion 0|1 (own_predict's
 *   command path takes the predicted throttle). Installs GetPlayerSpeed's entry hook. 1, or 0 and why; an identical rerun
 *   after a loading-UI reload succeeds.
 * gate.own_throttle_frame(tick): once per admitted tick after gate.own_predict_frame; a row when one is due, else nil and
 *   '-' (nothing due) or why.
 * gate.own_throttle_speed(actual, targeted, mps, boosting, travel): GetPlayerSpeed's first five answers (booleans as 0|1),
 *   returned as the HUD shows them (unchanged outside the own slot's presentation); the hook does the same natively.
 * gate.own_throttle_status(). */
static int lua_own_throttle_arm(lua_State *L) {
    const double rows=p_lua_tonumber(L,1),motion=p_lua_tonumber(L,2);const char *why="ui";int ok=0;
    if(!(isfinite(rows) && floor(rows)==rows && rows>=0 && rows<=3600 && (motion==0 || motion==1))) why="range";
    else if(accept_ui() && (ok=othr_arm((unsigned)rows,(int)motion,&why)) && !othr_hook_install(&why)) {othr_restore();ok=0;}
    p_lua_pushnumber(L,ok);
    if(ok) return 1;
    p_lua_pushlstring(L,why,strlen(why));return 2;
}
static int lua_own_throttle_frame(lua_State *L) {
    const double tick=p_lua_tonumber(L,1);char out[512];const char *why="ui";int n=-1;
    if(accept_ui() && stats.active && boot_phase==3 && isfinite(tick) && floor(tick)==tick && tick>=1 && tick<9007199254740992.0)
        n=othr_frame((uint64_t)tick,out,sizeof(out),&why);
    if(n>0) {p_lua_pushlstring(L,out,(size_t)n);return 1;}
    p_lua_pushnil(L);p_lua_pushlstring(L,why,strlen(why));return 2;
}
static int lua_own_throttle_speed(lua_State *L) {
    double in[5],out[5];
    for(int i=0;i<5;i++) in[i]=p_lua_tonumber(L,i+1);
    if(accept_ui()) othr_speed(in,out);else memcpy(out,in,sizeof(out));
    for(int i=0;i<5;i++) p_lua_pushnumber(L,out[i]);
    return 5;
}
static int lua_own_throttle_status(lua_State *L) {
    char out[2048];int n=othr_status(out,sizeof(out));
    if(n>0) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
