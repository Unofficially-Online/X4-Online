/* gate.render_camera(mode), gate.render_motion(mode), gate.render_display_rows(), gate.render_display_status(),
 * gate.render_display_row(cursor) (lockstep_render_display.h). mode: 'extrapolate' or 'interpolate'. */
static int lua_render_mode(lua_State *L,int camera) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);
    p_lua_pushnumber(L,rdisp_arm(camera,rdisp_mode_of(mode,length),used));return 1;
}
static int lua_render_camera(lua_State *L) {return lua_render_mode(L,1);}
static int lua_render_motion(lua_State *L) {return lua_render_mode(L,0);}
static int lua_render_display_rows(lua_State *L) {
    int ok=rdisp_armed && (!used || rdisp_rows_on);
    if(ok) rdisp_rows_on=1;
    p_lua_pushnumber(L,ok);return 1;
}
static int lua_render_display_status(lua_State *L) {
    char out[2048];int n=accept_ui()?rdisp_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* The row at the cursor (or the oldest kept) and the next cursor; nil when there is none yet. */
static int lua_render_display_row(lua_State *L) {
    char out[256];double cursor=p_lua_tonumber(L,1);uint64_t next=0;
    int n=accept_ui() && isfinite(cursor) && cursor>=0 && floor(cursor)==cursor?rdisp_row_text((uint64_t)cursor,out,sizeof(out),&next):0;
    if(!n) {p_lua_pushnil(L);return 1;}
    p_lua_pushlstring(L,out,(size_t)n);p_lua_pushnumber(L,(double)next);return 2;
}
/* gate.render_radar(mode) 'rotate' | 'observe', gate.render_radar_status() (lockstep_render_radar.h);
 * gate.render_shots(mode) 'lead' | 'motion' | 'probe', gate.render_shots_status() (lockstep_render_shots.h). */
static int lua_render_radar(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);
    p_lua_pushnumber(L,rradar_arm(rradar_mode_of(mode,length),used));return 1;
}
static int lua_render_radar_status(lua_State *L) {
    char out[1600];int n=accept_ui()?rradar_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_render_shots(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);
    p_lua_pushnumber(L,rshot_arm(rshot_mode_of(mode,length),used));return 1;
}
static int lua_render_shots_status(lua_State *L) {
    char out[1600];int n=accept_ui()?rshot_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* gate.render_clock(mode) 'gap' | 'steady' (lockstep_render_display.h, the display clock). */
static int lua_render_clock(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);
    p_lua_pushnumber(L,rdisp_clock_arm(rdisp_clock_of(mode,length),used));return 1;
}
