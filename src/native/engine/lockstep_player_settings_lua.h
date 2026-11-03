/* Lua surface of the player settings per slot (lockstep_player_settings.h; native_gate_player_settings_per_slot; the Lua half
 * rides X4LAN_UI_FACTION in native_gate_ui_replay_core.lua). */
/* gate.player_settings_pins(): 1, or 0 and the first pinned function that differs. The probe asks before modules patch X4. */
static int lua_player_settings_pins(lua_State *L) {
    char why[48];
    int ok=accept_ui() && pss_pins();
    p_lua_pushnumber(L,ok);
    if(ok) return 1;
    snprintf(why,sizeof(why),"pins_player_settings:%x",pin_failed_rva);p_lua_pushlstring(L,why,strlen(why));return 2;
}
/* gate.player_settings(): after gate.ui_faction, before the first tick: 1 and the number of patched entries, or 0 and why. */
static int lua_player_settings(lua_State *L) {
    int ok=accept_ui() && pss_arm();
    p_lua_pushnumber(L,ok);
    if(ok) {p_lua_pushnumber(L,(double)PSS_HOOKS);return 2;}
    p_lua_pushlstring(L,pss_arm_why,strlen(pss_arm_why));return 2;
}
/* gate.player_settings_faction(id): this client's slot faction's ID (the owner of its ship), for the menus' "player". */
static int lua_player_settings_faction(lua_State *L) {
    size_t n=0;const char *id=p_lua_tolstring(L,1,&n);int ok=0;
    if(accept_ui() && pss_armed && id && n<=PSS_ID_MAX && !memchr(id,0,n)) {memcpy(pss_own_id,id,n);pss_own_id[n]=0;ok=1;}
    p_lua_pushnumber(L,ok);return 1;
}
/* gate.player_settings_state() / gate.player_settings_status(): readable rows, nil when off. */
static int lua_player_settings_state(lua_State *L) {
    char out[1536];int n=accept_ui() && pss_armed?pss_state_text(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_player_settings_status(lua_State *L) {
    char out[768];int n=accept_ui() && pss_armed?pss_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* gate.player_settings_themes(slot): slot k's paint and clothing theme IDs ('' none; slot 1 X4's player), nil when off. */
static int lua_player_settings_themes(lua_State *L) {
    double slot=p_lua_tonumber(L,1);char paint[PSS_ID_MAX],cloth[PSS_ID_MAX];
    if(!accept_ui() || !pss_armed || !isfinite(slot) || floor(slot)!=slot || slot<1 || slot>PILOT_SLOTS) {p_lua_pushnil(L);return 1;}
    pss_themes_of((unsigned)slot,paint,cloth,sizeof(paint));
    p_lua_pushlstring(L,paint,strlen(paint));p_lua_pushlstring(L,cloth,strlen(cloth));return 2;
}
