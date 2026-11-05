/* The gate exit sum in the Windows order on the Linux X4 9.00 executable (native_gate_warp_exit, Linux only). Included
 * by gate_linux.c only, after gate_warp_rng.h.
 *
 * The gate poster 0x1614ff0 (Windows 0x5ce690) builds a jump's exit from A, the ship relative to the source gate, and
 * B, the exit relative to the destination zone. A's z is not from GetTransformRelativeTo: it is the sum of two
 * extents of the poster's other gate (r12+0x58 -> +0x268, +0x278; Windows r13+0x58 -> +0x258, +0x268) and two of the
 * ship (rbx+0x58 -> +0x268, |+0x278|; Windows rdi+0x58 -> +0x258, |+0x268|). MSVC adds (a + b) + (c + |d|) (Windows
 * 0x5ceb7c..0x5ceb9d); GCC adds ((a + b) + c) + |d| (0x1615324..0x161534f). The two round apart: W26..W29 against
 * lx16..lx19 (ship 0a5d7a, tick 1862) put A.z 1 ulp apart (W 453a6a72, L 453a6a71; A.x and A.y equal), with it the
 * exit position's x (W 39e1f146, L 3ae1f146), and from 1864 the ship's pose.
 *
 * Here the Linux poster's own seven instructions run in the Windows order:
 *   movss xmm0,[rdx+0x268]; movss xmm1,[rax+0x278]; addss xmm0,[rdx+0x278]; andps xmm1,xmm4;
 *   addss xmm1,[rax+0x268]; movss [rbp-0x2a8],xmm2; addss xmm0,xmm1
 * (the native bytes as pinned, the fourth add's ModRM retargeted from xmm0 to xmm1; c + |d| is |d| + c exactly), in a
 * stub near the image that counts its runs (lock inc on its own data page: the flags are dead there, their next
 * reader is behind the call at 0x161536e) and jumps back to 0x1615353. The entry 0x1615324's 8-byte movss becomes
 * jmp rel32 + a 3-byte nop: bytes 4..11 of one 16-byte line, one compare-exchange; a thread already past the entry
 * finishes on the native bytes, which stay. No branch, rel32 call or absolute pointer in the executable leads into
 * 0x1615325..0x1615352. Pin warp_exit_sum (0x16152fc..0x1615353: the loads of rdx and rax, xmm4's mask, xmm2, the
 * sum and its store) read with the entry as found. Before the first admitted frame, on the MoveWorker; the per-tick
 * check (stop reason 146) and the restore are this file's. Windows needs nothing: its own order is the one kept. */
#define WX_STOP_REASON 146
#define LXWX_LINE (LX_WX_ENTRY_VA&~15u)
#define LXWX_SUM 0x2fu        /* 0x1615324..0x1615352 */
#define LXWX_STUB_BYTES 69u
_Static_assert(LX_WX_ENTRY_VA-LXWX_LINE+8u<=16u,"the entry's 8 bytes in one 16-byte line");
_Static_assert(LX_WX_ENTRY_VA+LXWX_SUM==LX_WX_RETURN_VA,"the sum ends at the return");
static const unsigned char lxwx_original[8]={0xf3,0x0f,0x10,0x82,0x68,0x02,0x00,0x00}; /* movss xmm0,[rdx+0x268] */
static unsigned char *lxwx_page,*lxwx_data; /* the stub (RX) and its counter (RW), both near the image */
static LONG64 wx_failures;
static const char *wx_stage="idle";
static const lx_site *lxwx_site(const char *name) {
    for(unsigned i=0;i<gate_site_count;i++) if(!strcmp(gate_sites[i].name,name)) return &gate_sites[i];
    return NULL;
}
/* jmp rel32 to `to`, nop3: the entry's patched 8 bytes. */
static void lxwx_entry(unsigned char out[8],const unsigned char *to) {
    int32_t d=(int32_t)((intptr_t)to-(intptr_t)(LX(LX_WX_ENTRY_VA)+5));
    out[0]=0xe9;memcpy(out+1,&d,4);out[5]=0x0f;out[6]=0x1f;out[7]=0x00;
}
/* 0 the native movss, 1 our jmp to the stub, -1 anything else. */
static int lxwx_state(void) {
    unsigned char b[8],want[8];
    if(!gate_read_local(LX(LX_WX_ENTRY_VA),b,8)) return -1;
    if(!memcmp(b,lxwx_original,8)) return 0;
    if(lxwx_page) {lxwx_entry(want,lxwx_page);if(!memcmp(b,want,8)) return 1;}
    return -1;
}
/* The native sum's bytes with the entry as the original. */
static int lxwx_sum(unsigned char out[LXWX_SUM]) {
    if(!gate_read_local(LX(LX_WX_ENTRY_VA),out,LXWX_SUM)) return 0;
    memcpy(out,lxwx_original,8);return 1;
}
static int lxwx_pins(void) {
    const lx_site *s=lxwx_site("warp_exit_sum");unsigned char copy[0x80];
    if(!s || s->length>sizeof(copy) || LX_WX_ENTRY_VA<s->va || LX_WX_RETURN_VA>s->va+s->length || lxwx_state()<0 ||
       !gate_read_local(LX(s->va),copy,s->length)) return 0;
    memcpy(copy+(LX_WX_ENTRY_VA-s->va),lxwx_original,8);
    return hash_ok(copy,s->length,s->sha256);
}
/* lock inc qword [data]; the sum in the Windows order; jmp [rip] -> back. */
static void lxwx_stub_bytes(unsigned char out[LXWX_STUB_BYTES],const unsigned char *page,const unsigned char *data,const unsigned char sum[LXWX_SUM],
                            const void *back) {
    int32_t d=(int32_t)((intptr_t)data-(intptr_t)(page+8));
    out[0]=0xf0;out[1]=0x48;out[2]=0xff;out[3]=0x05;memcpy(out+4,&d,4);
    memcpy(out+8,sum,24);                 /* movss xmm0,[rdx+0x268]; movss xmm1,[rax+0x278]; addss xmm0,[rdx+0x278] */
    memcpy(out+32,sum+40,3);              /* andps xmm1,xmm4 */
    memcpy(out+35,sum+24,8);out[38]=0x88; /* addss xmm1,[rax+0x268] (ModRM 0x80 reads into xmm0, 0x88 into xmm1) */
    memcpy(out+43,sum+32,8);              /* movss [rbp-0x2a8],xmm2 */
    memcpy(out+51,sum+43,4);              /* addss xmm0,xmm1 */
    out[55]=0xff;out[56]=0x25;memset(out+57,0,4);memcpy(out+61,&back,8);
}
static int lxwx_swap(const unsigned char from[8],const unsigned char to[8]) {
    unsigned char a[16],b[16];unsigned __int128 expected,desired;
    memcpy(a,LX(LXWX_LINE),16);memcpy(b,a,16);
    memcpy(a+(LX_WX_ENTRY_VA-LXWX_LINE),from,8);memcpy(b+(LX_WX_ENTRY_VA-LXWX_LINE),to,8);
    memcpy(&expected,a,16);memcpy(&desired,b,16);
    return gate_code_swap128(LX(LXWX_LINE),expected,desired);
}
static int lxwx_install(void) {
    unsigned char sum[LXWX_SUM],to[8];
    if(!lxwx_sum(sum) || sum[27]!=0x80) return 0;
    if(!lxwx_page) lxwx_page=gate_near_page(LX(LX_WX_ENTRY_VA));
    if(!lxwx_page) return 0;
    if(!lxwx_data) lxwx_data=gate_near_page(lxwx_page);
    if(!lxwx_data) return 0;
    intptr_t d=(intptr_t)lxwx_page-(intptr_t)(LX(LX_WX_ENTRY_VA)+5);if(d<INT32_MIN || d>INT32_MAX) return 0;
    intptr_t e=(intptr_t)lxwx_data-(intptr_t)(lxwx_page+8);if(e<INT32_MIN || e>INT32_MAX) return 0;
    if(mprotect(lxwx_data,GATE_PAGE,PROT_READ|PROT_WRITE)) return 0;
    memset(lxwx_data,0,GATE_PAGE);
    if(mprotect(lxwx_page,GATE_PAGE,PROT_READ|PROT_WRITE)) return 0;
    memset(lxwx_page,0xcc,GATE_PAGE);lxwx_stub_bytes(lxwx_page,lxwx_page,lxwx_data,sum,LX(LX_WX_RETURN_VA));
    if(mprotect(lxwx_page,GATE_PAGE,PROT_READ|PROT_EXEC)) return 0;
    lxwx_entry(to,lxwx_page);
    return lxwx_swap(lxwx_original,to);
}
static uint64_t lxwx_calls(void) {return lxwx_data?__atomic_load_n((uint64_t*)(void*)lxwx_data,__ATOMIC_RELAXED):0;}
static int lxwx_owned(void) {
    if(!wx_installed) return 1;
    unsigned char sum[LXWX_SUM],want[LXWX_STUB_BYTES];
    if(lxwx_state()!=1 || !lxwx_sum(sum)) return 0;
    lxwx_stub_bytes(want,lxwx_page,lxwx_data,sum,LX(LX_WX_RETURN_VA));
    return !memcmp(lxwx_page,want,LXWX_STUB_BYTES);
}
/* Before the first admitted frame, on the MoveWorker. */
static int wx_arm(void) {
    if(wx_installed) return stats.active; /* Identical rerun after a loading-UI reload. */
    if(!stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(lxwx_state()!=0) {wx_stage="entry";return 0;}
    if(!lxwx_pins()) {wx_stage="pins";return 0;}
    wx_failures=0;
    if(!lxwx_install()) {wx_stage="install";return 0;}
    wx_installed=1;wx_restored=0;wx_stage="armed";return 1;
}
/* Main-thread tick check; any failure stops the gate with reason 146. */
static int wx_check(void) {
    const char *why=!lxwx_owned()?"entry_changed":!lxwx_pins()?"block_changed":NULL;
    if(why) {wx_stage=why;InterlockedIncrement64(&wx_failures);return 0;}
    return 1;
}
static void wx_restore(void) {
    if(!wx_installed) return;
    int state=lxwx_state();
    if(state==1) {unsigned char from[8];lxwx_entry(from,lxwx_page);if(!lxwx_swap(from,lxwx_original)) return;}
    else if(state!=0) return; /* a foreign value stays */
    wx_installed=0;wx_restored=1; /* the stub page stays: a thread may still be inside it */
}
static int wx_status(char *out,size_t size) {
    int n=snprintf(out,size,"tick=%llu installed=%d restored=%d calls=%llu failures=%lld state=%d pin=%d stage=%s",
        (unsigned long long)stats.issued,wx_installed,wx_restored,(unsigned long long)lxwx_calls(),(long long)wx_failures,lxwx_state(),lxwx_pins(),wx_stage);
    return n>0 && (size_t)n<size?n:0;
}
static int lua_warp_exit(lua_State *L) {p_lua_pushnumber(L,wx_arm());return 1;}
static int lua_warp_exit_status(lua_State *L) {
    char out[256];int n=accept_ui()?wx_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
