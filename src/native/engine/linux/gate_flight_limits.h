/* The ship's flight limits in the Windows order on the Linux X4 9.00 executable (native_gate_flight_limits, Linux
 * only). Included by gate_linux.c only, after gate_warp_exit.h.
 *
 * The engine-parameter computation (Windows 0x1406dc610, Linux 0x148cb50, under the flight-parameter builder 0xea1e80
 * / 0x14378c0) ends a ship's limits with seven words of the destination: +0x0..+0xc, the pair +0x18/+0x1c and +0x20,
 * each max(dest, max(k, |v|) / m) of a vector v, a floor k and the divisor m. MSVC hoisted the division: r = 1.0f / m
 * once (divss xmm6,xmm7 with xmm6 = 1.0, Windows 0x1406dcd14), then max(k, |v|) * r per word. GCC divides each word
 * (0x148d812..0x148d931: five divss and one divps by (m, m, 1, 1)). The two round apart: a battle fixture's limits at
 * the Agent's +0x340 were one ulp apart (W 426d8001 against L 426d8000 for 59.375; 208.932, 195.263, 195.874 likewise),
 * and with them the first steering update's velocity (W35 against lx38a, tick 14).
 *
 * Here the Linux block's own instructions run with xmm1 = 1.0f / m (m as loaded from [rsp]; xmm2 is loaded next, so it
 * is free for the division) and every division by xmm1 a multiplication (divss 0x5e -> mulss 0x59, divps -> mulps; the
 * divps lanes 2 and 3 are 1.0 and never stored), in a stub near the image that counts its runs (lock inc on its own
 * data page: the flags are dead there, the block follows a call and sets none before its own compares) and jumps back
 * to the block's own target 0x148d05e. xmm1 is dead after the block (0x148d05e and 0x148ce60 write xmm0 and reach a
 * call before any read of xmm1; the other exit returns). The entry 0x148d812's 8-byte movss xmm3,[rip+...] becomes
 * jmp rel32 + a 3-byte nop: bytes 2..9 of one 16-byte line, one compare-exchange; a thread already past the entry
 * finishes on the native bytes, which stay. No branch in the executable leads into 0x148d813..0x148d936. Pin
 * flight_limits_block (0x148d812..0x148d936) read with the entry as found. Before the first admitted frame, on the
 * MoveWorker; the per-tick check (stop reason 189) and the restore are this file's. Windows needs nothing. */
#define FL_STOP_REASON 189
#define LXFL_LINE (LX_FL_ENTRY_VA&~15u)
#define LXFL_BLOCK 0x125u         /* 0x148d812..0x148d936, the final jmp included */
#define LXFL_BODY 0x120u          /* without the final jmp */
#define LXFL_STUB_BYTES (8u+8u+5u+8u+4u+3u+(LXFL_BODY-0x0du)+14u)
_Static_assert(LX_FL_ENTRY_VA-LXFL_LINE+8u<=16u,"the entry's 8 bytes in one 16-byte line");
static const unsigned char lxfl_original[8]={0xf3,0x0f,0x10,0x1d,0x86,0x70,0x99,0x01}; /* movss xmm3,[rip+0x1997086] */
/* The block's RIP-relative loads after the entry (offset, length, displacement offset) and its divisions (offset of
 * the opcode byte 0x5e). */
static const struct {uint16_t at;uint8_t length,disp;} lxfl_rip[4]={{0x1f,8,4},{0x56,8,4},{0xd8,7,3},{0x103,8,4}};
static const uint16_t lxfl_div[6]={0x30,0x60,0x8b,0xb2,0xe0,0x114}; /* the divss at +0x2e,+0x5e,+0x89,+0xb0,+0x112 and the divps at +0xdf */
static unsigned char *lxfl_page,*lxfl_data; /* the stub (RX) and its counter and 1.0f (RW), both near the image */
static LONG64 fl_failures;
static const char *fl_stage="idle";
static const lx_site *lxfl_site(const char *name) {
    for(unsigned i=0;i<gate_site_count;i++) if(!strcmp(gate_sites[i].name,name)) return &gate_sites[i];
    return NULL;
}
/* jmp rel32 to `to`, nop3: the entry's patched 8 bytes. */
static void lxfl_entry(unsigned char out[8],const unsigned char *to) {
    int32_t d=(int32_t)((intptr_t)to-(intptr_t)(LX(LX_FL_ENTRY_VA)+5));
    out[0]=0xe9;memcpy(out+1,&d,4);out[5]=0x0f;out[6]=0x1f;out[7]=0x00;
}
/* 0 the native movss, 1 our jmp to the stub, -1 anything else. */
static int lxfl_state(void) {
    unsigned char b[8],want[8];
    if(!gate_read_local(LX(LX_FL_ENTRY_VA),b,8)) return -1;
    if(!memcmp(b,lxfl_original,8)) return 0;
    if(lxfl_page) {lxfl_entry(want,lxfl_page);if(!memcmp(b,want,8)) return 1;}
    return -1;
}
/* The native block's bytes with the entry as the original. */
static int lxfl_block(unsigned char out[LXFL_BLOCK]) {
    if(!gate_read_local(LX(LX_FL_ENTRY_VA),out,LXFL_BLOCK)) return 0;
    memcpy(out,lxfl_original,8);return 1;
}
static int lxfl_pins(void) {
    const lx_site *s=lxfl_site("flight_limits_block");unsigned char copy[LXFL_BLOCK];
    if(!s || s->va!=LX_FL_ENTRY_VA || s->length!=LXFL_BLOCK || lxfl_state()<0 || !lxfl_block(copy)) return 0;
    return hash_ok(copy,s->length,s->sha256);
}
/* The stub for the block `block` (native bytes, its own address `at`) on `page`, with its data page `data`:
 *   lock inc qword [data]; movss xmm3,[k]; movss xmm1,[rsp]; movss xmm2,[data+8] (1.0f); divss xmm2,xmm1;
 *   movaps xmm1,xmm2; the block from its third instruction with every division by xmm1 a multiplication and its
 *   RIP-relative loads retargeted; jmp [rip] -> back. 0 when a displacement does not fit. */
static int lxfl_stub_bytes(unsigned char out[LXFL_STUB_BYTES],const unsigned char *page,const unsigned char *data,
                           const unsigned char block[LXFL_BLOCK],const unsigned char *at,const void *back) {
    unsigned n=0;int32_t d;
#define REL(target,end) (d=(int32_t)((intptr_t)(target)-(intptr_t)(end)),(intptr_t)(target)-(intptr_t)(end)==(intptr_t)d)
    out[n++]=0xf0;out[n++]=0x48;out[n++]=0xff;out[n++]=0x05;if(!REL(data,page+n+4)) return 0;memcpy(out+n,&d,4);n+=4;
    {int32_t native;memcpy(&native,block+4,4);const unsigned char *target=at+8+native; /* movss xmm3,[rip+...] */
     memcpy(out+n,block,4);if(!REL(target,page+n+8)) return 0;memcpy(out+n+4,&d,4);n+=8;}
    memcpy(out+n,block+8,5);n+=5;                                                    /* movss xmm1,[rsp] */
    out[n++]=0xf3;out[n++]=0x0f;out[n++]=0x10;out[n++]=0x15;if(!REL(data+8,page+n+4)) return 0;memcpy(out+n,&d,4);n+=4;
    out[n++]=0xf3;out[n++]=0x0f;out[n++]=0x5e;out[n++]=0xd1;                         /* divss xmm2,xmm1 */
    out[n++]=0x0f;out[n++]=0x28;out[n++]=0xca;                                       /* movaps xmm1,xmm2 */
    const unsigned base=n-0x0du; /* stub offset of block offset 0x0d onwards */
    memcpy(out+n,block+0x0d,LXFL_BODY-0x0du);n+=LXFL_BODY-0x0du;
    for(unsigned i=0;i<6;i++) {if(out[base+lxfl_div[i]]!=0x5e) return 0;out[base+lxfl_div[i]]=0x59;}
    for(unsigned i=0;i<4;i++) {
        const unsigned o=lxfl_rip[i].at;int32_t native;memcpy(&native,block+o+lxfl_rip[i].disp,4);
        const unsigned char *target=at+o+lxfl_rip[i].length+native;
        if(!REL(target,page+base+o+lxfl_rip[i].length)) return 0;
        memcpy(out+base+o+lxfl_rip[i].disp,&d,4);
    }
    out[n++]=0xff;out[n++]=0x25;memset(out+n,0,4);n+=4;memcpy(out+n,&back,8);n+=8;
#undef REL
    return n==LXFL_STUB_BYTES;
}
static int lxfl_swap(const unsigned char from[8],const unsigned char to[8]) {
    unsigned char a[16],b[16];unsigned __int128 expected,desired;
    memcpy(a,LX(LXFL_LINE),16);memcpy(b,a,16);
    memcpy(a+(LX_FL_ENTRY_VA-LXFL_LINE),from,8);memcpy(b+(LX_FL_ENTRY_VA-LXFL_LINE),to,8);
    memcpy(&expected,a,16);memcpy(&desired,b,16);
    return gate_code_swap128(LX(LXFL_LINE),expected,desired);
}
static int lxfl_install(void) {
    unsigned char block[LXFL_BLOCK],to[8];
    if(!lxfl_block(block)) return 0;
    if(!lxfl_page) lxfl_page=gate_near_page(LX(LX_FL_ENTRY_VA));
    if(!lxfl_page) return 0;
    if(!lxfl_data) lxfl_data=gate_near_page(lxfl_page);
    if(!lxfl_data) return 0;
    intptr_t d=(intptr_t)lxfl_page-(intptr_t)(LX(LX_FL_ENTRY_VA)+5);if(d<INT32_MIN || d>INT32_MAX) return 0;
    if(mprotect(lxfl_data,GATE_PAGE,PROT_READ|PROT_WRITE)) return 0;
    memset(lxfl_data,0,GATE_PAGE);{const float one=1.0f;memcpy(lxfl_data+8,&one,4);}
    if(mprotect(lxfl_page,GATE_PAGE,PROT_READ|PROT_WRITE)) return 0;
    memset(lxfl_page,0xcc,GATE_PAGE);
    if(!lxfl_stub_bytes(lxfl_page,lxfl_page,lxfl_data,block,LX(LX_FL_ENTRY_VA),LX(LX_FL_RETURN_VA))) return 0;
    if(mprotect(lxfl_page,GATE_PAGE,PROT_READ|PROT_EXEC)) return 0;
    lxfl_entry(to,lxfl_page);
    return lxfl_swap(lxfl_original,to);
}
static uint64_t lxfl_calls(void) {return lxfl_data?__atomic_load_n((uint64_t*)(void*)lxfl_data,__ATOMIC_RELAXED):0;}
static int lxfl_owned(void) {
    if(!fl_installed) return 1;
    unsigned char block[LXFL_BLOCK],want[LXFL_STUB_BYTES];float one;
    if(lxfl_state()!=1 || !lxfl_block(block) ||
       !lxfl_stub_bytes(want,lxfl_page,lxfl_data,block,LX(LX_FL_ENTRY_VA),LX(LX_FL_RETURN_VA))) return 0;
    memcpy(&one,lxfl_data+8,4);
    return one==1.0f && !memcmp(lxfl_page,want,LXFL_STUB_BYTES);
}
/* The second site, the thrust terms before it (Windows 0x1406db200, Linux 0x14811c0): the limits' first four words
 * are in / S (times an engine factor and, for +0x8/+0xc, the ship's +0x398 holder's +0x50 or 1.0) with
 * S = (e[0x914] * v + a) + b. MSVC takes r = 1.0f / S once (Windows 0x1406db3e3) and computes (r * in0) * e918,
 * (r * in1) * e91c, (sp * e920) * (r * in2) and (sp * e924) * (r * in3) (Windows 0x1406db3ed..0x1406db465); GCC divides
 * each word and multiplies left to right ((in2 / S) * e920) * sp (0x148136b..0x14813d8, the null holder's 1.0 at
 * 0x1481520 rejoining at 0x14813c3). These were the fixture's one-ulp limits (W35 against lx40a: 208.932, 195.263,
 * 59.375, 195.874 at the Agent's +0x340). The Linux engine block's fields sit 0x10 above Windows' (e914 is Windows'
 * 0x904), the input vector is r13 (Windows rsi), the destination rbp (rdi).
 * The stub (assembled from tools-free bytes below) replays the displaced store movss [rbp+0x34],xmm11, counts, forms S
 * in the native order and the four words in Windows', then jumps to 0x14813dd. At the entry 0x1481365 the live
 * registers are as the native block finds them: xmm1 v, xmm6 a, xmm10 b, xmm5/xmm9/xmm4/xmm2 the inputs, rax the
 * holder, rbx the engine block, rbp the destination; after it xmm2..xmm5 are reloaded before any read and xmm9,
 * xmm11, xmm12 are dead (0x14813dd.. until the call at 0x148149d). The only jump into 0x1481366..0x14813dc is the null
 * path's (0x1481528 -> 0x14813c3), which starts inside it and is no longer reached. Pin flight_thrust_block
 * (0x1481274..0x14813dc: the inputs' loads, the holder's, the sum and the words). The entry's 6 bytes become jmp rel32 +
 * nop: bytes 5..10 of one 16-byte line, one compare-exchange. */
#define LXFT_LINE (LX_FT_ENTRY_VA&~15u)
#define LXFT_PIN_VA 0x1481274u
#define LXFT_PIN 0x169u
#define LXFT_STUB_BYTES 0xbdu
_Static_assert(LX_FT_ENTRY_VA-LXFT_LINE+6u<=16u,"the thrust entry's 6 bytes in one 16-byte line");
static const unsigned char lxft_original[6]={0xf3,0x44,0x0f,0x11,0x5d,0x34}; /* movss [rbp+0x34],xmm11 */
static const unsigned char lxft_template[LXFT_STUB_BYTES]={
    0xf3,0x44,0x0f,0x11,0x5d,0x34, 0xf0,0x48,0xff,0x05,0,0,0,0, 0xf3,0x0f,0x10,0x9b,0x14,0x09,0x00,0x00, 0xf3,0x0f,0x59,0xd9,
    0xf3,0x0f,0x58,0xde, 0xf3,0x41,0x0f,0x58,0xda, 0xf3,0x44,0x0f,0x10,0x1d,0,0,0,0, 0xf3,0x44,0x0f,0x5e,0xdb,
    0x45,0x0f,0x28,0xe3, 0xf3,0x44,0x0f,0x59,0xe5, 0xf3,0x44,0x0f,0x59,0xa3,0x18,0x09,0x00,0x00, 0xf3,0x44,0x0f,0x11,0x65,0x00,
    0x45,0x0f,0x28,0xe3, 0xf3,0x45,0x0f,0x59,0xe1, 0xf3,0x44,0x0f,0x59,0xa3,0x1c,0x09,0x00,0x00, 0xf3,0x44,0x0f,0x11,0x65,0x04,
    0x48,0x85,0xc0, 0x74,0x07, 0xf3,0x0f,0x10,0x68,0x50, 0xeb,0x08, 0xf3,0x0f,0x10,0x2d,0,0,0,0,
    0xf3,0x41,0x0f,0x59,0xe3, 0x44,0x0f,0x28,0xe5, 0xf3,0x44,0x0f,0x59,0xa3,0x20,0x09,0x00,0x00, 0xf3,0x44,0x0f,0x59,0xe4,
    0xf3,0x44,0x0f,0x11,0x65,0x08, 0xf3,0x41,0x0f,0x59,0xd3, 0x44,0x0f,0x28,0xe5, 0xf3,0x44,0x0f,0x59,0xa3,0x24,0x09,0x00,0x00,
    0xf3,0x44,0x0f,0x59,0xe2, 0xf3,0x44,0x0f,0x11,0x65,0x0c, 0xff,0x25,0,0,0,0, 0,0,0,0,0,0,0,0};
/* lxft_template: displacements at +0x0a (the counter, end +0x0e), +0x28 (1.0f, end +0x2c), +0x71 (1.0f, end +0x75);
 * the way back at +0xb5. Its S bytes (+0x0e..+0x22) are the native 0x148136b..0x148137f. */
static unsigned char *lxft_page,*lxft_data;
static void lxft_entry(unsigned char out[6],const unsigned char *to) {
    int32_t d=(int32_t)((intptr_t)to-(intptr_t)(LX(LX_FT_ENTRY_VA)+5));
    out[0]=0xe9;memcpy(out+1,&d,4);out[5]=0x90;
}
static int lxft_state(void) {
    unsigned char b[6],want[6];
    if(!gate_read_local(LX(LX_FT_ENTRY_VA),b,6)) return -1;
    if(!memcmp(b,lxft_original,6)) return 0;
    if(lxft_page) {lxft_entry(want,lxft_page);if(!memcmp(b,want,6)) return 1;}
    return -1;
}
static int lxft_pins(void) {
    const lx_site *s=lxfl_site("flight_thrust_block");unsigned char copy[LXFT_PIN];
    if(!s || s->va!=LXFT_PIN_VA || s->length!=LXFT_PIN || lxft_state()<0 || !gate_read_local(LX(s->va),copy,s->length)) return 0;
    memcpy(copy+(LX_FT_ENTRY_VA-LXFT_PIN_VA),lxft_original,6);
    if(memcmp(copy+(0x148136bu-LXFT_PIN_VA),lxft_template+0x0e,0x15)) return 0; /* the sum as the stub replays it */
    return hash_ok(copy,s->length,s->sha256);
}
static int lxft_stub_bytes(unsigned char out[LXFT_STUB_BYTES],const unsigned char *page,const unsigned char *data,const void *back) {
    static const struct {uint8_t disp,end;uint8_t target;} fix[3]={{0x0a,0x0e,0},{0x28,0x2c,8},{0x71,0x75,8}};
    memcpy(out,lxft_template,LXFT_STUB_BYTES);
    for(unsigned i=0;i<3;i++) {
        const intptr_t d=(intptr_t)(data+fix[i].target)-(intptr_t)(page+fix[i].end);
        if(d<INT32_MIN || d>INT32_MAX) return 0;
        int32_t d32=(int32_t)d;memcpy(out+fix[i].disp,&d32,4);
    }
    memcpy(out+0xb5,&back,8);return 1;
}
static int lxft_swap(const unsigned char from[6],const unsigned char to[6]) {
    unsigned char a[16],b[16];unsigned __int128 expected,desired;
    memcpy(a,LX(LXFT_LINE),16);memcpy(b,a,16);
    memcpy(a+(LX_FT_ENTRY_VA-LXFT_LINE),from,6);memcpy(b+(LX_FT_ENTRY_VA-LXFT_LINE),to,6);
    memcpy(&expected,a,16);memcpy(&desired,b,16);
    return gate_code_swap128(LX(LXFT_LINE),expected,desired);
}
static int lxft_install(void) {
    unsigned char to[6];
    if(!lxft_page) lxft_page=gate_near_page(LX(LX_FT_ENTRY_VA));
    if(!lxft_page) return 0;
    if(!lxft_data) lxft_data=gate_near_page(lxft_page);
    if(!lxft_data) return 0;
    intptr_t d=(intptr_t)lxft_page-(intptr_t)(LX(LX_FT_ENTRY_VA)+5);if(d<INT32_MIN || d>INT32_MAX) return 0;
    if(mprotect(lxft_data,GATE_PAGE,PROT_READ|PROT_WRITE)) return 0;
    memset(lxft_data,0,GATE_PAGE);{const float one=1.0f;memcpy(lxft_data+8,&one,4);}
    if(mprotect(lxft_page,GATE_PAGE,PROT_READ|PROT_WRITE)) return 0;
    memset(lxft_page,0xcc,GATE_PAGE);
    if(!lxft_stub_bytes(lxft_page,lxft_page,lxft_data,LX(LX_FT_RETURN_VA))) return 0;
    if(mprotect(lxft_page,GATE_PAGE,PROT_READ|PROT_EXEC)) return 0;
    lxft_entry(to,lxft_page);
    return lxft_swap(lxft_original,to);
}
static uint64_t lxft_calls(void) {return lxft_data?__atomic_load_n((uint64_t*)(void*)lxft_data,__ATOMIC_RELAXED):0;}
static int lxft_owned(void) {
    if(!fl_installed) return 1;
    unsigned char want[LXFT_STUB_BYTES];float one;
    if(lxft_state()!=1 || !lxft_stub_bytes(want,lxft_page,lxft_data,LX(LX_FT_RETURN_VA))) return 0;
    memcpy(&one,lxft_data+8,4);
    return one==1.0f && !memcmp(lxft_page,want,LXFT_STUB_BYTES);
}
static void lxft_restore(void) {
    if(lxft_state()==1) {unsigned char from[6];lxft_entry(from,lxft_page);(void)lxft_swap(from,lxft_original);}
}
/* Before the first admitted frame, on the MoveWorker: both sites or neither. */
static int fl_arm(void) {
    if(fl_installed) return stats.active; /* Identical rerun after a loading-UI reload. */
    if(!stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(lxfl_state()!=0 || lxft_state()!=0) {fl_stage="entry";return 0;}
    if(!lxfl_pins() || !lxft_pins()) {fl_stage="pins";return 0;}
    fl_failures=0;
    if(!lxft_install()) {fl_stage="install";return 0;}
    if(!lxfl_install()) {lxft_restore();fl_stage="install";return 0;}
    fl_installed=1;fl_restored=0;fl_stage="armed";return 1;
}
/* Main-thread tick check; any failure stops the gate with reason 189. */
static int fl_check(void) {
    const char *why=!lxfl_owned() || !lxft_owned()?"entry_changed":!lxfl_pins() || !lxft_pins()?"block_changed":NULL;
    if(why) {fl_stage=why;InterlockedIncrement64(&fl_failures);return 0;}
    return 1;
}
static void fl_restore(void) {
    if(!fl_installed) return;
    int state=lxfl_state(),thrust=lxft_state();
    if(state==1) {unsigned char from[8];lxfl_entry(from,lxfl_page);if(lxfl_swap(from,lxfl_original)) state=0;}
    if(thrust==1) {unsigned char from[6];lxft_entry(from,lxft_page);if(lxft_swap(from,lxft_original)) thrust=0;}
    if(state!=0 || thrust!=0) return; /* a foreign value stays (and keeps the session unrestored) */
    fl_installed=0;fl_restored=1; /* the stub pages stay: a thread may still be inside one */
}
static int fl_status(char *out,size_t size) {
    int n=snprintf(out,size,"tick=%llu installed=%d restored=%d calls=%llu failures=%lld state=%d pin=%d stage=%s thrust_calls=%llu thrust_state=%d thrust_pin=%d",
        (unsigned long long)stats.issued,fl_installed,fl_restored,(unsigned long long)lxfl_calls(),(long long)fl_failures,lxfl_state(),lxfl_pins(),fl_stage,
        (unsigned long long)lxft_calls(),lxft_state(),lxft_pins());
    return n>0 && (size_t)n<size?n:0;
}
static int lua_flight_limits(lua_State *L) {p_lua_pushnumber(L,fl_arm());return 1;}
static int lua_flight_limits_status(lua_State *L) {
    char out[256];int n=accept_ui()?fl_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
