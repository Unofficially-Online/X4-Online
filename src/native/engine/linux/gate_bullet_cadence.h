/* native_gate_bullet_cadence on the Linux gate (Windows: lockstep_bullet_cadence.h, the same modes, status text and
 * stop reason 94): every bullet is hit-tested every frame, independent of the camera. Included by gate_linux.c.
 *
 * U::Bullet::UpdateFlight on Linux is 0x1948840 (Windows 0xd4f240). After a hit test it schedules the next one: the
 * view-cache dirty word's bit 0x40000 (byte +0x11e bit 2 of the bullet's cache entry, index from 0x12021e0; Windows
 * dword +0x11c) set means interval 0 (0x194894a); clear, it jumps at 0x1948944 (je rel32) to 0x19493a0, which asks
 * the camera's distance (0x1c98690) and below 2.5e7 (5 km) takes interval 0 as well, else 0.1 s plus a FloatState(0.1)
 * draw on the thread's word (0x1a491c0) -- Windows' block 0xd503ea, with the branch the other way round (Windows jne
 * to the interval-0 path at 0xd50414). 'every' replaces the je with a 6-byte nop (66 0f 1f 44 00 00): interval 0 for
 * every bullet, no draw, as Windows' jne -> jmp; one 16-byte line, swapped by CAS, re-checked every gate tick, restored
 * on stop; a foreign value is never overwritten. 'observe' installs nothing. Windows' per-frame counters come from its
 * bullet module's Move wrapper, which the Linux gate does not have: they print 0, the camera 'none'. Pins: the
 * function's head, the scheduling block with the original je, and the distance block. */
#define LXBC_STOP_REASON 94u
#define LXBC_SITE_VA 0x1948944u
static const unsigned char lxbc_original[6]={0x0f,0x84,0x56,0x0a,0x00,0x00};  /* je 0x19493a0 */
static const unsigned char lxbc_patched[6]={0x66,0x0f,0x1f,0x44,0x00,0x00};   /* nop word [rax+rax] */
static const char *const lxbc_pins[]={"bullet_cadence_head","bullet_cadence_block","bullet_cadence_distance"};
enum {LXBC_OFF,LXBC_EVERY,LXBC_OBSERVE};
static int lxbc_mode,lxbc_installed,lxbc_restored,lxbc_patched_now;
static volatile int64_t lxbc_failures;
static const char *lxbc_stage="idle";
static uint16_t lxbc_site(void) {uint16_t v=0;gate_read_local(LX(LXBC_SITE_VA),&v,2);return v;}
/* 0 original, 1 ours, -1 anything else. */
static int lxbc_state(void) {
    const unsigned char *at=LX(LXBC_SITE_VA);
    return !memcmp(at,lxbc_original,6)?0:!memcmp(at,lxbc_patched,6)?1:-1;
}
/* The pins, the block's read with the original je in place of a patched one. */
static int lxbc_pins_ok(void) {
    for(unsigned i=0;i<sizeof(lxbc_pins)/sizeof(*lxbc_pins);i++) {
        const lx_site *pin=site_named(lxbc_pins[i]);unsigned char copy[0x100];
        if(!pin || pin->length>sizeof(copy)) return 0;
        memcpy(copy,LX(pin->va),pin->length);
        if(lxbc_state()==1 && LXBC_SITE_VA>=pin->va && LXBC_SITE_VA+6<=pin->va+pin->length) memcpy(copy+(LXBC_SITE_VA-pin->va),lxbc_original,6);
        if(!hash_ok(copy,pin->length,pin->sha256)) return 0;
    }
    return 1;
}
static int lxbc_line(int ours,unsigned __int128 *line) {
    unsigned char *at=LX(LXBC_SITE_VA),*block=(unsigned char*)((uintptr_t)at&~(uintptr_t)15);const unsigned off=(unsigned)(at-block);
    if(off+6>16) return 0;
    memcpy(line,block,16);memcpy((unsigned char*)line+off,ours?lxbc_patched:lxbc_original,6);
    return 1;
}
static int lxbc_swap(int to_ours) {
    unsigned __int128 from,to;
    if(!lxbc_line(!to_ours,&from) || !lxbc_line(to_ours,&to)) return 0;
    return gate_code_swap128((unsigned char*)((uintptr_t)LX(LXBC_SITE_VA)&~(uintptr_t)15),from,to);
}
static int lxbc_arm(int mode) {
    if(lxbc_installed) return stats.active && mode==lxbc_mode;   /* an identical rerun after a loading-UI reload */
    if((mode!=LXBC_EVERY && mode!=LXBC_OBSERVE) || !stats.active || (boot_mode && boot_phase) || stats.issued ||
       GetCurrentThreadId()!=ui_thread) {lxbc_stage="guard";return 0;}
    if(!lxbc_pins_ok()) {lxbc_stage="pins";return 0;}
    if(lxbc_state()!=0) {lxbc_stage="site";return 0;}
    lxbc_restored=0;lxbc_failures=0;
    if(mode==LXBC_EVERY) {
        if(!lxbc_swap(1)) {lxbc_stage="cas";return 0;}
        lxbc_patched_now=1;
    }
    lxbc_mode=mode;lxbc_installed=1;lxbc_stage="armed";return 1;
}
/* The tick check (stop 94 on a failure). */
static int lxbc_check(void) {
    if(!lxbc_installed) return 1;
    if(__atomic_load_n(&lxbc_failures,__ATOMIC_SEQ_CST)) return 0;
    const int state=lxbc_state();
    const char *why=(lxbc_patched_now && state!=1) || (!lxbc_patched_now && state!=0)?"site_changed":!lxbc_pins_ok()?"block_changed":NULL;
    if(why) {lxbc_stage=why;__atomic_add_fetch(&lxbc_failures,1,__ATOMIC_SEQ_CST);return 0;}
    return 1;
}
static void lxbc_restore(void) {
    if(!lxbc_installed) return;
    if(lxbc_patched_now) {
        if(lxbc_state()!=1 || !lxbc_swap(0)) return;   /* a foreign value stays */
        lxbc_patched_now=0;
    }
    lxbc_restored=1;lxbc_installed=0;lxbc_stage="restored";
}
static int lxbc_status(char *out,size_t cap) {
    const int n=snprintf(out,cap,"mode=%s armed=%d installed=%d site=%04x frame=%llu "
        "fixture=0/0/0/0 other=0/0/0/0 total_fixture=0/0/0/0 total_other=0/0/0/0 first_later=0 unreadable=0 camera=none "
        "restored=%d stage=%s failures=%lld",
        lxbc_mode==LXBC_EVERY?"every":lxbc_mode==LXBC_OBSERVE?"observe":"off",lxbc_installed,lxbc_installed,lxbc_site(),
        (unsigned long long)stats.issued,lxbc_restored,lxbc_stage,(long long)lxbc_failures);
    return n>0 && (size_t)n<cap?n:0;
}
static int lua_bullet_cadence(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);
    const int m=mode && length==5 && !memcmp(mode,"every",5)?LXBC_EVERY:mode && length==7 && !memcmp(mode,"observe",7)?LXBC_OBSERVE:LXBC_OFF;
    p_lua_pushnumber(L,accept_ui() && m!=LXBC_OFF && lxbc_arm(m));return 1;
}
static int lua_bullet_cadence_status(lua_State *L) {
    char out[768];const int n=accept_ui()?lxbc_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
