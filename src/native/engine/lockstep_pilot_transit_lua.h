/* gate.pilot_present_transit(on) (native_gate_pilot_present_transit): on a client of slot 2..N a gate or accelerator its
 * own slot's ship passes gives X4's cockpit effect of the player on this client's screen (lockstep_pilot_transit.h).
 * Before gate.pilot_present; 1 = set (on: the pins hold). Client 1 presents as X4 does. */
static int lua_pilot_present_transit(lua_State *L) {
    const double on=p_lua_tonumber(L,1);int ok=accept_ui() && (on==0.0 || on==1.0);
    if(ok && pres_armed) ok=ptran_asked==(int)on;          /* an identical rerun after a UI reload */
    else if(ok) {if(on==1.0 && stats.slot>=2) ok=ptran_pins();if(ok) ptran_reset();}   /* the first arming: counters from 0 */
    if(ok) {ptran_asked=(int)on;ptran_on=(int)on && stats.slot>=2;}
    p_lua_pushnumber(L,ok);return 1;
}
