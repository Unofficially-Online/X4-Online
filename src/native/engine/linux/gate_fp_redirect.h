/* native_gate_fp_redirect on the Linux X4 9.00 executable: the hooks of lockstep_fp_redirect.h, whose strict
 * recomputation, counters, rows and status are shared. Included by gate_linux.c only. Windows twin:
 * lockstep_fp_redirect_win.h.
 *
 * The refresh 0x14378c0 (rdi owner, rsi slot, edx flag) has twelve direct callers, so its prologue moves instead:
 * push r15; push r14; push r13 (6 bytes) into a trampoline, the entry's aligned 8 bytes (with push r12 after them)
 * change in one compare-exchange to a jmp to the thunk page. Inside it: G4 0x15d1770 at 0x1437c0b, G0 0x15d18a0 at
 * 0x1437c1f (rdi comp = r12, rsi [owner+0x20], rdx r14 = rsp+0x70, filled by 0x18a0090), M 0x1429e80 at 0x14381e2
 * (+0x4) and 0x1438213 (+0x0) (edi flag, xmm0 a, xmm1 b, xmm2 c); each rel32 in one aligned 8-byte word. The query
 * vector is rsp+0x30 and w its lane 2, [rsp+0x38] (minss at 0x1437c3d): w = buffer - 0x38. comp[A] = +0x5d4
 * (+0x24 = 1/that, 0x1437cc7), comp[B] = +0x5d8. L is 0x1a48040 (x0 xmm0, x1 xmm1, a xmm2, b xmm3, x xmm4,
 * eps xmm5). The executable's file pin covers every byte; test_gate_linux runs its L and M against ours. */
#define FR_COMP_A 0x5d4
#define FR_COMP_B 0x5d8
#define FR_W_FROM_BUFFER (-0x38)
#define FR_M_PARAMS uint64_t flag,float a,float b,float c
#define FR_M_ARGS flag,a,b,c
#define FR_P_PARAMS void *o20,unsigned char *slot,uint64_t flag,uint64_t a4,uint64_t a5,double time /* rdi, rsi, edx, ecx, r8d, xmm0 */
#define FR_P_ARGS o20,slot,flag,a4,a5,time
/* FR27's solver calls the portable sinf/cosf/atan2f itself (the same code the preload gives the executable): compiled
 * into this module (hidden symbols; x4pm_kat checked at arm). */
#include "../../portable_math/portable_math.c"
#include "lockstep_fp_redirect.h"
#include "lockstep_fp_attack.h" /* FR29b: the attack capture (its Linux part: gate_fp_attack.h) */
#define LXF_THUNK 0x800u /* the relocated prologue, then jmp [rip] -> the refresh + 6 */
#define LXF_HOOK 0x840u  /* jmp [rip] -> fr_refresh_hook */
#define LXF_STUBS 0x860u /* five jmp [rip] -> the getter, M and first-call hooks, 0x20 apart (to 0x900) */
#define LXF_SITES 5u
#define LXF_ENTRY_DONE 0x100u
#define LXF_DETOURS 3u
#define LXF_DETOUR_DONE 0x200u /* and 0x400, 0x800 */
#define LXF_V_STUB 0x940u      /* movsd xmm0,fs:-0x2728 (moved); movss xmm1,[rsp] (d); call [rip] -> lxf_v_hook; jmp [rip] -> 0x148d812 */
#define LXF_MULPS_STUB 0x980u  /* the branch's four argument moves (moved); call [rip] -> lxf_mulps_hook; jmp [rip] -> 0x148d953 */
#define LXF_WRITER_STUB 0x9c0u /* the watched path's three argument moves (moved); call [rip] -> fr_writer_run; jmp [rip] -> 0x148cd63 */
/* FR27: the GetSafePos solver 0x1a4b9f0 (rdi the boxes vector, rsi the point, rdx the direction, ecx the x flag, r8d the
 * y flag, r9 the move direction, xmm0 radius, xmm1 a5, xmm2 a9; callers 0x115563c, 0x1d25dbb, 0x1d3702b read only the
 * point after it). Its entry movabs rax (10 bytes, no rip operand) moves into a trampoline and its aligned 16-byte line
 * gets a jmp to the entry stub (FR25's scheme, gate_step_trace.h; with the step chain armed, its entry hook owns the
 * line and routes the call here: lxc_sp_route). The executable's file pin covers every byte. */
#define LXF_SP_ENTRY 0xd00u /* jmp [rip] -> lxf_sp_hook (after the step chain's 0xcc0..0xcf8) */
#define LXF_SP_TRAMP 0xd20u /* movabs rax (moved), jmp [rip] -> the solver + 10 */
#define LXF_FA_STUB 0xd40u  /* FR29b: the attack capture's stubs (0xd40..0xe20): one jmp [rip] -> hook per kind */
static const struct {uint32_t site,target;const char *name;} lxf_sites[LXF_SITES]={
    {LX_FR_G4_SITE_VA,LX_FR_G4_VA,"g4"},{LX_FR_G0_SITE_VA,LX_FR_G0_VA,"g0"},{LX_FR_M4_SITE_VA,LX_FR_M_VA,"m4"},{LX_FR_M0_SITE_VA,LX_FR_M_VA,"m0"},
    {LX_FR_P_SITE_VA,LX_FR_P_VA,"params"},
};
static uint64_t lxf_entry_patched;
/* Three calls inside the first call whose rel32 crosses a 16-byte line: the bytes from a detour's start up to and
 * including the call (orig, len bytes; back = the address after the call) move into a stub that replays them up to
 * the call, calls our hook in its place (same arguments, same rsp: the stub is entered by a jmp) and jumps back.
 *   v       0x148d803: movsd from fs (every argument of the v call already set; d at [rsp], stored at 0x148d7e5),
 *           then movss xmm1,[rsp] for the hook's eighth argument; call 0x18a15f0.
 *   mulps   0x148d940: mov rsi,[rsp+0x28]; mov rcx,rbx; mov rdx,r14; mov rdi,r12; call 0x14811c0 (the writer).
 *   writer  0x148cd53: mov rsi,[rsp+0x28]; mov rcx,rbx; mov rdi,r12 (rdx = r14 set at 0x148cd4a); call 0x14811c0, on
 *           the watched ships' path after the defaults struct's rep movsq.
 * No branch lands inside any of them (checked over the whole .text); only the first 5 bytes change. */
static const struct {uint32_t at,back,stub,len;const char *orig,*extra;} lxf_detours[LXF_DETOURS]={
    {LX_FR_V_AT_VA,LX_FR_V_BACK_VA,LXF_V_STUB,15,"\x64\xf2\x0f\x10\x04\x25\xd8\xd8\xff\xff\xe8\xde\x3d\x41\x00","\xf3\x0f\x10\x0c\x24"},
    {LX_FR_MULPS_AT_VA,LX_FR_MULPS_BACK_VA,LXF_MULPS_STUB,19,"\x48\x8b\x74\x24\x28\x48\x89\xd9\x4c\x89\xf2\x4c\x89\xe7\xe8\x6d\x38\xff\xff",""},
    {LX_FR_WRITER_AT_VA,LX_FR_WRITER_BACK_VA,LXF_WRITER_STUB,16,"\x48\x8b\x74\x24\x28\x48\x89\xd9\x4c\x89\xe7\xe8\x5d\x44\xff\xff",""},
};
typedef uint64_t (*lxf_v_fn)(void *,unsigned char *,uint64_t,uint64_t,uint64_t,uint64_t,double);
static uint64_t lxf_v_hook(void *o20,unsigned char *buf,uint64_t a3,uint64_t a4,uint64_t a5,uint64_t a6,double time,float d) {
    const uint64_t r=((lxf_v_fn)(void*)LX(LX_FR_V_VA))(o20,buf,a3,a4,a5,a6,time);fr_v_seen(buf,d);return r;
}
static uint64_t lxf_mulps_hook(void *o20,void *data,void *a,void *slot) {fr_mulps_seen();return fr_writer_run(o20,data,a,slot);}
static void *lxf_detour_hook(unsigned j) {return j==0?(void*)lxf_v_hook:j==1?(void*)lxf_mulps_hook:(void*)fr_writer_run;}
static uint32_t fr_k_read(void) {
    const unsigned char *table=NULL;uint32_t k=0;
    if(!read_local(LX(LX_FR_K_TABLE_VA),&table,8) || !table || !read_local(table+0x318,&k,4)) return 0;
    return k;
}
/* The writer's layout on Linux: table [0x3db83c8] (+0x4fd0 scale, +0x318 K, +0x330 clamp), D +0x914..+0x924,
 * multipliers +0x998..+0x9a4 (0x1481050), virtuals +0x2078..+0x2098. */
static int fr_w_read(void *o20,const unsigned char *data,const unsigned char *a,fr_w_in *in) {
    return fr_w_gather(o20,data,a,LX(LX_FR_K_TABLE_VA),0x4fd0,0x318,0x330,0x914,0x998,0x2078,in);
}
static void lxf_detour_code(unsigned char *s,unsigned j) {
    const unsigned replay=lxf_detours[j].len-5,extra=(unsigned)strlen(lxf_detours[j].extra);
    unsigned n=0;int32_t d;void *hook=lxf_detour_hook(j),*back=LX(lxf_detours[j].back);
    memset(s,0xcc,0x40);
    memcpy(s,lxf_detours[j].orig,replay);n=replay;memcpy(s+n,lxf_detours[j].extra,extra);n+=extra;
    s[n++]=0xff;s[n++]=0x15;d=(int32_t)(0x30-(n+4));memcpy(s+n,&d,4);n+=4;
    s[n++]=0xff;s[n++]=0x25;d=(int32_t)(0x38-(n+4));memcpy(s+n,&d,4);
    memcpy(s+0x30,&hook,8);memcpy(s+0x38,&back,8);
}
static void lxf_detour_jump(unsigned j,unsigned char out[5]) {
    const int32_t d=(int32_t)((intptr_t)(thunk+lxf_detours[j].stub)-(intptr_t)(LX(lxf_detours[j].at)+5));out[0]=0xe9;memcpy(out+1,&d,4);
}
/* 0 original, 1 ours (the jump, the rest as it was), -1 anything else. */
static int lxf_detour_state(unsigned j) {
    const unsigned char *at=LX(lxf_detours[j].at);unsigned char jump[5];const unsigned len=lxf_detours[j].len;
    if(!memcmp(at,lxf_detours[j].orig,len)) return 0;
    if(thunk && !memcmp(at+5,lxf_detours[j].orig+5,len-5)) {lxf_detour_jump(j,jump);if(!memcmp(at,jump,5)) return 1;}
    return -1;
}
/* The first 5 bytes: one aligned 16-byte block (0x148d803: +3; 0x148d940: +0; 0x148cd53: +3). */
static int lxf_detour_swap(unsigned j,const unsigned char *expected,const unsigned char *value) {
    unsigned char *at=LX(lxf_detours[j].at),*block=(unsigned char*)((uintptr_t)at&~(uintptr_t)15);
    const unsigned offset=(unsigned)(at-block);
    unsigned __int128 current,desired;memcpy(&current,block,16);desired=current;
    if(offset+5>16 || memcmp((unsigned char*)&current+offset,expected,5)) return 0;
    memcpy((unsigned char*)&desired+offset,value,5);
    return gate_code_swap128(block,current,desired);
}
static unsigned lxf_done;
typedef uint64_t (*lxf_sp_fn)(void *,float *,float *,uint32_t,uint32_t,float *,float,float,float);
typedef struct {lxf_sp_fn fn;float *dir;uint32_t x,y;float *move;float r,a5,a9;} lxf_sp_args;
static lxf_sp_fn lxf_sp_original; /* our trampoline, or the step chain's (FR25) when its hook routes here */
static int lxf_sp_entry_done;
static void lxf_sp_native(const void *args,void *vec,float *p) {
    const lxf_sp_args *a=(const lxf_sp_args*)args;a->fn(vec,p,a->dir,a->x,a->y,a->move,a->r,a->a5,a->a9);
}
static uint64_t lxf_sp_hook(void *vec,float *p,float *dir,uint32_t x,uint32_t y,float *move,float r,float a5,float a9) {
    const lxf_sp_args a={lxf_sp_original,dir,x,y,move,r,a5,a9};
    fsp_run(lxf_sp_native,&a,vec,p,r,dir,a5,(int)(y&0xffu),move,a9,(x&0xffu)==0);
    return 0; /* the solver returns nothing its callers read */
}
/* The solver's entry line: 0 original, 1 our jump (the rest of the line as it was), -1 anything else. */
static void lxf_sp_lines(unsigned __int128 *original,unsigned __int128 *ours) {
    unsigned char *at=LX(LX_SC_SP_SOLVER_VA);memcpy(original,at,16);
    memcpy(original,lxc_sp_entry_original,LX_SC_SP_SOLVER_PROLOGUE);
    const int32_t d=(int32_t)((intptr_t)(thunk+LXF_SP_ENTRY)-(intptr_t)(at+5));unsigned char j[5]={0xe9};memcpy(j+1,&d,4);
    memcpy(ours,original,16);memcpy(ours,j,5);
}
static int lxf_sp_state(void) {
    unsigned __int128 o,w,now;memcpy(&now,LX(LX_SC_SP_SOLVER_VA),16);
    if(!thunk) return memcmp(&now,lxc_sp_entry_original,LX_SC_SP_SOLVER_PROLOGUE)?-1:0;
    lxf_sp_lines(&o,&w);
    if(!memcmp(&now,&o,16)) return 0;
    if(!memcmp(&now,&w,16)) return 1;
    return -1;
}
/* The refresh's entry: the whole refresh, its traversal included, between begin and end. */
static void fr_refresh_hook(void *owner,void *slot,uint64_t flag) {
    InterlockedIncrement64(&fr_calls);
    fr_ctx *c=fr_begin(slot);
    fr_native_refresh(owner,slot,flag);
    if(c) fr_end(c);
}
#include "gate_fp_attack.h"
#include "gate_fp_drones.h" /* FR30 */
static void *lxf_stub_target(unsigned i) {
    return i==0?(void*)fr_g4_hook:i==1?(void*)fr_g0_hook:i==2?(void*)fr_m4_hook:i==3?(void*)fr_m0_hook:(void*)fr_params_hook;
}
static void lxf_thunk_write(unsigned char *page) {
    lxw_jump_write(page+LXF_HOOK,(void*)fr_refresh_hook);
    unsigned char *p=page+LXF_THUNK;memcpy(p,"\x41\x57\x41\x56\x41\x55",LX_FR_PROLOGUE); /* push r15; push r14; push r13 */
    lxw_jump_write(p+LX_FR_PROLOGUE,LX(LX_FR_REFRESH_VA)+LX_FR_PROLOGUE);
    for(unsigned i=0;i<LXF_SITES;i++) lxw_jump_write(page+LXF_STUBS+0x20u*i,lxf_stub_target(i));
    for(unsigned j=0;j<LXF_DETOURS;j++) lxf_detour_code(page+lxf_detours[j].stub,j);
    lxw_jump_write(page+LXF_SP_ENTRY,(void*)lxf_sp_hook); /* FR27 */
    memcpy(page+LXF_SP_TRAMP,lxc_sp_entry_original,LX_SC_SP_SOLVER_PROLOGUE);
    lxw_jump_write(page+LXF_SP_TRAMP+LX_SC_SP_SOLVER_PROLOGUE,(void*)(LX(LX_SC_SP_SOLVER_VA)+LX_SC_SP_SOLVER_PROLOGUE));
    fa_thunk_write(page); /* FR29b */
    fdr_thunk_write(page); /* FR30 */
}
static unsigned char *lxf_site(unsigned i) {return LX(lxf_sites[i].site);}
static unsigned char *lxf_stub(unsigned i) {return thunk+LXF_STUBS+0x20u*i;}
/* 0 original, 1 ours, -1 anything else. */
static int lxf_site_state(unsigned i) {
    const unsigned char *site=lxf_site(i);int32_t d;
    if(site[0]!=0xe8) return -1;
    memcpy(&d,site+1,4);
    if(site+5+d==LX(lxf_sites[i].target)) return 0;
    if(thunk && site+5+d==lxf_stub(i)) return 1;
    return -1;
}
/* The rel32 of a call: one aligned 8-byte word (checked offline for every site). */
static int lxf_swap(unsigned i,const void *expected,const void *value) {
    unsigned char *site=lxf_site(i),*word=(unsigned char*)((uintptr_t)(site+1)&~(uintptr_t)7);
    const unsigned at=(unsigned)(site+1-word);
    const int64_t e=(intptr_t)expected-(intptr_t)(site+5),v=(intptr_t)value-(intptr_t)(site+5);
    if(at>4 || e<INT32_MIN || e>INT32_MAX || v<INT32_MIN || v>INT32_MAX) return 0;
    const int32_t e32=(int32_t)e,v32=(int32_t)v;int32_t now;
    uint64_t before=rd64(word),after=before;memcpy(&now,(unsigned char*)&before+at,4);
    if(now!=e32) return 0;
    memcpy((unsigned char*)&after+at,&v32,4);
    return gate_code_swap64(word,before,after);
}
/* FR27 first: the solver back to native (our entry line, or the step chain's route). */
static void fsp_restore(void) {
    if(!fsp_installed) return;
    if(fsp_route) {lxc_sp_route=NULL;fsp_route=0;fsp_installed=0;fsp_mode=FSP_OFF;fsp_stage="restored";return;}
    if(lxf_sp_entry_done && lxf_sp_state()==1) {
        unsigned __int128 o,w;lxf_sp_lines(&o,&w);
        if(gate_code_swap128(LX(LX_SC_SP_SOLVER_VA),w,o)) {lxf_sp_entry_done=0;fsp_installed=0;fsp_mode=FSP_OFF;fsp_stage="restored";}
    }
}
static void fr_restore(void) {
    if(!fr_installed) return;
    fdr_restore(); /* FR30 first: the drone factor sites back to their instructions */
    fa_restore(); /* FR29b: its call sites back to the callees */
    fsp_restore();
    /* The entry first: no new refresh enters our hooks while the inner sites go back. */
    if(lxf_done&LXF_ENTRY_DONE && gate_code_swap64(LX(LX_FR_REFRESH_VA),lxf_entry_patched,LX_FR_ENTRY_ORIGINAL)) lxf_done&=~LXF_ENTRY_DONE;
    for(unsigned j=0;j<LXF_DETOURS;j++) {
        unsigned char jump[5];
        if(!(lxf_done&(LXF_DETOUR_DONE<<j)) || lxf_detour_state(j)!=1) continue;
        lxf_detour_jump(j,jump);if(lxf_detour_swap(j,jump,(const unsigned char*)lxf_detours[j].orig)) lxf_done&=~(LXF_DETOUR_DONE<<j);
    }
    for(unsigned i=0;i<LXF_SITES;i++)
        if((lxf_done>>i)&1u && lxf_site_state(i)==1 && lxf_swap(i,lxf_stub(i),LX(lxf_sites[i].target))) lxf_done&=~(1u<<i);
    if(!lxf_done && !lxf_sp_entry_done && !fa_done && !fdr_done) {fr_installed=0;fr_restored=1;fr_mode=FR_OFF;}
}
/* Before the snapshot load and the first admitted frame, on the MoveWorker; a rerun with the same mode is accepted. */
static int fr_arm(int mode,uint64_t ticks) {
    if(fr_installed) return stats.active && fr_mode==mode && fr_ticks==ticks;
    if(!fr_arm_ready(mode,ticks)) return 0;
    if(!thunk) {fr_stage="page";return 0;}
    if(rd64(LX(LX_FR_REFRESH_VA))!=LX_FR_ENTRY_ORIGINAL) {fr_stage="pins";return 0;}
    for(unsigned i=0;i<LXF_SITES;i++) if(lxf_site_state(i)!=0) {fr_stage="pins";return 0;}
    for(unsigned j=0;j<LXF_DETOURS;j++) if(lxf_detour_state(j)!=0) {fr_stage="pins";return 0;}
    fr_native_refresh=(fr_refresh_fn)(void*)(thunk+LXF_THUNK);
    fr_native_g4=(fr_getter_fn)(void*)LX(LX_FR_G4_VA);fr_native_g0=(fr_getter_fn)(void*)LX(LX_FR_G0_VA);
    fr_native_m=(fr_m_fn)(void*)LX(LX_FR_M_VA);fr_native_params=(fr_params_fn)(void*)LX(LX_FR_P_VA);
    fr_native_writer=(fr_writer_fn)(void*)LX(LX_FR_WRITER_VA);
    fr_mode=mode;fr_installed=1;fr_restored=0;lxf_done=0;
    /* The inner sites first, then the entry: a refresh entered through our hook finds them in place. */
    for(unsigned i=0;i<LXF_SITES;i++) {
        if(!lxf_swap(i,LX(lxf_sites[i].target),lxf_stub(i))) {fr_stage="cas";fr_restore();fr_installed=0;fr_mode=FR_OFF;return 0;}
        lxf_done|=1u<<i;
    }
    for(unsigned j=0;j<LXF_DETOURS;j++) {
        unsigned char jump[5];lxf_detour_jump(j,jump);
        if(!lxf_detour_swap(j,(const unsigned char*)lxf_detours[j].orig,jump)) {fr_stage="cas";fr_restore();fr_installed=0;fr_mode=FR_OFF;return 0;}
        lxf_done|=LXF_DETOUR_DONE<<j;
    }
    if(!lxw_patch(LX(LX_FR_REFRESH_VA),LX_FR_ENTRY_ORIGINAL,LXF_HOOK,&lxf_entry_patched)) {fr_stage="cas";fr_restore();fr_installed=0;fr_mode=FR_OFF;return 0;}
    lxf_done|=LXF_ENTRY_DONE;
    fr_stage="armed";return 1;
}
/* FR27: gate.fp_redirect_solver(mode), after fp_redirect. With the step chain armed (its FR25 hook on the entry line)
 * the call is routed through that hook; else our own entry line. */
static int fsp_arm(int mode) {
    if(fsp_installed) return fsp_mode==mode;
    if(!fsp_arm_ready(mode)) return 0;
    if(!thunk) {fsp_stage="page";return 0;}
    if(lxc_sp_entry_done && lxc_sp_entry_state()==1) {
        lxf_sp_original=(lxf_sp_fn)(void*)(thunk+LXC_SP_TRAMP);fsp_mode=mode;fsp_route=1;fsp_installed=1;
        lxc_sp_route=lxf_sp_hook;fsp_stage="armed";return 1;
    }
    if(lxf_sp_state()!=0 || ((uintptr_t)LX(LX_SC_SP_SOLVER_VA)&15)) {fsp_stage="pins";return 0;}
    lxf_sp_original=(lxf_sp_fn)(void*)(thunk+LXF_SP_TRAMP);fsp_mode=mode;fsp_route=0;
    unsigned __int128 o,w;lxf_sp_lines(&o,&w);
    if(!gate_code_swap128(LX(LX_SC_SP_SOLVER_VA),o,w)) {fsp_mode=FSP_OFF;fsp_stage="cas";return 0;}
    lxf_sp_entry_done=1;fsp_installed=1;fsp_stage="armed";return 1;
}
/* The thunk page is r-x for good; the entry and the sites must still lead there. */
static int fr_owned(void) {
    if(lxf_done&LXF_ENTRY_DONE && rd64(LX(LX_FR_REFRESH_VA))!=lxf_entry_patched) return 0;
    for(unsigned i=0;i<LXF_SITES;i++) if((lxf_done>>i)&1u && lxf_site_state(i)!=1) return 0;
    for(unsigned j=0;j<LXF_DETOURS;j++) if(lxf_done&(LXF_DETOUR_DONE<<j) && lxf_detour_state(j)!=1) return 0;
    if(lxf_sp_entry_done && lxf_sp_state()!=1) return 0;
    if(fsp_route && (lxc_sp_route!=lxf_sp_hook || lxc_sp_entry_state()!=1)) return 0;
    return fa_owned() && fdr_owned(); /* FR29b, FR30 */
}
static int fr_check(void) {
    if(InterlockedCompareExchange64(&fr_failures,0,0)) return 0;
    if(!fr_owned()) {fr_stage="site_changed";InterlockedIncrement64(&fr_failures);return 0;}
    return 1;
}
#include "lockstep_fp_redirect_lua.h"
