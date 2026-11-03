/* Hot join Lua API (lockstep_join.h; the probe glue tools/dev/native_gate_join_core.lua):
 *   gate.join_arm(T, k, mask, save_folder[, mode])  a saver: save and capture after step T (UI thread, >= 2 ticks
 *                                                   ahead); mode 0 strict (refuse unaudited modules), 1 report
 *   gate.join_result()                              'state|T|k|bytes|package_sha|portable|why|detail' of the last
 *                                                   arming (state idle, armed, done, failed)
 *   gate.join_load(package, sha[, mode])            a joiner, before the load request: verify and keep the package
 *   gate.join_handover(H)                           a joiner: its handover tick (from the HANDOVER record)
 *   gate.join_base()                                a joiner's tick base T after its admission, else nil
 *   gate.join_lua(T, text)                          a saver in frame T: the probe's state providers' text for the package
 *   gate.join_lua_state()                           a joiner: that text from its package ('' when none)
 *   gate.join_status()                              the JOIN status line (both sides)
 *   gate.join_unpaired(i)                           a joiner after its admission: the i-th sample row (1-based) of the unpaired
 *                                                   contexts and MD heap entries with every key part, nil past the last */
static int lua_join_arm(lua_State *L) {
    double tick=p_lua_tonumber(L,1),slot=p_lua_tonumber(L,2),mask=p_lua_tonumber(L,3),mode=p_lua_tonumber(L,5);
    const char *folder=p_lua_tolstring(L,4,NULL);int ok=0;
    if(isfinite(tick) && floor(tick)==tick && tick>0 && tick<=4294967295.0 && isfinite(slot) && floor(slot)==slot && slot>=1 && slot<=8 &&
       isfinite(mask) && floor(mask)==mask && mask>=1 && mask<=255 && (!isfinite(mode) || mode==0 || mode==1))
        ok=join_arm((uint64_t)tick,(unsigned)slot,(unsigned)mask,folder,isfinite(mode)?(int)mode:JOIN_STRICT);
    p_lua_pushnumber(L,ok);return 1;
}
static int lua_join_result(lua_State *L) {
    static const char *const states[]={"idle","armed","done","failed"};char out[640],pkg[65],port[33];
    if(!accept_ui()) {p_lua_pushnil(L);return 1;}
    hex32(join_cap.package_sha,pkg);for(int i=0;i<16;i++) snprintf(port+2*i,3,"%02x",join_cap.portable[i]);
    int n=snprintf(out,sizeof(out),"%s|%llu|%u|%llu|%s|%s|%u|%s",states[join_cap.state],(unsigned long long)join_cap.tick,join_cap.slot_k,
                   (unsigned long long)join_cap.package_bytes,pkg,port,join_cap.why,join_cap.detail[0]?join_cap.detail:"-");
    if(n>0 && (size_t)n<sizeof(out)) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
static int lua_join_load(lua_State *L) {
    const char *path=p_lua_tolstring(L,1,NULL),*sha=p_lua_tolstring(L,2,NULL);double mode=p_lua_tonumber(L,3);
    int ok=(!isfinite(mode) || mode==0 || mode==1) && join_load(path,sha,isfinite(mode)?(int)mode:JOIN_STRICT);
    p_lua_pushnumber(L,ok);return 1;
}
static int lua_join_handover(lua_State *L) {
    double tick=p_lua_tonumber(L,1);
    p_lua_pushnumber(L,accept_ui() && isfinite(tick) && floor(tick)==tick && tick>0 && join_handover((uint64_t)tick));return 1;
}
static int lua_join_base(lua_State *L) {
    if(accept_ui() && join_rx.based) p_lua_pushnumber(L,(double)join_rx.t.gate.tick);else p_lua_pushnil(L);
    return 1;
}
static int lua_join_lua(lua_State *L) {
    double tick=p_lua_tonumber(L,1);size_t length=0;const char *text=p_lua_tolstring(L,2,&length);
    p_lua_pushnumber(L,isfinite(tick) && floor(tick)==tick && tick>0 && join_lua_put((uint64_t)tick,text,length));return 1;
}
static int lua_join_lua_state(lua_State *L) {
    if(accept_ui() && join_rx.loaded) p_lua_pushlstring(L,join_rx.t.lua?join_rx.t.lua:"",join_rx.t.lua_len);else p_lua_pushnil(L);
    return 1;
}
static int lua_join_status(lua_State *L) {
    char out[3584];int n=accept_ui()?join_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
/* gate.join_t1_row(i): the steering and spline models after step T+1, both sides (lockstep_join_world.h join_t1x). */
static int lua_join_t1_row(lua_State *L) {
    double i=p_lua_tonumber(L,1);const char *row=accept_ui() && isfinite(i) && floor(i)==i && i>=1 && i<=JOIN_T1X?join_t1x_row((unsigned)i):NULL;
    if(row) p_lua_pushlstring(L,row,strlen(row));else p_lua_pushnil(L);
    return 1;
}
static int lua_join_unpaired(lua_State *L) {
    double i=p_lua_tonumber(L,1);const char *row=accept_ui() && isfinite(i) && floor(i)==i && i>=1 && i<=JOIN_ROWS?join_unpaired_row((unsigned)i):NULL;
    if(row) p_lua_pushlstring(L,row,strlen(row));else p_lua_pushnil(L);
    return 1;
}
