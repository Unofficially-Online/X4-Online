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
/* gate.join_evq_dump(on): every pending event at the capture and at the admission into the crash note and log (diagnostic). */
static int lua_join_evq_dump(lua_State *L) {join_evq_dump_on=p_lua_tonumber(L,1)!=0.0;p_lua_pushnumber(L,1);return 1;}
/* gate.join_hold_boot_events(on): a version 13 joiner holds its boot event passes until its admission (before join_load). */
static int lua_join_hold_boot_events(lua_State *L) {join_bootev_on=p_lua_tonumber(L,1)!=0.0;p_lua_pushnumber(L,1);return 1;}
/* gate.join_precise(on): a saver writes its join save at full precision, a version 13 joiner reads it exactly (lockstep_join_precise.h). */
static int lua_join_precise(lua_State *L) {join_precise_on=p_lua_tonumber(L,1)!=0.0;p_lua_pushnumber(L,1);return 1;}
/* gate.join_ids(on): a joiner's saved objects take the session's runtime IDs in its load (lockstep_join_ids.h). */
static int lua_join_ids(lua_State *L) {join_ids_on=p_lua_tonumber(L,1)!=0.0;p_lua_pushnumber(L,1);return 1;}
/* gate.join_buildp(on): JOIN_BUILDP rows after T+1 and T+14 on both sides (lockstep_join_world.h; diagnostic, with the watch). */
static int lua_join_buildp(lua_State *L) {join_buildp_on=p_lua_tonumber(L,1)!=0.0;p_lua_pushnumber(L,1);return 1;}
/* gate.join_viewf_write(on): the joiner writes the session's VIEWF words (lockstep_join_world.h; diagnostic, default off). */
static int lua_join_viewf_write(lua_State *L) {join_viewf_write=p_lua_tonumber(L,1)!=0.0;p_lua_pushnumber(L,1);return 1;}
/* gate.join_viewl(on): a joiner runs the view handlers where its live/player view flags differ (VIEWL; default off). */
static int lua_join_viewl(lua_State *L) {join_viewl_on=p_lua_tonumber(L,1)!=0.0;p_lua_pushnumber(L,1);return 1;}
/* gate.join_rcnt(on): a joiner lowers the created ships' and bays' reference counts to the session's (RCNT; experiment, default off). */
/* gate.join_ecos(on): a joiner takes the session's economy schedules (ECOS, default off). */
static int lua_join_ecos(lua_State *L) {join_ecos_on=p_lua_tonumber(L,1)!=0.0;p_lua_pushnumber(L,1);return 1;}
/* gate.join_shpw(on): a joiner takes the session's ship words (SHPW, default off). */
static int lua_join_shpw(lua_State *L) {join_shpw_on=p_lua_tonumber(L,1)!=0.0;p_lua_pushnumber(L,1);return 1;}
/* gate.join_trdn(on): a joiner takes the session's next trade offer updates (TRDN, default off). */
static int lua_join_trdn(lua_State *L) {join_trdn_on=p_lua_tonumber(L,1)!=0.0;p_lua_pushnumber(L,1);return 1;}
/* gate.join_npca(on): a joiner takes the session's NPC activations (NPCA, default off). */
static int lua_join_npca(lua_State *L) {join_npca_on=p_lua_tonumber(L,1)!=0.0;p_lua_pushnumber(L,1);return 1;}
/* gate.join_jobg(on): a joiner takes the session's job generator words (JOBG, default off). */
static int lua_join_jobg(lua_State *L) {join_jobg_on=p_lua_tonumber(L,1)!=0.0;p_lua_pushnumber(L,1);return 1;}
/* gate.join_jobw(on): a joiner writes the session's job state +0x48 (experiment, default off). */
static int lua_join_jobw(lua_State *L) {join_jobw_on=p_lua_tonumber(L,1)!=0.0;p_lua_pushnumber(L,1);return 1;}
/* gate.join_code(on): a joiner takes the session's object code counter (CODE, default off). */
static int lua_join_code(lua_State *L) {join_code_on=p_lua_tonumber(L,1)!=0.0;p_lua_pushnumber(L,1);return 1;}
static int lua_join_rcnt(lua_State *L) {join_rcnt_on=p_lua_tonumber(L,1)!=0.0;p_lua_pushnumber(L,1);return 1;}
/* gate.join_pend(on): a joiner takes the session's pending view cache flags (PEND; lockstep_join_world.h; default off). */
static int lua_join_pend(lua_State *L) {join_pend_on=p_lua_tonumber(L,1)!=0.0;p_lua_pushnumber(L,1);return 1;}
/* gate.join_viewf_bits(mask): the bits of the session's VIEWF flag words a joiner takes (lockstep_join_world.h; 0 off); 1 or 0 refused. */
static int lua_join_viewf_bits(lua_State *L) {
    const double m=p_lua_tonumber(L,1);const int ok=isfinite(m) && m>=0 && m<=4294967295.0 && floor(m)==m;
    if(ok) join_viewf_bits=(uint32_t)m;p_lua_pushnumber(L,ok?1:0);return 1;
}
/* gate.join_view_skip(mask): view-bank writes a joiner's admission leaves out (lockstep_join_world.h; diagnostic). */
static int lua_join_view_skip(lua_State *L) {
    const double m=p_lua_tonumber(L,1);const int ok=isfinite(m) && m>=0 && m<=15 && floor(m)==m;
    if(ok) join_view_skip=(unsigned)m;p_lua_pushnumber(L,ok?1:0);return 1;
}
/* gate.join_watch_span(n): the JOIN_WATCH rows after each step T+1 .. T+n (2 .. 300; default 2); n, 0 refused. */
static int lua_join_watch_span(lua_State *L) {
    const double n=p_lua_tonumber(L,1);const int ok=isfinite(n) && floor(n)==n && n>=2.0 && n<=(double)JOIN_WATCH_SPAN_MAX;
    if(ok) join_watch_span=(uint32_t)n;
    p_lua_pushnumber(L,ok?n:0.0);return 1;
}
/* gate.join_watch(id): one more watched object (16 hex digits, a portable ID) for the JOIN_WATCH rows; its place, 0 refused. */
static int lua_join_watch(lua_State *L) {
    size_t length=0;const char *text=p_lua_tolstring(L,1,&length);uint64_t id=0;int ok=text && length==16;
    for(size_t i=0;ok && i<16;i++) {
        const char c=text[i];unsigned v=c>='0' && c<='9'?(unsigned)(c-'0'):c>='a' && c<='f'?(unsigned)(c-'a'+10):16u;
        if(v>15u) ok=0;else id=id<<4|v;
    }
    p_lua_pushnumber(L,ok?(double)join_watch_add(id):0.0);return 1;
}
/* gate.join_jobcnt_at(n): JOIN_JOBCNT rows after step n (1 .. 100000); n, 0 refused. */
static int lua_join_jobcnt_at(lua_State *L) {
    const double n=p_lua_tonumber(L,1);const int ok=isfinite(n) && floor(n)==n && n>=1.0 && n<=100000.0;
    if(ok) join_jobcnt_at=(uint64_t)n;
    p_lua_pushnumber(L,ok?n:0.0);return 1;
}
/* gate.join_jobdump(hash): one more job for JOIN_JOBDUMP (16 hex digits, the FNV-1a hash of its ID); 1, 0 refused. */
static int lua_join_jobdump(lua_State *L) {
    size_t length=0;const char *text=p_lua_tolstring(L,1,&length);uint64_t id=0;int ok=text && length==16;
    for(size_t i=0;ok && i<16;i++) {
        const char c=text[i];unsigned v=c>='0' && c<='9'?(unsigned)(c-'0'):c>='a' && c<='f'?(unsigned)(c-'a'+10):16u;
        if(v>15u) ok=0;else id=id<<4|v;
    }
    p_lua_pushnumber(L,ok && join_jobdump_add(id)?1.0:0.0);return 1;
}
/* gate.join_refwatch(spec): one JOIN_REFWATCH field, '<portable>:<deref>/<deref>:<offset>:<len>' (hex; lockstep_join_world.h); 1, 0 refused. */
static int lua_join_refwatch(lua_State *L) {
    size_t length=0;const char *text=p_lua_tolstring(L,1,&length);
    p_lua_pushnumber(L,text && length<96u && join_rw_add(text,length)?1.0:0.0);return 1;
}
/* gate.join_refwatch_from(n): a saver arms the JOIN_REFWATCH fields from step n on (snapshot objects by portable ID; 1 .. 100000); n, 0 refused. */
static int lua_join_refwatch_from(lua_State *L) {
    const double n=p_lua_tonumber(L,1);const int ok=isfinite(n) && floor(n)==n && n>=1.0 && n<=100000.0;
    if(ok) join_rw_from=(uint64_t)n;
    p_lua_pushnumber(L,ok?n:0.0);return 1;
}
static int lua_join_status(lua_State *L) {
    char out[8192];int n=accept_ui()?join_status(out,sizeof(out)):0;   /* 3584 until IKV's refusals (the line passed 3200) */
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
