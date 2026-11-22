/* native_gate_weave_phase (Linux gate only): the weaving behaviours' phase in the Windows order. Included by
 * gate_linux.c only.
 *
 * Two steering behaviours swing their aim point with cos(t * pi / n), t the time since the behaviour started: n = 3
 * (Windows 0xed7d30, Linux 0x13de0b0) and n = 5 (Windows 0xed8140's sibling at 0xed81e2, Linux 0x13de470). MSVC folded
 * pi / n into one constant, t * 1.0471976 (0x3f860a92, 0xed7dca) and t * 0.62831855 (0x3f20d97c); GCC computes
 * (t * pi) / n (0x13de0e4, 0x13de4a4). The two round apart, the cosine and the aim point with them: lx69a against
 * W36q, the Xenon fighters' turn-rate commands some ulps apart from tick 106 (actor 11's yaw rate bd5af5a1 / bd5af596,
 * actor 12's pitch rate at 113), the chain to the tick-133 state difference.
 *
 * Here each run (mulss xmm0,[pi]; divss xmm0,[n], 16 bytes) runs in a stub on a page near the image as one mulss by
 * the page's folded constant. Each run's first five bytes become jmp rel32 (one compare-exchange of its 16-byte
 * line); no branch in the executable leads inside either run. Pinned (weave_phase_3 0x13de0b0..0x13de0f4,
 * weave_phase_5 0x13de470..0x13de4b4, read with the entries as found). Before the first admitted frame, on the
 * MoveWorker; the per-tick check (stop reason 216) and the restore are this file's. Windows needs nothing. */
#define LXWP_STOP_REASON 216u
#define LXWP_SITES 2u
#define LXWP_RUN 16u
#define LXWP_STUB 0x20u  /* per site: mulss xmm0,[rip+k]; jmp back; the constant at +0x10 */
#define LXWP_CODE (LXWP_SITES*LXWP_STUB)
static const struct {uint32_t va,constant;unsigned char original[LXWP_RUN];const char *pin;} lxwp_sites[LXWP_SITES]={
    {0x13de0e4u,0x3f860a92u,{0xf3,0x0f,0x59,0x05,0x68,0x90,0xa4,0x01,0xf3,0x0f,0x5e,0x05,0x4c,0x68,0xa4,0x01},"weave_phase_3"},
    {0x13de4a4u,0x3f20d97cu,{0xf3,0x0f,0x59,0x05,0xa8,0x8c,0xa4,0x01,0xf3,0x0f,0x5e,0x05,0xd4,0x78,0xa4,0x01},"weave_phase_5"}};
static unsigned char *lxwp_page;
static int lxwp_installed,lxwp_restored;
static const char *lxwp_stage="idle";
static int lxwp_rel(unsigned char *out,const unsigned char *page,unsigned char *at,const void *target) {
    const intptr_t d=(intptr_t)target-(intptr_t)(page+(at-out)+4);
    if(d!=(intptr_t)(int32_t)d) return 0;
    const int32_t r=(int32_t)d;memcpy(at,&r,4);return 1;
}
static int lxwp_code(unsigned char out[LXWP_CODE],const unsigned char *page) {
    static const unsigned char mul[4]={0xf3,0x0f,0x59,0x05}; /* mulss xmm0,[rip+..] */
    memset(out,0xcc,LXWP_CODE);
    for(unsigned i=0;i<LXWP_SITES;i++) {
        unsigned char *s=out+i*LXWP_STUB;
        memcpy(s,mul,4);if(!lxwp_rel(out,page,s+4,page+i*LXWP_STUB+0x10)) return 0;
        s[8]=0xe9;if(!lxwp_rel(out,page,s+9,LX(lxwp_sites[i].va+LXWP_RUN))) return 0;
        memcpy(s+0x10,&lxwp_sites[i].constant,4);
    }
    return 1;
}
static int lxwp_page_ok(void) {unsigned char want[LXWP_CODE];return lxwp_page && lxwp_code(want,lxwp_page) && !memcmp(lxwp_page,want,LXWP_CODE);}
static int lxwp_patch(unsigned char out[5],unsigned i) {
    out[0]=0xe9;const intptr_t d=(intptr_t)(lxwp_page+i*LXWP_STUB)-(intptr_t)(LX(lxwp_sites[i].va)+5);
    if(!lxwp_page || d!=(intptr_t)(int32_t)d) return 0;
    const int32_t r=(int32_t)d;memcpy(out+1,&r,4);return 1;
}
static int lxwp_site_state(unsigned i) { /* 0 native, 1 ours, -1 other */
    unsigned char now[5],ours[5];
    if(!gate_read_local(LX(lxwp_sites[i].va),now,5)) return -1;
    if(!memcmp(now,lxwp_sites[i].original,5)) return 0;
    if(lxwp_patch(ours,i) && !memcmp(now,ours,5)) return 1;
    return -1;
}
static int lxwp_state(void) {
    const int a=lxwp_site_state(0),b=lxwp_site_state(1);
    return a==0 && b==0?0:a==1 && b==1?1:-1;
}
static int lxwp_pins(void) {
    for(unsigned i=0;i<LXWP_SITES;i++) {
        const lx_site *s=site_named(lxwp_sites[i].pin);unsigned char copy[0x80];
        if(!s || s->length>sizeof(copy) || lxwp_sites[i].va<s->va || lxwp_sites[i].va+LXWP_RUN!=s->va+s->length || lxwp_site_state(i)<0 ||
           !gate_read_local(LX(s->va),copy,s->length)) return 0;
        memcpy(copy+(lxwp_sites[i].va-s->va),lxwp_sites[i].original,5);
        if(!hash_ok(copy,s->length,s->sha256)) return 0;
    }
    return 1;
}
static int lxwp_swap(unsigned va,const unsigned char from[5],const unsigned char to[5]) {
    unsigned char *line=(unsigned char*)((uintptr_t)LX(va)&~(uintptr_t)15);const unsigned at=(unsigned)(LX(va)-line);
    unsigned __int128 current,desired;
    if(at+5>16) return 0;
    memcpy(&current,line,16);
    if(memcmp((unsigned char*)&current+at,from,5)) return 0;
    desired=current;memcpy((unsigned char*)&desired+at,to,5);
    return gate_code_swap128(line,current,desired);
}
/* Before the first admitted frame, on the MoveWorker. */
static int lxwp_arm(void) {
    if(lxwp_installed) return stats.active;
    if(!stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) {lxwp_stage="guard";return 0;}
    if(lxwp_state()!=0) {lxwp_stage="entry";return 0;}
    if(!lxwp_pins()) {lxwp_stage="pins";return 0;}
    if(!lxwp_page) lxwp_page=gate_near_page(LX(lxwp_sites[0].va));
    if(!lxwp_page || mprotect(lxwp_page,GATE_PAGE,PROT_READ|PROT_WRITE)) {lxwp_stage="page";return 0;}
    memset(lxwp_page,0xcc,GATE_PAGE);
    unsigned char a[5],b[5];
    if(!lxwp_code(lxwp_page,lxwp_page) || mprotect(lxwp_page,GATE_PAGE,PROT_READ|PROT_EXEC) || !lxwp_patch(a,0) || !lxwp_patch(b,1)) {lxwp_stage="install";return 0;}
    if(!lxwp_swap(lxwp_sites[0].va,lxwp_sites[0].original,a)) {lxwp_stage="install";return 0;}
    if(!lxwp_swap(lxwp_sites[1].va,lxwp_sites[1].original,b)) {lxwp_swap(lxwp_sites[0].va,a,lxwp_sites[0].original);lxwp_stage="install";return 0;}
    lxwp_installed=1;lxwp_restored=0;lxwp_stage="armed";return 1;
}
static int lxwp_check(void) {
    const char *why=lxwp_state()!=1?"entry_changed":!lxwp_page_ok()?"page_changed":!lxwp_pins()?"block_changed":NULL;
    if(why) {lxwp_stage=why;return 0;}
    return 1;
}
static void lxwp_restore(void) {
    if(!lxwp_installed) return;
    unsigned char a[5],b[5];
    if(lxwp_state()!=1 || !lxwp_patch(a,0) || !lxwp_patch(b,1)) {lxwp_stage="restore_foreign";return;}
    const int ra=lxwp_swap(lxwp_sites[0].va,a,lxwp_sites[0].original),rb=lxwp_swap(lxwp_sites[1].va,b,lxwp_sites[1].original);
    if(ra && rb) {lxwp_installed=0;lxwp_restored=1;lxwp_stage="restored";} /* the page stays: a thread may still be inside a stub */
}
static int lxwp_status(char *out,size_t size) {
    const int n=snprintf(out,size,"tick=%llu installed=%d restored=%d state=%d pin=%d page=%d stage=%s",(unsigned long long)stats.issued,
        lxwp_installed,lxwp_restored,lxwp_state(),lxwp_pins(),lxwp_page_ok(),lxwp_stage);
    return n>0 && (size_t)n<size?n:0;
}
static int lua_weave_phase(lua_State *L) {p_lua_pushnumber(L,lxwp_arm());return 1;}
static int lua_weave_phase_status(lua_State *L) {
    char out[200];const int n=accept_ui()?lxwp_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
