/* FR8 on the Windows X4 9.00 executable (lockstep_step_trace.h, native_gate_step_chain; diagnostic, read-only,
 * default off). Included by lockstep_gate.c only. Linux twin: linux/gate_step_trace.h.
 *
 * The Linear step 0xeb27c0's attitude block, 0xeb379b..0xeb3917: the current pitch asinf(clamp(bank row2.y)) at
 * 0xeb37ff, the target pitch asinf(clamp(T.y)) at 0xeb3892 (asinf 0x1b43ce0, xmm0 in and out; portable math may
 * have moved its entry, which these calls reach as any other caller does), the approach helper 0xeb1db0 (rcx &angle,
 * xmm1 target, xmm2 rate, xmm3 dt, fifth argument the wrap period; nothing returned) for yaw 0xeb386f, pitch 0xeb38b5
 * (period xmm12 = 0.0: no wrap) and roll 0xeb38f2, and the rotation builder 0x34bff0 at 0xeb3912 (rcx the written
 * bank, xmm1 yaw, xmm2 pitch, xmm3 roll; rows at +0x10..+0x3f). After each of these calls the step uses only
 * callee-saved registers and memory, so a C hook with the same signature that calls the original and records stands
 * in for it. Each call's rel32 lies inside one 16-byte line; it is swapped to a stub (jmp [rip]) on a page within
 * rel32 reach. Pins: the block and the helper, as the file has them; while armed, hash_ok and read_local see the
 * original rel32 bytes (fp_patch_view), so other modules' pins over the step (the world trace's) keep passing. */
#define STC_BLOCK 0xeb379bu
#define STC_BLOCK_SIZE 0x17cu
#define STC_BLOCK_SHA "8b5ed48bfccb83af289c8355af8a4b322bf85952062b5ccd00543e32dca833aa"
#define STC_HELPER 0xeb1db0u
#define STC_HELPER_SIZE 0xafu
#define STC_HELPER_SHA "8ecad21ab6fae7289e6aee01ee801edab9d39753470e6ba98dd0ba7beaf744be"
#define STC_ASINF 0x1b43ce0u
#define STC_BUILDER 0x34bff0u
/* FR17: GetTargetPositionForWingMan (rcx formation: members' agents at +0x48/+0x50, shape +0x60; rdx out mat4; r8 the
 * member index; xmm3 the double time; returns al), called by the agent's wrapper 0xe8b990 (formation agent+0xf20, index
 * +0xf28) at 0xe8ba17 and by 0xe9a909 at 0xe9a977; pinned with the wrapper. */
#define STC_FM 0xe99d70u
#define STC_FM_SIZE 0xa95u
#define STC_FM_SHA "167ea3707c777eceb78653d1b7b4728b73e2c85b1a0513e956d8d6e95ba7435b"
#define STC_FM_WRAPPER 0xe8b990u
#define STC_FM_WRAPPER_SIZE 0xb8u
#define STC_FM_WRAPPER_SHA "a8f5feb887cc1e94b5f778f412f17ce1fdf853ad8d120650f0e6d8e226c18c6a"
/* FR18 (lockstep_step_trace.h): UpdatePathing 0xe8c380 converts the agent's first path point at 0xe8cf11: 0xe8be60(rcx
 * -, rdx out mat4, r8 the point [agent+0x6a8], r9 the frame object, [rsp+0x20] / [rsp+0x28] two more objects; returns
 * al), then hands out to SetTarget (0xe8cf52, [model+0x18]). 0xe8be60 stores rcx to its home slot and reuses that slot
 * as a dword local, never reading rcx, so the stub puts the agent (r15 of 0xe8c380, set once from its rcx) there; the
 * stack arguments stay where a jump leaves them. Inside, 0xe8bd20(out, point, frame) calls the conversion 0xe8a4a0(rcx
 * out, rdx velocity 0, r8 &ref, r9 &frame, [rsp+0x20] byte 1) at 0xe8bdeb. Pinned: 0xe8be60, 0xe8bd20 and the caller's
 * bytes 0xe8cef6..0xe8cf55 (the stack arguments, the point load, the call, the SetTarget call). */
#define STC_PT 0xe8be60u
#define STC_PT_SIZE 0x520u
#define STC_PT_SHA "4c0e585c1be84586d171aa40f14ccdc0724bcf3167dc0be62eac7150fc95300e"
#define STC_PT_CONVERT 0xe8bd20u
#define STC_PT_CONVERT_SIZE 0x140u
#define STC_PT_CONVERT_SHA "f7a2196cf7a462cca42f404a629036eabd7122d790fbe97d9105ebce2490ba43"
#define STC_PT_CALLER 0xe8cef6u
#define STC_PT_CALLER_SIZE 0x5fu
#define STC_PT_CALLER_SHA "9daa93d1e96ae8227328d551d2e94fa768ea96934b45ff0e7fb184dab4f00789"
#define STC_PT_MATH 0xe8a4a0u
#define STC_PT_SITE 8u /* its stub: mov rcx,r15 first */
/* FR20 (lockstep_step_trace.h): UpdatePathing's other conversions of the path point, the ones in front of SetTarget at
 * 0xe8d774 (FR19's caller 0xe8d777): 0xe8bd20(rcx out, rdx [r15+0x6a8], r8 the frame rbx; returns out) at 0xe8d502,
 * 0xe8d60f and 0xe8d6bc, and the object path 0x3dde10(rcx the reference object rsi, rdx the matrix, r8 the frame, r9 0
 * (xor r9d,r9d just before)) at 0xe8d5ea. Their stubs pass the agent r15 in r9 (free at the first three, always 0 at the
 * fourth, which the hook passes on). 0x3dde10 is also the hierarchy function the level probes call (ST_HIER). The path
 * builder 0xe86bb0(rcx agent, rdx description, r8 string, r9 out) at 0xeac498 (the PathingManager's 0xeac380).
 * Pinned: the window 0xe8d4e1..0xe8d6e0 (the four calls and their argument setup), 0x3dde10 and 0xe86bb0 (their
 * .pdata extents) and 0xeac470..0xeac49d. */
#define STC_PT_CALLER2 0xe8d4e1u
#define STC_PT_CALLER2_SIZE 0x1ffu
#define STC_PT_CALLER2_SHA "157aeede82a0a4db9300533e904b297d04b146b7be4ff361c882cffd911e67bc"
#define STC_HIER 0x3dde10u
#define STC_HIER_SIZE 0x811u
#define STC_HIER_SHA "3b757e4ac925248a081c1a675a3491b28a368289fb7434ced473c75dd073d0eb"
#define STC_PS 0xe86bb0u
#define STC_PS_SIZE 0x1140u
#define STC_PS_SHA "f9058c5168d815dbde0ae41dc46a62c378907fdd513a8f7c5cee58c711923ba0"
#define STC_PS_CALLER 0xeac470u
#define STC_PS_CALLER_SIZE 0x2du
#define STC_PS_CALLER_SHA "3288039193275ac5f7891794bc003f0d0a24f016f2c9d42ad3652cd78cd04acc"
#define STC_PS_SITE 14u
/* FR21 (lockstep_step_trace.h): the Highway's point map 0x5f6480 (pinned with its ret, ST_HW_SHA) calls the banking
 * 0x5f7520(rcx the highway, xmm1 t; xmm0 out) at 0x5f6643. The wingmen's points: 0xe89e40(rcx agent, rdx the point)
 * at 0xe95f7e (appends to the pending points +0x6c0), 0xe8afd0(rcx, rdx agent, r8 the point) at 0xe9acd0,
 * 0xe89ed0(rcx the vector, rdx the point) at 0xe8ef47 and 0xe89f10(rcx agent, dl) at seven sites; each hook passes four
 * registers and two stack words on (the targets take at most three). FR22: the vector 0xe8ef47 hands over is the
 * CURRENT points' (0xe8dfc0: rdi = lea [rsi+0x6a8], rsi the agent, [rbp-0x50]), not the pending ones', so FR21's
 * rcx-0x6c0 never found the agent (pc_vec 10869 calls, pc_kept 0): the agent is now taken per site (stc_pc_agent).
 * Pinned: 0x5f6480..0x5f6a12 and the four targets' entries. */
#define STC_HW_BANK 0x5f7520u
#define STC_HW_SITE 15u
static const struct {uint32_t rva,size;const char *sha;} stc_pc_pins[5]={
    {0x5f6480u,0x592u,"534a95378e34c68c279998a60d78a4f3ca5574ba8379787de3c22651f65396d6"},
    {0xe89e40u,0x8du,"4acf143b3200d1632cd984d9881f2a43a07d3d6786059c4dd0d8659821dbbc9e"},
    {0xe89ed0u,0x40u,"b7312b7b79cb534eb7027a6cc05ac040dcef8e828de0101bc9995c975390a39a"},
    {0xe89f10u,0x20u,"2db860614caf74423c8856c499d25f2b6b767a6f8f698a716be77dc384505437"},
    {0xe8afd0u,0x30u,"312848c6c4a9ec93f8ff492cb384ced664cbd75c5e02ad00c6839901e9b8ab44"},
};
static const struct {uint32_t site,target;const char *name;} stc_sites[ST_CHAIN_SITE_COUNT]={
    {0xeb37ff,STC_ASINF,"asin_cur"},{0xeb386f,STC_HELPER,"yaw"},{0xeb3892,STC_ASINF,"asin_target"},
    {0xeb38b5,STC_HELPER,"pitch"},{0xeb38f2,STC_HELPER,"roll"},{0xeb3912,STC_BUILDER,"builder"},
    {0xe8ba17,STC_FM,"fm_agent"},{0xe9a977,STC_FM,"fm_other"},
    {0xe8cf11,STC_PT,"pt"},{0xe8bdeb,STC_PT_MATH,"pt_conv"},
    {0xe8d502,STC_PT_CONVERT,"pt_w1"},{0xe8d60f,STC_PT_CONVERT,"pt_w2"},{0xe8d6bc,STC_PT_CONVERT,"pt_w3"},
    {0xe8d5ea,STC_HIER,"pt_obj"},{0xeac498,STC_PS,"pathset"},
    {0x5f6643,STC_HW_BANK,"hw_bank"},{0xe95f7e,0xe89e40,"pc_add"},{0xe9acd0,0xe8afd0,"pc_add2"},{0xe8ef47,0xe89ed0,"pc_vec"},
    {0xe8ddc4,0xe89f10,"pc_clr1"},{0xe8deac,0xe89f10,"pc_clr2"},{0xe8df92,0xe89f10,"pc_clr3"},{0xe8f60d,0xe89f10,"pc_clr4"},
    {0xe90152,0xe89f10,"pc_clr5"},{0xe9acbc,0xe89f10,"pc_clr6"},{0xeb79ae,0xe89f10,"pc_clr7"},
};
/* Stubs: the first eight 0x10 apart, FR18's and FR20's 0x20 apart from 0x80. */
#define STC_CODE_BYTES (0x80u+0x20u*(ST_CHAIN_SITE_COUNT-8u))
static unsigned stc_stub(unsigned i) {return i<8u?0x10u*i:0x80u+0x20u*(i-8u);}
static const char *st_chain_site_name(unsigned i) {return i<ST_CHAIN_SITE_COUNT?stc_sites[i].name:"?";}
static unsigned char *stc_page;
static unsigned stc_done;
typedef void (*stc_approach_fn)(float *,float,float,float,float);
typedef float (*stc_asin_fn)(float);
typedef void (*stc_builder_fn)(void *,float,float,float);
static void stc_approach(unsigned site,unsigned slot,float *angle,float target,float rate,float dt,float period) {
    const float in=*angle;
    ((stc_approach_fn)(void*)(image+STC_HELPER))(angle,target,rate,dt,period);
    st_chain_call(site,slot,in,target,rate,dt,period,*angle);
}
static void stc_yaw(float *a,float t,float r,float dt,float p) {stc_approach(1,0,a,t,r,dt,p);}
static void stc_pitch(float *a,float t,float r,float dt,float p) {stc_approach(3,1,a,t,r,dt,p);}
static void stc_roll(float *a,float t,float r,float dt,float p) {stc_approach(4,2,a,t,r,dt,p);}
static float stc_asin_cur(float x) {const float y=((stc_asin_fn)(void*)(image+STC_ASINF))(x);st_chain_call(0,3,x,0.0f,0.0f,0.0f,0.0f,y);return y;}
static float stc_asin_target(float x) {const float y=((stc_asin_fn)(void*)(image+STC_ASINF))(x);st_chain_call(2,4,x,0.0f,0.0f,0.0f,0.0f,y);return y;}
static void stc_builder(void *bank,float yaw,float pitch,float roll) {
    ((stc_builder_fn)(void*)(image+STC_BUILDER))(bank,yaw,pitch,roll);
    st_chain_builder(5,(const unsigned char*)bank+0x10,yaw,pitch,roll,NULL,NULL);
}
static uint64_t stc_fm_agent(void *f,void *out,uint64_t index,double time) {return st_fm_call(6,0,image+STC_FM,f,out,index,time);}
static uint64_t stc_fm_other(void *f,void *out,uint64_t index,double time) {return st_fm_call(7,1,image+STC_FM,f,out,index,time);}
/* FR18: the converter with the agent in rcx (from the stub), the original called with the same six arguments. */
typedef uint64_t (*stc_pt_fn)(void *,void *,void *,void *,void *,void *);
typedef uint64_t (*stc_pt_math_fn)(void *,void *,void *,void *,uint64_t);
static uint64_t stc_pt(void *agent,void *out,void *point,void *frame,void *a5,void *a6) {
    st_pt_ctx c;const int on=st_pt_begin(&c,8,0,agent,point,frame,a5,a6);
    const uint64_t result=((stc_pt_fn)(void*)(image+STC_PT))(agent,out,point,frame,a5,a6);
    if(on) st_pt_end(&c,out,(uint32_t)(result&0xff));
    return result;
}
static uint64_t stc_pt_conv(void *out,void *velocity,void *ref,void *frame,uint64_t flag) {
    uint32_t *e;st_pt_conversion_begin(9,out,ref,frame,&e);
    const uint64_t result=((stc_pt_math_fn)(void*)(image+STC_PT_MATH))(out,velocity,ref,frame,flag);
    st_pt_conversion_end(e,out);
    return result;
}
/* FR20: 0xe8bd20 with the agent in r9 (from the stub), the original called with its three arguments. */
typedef void *(*stc_convert_fn)(void *,void *,void *);
static void *stc_pt_frame(unsigned site,unsigned which,void *out,void *point,void *frame,void *agent) {
    st_pt_ctx c;const int on=st_pt_begin(&c,site,which,agent,point,frame,NULL,NULL);
    void *result=((stc_convert_fn)(void*)(image+STC_PT_CONVERT))(out,point,frame);
    if(on) st_pt_end(&c,out,1);
    return result;
}
static void *stc_pt_w1(void *out,void *point,void *frame,void *agent) {return stc_pt_frame(10,1,out,point,frame,agent);}
static void *stc_pt_w2(void *out,void *point,void *frame,void *agent) {return stc_pt_frame(11,2,out,point,frame,agent);}
static void *stc_pt_w3(void *out,void *point,void *frame,void *agent) {return stc_pt_frame(12,3,out,point,frame,agent);}
static void stc_pt_obj(void *object,void *mat,void *frame,void *agent) {st_pt_object_call(13,4,object,mat,frame,agent);}
/* FR20/FR21: the call stack from the hook into e[8..39] (return addresses less the image base). */
static void stc_stack(uint32_t *e) {
    void *frames[ST_PS_FRAMES];const unsigned n=RtlCaptureStackBackTrace(0,ST_PS_FRAMES,frames,NULL);e[7]=n;
    for(unsigned i=0;i<ST_PS_FRAMES;i++) {
        const uintptr_t f=i<n?(uintptr_t)frames[i]:0;
        e[8+i]=i>=n?0xffffffffu:f>=(uintptr_t)image && f-(uintptr_t)image<0x6fa9000u?(uint32_t)(f-(uintptr_t)image):0xfffffffeu;
    }
}
/* FR21: the banking, and the wingmen's point calls (the original once, same arguments and result). */
static float stc_hw_bank(void *hw,float t) {return st_hw_banking_call(STC_HW_SITE,image+STC_HW_BANK,hw,t);}
typedef uint64_t (*stc_any_fn)(void *,void *,void *,void *,void *,void *);
/* The agent of site which's call (1 pc_add: rcx; 2 pc_add2: rdx; 3 pc_vec: rcx = the agent's current points vector
 * +0x6a8; 4..10 pc_clr: rcx) and the point it adds (pc_add, pc_vec: rdx, 0x90 bytes; pc_add2: r8, 0x40), or NULL. */
static const unsigned char *stc_pc_agent(unsigned which,void *a1,void *a2,void *a3,const unsigned char **point,unsigned *point_size) {
    *point=NULL;*point_size=0;
    switch(which) {
    case 1: *point=a2;*point_size=0x90u;return a1;
    case 2: *point=a3;*point_size=0x40u;return a2;
    case 3: *point=a2;*point_size=0x90u;return (uintptr_t)a1>ST_AGENT_POINT?(const unsigned char*)a1-ST_AGENT_POINT:NULL;
    default: return a1;
    }
}
static uint64_t stc_point(unsigned site,unsigned which,void *a1,void *a2,void *a3,void *a4,void *a5,void *a6) {
    InterlockedIncrement64(&st_chain_hits[site]);
    const stc_any_fn native=(stc_any_fn)(void*)(image+stc_sites[site].target);
    const uint64_t tick=stats.issued;uint64_t key=0;int w=-1;const unsigned char *agent=NULL,*point=NULL;unsigned point_size=0;
    if(stats.active && boot_phase==3 && tick>=1 && tick<=st_ticks) {
        agent=stc_pc_agent(which,a1,a2,a3,&point,&point_size);
        if((uintptr_t)agent>0x10000u) w=st_agent_watched(agent,&key);
    }
    if(w<0) return native(a1,a2,a3,a4,a5,a6);
    uint32_t e[ST_PS_WORDS];memset(e,0,sizeof(e));
    e[0]=ST_PC_TAG;e[1]=(uint32_t)tick;e[2]=GetCurrentThreadId()==ui_thread?0:1;e[3]=which;memcpy(e+4,&key,8);
    stc_stack(e);
    const unsigned char *pending=NULL,*end=NULL,*begin=NULL;
    if(!read_local(agent+0x6c0,e+40,24)) memset(e+40,0xff,24);
    if(!read_local(agent+ST_AGENT_POINT,e+46,24)) memset(e+46,0xff,24);
    if(!read_local(agent+0x6c0,&pending,8) || !pending || !read_local(pending,e+52,0x90)) memset(e+52,0xff,0x90);
    if(point_size && (!point || !read_local(point,e+88,point_size))) memset(e+88,0xff,point_size);
    const unsigned char *before=st_ps_point(agent,e+168);
    const uint64_t result=native(a1,a2,a3,a4,a5,a6);
    const unsigned char *after=st_ps_point(agent,e+200);
    e[232]=before!=after;
    /* FR22: the current points' header after the call and their last point (0x90 bytes; 0xff when none). */
    if(!read_local(agent+ST_AGENT_POINT,e+234,24)) memset(e+234,0xff,24);
    if(!read_local(agent+ST_AGENT_POINT,&begin,8) || !read_local(agent+ST_AGENT_POINT+8,&end,8) || !begin || end<begin+0x90 ||
       !read_local(end-0x90,e+124,0x90)) memset(e+124,0xff,0x90);
    st_ps_publish(w,e);InterlockedIncrement64(&st_pc_kept);
    return result;
}
#define STC_PC(i,which) static uint64_t stc_pc##i(void *a,void *b,void *c,void *d,void *e,void *f) {return stc_point(i,which,a,b,c,d,e,f);}
STC_PC(16,1) STC_PC(17,2) STC_PC(18,3) STC_PC(19,4) STC_PC(20,5) STC_PC(21,6) STC_PC(22,7) STC_PC(23,8) STC_PC(24,9) STC_PC(25,10)
/* FR20: the path builder; for a watched agent the call stack, the description and the first point around it. */
typedef uint64_t (*stc_ps_fn)(void *,void *,void *,void *);
static uint64_t stc_pathset(void *agent,void *desc,void *a3,void *a4) {
    InterlockedIncrement64(&st_chain_hits[STC_PS_SITE]);
    const stc_ps_fn native=(stc_ps_fn)(void*)(image+STC_PS);
    const uint64_t tick=stats.issued;uint64_t key=0;int w=-1;
    if(!stats.active || boot_phase!=3 || tick<1 || tick>st_ticks || (w=st_agent_watched(agent,&key))<0) return native(agent,desc,a3,a4);
    uint32_t e[ST_PS_WORDS];memset(e,0,sizeof(e));
    e[0]=ST_PS_TAG;e[1]=(uint32_t)tick;e[2]=GetCurrentThreadId()==ui_thread?0:1;memcpy(e+4,&key,8);
    stc_stack(e);
    if(!desc || !read_local(desc,e+40,0x200)) memset(e+40,0xff,0x200);
    const unsigned char *before=st_ps_point(agent,e+168);
    const uint64_t result=native(agent,desc,a3,a4);
    const unsigned char *after=st_ps_point(agent,e+200);
    e[3]=(uint32_t)result;e[232]=before!=after;
    st_ps_publish(w,e);
    return result;
}
static void *stc_hook(unsigned i) {
    switch(i) {
    case 0: return (void*)stc_asin_cur;
    case 1: return (void*)stc_yaw;
    case 2: return (void*)stc_asin_target;
    case 3: return (void*)stc_pitch;
    case 4: return (void*)stc_roll;
    case 5: return (void*)stc_builder;
    case 6: return (void*)stc_fm_agent;
    case 7: return (void*)stc_fm_other;
    case 8: return (void*)stc_pt;
    case 9: return (void*)stc_pt_conv;
    case 10: return (void*)stc_pt_w1;
    case 11: return (void*)stc_pt_w2;
    case 12: return (void*)stc_pt_w3;
    case 13: return (void*)stc_pt_obj;
    case 14: return (void*)stc_pathset;
    case 15: return (void*)stc_hw_bank;
    case 16: return (void*)stc_pc16;
    case 17: return (void*)stc_pc17;
    case 18: return (void*)stc_pc18;
    case 19: return (void*)stc_pc19;
    case 20: return (void*)stc_pc20;
    case 21: return (void*)stc_pc21;
    case 22: return (void*)stc_pc22;
    case 23: return (void*)stc_pc23;
    case 24: return (void*)stc_pc24;
    default: return (void*)stc_pc25;
    }
}
/* Stub i at page+stc_stub(i): jmp [rip] -> its hook (FR18's converter: mov rcx,r15 first; FR20's four conversion
 * sites: mov r9,r15 first). */
static void stc_write_code(unsigned char *p) {
    memset(p,0xcc,STC_CODE_BYTES);
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) {
        static const unsigned char jump[6]={0xff,0x25,0,0,0,0},agent[3]={0x4c,0x89,0xf9},agent9[3]={0x4d,0x89,0xf9};void *hook=stc_hook(i);
        unsigned char *s=p+stc_stub(i);
        if(i==STC_PT_SITE) {memcpy(s,agent,3);s+=3;}
        else if(i>=10 && i<=13) {memcpy(s,agent9,3);s+=3;}
        memcpy(s,jump,6);memcpy(s+6,&hook,8);
    }
}
static int32_t stc_rel(unsigned i,const void *target) {return (int32_t)((intptr_t)target-(intptr_t)(image+stc_sites[i].site+5));}
/* 0 original, 1 ours, -1 anything else. Read raw: read_local would show the view. */
static int stc_site_state(unsigned i) {
    unsigned char now[5];int32_t d;
    memcpy(now,(const void*)(image+stc_sites[i].site),5);
    if(now[0]!=0xe8) return -1;
    memcpy(&d,now+1,4);
    if(d==stc_rel(i,image+stc_sites[i].target)) return 0;
    if(stc_page && d==stc_rel(i,stc_page+stc_stub(i))) return 1;
    return -1;
}
static unsigned char *stc_alloc_page(void) {
    uintptr_t at=(uintptr_t)(image+STC_BLOCK)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            intptr_t d=(intptr_t)p-(intptr_t)(image+STC_BLOCK);
            if(d>INT32_MIN+0x10000 && d<INT32_MAX-0x10000) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static int stc_site_cas(unsigned i,const void *expected,const void *value) {
    const int32_t e=stc_rel(i,expected),v=stc_rel(i,value);unsigned char eb[4],vb[4];memcpy(eb,&e,4);memcpy(vb,&v,4);
    return view_cas_bytes(stc_sites[i].site+1,eb,vb,4);
}
/* The view for other modules' pins: the original rel32 of every site that is ours. */
static int stc_overlaps(const void *source,size_t bytes) {
    if(!stc_done) return 0;
    const uintptr_t a=(uintptr_t)source,b=a+bytes;
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) {
        const uintptr_t s=(uintptr_t)(image+stc_sites[i].site+1);
        if((stc_done>>i)&1u && s<b && s+4>a) return 1;
    }
    return 0;
}
static void stc_unpatch_copy(const void *source,void *copy,size_t bytes) {
    if(!stc_done) return;
    const uintptr_t a=(uintptr_t)source;
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) {
        if(!((stc_done>>i)&1u)) continue;
        const int32_t d=stc_rel(i,image+stc_sites[i].target);unsigned char rel[4];memcpy(rel,&d,4);
        const uintptr_t s=(uintptr_t)(image+stc_sites[i].site+1);
        for(unsigned k=0;k<4;k++) if(s+k>=a && s+k<a+bytes) ((unsigned char*)copy)[s+k-a]=rel[k];
    }
}
static void st_chain_restore(void) {
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++)
        if((stc_done>>i)&1u && stc_site_state(i)==1 && stc_site_cas(i,stc_page+stc_stub(i),image+stc_sites[i].target)) stc_done&=~(1u<<i);
    if(!stc_done) {st_chain_installed=0;st_chain_restored=1;st_chain_stage="restored";}
}
/* With st_arm, before the entry swap: every site original, the pins, the page, then the six swaps. */
static int st_chain_arm(void) {
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) if(stc_site_state(i)!=0) {st_chain_stage="pins";return 0;}
    if(!hash_ok(image+STC_BLOCK,STC_BLOCK_SIZE,STC_BLOCK_SHA) || !hash_ok(image+STC_HELPER,STC_HELPER_SIZE,STC_HELPER_SHA) ||
       !hash_ok(image+STC_FM,STC_FM_SIZE,STC_FM_SHA) || !hash_ok(image+STC_FM_WRAPPER,STC_FM_WRAPPER_SIZE,STC_FM_WRAPPER_SHA) ||
       !hash_ok(image+STC_PT,STC_PT_SIZE,STC_PT_SHA) || !hash_ok(image+STC_PT_CONVERT,STC_PT_CONVERT_SIZE,STC_PT_CONVERT_SHA) ||
       !hash_ok(image+STC_PT_CALLER,STC_PT_CALLER_SIZE,STC_PT_CALLER_SHA) ||
       !hash_ok(image+STC_PT_CALLER2,STC_PT_CALLER2_SIZE,STC_PT_CALLER2_SHA) || !hash_ok(image+STC_HIER,STC_HIER_SIZE,STC_HIER_SHA) ||
       !hash_ok(image+STC_PS,STC_PS_SIZE,STC_PS_SHA) || !hash_ok(image+STC_PS_CALLER,STC_PS_CALLER_SIZE,STC_PS_CALLER_SHA)) {st_chain_stage="pins";return 0;}
    for(unsigned i=0;i<5u;i++) if(!hash_ok(image+stc_pc_pins[i].rva,stc_pc_pins[i].size,stc_pc_pins[i].sha)) {st_chain_stage="pins";return 0;}
    if(!stc_page) stc_page=stc_alloc_page();
    if(!stc_page) {st_chain_stage="page";return 0;}
    DWORD old,ignored;if(!VirtualProtect(stc_page,0x1000,PAGE_READWRITE,&old)) {st_chain_stage="page";return 0;}
    stc_write_code(stc_page);
    if(!VirtualProtect(stc_page,0x1000,PAGE_EXECUTE_READ,&ignored)) {st_chain_stage="page";return 0;}
    FlushInstructionCache(GetCurrentProcess(),stc_page,0x1000);
    st_chain_installed=1;st_chain_restored=0;stc_done=0;
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) {
        if(!stc_site_cas(i,image+stc_sites[i].target,stc_page+stc_stub(i))) {st_chain_restore();st_chain_installed=stc_done!=0;st_chain_stage="cas";return 0;}
        stc_done|=1u<<i;
    }
    st_chain_stage="armed";return 1;
}
static int st_chain_owned(void) {
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) if((stc_done>>i)&1u && stc_site_state(i)!=1) return 0;
    unsigned char want[STC_CODE_BYTES];stc_write_code(want);
    return stc_page && !memcmp(stc_page,want,sizeof(want));
}
