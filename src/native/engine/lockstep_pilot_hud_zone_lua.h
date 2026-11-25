/* gate.pilot_present_hud_zone(on) (native_gate_pilot_present_hud_zone): on a client of slot 2..N the UI's zone scanners
 * hold that slot's zone in its presentation scope, so the HUD has a target element for every object around that player
 * (lockstep_pilot_hud_zone.h). Before gate.pilot_present; 1 = set (on: the pins hold). Client 1 presents as X4 does. */
static int lua_pilot_present_hud_zone(lua_State *L) {
    const double on=p_lua_tonumber(L,1);int ok=accept_ui() && (on==0.0 || on==1.0);
    if(ok && pres_armed) ok=phz_asked==(int)on;           /* an identical rerun after a UI reload */
    else if(ok) {if(on==1.0 && stats.slot>=2) ok=phz_pins();if(ok) phz_reset();}   /* the first arming: counters from 0 */
    if(ok) {phz_asked=(int)on;phz_on=(int)on && stats.slot>=2;}
    p_lua_pushnumber(L,ok);return 1;
}
