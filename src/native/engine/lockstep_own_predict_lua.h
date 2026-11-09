/* Lua surface of the default-off own-ship display prediction (lockstep_own_predict.h).
 * gate.own_predict(mode, others, tau_ms, horizon_cap): mode 'observe', 'present' or 'swap', others 0|1
 *   (also predict the other stock slots' ships), tau_ms 0..2000 correction time constant,
 *   horizon_cap 1..30 ticks. After gate.pilot_stock (and gate.pilot_present), before the first
 *   admitted tick; an identical rerun after a loading-UI reload succeeds. Returns 1, or 0 and why.
 * gate.own_predict_frame(tick): one step (UI thread, slot 6, after this tick's stock capture);
 *   returns the per-tick row or nil and a reason.
 * gate.own_predict_status(): totals, error and lead statistics, resets by cause. */
static int lua_own_predict(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length),*why="ui";
    double others=p_lua_tonumber(L,2),tau=p_lua_tonumber(L,3),cap=p_lua_tonumber(L,4);
    int kind=mode && !strcmp(mode,"observe")?OPRED_OBSERVE:mode && !strcmp(mode,"present")?OPRED_PRESENT:
        mode && !strcmp(mode,"swap")?OPRED_SWAP:OPRED_OFF,ok=0;
    if(!kind) why="mode";
    else if(!(isfinite(others) && (others==0 || others==1) && isfinite(tau) && floor(tau)==tau && tau>=0 && tau<=OPRED_TAU_MAX_MS &&
              isfinite(cap) && floor(cap)==cap && cap>=1 && cap<=OPRED_HORIZON_MAX)) why="range";
    else if(accept_ui()) ok=opred_arm_joined(kind,(int)others,(unsigned)tau,(unsigned)cap,&why);
    p_lua_pushnumber(L,ok);
    if(ok) return 1;
    p_lua_pushlstring(L,why,strlen(why));return 2;
}
static int lua_own_predict_frame(lua_State *L) {
    double tick=p_lua_tonumber(L,1);char out[2048];const char *why="ui";int n=0;
    if(accept_ui() && stats.active && boot_phase==3 && isfinite(tick) && floor(tick)==tick && tick>=1 && tick<9007199254740992.0 &&
       opred_frame((uint64_t)tick,&why)) n=opred_row(out,sizeof(out));
    if(n) {p_lua_pushlstring(L,out,(size_t)n);return 1;}
    if(!strcmp(why,"-")) why="row";
    p_lua_pushnil(L);p_lua_pushlstring(L,why,strlen(why));return 2;
}
/* The HUD on the displayed camera (lockstep_own_predict_hud.h).
 * gate.own_predict_hud_arm(): after gate.own_predict('present', ...); 1, or 0 and why.
 * gate.own_predict_hud(0) before the probe's frame and gate.own_predict_hud(1) after it (the frame reads the simulated
 *   state). (1) returns 1 when displayed poses are in place: only when armed and inside a UI entry that closes the scope.
 *   (2) opens a scope of its own for the diagnostic rows (native_gate_own_predict_hud_probe), closed by (0).
 * gate.own_predict_hud_status(). */
static int lua_own_predict_hud_arm(lua_State *L) {
    const int ok=accept_ui() && ophud_arm();
    p_lua_pushnumber(L,ok);
    if(ok) return 1;
    p_lua_pushlstring(L,ophud_why,strlen(ophud_why));return 2;
}
static int lua_own_predict_hud(lua_State *L) {
    const double on=p_lua_tonumber(L,1);int ok=0;
    if(accept_ui() && (on==1.0 || on==2.0)) ok=ophud_enter_as(on==2.0);   /* 2: the diagnostic's own scope */
    else if(accept_ui() && on==0.0) {if(ophud_open) ophud_leave();else ++ophud_unbalanced;ok=1;}
    p_lua_pushnumber(L,ok);return 1;
}
static int lua_own_predict_hud_open(lua_State *L) {p_lua_pushnumber(L,ophud_open);return 1;}
static int lua_own_predict_hud_status(lua_State *L) {
    char out[512];int n=ophud_status(out,sizeof(out));
    p_lua_pushlstring(L,out,(size_t)(n>0?n:0));return 1;
}
static int lua_own_predict_status(lua_State *L) {
    char out[16384];int n=accept_ui() && opred_armed?opred_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
