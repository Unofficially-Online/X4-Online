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
