/* native_gate_ik_wrap (Linux gate only, default off): a skeleton joint's wrapped target in the Windows order. Included
 * by gate_linux.c only.
 *
 * A joint whose angle wraps (+0x21 set: a weapon's gimbal, U::IKValue's owner) takes its new target a (an atan2 of the
 * aim) through t1 = fmod(a, 2pi) (+2pi when negative), t2 = fmod((t1 - value) + pi, 2pi) (+2pi when negative), and
 * stores target = (t2 + value) - pi on Windows (0xe1f840: addss value, subss pi) but (t2 - pi) + value on Linux
 * (0x1565b00's tail at 0x15661fd: subss pi, then addss value). t2 is near pi, so Windows rounds t2 + value at pi's ulp
 * (2^-22): every Windows target sits on that grid, Linux's keeps the low bits. lx52a against W36l: every joint equal
 * through tick 14 (value 0 there, where the two orders agree), then from tick 15 every gun's gimbal target apart
 * (3c4e3000 against 3c4e2fad, ...), the values and rates after it, then the bullets that leave along the gimbals (the
 * tick-28 hit). The Windows order reproduces Windows' targets from the rows (test).
 *
 * Here the tail's 11 bytes, subss xmm3,xmm6; movaps xmm0,xmm3; addss xmm0,xmm7 (xmm3 = t2, xmm6 = pi, xmm7 = value),
 * become addss xmm3,xmm7; subss xmm3,xmm6; movaps xmm0,xmm3, the same length: xmm0 = (t2 + value) - pi as on Windows;
 * xmm3 is dead after it (both paths from the jump target 0x1565b3d write it before any read). Every way in enters at
 * 0x15661fd (from 0x15661f3 and 0x1566673); nothing branches into the rest. The bytes span two 16-byte lines (three in
 * 0x15661f0, eight in 0x1566200), so the rewrite is two compare-exchanges; between them a thread there would compute
 * subss xmm3,xmm7 or addss xmm3,xmm6 first, valid code with a wrong target for that one call, and the rewrite happens
 * before the snapshot load, so no session value can see it. Pinned (ik_wrap_tail 0x15661c0..0x1566220, read with the
 * tail as found). Before the first admitted frame, on the MoveWorker; the per-tick check (stop reason 209) and the
 * restore are this file's. Windows needs nothing. */
#define LXIW_STOP_REASON 209u
#define LXIW_VA 0x15661fdu
#define LXIW_PIN_VA 0x15661c0u
#define LXIW_PIN 0x60u
static const unsigned char lxiw_original[11]={0xf3,0x0f,0x5c,0xde,0x0f,0x28,0xc3,0xf3,0x0f,0x58,0xc7};
static const unsigned char lxiw_windows[11]={0xf3,0x0f,0x58,0xdf,0xf3,0x0f,0x5c,0xde,0x0f,0x28,0xc3};
static int lxiw_installed,lxiw_restored;
static const char *lxiw_stage="idle";
static int lxiw_state(void) { /* 0 native, 1 ours, -1 anything else */
    unsigned char now[11];
    if(!gate_read_local(LX(LXIW_VA),now,11)) return -1;
    return !memcmp(now,lxiw_original,11)?0:!memcmp(now,lxiw_windows,11)?1:-1;
}
static int lxiw_pins(void) {
    const lx_site *s=site_named("ik_wrap_tail");unsigned char copy[LXIW_PIN];
    if(!s || s->va!=LXIW_PIN_VA || s->length!=LXIW_PIN || lxiw_state()<0 || !gate_read_local(LX(s->va),copy,LXIW_PIN)) return 0;
    if(lxiw_state()==1) memcpy(copy+(LXIW_VA-LXIW_PIN_VA),lxiw_original,11);
    return hash_ok(copy,LXIW_PIN,s->sha256);
}
/* The tail's bytes from `from` to `to`: one compare-exchange per 16-byte line it touches, the later line first. */
static int lxiw_swap(const unsigned char from[11],const unsigned char to[11]) {
    unsigned char *first=(unsigned char*)((uintptr_t)LX(LXIW_VA)&~(uintptr_t)15),*second=first+16;
    const unsigned at=(unsigned)(LX(LXIW_VA)-first),head=16u-at; /* bytes in the first line */
    unsigned __int128 a,b,a2,b2;
    memcpy(&a,first,16);memcpy(&b,second,16);
    if(memcmp((unsigned char*)&a+at,from,head) || memcmp(&b,from+head,11u-head)) return 0;
    a2=a;b2=b;memcpy((unsigned char*)&a2+at,to,head);memcpy(&b2,to+head,11u-head);
    if(!gate_code_swap128(second,b,b2)) return 0;
    if(!gate_code_swap128(first,a,a2)) {gate_code_swap128(second,b2,b);return 0;}
    return 1;
}
/* Before the first admitted frame, on the MoveWorker. */
static int lxiw_arm(void) {
    if(lxiw_installed) return stats.active;
    if(!stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) {lxiw_stage="guard";return 0;}
    if(lxiw_state()!=0) {lxiw_stage="entry";return 0;}
    if(!lxiw_pins()) {lxiw_stage="pins";return 0;}
    if(!lxiw_swap(lxiw_original,lxiw_windows)) {lxiw_stage="install";return 0;}
    lxiw_installed=1;lxiw_restored=0;lxiw_stage="armed";return 1;
}
static int lxiw_check(void) {
    const char *why=lxiw_state()!=1?"tail_changed":!lxiw_pins()?"block_changed":NULL;
    if(why) {lxiw_stage=why;return 0;}
    return 1;
}
static void lxiw_restore(void) {
    if(!lxiw_installed) return;
    if(lxiw_state()!=1) {lxiw_stage="restore_foreign";return;}
    if(lxiw_swap(lxiw_windows,lxiw_original)) {lxiw_installed=0;lxiw_restored=1;lxiw_stage="restored";}
}
static int lxiw_status(char *out,size_t size) {
    const int n=snprintf(out,size,"tick=%llu installed=%d restored=%d state=%d pin=%d stage=%s",(unsigned long long)stats.issued,
        lxiw_installed,lxiw_restored,lxiw_state(),lxiw_pins(),lxiw_stage);
    return n>0 && (size_t)n<size?n:0;
}
static int lua_ik_wrap(lua_State *L) {p_lua_pushnumber(L,lxiw_arm());return 1;}
static int lua_ik_wrap_status(lua_State *L) {
    char out[200];const int n=accept_ui()?lxiw_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
