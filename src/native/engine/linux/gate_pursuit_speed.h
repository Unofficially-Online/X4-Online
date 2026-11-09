/* native_gate_pursuit_speed (Linux gate only): the pursuit behaviour's speed factor in the Windows order. Included by
 * gate_linux.c only.
 *
 * A steering behaviour that flies at a target (Windows 0xed2510, Linux 0x13d7ac0) sets its forward speed command from
 * the cosine c of the angle to the target: 1 for c <= -1, 0.2 for c >= 0.5, between them the line 1 - (c + 1) 0.8 / 1.5,
 * times the speed limit. MSVC folded 0.8 / 1.5 into one constant and computes 1 - (c + 1) * 0.53333336 (0x3f088889,
 * 0xed2646); GCC computes ((c + 1) * -0.8) / 1.5 + 1 (0x13d7bd4, 0x13d7bdc). The two round apart in a share of the
 * values: lx66a against W36q, actor 3's speed command 2 ulps apart from tick 99 (42df5eb0 / 42df5eb2, the factor
 * 3eaebbb8 / 3eaebbba), then the steering slot's inputs, actor 5's velocity at tick 124 and every state after it.
 *
 * Here the run 0x13d7bd4..0x13d7be4 (mulss xmm3,[-0.8]; divss xmm3,[1.5]) runs in a stub on a page near the image as
 * one mulss by the page's -0.53333336 (the negated product, then the native addss of 1: 1 - (c + 1) K exactly as
 * Windows rounds it). The run's first five bytes become jmp rel32 (one compare-exchange of its 16-byte line); no
 * branch in the executable leads inside the run; it is the only division by that 1.5 in the executable. Pinned
 * (pursuit_speed 0x13d7b9c..0x13d7be4, read with the entry as found). Before the first admitted frame, on the
 * MoveWorker; the per-tick check (stop reason 213) and the restore are this file's. Windows needs nothing. */
#define LXPS_STOP_REASON 213u
#define LXPS_VA 0x13d7bd4u
#define LXPS_BACK 0x13d7be4u
#define LXPS_K 0x10u /* -0.53333336f */
#define LXPS_CODE 0x14u
static const unsigned char lxps_original[16]={0xf3,0x0f,0x59,0x1d,0x10,0x44,0x98,0x01,0xf3,0x0f,0x5e,0x1d,0x7c,0x44,0x98,0x01};
static unsigned char *lxps_page;
static int lxps_installed,lxps_restored;
static const char *lxps_stage="idle";
static int lxps_rel(unsigned char *out,const unsigned char *page,unsigned char *at,const void *target) {
    const intptr_t d=(intptr_t)target-(intptr_t)(page+(at-out)+4);
    if(d!=(intptr_t)(int32_t)d) return 0;
    const int32_t r=(int32_t)d;memcpy(at,&r,4);return 1;
}
static int lxps_code(unsigned char out[LXPS_CODE],const unsigned char *page) {
    memset(out,0xcc,LXPS_CODE);
    memcpy(out,"\xf3\x0f\x59\x1d",4);if(!lxps_rel(out,page,out+4,page+LXPS_K)) return 0; /* mulss xmm3,[-K] */
    out[8]=0xe9;if(!lxps_rel(out,page,out+9,LX(LXPS_BACK))) return 0;
    const uint32_t k=0xbf088889u;memcpy(out+LXPS_K,&k,4);
    return 1;
}
static int lxps_page_ok(void) {unsigned char want[LXPS_CODE];return lxps_page && lxps_code(want,lxps_page) && !memcmp(lxps_page,want,LXPS_CODE);}
static int lxps_patch(unsigned char out[5]) {
    out[0]=0xe9;const intptr_t d=(intptr_t)lxps_page-(intptr_t)(LX(LXPS_VA)+5);
    if(!lxps_page || d!=(intptr_t)(int32_t)d) return 0;
    const int32_t r=(int32_t)d;memcpy(out+1,&r,4);return 1;
}
static int lxps_state(void) { /* 0 native, 1 ours, -1 other */
    unsigned char now[5],ours[5];
    if(!gate_read_local(LX(LXPS_VA),now,5)) return -1;
    if(!memcmp(now,lxps_original,5)) return 0;
    if(lxps_patch(ours) && !memcmp(now,ours,5)) return 1;
    return -1;
}
static int lxps_pins(void) {
    const lx_site *s=site_named("pursuit_speed");unsigned char copy[0x80];
    if(!s || s->length>sizeof(copy) || LXPS_VA<s->va || LXPS_VA+sizeof(lxps_original)!=s->va+s->length || lxps_state()<0 ||
       !gate_read_local(LX(s->va),copy,s->length)) return 0;
    memcpy(copy+(LXPS_VA-s->va),lxps_original,5);
    return hash_ok(copy,s->length,s->sha256);
}
static int lxps_swap(const unsigned char from[5],const unsigned char to[5]) {
    unsigned char *line=(unsigned char*)((uintptr_t)LX(LXPS_VA)&~(uintptr_t)15);const unsigned at=(unsigned)(LX(LXPS_VA)-line);
    unsigned __int128 current,desired;
    if(at+5>16) return 0;
    memcpy(&current,line,16);
    if(memcmp((unsigned char*)&current+at,from,5)) return 0;
    desired=current;memcpy((unsigned char*)&desired+at,to,5);
    return gate_code_swap128(line,current,desired);
}
/* Before the first admitted frame, on the MoveWorker. */
static int lxps_arm(void) {
    if(lxps_installed) return stats.active;
    if(!stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) {lxps_stage="guard";return 0;}
    if(lxps_state()!=0) {lxps_stage="entry";return 0;}
    if(!lxps_pins()) {lxps_stage="pins";return 0;}
    if(!lxps_page) lxps_page=gate_near_page(LX(LXPS_VA));
    if(!lxps_page || mprotect(lxps_page,GATE_PAGE,PROT_READ|PROT_WRITE)) {lxps_stage="page";return 0;}
    memset(lxps_page,0xcc,GATE_PAGE);
    unsigned char to[5];
    if(!lxps_code(lxps_page,lxps_page) || mprotect(lxps_page,GATE_PAGE,PROT_READ|PROT_EXEC) || !lxps_patch(to) ||
       !lxps_swap(lxps_original,to)) {lxps_stage="install";return 0;}
    lxps_installed=1;lxps_restored=0;lxps_stage="armed";return 1;
}
static int lxps_check(void) {
    const char *why=lxps_state()!=1?"entry_changed":!lxps_page_ok()?"page_changed":!lxps_pins()?"block_changed":NULL;
    if(why) {lxps_stage=why;return 0;}
    return 1;
}
static void lxps_restore(void) {
    if(!lxps_installed) return;
    unsigned char from[5];
    if(lxps_state()!=1 || !lxps_patch(from)) {lxps_stage="restore_foreign";return;}
    if(lxps_swap(from,lxps_original)) {lxps_installed=0;lxps_restored=1;lxps_stage="restored";} /* the page stays */
}
static int lxps_status(char *out,size_t size) {
    const int n=snprintf(out,size,"tick=%llu installed=%d restored=%d state=%d pin=%d page=%d stage=%s",(unsigned long long)stats.issued,
        lxps_installed,lxps_restored,lxps_state(),lxps_pins(),lxps_page_ok(),lxps_stage);
    return n>0 && (size_t)n<size?n:0;
}
static int lua_pursuit_speed(lua_State *L) {p_lua_pushnumber(L,lxps_arm());return 1;}
static int lua_pursuit_speed_status(lua_State *L) {
    char out[200];const int n=accept_ui()?lxps_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
