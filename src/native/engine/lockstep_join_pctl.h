/* native_gate_join_pctl (diagnostic, default off): X4's own PlayerController in a hot join.
 * A load recreates the player ship's PlayerController (vtable 0x2c39488) from <movement class="player">: its double quaternions
 * (0, 0, 0, 1), its transform copies, times, factors and step state at the load's defaults where the session's carry their
 * history (lockstep_pilot_join.h pjoin_pctl_offsets). In step T+1 the controller's transform check 0x1191740 compares the incoming
 * transform with its stored previous one (+0x220..+0x258) and on a mismatch resets its history (0x11914a0: the elapsed times
 * +0x3f0/+0x5c8 to one step, the cached rows from the load's quaternions) [F: the hot join's JOIN_REFWATCH, j1-187c; w1z23 inside
 * the pilot window, where the resume carries the same words: slot 1's controller equal to the session's after T+1].
 * The saver provides the package line join_pctl = v1|<the controlled ship's runtime ID>|<the words, 16 hex digits each, in
 * pjoin_pctl_offsets order>; the joiner writes them at its admission (join_admit_passes, main thread) into its own
 * PlayerController when that controls the same ship (join_ids keeps runtime IDs). Never refuses: status
 * jpctl=armed:written:why. */
#ifndef LOCKSTEP_JOIN_PCTL_H
#define LOCKSTEP_JOIN_PCTL_H
static int jpctl_armed;
static uint64_t jpctl_written;
static const char *jpctl_why="-";
static unsigned char *(*jpctl_controller)(void)=pilot_player_controller;   /* tests replace it */
/* The controller and the runtime ID of the ship it controls (+0x28); NULL without both. */
static unsigned char *jpctl_own(uint64_t *runtime) {
    unsigned char *c=jpctl_controller(),*object=NULL;*runtime=0;
    if(!c || !read_local(c+0x28,&object,8) || !object || !read_local(object+8,runtime,8) || !*runtime) return NULL;
    return c;
}
/* The saver's line; 0 without X4's controller or room. */
static int jpctl_state(char *out,size_t capacity) {
    uint64_t runtime=0;unsigned char *c=jpctl_own(&runtime);
    if(!c || capacity<32+16*PJOIN_PCTL_WORDS) return 0;
    size_t at=(size_t)snprintf(out,capacity,"v1|%016llx|",(unsigned long long)runtime);
    for(unsigned i=0;i<PJOIN_PCTL_WORDS;i++) {
        uint64_t w=0;
        if(!read_local(c+pjoin_pctl_offsets[i],&w,8)) return 0;
        at+=(size_t)snprintf(out+at,capacity-at,"%016llx",(unsigned long long)w);
        if(at>=capacity) return 0;
    }
    return (int)at;
}
/* A line of the received package's LUA section (join_package_line needs the admission done; this runs inside it). */
static int jpctl_line(const char *name,const char *lua,size_t lua_len,char *out,size_t capacity) {
    const size_t len=strlen(name);const char *p=lua,*end=lua?lua+lua_len:NULL;
    while(p && p<end) {
        const char *eol=memchr(p,'\n',(size_t)(end-p));if(!eol) eol=end;
        if((size_t)(eol-p)>len && !memcmp(p,name,len) && p[len]=='=') {
            const size_t n=(size_t)(eol-p)-len-1;
            if(n+1>capacity) return 0;
            memcpy(out,p+len+1,n);out[n]=0;return 1;
        }
        p=eol+1;
    }
    return 0;
}
static int jpctl_hex64(const char *p,uint64_t *v) {
    uint64_t x=0;
    for(unsigned i=0;i<16;i++) {
        const char c=p[i];const int d=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;
        if(d<0) return 0;
        x=x<<4|(uint64_t)d;
    }
    *v=x;return 1;
}
/* The joiner: the line onto its own controller. 1 written. */
static int jpctl_apply_text(const char *text) {
    uint64_t want=0,runtime=0,words[PJOIN_PCTL_WORDS];
    if(strncmp(text,"v1|",3) || strlen(text)!=3+16+1+16*PJOIN_PCTL_WORDS || text[19]!='|' || !jpctl_hex64(text+3,&want))
        {jpctl_why="shape";return 0;}
    for(unsigned i=0;i<PJOIN_PCTL_WORDS;i++) if(!jpctl_hex64(text+20+16*i,&words[i])) {jpctl_why="shape";return 0;}
    unsigned char *c=jpctl_own(&runtime);
    if(!c) {jpctl_why="no_controller";return 0;}
    if(runtime!=want) {jpctl_why="other_ship";return 0;}
    for(unsigned i=0;i<PJOIN_PCTL_WORDS;i++) memcpy(c+pjoin_pctl_offsets[i],&words[i],8);
    ++jpctl_written;jpctl_why="written";return 1;
}
static char jpctl_text[64+16*PJOIN_PCTL_WORDS];
static void jpctl_admit(void) {
    if(!jpctl_armed) return;
    if(!jpctl_line("join_pctl",join_rx.t.lua,join_rx.t.lua_len,jpctl_text,sizeof(jpctl_text))) {jpctl_why="no_line";return;}
    if(!strcmp(jpctl_text,"-")) {jpctl_why="saver_none";return;}
    (void)jpctl_apply_text(jpctl_text);
}
static int jpctl_arm(void) {
    if(stats.issued) return 0;
    jpctl_armed=1;jadmit_extra=jpctl_admit;join_admit_passes=jadmit_dispatch;return 1;
}
static int jpctl_status(char *out,size_t capacity) {
    return snprintf(out,capacity," jpctl=%d:%llu:%s",jpctl_armed,(unsigned long long)jpctl_written,jpctl_why);
}
#endif
