/* gate.render_frames(fps), gate.render_frames_status(), gate.render_split() (lockstep_render_frames.h). */
static int lua_render_frames(lua_State *L) {p_lua_pushnumber(L,rf_configure(p_lua_tonumber(L,1),used));return 1;}
static int lua_render_frames_status(lua_State *L) {
    char out[768];int n=accept_ui()?rf_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_render_split(lua_State *L) {
    char out[160];int n=accept_ui()?rf_split(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
