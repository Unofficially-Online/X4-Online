/* native_gate_fp_redirect (lockstep_fp_redirect.h, lockstep_fp_redirect_win.h): the strict vectors; the shadow
 * flow with stand-in natives (getters, M, and a refresh that calls the hooks as the executable's does, its frame
 * as there), the load's calls keyed after admission; counters, status and rows; arming refusals; the G4 and tail
 * stubs run; site CAS, ownership and restore on the plain image. On
 * the native image: the pins, and the executable's own M (0xea3dc0) and inlined +0x0 block (0xea22cf..0xea2396)
 * run on its bytes against ours. */
#include "test_fp_redirect_vectors.h"
static unsigned char tfr_comp[0x700],tfr_ship[0x20];
static float tfr_w;
static uint32_t tfr_perturb;
static int tfr_branch;
static float tfr_g4(void *comp,void *o20,void *buf) {(void)buf;assert(comp==tfr_comp && o20==tfr_ship);return fr_f32(0x43c9859bu);}
static float tfr_g0(void *comp,void *o20,void *buf) {(void)buf;assert(comp==tfr_comp && o20==tfr_ship);return fr_f32(0x4383fc81u);}
static float tfr_m(float a,float b,float c,uint64_t flag) {return fr_m((unsigned)(flag&0xffu),a,b,c);}
/* The first call: writes +0x10c (10.0) on every path. */
static void tfr_params(void *o20,unsigned char *slot,uint64_t flag,double time,uint64_t a5,uint64_t a6) {
    (void)flag;(void)time;(void)a5;(void)a6;assert(o20==tfr_ship);const float ten=10.0f;memcpy(slot+0x10c,&ten,4);
}
/* The executable's order: G4 (where the path begins, with the slot) -> +0x4, G0, +0x0 = G0 * L, +0x24, +0x8/+0xc =
 * 400; the M branch on request; every path ends at the tail. */
static void tfr_refresh(void *owner,void *slot,uint64_t flag) {
    unsigned char frame[0x100],*buf=frame+0x80,*s=slot;(void)owner;
    fr_params_hook(tfr_ship,s,flag,0.0,0,0);
    if(!(flag&1)) {
        memcpy(buf+FR_W_FROM_BUFFER,&tfr_w,4);
        const float g4=fr_g4_slot_hook(tfr_comp,tfr_ship,buf,slot);memcpy(s+4,&g4,4);
        const float g0=fr_g0_hook(tfr_comp,tfr_ship,buf);
        uint32_t ca,cb,out[3],four=0x43c80000u;memcpy(&ca,tfr_comp+FR_COMP_A,4);memcpy(&cb,tfr_comp+FR_COMP_B,4);
        fr_recompute(ca,cb,fr_bits(g4),fr_bits(g0),fr_bits(tfr_w),out);out[0]+=tfr_perturb;
        memcpy(s,&out[0],4);memcpy(s+0x24,&out[2],4);memcpy(s+8,&four,4);memcpy(s+0xc,&four,4);
        if(tfr_branch) {
            float a,b,v,m;memcpy(&a,s+0x2c,4);memcpy(&b,s+0x24,4);
            m=fr_m4_hook(a,b,fr_f32(0x43960000u),1);memcpy(&v,s+4,4);v=m*v;memcpy(s+4,&v,4);
            m=fr_m0_hook(b,a,fr_f32(0x43960000u),1);memcpy(&v,s,4);v=m*v;memcpy(s,&v,4);
        }
    }
    fr_tail_hook(slot);
}
/* The page's two code stubs run for real: G4's (mov r9,r15; jmp) and the tail's (call with r15, mov r13d, jmp back). */
__asm__(
".text\n"
"test_fr_g4_run:\n push %r15\n mov 0x30(%rsp),%r15\n mov %rcx,%r10\n mov %rdx,%rcx\n mov %r8,%rdx\n mov %r9,%r8\n sub $0x20,%rsp\n call *%r10\n add $0x20,%rsp\n pop %r15\n ret\n"
"test_fr_tail_run:\n push %rbp\n push %r15\n push %r13\n push %rbx\n mov %rsp,%rbp\n and $-16,%rsp\n mov %rdx,%r15\n xor %r13d,%r13d\n jmp *%rcx\n"
"test_fr_tail_back:\n mov %r13d,%eax\n mov %rbp,%rsp\n pop %rbx\n pop %r13\n pop %r15\n pop %rbp\n ret\n"
);
extern float test_fr_g4_run(const void *stub,void *comp,void *o20,void *buf,void *slot);
/* FR10/FR11: a length stub runs for real. r10 + rax = the segment record, xmm0-5 and xmm9 from the block (+0x00..+0x70),
 * xmm13 from +0x70 (0.5), sentinels in rcx, rdx, r8, r9, r11, rsp 8 off 16; the stub's back jump (planted at
 * image+back) leads to test_fr_len_back, which stores xmm0-5, xmm9 (+0x60) and rax, rcx, rdx, r8-r11, rbx (+0x80..). */
__asm__(
".text\n"
"test_fr_len_run:\n push %rbp\n push %rbx\n push %rsi\n push %rdi\n mov %rsp,%rbp\n sub $0x40,%rsp\n movdqu %xmm9,(%rsp)\n movdqu %xmm13,0x10(%rsp)\n"
" mov %r9,%rsi\n mov %rcx,%rbx\n mov %rdx,%r10\n mov %r8,%rax\n"
" movups (%rsi),%xmm0\n movups 0x10(%rsi),%xmm1\n movups 0x20(%rsi),%xmm2\n movups 0x30(%rsi),%xmm3\n movups 0x40(%rsi),%xmm4\n"
" movups 0x50(%rsi),%xmm5\n movups 0x60(%rsi),%xmm9\n movups 0x70(%rsi),%xmm13\n"
" movabs $0x1111111111111111,%rcx\n movabs $0x2222222222222222,%rdx\n movabs $0x8888888888888888,%r8\n movabs $0x9999999999999999,%r9\n"
" movabs $0xbbbbbbbbbbbbbbbb,%r11\n and $-16,%rsp\n sub $8,%rsp\n jmp *%rbx\n"
"test_fr_len_back:\n movups %xmm0,(%rsi)\n movups %xmm1,0x10(%rsi)\n movups %xmm2,0x20(%rsi)\n movups %xmm3,0x30(%rsi)\n movups %xmm4,0x40(%rsi)\n"
" movups %xmm5,0x50(%rsi)\n movups %xmm9,0x60(%rsi)\n mov %rax,0x80(%rsi)\n mov %rcx,0x88(%rsi)\n mov %rdx,0x90(%rsi)\n mov %r8,0x98(%rsi)\n"
" mov %r9,0xa0(%rsi)\n mov %r10,0xa8(%rsi)\n mov %r11,0xb0(%rsi)\n mov %rbx,0xb8(%rsi)\n"
" lea -0x40(%rbp),%rsp\n movdqu (%rsp),%xmm9\n movdqu 0x10(%rsp),%xmm13\n mov %rbp,%rsp\n pop %rdi\n pop %rsi\n pop %rbx\n pop %rbp\n ret\n"
);
extern void test_fr_len_run(const void *stub,unsigned char *r10,uint64_t rax,unsigned char *block);
extern char test_fr_len_back[];
/* Synthetic (not recorded) control points: the canonical length and Windows' pairwise one, by numpy float32. */
static const struct {uint32_t p[16],canonical,pairwise;} tfl_vectors[]={
    {{0xc459c812u,0x43d4eac9u,0xc50bec69u,0x448d5898u,0x45107ec8u,0x448c18c5u,0xc3a39c4fu,0x448a544eu,0x44ecabffu,0x449d0b6au,0x44fb79f3u,0x450b610du,0xc50ee54au,0x42b4e758u,0xc50705e2u,0xc47973cfu},0x45d50687u,0x45d50688u},
    {{0x45364516u,0x45350560u,0xc4fce866u,0xc5360ca1u,0x44dd4fffu,0x4400b6e4u,0x44e62cbau,0xc52bb60bu,0x42e202edu,0x452263f9u,0xc3fc140cu,0x43ccd111u,0x44e7a6a8u,0x4417e44au,0xc4f63aabu,0x44176ef3u},0x45d74566u,0x45d74566u},
    {{0xc4118ff0u,0x44432bd6u,0xc39ac802u,0x44c4d652u,0xc4cb368au,0xc53b6975u,0xc44d35b7u,0xc4637806u,0xc460abcau,0x44748f2eu,0xc4230a71u,0x448db214u,0xc3f7fe7eu,0xc5289833u,0x452acad1u,0x42530a4au},0x4609c5ccu,0x4609c5ccu},
    {{0x4491ba1du,0xc4952879u,0x452d0f2fu,0xc50d334eu,0xc51bb2a3u,0xc4fcaa20u,0x449330cdu,0xc511e2b7u,0x45076034u,0xc3a6cb98u,0xc4fc0404u,0x44fe93feu,0xc4a42e62u,0xc50c99abu,0xc53b62f9u,0xc514ff54u},0x461f9127u,0x461f9126u},
    {{0x450df357u,0xc4f5aae3u,0xc52f410du,0xc457e454u,0xc380155du,0xc4e95c64u,0xc50d5606u,0x4538d95eu,0x44f86424u,0x44168729u,0xc53a8ba7u,0x453730ceu,0xc4f11011u,0xc52e061fu,0xc4a83f0bu,0xc3a51e35u},0x45f93b66u,0x45f93b67u},
    {{0x43bbf96eu,0x44919b55u,0xc3bb887au,0x445e3aa8u,0xc330f90fu,0x42d6412du,0x4528794bu,0x4532f7c5u,0x442fe982u,0x451dc4fdu,0x44bc69b8u,0x43af99deu,0xc4b0e98fu,0x44e88f07u,0xc481170au,0xc4363875u},0x45b433f9u,0x45b433fau},
};
/* One stub run: segment record seg (points set), V = 2 * native (the stub's mulss halves it), every other register
 * a pattern; checks the store, V (lane 0 = want_v, lanes 1-3 kept) and that nothing else changed. */
static void tfl_run(const unsigned char *page,unsigned i,unsigned char *seg,uint32_t native,uint32_t want_store,uint32_t want_v) {
    unsigned char block[0xc0],in[0xc0];
    for(unsigned k=0;k<0x70;k++) block[k]=(unsigned char)(0x40+k);
    const unsigned v=fr_len_sites[i].reg;const float twice=fr_f32(native)*2.0f;unsigned char *vslot=block+(v==4?0x40:0x60);
    memcpy(vslot,&twice,4);const float half=0.5f;memset(block+0x70,0,16);memcpy(block+0x70,&half,4);memcpy(in,block,sizeof(in));
    memset(seg+0x18,0xee,4);
    test_fr_len_run(page+FR_LEN_STUB+FR_LEN_STUB_SIZE*i,seg-0x30,0x30,block);
    uint32_t stored,got;memcpy(&stored,seg+0x18,4);memcpy(&got,vslot,4);
    assert(stored==want_store && got==want_v && !memcmp(vslot+4,(v==4?in+0x44:in+0x64),12));
    for(unsigned k=0;k<0x70;k+=16) if(block+k!=vslot) assert(!memcmp(block+k,in+k,16));
    uint64_t r[8];memcpy(r,block+0x80,64);
    assert(r[0]==0x30 && r[1]==0x1111111111111111ull && r[2]==0x2222222222222222ull && r[3]==0x8888888888888888ull);
    assert(r[4]==0x9999999999999999ull && r[5]==(uint64_t)(uintptr_t)(seg-0x30) && r[6]==0xbbbbbbbbbbbbbbbbull && r[7]==(uint64_t)(uintptr_t)(page+FR_LEN_STUB+FR_LEN_STUB_SIZE*i));
}
static void test_fr_len_stubs(void) {
    for(unsigned k=0;k<sizeof(tfl_vectors)/sizeof(*tfl_vectors);k++) {
        assert(sl_length((const unsigned char*)tfl_vectors[k].p)==tfl_vectors[k].canonical);
        assert(sl_length_pairwise((const unsigned char*)tfl_vectors[k].p)==tfl_vectors[k].pairwise);
    }
    assert(sl_stored_linux(0)==SL_ONE && sl_stored_linux(0x80000000u)==SL_ONE && sl_stored_linux(0x7fc00000u)==0x7fc00000u);
    assert(sl_stored_windows(0)==SL_ONE && sl_stored_windows(0x7fc00000u)==SL_ONE && sl_stored_windows(0x3f000000u)==0x3f000000u);
    unsigned char *page=fr_alloc_page(),*saved=fr_page;DWORD o2,i2;assert(page);
    assert(VirtualProtect(page,0x1000,PAGE_READWRITE,&o2));fr_page=page;fr_write_code(page);fr_page=saved;
    assert(VirtualProtect(page,0x1000,PAGE_EXECUTE_READ,&i2));FlushInstructionCache(GetCurrentProcess(),page,0x1000);
    for(unsigned i=0;i<FR_LEN_SITES;i++) {
        unsigned char back[14]={0xff,0x25,0,0,0,0};void *to=(void*)test_fr_len_back;memcpy(back+6,&to,8);
        unsigned char *at=image+fr_len_sites[i].back;DWORD old;assert(VirtualProtect(at,14,PAGE_EXECUTE_READWRITE,&old));
        memcpy(at,back,14);FlushInstructionCache(GetCurrentProcess(),at,14);
    }
    static unsigned char seg[0xa0];
    const int saved_mode=fr_mode;const uint32_t saved_active=stats.active;stats.active=1;
    const LONG64 n0=fr_n[FR_LEN],d0=fr_diff[FR_LEN],h0=fr_hits[FR_HIT_LEN0],h1=fr_hits[FR_HIT_LEN1],w0=fr_len_written;
    for(unsigned i=0;i<FR_LEN_SITES;i++) {
        memcpy(seg+0x60,tfl_vectors[0].p,64);
        const uint32_t ours=tfl_vectors[0].canonical,native=tfl_vectors[0].pairwise;
        fr_mode=FR_OFF;tfl_run(page,i,seg,native,native,native);                       /* off: the block's own */
        fr_mode=FR_SHADOW;tfl_run(page,i,seg,native,native,native);                    /* shadow: compared, native kept */
        fr_mode=FR_WRITER;tfl_run(page,i,seg,native,native,native);                    /* writer only: the length shadowed */
        fr_mode=FR_REDIRECT;tfl_run(page,i,seg,native,ours,ours);                      /* redirect: ours, Linux's rule */
        memcpy(seg+0x60,tfl_vectors[1].p,64);tfl_run(page,i,seg,tfl_vectors[1].pairwise,tfl_vectors[1].canonical,tfl_vectors[1].canonical);
        memset(seg+0x60,0,64);                                                         /* zero length: 1.0 stored, V = 0 */
        fr_mode=FR_SHADOW;tfl_run(page,i,seg,0,SL_ONE,0);fr_mode=FR_REDIRECT;tfl_run(page,i,seg,0,SL_ONE,0);
        const uint32_t nan=0x7fc00000u;memcpy(seg+0x60,&nan,4);                        /* NaN: Windows stores 1.0, Linux keeps it */
        fr_mode=FR_SHADOW;tfl_run(page,i,seg,nan,SL_ONE,nan);
        fr_mode=FR_REDIRECT;tfl_run(page,i,seg,nan,sl_length(seg+0x60),sl_length(seg+0x60));
        assert(sl_is_nan(sl_length(seg+0x60)));
    }
    /* per site: 9 runs, compared 8 (all but off), differing 3 (vector 0 in shadow, writer and redirect; the NaN
     * propagates unchanged), written 2 in redirect (vector 0, and the NaN: Linux keeps it where Windows stores 1.0) */
    assert(fr_n[FR_LEN]==n0+16 && fr_diff[FR_LEN]==d0+6 && fr_hits[FR_HIT_LEN0]==h0+9 && fr_hits[FR_HIT_LEN1]==h1+9 && fr_len_written==w0+4);
    char out[2048];assert(fr_status(out,sizeof(out))>0 && strstr(out," len=1:") && strstr(out," len_written="));
    fr_mode=saved_mode;stats.active=saved_active;
    for(unsigned i=0;i<FR_LEN_SITES;i++) memset(image+fr_len_sites[i].back,0,14);
    VirtualFree(page,0,MEM_RELEASE);
    assert(fr_lua_mode("writer",6)==FR_WRITER && fr_lua_mode("shadow",6)==FR_SHADOW && fr_lua_mode("redirect",8)==FR_REDIRECT && fr_lua_mode("writex",6)<0);
    printf("fp redirect length: %u strict vectors, both stubs off/shadow/writer/redirect (zero, NaN), registers kept PASS\n",
        (unsigned)(sizeof(tfl_vectors)/sizeof(*tfl_vectors)));
}
/* FR13: a handle stub runs for real. xmm0-5 from the block (+0x00..+0x5f, xmm2 = d in lane 0), xmm8 from +0x60 (k),
 * sentinels in rax, rcx, rdx, r8-r11, rsp 8 off 16; the back jump (planted at image+back) leads to test_fr_h_back,
 * which stores xmm0-5, xmm8 (+0x60) and rax, rcx, rdx, r8-r11, rbx (+0x80..). */
__asm__(
".text\n"
"test_fr_h_run:\n push %rbp\n push %rbx\n push %rsi\n push %rdi\n mov %rsp,%rbp\n sub $0x40,%rsp\n movdqu %xmm8,(%rsp)\n"
" mov %rdx,%rsi\n mov %rcx,%rbx\n"
" movups (%rsi),%xmm0\n movups 0x10(%rsi),%xmm1\n movups 0x20(%rsi),%xmm2\n movups 0x30(%rsi),%xmm3\n movups 0x40(%rsi),%xmm4\n"
" movups 0x50(%rsi),%xmm5\n movups 0x60(%rsi),%xmm8\n"
" movabs $0x0123456789abcdef,%rax\n movabs $0x1111111111111111,%rcx\n movabs $0x2222222222222222,%rdx\n movabs $0x8888888888888888,%r8\n"
" movabs $0x9999999999999999,%r9\n movabs $0xaaaaaaaaaaaaaaaa,%r10\n movabs $0xbbbbbbbbbbbbbbbb,%r11\n and $-16,%rsp\n sub $8,%rsp\n jmp *%rbx\n"
"test_fr_h_back:\n movups %xmm0,(%rsi)\n movups %xmm1,0x10(%rsi)\n movups %xmm2,0x20(%rsi)\n movups %xmm3,0x30(%rsi)\n movups %xmm4,0x40(%rsi)\n"
" movups %xmm5,0x50(%rsi)\n movups %xmm8,0x60(%rsi)\n mov %rax,0x80(%rsi)\n mov %rcx,0x88(%rsi)\n mov %rdx,0x90(%rsi)\n mov %r8,0x98(%rsi)\n"
" mov %r9,0xa0(%rsi)\n mov %r10,0xa8(%rsi)\n mov %r11,0xb0(%rsi)\n mov %rbx,0xb8(%rsi)\n"
" lea -0x40(%rbp),%rsp\n movdqu (%rsp),%xmm8\n mov %rbp,%rsp\n pop %rdi\n pop %rsi\n pop %rbx\n pop %rbp\n ret\n"
);
extern void test_fr_h_run(const void *stub,unsigned char *block);
extern char test_fr_h_back[];
#define TFH_K 0x3eaaaaabu /* 0.333333343f, Windows' xmm8 */
/* One stub run: xmm2 lane 0 = d, lanes 1-3 and the other registers a pattern; checks xmm2 = want (lanes 1-3 kept) and
 * that nothing else changed. */
static void tfh_run(const unsigned char *page,unsigned i,uint32_t d,uint32_t want) {
    unsigned char block[0xc0],in[0xc0];
    for(unsigned k=0;k<0x60;k++) block[k]=(unsigned char)(0x40+k);
    memcpy(block+0x20,&d,4);const uint32_t k8[4]={TFH_K,0x11223344u,0x55667788u,0x99aabbccu};memcpy(block+0x60,k8,16);
    memcpy(in,block,sizeof(in));
    test_fr_h_run(page+FR_H_STUB+FR_LEN_STUB_SIZE*i,block);
    uint32_t got;memcpy(&got,block+0x20,4);
    if(got!=want) {printf("fp redirect handle stub %u: d %08x got %08x want %08x\n",i,d,got,want);assert(0);}
    assert(!memcmp(block+0x24,in+0x24,12) && !memcmp(block+0x60,in+0x60,16));
    for(unsigned k=0;k<0x60;k+=16) if(k!=0x20) assert(!memcmp(block+k,in+k,16));
    uint64_t r[8];memcpy(r,block+0x80,64);
    assert(r[0]==0x0123456789abcdefull && r[1]==0x1111111111111111ull && r[2]==0x2222222222222222ull && r[3]==0x8888888888888888ull);
    assert(r[4]==0x9999999999999999ull && r[5]==0xaaaaaaaaaaaaaaaaull && r[6]==0xbbbbbbbbbbbbbbbbull && r[7]==(uint64_t)(uintptr_t)(page+FR_H_STUB+FR_LEN_STUB_SIZE*i));
}
static void test_fr_h_stubs(void) {
    /* Strict vectors (numpy float32): 100/3 = 42055555, 100 * 0.333333343 = 42055556; 3 -> 1.0 both ways. */
    assert(sl_handle(0x42c80000u)==0x42055555u && sl_handle_windows(0x42c80000u,TFH_K)==0x42055556u);
    assert(sl_handle(0x40400000u)==0x3f800000u && sl_handle_windows(0x40400000u,TFH_K)==0x3f800000u);
    unsigned char *page=fr_alloc_page(),*saved=fr_page;DWORD o2,i2;assert(page);
    assert(VirtualProtect(page,0x1000,PAGE_READWRITE,&o2));fr_page=page;fr_write_code(page);fr_page=saved;
    assert(VirtualProtect(page,0x1000,PAGE_EXECUTE_READ,&i2));FlushInstructionCache(GetCurrentProcess(),page,0x1000);
    for(unsigned i=0;i<FR_H_SITES;i++) {
        unsigned char back[14]={0xff,0x25,0,0,0,0};void *to=(void*)test_fr_h_back;memcpy(back+6,&to,8);
        unsigned char *at=image+fr_h_sites[i].back;DWORD old;assert(VirtualProtect(at,14,PAGE_EXECUTE_READWRITE,&old));
        memcpy(at,back,14);FlushInstructionCache(GetCurrentProcess(),at,14);
    }
    const int saved_mode=fr_mode;const uint32_t saved_active=stats.active;stats.active=1;
    const LONG64 n0=fr_n[FR_HANDLE],d0=fr_diff[FR_HANDLE],w0=fr_handle_written;LONG64 h0[FR_H_SITES];
    for(unsigned i=0;i<FR_H_SITES;i++) h0[i]=fr_hits[FR_HIT_HANDLE0+i];
    for(unsigned i=0;i<FR_H_SITES;i++) {
        fr_mode=FR_OFF;tfh_run(page,i,0x42c80000u,0x42055556u);       /* off: native d*k */
        fr_mode=FR_SHADOW;tfh_run(page,i,0x42c80000u,0x42055556u);    /* shadow: compared, native kept */
        fr_mode=FR_WRITER;tfh_run(page,i,0x42c80000u,0x42055556u);    /* writer only: the handle shadowed */
        fr_mode=FR_REDIRECT;tfh_run(page,i,0x42c80000u,0x42055555u);  /* redirect: ours, Linux's division */
        tfh_run(page,i,0x40400000u,0x3f800000u);                       /* equal both ways: nothing written */
    }
    /* per site: 5 runs, compared 4, differing 3, written 1 */
    assert(fr_n[FR_HANDLE]==n0+16 && fr_diff[FR_HANDLE]==d0+12 && fr_handle_written==w0+4);
    for(unsigned i=0;i<FR_H_SITES;i++) assert(fr_hits[FR_HIT_HANDLE0+i]==h0[i]+5);
    char out[2048];assert(fr_status(out,sizeof(out))>0 && strstr(out," handle=1:") && strstr(out," handle_written="));
    fr_mode=saved_mode;stats.active=saved_active;
    for(unsigned i=0;i<FR_H_SITES;i++) memset(image+fr_h_sites[i].back,0,14);
    VirtualFree(page,0,MEM_RELEASE);
    printf("fp redirect handle: strict vectors, four stubs off/shadow/writer/redirect, registers kept PASS\n");
}
/* FR16: the PointGuard angle stub runs for real in test_fr_h_run's frame: xmm1 lane 0 = rem (+0x10), xmm2 lane 0 = m+1
 * (+0x20); checks xmm1 = want (lanes 1-3 kept) and that nothing else changed. */
static void tpg_run(const unsigned char *page,uint32_t rem,uint32_t m1,uint32_t want) {
    unsigned char block[0xc0],in[0xc0];
    for(unsigned k=0;k<0x60;k++) block[k]=(unsigned char)(0x40+k);
    memcpy(block+0x10,&rem,4);memcpy(block+0x20,&m1,4);
    const uint32_t k8[4]={0x3eaaaaabu,0x11223344u,0x55667788u,0x99aabbccu};memcpy(block+0x60,k8,16);
    memcpy(in,block,sizeof(in));
    test_fr_h_run(page+FR_PG_STUB,block);
    uint32_t got;memcpy(&got,block+0x10,4);
    if(got!=want) {printf("fp redirect pg stub: rem %08x m1 %08x got %08x want %08x\n",rem,m1,got,want);assert(0);}
    assert(!memcmp(block+0x14,in+0x14,12) && !memcmp(block+0x60,in+0x60,16));
    for(unsigned k=0;k<0x60;k+=16) if(k!=0x10) assert(!memcmp(block+k,in+k,16));
    uint64_t r[8];memcpy(r,block+0x80,64);
    assert(r[0]==0x0123456789abcdefull && r[1]==0x1111111111111111ull && r[2]==0x2222222222222222ull && r[3]==0x8888888888888888ull);
    assert(r[4]==0x9999999999999999ull && r[5]==0xaaaaaaaaaaaaaaaaull && r[6]==0xbbbbbbbbbbbbbbbbull && r[7]==(uint64_t)(uintptr_t)(page+FR_PG_STUB));
}
static void test_fr_pg_stubs(void) {
    /* Strict vectors (numpy float32): rem 1, m+1 7: Windows 1 * (pi/7) = 3ee5c8fa, Linux (1/14) * 2pi = 3ee5c8fb; rem 3, m+1 5:
     * 3ff1463a / 3ff1463b; rem 1, m+1 4: 3f490fdb both; rem 2, m+1 3: 40060a92 both; rem 0: 0 both. */
    assert(fr_pg_angle_windows(0x3f800000u,0x40e00000u)==0x3ee5c8fau && fr_pg_angle(0x3f800000u,0x40e00000u)==0x3ee5c8fbu);
    assert(fr_pg_angle_windows(0x40400000u,0x40a00000u)==0x3ff1463au && fr_pg_angle(0x40400000u,0x40a00000u)==0x3ff1463bu);
    assert(fr_pg_angle_windows(0x3f800000u,0x40800000u)==0x3f490fdbu && fr_pg_angle(0x3f800000u,0x40800000u)==0x3f490fdbu);
    assert(fr_pg_angle_windows(0x40000000u,0x40400000u)==0x40060a92u && fr_pg_angle(0x40000000u,0x40400000u)==0x40060a92u);
    assert(fr_pg_angle_windows(0,0x40400000u)==0 && fr_pg_angle(0,0x40400000u)==0);
    unsigned char *page=fr_alloc_page(),*saved=fr_page;DWORD o2,i2;assert(page);
    assert(VirtualProtect(page,0x1000,PAGE_READWRITE,&o2));fr_page=page;fr_write_code(page);fr_page=saved;
    assert(VirtualProtect(page,0x1000,PAGE_EXECUTE_READ,&i2));FlushInstructionCache(GetCurrentProcess(),page,0x1000);
    {   unsigned char back[14]={0xff,0x25,0,0,0,0};void *to=(void*)test_fr_h_back;memcpy(back+6,&to,8);
        unsigned char *at=image+FR_PG_BACK;DWORD old;assert(VirtualProtect(at,14,PAGE_EXECUTE_READWRITE,&old));
        memcpy(at,back,14);FlushInstructionCache(GetCurrentProcess(),at,14);
    }
    const int saved_mode=fr_mode;const uint32_t saved_active=stats.active;stats.active=1;
    const LONG64 n0=fr_n[FR_PG],d0=fr_diff[FR_PG],w0=fr_pg_written,h0=fr_hits[FR_HIT_PG];
    fr_mode=FR_OFF;tpg_run(page,0x3f800000u,0x40e00000u,0x3ee5c8fau);      /* off: native rem * (pi/(m+1)) */
    fr_mode=FR_SHADOW;tpg_run(page,0x3f800000u,0x40e00000u,0x3ee5c8fau);   /* shadow: compared, native kept */
    fr_mode=FR_WRITER;tpg_run(page,0x40400000u,0x40a00000u,0x3ff1463au);   /* writer only: the angle shadowed */
    fr_mode=FR_REDIRECT;tpg_run(page,0x3f800000u,0x40e00000u,0x3ee5c8fbu); /* redirect: ours, Linux's order */
    tpg_run(page,0x3f800000u,0x40800000u,0x3f490fdbu);                      /* equal both ways: nothing written */
    /* 5 runs, compared 4, differing 3, written 1 */
    assert(fr_n[FR_PG]==n0+4 && fr_diff[FR_PG]==d0+3 && fr_pg_written==w0+1 && fr_hits[FR_HIT_PG]==h0+5);
    char out[2048];assert(fr_status(out,sizeof(out))>0 && strstr(out," pg=1:") && strstr(out," pg_hits=") && strstr(out," pg_written="));
    fr_mode=saved_mode;stats.active=saved_active;
    memset(image+FR_PG_BACK,0,14);
    VirtualFree(page,0,MEM_RELEASE);
    printf("fp redirect pointguard angle: strict vectors, stub off/shadow/writer/redirect, registers kept PASS\n");
}
/* FR22: the affine weight stub runs for real in test_fr_h_run's frame: xmm0 lane 0 = dp (+0x00), xmm1 lane 0 = sum
 * (+0x10), xmm2 = 2.0 (+0x20); checks xmm2 = want (lanes 1-3 kept) and that nothing else changed. */
static void tfa_run(const unsigned char *page,uint32_t sum,uint32_t dp,uint32_t want) {
    unsigned char block[0xc0],in[0xc0];
    for(unsigned k=0;k<0x60;k++) block[k]=(unsigned char)(0x40+k);
    const uint32_t two=0x40000000u;memcpy(block,&dp,4);memcpy(block+0x10,&sum,4);memcpy(block+0x20,&two,4);
    const uint32_t k8[4]={0x3eaaaaabu,0x11223344u,0x55667788u,0x99aabbccu};memcpy(block+0x60,k8,16);
    memcpy(in,block,sizeof(in));
    test_fr_h_run(page+FR_AFF_STUB,block);
    uint32_t got;memcpy(&got,block+0x20,4);
    if(got!=want) {printf("fp redirect affine stub: sum %08x dp %08x got %08x want %08x\n",sum,dp,got,want);assert(0);}
    assert(!memcmp(block+0x24,in+0x24,12) && !memcmp(block+0x60,in+0x60,16));
    for(unsigned k=0;k<0x60;k+=16) if(k!=0x20) assert(!memcmp(block+k,in+k,16));
    uint64_t r[8];memcpy(r,block+0x80,64);
    assert(r[0]==0x0123456789abcdefull && r[1]==0x1111111111111111ull && r[2]==0x2222222222222222ull && r[3]==0x8888888888888888ull);
    assert(r[4]==0x9999999999999999ull && r[5]==0xaaaaaaaaaaaaaaaaull && r[6]==0xbbbbbbbbbbbbbbbbull && r[7]==(uint64_t)(uintptr_t)(page+FR_AFF_STUB));
}
static void test_fr_aff_stubs(void) {
    /* Strict vectors (numpy float32): a ramp pair (speeds 0.2 and 1: sum 3fb93e4c), dp 3ea7a0f9: Windows (2/sum)*dp =
     * 3ee7a845, Linux dp/(sum*0.5) = 3ee7a844; dp 3e9d82fd: 3ed9ad00 / 3ed9acff; sum 2.0 (speeds 1, 1): equal. */
    assert(sl_affine_weight_windows(0x3fb93e4cu,0x3ea7a0f9u)==0x3ee7a845u && sl_affine_weight(0x3fb93e4cu,0x3ea7a0f9u)==0x3ee7a844u);
    assert(sl_affine_weight_windows(0x3fb93e4cu,0x3e9d82fdu)==0x3ed9ad00u && sl_affine_weight(0x3fb93e4cu,0x3e9d82fdu)==0x3ed9acffu);
    assert(sl_affine_weight_windows(0x40000000u,0x3ea7a0f9u)==0x3ea7a0f9u && sl_affine_weight(0x40000000u,0x3ea7a0f9u)==0x3ea7a0f9u);
    unsigned char *page=fr_alloc_page(),*saved=fr_page;DWORD o2,i2;assert(page);
    assert(VirtualProtect(page,0x1000,PAGE_READWRITE,&o2));fr_page=page;fr_write_code(page);fr_page=saved;
    assert(VirtualProtect(page,0x1000,PAGE_EXECUTE_READ,&i2));FlushInstructionCache(GetCurrentProcess(),page,0x1000);
    {   unsigned char back[14]={0xff,0x25,0,0,0,0};void *to=(void*)test_fr_h_back;memcpy(back+6,&to,8);
        unsigned char *at=image+FR_AFF_BACK;DWORD old;assert(VirtualProtect(at,14,PAGE_EXECUTE_READWRITE,&old));
        memcpy(at,back,14);FlushInstructionCache(GetCurrentProcess(),at,14);
    }
    const int saved_mode=fr_mode;const uint32_t saved_active=stats.active;stats.active=1;
    const LONG64 n0=fr_n[FR_AFF],d0=fr_diff[FR_AFF],w0=fr_aff_written,h0=fr_hits[FR_HIT_AFF];
    fr_mode=FR_OFF;tfa_run(page,0x3fb93e4cu,0x3ea7a0f9u,0x3ee7a845u);      /* off: native (2/sum)*dp */
    fr_mode=FR_SHADOW;tfa_run(page,0x3fb93e4cu,0x3ea7a0f9u,0x3ee7a845u);   /* shadow: compared, native kept */
    fr_mode=FR_WRITER;tfa_run(page,0x3fb93e4cu,0x3e9d82fdu,0x3ed9ad00u);   /* writer only: the weight shadowed */
    fr_mode=FR_REDIRECT;tfa_run(page,0x3fb93e4cu,0x3ea7a0f9u,0x3ee7a844u); /* redirect: ours, Linux's division */
    tfa_run(page,0x40000000u,0x3ea7a0f9u,0x3ea7a0f9u);                      /* equal both ways: nothing written */
    /* 5 runs, compared 4, differing 3, written 1 */
    assert(fr_n[FR_AFF]==n0+4 && fr_diff[FR_AFF]==d0+3 && fr_aff_written==w0+1 && fr_hits[FR_HIT_AFF]==h0+5);
    char out[2048];assert(fr_status(out,sizeof(out))>0 && strstr(out," aff=1:") && strstr(out," aff_hits=") && strstr(out," aff_written="));
    fr_mode=saved_mode;stats.active=saved_active;
    memset(image+FR_AFF_BACK,0,14);
    VirtualFree(page,0,MEM_RELEASE);
    printf("fp redirect affine weight: strict vectors, stub off/shadow/writer/redirect, registers kept PASS\n");
}
/* FR23: the velocity stub runs for real. test_fr_vel_run enters block[0xd0] with rsi = block[0xc0] (the bank),
 * rdi = block[0xc8] (a model), xmm0-5 from +0x00..+0x5f, xmm11 (+0x60, lane 0 = s), xmm12 (+0x70), sentinels in rax,
 * rcx, rdx, r8-r11, rsp 8 off 16; the back jump (planted at the back address) leads to test_fr_vel_back, which stores
 * xmm0-5, xmm11, xmm12, rax, rcx, rdx, r8-r11 and rsi (+0x80..+0xb8). rbx holds the block throughout. */
__asm__(
".text\n"
"test_fr_vel_run:\n push %rbp\n push %rbx\n push %rsi\n push %rdi\n mov %rsp,%rbp\n sub $0x40,%rsp\n movdqu %xmm11,(%rsp)\n movdqu %xmm12,0x10(%rsp)\n"
" mov %rcx,%rbx\n mov 0xc0(%rbx),%rsi\n mov 0xc8(%rbx),%rdi\n"
" movups (%rbx),%xmm0\n movups 0x10(%rbx),%xmm1\n movups 0x20(%rbx),%xmm2\n movups 0x30(%rbx),%xmm3\n movups 0x40(%rbx),%xmm4\n"
" movups 0x50(%rbx),%xmm5\n movups 0x60(%rbx),%xmm11\n movups 0x70(%rbx),%xmm12\n"
" movabs $0x0123456789abcdef,%rax\n movabs $0x1111111111111111,%rcx\n movabs $0x2222222222222222,%rdx\n movabs $0x8888888888888888,%r8\n"
" movabs $0x9999999999999999,%r9\n movabs $0xaaaaaaaaaaaaaaaa,%r10\n movabs $0xbbbbbbbbbbbbbbbb,%r11\n and $-16,%rsp\n sub $8,%rsp\n jmp *0xd0(%rbx)\n"
"test_fr_vel_back:\n movups %xmm0,(%rbx)\n movups %xmm1,0x10(%rbx)\n movups %xmm2,0x20(%rbx)\n movups %xmm3,0x30(%rbx)\n movups %xmm4,0x40(%rbx)\n"
" movups %xmm5,0x50(%rbx)\n movups %xmm11,0x60(%rbx)\n movups %xmm12,0x70(%rbx)\n mov %rax,0x80(%rbx)\n mov %rcx,0x88(%rbx)\n mov %rdx,0x90(%rbx)\n"
" mov %r8,0x98(%rbx)\n mov %r9,0xa0(%rbx)\n mov %r10,0xa8(%rbx)\n mov %r11,0xb0(%rbx)\n mov %rsi,0xb8(%rbx)\n"
" lea -0x40(%rbp),%rsp\n movdqu (%rsp),%xmm11\n movdqu 0x10(%rsp),%xmm12\n mov %rbp,%rsp\n pop %rdi\n pop %rsi\n pop %rbx\n pop %rbp\n ret\n"
);
extern void test_fr_vel_run(unsigned char *block);
extern char test_fr_vel_back[];
#define TFV_BLOCK 0xd8u
static __declspec(align(16)) unsigned char tfv_bank[0x60],tfv_model[0x100];
/* One run from entry over rows r0..r2 (bank +0x10..+0x3f) and s; the block (registers after) and the bank's velocity
 * (+0x40) and +0x50 out. xmm12 = 0 in every lane, as the step's xorps leaves it; xmm11's lanes 1-3 a pattern. */
static void tfv_run(const void *entry,const uint32_t rows[12],uint32_t s,unsigned flag,unsigned char block[TFV_BLOCK],uint32_t vel[8]) {
    for(unsigned k=0;k<0x60;k++) block[k]=(unsigned char)(0x40+k);
    const uint32_t x11[4]={s,0x3f000000u,0x40000000u,0x40400000u},x12[4]={0,0,0,0};memcpy(block+0x60,x11,16);memcpy(block+0x70,x12,16);
    memset(block+0x80,0,0x40);
    memset(tfv_bank,0x5a,sizeof(tfv_bank));memcpy(tfv_bank+0x10,rows,48);tfv_model[0x95]=(unsigned char)flag;
    void *b=tfv_bank,*m=tfv_model;memcpy(block+0xc0,&b,8);memcpy(block+0xc8,&m,8);memcpy(block+0xd0,&entry,8);
    test_fr_vel_run(block);memcpy(vel,tfv_bank+0x40,32);
}
/* rows A: r0 (0.6,0,0.8), r1 (0,1,0), r2 (-0.8,0,0.6) (a yaw of about -53 degrees); rows B: every x negative, so even
 * Linux's sum stays -0 in lane 0 at s = +0. */
static const uint32_t tfv_rows_a[12]={0x3f19999au,0,0x3f4ccccdu,0, 0,0x3f800000u,0,0, 0xbf4ccccdu,0,0x3f19999au,0};
static const uint32_t tfv_rows_b[12]={0xbf19999au,0,0x3f4ccccdu,0, 0xbf800000u,0,0,0, 0xbf4ccccdu,0,0x3f19999au,0};
/* The stub against native's block alone (a copy of its 27 bytes, then the back jump): every register the same; the
 * velocity native's (the reference run's) or, in 'redirect', ours. */
static void tfv_check(const unsigned char *stub,const unsigned char *reference,const uint32_t rows[12],uint32_t s,const uint32_t want[4]) {
    unsigned char a[TFV_BLOCK],b[TFV_BLOCK];uint32_t va[8],vb[8];
    tfv_run(reference,rows,s,1,a,va);tfv_run(stub,rows,s,1,b,vb);
    if(memcmp(vb,want,16)) {printf("fp redirect velocity stub: s %08x got %08x %08x %08x %08x want %08x %08x %08x %08x\n",s,vb[0],vb[1],vb[2],vb[3],want[0],want[1],want[2],want[3]);assert(0);}
    assert(!memcmp(a,b,0xb8) && !memcmp(va+4,vb+4,16) && va[4]==0x5a5a5a5au);
    uint32_t w[4];fr_velocity_windows(rows,s,w);assert(!memcmp(va,w,16));
    uint64_t r[8];memcpy(r,b+0x80,64);
    assert(r[0]==0x0123456789abcdefull && r[1]==0x1111111111111111ull && r[2]==0x2222222222222222ull && r[3]==0x8888888888888888ull);
    assert(r[4]==0x9999999999999999ull && r[5]==0xaaaaaaaaaaaaaaaaull && r[6]==0xbbbbbbbbbbbbbbbbull && r[7]==(uint64_t)(uintptr_t)tfv_bank);
}
static void test_fr_vel_stubs(void) {
    /* Strict vectors (numpy float32): rows A at s = +0: Windows (-0,0,0,0), Linux (0,0,0,0); at s = -0: Windows
     * (0,-0,-0,0), Linux (0,0,0,0); at s = 250: (-200,0,150,0) both. Rows B at s = +0: (-0,0,0,0) both. */
    static const uint32_t z4[4]={0,0,0,0},wa0[4]={0x80000000u,0,0,0},wan[4]={0,0x80000000u,0x80000000u,0},v250[4]={0xc3480000u,0,0x43160000u,0};
    uint32_t o[4];
    fr_velocity_windows(tfv_rows_a,0,o);assert(!memcmp(o,wa0,16));fr_velocity(tfv_rows_a,0,o);assert(!memcmp(o,z4,16));
    fr_velocity_windows(tfv_rows_a,0x80000000u,o);assert(!memcmp(o,wan,16));fr_velocity(tfv_rows_a,0x80000000u,o);assert(!memcmp(o,z4,16));
    fr_velocity_windows(tfv_rows_a,0x437a0000u,o);assert(!memcmp(o,v250,16));fr_velocity(tfv_rows_a,0x437a0000u,o);assert(!memcmp(o,v250,16));
    fr_velocity_windows(tfv_rows_b,0,o);assert(!memcmp(o,wa0,16));fr_velocity(tfv_rows_b,0,o);assert(!memcmp(o,wa0,16));
    unsigned char *page=fr_alloc_page(),*saved=fr_page;DWORD o2,i2;assert(page);
    assert(VirtualProtect(page,0x1000,PAGE_READWRITE,&o2));fr_page=page;fr_write_code(page);fr_page=saved;
    /* the reference: native's block alone at +0xf00, then jmp [rip] -> the harness's back */
    memcpy(page+0xf00,fr_vel_block,FR_VEL_BLOCK);
    {unsigned char back[14]={0xff,0x25,0,0,0,0};void *to=(void*)test_fr_vel_back;memcpy(back+6,&to,8);memcpy(page+0xf00+FR_VEL_BLOCK,back,14);}
    assert(VirtualProtect(page,0x1000,PAGE_EXECUTE_READ,&i2));FlushInstructionCache(GetCurrentProcess(),page,0x1000);
    {   unsigned char back[14]={0xff,0x25,0,0,0,0};void *to=(void*)test_fr_vel_back;memcpy(back+6,&to,8);
        unsigned char *at=image+FR_VEL_BACK;DWORD old;assert(VirtualProtect(at,14,PAGE_EXECUTE_READWRITE,&old));
        memcpy(at,back,14);FlushInstructionCache(GetCurrentProcess(),at,14);
    }
    const int saved_mode=fr_mode;const uint32_t saved_active=stats.active;stats.active=1;
    const LONG64 n0=fr_n[FR_VEL],d0=fr_diff[FR_VEL],w0=fr_vel_written,h0=fr_hits[FR_HIT_VEL];
    const unsigned char *stub=page+FR_VEL_STUB,*ref=page+0xf00;
    fr_mode=FR_OFF;tfv_check(stub,ref,tfv_rows_a,0,wa0);              /* off: native's -0 */
    fr_mode=FR_SHADOW;tfv_check(stub,ref,tfv_rows_a,0,wa0);           /* shadow: compared, native kept */
    fr_mode=FR_WRITER;tfv_check(stub,ref,tfv_rows_a,0x80000000u,wan); /* writer only: the velocity shadowed */
    fr_mode=FR_REDIRECT;tfv_check(stub,ref,tfv_rows_a,0,z4);          /* redirect: ours, Linux's +0 */
    tfv_check(stub,ref,tfv_rows_a,0x437a0000u,v250);                    /* a moving ship: equal, nothing written */
    tfv_check(stub,ref,tfv_rows_b,0,wa0);                               /* -0 both ways: nothing written */
    /* 6 runs, compared 5, differing 3, written 1 */
    assert(fr_n[FR_VEL]==n0+5 && fr_diff[FR_VEL]==d0+3 && fr_vel_written==w0+1 && fr_hits[FR_HIT_VEL]==h0+6);
    char out[2048];assert(fr_status(out,sizeof(out))>0 && strstr(out," vel=0:") && strstr(out," vel_hits=") && strstr(out," vel_written="));
    fr_mode=saved_mode;stats.active=saved_active;
    memset(image+FR_VEL_BACK,0,14);
    VirtualFree(page,0,MEM_RELEASE);
    printf("fp redirect velocity: strict vectors, stub off/shadow/writer/redirect against native's block, registers kept PASS\n");
}
/* FR24: the facing factor stub runs for real. test_fr_fac_run enters block[0xd0] with rdi = block[0xc8] (a model whose
 * +0x98 holds m), xmm0-5 from +0x00..+0x5f (xmm1 lane 0 = m), xmm13 from +0x60 (lane 0 = 1.0), sentinels in rax, rcx,
 * rdx, r8-r11, rsp 8 off 16; the back jump leads to test_fr_fac_back, which stores xmm0-5, xmm13, rax, rcx, rdx, r8-r11
 * and rdi (+0x80..+0xbf). rbx holds the block throughout. */
__asm__(
".text\n"
"test_fr_fac_run:\n push %rbp\n push %rbx\n push %rsi\n push %rdi\n mov %rsp,%rbp\n sub $0x40,%rsp\n movdqu %xmm13,(%rsp)\n"
" mov %rcx,%rbx\n mov 0xc8(%rbx),%rdi\n"
" movups (%rbx),%xmm0\n movups 0x10(%rbx),%xmm1\n movups 0x20(%rbx),%xmm2\n movups 0x30(%rbx),%xmm3\n movups 0x40(%rbx),%xmm4\n"
" movups 0x50(%rbx),%xmm5\n movups 0x60(%rbx),%xmm13\n"
" movabs $0x0123456789abcdef,%rax\n movabs $0x1111111111111111,%rcx\n movabs $0x2222222222222222,%rdx\n movabs $0x8888888888888888,%r8\n"
" movabs $0x9999999999999999,%r9\n movabs $0xaaaaaaaaaaaaaaaa,%r10\n movabs $0xbbbbbbbbbbbbbbbb,%r11\n and $-16,%rsp\n sub $8,%rsp\n jmp *0xd0(%rbx)\n"
"test_fr_fac_back:\n movups %xmm0,(%rbx)\n movups %xmm1,0x10(%rbx)\n movups %xmm2,0x20(%rbx)\n movups %xmm3,0x30(%rbx)\n movups %xmm4,0x40(%rbx)\n"
" movups %xmm5,0x50(%rbx)\n movups %xmm13,0x60(%rbx)\n mov %rax,0x80(%rbx)\n mov %rcx,0x88(%rbx)\n mov %rdx,0x90(%rbx)\n"
" mov %r8,0x98(%rbx)\n mov %r9,0xa0(%rbx)\n mov %r10,0xa8(%rbx)\n mov %r11,0xb0(%rbx)\n mov %rdi,0xb8(%rbx)\n"
" lea -0x40(%rbp),%rsp\n movdqu (%rsp),%xmm13\n mov %rbp,%rsp\n pop %rdi\n pop %rsi\n pop %rbx\n pop %rbp\n ret\n"
);
extern void test_fr_fac_run(unsigned char *block);
extern char test_fr_fac_back[];
#define TFC_BLOCK 0xd8u
static __declspec(align(16)) unsigned char tfc_model[0x100];
/* One run from entry with m; the block holds the registers after. xmm13 = (1.0, patterns), the other lanes patterns. */
static void tfc_run(const void *entry,uint32_t m,unsigned char block[TFC_BLOCK]) {
    for(unsigned k=0;k<0x60;k++) block[k]=(unsigned char)(0x40+k);
    memcpy(block+0x10,&m,4);
    const uint32_t x13[4]={0x3f800000u,0x3f000000u,0x40000000u,0x40400000u};memcpy(block+0x60,x13,16);memset(block+0x70,0,0x50);
    memset(tfc_model,0x5a,sizeof(tfc_model));memcpy(tfc_model+0x98,&m,4);
    void *mp=tfc_model;memcpy(block+0xc8,&mp,8);memcpy(block+0xd0,&entry,8);
    test_fr_fac_run(block);
}
/* The stub against native's block alone (its 16 bytes with the operand on the page, then the back jump): every register
 * the same as native's but xmm0's lane 0, which is native's factor or, in 'redirect', ours. */
static void tfc_check(const unsigned char *stub,const unsigned char *reference,uint32_t m,uint32_t want) {
    unsigned char a[TFC_BLOCK],b[TFC_BLOCK];uint32_t got,ref;
    tfc_run(reference,m,a);tfc_run(stub,m,b);memcpy(&got,b,4);memcpy(&ref,a,4);
    if(got!=want) {printf("fp redirect facing stub: m %08x got %08x want %08x\n",m,got,want);assert(0);}
    assert(ref==fr_facing_windows(m) && !memcmp(a+4,b+4,0xbc));
    uint64_t r[8];memcpy(r,b+0x80,64);
    assert(r[0]==0x0123456789abcdefull && r[1]==0x1111111111111111ull && r[2]==0x2222222222222222ull && r[3]==0x8888888888888888ull);
    assert(r[4]==0x9999999999999999ull && r[5]==0xaaaaaaaaaaaaaaaaull && r[6]==0xbbbbbbbbbbbbbbbbull && r[7]==(uint64_t)(uintptr_t)tfc_model);
    uint32_t x1;memcpy(&x1,b+0x10,4);assert(x1==fr_bits(fr_f32(m)*0.636619747f)); /* xmm1 as native left it: m*k */
}
static void test_fr_fac_stubs(void) {
    /* Strict vectors (numpy float32): 0828fc's tick-43 angle m = 3ee75aac: Windows 3f365b91 (its FR23 +0x9c), Linux
     * 3f365b90 (its FR23 +0x8c); m = 3fb46543: 3dd28190 / 3dd28188; equal at 0 (1.0), pi/2 (0) and 1.0 (3eba0cfa). */
    assert(fr_facing_windows(0x3ee75aacu)==0x3f365b91u && fr_facing(0x3ee75aacu)==0x3f365b90u);
    assert(fr_facing_windows(0x3fb46543u)==0x3dd28190u && fr_facing(0x3fb46543u)==0x3dd28188u);
    assert(fr_facing_windows(0)==0x3f800000u && fr_facing(0)==0x3f800000u && fr_facing_windows(0x3fc90fdbu)==0 && fr_facing(0x3fc90fdbu)==0);
    assert(fr_facing_windows(0x3f800000u)==0x3eba0cfau && fr_facing(0x3f800000u)==0x3eba0cfau);
    unsigned char *page=fr_alloc_page(),*saved=fr_page;DWORD o2,i2;assert(page);
    assert(VirtualProtect(page,0x1000,PAGE_READWRITE,&o2));fr_page=page;fr_write_code(page);fr_page=saved;
    /* the reference: native's block at +0xf00 with its operand at +0xf20 (k), then jmp [rip] -> the harness's back */
    memcpy(page+0xf00,fr_fac_block,sizeof(fr_fac_block));
    {   const int32_t at=0x20-8;memcpy(page+0xf04,&at,4);const uint32_t k=FR_FAC_K_BITS;memcpy(page+0xf20,&k,4);
        unsigned char back[14]={0xff,0x25,0,0,0,0};void *to=(void*)test_fr_fac_back;memcpy(back+6,&to,8);memcpy(page+0xf10,back,14);}
    assert(!memcmp(page+FR_FAC_STUB,page+0xf00,4) && !memcmp(page+FR_FAC_STUB+8,fr_fac_block+8,8)); /* the stub's block, k at +0x18 */
    {uint32_t k;memcpy(&k,page+FR_FAC_STUB+0x18,4);assert(k==FR_FAC_K_BITS && page[FR_FAC_STUB+0x10]==0xeb);}
    assert(VirtualProtect(page,0x1000,PAGE_EXECUTE_READ,&i2));FlushInstructionCache(GetCurrentProcess(),page,0x1000);
    {   unsigned char back[14]={0xff,0x25,0,0,0,0};void *to=(void*)test_fr_fac_back;memcpy(back+6,&to,8);
        unsigned char *at=image+FR_FAC_BACK;DWORD old;assert(VirtualProtect(at,14,PAGE_EXECUTE_READWRITE,&old));
        memcpy(at,back,14);FlushInstructionCache(GetCurrentProcess(),at,14);
    }
    const int saved_mode=fr_mode;const uint32_t saved_active=stats.active;stats.active=1;
    const LONG64 n0=fr_n[FR_FAC],d0=fr_diff[FR_FAC],w0=fr_fac_written,h0=fr_hits[FR_HIT_FAC];
    const unsigned char *stub=page+FR_FAC_STUB,*ref=page+0xf00;
    fr_mode=FR_OFF;tfc_check(stub,ref,0x3ee75aacu,0x3f365b91u);       /* off: native's */
    fr_mode=FR_SHADOW;tfc_check(stub,ref,0x3ee75aacu,0x3f365b91u);    /* shadow: compared, native kept */
    fr_mode=FR_WRITER;tfc_check(stub,ref,0x3fb46543u,0x3dd28190u);    /* writer only: shadowed */
    fr_mode=FR_REDIRECT;tfc_check(stub,ref,0x3ee75aacu,0x3f365b90u);  /* redirect: ours, Linux's */
    tfc_check(stub,ref,0x3f800000u,0x3eba0cfau);                        /* equal: nothing written */
    /* 5 runs, compared 4, differing 3, written 1 */
    assert(fr_n[FR_FAC]==n0+4 && fr_diff[FR_FAC]==d0+3 && fr_fac_written==w0+1 && fr_hits[FR_HIT_FAC]==h0+5);
    char out[2048];assert(fr_status(out,sizeof(out))>0 && strstr(out," fac=8:") && strstr(out," fac_hits=") && strstr(out," fac_written="));
    fr_mode=saved_mode;stats.active=saved_active;
    memset(image+FR_FAC_BACK,0,14);
    VirtualFree(page,0,MEM_RELEASE);
    printf("fp redirect facing factor: strict vectors (0828fc's tick 43), stub off/shadow/writer/redirect against native's block, registers kept PASS\n");
}
/* FR22: 8bd90's highway segments 2 and 3 (A, B, C at the segment's +0x10; the FR21 Windows capture at tick 2), whose
 * stored lengths split: n = 100 Linux 478e6bb1 / Windows 478e6bb2 (2), 470fcea2 / 470fcea0 (3); n = 64 equal. */
static const uint32_t tbl_coef[2][12]={
    {0x469a350cu,0,0x463e9a96u,0,0xc69ad8f8u,0,0xc690eab4u,0,0xc7828a7cu,0,0xc6afd36au,0},
    {0x45f5ac58u,0,0x45c6e2e2u,0,0x45dc2a98u,0,0xc4604600u,0,0xc7389e1bu,0,0xc6b3c0acu,0}};
/* The hook's native: a stand-in at image+FR_BL_FN storing Windows' length. */
static void tbl_native(unsigned char *segment,uint32_t n) {const uint32_t v=sl_arc_length_windows(segment+0x10,n);memcpy(segment+8,&v,4);}
static uint32_t tbl_run(uint32_t which,uint32_t n,int foreign) {
    static __declspec(align(16)) unsigned char seg[0x40];memset(seg,0x5a,sizeof(seg));
    const void *vt=foreign?(const void*)(image+0x2a84fa0u):(const void*)(image+FR_BL_VTABLE);memcpy(seg,&vt,8);memcpy(seg+0x10,tbl_coef[which],48);
    fr_bl_hook(seg,n);
    uint32_t v;memcpy(&v,seg+8,4);
    for(unsigned k=0x0c;k<0x10;k++) assert(seg[k]==0x5a);
    assert(!memcmp(seg+0x10,tbl_coef[which],48));
    return v;
}
static void test_fr_bl_hook(void) {
    assert(sl_arc_length((const unsigned char*)tbl_coef[0],100)==0x478e6bb1u && sl_arc_length_windows((const unsigned char*)tbl_coef[0],100)==0x478e6bb2u);
    assert(sl_arc_length((const unsigned char*)tbl_coef[1],100)==0x470fcea2u && sl_arc_length_windows((const unsigned char*)tbl_coef[1],100)==0x470fcea0u);
    assert(sl_arc_length((const unsigned char*)tbl_coef[1],64)==0x470fce9eu && sl_arc_length_windows((const unsigned char*)tbl_coef[1],64)==0x470fce9eu);
    assert(sl_arc_length((const unsigned char*)tbl_coef[1],7)==0x470fce51u && sl_arc_length_windows((const unsigned char*)tbl_coef[1],7)==0x470fce52u);
    assert(sl_arc_length((const unsigned char*)tbl_coef[1],0)==0 && sl_arc_length_windows((const unsigned char*)tbl_coef[1],0)==0);
    unsigned char code[14]={0xff,0x25,0,0,0,0};void *to=(void*)tbl_native;memcpy(code+6,&to,8);
    unsigned char *at=image+FR_BL_FN;DWORD old;assert(VirtualProtect(at,14,PAGE_EXECUTE_READWRITE,&old));
    memcpy(at,code,14);FlushInstructionCache(GetCurrentProcess(),at,14);
    const int saved_mode=fr_mode;const uint32_t saved_active=stats.active;stats.active=1;
    const LONG64 n0=fr_n[FR_BL],d0=fr_diff[FR_BL],w0=fr_bl_written,h100=fr_hits[FR_HIT_BL100],h10=fr_hits[FR_HIT_BL10],hn=fr_hits[FR_HIT_BLN],hf=fr_hits[FR_HIT_BL_FOREIGN];
    fr_mode=FR_OFF;assert(tbl_run(1,100,0)==0x470fcea0u);           /* off: native, not compared */
    fr_mode=FR_SHADOW;assert(tbl_run(1,100,0)==0x470fcea0u);        /* shadow: compared, native kept */
    fr_mode=FR_WRITER;assert(tbl_run(0,100,0)==0x478e6bb2u);        /* writer only: the length shadowed */
    fr_mode=FR_REDIRECT;assert(tbl_run(1,100,0)==0x470fcea2u);      /* redirect: Linux's i/n stored */
    assert(tbl_run(0,100,0)==0x478e6bb1u);
    assert(tbl_run(1,64,0)==0x470fce9eu);                           /* equal both ways: nothing written */
    assert(tbl_run(1,7,0)==0x470fce51u && tbl_run(1,10,0)==0x470fce7au);
    assert(tbl_run(1,100,1)==0x470fcea0u);                          /* another vtable: native left, counted */
    /* 9 calls: 8 seen (100 x5, 64 and 7 in other, 10), compared 7, differing 5 (the n = 100 ones and 7), written 3 */
    assert(fr_hits[FR_HIT_BL100]==h100+5 && fr_hits[FR_HIT_BL10]==h10+1 && fr_hits[FR_HIT_BLN]==hn+2 && fr_hits[FR_HIT_BL_FOREIGN]==hf+1);
    assert(fr_n[FR_BL]==n0+7 && fr_diff[FR_BL]==d0+5 && fr_bl_written==w0+3 && fr_max_ulp[FR_BL]>=2);
    char out[2048];assert(fr_status(out,sizeof(out))>0 && strstr(out," bl=2:") && strstr(out," bl_hits=") && strstr(out," bl_written="));
    fr_mode=saved_mode;stats.active=saved_active;
    memset(at,0,14);
    printf("fp redirect arc length: strict vectors (8bd90's highway), hook off/shadow/writer/redirect, foreign vtable PASS\n");
}
/* FR26: boxes (half size +0..+8, centre +0x10, radius +0x20) with the strict radius (numpy float32, correctly rounded
 * sqrt of x*x + (y*y + z*z)) and a cached Windows radius rcp(rsqrt(that sum)) as an AMD Zen 5 CPU returns it (stored
 * bits here, not computed). Box 0 is 084533's radius (FR25: 3170.0 cached, Linux 3170.4248), box 2 a box with adbc2's
 * safesize/2 (81.625 / 81.641586). */
static const struct {uint32_t h[3],radius,cached;} tsz_boxes[]={
    {{0x454626ccu,0,0},0x454626ccu,0x45462000u},
    {{0x42220000u,0x42750000u,0x41b80000u},0x4299e4c5u,0x4299e000u},
    {{0x4243f098u,0x4282a065u,0},0x42a3487eu,0x42a34000u},
    {{0x449a5225u,0x42b2051fu,0x3b23d70au},0x449ab8b1u,0x449ab800u}};
static float (*tsz_fn)(const unsigned char *); /* fr_size_hook, or the stub on a page that jumps to it */
static uint32_t tsz_run(unsigned i,uint32_t cached) {
    static __declspec(align(16)) unsigned char box[0x30];memset(box,0x5a,sizeof(box));
    memcpy(box,tsz_boxes[i].h,12);memcpy(box+0x20,&cached,4);
    const uint32_t r=fr_bits(tsz_fn(box));
    assert(!memcmp(box,tsz_boxes[i].h,12) && !memcmp(box+0x20,&cached,4) && box[0x0c]==0x5a && box[0x24]==0x5a); /* the box is read only */
    return r;
}
/* Every mode on the four boxes, a box whose cached radius is not |half| (far), a NaN half (far) and an exact cache. */
static void tsz_modes(float (*fn)(const unsigned char *)) {
    tsz_fn=fn;
    const int saved_mode=fr_mode;const uint32_t saved_active=stats.active;stats.active=1;
    const LONG64 n0=fr_sz_n,d0=fr_sz_diff,h0=fr_sz_hits,w0=fr_sz_written,f0=fr_sz_far;
    fr_mode=FR_OFF;for(unsigned i=0;i<4;i++) assert(tsz_run(i,tsz_boxes[i].cached)==tsz_boxes[i].cached);    /* native, not compared */
    fr_mode=FR_SHADOW;for(unsigned i=0;i<4;i++) assert(tsz_run(i,tsz_boxes[i].cached)==tsz_boxes[i].cached); /* compared, native kept */
    fr_mode=FR_WRITER;assert(tsz_run(0,tsz_boxes[0].cached)==tsz_boxes[0].cached);                           /* writer only: shadowed */
    fr_mode=FR_REDIRECT;for(unsigned i=0;i<4;i++) assert(tsz_run(i,tsz_boxes[i].cached)==tsz_boxes[i].radius); /* ours */
    assert(tsz_run(1,tsz_boxes[1].radius)==tsz_boxes[1].radius);                                              /* exact already */
    assert(tsz_run(0,0x3f800000u)==0x3f800000u);                                                              /* 1.0: far, native */
    {   static __declspec(align(16)) unsigned char box[0x30];memset(box,0,sizeof(box));
        const uint32_t nan=0x7fc00000u,cached=0x40000000u;memcpy(box+4,&nan,4);memcpy(box+0x20,&cached,4);
        tsz_fn=fn;assert(fr_bits(fn(box))==cached);                                                            /* NaN: far, native */
        memset(box,0,12);const uint32_t zero=0;memcpy(box+0x20,&zero,4);assert(fr_bits(fn(box))==0);           /* 0 = 0 */
    }
    /* 4 off (hits only) + 4 shadow + 1 writer + 4 redirect + exact + far + NaN + zero: hits 17, compared 13, differing
     * 11 (shadow 4, writer 1, redirect 4, far, NaN), far 2, written 4 */
    assert(fr_sz_hits==h0+17 && fr_sz_n==n0+13 && fr_sz_diff==d0+11 && fr_sz_far==f0+2 && fr_sz_written==w0+4 && fr_sz_max_ulp>=0x4000);
    char out[2048];assert(fr_status_win(out,sizeof(out))>0 && strstr(out," fac=") && strstr(out," size=") && strstr(out," size_hits=") &&
        strstr(out," size_written=") && strstr(out," size_far=") && fr_status(out,sizeof(out))>0 && !strstr(out," size="));
    assert(fr_size_near(0x45462000u,0x454626ccu) && fr_size_near(0x454626ccu,0x45462000u) && !fr_size_near(0x3f800000u,0x454626ccu) &&
           !fr_size_near(0x7fc00000u,0x3f800000u) && fr_size_near(0,0) && !fr_size_near(0x7f800000u,0x7f800000u));
    fr_mode=saved_mode;stats.active=saved_active;
}
static void test_fr_size_hook(void) {
    for(unsigned i=0;i<4;i++) {
        unsigned char box[12];memcpy(box,tsz_boxes[i].h,12);
        if(fr_box_radius(box)!=tsz_boxes[i].radius) {printf("fp redirect size box %u: %08x want %08x\n",i,fr_box_radius(box),tsz_boxes[i].radius);assert(0);}
    }
    tsz_modes(fr_size_hook);
    printf("fp redirect size radius: strict vectors (084533's and adbc2's radii), hook off/shadow/writer/redirect, far and NaN kept native PASS\n");
}
/* The v stub as 0x6dcce6 calls it: rcx, rdx, xmm2, r9, three on the stack, and d in xmm7 (callee-saved). */
__asm__(
".text\n"
"test_fr_v_run:\n push %rbx\n sub $0x50,%rsp\n movdqu %xmm7,0x40(%rsp)\n mov %rcx,%r10\n mov %rdx,%rcx\n mov %r8,%rdx\n movaps %xmm3,%xmm2\n"
" mov 0x80(%rsp),%r9\n mov 0x88(%rsp),%rax\n mov %rax,0x20(%rsp)\n mov 0x90(%rsp),%rax\n mov %rax,0x28(%rsp)\n mov 0x98(%rsp),%rax\n mov %rax,0x30(%rsp)\n"
" movss 0xa0(%rsp),%xmm7\n call *%r10\n movdqu 0x40(%rsp),%xmm7\n add $0x50,%rsp\n pop %rbx\n ret\n"
);
extern uint64_t test_fr_v_run(const void *stub,void *o20,unsigned char *buf,double time,uint64_t a4,uint64_t a5,uint64_t a6,uint64_t a7,float d);
static unsigned char *tfr_vstub;
static int tfr_win_form;
static uint64_t tfr_v(void *o20,unsigned char *buf,double time,uint64_t a4,uint64_t a5,uint64_t a6,uint64_t a7) {
    assert(o20==tfr_ship && time==2.5 && a4==4 && a5==5 && a6==6 && a7==7);
    const float v[8]={-1.0f,2.0f,-100.0f,0,0,0,0,0};memcpy(buf,v,32);return 0x1234;
}
static uint64_t tfr_mulps(void *a,void *b,void *c,void *e) {(void)b;(void)c;(void)e;assert(a==tfr_ship);return 0;}
/* The first call's v path: the branch, then v through the stub, then the max block in Linux's or Windows' form. */
static void tfr_params_v(void *o20,unsigned char *slot,uint64_t flag,double time,uint64_t a5,uint64_t a6) {
    (void)flag;(void)time;(void)a5;(void)a6;(void)fr_mulps_hook(o20,0,0,0);
    unsigned char v[32];const float d=3.0f;static const unsigned lanes[4]={2,2,0,1};
    assert(test_fr_v_run(tfr_vstub,o20,v,2.5,4,5,6,7,d)==0x1234);
    for(unsigned i=0;i<4;i++) {
        float a,c;memcpy(&a,v+4*lanes[i],4);a=fr_abs(a);memcpy(&c,slot+4*i,4);
        const float n=tfr_win_form?a*(1.0f/d):a/d,o=n>c?n:c;memcpy(slot+4*i,&o,4);
    }
}
/* A fake ship for the writer: its vtable's five getters (+0x1fe8..+0x2008) return tfw_r, +0x398 points at tfw_p when
 * p is present; D, A and the table ([0x146cf1538] in this image) in Windows' layout. */
static float tfw_r[5];
static float tfw_r0(void *o) {(void)o;return tfw_r[0];}
static float tfw_r1(void *o) {(void)o;return tfw_r[1];}
static float tfw_r2(void *o) {(void)o;return tfw_r[2];}
static float tfw_r3(void *o) {(void)o;return tfw_r[3];}
static float tfw_r4(void *o) {(void)o;return tfw_r[4];}
static void *tfw_vt[0x410];
static unsigned char tfw_ship[0x400],tfw_data[0xa00],tfw_a[0x40],tfw_p[0x60],tfw_table[0x5100];
static void tfw_load(const fr_w_in *in) {
    static float (*const fns[5])(void *)={tfw_r0,tfw_r1,tfw_r2,tfw_r3,tfw_r4};
    void *vt=tfw_vt,*t=tfw_table,*p=in->has_p?(void*)tfw_p:NULL;memcpy(tfw_ship,&vt,8);memcpy(tfw_ship+0x398,&p,8);memcpy(tfw_p+0x50,&in->p,4);
    for(unsigned i=0;i<5;i++) {tfw_vt[(0x1fe8u>>3)+i]=(void*)fns[i];memcpy(&tfw_r[i],&in->r[i],4);}
    memcpy(tfw_a,in->a,16);memcpy(tfw_data+0x904,in->d,20);memcpy(tfw_data+0x988,in->dm,16);
    memcpy(tfw_table+0x5050,&in->t_scale,4);memcpy(tfw_table+0x300,&in->k,4);memcpy(tfw_table+0x318,&in->t_clamp,4);
    memcpy(image+0x6cf1538u,&t,8);
}
/* A stand-in writer: writer vector 0's +0x0..+0xc in Linux's order, or Windows' (tfr_win_form). */
static uint64_t tfr_writer(void *o20,void *data,void *a,void *slot) {
    assert(o20==tfw_ship && data==tfw_data && a==tfw_a);
    uint32_t q[4];if(tfr_win_form) tfr_w_windows(&tfr_w_vectors[0].in,q);else fr_w_recompute(&tfr_w_vectors[0].in,q);
    memcpy(slot,q,16);return 0;
}
static unsigned char *tfr_wstub;
static void tfr_params_w(void *o20,unsigned char *slot,uint64_t flag,double time,uint64_t a5,uint64_t a6) {
    (void)o20;(void)flag;(void)time;(void)a5;(void)a6;(void)((fr_writer_fn)(void*)tfr_wstub)(tfw_ship,tfw_data,tfw_a,slot);
}
extern uint32_t test_fr_tail_run(const void *stub,void *slot);
extern char test_fr_tail_back[];
/* ---- FR27: the GetSafePos solver (lockstep_safe_pos.h, fsp_run, fsp_arm). ---- */
static uint32_t tfsp_seed;
static float tfsp_rand(void) {tfsp_seed=tfsp_seed*1664525u+1013904223u;return (float)(tfsp_seed>>8)*(1.0f/16777216.0f);}
static float tfsp_range(float a,float b) {return a+(b-a)*tfsp_rand();}
typedef struct {_Alignas(16) unsigned char boxes[6*FSP_BOX];_Alignas(16) float p[4];_Alignas(16) float dir[4];unsigned n;float r,a5,a9;int y;} tfsp_scene;
/* A random scene as the offline fuzz builds them: up to six boxes (half sizes, centres, positions, rows from a unit
 * quaternion or the identity, the radius |half| or 0), a point among them, a radius, a unit direction (sometimes
 * vertical), a5 0 or up to 1.5, allow-y, a9 25000, 0 or a few hundred. */
static void tfsp_make(tfsp_scene *s) {
    memset(s,0,sizeof(*s));s->n=1+(unsigned)(tfsp_rand()*6.0f);if(s->n>6) s->n=6;
    const float scale=tfsp_rand()<0.34f?0.1f:tfsp_rand()<0.5f?1.0f:30.0f;
    for(unsigned b=0;b<s->n;b++) {
        float box[28];memset(box,0,sizeof(box));
        for(int k=0;k<3;k++) {box[k]=tfsp_range(5.0f,300.0f)*scale;box[4+k]=tfsp_range(-100.0f,100.0f)*scale;box[12+k]=tfsp_range(-100.0f,100.0f)*scale;}
        box[7]=1.0f;box[8]=tfsp_rand()<0.05f?0.0f:fsp_len3(box);
        if(tfsp_rand()<0.2f) box[12]=box[13]=box[14]=0.0f;
        if(tfsp_rand()<0.25f) {box[16]=box[21]=box[26]=1.0f;}
        else {
            float q[4],l=0.0f;for(int k=0;k<4;k++) {q[k]=tfsp_range(-1.0f,1.0f);l+=q[k]*q[k];}
            l=fsp_sqrt(l);if(l<1e-3f) {q[0]=1.0f;l=1.0f;}
            const float w=q[0]/l,x=q[1]/l,y=q[2]/l,z=q[3]/l;
            const float m[9]={1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w),2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w),2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)};
            for(int r=0;r<3;r++) for(int c=0;c<3;c++) box[16+4*r+c]=m[3*r+c];
        }
        memcpy(s->boxes+b*FSP_BOX,box,FSP_BOX);
    }
    for(int k=0;k<3;k++) {s->p[k]=tfsp_range(-60.0f,60.0f)*scale;s->dir[k]=tfsp_range(-1.0f,1.0f);}
    if(tfsp_rand()<0.1f) s->dir[0]=s->dir[2]=0.0f;
    const float dl=fsp_len3(s->dir);if(dl>1e-6f) for(int k=0;k<3;k++) s->dir[k]=s->dir[k]/dl;
    s->r=tfsp_rand()<0.5f?tfsp_range(0.5f,50.0f):tfsp_range(50.0f,2000.0f)*scale;
    s->a5=tfsp_rand()<0.6f?0.0f:tfsp_range(0.05f,1.5f);s->y=tfsp_rand()<0.5f;
    s->a9=tfsp_rand()<0.34f?25000.0f:tfsp_rand()<0.5f?0.0f:tfsp_range(10.0f,500.0f)*scale;
}
typedef struct {float r;const float *dir;float a5,a9;int y,perturb;unsigned calls;} tfsp_args;
/* A stand-in for the native solver: ours, and with perturb one ulp more on x afterwards. */
static void tfsp_native(const void *args,void *vec,float *p) {
    tfsp_args *a=(tfsp_args*)(uintptr_t)args;uintptr_t v[2];memcpy(v,vec,16);++a->calls;
    if(v[1]<v[0] || (v[1]-v[0])%FSP_BOX) return; /* the refused vectors below */
    fsp_solve((unsigned char*)v[0],(v[1]-v[0])/FSP_BOX,p,a->r,a->dir,a->a5,a->y,NULL,a->a9);
    if(a->perturb) {uint32_t x;memcpy(&x,p,4);++x;memcpy(p,&x,4);}
}
static uint64_t tfsp_w_native(void *vec,float *p,float r,float *dir,float a5,uint64_t a6,uint64_t a7,float *move,float a9) {
    (void)a6;(void)move;uintptr_t v[2];memcpy(v,vec,16);
    fsp_solve((unsigned char*)v[0],(v[1]-v[0])/FSP_BOX,p,r,dir,a5,(int)(a7&0xff),NULL,a9);return 0;
}
static void test_fsp_synthetic(void) {
    assert(fsp_selftest() && x4pm_kat()==X4PM_KAT_EXPECTED);
    assert(fsp_lua_mode("redirect",8)==FSP_REDIRECT && fsp_lua_mode("shadow",6)==FSP_SHADOW && fsp_lua_mode("writer",6)<0 && fsp_lua_mode(NULL,0)<0);
    const uint32_t saved_active=stats.active;stats.active=1;
    static _Alignas(16) unsigned char boxes[3*FSP_BOX],kept[3*FSP_BOX];_Alignas(16) float p[4],dir[4];
    static const uint32_t pa[4]={0x404dcb7cu,0x40626557u,0xbf030421u,0x3f800000u},da[4]={0x3f6ee60au,0xbd9c4598u,0xbeb3cc79u,0},
        oa[4]={0x41f34a4au,0x3efe2f08u,0x41e3b01du,0x3f800000u};
    memcpy(boxes,fsp_kat_a_boxes,sizeof(boxes));memcpy(kept,boxes,sizeof(boxes));memcpy(p,pa,16);memcpy(dir,da,16);
    uintptr_t vec[3]={(uintptr_t)boxes,(uintptr_t)(boxes+sizeof(boxes)),(uintptr_t)(boxes+sizeof(boxes))};
    tfsp_args a={fsp_f(0x412ae48eu),dir,fsp_f(0x3f96a609u),0.0f,1,0,0};
    fsp_calls=fsp_ours=fsp_n=fsp_diff=fsp_max_ulp=fsp_order=fsp_move=fsp_invalid=fsp_x0=fsp_nocopy=0;
    /* off: native alone */
    fsp_mode=FSP_OFF;fsp_run(tfsp_native,&a,vec,p,a.r,dir,a.a5,1,NULL,a.a9,0);
    assert(a.calls==1 && fsp_calls==1 && fsp_n==0 && !memcmp(p,oa,16));
    /* redirect: native on a copy, ours on the scene; equal */
    memcpy(boxes,kept,sizeof(boxes));memcpy(p,pa,16);fsp_mode=FSP_REDIRECT;
    fsp_run(tfsp_native,&a,vec,p,a.r,dir,a.a5,1,NULL,a.a9,0);
    assert(a.calls==2 && fsp_ours==1 && fsp_n==1 && fsp_diff==0 && fsp_order==0 && !memcmp(p,oa,16));
    /* a native one ulp off: counted, ours stays on the scene */
    memcpy(boxes,kept,sizeof(boxes));memcpy(p,pa,16);a.perturb=1;
    fsp_run(tfsp_native,&a,vec,p,a.r,dir,a.a5,1,NULL,a.a9,1);
    assert(fsp_n==2 && fsp_diff==1 && fsp_max_ulp==1 && fsp_x0==1 && !memcmp(p,oa,16));
    /* shadow: native on the scene (its bits stay), ours on the copy */
    memcpy(boxes,kept,sizeof(boxes));memcpy(p,pa,16);fsp_mode=FSP_SHADOW;
    fsp_run(tfsp_native,&a,vec,p,a.r,dir,a.a5,1,NULL,a.a9,0);
    {uint32_t x;memcpy(&x,p,4);assert(fsp_n==3 && fsp_diff==2 && fsp_ours==2 && x==oa[0]+1);}
    a.perturb=0;
    /* a move direction: native alone; an invalid vector: native alone */
    const float move[4]={1,0,0,0};memcpy(p,pa,16);const unsigned before=a.calls;
    fsp_run(tfsp_native,&a,vec,p,a.r,dir,a.a5,1,move,a.a9,0);assert(fsp_move==1 && a.calls==before+1 && fsp_n==3);
    uintptr_t bad[3]={vec[1],vec[0],vec[0]};fsp_mode=FSP_REDIRECT;
    fsp_run(tfsp_native,&a,bad,p,a.r,dir,a.a5,1,NULL,a.a9,0);assert(fsp_invalid==1 && a.calls==before+2 && fsp_n==3);
    uintptr_t odd[3]={vec[0],vec[0]+FSP_BOX+4,vec[0]};
    fsp_run(tfsp_native,&a,odd,p,a.r,dir,a.a5,1,NULL,a.a9,0);assert(fsp_invalid==2);
    /* the box order compares what boxes mean (the padding +0x24..+0x2f does not count) */
    memcpy(boxes,kept,sizeof(boxes));boxes[0x24]^=0xff;assert(fsp_same_order(boxes,kept,3));boxes[0x24]^=0xff;
    boxes[0x30]^=1;assert(!fsp_same_order(boxes,kept,3));boxes[0x30]^=1;
    /* the step chain's FR25 entry hook routes the call here while armed */
    memcpy(boxes,kept,sizeof(boxes));memcpy(p,pa,16);fsp_w_original=tfsp_w_native;stc_sp_route=fsp_hook_w;
    const LONG64 ours_before=fsp_ours;
    stc_sp_solver(vec,p,a.r,dir,a.a5,0,1,NULL,a.a9);
    assert(fsp_ours==ours_before+1 && fsp_diff==2 && !memcmp(p,oa,16));
    stc_sp_route=NULL;fsp_w_original=NULL;
    char out[2048];fsp_installed=1;fsp_route=1;fsp_stage="armed";
    assert(fr_status(out,sizeof(out))>0 && strstr(out," solver=redirect sp_stage=armed sp_entry=chain sp_calls=8 sp_ours=3 sp=1:2:4 sp_order=0 sp_move=1 sp_invalid=2 sp_x0=1 sp_nocopy=0"));
    fsp_installed=0;fsp_route=0;fsp_stage="idle";
    /* arm refusals: the mode, fp_redirect not armed, the zero image's pins */
    const DWORD saved_ui=ui_thread;const uint64_t saved_issued=stats.issued;const int saved_boot=boot_mode,saved_phase=boot_phase,saved_fr=fr_installed;
    ui_thread=GetCurrentThreadId();stats.issued=0;boot_mode=0;boot_phase=0;fr_installed=0;
    assert(!fsp_arm(7) && !strcmp(fsp_stage,"mode") && !fsp_arm(FSP_REDIRECT) && !strcmp(fsp_stage,"refused"));
    fr_installed=1;assert(!fsp_arm(FSP_REDIRECT) && !strcmp(fsp_stage,"pins") && !fsp_installed);fr_installed=saved_fr;
    ui_thread=saved_ui;stats.issued=saved_issued;boot_mode=saved_boot;boot_phase=saved_phase;
    fsp_mode=FSP_OFF;fsp_stage="idle";stats.active=saved_active;
    fsp_calls=fsp_ours=fsp_n=fsp_diff=fsp_max_ulp=fsp_order=fsp_move=fsp_invalid=fsp_x0=fsp_nocopy=0;
    printf("fp redirect FR27 solver: known answers, modes/compare/move/invalid/x0/order, the step chain's route, status, arm refusals PASS\n");
}
#include "test_lockstep_fp_attack.h"
#include "test_lockstep_fp_drones.h" /* FR30 */
static void test_fp_redirect(void) {
    test_fr_vectors();
    test_fsp_synthetic();
    test_fa_synthetic();
    test_fdr_synthetic();
    const uint64_t saved_issued=stats.issued;const DWORD saved_ui=ui_thread;const uint32_t saved_active=stats.active;
    const int saved_boot=boot_mode,saved_phase=boot_phase,saved_ids=world_identity_installed;
    world_identity_entry *prior=world_identities;const unsigned prior_count=world_identity_count;const LONG prior_sealed=world_identity_sealed;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);world_identity_count=0;world_identity_sealed=0;assert(world_identity_record(0x5151,0x7dcd0));
    const uint64_t runtime=0x5151;memcpy(tfr_ship+8,&runtime,8);
    const uint32_t ca=tfr_vectors[0].ca,cb=tfr_vectors[0].cb;memcpy(tfr_comp+FR_COMP_A,&ca,4);memcpy(tfr_comp+FR_COMP_B,&cb,4);
    tfr_w=fr_f32(tfr_vectors[0].w);
    fr_native_refresh=tfr_refresh;fr_native_g4=tfr_g4;fr_native_g0=tfr_g0;fr_native_m=tfr_m;fr_native_params=tfr_params;
    /* Arming refusals: mode, ticks, the watched IDs' identities, then the zero image's pins. */
    stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();
    assert(!fr_arm(7,3) && !strcmp(fr_stage,"mode") && !fr_arm(FR_SHADOW,0) && !strcmp(fr_stage,"ticks"));
    assert(fr_watch(0x7dcd0) && fr_watch(0x7dcd0) && fr_id_count==1 && !fr_watch(0));
    world_identity_installed=0;assert(!fr_arm(FR_SHADOW,3) && !strcmp(fr_stage,"needs"));world_identity_installed=1;
    assert(!fr_arm(FR_SHADOW,3) && !strcmp(fr_stage,"pins") && !fr_installed);
    boot_phase=1;assert(!fr_arm(FR_SHADOW,3) && !strcmp(fr_stage,"refused"));boot_phase=0;
    /* The flow, as armed: one call in the load (tick 0), then ticks. */
    assert(fr_arm_ready(FR_SHADOW,3));fr_mode=FR_SHADOW;
    unsigned char slot[0x1a0];memset(slot,0,sizeof(slot));const float s2c=fr_f32(0x43820000u);memcpy(slot+0x2c,&s2c,4);
    void *owner=(void*)0x1234;
    boot_phase=2;stats.issued=0;tfr_refresh(owner,slot,0);          /* the load: compared, kept unkeyed */
    char out[2048];assert(!fr_row(0,out,sizeof(out)));              /* rows only after admission */
    boot_phase=3;stats.issued=1;
    tfr_refresh(owner,slot,0);                                      /* equal */
    tfr_refresh(owner,slot,1);                                      /* the flag path: skipped */
    tfr_perturb=1;tfr_refresh(owner,slot,0);tfr_perturb=0;          /* native +0x0 one ulp off */
    tfr_branch=1;tfr_refresh(owner,slot,0);tfr_branch=0;            /* the M branch */
    stats.issued=4;tfr_refresh(owner,slot,0);                       /* compared, no row (tick 4 > 3) */
    unsigned char frame[0x100];memset(frame,0,sizeof(frame));(void)fr_g4_hook(tfr_comp,tfr_ship,frame+0x80); /* outside a refresh */
    fr_mode=FR_OFF;tfr_refresh(owner,slot,0);fr_mode=FR_SHADOW;     /* off: native only */
    assert(fr_status(out,sizeof(out))>0);
    assert(strstr(out,"calls=7 compared=5 load_compared=1 skipped=1 unfinished=0 no_context=0 orphans=1 m_branch=1 v8_branch=0 comp_changed=0"));
    assert(strstr(out," f0=1:1:5 f4=0:0:5 f24=0:0:5 m=0:0:2 f0_out=1:1:5 f4_out=0:0:5 p0=0:0:0 p4=0:0:0 p8=0:0:0 pc=0:0:0 w0=0:0:0 w4=0:0:0 w8=0:0:0 wc=0:0:0 hits=g4:7,g0:6,m4:1,m0:1 params=7 params_flag=1 v=0 mulps=0 writer=0 w_unread=0 w_written=0 ids=1 ticks=3 records=9 dropped=0 scanned=0 keyed=0 rows=0 failures=0"));
    assert(!fr_row(1,out,sizeof(out)));                             /* in order from 0 */
    assert(fr_row(0,out,sizeof(out))>0 && strstr(out,"index=0 record=0 kind=params tick=0 key=000000000007dcd0 ordinal=0 thread=ui flag=0 v_path=0 mulps=0 d=00000000") && strstr(out,"post100=00000000000000000000000000002041"));
    assert(fr_row(1,out,sizeof(out))>0 && strstr(out,"index=1 record=1 kind=compare tick=0 key=000000000007dcd0 ordinal=1 thread=ui seen=3 ca=3b23d70a cb=3b83126f g4=43c9859b g0=4383fc81 w=43960000"));
    assert(strstr(out,"native=468a2c56,43c9859b,43c80000 ours=468a2c56,43c9859b,43c80000 v8=43c80000 v12=43c80000 m4=00000000"));
    assert(fr_row(2,out,sizeof(out))>0 && strstr(out,"kind=params tick=1 key=000000000007dcd0 ordinal=0 thread=ui flag=0"));
    assert(fr_row(3,out,sizeof(out))>0 && strstr(out,"index=3 record=3 kind=compare tick=1 key=000000000007dcd0 ordinal=1"));
    assert(fr_row(4,out,sizeof(out))>0 && strstr(out,"kind=params tick=1 key=000000000007dcd0 ordinal=2 thread=ui flag=1"));
    assert(fr_row(5,out,sizeof(out))>0 && strstr(out,"kind=params"));
    assert(fr_row(6,out,sizeof(out))>0 && strstr(out,"kind=compare tick=1 key=000000000007dcd0 ordinal=4") && strstr(out,"native=468a2c57,"));
    assert(fr_row(7,out,sizeof(out))>0 && strstr(out,"kind=params"));
    assert(fr_row(8,out,sizeof(out))>0 && strstr(out,"ordinal=6 thread=ui seen=f") && strstr(out,"m4=43820000,43c80000,43960000,1,3f124925,3f124925,43c9859b"));
    assert(!fr_row(9,out,sizeof(out)));
    assert(fr_status(out,sizeof(out))>0 && strstr(out,"records=9 dropped=0 scanned=9 keyed=9 rows=9 failures=0"));
    /* The first call's v path: the v stub (d from xmm7) runs for real; +0x0..+0xc as Linux writes them, then as Windows. */
    {
        unsigned char *page=fr_alloc_page(),*saved=fr_page;DWORD o2,i2;assert(page);
        assert(VirtualProtect(page,0x1000,PAGE_READWRITE,&o2));fr_page=page;fr_write_code(page);fr_page=saved;
        assert(VirtualProtect(page,0x1000,PAGE_EXECUTE_READ,&i2));FlushInstructionCache(GetCurrentProcess(),page,0x1000);
        /* No K table ([image+0x6cf1538], also the economy settings pointer an earlier synthetic test may have left set,
         * whose +0x300 then depends on the memory layout): k=0 below. */
        memset(image+0x6cf1538u,0,8);
        tfr_vstub=page+FR_V_STUB;fr_native_params=tfr_params_v;fr_native_v=tfr_v;fr_native_writer=tfr_mulps;stats.issued=2;
        static unsigned char slot2[0x1a0];
        memset(slot2,0,sizeof(slot2));tfr_win_form=0;fr_params_hook(tfr_ship,slot2,0,0.0,0,0);
        memset(slot2,0,sizeof(slot2));tfr_win_form=1;fr_params_hook(tfr_ship,slot2,0,0.0,0,0);
        assert(fr_status(out,sizeof(out))>0 && strstr(out," p0=1:1:2 p4=1:1:2 p8=0:0:2 pc=0:0:2 ") && strstr(out," v=2 mulps=2 "));
        assert(fr_row(9,out,sizeof(out))>0 && strstr(out,"kind=params tick=2 key=000000000007dcd0 ordinal=0 thread=ui flag=0 v_path=1 mulps=1 d=40400000 k=00000000 "
            "v=bf800000,40000000,c2c80000,00000000,00000000,00000000,00000000,00000000 cur=00000000,00000000,00000000,00000000 "
            "ours=42055555,42055555,3eaaaaab,3f2aaaab pre0=") && strstr(out,"post0=5555054255550542abaaaa3eabaa2a3f"));
        assert(fr_row(10,out,sizeof(out))>0 && strstr(out,"ours=42055555,42055555,3eaaaaab,3f2aaaab pre0=") && strstr(out,"post0=5655054256550542abaaaa3eabaa2a3f"));
        fr_native_params=tfr_params;stats.issued=4;VirtualFree(page,0,MEM_RELEASE);
    }
    /* The writer at 0x6dcaaf inside a first call, through its page stub (a jmp to fr_writer_run): writer vector 0's
     * inputs read back through the fake ship's vtable and the table pointer; a stand-in writer in Linux's order,
     * then in Windows' (all four one ulp apart). */
    {
        unsigned char *page=fr_alloc_page(),*saved=fr_page;DWORD o2,i2;assert(page);
        assert(VirtualProtect(page,0x1000,PAGE_READWRITE,&o2));fr_page=page;fr_write_code(page);fr_page=saved;
        assert(VirtualProtect(page,0x1000,PAGE_EXECUTE_READ,&i2));FlushInstructionCache(GetCurrentProcess(),page,0x1000);
        tfw_load(&tfr_w_vectors[0].in);tfr_wstub=page+FR_WRITER_STUB;fr_native_params=tfr_params_w;fr_native_writer=tfr_writer;stats.issued=2;
        static unsigned char slot3[0x1a0];
        memset(slot3,0,sizeof(slot3));tfr_win_form=0;fr_params_hook(tfr_ship,slot3,0,0.0,0,0);
        memset(slot3,0,sizeof(slot3));tfr_win_form=1;fr_params_hook(tfr_ship,slot3,0,0.0,0,0);
        assert(fr_status(out,sizeof(out))>0 && strstr(out," w0=1:1:2 w4=1:1:2 w8=1:1:2 wc=1:1:2 ") && strstr(out," mulps=2 writer=4 w_unread=0 "));
        assert(fr_row(11,out,sizeof(out))>0 && strstr(out,"kind=params tick=2 key=000000000007dcd0 ordinal=2") && strstr(out," writer=1:1 wa=42c80000,42480000,41a00000,41200000 "
            "wd=3f800000,3f800000,3f800000,3f800000,3f800000 wm=3f800000,3f800000,3f800000,3f800000 wt=3f800000,00000000,3f800000 wp=0:3f800000 "
            "wr=3f800000,3f800000,3f800000,3f800000,00000000 wnative=42055555,41855555,40d55555,40555555 wours=42055555,41855555,40d55555,40555555"));
        assert(fr_row(12,out,sizeof(out))>0 && strstr(out," wnative=42055556,41855556,40d55556,40555556 wours=42055555,41855555,40d55555,40555555"));
        /* 'redirect': Windows' bits are replaced by ours before the first call goes on (post0 = Linux's); Linux's
         * order writes nothing; inputs that cannot be read are a failure (the table pointer gone). */
        fr_mode=FR_REDIRECT;
        memset(slot3,0,sizeof(slot3));tfr_win_form=1;fr_params_hook(tfr_ship,slot3,0,0.0,0,0);
        assert(!memcmp(slot3,tfr_w_vectors[0].linux_form,16));
        memset(slot3,0,sizeof(slot3));tfr_win_form=0;fr_params_hook(tfr_ship,slot3,0,0.0,0,0);
        assert(!memcmp(slot3,tfr_w_vectors[0].linux_form,16) && fr_status(out,sizeof(out))>0 && strstr(out,"mode=redirect ") && strstr(out," w_unread=0 w_written=1 "));
        assert(fr_row(13,out,sizeof(out))>0 && strstr(out,"post0=55550542555585415555d54055555540") && strstr(out," wnative=42055556,41855556,40d55556,40555556 "));
        memset(image+0x6cf1538u,0,8);memset(slot3,0,sizeof(slot3));tfr_win_form=1;fr_params_hook(tfr_ship,slot3,0,0.0,0,0);
        assert(fr_w_unread==1 && fr_failures==1 && !fr_check() && fr_w_written==1 && !memcmp(slot3,tfr_w_vectors[0].windows_form,16));
        fr_failures=0;fr_mode=FR_SHADOW;fr_stage="idle";
        memset(image+0x6cf1538u,0,8);fr_native_params=tfr_params;stats.issued=4;VirtualFree(page,0,MEM_RELEASE);
    }
    test_fr_len_stubs();
    test_fr_h_stubs();
    test_fr_pg_stubs();
    test_fr_aff_stubs();
    test_fr_vel_stubs();
    test_fr_fac_stubs();
    test_fr_bl_hook();
    test_fr_size_hook();
    /* A G4 without its tail: the next one restarts. */
    (void)fr_g4_slot_hook(tfr_comp,tfr_ship,frame+0x80,slot);(void)fr_g4_slot_hook(tfr_comp,tfr_ship,frame+0x80,slot);assert(fr_unfinished==1);
    fr_tail_hook(slot);
    /* Site CAS, ownership and restore on the plain image; the two code stubs run. */
    unsigned char *saved_page=fr_page;fr_page=fr_alloc_page();assert(fr_page);DWORD old,ignored;
    for(unsigned i=0;i<FR_SITES;i++) {image[fr_sites[i].site]=0xe8;int32_t d=fr_rel_native(i);memcpy(image+fr_sites[i].site+1,&d,4);assert(fr_site_state(i)==0);}
    memcpy(image+FR_TAIL,fr_tail_original,5);image[FR_TAIL+5]=0;assert(fr_tail_state()==0);
    {unsigned char back[14]={0xff,0x25,0,0,0,0};void *to=(void*)test_fr_tail_back;memcpy(back+6,&to,8);memcpy(image+FR_TAIL_BACK,back,14);}
    assert(VirtualProtect(fr_page,0x1000,PAGE_READWRITE,&old));fr_write_code(fr_page);
    assert(VirtualProtect(fr_page,0x1000,PAGE_EXECUTE_READ,&ignored));
    assert(VirtualProtect(image+(FR_TAIL_BACK&~0xfffu),0x2000,PAGE_EXECUTE_READWRITE,&old));
    FlushInstructionCache(GetCurrentProcess(),image+(FR_TAIL_BACK&~0xfffu),0x2000);
    {   /* G4's stub: the slot arrives from r15 and a refresh begins. */
        const LONG64 unfinished=fr_unfinished;
        assert(test_fr_g4_run(fr_page,tfr_comp,tfr_ship,frame+0x80,slot)==fr_f32(0x43c9859bu));
        fr_ctx *c=fr_context(0);assert(c && c->depth==1 && c->slot==slot && c->seen==FR_SEEN_G4 && fr_unfinished==unfinished);
        (void)fr_g0_hook(tfr_comp,tfr_ship,frame+0x80);
        /* The tail's stub: fr_tail_hook(r15) ends it (compared), r13d = 0x300, back at 0xea29c2. */
        const LONG64 compared=fr_compared,calls=fr_calls;
        assert(test_fr_tail_run(fr_page+FR_TAIL_STUB,slot)==0x300u && fr_compared==compared+1 && fr_calls==calls+1 && !c->depth);
    }
    fr_done=0;
    for(unsigned i=0;i<FR_SITES;i++) {assert(fr_site_cas(i,fr_rel_native(i),fr_rel_ours(i)) && fr_site_state(i)==1);fr_done|=1u<<i;}
    {unsigned char jump[5];fr_tail_jump(jump);assert(view_cas_bytes(FR_TAIL,fr_tail_original,jump,5) && fr_tail_state()==1);fr_done|=FR_TAIL_DONE;}
    /* The length sites: CAS in, the view shows the original mulss to pins (read_local, hash_ok), state per site. */
    for(unsigned i=0;i<FR_LEN_SITES;i++) {
        memcpy(image+fr_len_sites[i].site,fr_len_original[i],5);assert(fr_len_state(i)==0 && fr_len_reach(i));
        assert(!fp_patch_overlaps(image+fr_len_sites[i].site,5) && fr_len_tail_clear(i));
        unsigned char jump[5];fr_len_jump(i,jump);assert(view_cas_bytes(fr_len_sites[i].site,fr_len_original[i],jump,5) && fr_len_state(i)==1);
        fr_done|=FR_LEN_DONE<<i;
        unsigned char seen[16];assert(fp_patch_overlaps(image+fr_len_sites[i].site-3,16) && read_local(image+fr_len_sites[i].site-3,seen,16));
        assert(!memcmp(seen+3,fr_len_original[i],5) && image[fr_len_sites[i].site]==0xe9);
    }
    /* FR16: the PointGuard angle site likewise; clear covers site..back, which the jmp makes differ from the view. */
    memcpy(image+FR_PG_SITE,fr_pg_original,5);assert(fr_pg_state()==0 && fr_pg_reach() && fr_pg_clear());
    {   unsigned char jump[5];fr_pg_jump(jump);assert(view_cas_bytes(FR_PG_SITE,fr_pg_original,jump,5) && fr_pg_state()==1);
        fr_done|=FR_PG_DONE;
        unsigned char seen[16];assert(fp_patch_overlaps(image+FR_PG_SITE-3,16) && read_local(image+FR_PG_SITE-3,seen,16));
        assert(!memcmp(seen+3,fr_pg_original,5) && image[FR_PG_SITE]==0xe9 && !fr_pg_clear());
    }
    /* FR22: the affine weight site likewise. */
    memcpy(image+FR_AFF_SITE,fr_aff_original,5);assert(fr_aff_state()==0 && fr_aff_reach() && fr_aff_clear());
    {   unsigned char jump[5];fr_aff_jump(jump);assert(view_cas_bytes(FR_AFF_SITE,fr_aff_original,jump,5) && fr_aff_state()==1);
        fr_done|=FR_AFF_DONE;
        unsigned char seen[16];assert(fp_patch_overlaps(image+FR_AFF_SITE-3,16) && read_local(image+FR_AFF_SITE-3,seen,16));
        assert(!memcmp(seen+3,fr_aff_original,5) && image[FR_AFF_SITE]==0xe9 && !fr_aff_clear());
    }
    /* FR23: the velocity site likewise (its block's bytes in place, so clear and the block pin can be seen). */
    memcpy(image+FR_VEL_SITE,fr_vel_block,sizeof(fr_vel_block));assert(fr_vel_state()==0 && fr_vel_reach() && fr_vel_clear());
    {   unsigned char jump[5];fr_vel_jump(jump);assert(view_cas_bytes(FR_VEL_SITE,fr_vel_original,jump,5) && fr_vel_state()==1);
        fr_done|=FR_VEL_DONE;
        unsigned char seen[16];assert(fp_patch_overlaps(image+FR_VEL_SITE-3,16) && read_local(image+FR_VEL_SITE-3,seen,16));
        assert(!memcmp(seen+3,fr_vel_original,5) && image[FR_VEL_SITE]==0xe9 && !fr_vel_clear());
        unsigned char viewed[sizeof(fr_vel_block)];assert(read_local(image+FR_VEL_SITE,viewed,sizeof(viewed)) && !memcmp(viewed,fr_vel_block,sizeof(viewed)));
    }
    /* FR24: the facing factor site likewise. */
    memcpy(image+FR_FAC_SITE,fr_fac_block,sizeof(fr_fac_block));assert(fr_fac_state()==0 && fr_fac_reach() && fr_fac_clear());
    {   unsigned char jump[5];fr_fac_jump(jump);assert(view_cas_bytes(FR_FAC_SITE,fr_fac_original,jump,5) && fr_fac_state()==1);
        fr_done|=FR_FAC_DONE;
        unsigned char seen[16];assert(fp_patch_overlaps(image+FR_FAC_SITE-3,16) && read_local(image+FR_FAC_SITE-3,seen,16));
        assert(!memcmp(seen+3,fr_fac_original,5) && image[FR_FAC_SITE]==0xe9 && !fr_fac_clear());
        unsigned char viewed[sizeof(fr_fac_block)];assert(read_local(image+FR_FAC_SITE,viewed,sizeof(viewed)) && !memcmp(viewed,fr_fac_block,sizeof(viewed)));
    }
    /* FR26: the size radius call likewise (a call: ours is e8 to the stub); the stub runs for real as the call would. */
    memcpy(image+FR_SZ_SITE,fr_sz_original,5);assert(fr_sz_state()==0 && fr_sz_reach() && fr_sz_clear());
    {   unsigned char call[5];fr_sz_call(call);assert(view_cas_bytes(FR_SZ_SITE,fr_sz_original,call,5) && fr_sz_state()==1);
        fr_done|=FR_SZ_DONE;
        unsigned char seen[16];assert(fp_patch_overlaps(image+FR_SZ_SITE-3,16) && read_local(image+FR_SZ_SITE-3,seen,16));
        assert(!memcmp(seen+3,fr_sz_original,5) && image[FR_SZ_SITE]==0xe8 && memcmp(image+FR_SZ_SITE,fr_sz_original,5) && !fr_sz_clear());
        int32_t d;memcpy(&d,image+FR_SZ_SITE+1,4);assert(image+FR_SZ_SITE+5+d==fr_page+FR_SZ_STUB);
        tsz_modes((float(*)(const unsigned char *))(void*)(fr_page+FR_SZ_STUB));
    }
    /* FR22: the BezierSpline vtable slot: native in it, swapped to the hook, owned, a foreign value refused. */
    {   void *fn=image+FR_BL_FN;memcpy(image+FR_BL_SLOT,&fn,8);
        assert(fr_bl_state()==0 && !fr_bl_swap((void*)fr_bl_hook,fn) && fr_bl_state()==0);
        assert(fr_bl_swap(fn,(void*)fr_bl_hook) && fr_bl_state()==1);fr_done|=FR_BL_DONE;
    }
    fr_installed=1;fr_failures=0;assert(fr_owned() && fr_check());
    image[FR_AFF_SITE+1]^=1;assert(fr_aff_state()<0 && !fr_owned());image[FR_AFF_SITE+1]^=1;assert(fr_owned());
    image[FR_VEL_SITE+2]^=1;assert(fr_vel_state()<0 && !fr_owned());image[FR_VEL_SITE+2]^=1;assert(fr_owned());
    image[FR_FAC_SITE+3]^=1;assert(fr_fac_state()<0 && !fr_owned());image[FR_FAC_SITE+3]^=1;assert(fr_owned());
    image[FR_SZ_SITE+2]^=1;assert(fr_sz_state()<0 && !fr_owned());image[FR_SZ_SITE+2]^=1;assert(fr_owned());
    {   void *other=(void*)tbl_native;assert(fr_bl_swap((void*)fr_bl_hook,other) && fr_bl_state()<0 && !fr_owned());
        assert(fr_bl_swap(other,(void*)fr_bl_hook) && fr_owned());}
    image[FR_PG_SITE+1]^=1;assert(fr_pg_state()<0 && !fr_owned());image[FR_PG_SITE+1]^=1;assert(fr_owned());
    image[fr_len_sites[1].site+2]^=1;assert(fr_len_state(1)<0 && !fr_owned());image[fr_len_sites[1].site+2]^=1;assert(fr_owned());
    image[fr_sites[1].site+2]^=1;assert(fr_site_state(1)<0 && !fr_owned());image[fr_sites[1].site+2]^=1;assert(fr_owned());
    image[FR_TAIL+3]^=1;assert(fr_tail_state()<0 && !fr_owned());image[FR_TAIL+3]^=1;assert(fr_owned());
    image[fr_sites[2].site+3]^=1;assert(!fr_check() && !strcmp(fr_stage,"site_changed") && fr_failures==1);image[fr_sites[2].site+3]^=1;
    assert(!fr_check()); /* a failure stays */
    fr_restore();assert(!fr_installed && fr_restored && !fr_done && fr_mode==FR_OFF && fr_tail_state()==0);
    for(unsigned i=0;i<FR_SITES;i++) {assert(fr_site_state(i)==0);memset(image+fr_sites[i].site,0,5);}
    for(unsigned i=0;i<FR_LEN_SITES;i++) {assert(fr_len_state(i)==0 && !fp_patch_overlaps(image+fr_len_sites[i].site,5));memset(image+fr_len_sites[i].site,0,5);}
    assert(fr_pg_state()==0 && !fp_patch_overlaps(image+FR_PG_SITE,5));memset(image+FR_PG_SITE,0,5);
    assert(fr_aff_state()==0 && !fp_patch_overlaps(image+FR_AFF_SITE,5));memset(image+FR_AFF_SITE,0,5);
    assert(fr_vel_state()==0 && !fp_patch_overlaps(image+FR_VEL_SITE,5) && fr_vel_clear());memset(image+FR_VEL_SITE,0,sizeof(fr_vel_block));
    assert(fr_fac_state()==0 && !fp_patch_overlaps(image+FR_FAC_SITE,5) && fr_fac_clear());memset(image+FR_FAC_SITE,0,sizeof(fr_fac_block));
    assert(fr_sz_state()==0 && !fp_patch_overlaps(image+FR_SZ_SITE,5) && fr_sz_clear());memset(image+FR_SZ_SITE,0,5);
    assert(fr_bl_state()==0);memset(image+FR_BL_SLOT,0,8);
    memset(image+FR_TAIL,0,20);
    VirtualFree(fr_page,0,MEM_RELEASE);fr_page=saved_page;
    fr_native_refresh=NULL;fr_native_g4=fr_native_g0=NULL;fr_native_m=NULL;fr_native_params=NULL;fr_native_writer=NULL;fr_id_count=0;fr_restored=0;fr_failures=0;fr_stage="idle";
    fr_records=0;fr_scan=fr_emitted=fr_scanned=fr_keyed=0;
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=prior;world_identity_count=prior_count;world_identity_sealed=prior_sealed;
    world_identity_installed=saved_ids;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    printf("fp redirect shadow flow/load rows/status/arm/stubs/site tests PASS\n");
}
/* The executable's +0x0 block, entered at 0xea22cf with xmm0 = G0, r14 = comp, rbp = frame, r15 = slot, xmm8 = the
 * abs mask (set by the prologue at 0xea1edc); 0xea239b (after the store to [r15]) jumps back here. */
__asm__(
".text\n"
"test_fr_win_block:\n push %rbp\n push %r14\n push %r15\n push %rbx\n sub $0x28,%rsp\n movdqu %xmm7,(%rsp)\n movdqu %xmm8,0x10(%rsp)\n"
" mov %rdx,%r14\n mov %r8,%rbp\n mov %r9,%r15\n mov $0x7fffffff,%eax\n movd %eax,%xmm8\n movss (%r15),%xmm0\n jmp *%rcx\n"
"test_fr_win_back:\n movss (%r15),%xmm0\n movdqu (%rsp),%xmm7\n movdqu 0x10(%rsp),%xmm8\n add $0x28,%rsp\n pop %rbx\n pop %r15\n pop %r14\n pop %rbp\n ret\n"
);
extern float test_fr_win_block(const void *entry,void *comp,void *frame,void *slot);
extern char test_fr_win_back[];
/* An inlined Set block of 0x857960 / 0x31c640, entered at its rax load (0x857e45 / 0x31cad5) with rbp = frame
 * ([rbp+0x10] = the segment array), r10 = the record's offset, the absolute control points P0..P3 in xmm10, xmm3 and
 * xmm2 (P1: 0x857960 reads xmm3, 0x31c640 xmm2), xmm5, xmm9, xmm11 = 3.0 (the coefficients'), xmm13 = 0.5,
 * xmm14 = 0; it subtracts the record's centre (+0x00), stores P0..P3 at +0x60..+0x90 and the length at +0x18; the jump
 * planted at the tail's end (0x857f8c / 0x31cc1c) returns through test_fr_len_block_back. */
__asm__(
".text\n"
"test_fr_len_block:\n push %rbp\n push %rbx\n push %rsi\n push %rdi\n sub $0x68,%rsp\n movdqu %xmm9,(%rsp)\n movdqu %xmm10,0x10(%rsp)\n"
" movdqu %xmm11,0x20(%rsp)\n movdqu %xmm13,0x30(%rsp)\n movdqu %xmm14,0x40(%rsp)\n mov %rsp,%rbx\n mov %rdx,%rbp\n mov %r8,%r10\n"
" movups (%r9),%xmm10\n movups 0x10(%r9),%xmm3\n movups 0x10(%r9),%xmm2\n movups 0x20(%r9),%xmm5\n movups 0x30(%r9),%xmm9\n"
" mov $0x40400000,%eax\n movd %eax,%xmm11\n shufps $0,%xmm11,%xmm11\n mov $0x3f000000,%eax\n movd %eax,%xmm13\n xorps %xmm14,%xmm14\n jmp *%rcx\n"
"test_fr_len_block_back:\n mov %rbx,%rsp\n movdqu (%rsp),%xmm9\n movdqu 0x10(%rsp),%xmm10\n movdqu 0x20(%rsp),%xmm11\n movdqu 0x30(%rsp),%xmm13\n"
" movdqu 0x40(%rsp),%xmm14\n add $0x68,%rsp\n pop %rdi\n pop %rsi\n pop %rbx\n pop %rbp\n ret\n"
);
extern void test_fr_len_block(const void *entry,void *frame,uint64_t r10,const float *points);
extern char test_fr_len_block_back[];
/* FR13: a handle site's exact-reciprocal window (rsqrtss xmm2,xmm2; rcpss xmm2,xmm2 at site-8), the site and the
 * store after it, on the executable's own bytes: entered at the window with xmm2 = x (the squared chord), xmm8 = k,
 * r9 = rcx = buf, rbx = 0; the out-handle stores [r9+rbx*8+4], the in-handle [rcx+rbx*8+8]; a jump planted after the
 * store returns through test_fr_h_block_back. */
__asm__(
".text\n"
"test_fr_h_block:\n push %rbp\n push %rbx\n push %rsi\n push %rdi\n sub $0x28,%rsp\n movdqu %xmm8,(%rsp)\n mov %rsp,%rsi\n"
" mov %rcx,%rax\n mov %r9,%rcx\n movd %edx,%xmm2\n movd %r8d,%xmm8\n xor %ebx,%ebx\n jmp *%rax\n"
"test_fr_h_block_back:\n mov %rsi,%rsp\n movdqu (%rsp),%xmm8\n add $0x28,%rsp\n pop %rdi\n pop %rsi\n pop %rbx\n pop %rbp\n ret\n"
);
extern void test_fr_h_block(const void *entry,uint32_t x,uint32_t k,void *buf);
extern char test_fr_h_block_back[];
static const uint32_t tfh_after[FR_H_SITES]={0x857db2u,0x857dfbu,0x31ca42u,0x31ca8bu}; /* the instruction after each store */
/* rounds per site; mode as set; returns the splits (d/3 != d*k) seen and checks each stored value against the mode's rule. */
static unsigned tfh_native_runs(unsigned rounds,int through_stub) {
    uint32_t k;memcpy(&k,image+0x2cbe3a4u,4);assert(k==TFH_K);
    unsigned split=0;
    for(unsigned i=0;i<FR_H_SITES;i++) for(unsigned r=0;r<rounds;r++) {
        const float dist=tfr_range(0.001f,90000.0f),x=dist*dist;uint32_t xb;memcpy(&xb,&x,4);
        uint32_t buf[4]={0xdeadbeefu,0xdeadbeefu,0xdeadbeefu,0xdeadbeefu};
        test_fr_h_block(image+fr_h_sites[i].site-8,xb,k,buf);
        const uint32_t d=fr_bits(sl_sqrt(x)),native=sl_handle_windows(d,k),ours=sl_handle(d);
        const uint32_t got=buf[(i&1)?2:1],want=through_stub && fr_mode==FR_REDIRECT?ours:native;
        if(got!=want) {printf("fp redirect handle %u/%u: x %08x got %08x want %08x (native %08x ours %08x)\n",i,r,xb,got,want,native,ours);assert(0);}
        assert(buf[0]==0xdeadbeefu && buf[3]==0xdeadbeefu && buf[(i&1)?1:2]==0xdeadbeefu);
        split+=native!=ours;
    }
    return split;
}
/* FR16: the PointGuard angle on the executable's own bytes: entered at 0xe9c4bc with xmm1 = rem, xmm2 = m+1 (rsp 16-
 * aligned, as in the function's body); a jump planted at 0xe9c4cc returns through test_fr_pg_block_back with xmm1[0]. */
__asm__(
".text\n"
"test_fr_pg_block:\n push %rbp\n push %rbx\n push %rsi\n push %rdi\n sub $0x28,%rsp\n mov %rsp,%rsi\n"
" mov %rcx,%rax\n movd %edx,%xmm1\n movd %r8d,%xmm2\n jmp *%rax\n"
"test_fr_pg_block_back:\n mov %rsi,%rsp\n movd %xmm1,%eax\n add $0x28,%rsp\n pop %rdi\n pop %rsi\n pop %rbx\n pop %rbp\n ret\n"
);
extern uint32_t test_fr_pg_block(const void *entry,uint32_t rem,uint32_t m1);
extern char test_fr_pg_block_back[];
#define TPG_M1 200u
#define TPG_RUNS (TPG_M1*(TPG_M1+1u)/2u) /* every (m+1, rem) with m+1 = 1..200, rem = 0..m */
/* Mode as set; returns the splits (ours != native) seen and checks each angle against the mode's rule. */
static unsigned tpg_native_runs(int through_stub) {
    uint32_t pi;memcpy(&pi,image+FR_PG_PI,4);assert(pi==FR_PG_PI_BITS);
    unsigned split=0;
    for(unsigned m1=1;m1<=TPG_M1;m1++) for(unsigned rem=0;rem<m1;rem++) {
        const float fr=(float)rem,fm=(float)m1;uint32_t rb,mb;memcpy(&rb,&fr,4);memcpy(&mb,&fm,4);
        const uint32_t got=test_fr_pg_block(image+FR_PG_SITE,rb,mb),native=fr_pg_angle_windows(rb,mb),ours=fr_pg_angle(rb,mb);
        const uint32_t want=through_stub && fr_mode==FR_REDIRECT?ours:native;
        if(got!=want) {printf("fp redirect pg m+1 %u rem %u: got %08x want %08x (native %08x ours %08x)\n",m1,rem,got,want,native,ours);assert(0);}
        split+=native!=ours;
    }
    return split;
}
/* Both inlined blocks on their own bytes (exact-reciprocal windows installed, as in the game): native, then through
 * the length stubs in 'shadow' and 'redirect'. Returns the splits (canonical != pairwise) seen. */
static unsigned tfl_native_runs(unsigned rounds,unsigned *zeros) {
    static unsigned char segs[0x200];static uint64_t frame[8];unsigned char *const seg=segs+0x40;
    frame[2]=(uint64_t)(uintptr_t)segs;
    unsigned split=0;
    for(unsigned i=0;i<FR_LEN_SITES;i++) for(unsigned k=0;k<rounds;k++) {
        float pts[16],centre[4];
        for(unsigned c=0;c<4;c++) centre[c]=tfr_range(-50000.0f,50000.0f);
        for(unsigned c=0;c<16;c++) pts[c]=centre[c&3]+tfr_range(-4000.0f,4000.0f);
        if(k%97==0) for(unsigned c=4;c<16;c++) pts[c]=pts[c&3];             /* one point four times: length 0 */
        memset(seg,0x5a,0xa0);memcpy(seg,centre,16);
        test_fr_len_block(image+fr_len_sites[i].pin,frame,0x40,pts);
        uint32_t stored;memcpy(&stored,seg+0x18,4);
        const uint32_t ours=sl_length(seg+0x60),pairwise=sl_length_pairwise(seg+0x60);
        if(fr_mode==FR_REDIRECT) assert(stored==sl_stored_linux(ours));
        else if(stored!=sl_stored_windows(pairwise)) {printf("fp redirect length %u/%u: stored %08x, pairwise %08x\n",i,k,stored,pairwise);assert(0);}
        split+=ours!=pairwise;*zeros+=stored==SL_ONE;
    }
    return split;
}
/* FR22: BezierSpline::ComputeLength 0x14e4f20 with its chord 0x14e4a20 on their own bytes (the segment's vptr the
 * BezierSpline vtable of this copy, its +0x10 and +0x28 relocated): native = Windows' u = i*(1/n) on every input;
 * through the slot, 'shadow' keeps it and 'redirect' stores Linux's u = i/n. Returns the splits seen. */
static unsigned tbl_native_runs(unsigned rounds,int through_slot) {
    static const uint32_t ns[]={100,10,7,3,1,64,0,50,1000,33};
    static __declspec(align(16)) unsigned char seg[0x40];
    const void *vt=image+FR_BL_VTABLE;unsigned split=0;
    for(unsigned r=0;r<rounds;r++) for(unsigned j=0;j<sizeof(ns)/sizeof(ns[0]);j++) {
        const uint32_t n=ns[j];if(n==1000 && r%16) continue;
        float c[12];const float scale=r%3==0?100000.0f:r%3==1?2000.0f:5.0f;
        for(unsigned k=0;k<12;k++) c[k]=(k&3)==3?tfr_range(-1e30f,1e30f):tfr_range(-scale,scale);
        if(r%5==0) for(unsigned k=1;k<12;k+=4) c[k]=0.0f; /* flat, as the highways */
        memset(seg,0x5a,sizeof(seg));memcpy(seg,&vt,8);memcpy(seg+0x10,c,48);
        const fr_bl_fn fn=through_slot?*(const fr_bl_fn*)(image+FR_BL_SLOT):(fr_bl_fn)(void*)(image+FR_BL_FN);
        fn(seg,n);
        uint32_t got;memcpy(&got,seg+8,4);
        const uint32_t native=sl_arc_length_windows(seg+0x10,n),ours=sl_arc_length(seg+0x10,n);
        const uint32_t want=through_slot && fr_mode==FR_REDIRECT?ours:native;
        if(got!=want) {printf("fp redirect arc length %u n %u: got %08x want %08x (native %08x ours %08x)\n",r,n,got,want,native,ours);assert(0);}
        assert(!memcmp(seg+0x10,c,48) && seg[0xc]==0x5a && seg[0xf]==0x5a);
        split+=native!=ours;
    }
    return split;
}
/* FR22: the AffineSpline constructor 0x65a580 on its own bytes over a composite's knot pairs (the knot vector's capacity
 * enough, so no allocator call): native = Windows' weight (2/sum)*dp, through the stub 'shadow' the same, 'redirect'
 * Linux's dp/(sum*0.5); total at +0x18, knots normalised, the composite's length copied to +8. */
static void tfa_expect(const float *pairs,unsigned n,int linux_form,float *knots,float *total) {
    float t=0.0f;knots[0]=0.0f;
    for(unsigned i=1;i<n;i++) {
        const float sum=sl_sqrt(pairs[2*(i-1)+1])+sl_sqrt(pairs[2*i+1]),dp=pairs[2*i]-pairs[2*(i-1)];
        const float q=fr_f32(linux_form?sl_affine_weight(fr_bits(sum),fr_bits(dp)):sl_affine_weight_windows(fr_bits(sum),fr_bits(dp)));
        t=q+t;knots[i]=q+knots[i-1];
    }
    if(t>0.0f) for(unsigned i=1;i<n;i++) knots[i]=knots[i]/t;
    *total=t;
}
static unsigned tfa_native_runs(unsigned rounds,int through_stub) {
    static __declspec(align(16)) unsigned char aff[0x40],comp[0x40];static float pairs[16],knots[8];
    static const float speeds[]={0.2f,1.0f,0.5f,0.8f,0.35f,1.0f,0.2f,0.65f};
    unsigned split=0;
    for(unsigned r=0;r<rounds;r++) {
        const unsigned n=2+r%7;float p=0.0f;
        for(unsigned i=0;i<n;i++) {
            if(i) p+=tfr_range(0.001f,0.4f);
            pairs[2*i]=i+1==n && r%2?1.0f:p;pairs[2*i+1]=r%4==3?tfr_range(0.05f,2.0f):speeds[(i+r)%8];
        }
        if(r%11==0) for(unsigned i=0;i<n;i++) pairs[2*i]=0.0f; /* no length: the knots stay unnormalised */
        const float length=tfr_range(10.0f,300000.0f);
        memset(comp,0,sizeof(comp));memcpy(comp+8,&length,4);
        uint64_t v[2]={(uint64_t)(uintptr_t)pairs,(uint64_t)(uintptr_t)(pairs+2*n)};memcpy(comp+0x28,v,16);
        memset(aff,0x5a,sizeof(aff));void *q=comp;memcpy(aff+0x10,&q,8);
        uint64_t kv[3]={(uint64_t)(uintptr_t)knots,(uint64_t)(uintptr_t)(knots+5),(uint64_t)(uintptr_t)(knots+8)};memcpy(aff+0x20,kv,24);
        memset(knots,0x77,sizeof(knots));
        ((void (*)(void *))(void*)(image+FR_AFF_PIN))(aff);
        float native[8],ours[8],tn,to;tfa_expect(pairs,n,0,native,&tn);tfa_expect(pairs,n,1,ours,&to);
        const int linux_form=through_stub && fr_mode==FR_REDIRECT;
        const float *want=linux_form?ours:native;const float want_total=linux_form?to:tn;
        uint64_t end;memcpy(&end,aff+0x28,8);float total,len;memcpy(&total,aff+0x18,4);memcpy(&len,aff+8,4);
        if(end!=(uint64_t)(uintptr_t)(knots+n) || memcmp(knots,want,4*n) || fr_bits(total)!=fr_bits(want_total) || fr_bits(len)!=fr_bits(length)) {
            printf("fp redirect affine %u (n %u): total %08x want %08x\n",r,n,fr_bits(total),fr_bits(want_total));
            for(unsigned i=0;i<n;i++) printf("  knot %u %08x want %08x (native %08x ours %08x)\n",i,fr_bits(knots[i]),fr_bits(want[i]),fr_bits(native[i]),fr_bits(ours[i]));
            assert(0);
        }
        split+=memcmp(native,ours,4*n)!=0 || fr_bits(tn)!=fr_bits(to);
    }
    return split;
}
/* FR23: the step's velocity block on its own bytes, entered at 0xeb32e6 (movaps xmm2,xmm12; ...; the flag test) with
 * rdi a model whose +0x95 is set or clear, rsi a bank, xmm11 = s, xmm12 = 0, up to 0xeb3322 (a back jump planted there).
 * Pass 0 natively keeps every case's registers and bank; passes through the stub must leave the same registers and +0x50,
 * and the velocity native's ('shadow') or ours ('redirect') where the flag is set, the plain (0,0,s,0) where it is not. */
#define TFV_CASES 1500u
static unsigned char tfv_native_blocks[TFV_CASES][TFV_BLOCK];
static uint32_t tfv_native_vel[TFV_CASES][8];
static void tfv_case(unsigned i,uint32_t rows[12],uint32_t *s,unsigned *flag) {
    for(unsigned k=0;k<12;k++) rows[k]=(k&3)==3?0:fr_bits(tfr_range(-1.0f,1.0f));
    if(i%9==0) rows[3]=fr_bits(tfr_range(-1.0f,1.0f)); /* a w lane now and then */
    const unsigned pick=i%5;
    *s=pick==0?0u:pick==1?0x80000000u:pick==2?fr_bits(tfr_range(0.0f,600.0f)):pick==3?fr_bits(tfr_range(-1e-30f,1e-30f)):0u;
    *flag=i%7!=3;
}
static unsigned tfv_native_runs(int pass) {
    unsigned split=0;const uint64_t keep=tfr_state;tfr_state=0x51ed270b3c2f4a1dull; /* every pass the same cases */
    for(unsigned i=0;i<TFV_CASES;i++) {
        uint32_t rows[12],s,vel[8],ours[4],win[4];unsigned flag;unsigned char block[TFV_BLOCK];
        tfv_case(i,rows,&s,&flag);tfv_run(image+0xeb32e6u,rows,s,flag,block,vel);
        fr_velocity(rows,s,ours);fr_velocity_windows(rows,s,win);
        const uint32_t plain[4]={0,0,s,0};
        if(pass==0) {
            memcpy(tfv_native_blocks[i],block,TFV_BLOCK);memcpy(tfv_native_vel[i],vel,32);
            if(memcmp(vel,flag?win:plain,16)) {printf("fp redirect velocity native %u: %08x %08x %08x %08x\n",i,vel[0],vel[1],vel[2],vel[3]);assert(0);}
        } else {
            assert(!memcmp(block,tfv_native_blocks[i],0xb8) && !memcmp(vel+4,tfv_native_vel[i]+4,16));
            const uint32_t *want=!flag?plain:fr_mode==FR_REDIRECT?ours:win;
            if(memcmp(vel,want,16)) {printf("fp redirect velocity %u (pass %d): %08x %08x %08x %08x\n",i,pass,vel[0],vel[1],vel[2],vel[3]);assert(0);}
        }
        if(flag && memcmp(ours,win,16)) {assert((s&0x7fffffffu)==0);++split;}
    }
    tfr_state=keep;return split;
}
/* FR24: the step's facing factor block on its own bytes, entered at the site 0xeb3a91 with xmm1 = m, xmm13 = 1.0 and
 * rdi a model holding m at +0x98, up to 0xeb3aa1 (a back jump planted there). Pass 0 natively: xmm0 = 1 - m*k, xmm1 =
 * m*k; passes through the stub: every register the same, xmm0's lane 0 native's ('shadow') or ours ('redirect'). */
#define TFC_CASES 4000u
static unsigned char tfc_native_blocks[TFC_CASES][TFC_BLOCK];
static unsigned tfc_native_runs(int pass) {
    unsigned split=0;const uint64_t keep=tfr_state;tfr_state=0x2f1c4a8b77e01d35ull; /* every pass the same cases */
    for(unsigned i=0;i<TFC_CASES;i++) {
        const uint32_t m=i%50==0?0u:i%50==1?0x3fc90fdbu:i%50==2?fr_bits(tfr_range(0.0f,1e-30f)):fr_bits(tfr_range(0.0f,1.57079637f));
        unsigned char block[TFC_BLOCK];tfc_run(image+FR_FAC_SITE,m,block);
        const uint32_t win=fr_facing_windows(m),ours=fr_facing(m);uint32_t got,x1;memcpy(&got,block,4);memcpy(&x1,block+0x10,4);
        if(pass==0) {
            memcpy(tfc_native_blocks[i],block,TFC_BLOCK);
            if(got!=win || x1!=fr_bits(fr_f32(m)*0.636619747f)) {printf("fp redirect facing native %u: m %08x got %08x x1 %08x\n",i,m,got,x1);assert(0);}
        } else {
            assert(!memcmp(block+4,tfc_native_blocks[i]+4,0xbc));
            const uint32_t want=fr_mode==FR_REDIRECT?ours:win;
            if(got!=want) {printf("fp redirect facing %u (pass %d): m %08x got %08x want %08x\n",i,pass,m,got,want);assert(0);}
        }
        split+=ours!=win;
    }
    tfr_state=keep;return split;
}
static void test_fp_redirect_native_image(const char *path) {
    FILE *file=fopen(path,"rb");assert(file);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    assert(fread(&dos,1,sizeof(dos),file)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0 && dos.e_lfanew<4096);
    assert(!fseek(file,dos.e_lfanew,SEEK_SET) && fread(&nt,1,sizeof(nt),file)==sizeof(nt) && nt.OptionalHeader.SizeOfImage==0x6fa9000);
    unsigned char *prior=image;image=VirtualAlloc(NULL,nt.OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(image);
    for(unsigned i=0;i<nt.FileHeader.NumberOfSections;i++) {
        IMAGE_SECTION_HEADER section;assert(fread(&section,1,sizeof(section),file)==sizeof(section));long next=ftell(file);
        if(section.SizeOfRawData) {
            assert(!fseek(file,section.PointerToRawData,SEEK_SET));
            assert(fread(image+section.VirtualAddress,1,section.SizeOfRawData,file)==section.SizeOfRawData);
        }
        assert(!fseek(file,next,SEEK_SET));
    }
    fclose(file);
    /* FR22: the BezierSpline vtable's +0x10 and +0x28 hold the file's preferred addresses; this copy's, as the loader's. */
    {uint64_t q;memcpy(&q,image+FR_BL_CHORD_SLOT,8);assert(q==nt.OptionalHeader.ImageBase+FR_BL_CHORD);
     memcpy(&q,image+FR_BL_SLOT,8);assert(q==nt.OptionalHeader.ImageBase+FR_BL_FN);
     memcpy(&q,image+0x2a84fc8u,8);assert(q==nt.OptionalHeader.ImageBase+FR_BL_FN); /* the abstract base's */
     void *f=image+FR_BL_CHORD;memcpy(image+FR_BL_CHORD_SLOT,&f,8);f=image+FR_BL_FN;memcpy(image+FR_BL_SLOT,&f,8);}
    unsigned char *saved_page=fr_page;fr_page=NULL;
    assert(fr_pins());
    image[FR_REFRESH+0x400]^=1;assert(!fr_pins());image[FR_REFRESH+0x400]^=1;
    image[FR_M_RVA+0x10]^=1;assert(!fr_pins());image[FR_M_RVA+0x10]^=1;assert(fr_pins());
    image[FR_W_BODY+0x1e3]^=1;assert(!fr_pins());image[FR_W_BODY+0x1e3]^=1;              /* the writer and its call site */
    image[FR_W_SITE_BLOCK+0x10]^=1;assert(!fr_pins());image[FR_W_SITE_BLOCK+0x10]^=1;assert(fr_pins());
    for(unsigned i=0;i<2;i++) {image[fr_h_pins[i].pin+0x20]^=1;assert(!fr_pins() && !fr_h_pins_ok());image[fr_h_pins[i].pin+0x20]^=1;} /* FR13 */
    for(unsigned i=0;i<FR_H_SITES;i++) assert(!memcmp(image+fr_h_sites[i].site,fr_h_original,5) && fr_h_sites[i].site-8>=fr_h_pins[fr_h_sites[i].pin].pin);
    /* FR16: the PointGuard function and pi_f pinned; the site's three instructions and the movaps/call after them. */
    image[FR_PG_PIN+0x20]^=1;assert(!fr_pins() && !fr_pg_pins_ok());image[FR_PG_PIN+0x20]^=1;
    image[FR_PG_PI]^=1;assert(!fr_pins() && !fr_pg_pins_ok());image[FR_PG_PI]^=1;
    assert(!memcmp(image+FR_PG_SITE,"\xf3\x0f\x10\x05\xf0\x25\xe2\x01\xf3\x0f\x5e\xc2\xf3\x0f\x59\xc8\x0f\x28\xc1\xe8",20) && FR_PG_BACK-FR_PG_SITE==16);
    assert(FR_PG_SITE+8+0x1e225f0u==FR_PG_PI && FR_PG_SITE>FR_PG_PIN && FR_PG_BACK<FR_PG_PIN+FR_PG_PIN_SIZE);
    /* FR22: ComputeLength, its chord, their vtable slot and 1.0; the affine constructor and its 2.0; the site's two
     * instructions (divss xmm2,xmm1; mulss xmm2,xmm0) after movaps xmm2,xmm6 and before movaps xmm0,xmm2. */
    image[FR_BL_FN+0x40]^=1;assert(!fr_pins() && !fr_bl_pins_ok());image[FR_BL_FN+0x40]^=1;
    image[FR_BL_CHORD+0x20]^=1;assert(!fr_pins() && !fr_bl_pins_ok());image[FR_BL_CHORD+0x20]^=1;
    image[FR_BL_CHORD_SLOT]^=8;assert(!fr_pins() && !fr_bl_pins_ok());image[FR_BL_CHORD_SLOT]^=8;
    image[FR_BL_ONE+1]^=1;assert(!fr_pins() && !fr_bl_pins_ok());image[FR_BL_ONE+1]^=1;
    image[FR_AFF_PIN+0x30]^=1;assert(!fr_pins() && !fr_aff_pins_ok());image[FR_AFF_PIN+0x30]^=1;
    image[FR_AFF_TWO+3]^=1;assert(!fr_pins() && !fr_aff_pins_ok());image[FR_AFF_TWO+3]^=1;
    assert(!memcmp(image+FR_AFF_SITE-6,"\xf3\x0f\x5c\x44\xf1\xf8\xf3\x0f\x5e\xd1\xf3\x0f\x59\xd0\x0f\x28\xc2",17) && !memcmp(image+0x65a656,"\x0f\x28\xd6",3) && FR_AFF_BACK-FR_AFF_SITE==8);
    assert(!memcmp(image+0x65a63b,"\xf3\x0f\x10\x35",4) && 0x65a643u+0x26642d5u==FR_AFF_TWO && FR_AFF_SITE>FR_AFF_PIN && FR_AFF_BACK<FR_AFF_PIN+FR_AFF_PIN_SIZE);
    assert(!memcmp(image+0x14e4f49,"\xf3\x0f\x10\x35",4) && 0x14e4f51u+0x17d9683u==FR_BL_ONE);
    assert(fr_bl_state()==0 && fr_pins());
    /* FR23: the Linear step (the world trace's pin) and the velocity block; the facts the stub relies on: je 0xeb331a
     * on [rdi+0x95] clear right before the site, xmm12 zeroed at 0xeb2873, the comiss after the back address. */
    image[FR_VEL_PIN+0x40]^=1;assert(!fr_pins() && !fr_vel_pins_ok());image[FR_VEL_PIN+0x40]^=1;
    image[FR_VEL_SITE+0x15]^=1;assert(!fr_pins() && !fr_vel_pins_ok());image[FR_VEL_SITE+0x15]^=1;
    assert(fr_vel_pins_ok() && !memcmp(image+FR_VEL_SITE,fr_vel_block,sizeof(fr_vel_block)) && !memcmp(image+0xeb32f4,"\x80\xbf\x95\x00\x00\x00\x00\x74\x1d",9));
    assert(!memcmp(image+0xeb2873,"\x45\x0f\x57\xe4",4) && !memcmp(image+FR_VEL_BACK,"\x0f\x11\x66\x50\x44\x0f\x2f\x05",8) && 0xeb32fdu+0x1du==0xeb331au);
    /* FR24: the facing factor block and k; the facts the stub relies on: m stored at +0x98 by 0xeb3a6c, the comiss with
     * pi/2 and the jbe to the site (its only branch in), xmm13 = 1.0 (0xeb286a, 1.0 at 0x2cbe5d4), the store and the
     * blend's comiss after the back address, k's address. */
    image[FR_FAC_SITE+9]^=1;assert(!fr_pins() && !fr_fac_pins_ok());image[FR_FAC_SITE+9]^=1;
    image[FR_FAC_K+2]^=1;assert(!fr_pins() && !fr_fac_pins_ok());image[FR_FAC_K+2]^=1;
    assert(fr_fac_pins_ok() && !memcmp(image+0xeb3a6c,"\xf3\x0f\x11\x8f\x98\x00\x00\x00",8) && !memcmp(image+0xeb3a83,"\x76\x0c",2));
    assert(!memcmp(image+0xeb286a,"\xf3\x44\x0f\x10\x2d",5) && 0xeb2873u+0x1e0bd61u==0x2cbe5d4u && 0xeb3a99u+0x1e0a9ebu==FR_FAC_K);
    assert(!memcmp(image+FR_FAC_BACK,"\xf3\x0f\x11\x87\x9c\x00\x00\x00\x0f\x2f\xc2",11) && FR_FAC_SITE>FR_VEL_PIN && FR_FAC_BACK<FR_VEL_PIN+FR_VEL_PIN_SIZE);
    /* FR26: the size block, the getter and 2.0 pinned; the facts the hook relies on: the call's target is the getter,
     * the mulss reads 2.0, 'size' (id 0x4b1) is case 0xd08b64 and jumps from 0xd08ba1 to 0xd0042b, 'coresize' falls
     * through from the call at 0xd00426, 'safesize' joins at 0xd00433 (from 0xd08bc6), and no case of the evaluator's
     * table (0xd0bdf0, 0x56c entries by id - 3) lands in the call's bytes. */
    image[FR_SZ_BLOCK+0x10]^=1;assert(!fr_pins() && !fr_sz_pins_ok());image[FR_SZ_BLOCK+0x10]^=1;
    image[FR_SZ_GETTER+4]^=1;assert(!fr_pins() && !fr_sz_pins_ok());image[FR_SZ_GETTER+4]^=1;
    assert(fr_sz_pins_ok() && fr_pins() && !memcmp(image+FR_SZ_SITE,fr_sz_original,5) && FR_SZ_SITE==FR_SZ_BLOCK+3);
    {   int32_t d;memcpy(&d,fr_sz_original+1,4);assert(FR_SZ_SITE+5+d==FR_SZ_GETTER && 0xd0043bu+0x1fbe4ddu==FR_AFF_TWO);
        uint32_t c;memcpy(&c,image+0xd0bdf0u+4*(0x4b1u-3),4);assert(c==0xd08b64u);
        assert(image[0xd08ba1]==0xe9);memcpy(&d,image+0xd08ba2,4);assert(0xd08ba6u+d==FR_SZ_BLOCK);
        assert(image[0xd08bc6]==0xe9);memcpy(&d,image+0xd08bc7,4);assert(0xd08bcbu+d==FR_SZ_SITE+5);
        assert(image[0xd00426]==0xe8 && 0xd00426u+5==FR_SZ_BLOCK);
        for(unsigned i=0;i<0x56c;i++) {memcpy(&c,image+0xd0bdf0u+4*i,4);assert(c<=FR_SZ_SITE || c>=FR_SZ_SITE+5);}
        /* the getter on its own bytes (this copy is mapped read-write: its page made executable) */
        DWORD old;assert(VirtualProtect(image+FR_SZ_GETTER,6,PAGE_EXECUTE_READWRITE,&old));FlushInstructionCache(GetCurrentProcess(),image+FR_SZ_GETTER,6);
        static __declspec(align(16)) unsigned char box[0x30];memset(box,0,sizeof(box));memcpy(box,tsz_boxes[2].h,12);memcpy(box+0x20,&tsz_boxes[2].cached,4);
        assert(fr_bits(((float(*)(const unsigned char *))(void*)(image+FR_SZ_GETTER))(box))==tsz_boxes[2].cached);
    }
    /* The live boot's order: the exact-reciprocal windows armed first (their view then covers the whole image), then
     * this module through fr_arm itself, in 'redirect': pins (both length blocks hold windows), tails clear, every
     * site installed and owned, restored. FR10/FR11/W5 refused here with stage=pins. */
    {
        assert(xrcp_pins() && xrcp_build() && xrcp_switch(1)==1);xrcp_installed=1;
        const uint32_t sa=stats.active;const uint64_t si=stats.issued;const DWORD su=ui_thread;const int sb=boot_mode;const unsigned sids=fr_id_count;
        stats.active=1;stats.issued=0;ui_thread=GetCurrentThreadId();boot_mode=0;fr_id_count=0;
        for(unsigned i=0;i<FR_LEN_SITES;i++) assert(fp_patch_overlaps(image+fr_len_sites[i].site,5) && fr_len_tail_clear(i));
        for(unsigned i=0;i<FR_H_SITES;i++) assert(fr_h_state(i)==0 && fr_h_clear(i));
        assert(fr_pg_state()==0 && fr_pg_clear());
        assert(fr_aff_state()==0 && fr_aff_clear() && fr_bl_state()==0);
        assert(fr_vel_state()==0 && fr_vel_clear() && fr_fac_state()==0 && fr_fac_clear() && fr_sz_state()==0 && fr_sz_clear());
        assert(fr_pins() && fr_arm(FR_REDIRECT,3) && fr_installed && !strcmp(fr_stage,"armed") && fr_check());
        assert(fr_sz_state()==1 && !fr_sz_clear() && (fr_done&FR_SZ_DONE) && fr_sz_pins_ok()); /* FR26: the view keeps the block's pin */
        {   /* the redirected call as the evaluator makes it: the stub with a box whose cached radius is approximate */
            static __declspec(align(16)) unsigned char box[0x30];memset(box,0,sizeof(box));memcpy(box,tsz_boxes[0].h,12);memcpy(box+0x20,&tsz_boxes[0].cached,4);
            int32_t d;memcpy(&d,image+FR_SZ_SITE+1,4);
            const LONG64 w=fr_sz_written;
            assert(fr_bits(((float(*)(const unsigned char *))(void*)(image+FR_SZ_SITE+5+d))(box))==tsz_boxes[0].radius && fr_sz_written==w+1);
        }
        assert(fr_aff_state()==1 && !fr_aff_clear() && (fr_done&FR_AFF_DONE) && fr_bl_state()==1 && (fr_done&FR_BL_DONE));
        assert(*(void**)(image+FR_BL_SLOT)==(void*)fr_bl_hook && fr_aff_pins_ok() && fr_bl_pins_ok());
        assert(fr_vel_state()==1 && !fr_vel_clear() && (fr_done&FR_VEL_DONE) && fr_vel_pins_ok()); /* the view keeps the step's pin */
        assert(fr_fac_state()==1 && !fr_fac_clear() && (fr_done&FR_FAC_DONE) && fr_fac_pins_ok());
        for(unsigned i=0;i<FR_LEN_SITES;i++) assert(fr_len_state(i)==1 && !fr_len_tail_clear(i)); /* ours, shown as the original */
        for(unsigned i=0;i<FR_H_SITES;i++) assert(fr_h_state(i)==1 && !fr_h_clear(i));
        assert(fr_pg_state()==1 && !fr_pg_clear() && (fr_done&FR_PG_DONE) && fr_owned());
        {   /* FR27: the solver's entry through fsp_arm (our jump: the step chain is off here), then the executable's solver
             * on its own bytes (exact reciprocal and the portable math installed, as in the lanes) through that entry on
             * random scenes: 'redirect' runs native on a copy and ours on the scene, 'shadow' the reverse; they must agree
             * bit for bit (no sp difference, no order difference), and both with ours called directly. */
            assert(fr_sp_state()==0 && fr_sp_clear() && fr_sp_pins_ok() && !stc_sp_entry_done);
            assert(!fsp_arm(7) && !strcmp(fsp_stage,"mode"));
            assert(fsp_arm(FSP_REDIRECT) && fsp_installed && !fsp_route && fr_sp_state()==1 && !fr_sp_clear() && (fr_done&FR_SP_DONE));
            assert(fr_sp_pins_ok() && fr_owned() && fsp_arm(FSP_REDIRECT) && !fsp_arm(FSP_SHADOW)); /* the view keeps the pin; a rerun */
            /* this copy is mapped read-write: the solver with its callees and the two portable-math entries it calls
             * (the sincos pair 0x1750253, atan2f 0x1a1a398) made executable */
            static const uint32_t exec_pages[3][2]={{0x14e1000u,0x4000u},{0x1750000u,0x1000u},{0x1a1a000u,0x1000u}};
            DWORD exec_old[3];
            for(unsigned i=0;i<3;i++) {
                assert(VirtualProtect(image+exec_pages[i][0],exec_pages[i][1],PAGE_EXECUTE_READWRITE,&exec_old[i]));
                FlushInstructionCache(GetCurrentProcess(),image+exec_pages[i][0],exec_pages[i][1]);
            }
            unsigned char *saved_pm=pmath_page;pmath_page=pmath_alloc_page();assert(pmath_page && pmath_write_page());
            for(unsigned i=0;i<PMATH_COUNT;i++) memcpy(pmath_original[i],image+pmath_sites[i].rva,5);
            assert(pmath_switch(1)==1);
            typedef uint64_t (*entry_fn)(void *,float *,float,float *,float,uint64_t,uint64_t,float *,float);
            const entry_fn entry=(entry_fn)(void*)(image+FR_SP_SOLVER);
            tfsp_seed=0x2727u;unsigned moved=0,searched=0,large=0;
            for(int mode=FSP_REDIRECT;mode>=FSP_SHADOW;mode--) {
                fsp_mode=mode;
                for(unsigned i=0;i<1500;i++) {
                    static tfsp_scene sc,t;tfsp_make(&sc);t=sc;
                    uintptr_t vec[3]={(uintptr_t)sc.boxes,(uintptr_t)(sc.boxes+sc.n*FSP_BOX),(uintptr_t)(sc.boxes+sc.n*FSP_BOX)};
                    const LONG64 n0=fsp_n,d0=fsp_diff,o0=fsp_order;float p0[4];memcpy(p0,sc.p,16);
                    entry(vec,sc.p,sc.r,sc.dir,sc.a5,0x1234,(uint64_t)sc.y,NULL,sc.a9);
                    if(fsp_n!=n0+1 || fsp_diff!=d0 || fsp_order!=o0) {printf("FR27 scene %u mode %d: n %lld diff %lld order %lld\n",i,mode,(long long)(fsp_n-n0),(long long)(fsp_diff-d0),(long long)(fsp_order-o0));assert(0);}
                    fsp_solve(t.boxes,t.n,t.p,t.r,t.dir,t.a5,t.y,NULL,t.a9);
                    assert(!memcmp(sc.p,t.p,16) && !memcmp(sc.boxes,t.boxes,t.n*FSP_BOX));
                    moved+=memcmp(p0,sc.p,16)!=0;searched+=sc.a5!=0.0f;large+=sc.a9>0.0f && sc.a9<25000.0f;
                }
            }
            assert(fsp_n==3000 && fsp_diff==0 && fsp_order==0 && fsp_calls==3000 && fsp_ours==1500 && fsp_move==0 && fsp_invalid==0);
            assert(moved>1000 && searched>600);
            assert(pmath_switch(0)==1);VirtualFree(pmath_page,0,MEM_RELEASE);pmath_page=saved_pm;
            for(unsigned i=0;i<3;i++) {DWORD ignored;assert(VirtualProtect(image+exec_pages[i][0],exec_pages[i][1],exec_old[i],&ignored));}
            fsp_mode=FSP_REDIRECT;
            printf("fp redirect FR27 native image: the solver 0x14e4320's entry armed/owned (pins through the view), its own bytes (exact reciprocal, portable math) through the entry on 3000 random scenes ('redirect' and 'shadow'; %u moved, %u with the angle search, %u with a9 in range): native == ours, sp=0:0:3000, order 0 PASS\n",
                   moved,searched,large);
        }
        test_fa_native_image(); /* FR29b, while armed */
        test_fdr_native_image(); /* FR30, while armed (FR29b's sites too) */
        assert(fr_len_pins() && fr_h_pins_ok() && fr_pg_pins_ok() && fr_arm(FR_REDIRECT,3)); /* the view keeps the pins passing; a rerun is accepted */
        fr_restore();assert(!fr_installed && fr_restored && !fr_done && fr_len_state(0)==0 && fr_len_state(1)==0 && fr_tail_state()==0);
        test_fa_native_image_restored(); /* FR29b: fr_restore took its sites back first */
        test_fdr_native_image_restored(); /* FR30: likewise */
        assert(!fsp_installed && fsp_mode==FSP_OFF && fr_sp_state()==0 && fr_sp_clear() && !strcmp(fsp_stage,"restored")); /* FR27 */
        fsp_stage="idle";fsp_calls=fsp_ours=fsp_n=fsp_diff=fsp_max_ulp=fsp_order=fsp_move=fsp_invalid=fsp_x0=fsp_nocopy=0;
        for(unsigned i=0;i<FR_H_SITES;i++) assert(fr_h_state(i)==0);
        assert(fr_pg_state()==0 && fr_pg_clear());
        assert(fr_aff_state()==0 && fr_aff_clear() && fr_bl_state()==0 && *(void**)(image+FR_BL_SLOT)==(void*)(image+FR_BL_FN));
        assert(fr_vel_state()==0 && fr_vel_clear() && fr_fac_state()==0 && fr_fac_clear() && fr_sz_state()==0 && fr_sz_clear());
        for(unsigned i=0;i<FR_SITES;i++) assert(fr_site_state(i)==0);
        VirtualFree(fr_page,0,MEM_RELEASE);fr_page=NULL;fr_restored=0;fr_stage="idle";fr_mode=FR_OFF;
        fr_native_refresh=NULL;fr_native_g4=fr_native_g0=NULL;fr_native_m=NULL;fr_native_params=NULL;fr_native_v=NULL;fr_native_writer=NULL;
        stats.active=sa;stats.issued=si;ui_thread=su;boot_mode=sb;fr_id_count=sids;
        xrcp_restore();assert(!xrcp_installed);VirtualFree(xrcp_region,0,MEM_RELEASE);xrcp_region=NULL;xrcp_restored=0;xrcp_stage="idle";xrcp_attempts=xrcp_busy=0;
        assert(fr_pins());
    }
    /* The frame and field facts the hooks rely on: the getters' buffer lea r8,[rbp+0x50]; w from [rbp-0x8]; comp[A]/[B]. */
    assert(!memcmp(image+0xea22a7,"\x4c\x8d\x45\x50",4) && !memcmp(image+0xea22be,"\x4c\x8d\x45\x50",4) && !memcmp(image+0xea2307,"\xf3\x0f\x10\x4d\xf8",5));
    assert(!memcmp(image+0xea229e,"\x48\x8d\x55\xf0",4) && !memcmp(image+0xea22e2,"\xf3\x41\x0f\x5e\x96\xf4\x05\x00\x00",9) && !memcmp(image+0xea22f6,"\xf3\x41\x0f\x5e\x9e\xf8\x05\x00\x00",9));
    assert(!memcmp(image+0xea23da,"\xf3\x41\x0f\x5e\xbe\xf4\x05\x00\x00",9) && !memcmp(image+0xea23e3,"\xf3\x41\x0f\x11\x7f\x24",6));
    /* The first call's max block: 1.0/d (xmm6 = 1.0 from 0x6dc6d5, d = xmm7) at 0x6dcd14, then mulss by it at 0x6dcd18;
     * K from [r15+0x300] (0x6dcceb), r15 = [0x146cf1538] (0x6dcbb0); v from rdx = rsp+0x40 (0x6dccc2). */
    static const unsigned char rcp[8]={0xf3,0x0f,0x5e,0xf7,0xf3,0x0f,0x59,0xc6},one[4]={0xf3,0x0f,0x10,0x35};
    static const unsigned char kload[9]={0xf3,0x41,0x0f,0x10,0x87,0x00,0x03,0x00,0x00},vbuf[5]={0x48,0x8d,0x54,0x24,0x40},table[3]={0x4c,0x8b,0x3d};
    assert(!memcmp(image+0x6dcd14,rcp,8) && !memcmp(image+0x6dc6d5,one,4) && !memcmp(image+0x6dcceb,kload,9) && !memcmp(image+0x6dccc2,vbuf,5));
    {int32_t d;memcpy(&d,image+0x6dcbb3,4);assert(!memcmp(image+0x6dcbb0,table,3) && 0x6dcbb7u+(uint32_t)d==0x6cf1538u);}
    /* Run the executable's M and +0x0 block on its own bytes (this copy only). */
    DWORD old;assert(VirtualProtect(image+0xea2000,0x2000,PAGE_EXECUTE_READWRITE,&old));
    unsigned char back[14]={0xff,0x25,0,0,0,0};void *to=(void*)test_fr_win_back;memcpy(back+6,&to,8);memcpy(image+0xea239b,back,14);
    FlushInstructionCache(GetCurrentProcess(),image+0xea2000,0x2000);
    const fr_m_fn native_m=(fr_m_fn)(void*)(image+FR_M_RVA);
    unsigned m_equal=0,m_negzero=0;
    for(unsigned i=0;i<20000;i++) {
        const float a=tfr_range(-500.0f,500.0f),b=(i%7==0)?a+tfr_range(-5e-5f,5e-5f):tfr_range(-500.0f,500.0f),c=(i%5==0)?a:tfr_range(-800.0f,800.0f);
        const uint64_t flag=tfr_next()&1u;
        const uint32_t n=fr_bits(native_m(a,b,c,flag)),o=fr_bits(fr_m((unsigned)flag,a,b,c));
        if(n==o) {++m_equal;continue;}
        assert(o==0x80000000u && n==0u && c==a && b-a<0.0f);++m_negzero; /* r = -0: Linux keeps it, Windows' maxss gives +0 */
    }
    assert(m_equal>15000 && m_negzero>0);
    static unsigned char frame[0x200];unsigned char slot[0x40];unsigned block_equal=0;
    unsigned char *rbp=frame+0x20;
    for(unsigned i=0;i<20000;i++) {
        const float ca=tfr_range(0.0005f,0.02f),cb=(i%9==0)?ca:tfr_range(0.0005f,0.02f),g0=tfr_range(0.01f,2000.0f);
        float w=tfr_range(0.0f,2500.0f);
        if(i%11==0) w=1.0f/cb;else if(i%13==0) w=1.0f/ca;
        memcpy(tfr_comp+FR_COMP_A,&ca,4);memcpy(tfr_comp+FR_COMP_B,&cb,4);memcpy(rbp-8,&w,4);memcpy(slot,&g0,4);
        const uint32_t n=fr_bits(test_fr_win_block(image+0xea22cf,tfr_comp,rbp,slot));
        uint32_t ours[3];fr_recompute(fr_bits(ca),fr_bits(cb),0,fr_bits(g0),fr_bits(w),ours);
        if(n!=ours[0]) {printf("fp redirect block %u: ca=%08x cb=%08x g0=%08x w=%08x native %08x ours %08x\n",i,fr_bits(ca),fr_bits(cb),fr_bits(g0),fr_bits(w),n,ours[0]);assert(0);}
        ++block_equal;
    }
    /* The writer 0x6db200 (and its helper 0x6db600) on its own bytes, random inputs through the fake ship and the table
     * pointer of this copy: native equals Windows' order always; against ours (Linux's order) it splits. */
    assert(!memcmp(image+0x6db3e3,"\xf3\x0f\x5e\xe2",4)); /* divss xmm4,xmm2: 1.0/den, then mulss by it */
    assert(VirtualProtect(image+0x6db000,0x1000,PAGE_EXECUTE_READWRITE,&old));FlushInstructionCache(GetCurrentProcess(),image+0x6db000,0x1000);
    const fr_writer_fn native_w=(fr_writer_fn)(void*)(image+FR_W_BODY);
    unsigned w_equal=0,w_split=0,w_k=0,w_fields[4]={0,0,0,0};uint32_t w_max=0;
    for(unsigned i=0;i<20000;i++) {
        fr_w_in in,got;tfr_w_random(i,&in);tfw_load(&in);
        unsigned char wslot[0x60];memset(wslot,0,sizeof(wslot));
        (void)native_w(tfw_ship,tfw_data,tfw_a,wslot);
        assert(fr_w_read(tfw_ship,tfw_data,tfw_a,&got) && got.has_p==in.has_p && (!in.has_p || got.p==in.p) && !memcmp(got.a,in.a,16) && !memcmp(got.d,in.d,20));
        assert(!memcmp(got.dm,in.dm,16) && !memcmp(got.r,in.r,20) && got.t_scale==in.t_scale && got.k==in.k && got.t_clamp==in.t_clamp);
        uint32_t n[4],ours[4],win[4];memcpy(n,wslot,16);fr_w_recompute(&got,ours);tfr_w_windows(&got,win);
        if(i%17==0) ++w_k;
        int split=0;
        for(unsigned k=0;k<4;k++) {
            if(n[k]!=win[k]) {printf("fp redirect writer %u field %u: native %08x, Windows' order %08x\n",i,k,n[k],win[k]);assert(0);}
            if(n[k]!=ours[k]) {split=1;++w_fields[k];const uint32_t u=fr_ulp(n[k],ours[k]);if(u>w_max) w_max=u;}
        }
        if(split) ++w_split;else ++w_equal;
    }
    memset(image+0x6cf1538u,0,8);
    assert(w_split>1000 && w_max<=8);
    /* FR10/FR11: the length pins; then both inlined Set blocks on their own bytes with the exact-reciprocal windows
     * installed (their rcp(rsqrt) chains as sqrt, as in the game): native stores Windows' pairwise length on every
     * input; through the stubs 'shadow' stores the same, 'redirect' the canonical length with Linux's zero rule. */
    for(unsigned i=0;i<FR_LEN_SITES;i++) {
        assert(!memcmp(image+fr_len_sites[i].site,fr_len_original[i],5));
        image[fr_len_sites[i].pin+0x30]^=1;assert(!fr_len_pins());image[fr_len_sites[i].pin+0x30]^=1;
    }
    assert(fr_len_pins() && xrcp_pins() && xrcp_build() && xrcp_switch(1)==1);xrcp_installed=1;
    for(unsigned i=0;i<FR_LEN_SITES;i++) {
        unsigned char *at=image+(fr_len_sites[i].pin&~0xfffu);assert(VirtualProtect(at,0x2000,PAGE_EXECUTE_READWRITE,&old));
        unsigned char back[14]={0xff,0x25,0,0,0,0};void *to=(void*)test_fr_len_block_back;memcpy(back+6,&to,8);
        memcpy(image+fr_len_sites[i].back,back,14);FlushInstructionCache(GetCurrentProcess(),at,0x2000);
    }
    const uint32_t saved_active=stats.active;stats.active=1;unsigned zeros=0;
    fr_mode=FR_OFF;const unsigned native_split=tfl_native_runs(10000,&zeros);
    fr_page=fr_alloc_page();assert(fr_page && VirtualProtect(fr_page,0x1000,PAGE_READWRITE,&old));fr_write_code(fr_page);
    assert(VirtualProtect(fr_page,0x1000,PAGE_EXECUTE_READ,&old));FlushInstructionCache(GetCurrentProcess(),fr_page,0x1000);
    for(unsigned i=0;i<FR_LEN_SITES;i++) {
        unsigned char jump[5];fr_len_jump(i,jump);assert(fr_len_reach(i) && view_cas_bytes(fr_len_sites[i].site,fr_len_original[i],jump,5));
        fr_done|=FR_LEN_DONE<<i;assert(fr_len_state(i)==1);
        unsigned char seen[5];assert(read_local(image+fr_len_sites[i].site,seen,5) && !memcmp(seen,fr_len_original[i],5)); /* the view */
    }
    const LONG64 n0=fr_n[FR_LEN],d0=fr_diff[FR_LEN],h0=fr_hits[FR_HIT_LEN0],h1=fr_hits[FR_HIT_LEN1];
    fr_mode=FR_SHADOW;const unsigned shadow_split=tfl_native_runs(10000,&zeros);
    assert(fr_n[FR_LEN]==n0+20000 && fr_diff[FR_LEN]==d0+shadow_split && fr_hits[FR_HIT_LEN0]==h0+10000 && fr_hits[FR_HIT_LEN1]==h1+10000);
    fr_mode=FR_REDIRECT;const unsigned redirect_split=tfl_native_runs(10000,&zeros);
    /* FR13: the handle sites on their own bytes (windows installed): native = d * 0.333333343f, then through the stubs
     * 'shadow' keeps it and 'redirect' stores d / 3.0f. */
    assert(fr_h_pins_ok());
    for(unsigned i=0;i<FR_H_SITES;i++) {
        assert(fr_h_state(i)==0 && fr_h_clear(i));
        unsigned char *at=image+(tfh_after[i]&~0xfffu);assert(VirtualProtect(at,0x2000,PAGE_EXECUTE_READWRITE,&old));
        unsigned char back[14]={0xff,0x25,0,0,0,0};void *to=(void*)test_fr_h_block_back;memcpy(back+6,&to,8);
        memcpy(image+tfh_after[i],back,14);FlushInstructionCache(GetCurrentProcess(),at,0x2000);
    }
    fr_mode=FR_OFF;const unsigned h_native=tfh_native_runs(5000,0);
    for(unsigned i=0;i<FR_H_SITES;i++) {
        unsigned char jump[5];fr_h_jump(i,jump);assert(fr_h_reach(i) && view_cas_bytes(fr_h_sites[i].site,fr_h_original,jump,5));
        fr_done|=FR_H_DONE<<i;assert(fr_h_state(i)==1 && !fr_h_clear(i));
        unsigned char seen[5];assert(read_local(image+fr_h_sites[i].site,seen,5) && !memcmp(seen,fr_h_original,5)); /* the view */
    }
    const LONG64 hn0=fr_n[FR_HANDLE];
    fr_mode=FR_SHADOW;const unsigned h_shadow=tfh_native_runs(5000,1);
    assert(fr_n[FR_HANDLE]==hn0+20000 && fr_diff[FR_HANDLE]==(LONG64)h_shadow);
    fr_mode=FR_REDIRECT;const unsigned h_redirect=tfh_native_runs(5000,1);
    assert(fr_handle_written==(LONG64)h_redirect && h_native>1000 && h_shadow>1000 && h_redirect>1000);
    /* FR16: the PointGuard angle on its own bytes for every (m+1, rem) up to 200: native = rem * (pi/(m+1)); through the
     * stub 'shadow' keeps it and 'redirect' leaves (rem / ((m+1)+(m+1))) * 2pi. */
    assert(fr_pg_pins_ok() && fr_pg_state()==0 && fr_pg_clear());
    {   unsigned char *at=image+(FR_PG_BACK&~0xfffu);assert(VirtualProtect(at,0x2000,PAGE_EXECUTE_READWRITE,&old));
        unsigned char back[14]={0xff,0x25,0,0,0,0};void *to=(void*)test_fr_pg_block_back;memcpy(back+6,&to,8);
        memcpy(image+FR_PG_BACK,back,14);FlushInstructionCache(GetCurrentProcess(),at,0x2000);
    }
    fr_mode=FR_OFF;const unsigned pg_native=tpg_native_runs(0);
    {   unsigned char jump[5];fr_pg_jump(jump);assert(fr_pg_reach() && view_cas_bytes(FR_PG_SITE,fr_pg_original,jump,5));
        fr_done|=FR_PG_DONE;assert(fr_pg_state()==1 && !fr_pg_clear());
        unsigned char seen[5];assert(read_local(image+FR_PG_SITE,seen,5) && !memcmp(seen,fr_pg_original,5)); /* the view */
    }
    const LONG64 pn0=fr_n[FR_PG],ph0=fr_hits[FR_HIT_PG];
    fr_mode=FR_SHADOW;const unsigned pg_shadow=tpg_native_runs(1);
    assert(fr_n[FR_PG]==pn0+TPG_RUNS && fr_diff[FR_PG]==(LONG64)pg_shadow && fr_hits[FR_HIT_PG]==ph0+TPG_RUNS);
    fr_mode=FR_REDIRECT;const unsigned pg_redirect=tpg_native_runs(1);
    assert(fr_pg_written==(LONG64)pg_redirect && pg_native>1000 && pg_shadow==pg_native && pg_redirect==pg_native && fr_max_ulp[FR_PG]<=2);
    /* FR22: ComputeLength on its own bytes, natively and then through the swapped slot; the affine constructor on its
     * own bytes, natively and then through the stub at 0x65a66e. */
    assert(fr_bl_pins_ok() && fr_aff_pins_ok() && fr_bl_state()==0 && fr_aff_state()==0 && fr_aff_clear());
    assert(VirtualProtect(image+(FR_BL_CHORD&~0xfffu),0x2000,PAGE_EXECUTE_READWRITE,&old));
    assert(VirtualProtect(image+(FR_AFF_PIN&~0xfffu),0x1000,PAGE_EXECUTE_READWRITE,&old));
    FlushInstructionCache(GetCurrentProcess(),image+(FR_BL_CHORD&~0xfffu),0x2000);FlushInstructionCache(GetCurrentProcess(),image+(FR_AFF_PIN&~0xfffu),0x1000);
    fr_mode=FR_OFF;const unsigned bl_native=tbl_native_runs(400,0),aff_native=tfa_native_runs(4000,0);
    assert(fr_bl_swap((void*)(image+FR_BL_FN),(void*)fr_bl_hook) && fr_bl_state()==1);fr_done|=FR_BL_DONE;
    {   unsigned char jump[5];fr_aff_jump(jump);assert(fr_aff_reach() && view_cas_bytes(FR_AFF_SITE,fr_aff_original,jump,5));
        fr_done|=FR_AFF_DONE;assert(fr_aff_state()==1 && !fr_aff_clear());
        unsigned char seen[5];assert(read_local(image+FR_AFF_SITE,seen,5) && !memcmp(seen,fr_aff_original,5)); /* the view */
    }
    const LONG64 bn0=fr_n[FR_BL],an0=fr_n[FR_AFF];
    fr_mode=FR_SHADOW;const unsigned bl_shadow=tbl_native_runs(400,1),aff_shadow=tfa_native_runs(4000,1);
    assert(fr_n[FR_BL]>bn0 && fr_diff[FR_BL]>0 && fr_n[FR_AFF]>an0 && fr_diff[FR_AFF]>0 && !fr_bl_written && !fr_aff_written);
    fr_mode=FR_REDIRECT;const unsigned bl_redirect=tbl_native_runs(400,1),aff_redirect=tfa_native_runs(4000,1);
    assert(fr_bl_written>0 && fr_aff_written>0 && bl_native>100 && bl_shadow>100 && bl_redirect>100 && aff_native>100 && aff_shadow>100 && aff_redirect>100);
    printf("fp redirect native image: BezierSpline::ComputeLength 0x14e4f20 on its own bytes: native = u*(1/n) (%u split from i/n, max %lld ulp); "
        "through the slot shadow keeps it (%u split), redirect stores i/n (%lld written); AffineSpline 0x65a580: native = (2/sum)*dp "
        "(%u of 4000 split from dp/(sum*0.5)), shadow the same (%u), redirect ours (%lld weights written) PASS\n",
        bl_native,(long long)fr_max_ulp[FR_BL],bl_shadow,(long long)fr_bl_written,aff_native,aff_shadow,(long long)fr_aff_written);
    fr_n[FR_BL]=fr_diff[FR_BL]=fr_max_ulp[FR_BL]=0;fr_bl_written=0;fr_n[FR_AFF]=fr_diff[FR_AFF]=fr_max_ulp[FR_AFF]=0;fr_aff_written=0;
    /* FR23: the velocity block on its own bytes: natively, then through the stub in 'shadow' and 'redirect'. */
    assert(fr_vel_pins_ok() && fr_vel_state()==0 && fr_vel_clear());
    unsigned char vel_saved[14];memcpy(vel_saved,image+0xeb3322u,14);
    {   unsigned char *at=image+(0xeb3322u&~0xfffu);assert(VirtualProtect(image+(FR_VEL_SITE&~0xfffu),0x2000,PAGE_EXECUTE_READWRITE,&old));
        unsigned char back[14]={0xff,0x25,0,0,0,0};void *to=(void*)test_fr_vel_back;memcpy(back+6,&to,8);
        memcpy(image+0xeb3322u,back,14);FlushInstructionCache(GetCurrentProcess(),at,0x2000);
    }
    fr_mode=FR_OFF;const unsigned vel_native=tfv_native_runs(0);
    {   unsigned char jump[5];fr_vel_jump(jump);assert(fr_vel_reach() && view_cas_bytes(FR_VEL_SITE,fr_vel_original,jump,5));
        fr_done|=FR_VEL_DONE;assert(fr_vel_state()==1 && !fr_vel_clear());
        unsigned char seen[5];assert(read_local(image+FR_VEL_SITE,seen,5) && !memcmp(seen,fr_vel_original,5)); /* the view */
    }
    const LONG64 vn0=fr_n[FR_VEL],vh0=fr_hits[FR_HIT_VEL];
    fr_mode=FR_SHADOW;const unsigned vel_shadow=tfv_native_runs(1);
    assert(fr_n[FR_VEL]>vn0 && fr_diff[FR_VEL]==(LONG64)vel_shadow && fr_hits[FR_HIT_VEL]==vh0+(fr_n[FR_VEL]-vn0) && !fr_vel_written);
    fr_mode=FR_REDIRECT;const unsigned vel_redirect=tfv_native_runs(2);
    assert(fr_vel_written==(LONG64)vel_redirect && vel_native>100 && vel_shadow==vel_native && vel_redirect==vel_native);
    printf("fp redirect native image: the Linear step's velocity block 0xeb32fd on its own bytes: native = (s,s,s,0)*row2 (%u of %u cases split "
        "from (r0*0 + 0*r1) + s*row2, all at s = +-0); through the stub every register as native, shadow keeps it, redirect stores ours (%lld written) PASS\n",
        vel_native,TFV_CASES,(long long)fr_vel_written);
    memcpy(image+0xeb3322u,vel_saved,14);FlushInstructionCache(GetCurrentProcess(),image+0xeb3000u,0x1000);
    fr_n[FR_VEL]=fr_diff[FR_VEL]=fr_max_ulp[FR_VEL]=0;fr_vel_written=0;
    /* FR24: the facing factor block on its own bytes: natively, then through the stub in 'shadow' and 'redirect'. */
    assert(fr_fac_pins_ok() && fr_fac_state()==0 && fr_fac_clear());
    unsigned char fac_saved[14];memcpy(fac_saved,image+FR_FAC_BACK,14);
    {   unsigned char back[14]={0xff,0x25,0,0,0,0};void *to=(void*)test_fr_fac_back;memcpy(back+6,&to,8);
        memcpy(image+FR_FAC_BACK,back,14);FlushInstructionCache(GetCurrentProcess(),image+0xeb3000u,0x1000);
    }
    fr_mode=FR_OFF;const unsigned fac_native=tfc_native_runs(0);
    {   unsigned char jump[5];fr_fac_jump(jump);assert(fr_fac_reach() && view_cas_bytes(FR_FAC_SITE,fr_fac_original,jump,5));
        fr_done|=FR_FAC_DONE;assert(fr_fac_state()==1 && !fr_fac_clear());
        unsigned char seen[5];assert(read_local(image+FR_FAC_SITE,seen,5) && !memcmp(seen,fr_fac_original,5)); /* the view */
    }
    const LONG64 cn0=fr_n[FR_FAC],ch0=fr_hits[FR_HIT_FAC];
    fr_mode=FR_SHADOW;const unsigned fac_shadow=tfc_native_runs(1);
    assert(fr_n[FR_FAC]==cn0+TFC_CASES && fr_diff[FR_FAC]==(LONG64)fac_shadow && fr_hits[FR_HIT_FAC]==ch0+TFC_CASES && !fr_fac_written);
    fr_mode=FR_REDIRECT;const unsigned fac_redirect=tfc_native_runs(2);
    assert(fr_fac_written==(LONG64)fac_redirect && fac_native>100 && fac_shadow==fac_native && fac_redirect==fac_native);
    printf("fp redirect native image: the Linear step's facing factor 0xeb3a91 on its own bytes: native = 1 - m*0.636619747f (%u of %u cases split "
        "from 1 - m/1.57079637f, max %lld ulp); through the stub every register as native, shadow keeps it, redirect leaves ours (%lld written) PASS\n",
        fac_native,TFC_CASES,(long long)fr_max_ulp[FR_FAC],(long long)fr_fac_written);
    memcpy(image+FR_FAC_BACK,fac_saved,14);FlushInstructionCache(GetCurrentProcess(),image+0xeb3000u,0x1000);
    fr_n[FR_FAC]=fr_diff[FR_FAC]=fr_max_ulp[FR_FAC]=0;fr_fac_written=0;
    fr_installed=1;fr_restore();assert(!fr_installed && !fr_done && fr_len_state(0)==0 && fr_len_state(1)==0);
    for(unsigned i=0;i<FR_H_SITES;i++) assert(fr_h_state(i)==0);
    assert(fr_pg_state()==0 && fr_aff_state()==0 && fr_bl_state()==0 && fr_vel_state()==0 && fr_vel_pins_ok() && fr_fac_state()==0 && fr_fac_pins_ok());
    printf("fp redirect native image: PointGuard angle 0xe9c4bc on its own bytes: native = rem*(pi/(m+1)) on %u (m+1 <= %u; %u split from "
        "(rem/2(m+1))*2pi, max %lld ulp); shadow keeps it (%u split), redirect leaves ours (%u written) PASS\n",
        TPG_RUNS,TPG_M1,pg_native,(long long)fr_max_ulp[FR_PG],pg_shadow,pg_redirect);
    fr_n[FR_PG]=fr_diff[FR_PG]=fr_max_ulp[FR_PG]=0;fr_pg_written=0;
    fr_n[FR_HANDLE]=fr_diff[FR_HANDLE]=fr_max_ulp[FR_HANDLE]=0;fr_handle_written=0;
    printf("fp redirect native image: handle sites 0x857d19/0x857df0/0x31c9a9/0x31ca80 on their own bytes (exact reciprocal installed): "
        "native = d*0.333333343f on 20000 (%u split from d/3); shadow keeps it (%u split), redirect stores d/3 (%u written) PASS\n",h_native,h_shadow,h_redirect);
    fr_restored=0;fr_mode=FR_OFF;stats.active=saved_active;fr_n[FR_LEN]=fr_diff[FR_LEN]=fr_max_ulp[FR_LEN]=0;fr_len_written=0;
    VirtualFree(fr_page,0,MEM_RELEASE);
    xrcp_restore();assert(!xrcp_installed);VirtualFree(xrcp_region,0,MEM_RELEASE);xrcp_region=NULL;xrcp_restored=0;xrcp_stage="idle";xrcp_attempts=xrcp_busy=0;
    assert(native_split>500 && shadow_split>500 && redirect_split>500 && zeros>=6*(10000/97));
    printf("fp redirect native image: Set inlined at 0x857960/0x31c640 on its own bytes (exact reciprocal installed): native = pairwise "
        "on 20000 (%u split from the canonical); shadow stores native (%u split), redirect the canonical (%u split); %u zero lengths stored 1.0 PASS\n",
        native_split,shadow_split,redirect_split,zeros);
    fr_page=saved_page;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    printf("fp redirect native image: pins, frame facts, M %u equal + %u -0 cases, +0x0 block %u equal PASS\n",m_equal,m_negzero,block_equal);
    printf("fp redirect native image: writer 0x6db200 = Windows' order on 20000 (%u all-K); vs ours %u equal, %u split (fields %u/%u/%u/%u), max %u ulp PASS\n",
        w_k,w_equal,w_split,w_fields[0],w_fields[1],w_fields[2],w_fields[3],w_max);
}
