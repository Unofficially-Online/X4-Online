/* The Linear step trace on the Linux X4 9.00 executable (lockstep_step_trace.h, Option B evidence E2): the
 * vtable entry 0x3b62558 holds LinearFlightControlModel's step 0x13ed050 (Windows 0xeb27c0 in 0x2c4f680). The
 * model's agent is at +0x8, the agent's ship at +0xf10. The entry lies in RELRO: one aligned 8-byte word,
 * swapped with the page writable for the moment. Diagnostic, read-only, default off; no stop reason.
 *
 * FR8 (native_gate_step_chain): the step's attitude block, compiled with the approach inlined (the pitch's without
 * the wrap, as Windows' period 0): asinf@plt 0x6579c0 of the target pitch at 0x13edc39 (input min(1, max(-1, T.y)),
 * T normalised just above) and of the current pitch at 0x13edcc5, both results kept at [rsp+0x130] / [rsp+0x100]
 * for the approach (0x13edde3..0x13ede19, rate [r12+0x37c] times dt [rsp+0x80]); the builder 0x1e02200 at
 * 0x13edecb (rdi r15+0x10, xmm0 pitch, xmm1 yaw, xmm2 roll; it takes nothing in rsi/rdx). Each call's rel32 is
 * swapped in its 16-byte line to a stub on the thunk page: asinf's jump to their hooks, the builder's passes the
 * caller's rsp (rsi) and r12 (rdx) first, so the row has the step's dt and the agent's three rates. The file pin
 * covers every byte; the sites must call their targets. */
#define ST_SLOT (LX_ST_SLOT_VA-LX_IMAGE_BASE)
#define ST_STEP (LX_ST_STEP_VA-LX_IMAGE_BASE)
#define ST_MODEL_AGENT 0x8
#define ST_AGENT_OWNER LXW_AGENT_OWNER
#define ST_AGENT_POSE LXW_AGENT_SLOTS /* the two pathing state banks (Windows +0xad0) */
#define ST_BANK_READ LX(LX_BANK_READ_VA)
#define ST_BANK_WRITE LX(LX_BANK_WRITE_VA)
#define ST_CHAIN_SITE_COUNT 22u /* FR8's three, FR17's two, FR18's four, FR20's two, FR21's two, FR24's four, FR25's five */
#define ST_SP_AI_VALUE 0xa0 /* FR25: a context Value's payload is the context plus this (Windows 0xa8) */
#define ST_SP_LAYOUT 1u
#define ST_OC_LAYOUT 1u /* FR24: a path point's reference at +0x8 */
#define ST_PT_REF 0x8 /* FR18: a path point's reference object runtime id (+0x10 its sub id, +0x20 the offset mat4; Windows +0x10) */
#define ST_HIER LX(LX_SC_HIER_VA) /* FR20: the level probes' hierarchy function (the file pin covers it) */
#define ST_SG_SLOT (LX_SG_SLOT_VA-LX_IMAGE_BASE) /* FR19: SetTarget's slot (RELRO, swapped like the step's) */
#define ST_SG_FN (LX_SG_FN_VA-LX_IMAGE_BASE)
#define ST_MODEL_TARGET 0x10 /* Windows 0x20 */
#define ST_AGENT_POINT 0x6d0 /* the path points' begin (Windows [agent+0x6a8]) */
#define ST_HW_SLOT (LX_HW_SLOT_VA-LX_IMAGE_BASE) /* FR21: the Highway's point map (RELRO, swapped like SetTarget's) */
#define ST_HW_FN (LX_HW_FN_VA-LX_IMAGE_BASE)
#define ST_HW_SPLINE 0x5e8 /* Windows 0x608 */
#define ST_MODEL_SPLINE 0xd0 /* FR9: the path spline (vtable 0x3b30558), built by 0x1151760 at 0x13ed3e3 (Windows +0xe0) */
#include "lockstep_step_trace.h"
static int st_slot_swap(void *expected,void *value) {
    return gate_data_swap64(LX(LX_ST_SLOT_VA),(uint64_t)(uintptr_t)expected,(uint64_t)(uintptr_t)value);
}
static int st_hw_swap(void *expected,void *value) {
    return gate_data_swap64(LX(LX_HW_SLOT_VA),(uint64_t)(uintptr_t)expected,(uint64_t)(uintptr_t)value);
}
static int st_sg_swap(void *expected,void *value) {
    return gate_data_swap64(LX(LX_SG_SLOT_VA),(uint64_t)(uintptr_t)expected,(uint64_t)(uintptr_t)value);
}
/* twenty-two stubs 0x20 apart: asinf target, asinf current, the builder's, FR17's two, FR18's four, FR20's two, FR21's
 * two, FR24's four, FR25's five (to 0xcc0); then FR25's solver entry stub (0xcc0) and its trampoline (0xce0) */
#define LXC_STUBS 0xa00u
static const struct {uint32_t site,target;const char *name;} lxc_sites[ST_CHAIN_SITE_COUNT]={
    {LX_SC_ASIN_TARGET_SITE_VA,LX_SC_ASINF_VA,"asin_target"},{LX_SC_ASIN_CUR_SITE_VA,LX_SC_ASINF_VA,"asin_cur"},
    {LX_SC_BUILDER_SITE_VA,LX_SC_BUILDER_VA,"builder"},
    {LX_SC_FM_AGENT_SITE_VA,LX_SC_FM_VA,"fm_agent"},{LX_SC_FM_OTHER_SITE_VA,LX_SC_FM_VA,"fm_other"},
    {LX_SC_PT_LOCAL_SITE_VA,LX_SC_PT_LOCAL_VA,"pt"},{LX_SC_PT_CONVERT_A_SITE_VA,LX_SC_PT_CONVERT_VA,"pt_a"},
    {LX_SC_PT_CONVERT_B_SITE_VA,LX_SC_PT_CONVERT_VA,"pt_b"},{LX_SC_PT_MATH_SITE_VA,LX_SC_PT_MATH_VA,"pt_conv"},
    {LX_SC_PT_CONVERT_C_SITE_VA,LX_SC_PT_CONVERT_VA,"pt_c"},{LX_SC_PT_OBJECT_SITE_VA,LX_SC_HIER_VA,"pt_obj"},
    {LX_SC_HW_BANK_SITE_VA,LX_SC_HW_BANK_VA,"hw_bank"},{LX_SC_HW_BANK2_SITE_VA,LX_SC_HW_BANK_VA,"hw_bank2"},
    {LX_SC_OC1_SITE_VA,LX_SC_OC_VA,"oc_1"},{LX_SC_OC_FM_SITE_VA,LX_SC_OC_VA,"oc_fm"},{LX_SC_OC3_SITE_VA,LX_SC_OC_VA,"oc_3"},
    {LX_SC_OC4_SITE_VA,LX_SC_OC_VA,"oc_4"},
    {LX_SC_SP_CV_SITE_VA,LX_SC_SETVAR_VA,"sp_cv"},{LX_SC_SP_GV_SITE_VA,LX_SC_SETVAR_VA,"sp_gv"},{LX_SC_SP_TR_SITE_VA,LX_SC_SP_TR_VA,"sp_tr"},
    {LX_SC_SP_GZ_SITE_VA,LX_SC_SP_ZONE_VA,"sp_gz"},{LX_SC_SP_GS_SITE_VA,LX_SC_SP_SECTOR_VA,"sp_gs"},
};
#define LXC_SP_CV 17u
#define LXC_SP_TR 19u
#define LXC_SP_GZ 20u
#define LXC_SP_ENTRY (LXC_STUBS+0x20u*ST_CHAIN_SITE_COUNT) /* 0xcc0 */
#define LXC_SP_TRAMP (LXC_SP_ENTRY+0x20u)
static const unsigned char lxc_sp_entry_original[LX_SC_SP_SOLVER_PROLOGUE]={0x48,0xb8,0xb7,0x6d,0xdb,0xb6,0x6d,0xdb,0xb6,0x6d}; /* movabs rax,0x6db6db6db6db6db7 */
static unsigned lxc_sp_entry_done;
static uintptr_t lxc_sp_gs_ret[2];
static const char *st_chain_site_name(unsigned i) {return i<ST_CHAIN_SITE_COUNT?lxc_sites[i].name:"?";}
static unsigned lxc_done;
typedef float (*lxc_asin_fn)(float);
typedef void (*lxc_builder_fn)(void *,float,float,float);
static float lxc_asin_target(float x) {const float y=((lxc_asin_fn)(void*)LX(LX_SC_ASINF_VA))(x);st_chain_call(0,4,x,0.0f,0.0f,0.0f,0.0f,y);return y;}
static float lxc_asin_cur(float x) {const float y=((lxc_asin_fn)(void*)LX(LX_SC_ASINF_VA))(x);st_chain_call(1,3,x,0.0f,0.0f,0.0f,0.0f,y);return y;}
/* From the builder's stub: rdi rows, rsi the step's rsp at the call, rdx its r12 (the agent), xmm0..2 pitch, yaw, roll. */
static void lxc_builder(void *rows,const unsigned char *frame,const unsigned char *agent,float pitch,float yaw,float roll) {
    ((lxc_builder_fn)(void*)LX(LX_SC_BUILDER_VA))(rows,pitch,yaw,roll);
    st_chain_builder(2,(const unsigned char*)rows,yaw,pitch,roll,frame+0x80,agent?agent+0x378:NULL);
}
/* FR17: GetTargetPositionForWingMan (rdi formation, rsi out, rdx index, xmm0 the double time; the same signature). */
static uint64_t lxc_fm_agent(void *f,void *out,uint64_t index,double time) {return st_fm_call(3,0,LX(LX_SC_FM_VA),f,out,index,time);}
static uint64_t lxc_fm_other(void *f,void *out,uint64_t index,double time) {return st_fm_call(4,1,LX(LX_SC_FM_VA),f,out,index,time);}
/* FR18: the three converter calls, their stubs passing the agent (rbx) in rcx; the conversion inside 0x141fa50. */
typedef void *(*lxc_pt_local_fn)(void *,void *);
typedef void *(*lxc_pt_convert_fn)(void *,void *,void *);
typedef uint64_t (*lxc_pt_math_fn)(void *,void *,void *,void *,uint64_t);
static void *lxc_pt(unsigned site,unsigned which,void *out,void *point,void *frame,void *agent) {
    st_pt_ctx c;const int on=st_pt_begin(&c,site,which,agent,point,frame,NULL,NULL);
    void *result=which?((lxc_pt_convert_fn)(void*)LX(LX_SC_PT_CONVERT_VA))(out,point,frame):((lxc_pt_local_fn)(void*)LX(LX_SC_PT_LOCAL_VA))(out,point);
    if(on) st_pt_end(&c,out,1);
    return result;
}
static void *lxc_pt_local(void *out,void *point,void *unused,void *agent) {(void)unused;return lxc_pt(5,0,out,point,NULL,agent);}
static void *lxc_pt_a(void *out,void *point,void *frame,void *agent) {return lxc_pt(6,1,out,point,frame,agent);}
static void *lxc_pt_b(void *out,void *point,void *frame,void *agent) {return lxc_pt(7,2,out,point,frame,agent);}
/* FR20: 0x143235b (as pt_a/pt_b) and the object path 0x1432611 (its ecx 0 replaced by the agent; 0 passed on). */
static void *lxc_pt_c(void *out,void *point,void *frame,void *agent) {return lxc_pt(9,3,out,point,frame,agent);}
static void lxc_pt_obj(void *object,void *mat,void *frame,void *agent) {st_pt_object_call(10,4,object,mat,frame,agent);}
static uint64_t lxc_pt_conv(void *out,void *velocity,void *ref,void *frame,uint64_t flag) {
    uint32_t *e;st_pt_conversion_begin(8,out,ref,frame,&e);
    const uint64_t result=((lxc_pt_math_fn)(void*)LX(LX_SC_PT_MATH_VA))(out,velocity,ref,frame,flag);
    st_pt_conversion_end(e,out);
    return result;
}
/* FR21: the banking (rdi the highway, xmm0 t; the same signature). */
static float lxc_hw_bank(void *hw,float t) {return st_hw_banking_call(11,LX(LX_SC_HW_BANK_VA),hw,t);}
static float lxc_hw_bank2(void *hw,float t) {return st_hw_banking_call(12,LX(LX_SC_HW_BANK_VA),hw,t);}
/* FR24: the queue at its four calls (the original once, then the entry for a watched ship); the propagation's stub
 * passes its frame (rbp) and the leader's pending point (r13). */
typedef uint64_t (*lxc_oc_fn)(void *,uint64_t,void *,uint64_t);
static uint64_t lxc_oc(unsigned site,unsigned which,void *manager,uint64_t ship,void *point,uint64_t issuer,const unsigned char *frame,const unsigned char *leader) {
    InterlockedIncrement64(&st_chain_hits[site]);
    const uint64_t r=((lxc_oc_fn)(void*)LX(LX_SC_OC_VA))(manager,ship,point,issuer);
    uint64_t key;const int w=st_oc_watched(ship,&key);
    if(w>=0) st_oc_note(w,key,which,(const unsigned char*)point,issuer,frame?frame-0x140:NULL,leader,NULL,lxc_sites[site].site);
    return r;
}
static uint64_t lxc_oc1(void *m,uint64_t s,void *p,uint64_t i) {return lxc_oc(13,0,m,s,p,i,NULL,NULL);}
static uint64_t lxc_oc_fm(void *m,uint64_t s,void *p,uint64_t i,const unsigned char *frame,const unsigned char *leader) {return lxc_oc(14,1,m,s,p,i,frame,leader);}
static uint64_t lxc_oc3(void *m,uint64_t s,void *p,uint64_t i) {return lxc_oc(15,2,m,s,p,i,NULL,NULL);}
static uint64_t lxc_oc4(void *m,uint64_t s,void *p,uint64_t i) {return lxc_oc(16,3,m,s,p,i,NULL,NULL);}
/* FR25: the results as the actions store them (the original once), the transform (its stub passes the context, rbp of
 * 0xd04a50, in r8), the solver through its entry jump (the original through the trampoline). */
typedef uint64_t (*lxc_setvar_fn)(void *,void *,void *,void *);
static uint64_t lxc_sp_set(unsigned site,unsigned which,void *var,void *ctx,void *name,void *value) {
    InterlockedIncrement64(&st_chain_hits[site]);
    st_sp_value(which,(const unsigned char*)ctx,(const unsigned char*)value);
    return ((lxc_setvar_fn)(void*)LX(LX_SC_SETVAR_VA))(var,ctx,name,value);
}
static uint64_t lxc_sp_cv(void *a,void *b,void *c,void *d) {return lxc_sp_set(LXC_SP_CV,0,a,b,c,d);}
static uint64_t lxc_sp_gv(void *a,void *b,void *c,void *d) {return lxc_sp_set(LXC_SP_CV+1,1,a,b,c,d);}
typedef void (*lxc_tr_fn)(void *,void *,void *,uint32_t,double);
static void lxc_sp_tr(void *self,void *mat,void *anc,uint32_t flag,const unsigned char *ctx,double time) {
    InterlockedIncrement64(&st_chain_hits[LXC_SP_TR]);
    st_sp_tr_ctx c;const int on=st_sp_tr_begin(&c,ctx,self,mat,anc,time,flag&0xff);
    ((lxc_tr_fn)(void*)LX(LX_SC_SP_TR_VA))(self,mat,anc,flag,time);
    if(on) st_sp_tr_end(&c,mat,self);
}
typedef uint64_t (*lxc_solver_fn)(void *,float *,float *,uint32_t,uint32_t,float *,float,float,float);
/* FR27 (gate_fp_redirect.h fsp_arm): while the fp redirect's solver is armed and this hook owns the entry, the call goes
 * there (which runs native through this module's trampoline as it needs); NULL: native. */
static lxc_solver_fn volatile lxc_sp_route;
static uint64_t lxc_sp_solver(void *boxes,float *p,float *dir,uint32_t xflag,uint32_t yflag,float *move,float radius,float a5,float a9) {
    st_sp_cap_ctx c;
    const int on=st_sp_cap_begin(&c,lxc_sp_gs_ret,2,boxes,p,dir,move,st_bits(radius),st_bits(a5),yflag,xflag,st_bits(a9),0,
                                 (uintptr_t)__builtin_return_address(0));
    const lxc_solver_fn route=lxc_sp_route;
    const uint64_t r=(route?route:(lxc_solver_fn)(void*)(thunk+LXC_SP_TRAMP))(boxes,p,dir,xflag,yflag,move,radius,a5,a9);
    if(on) st_sp_cap_end(&c);
    return r;
}
static void lxc_thunk_write(unsigned char *page) {
    unsigned char *p=page+LXC_STUBS;
    lxw_jump_write(p,(void*)lxc_asin_target);lxw_jump_write(p+0x20,(void*)lxc_asin_cur);
    memcpy(p+0x40,"\x48\x8d\x74\x24\x08\x4c\x89\xe2",8); /* lea rsi,[rsp+8]; mov rdx,r12 */
    lxw_jump_write(p+0x48,(void*)lxc_builder);
    lxw_jump_write(p+0x60,(void*)lxc_fm_agent);lxw_jump_write(p+0x80,(void*)lxc_fm_other);
    void *const pt[3]={(void*)lxc_pt_local,(void*)lxc_pt_a,(void*)lxc_pt_b};
    for(unsigned i=0;i<3;i++) {memcpy(p+0xa0+0x20*i,"\x48\x89\xd9",3);lxw_jump_write(p+0xa3+0x20*i,pt[i]);} /* mov rcx,rbx */
    lxw_jump_write(p+0x100,(void*)lxc_pt_conv);
    void *const fr20[2]={(void*)lxc_pt_c,(void*)lxc_pt_obj};
    for(unsigned i=0;i<2;i++) {memcpy(p+0x120+0x20*i,"\x48\x89\xd9",3);lxw_jump_write(p+0x123+0x20*i,fr20[i]);} /* mov rcx,rbx */
    lxw_jump_write(p+0x160,(void*)lxc_hw_bank);lxw_jump_write(p+0x180,(void*)lxc_hw_bank2);
    lxw_jump_write(p+0x1a0,(void*)lxc_oc1);
    memcpy(p+0x1c0,"\x49\x89\xe8\x4d\x89\xe9",6);lxw_jump_write(p+0x1c6,(void*)lxc_oc_fm); /* mov r8,rbp; mov r9,r13 */
    lxw_jump_write(p+0x1e0,(void*)lxc_oc3);lxw_jump_write(p+0x200,(void*)lxc_oc4);
    lxw_jump_write(p+0x220,(void*)lxc_sp_cv);lxw_jump_write(p+0x240,(void*)lxc_sp_gv);
    memcpy(p+0x260,"\x49\x89\xe8",3);lxw_jump_write(p+0x263,(void*)lxc_sp_tr); /* mov r8,rbp */
    const void *gate=(const void*)&st_sp_gate_gs;void *const to[2]={(void*)LX(LX_SC_SP_ZONE_VA),(void*)LX(LX_SC_SP_SECTOR_VA)};
    for(unsigned i=0;i<2;i++) {                                   /* mov r11,&gate; mov [r11],r12; mov [r11+8],rsp */
        unsigned char *s=p+0x280+0x20*i;memcpy(s,"\x49\xbb",2);memcpy(s+2,&gate,8);memcpy(s+10,"\x4d\x89\x23\x49\x89\x63\x08",7);lxw_jump_write(s+17,to[i]);
    }
    lxw_jump_write(page+LXC_SP_ENTRY,(void*)lxc_sp_solver);
    memcpy(page+LXC_SP_TRAMP,lxc_sp_entry_original,LX_SC_SP_SOLVER_PROLOGUE);
    lxw_jump_write(page+LXC_SP_TRAMP+LX_SC_SP_SOLVER_PROLOGUE,(void*)(LX(LX_SC_SP_SOLVER_VA)+LX_SC_SP_SOLVER_PROLOGUE));
}
/* FR25: the solver's entry line: its first five bytes a jump to the entry stub when ours. 0 original, 1 ours, -1 other. */
static void lxc_sp_entry_lines(unsigned __int128 *original,unsigned __int128 *ours) {
    unsigned char *at=LX(LX_SC_SP_SOLVER_VA);memcpy(original,at,16);*ours=*original;
    memcpy(original,lxc_sp_entry_original,LX_SC_SP_SOLVER_PROLOGUE);
    const int32_t d=(int32_t)((intptr_t)(thunk+LXC_SP_ENTRY)-(intptr_t)(at+5));unsigned char j[5]={0xe9};memcpy(j+1,&d,4);
    memcpy(ours,original,16);memcpy(ours,j,5);
}
static int lxc_sp_entry_state(void) {
    unsigned __int128 o,w,now;memcpy(&now,LX(LX_SC_SP_SOLVER_VA),16);
    if(!thunk) return memcmp(&now,lxc_sp_entry_original,LX_SC_SP_SOLVER_PROLOGUE)?-1:0;
    lxc_sp_entry_lines(&o,&w);
    if(!memcmp(&now,&o,16)) return 0;
    if(!memcmp(&now,&w,16)) return 1;
    return -1;
}
static unsigned char *lxc_stub(unsigned i) {return thunk+LXC_STUBS+0x20u*i;}
/* 0 original, 1 ours, -1 anything else. */
static int lxc_site_state(unsigned i) {
    const unsigned char *site=LX(lxc_sites[i].site);int32_t d;
    if(site[0]!=0xe8) return -1;
    memcpy(&d,site+1,4);
    if(site+5+d==LX(lxc_sites[i].target)) return 0;
    if(thunk && site+5+d==lxc_stub(i)) return 1;
    return -1;
}
/* The rel32 in its aligned 16-byte line (checked offline for the three sites). */
static int lxc_swap(unsigned i,const void *expected,const void *value) {
    unsigned char *site=LX(lxc_sites[i].site),*line=(unsigned char*)((uintptr_t)(site+1)&~(uintptr_t)15);
    const unsigned at=(unsigned)(site+1-line);
    const int64_t e=(intptr_t)expected-(intptr_t)(site+5),v=(intptr_t)value-(intptr_t)(site+5);
    if(at>12 || e<INT32_MIN || e>INT32_MAX || v<INT32_MIN || v>INT32_MAX) return 0;
    const int32_t e32=(int32_t)e,v32=(int32_t)v;int32_t now;
    unsigned __int128 before,after;memcpy(&before,line,16);after=before;memcpy(&now,(unsigned char*)&before+at,4);
    if(now!=e32) return 0;
    memcpy((unsigned char*)&after+at,&v32,4);
    return gate_code_swap128(line,before,after);
}
static void st_chain_restore(void) {
    if(lxc_sp_entry_done && lxc_sp_entry_state()==1) {
        unsigned __int128 o,w;lxc_sp_entry_lines(&o,&w);
        if(gate_code_swap128(LX(LX_SC_SP_SOLVER_VA),w,o)) lxc_sp_entry_done=0;
    }
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++)
        if((lxc_done>>i)&1u && lxc_site_state(i)==1 && lxc_swap(i,lxc_stub(i),LX(lxc_sites[i].target))) lxc_done&=~(1u<<i);
    if(!lxc_done && !lxc_sp_entry_done) {st_chain_installed=0;st_chain_restored=1;st_chain_stage="restored";st_sp_gate_clear();}
}
static int st_chain_arm(void) {
    if(!thunk) {st_chain_stage="page";return 0;}
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) if(lxc_site_state(i)!=0) {st_chain_stage="pins";return 0;}
    if(lxc_sp_entry_state()!=0 || ((uintptr_t)LX(LX_SC_SP_SOLVER_VA)&15)) {st_chain_stage="pins";return 0;}
    lxc_sp_gs_ret[0]=(uintptr_t)LX(LX_SC_SP_GZ_SITE_VA+5);lxc_sp_gs_ret[1]=(uintptr_t)LX(LX_SC_SP_GS_SITE_VA+5);st_sp_gate_clear();
    st_chain_installed=1;st_chain_restored=0;lxc_done=0;
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) {
        if(!lxc_swap(i,LX(lxc_sites[i].target),lxc_stub(i))) {st_chain_restore();st_chain_installed=lxc_done!=0;st_chain_stage="cas";return 0;}
        lxc_done|=1u<<i;
    }
    {unsigned __int128 o,w;lxc_sp_entry_lines(&o,&w);
     if(!gate_code_swap128(LX(LX_SC_SP_SOLVER_VA),o,w)) {st_chain_restore();st_chain_installed=lxc_done!=0;st_chain_stage="cas";return 0;}
     lxc_sp_entry_done=1;}
    st_chain_stage="armed";return 1;
}
/* The thunk page is r-x for good; the sites must still lead there. */
static int st_chain_owned(void) {
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) if((lxc_done>>i)&1u && lxc_site_state(i)!=1) return 0;
    if(lxc_sp_entry_done && lxc_sp_entry_state()!=1) return 0;
    return 1;
}
static int lua_step_trace(lua_State *L) {
    size_t length=0;const char *text=p_lua_tolstring(L,1,&length);uint64_t id=0;int ok=text && length==16;
    for(size_t i=0;ok && i<16;i++) {
        const char c=text[i];ok=(c>='0' && c<='9') || (c>='a' && c<='f');
        id=id*16+(uint64_t)(c<='9'?c-'0':c-'a'+10);
    }
    p_lua_pushnumber(L,ok && st_watch(id));return 1;
}
static int lua_step_trace_arm(lua_State *L) {
    double ticks=p_lua_tonumber(L,1);
    p_lua_pushnumber(L,isfinite(ticks) && ticks>=1 && ticks<=600 && floor(ticks)==ticks && st_arm((uint64_t)ticks));return 1;
}
static int lua_step_trace_status(lua_State *L) {
    char out[1280];int n=accept_ui()?st_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_step_trace_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[1024];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=st_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* step_chain(): FR8's hooks with the step trace (native_gate_step_chain), before step_trace_arm. */
static int lua_step_chain(lua_State *L) {p_lua_pushnumber(L,st_chain_want());return 1;}
