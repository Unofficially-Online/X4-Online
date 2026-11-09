/* native_gate_ik_step (Linux gate only, default off): the skeleton joints' fixed-step update in the Windows order.
 * Included by gate_linux.c only.
 *
 * A joint (a weapon's gimbal, U::IKValue's owner, 0x668 bytes on Linux, 0x680 on Windows) moves toward its target in
 * 0.02 s sub-steps (Windows 0xe1f840, Linux 0x1565b00: +0x08 value, +0x10 time, +0x18 rate, +0x1c target, the step's
 * velocity and acceleration estimates at +0x2c/+0x30). The step divides three times by the step length 0.02f (Linux
 * 0x2c76dd0): the velocity (target - [+0x2c]) / dt, the acceleration (that - [+0x30]) / dt and the last partial step's
 * fraction. MSVC's fast floating point made each a multiplication by 50.0f (Windows 0x142cbed30, xmm14); 0.02f is
 * 0.0199999996, so x / 0.02f is x * 50.0000011 and rounds apart from x * 50.0f in a large share of the steps. The
 * division is a candidate explanation for the different bullet trajectories in W36k against lx51a (a fixture
 * ship's second hit at tick 28 had its point 1e-4 and direction 1e-5 apart). Installation alone does not establish
 * that cause: the scalar backend bypasses these divisions. See reports/LOCKSTEP-LINUX-IK-VALIDATION-20261109.txt
 * for a native fixture in which only that scalar backend ran. Keep this correction opt-in until a matched
 * Windows/Linux comparison establishes both execution and its effect.
 *
 * Here the two runs that hold the divisions run in stubs on a page near the image with each divss by xmm1 (0.02f) a
 * mulss by 50.0f from the page:
 *   A 0x1565fd8: divss xmm5,xmm1; movaps xmm0,xmm5; subss xmm0,xmm9; divss xmm0,xmm1   -> back to 0x1565fe8
 *   C 0x1566050: subss xmm5,xmm1; mov rax,[rbx]; mov rbp,[rax+0x20]; subss xmm0,xmm5; divss xmm0,xmm1 -> 0x1566063
 * Each run's first five bytes become jmp rel32 (one compare-exchange of its 16-byte line; a thread already past them
 * finishes on the native bytes, which stay). No branch in the executable leads inside either run. Pinned
 * (ik_step_block 0x1565fb0..0x1566070, read with the entries as found, and ik_step_dt, the 0.02f). Before the first
 * admitted frame, on the MoveWorker; the per-tick check (stop reason 208) and the restore are this file's. Windows
 * needs nothing. */
#define LXIS_STOP_REASON 208u
#define LXIS_A_VA 0x1565fd8u
#define LXIS_A_BACK 0x1565fe8u
#define LXIS_C_VA 0x1566050u
#define LXIS_C_BACK 0x1566063u
#define LXIS_STUB_A 0x00u
#define LXIS_STUB_C 0x40u
#define LXIS_FIFTY 0x80u
#define LXIS_CODE 0x84u
static const unsigned char lxis_a_original[16]={0xf3,0x0f,0x5e,0xe9,0x0f,0x28,0xc5,0xf3,0x41,0x0f,0x5c,0xc1,0xf3,0x0f,0x5e,0xc1};
static const unsigned char lxis_c_original[19]={0xf3,0x0f,0x5c,0xe9,0x48,0x8b,0x03,0x48,0x8b,0x68,0x20,0xf3,0x0f,0x5c,0xc5,0xf3,0x0f,0x5e,0xc1};
static unsigned char *lxis_page;
static int lxis_installed,lxis_restored;
/* A site remains our cleanup responsibility until its original bytes and RX
 * protection are confirmed. This is separate from the fully armed state. */
static int lxis_own_a,lxis_own_c;
static int (*lxis_code_swap)(void *,unsigned __int128,unsigned __int128)=gate_code_swap128;
static int (*lxis_code_close)(void *)=gate_code_close;
static const char *lxis_stage="idle";
static int lxis_cleanup_pending(void) {return lxis_own_a || lxis_own_c;}
/* The rel32 at `at` of a buffer `out` whose code runs at `page`: relative to the end of those 4 bytes where they run. */
static int lxis_rel(unsigned char *out,const unsigned char *page,unsigned char *at,const void *target) {
    const intptr_t d=(intptr_t)target-(intptr_t)(page+(at-out)+4);
    if(d!=(intptr_t)(int32_t)d) return 0;
    const int32_t r=(int32_t)d;memcpy(at,&r,4);return 1;
}
/* The page's code: stub A, stub C and 50.0f; 0 if the page is out of reach. */
static int lxis_code(unsigned char out[LXIS_CODE],const unsigned char *page) {
    unsigned char *a=out+LXIS_STUB_A,*c=out+LXIS_STUB_C;const unsigned char *fifty=page+LXIS_FIFTY;
    memset(out,0xcc,LXIS_CODE);
    /* A: mulss xmm5,[rip+fifty]; movaps xmm0,xmm5; subss xmm0,xmm9; mulss xmm0,[rip+fifty]; jmp 0x1565fe8 */
    a[0]=0xf3;a[1]=0x0f;a[2]=0x59;a[3]=0x2d;
    if(!lxis_rel(out,page,a+4,fifty)) return 0;
    memcpy(a+8,lxis_a_original+4,8);                         /* movaps xmm0,xmm5; subss xmm0,xmm9 */
    a[16]=0xf3;a[17]=0x0f;a[18]=0x59;a[19]=0x05;
    if(!lxis_rel(out,page,a+20,fifty)) return 0;
    a[24]=0xe9;if(!lxis_rel(out,page,a+25,LX(LXIS_A_BACK))) return 0;
    /* C: subss xmm5,xmm1; mov rax,[rbx]; mov rbp,[rax+0x20]; subss xmm0,xmm5; mulss xmm0,[rip+fifty]; jmp 0x1566063 */
    memcpy(c,lxis_c_original,15);
    c[15]=0xf3;c[16]=0x0f;c[17]=0x59;c[18]=0x05;
    if(!lxis_rel(out,page,c+19,fifty)) return 0;
    c[23]=0xe9;if(!lxis_rel(out,page,c+24,LX(LXIS_C_BACK))) return 0;
    const float f=50.0f;memcpy(out+LXIS_FIFTY,&f,4);
    return 1;
}
static int lxis_page_ok(void) {unsigned char want[LXIS_CODE];return lxis_page && lxis_code(want,lxis_page) && !memcmp(lxis_page,want,LXIS_CODE);}
/* The five bytes at a run's entry when patched: jmp rel32 to its stub. */
static int lxis_patch(unsigned char out[5],unsigned va,unsigned stub) {
    out[0]=0xe9;const intptr_t d=(intptr_t)(lxis_page+stub)-(intptr_t)(LX(va)+5);
    if(!lxis_page || d!=(intptr_t)(int32_t)d) return 0;
    const int32_t r=(int32_t)d;memcpy(out+1,&r,4);return 1;
}
static int lxis_site_state(unsigned va,unsigned stub,const unsigned char *original) { /* 0 native, 1 ours, -1 other */
    unsigned char now[5],ours[5];
    if(!gate_read_local(LX(va),now,5)) return -1;
    if(!memcmp(now,original,5)) return 0;
    if(lxis_patch(ours,va,stub) && !memcmp(now,ours,5)) return 1;
    return -1;
}
static int lxis_state(void) {
    const int a=lxis_site_state(LXIS_A_VA,LXIS_STUB_A,lxis_a_original),c=lxis_site_state(LXIS_C_VA,LXIS_STUB_C,lxis_c_original);
    return a==0 && c==0?0:a==1 && c==1?1:-1;
}
static int lxis_pins(void) {
    const lx_site *b=site_named("ik_step_block"),*k=site_named("ik_step_dt");unsigned char copy[0xc0];
    if(!b || !k || b->va!=0x1565fb0u || b->length!=sizeof(copy) || !site_ok(k) || !gate_read_local(LX(b->va),copy,sizeof(copy))) return 0;
    if(lxis_installed) {memcpy(copy+(LXIS_A_VA-b->va),lxis_a_original,5);memcpy(copy+(LXIS_C_VA-b->va),lxis_c_original,5);}
    return hash_ok(copy,sizeof(copy),b->sha256);
}
/* One compare-exchange of the run's 16-byte line: its five entry bytes from `from` to `to`. */
static int lxis_swap(unsigned va,const unsigned char from[5],const unsigned char to[5]) {
    unsigned char *line=(unsigned char*)((uintptr_t)LX(va)&~(uintptr_t)15);const unsigned at=(unsigned)(LX(va)-line);
    unsigned __int128 current,desired;
    if(at+5>16) return 0;
    memcpy(&current,line,16);
    if(memcmp((unsigned char*)&current+at,from,5)) return 0;
    desired=current;memcpy((unsigned char*)&desired+at,to,5);
    return lxis_code_swap(line,current,desired);
}
/* A failed swap can still have changed the bytes if restoring RX failed.
 * Inspect the entry, restore only our jump, and independently confirm RX. */
static void lxis_restore_site(unsigned va,unsigned stub,const unsigned char *original,int *owned) {
    if(!*owned) return;
    int state=lxis_site_state(va,stub,original);
    if(state==1) {
        unsigned char jump[5];
        if(lxis_patch(jump,va,stub)) (void)lxis_swap(va,jump,original);
        state=lxis_site_state(va,stub,original);
    }
    pthread_mutex_lock(&gate_code_lock);
    const int closed=lxis_code_close(LX(va));
    pthread_mutex_unlock(&gate_code_lock);
    if(state==0 && closed) *owned=0;
}
static int lxis_install(void) {
    /* Never reuse a page: an old thread can still be executing its stub. */
    lxis_page=gate_near_page(LX(LXIS_A_VA));
    if(!lxis_page || mprotect(lxis_page,GATE_PAGE,PROT_READ|PROT_WRITE)) return 0;
    memset(lxis_page,0xcc,GATE_PAGE);
    if(!lxis_code(lxis_page,lxis_page) || mprotect(lxis_page,GATE_PAGE,PROT_READ|PROT_EXEC)) return 0;
    unsigned char a[5],c[5];
    if(!lxis_patch(a,LXIS_A_VA,LXIS_STUB_A) || !lxis_patch(c,LXIS_C_VA,LXIS_STUB_C)) return 0;
    lxis_own_a=1;
    if(!lxis_swap(LXIS_A_VA,lxis_a_original,a)) goto failed;
    lxis_own_c=1;
    if(!lxis_swap(LXIS_C_VA,lxis_c_original,c)) goto failed;
    return 1;
failed:
    lxis_restore_site(LXIS_A_VA,LXIS_STUB_A,lxis_a_original,&lxis_own_a);
    lxis_restore_site(LXIS_C_VA,LXIS_STUB_C,lxis_c_original,&lxis_own_c);
    return 0;
}
/* Before the first admitted frame, on the MoveWorker. */
static int lxis_arm(void) {
    if(lxis_installed) return stats.active;
    if(lxis_cleanup_pending()) {lxis_stage="cleanup_pending";return 0;}
    if(!stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) {lxis_stage="guard";return 0;}
    if(lxis_state()!=0) {lxis_stage="entry";return 0;}
    if(!lxis_pins()) {lxis_stage="pins";return 0;}
    lxis_restored=0;
    if(!lxis_install()) {lxis_stage=lxis_cleanup_pending()?"cleanup_pending":"install";return 0;}
    lxis_installed=1;lxis_restored=0;lxis_stage="armed";return 1;
}
static int lxis_check(void) {
    const char *why=lxis_state()!=1?"entry_changed":!lxis_page_ok()?"page_changed":!lxis_pins()?"block_changed":NULL;
    if(why) {lxis_stage=why;return 0;}
    return 1;
}
static void lxis_restore(void) {
    if(!lxis_installed && !lxis_cleanup_pending()) return;
    lxis_restore_site(LXIS_A_VA,LXIS_STUB_A,lxis_a_original,&lxis_own_a);
    lxis_restore_site(LXIS_C_VA,LXIS_STUB_C,lxis_c_original,&lxis_own_c);
    lxis_installed=0;
    if(lxis_cleanup_pending()) {lxis_restored=0;lxis_stage="cleanup_pending";}
    else {lxis_restored=1;lxis_stage="restored";} /* old pages stay executable */
}
static int lxis_status(char *out,size_t size) {
    const int n=snprintf(out,size,"tick=%llu installed=%d restored=%d state=%d pin=%d page=%d stage=%s cleanup=%d%d",(unsigned long long)stats.issued,
        lxis_installed,lxis_restored,lxis_state(),lxis_pins(),lxis_page_ok(),lxis_stage,lxis_own_a,lxis_own_c);
    return n>0 && (size_t)n<size?n:0;
}
static int lua_ik_step(lua_State *L) {p_lua_pushnumber(L,lxis_arm());return 1;}
static int lua_ik_step_status(lua_State *L) {
    char out[200];const int n=accept_ui()?lxis_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
