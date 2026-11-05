/* lockstep_join_pctl.h: the saver's line carries X4's controller's words for the ship it controls; the joiner writes them only into
 * a controller of that same ship, from the received package's LUA section, and names what it could not do. */
static __declspec(align(16)) unsigned char tjp_controller[0x800],tjp_ship[0x40];
static unsigned char *tjp_present;
static unsigned char *tjp_controller_fn(void) {return tjp_present;}
static void test_join_pctl(void) {
    unsigned char *(*const saved_fn)(void)=jpctl_controller;const int saved_armed=jpctl_armed;
    void (*const saved_extra)(void)=jadmit_extra;int (*const saved_hook)(uint64_t)=join_admit_passes;
    const uint64_t issued=stats.issued;char *const saved_lua=join_rx.t.lua;const size_t saved_len=join_rx.t.lua_len;
    static char line[64+16*PJOIN_PCTL_WORDS],lua[128+16*PJOIN_PCTL_WORDS];
    memset(tjp_controller,0,sizeof(tjp_controller));memset(tjp_ship,0,sizeof(tjp_ship));
    const uint64_t ship=0x6c725;unsigned char *sp=tjp_ship;memcpy(tjp_ship+8,&ship,8);memcpy(tjp_controller+0x28,&sp,8);
    for(unsigned i=0;i<PJOIN_PCTL_WORDS;i++) {const uint64_t w=0x1000u+i;memcpy(tjp_controller+pjoin_pctl_offsets[i],&w,8);}
    jpctl_controller=tjp_controller_fn;
    /* No controller: no line. */
    tjp_present=NULL;assert(!jpctl_state(line,sizeof(line)));
    tjp_present=tjp_controller;
    const int n=jpctl_state(line,sizeof(line));
    assert(n==(int)(3+16+1+16*PJOIN_PCTL_WORDS) && !strncmp(line,"v1|000000000006c725|",20));
    /* The joiner: its own words differ; the line from the package's LUA section writes the session's. */
    for(unsigned i=0;i<PJOIN_PCTL_WORDS;i++) {const uint64_t w=0xbad;memcpy(tjp_controller+pjoin_pctl_offsets[i],&w,8);}
    snprintf(lua,sizeof(lua),"pilot_window=v1|30,600\njoin_pctl=%s\nworld_hash=-\n",line);
    join_rx.t.lua=lua;join_rx.t.lua_len=strlen(lua);
    jpctl_armed=0;jpctl_written=0;jpctl_why="-";jpctl_admit();assert(!jpctl_written && !strcmp(jpctl_why,"-"));   /* unarmed */
    stats.issued=3;assert(!jpctl_arm());stats.issued=0;
    assert(jpctl_arm() && jpctl_armed && jadmit_extra==jpctl_admit && join_admit_passes==jadmit_dispatch);
    jpass_armed=0;assert(jadmit_dispatch(633));   /* the pass of T not armed: the extra write only, the admission goes on */
    assert(jpctl_written==1 && !strcmp(jpctl_why,"written"));
    for(unsigned i=0;i<PJOIN_PCTL_WORDS;i++) {uint64_t w=0;memcpy(&w,tjp_controller+pjoin_pctl_offsets[i],8);assert(w==0x1000u+i);}
    /* Another ship, a damaged line, the saver without a controller, no line at all: nothing written, named. */
    const uint64_t other=0x22bce;memcpy(tjp_ship+8,&other,8);jpctl_written=0;jpctl_admit();assert(!jpctl_written && !strcmp(jpctl_why,"other_ship"));
    memcpy(tjp_ship+8,&ship,8);
    assert(!jpctl_apply_text("v1|000000000006c725|00") && !strcmp(jpctl_why,"shape"));
    snprintf(lua,sizeof(lua),"join_pctl=-\n");join_rx.t.lua_len=strlen(lua);jpctl_admit();assert(!strcmp(jpctl_why,"saver_none"));
    snprintf(lua,sizeof(lua),"pilot_window=v1|30,600\n");join_rx.t.lua_len=strlen(lua);jpctl_admit();assert(!strcmp(jpctl_why,"no_line"));
    char status[96];assert(jpctl_status(status,sizeof(status))>0 && strstr(status," jpctl=1:0:no_line"));
    jpctl_controller=saved_fn;jpctl_armed=saved_armed;jadmit_extra=saved_extra;join_admit_passes=saved_hook;stats.issued=issued;
    join_rx.t.lua=saved_lua;join_rx.t.lua_len=saved_len;jpctl_why="-";jpctl_written=0;
}
