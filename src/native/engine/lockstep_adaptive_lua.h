/* Lua surface of protocol 5 (lockstep_adaptive.h). */
/* gate.lookahead_adaptive(kmax): protocol 5 with windows 1..kmax chosen by the relay; after gate.wire(3, N), before
 * gate.start/gate.boot; once started only the same maximum returns 1 (UI reloads). */
static int lua_lookahead_adaptive(lua_State *L) {
    double k=p_lua_tonumber(L,1);
    p_lua_pushnumber(L,isfinite(k) && floor(k)==k && k>=1 && k<=GATE_LOOKAHEAD_MAX && p_lua_rawseti && adapt_configure((unsigned)k));
    return 1;
}
static int lua_adaptive_status(lua_State *L) {
    char out[512];int n=adapt_status(out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
/* gate.adaptive_plan() -> pilot delay, lag and entries held once the plan arrived (with the first permits, before
 * the first gated step); nil before. The probe re-arms the pilot records with this delay after a UI reload. */
static int lua_adaptive_plan(lua_State *L) {
    if(!wire_adaptive || !adapt_epoch) {p_lua_pushnil(L);return 1;}
    p_lua_pushnumber(L,adapt_entries[0].pilot);p_lua_pushnumber(L,adapt_entries[0].lag);p_lua_pushnumber(L,adapt_epoch);
    return 3;
}
/* gate.poll() fields of protocol 5 (after s.inputs): s.adaptive = schedule entries held; s.lookahead = the window of the
 * inputs taken; s.inputs_tick = their proposal tick (absent for a gap permit and before the first sourced one);
 * s.quiet = 1 when the proposal of s.issued must stay empty (it is held back for a later tick); s.lag = that
 * proposal's lag (apply tick s.issued+lag); s.pilot_delay = the plan's; s.dropped = mask of dropped slots. */
static void adapt_push(lua_State *L) {
    p_lua_pushnumber(L,adapt_epoch);p_lua_setfield(L,-2,"adaptive");
    p_lua_pushnumber(L,wire_lookahead);p_lua_setfield(L,-2,"lookahead");
    if(adapt_source_set) {p_lua_pushnumber(L,(double)adapt_source);p_lua_setfield(L,-2,"inputs_tick");}
    if(!adapt_epoch) return;
    int quiet=adapt_quiet(stats.issued);
    p_lua_pushnumber(L,quiet);p_lua_setfield(L,-2,"quiet");
    if(!quiet) {p_lua_pushnumber(L,adapt_lag(stats.issued));p_lua_setfield(L,-2,"lag");}
    p_lua_pushnumber(L,adapt_entries[0].pilot);p_lua_setfield(L,-2,"pilot_delay");
    p_lua_pushnumber(L,adapt_mask);p_lua_setfield(L,-2,"dropped");
}
