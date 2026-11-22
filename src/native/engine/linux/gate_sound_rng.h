/* native_gate_sound_rng = 'split' on the Linux gate (Windows' region camera group 'sound_rng', lockstep_region_camera.h;
 * armed the same way, gate.region_camera('sound_rng'), without view points here): a sound start's presentation draws (its
 * pitch and its sample choice) come from a private word, never from the calling thread's simulation word. Included by
 * gate_linux.c before gate_mining_camera.h, whose gate.region_camera arms it.
 *
 * Why: on Windows every sound start passes XSfx 0x14eed60, which draws at two calls, FloatState (0x14eefdf) and the sample
 * choice 0x14f2550 (0x14eeee5, Random::I32 inside); 'split' makes both draw from private words. The Linux gate left them on
 * the thread's word, so a script that starts a sound drew more there: W46 against lx85a (today's defaults, ticks 1..30), the
 * fixture fighters' fight.attack.object.fighter scopes 9 draws on Windows, 11 on Linux (ticks 22..30, every following draw
 * of the scope moved); W49, Windows without sound_rng and ship_sound_rng, 11 as Linux.
 *
 * Linux site: the sound start 0x1a62520 (65 callers: Windows' 0x14eed60 with its one caller merged). The pitch is
 * Random::FloatState 0x1a491c0 at 0x1a62e8e (then x2 and the base, as Windows' mulss/addss), the sample choice is inlined:
 * Random::I32 0x1a49160 at 0x1a63a4d and 0x1a63ae4. Both functions step the thread word fs:-0x2720 (Windows TLS+0x318) and
 * call the log on a bad argument (no caller keeps a register across them). The call at 0x1a62e8e and the one at 0x1a63a4d
 * cross a 16-byte line, so the instruction before each (mov [rbp-0xb8],rax / r8, whole within its line) becomes a jmp rel32
 * to a stub that runs it, calls the wrapper and jumps back after the call (no branch leads into the bytes passed over); the
 * call at 0x1a63ae4 is retargeted at a stub jmp [rip]. The wrappers set the thread word to a private word seeded from a call
 * counter (never the simulation's), run the original and put the caller's word back. Pinned (sound_rng_pitch,
 * sound_rng_choice_a, sound_rng_choice_b, build_rng_float_range for FloatState, sound_rng_i32); the sites are checked by state. Before the first
 * admitted frame; every gate tick re-checks the sites and the page (stop reason 217); stop restores. Windows needs nothing. */
#define LXSND_STOP_REASON 217u
#define LXSND_FLOAT_VA 0x1a491c0u
#define LXSND_I32_VA 0x1a49160u
#define LXSND_SITES 3u
typedef float (*lxsnd_float_fn)(float);
typedef int (*lxsnd_i32_fn)(int);
/* 0: the pitch (jmp at the mov before the FloatState call), 1: choice A (jmp at the mov before the I32 call), 2: choice B
 * (the I32 call retargeted). */
static const struct {uint32_t va,back;unsigned char original[5];unsigned stub;} lxsnd_sites[LXSND_SITES]={
    {0x1a62e87u,0x1a62e93u,{0x48,0x89,0x85,0x48,0xff},0x00u},
    {0x1a63a46u,0x1a63a52u,{0x4c,0x89,0x85,0x48,0xff},0x20u},
    {0x1a63ae4u,0x1a63ae9u,{0xe8,0x77,0x56,0xfe,0xff},0x50u}};
static const char *const lxsnd_pin_names[]={"sound_rng_pitch","sound_rng_pitch_after","sound_rng_choice_a","sound_rng_choice_a_after",
                                           "sound_rng_choice_b","build_rng_float_range","sound_rng_i32"};
static unsigned char *lxsnd_page;
static int lxsnd_installed,lxsnd_restored;
static const char *lxsnd_stage="idle";
static volatile uint64_t lxsnd_float_calls,lxsnd_choice_calls,lxsnd_counter,lxsnd_failures;
static lxsnd_float_fn lxsnd_float_override;   /* tests */
static lxsnd_i32_fn lxsnd_i32_override;
static uint64_t lxsnd_private(void) {return lockstep_mix(0x534f554e44524e47ull^__atomic_add_fetch(&lxsnd_counter,1,__ATOMIC_SEQ_CST));}
static float lxsnd_float(float bound) {
    uint64_t *word=gate_tls_rng(gate_tls_self());
    const lxsnd_float_fn original=lxsnd_float_override?lxsnd_float_override:(lxsnd_float_fn)(void*)LX(LXSND_FLOAT_VA);
    __atomic_add_fetch(&lxsnd_float_calls,1,__ATOMIC_SEQ_CST);
    if(!word) {__atomic_add_fetch(&lxsnd_failures,1,__ATOMIC_SEQ_CST);return original(bound);}
    const uint64_t saved=*word;*word=lxsnd_private();
    const float r=original(bound);
    *word=saved;return r;
}
static int lxsnd_i32(int n) {
    uint64_t *word=gate_tls_rng(gate_tls_self());
    const lxsnd_i32_fn original=lxsnd_i32_override?lxsnd_i32_override:(lxsnd_i32_fn)(void*)LX(LXSND_I32_VA);
    __atomic_add_fetch(&lxsnd_choice_calls,1,__ATOMIC_SEQ_CST);
    if(!word) {__atomic_add_fetch(&lxsnd_failures,1,__ATOMIC_SEQ_CST);return original(n);}
    const uint64_t saved=*word;*word=lxsnd_private();
    const int r=original(n);
    *word=saved;return r;
}
static int lxsnd_rel(const unsigned char *from_end,const void *to,int32_t *out) {
    const intptr_t d=(intptr_t)to-(intptr_t)from_end;
    if(d!=(intptr_t)(int32_t)d) return 0;
    *out=(int32_t)d;return 1;
}
/* The page: stub 0 at +0x00 and stub 1 at +0x20 (the mov, call [rip+slot], jmp back), the wrappers' addresses at +0x40
 * and +0x48, stub 2 at +0x50 (jmp [rip+slot]). */
static int lxsnd_code(unsigned char *out,const unsigned char *page) {
    static const unsigned char mov_rax[7]={0x48,0x89,0x85,0x48,0xff,0xff,0xff},mov_r8[7]={0x4c,0x89,0x85,0x48,0xff,0xff,0xff};
    int32_t r;memset(out,0xcc,0x60);
    for(unsigned i=0;i<2;i++) {
        unsigned char *s=out+lxsnd_sites[i].stub;const unsigned slot=i==0?0x40u:0x48u;
        memcpy(s,i==0?mov_rax:mov_r8,7);
        s[7]=0xff;s[8]=0x15;if(!lxsnd_rel(page+lxsnd_sites[i].stub+13,page+slot,&r)) return 0;memcpy(s+9,&r,4);
        s[13]=0xe9;if(!lxsnd_rel(page+lxsnd_sites[i].stub+18,LX(lxsnd_sites[i].back),&r)) return 0;memcpy(s+14,&r,4);
    }
    {const void *f=(const void*)lxsnd_float,*c=(const void*)lxsnd_i32;memcpy(out+0x40,&f,8);memcpy(out+0x48,&c,8);}
    {unsigned char *s=out+0x50;s[0]=0xff;s[1]=0x25;if(!lxsnd_rel(page+0x56,page+0x48,&r)) return 0;memcpy(s+2,&r,4);}
    return 1;
}
static int lxsnd_page_ok(void) {unsigned char want[0x60];return lxsnd_page && lxsnd_code(want,lxsnd_page) && !memcmp(lxsnd_page,want,0x60);}
static int lxsnd_patch(unsigned i,unsigned char out[5]) {
    int32_t r;
    if(!lxsnd_page) return 0;
    out[0]=i==2?0xe8:0xe9;
    if(!lxsnd_rel(LX(lxsnd_sites[i].va)+5,lxsnd_page+lxsnd_sites[i].stub,&r)) return 0;
    memcpy(out+1,&r,4);return 1;
}
static int lxsnd_site_state(unsigned i) { /* 0 native, 1 ours, -1 anything else */
    unsigned char now[5],ours[5];
    if(!gate_read_local(LX(lxsnd_sites[i].va),now,5)) return -1;
    if(!memcmp(now,lxsnd_sites[i].original,5)) return 0;
    return lxsnd_patch(i,ours) && !memcmp(now,ours,5)?1:-1;
}
static int lxsnd_state(void) {
    int all0=1,all1=1;
    for(unsigned i=0;i<LXSND_SITES;i++) {const int s=lxsnd_site_state(i);all0&=s==0;all1&=s==1;}
    return all0?0:all1?1:-1;
}
static int lxsnd_pins(void) {
    for(unsigned i=0;i<sizeof(lxsnd_pin_names)/sizeof(*lxsnd_pin_names);i++) if(!site_ok(site_named(lxsnd_pin_names[i]))) return 0;
    return lxsnd_state()>=0;
}
static int lxsnd_swap(unsigned i,const unsigned char from[5],const unsigned char to[5]) {
    unsigned char *line=(unsigned char*)((uintptr_t)LX(lxsnd_sites[i].va)&~(uintptr_t)15);const unsigned at=(unsigned)(LX(lxsnd_sites[i].va)-line);
    unsigned __int128 current,desired;
    if(at+5>16) return 0;
    memcpy(&current,line,16);
    if(memcmp((unsigned char*)&current+at,from,5)) return 0;
    desired=current;memcpy((unsigned char*)&desired+at,to,5);
    return gate_code_swap128(line,current,desired);
}
static void lxsnd_unpatch(unsigned upto) {
    unsigned char ours[5];
    for(unsigned i=0;i<upto;i++) if(lxsnd_patch(i,ours)) lxsnd_swap(i,ours,lxsnd_sites[i].original);
}
/* gate.region_camera('sound_rng'): before the first admitted frame, on the MoveWorker. */
static int lxsnd_arm(void) {
    if(lxsnd_installed) return stats.active;
    if(!stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) {lxsnd_stage="guard";return 0;}
    if(lxsnd_state()!=0) {lxsnd_stage="entry";return 0;}
    if(!lxsnd_pins()) {lxsnd_stage="pins";return 0;}
    if(!lxsnd_page) lxsnd_page=gate_near_page(LX(lxsnd_sites[0].va));
    if(!lxsnd_page || mprotect(lxsnd_page,GATE_PAGE,PROT_READ|PROT_WRITE)) {lxsnd_stage="page";return 0;}
    memset(lxsnd_page,0xcc,GATE_PAGE);
    if(!lxsnd_code(lxsnd_page,lxsnd_page) || mprotect(lxsnd_page,GATE_PAGE,PROT_READ|PROT_EXEC)) {lxsnd_stage="page";return 0;}
    lxsnd_float_calls=lxsnd_choice_calls=lxsnd_failures=0;
    for(unsigned i=0;i<LXSND_SITES;i++) {
        unsigned char to[5];
        if(!lxsnd_patch(i,to) || !lxsnd_swap(i,lxsnd_sites[i].original,to)) {lxsnd_unpatch(i);lxsnd_stage="install";return 0;}
    }
    lxsnd_installed=1;lxsnd_restored=0;lxsnd_stage="armed";return 1;
}
static int lxsnd_check(void) {
    const char *why=__atomic_load_n(&lxsnd_failures,__ATOMIC_SEQ_CST)?"failures":lxsnd_state()!=1?"site_changed":!lxsnd_page_ok()?"page_changed":
                    !lxsnd_pins()?"block_changed":NULL;
    if(why) {lxsnd_stage=why;return 0;}
    return 1;
}
static void lxsnd_restore(void) {
    if(!lxsnd_installed) return;
    if(lxsnd_state()!=1) {lxsnd_stage="restore_foreign";return;}
    lxsnd_unpatch(LXSND_SITES);
    if(lxsnd_state()==0) {lxsnd_installed=0;lxsnd_restored=1;lxsnd_stage="restored";} /* the page stays: a thread may be inside */
}
/* Windows' region camera status says sound=float calls/choice calls. */
static int lxsnd_status(char *out,size_t cap) {
    const int n=snprintf(out,cap,"sound=%llu/%llu sound_installed=%d sound_restored=%d sound_state=%d sound_stage=%s sound_failures=%llu",
        (unsigned long long)lxsnd_float_calls,(unsigned long long)lxsnd_choice_calls,lxsnd_installed,lxsnd_restored,lxsnd_state(),
        lxsnd_stage,(unsigned long long)lxsnd_failures);
    return n>0 && (size_t)n<cap?n:0;
}
