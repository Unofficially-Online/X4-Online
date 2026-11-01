/* The Linear step trace (gate_step_trace.h): the vtable entry swapped and restored, a watched ship's call
 * recorded before and after the step, other ships and other ticks passed through. A stand-in step sits at the
 * step's address of the synthetic image; the entry holds it. Included by test_gate_linux.c. */
static unsigned ts_calls;
/* FR8's stand-ins: asinf and the builder behind their targets, and the three calls at their sites (asinf: sub rsp,8;
 * call; add rsp,8; ret; the builder: push r12; mov r12,rsi; sub rsp,0x90; movss [rsp+0x80],xmm3; call; add rsp,0x90;
 * pop r12; ret, so the stub finds a dt at the step's [rsp+0x80] and an agent in r12). */
static int ts_chain;
static unsigned char ts_rows[0x30];
typedef float (*tsc_asin_site)(float);
typedef void (*tsc_builder_site)(void *rows,const void *agent,float pitch,float yaw,float roll,float dt);
static float tsc_asinf(float x) {return x*0.5f;}
static void tsc_builder(void *rows,float pitch,float yaw,float roll) {
    float r[12];for(unsigned i=0;i<12;i++) r[i]=yaw*(float)i+pitch-roll;
    memcpy(rows,r,sizeof(r));
}
/* FR17's stand-in GetTargetPositionForWingMan: out[i] = 10*index + i + time; returns 1 in al (garbage above). */
typedef uint64_t (*tsc_fm_site)(void *formation,void *out,uint64_t index,double time);
static uint64_t tsc_fm(void *formation,void *out,uint64_t index,double time) {
    (void)formation;float o[16];for(unsigned i=0;i<16;i++) o[i]=(float)(10*index+i)+(float)time;
    memcpy(out,o,sizeof(o));return 0x7700u|1u;
}
/* FR18's stand-ins: the local transform copies the point's mat4 (+0x20) to out; the conversion into a frame does that
 * and runs the conversion through its site with the reference object tsc_pt_ref; the conversion adds 100*flag. The
 * three converter sites sit inside push rbx; mov rbx,rcx; call; pop rbx; ret (the agent in rbx as in 0x1431270). */
typedef void *(*tsc_pt_site)(void *out,void *point,void *frame,void *agent);
typedef uint64_t (*tsc_pt_math_site)(void *out,void *velocity,void *ref,void *frame,uint64_t flag);
static unsigned char *tsc_pt_ref;
static void *tsc_pt_local(void *out,void *point) {memcpy(out,(unsigned char*)point+0x20,0x40);return out;}
static void *tsc_pt_convert(void *out,void *point,void *frame) {
    memcpy(out,(unsigned char*)point+0x20,0x40);void *r=tsc_pt_ref,*f=frame;
    ((tsc_pt_math_site)(void*)(LX(LX_SC_PT_MATH_SITE_VA)-4))(out,NULL,&r,&f,1);return out;
}
static uint64_t tsc_pt_math(void *out,void *velocity,void *ref,void *frame,uint64_t flag) {
    (void)velocity;(void)ref;(void)frame;float m[16];memcpy(m,out,0x40);
    for(unsigned i=0;i<16;i++) m[i]+=100.0f*(float)(flag&0xff);
    memcpy(out,m,0x40);return 0;
}
/* FR20's stand-in hierarchy function (0x18a2900): m -> 2m + i + 0.25 lane by lane; ecx must be 0. */
static unsigned tsc_hier_calls;
static void tsc_hier(void *from,void *inout,void *to,uint64_t zero) {
    (void)from;(void)to;assert(!zero);++tsc_hier_calls;float m[16];memcpy(m,inout,0x40);
    for(unsigned i=0;i<16;i++) m[i]=2.0f*m[i]+(float)i+0.25f;
    memcpy(inout,m,0x40);
}
/* FR19's stand-in SetTarget (as 0x141d140: the mat4 to +0x10, a3 to +0x50, a4 != 0 to +0x5c, 1 to +0x5d), reached through
 * the slot by a caller at 0x1431b58 (sub rsp,8; call [rip -> the slot]; add rsp,8; ret) whose call returns to 0x1431b62
 * as the real SetTarget call 0x1431b5f does. */
typedef void (*tsc_sg_site)(void *model,const void *mat,void *a3,void *a4);
static void tsc_settarget(void *model,const void *mat,void *a3,void *a4) {
    unsigned char *m=model;memcpy(m+0x10,mat,0x40);memcpy(m+0x50,&a3,8);m[0x5c]=a4!=NULL;m[0x5d]=1;
}
#define TSC_SG_CALLER 0x1431b58u
#define TSC_BUILDER_PREFIX 21u
static void ts_step(void *model,const void *vector,float dt,double time) {
    unsigned char *m=model;float v[4];memcpy(v,vector,16);
    const float x=v[0]+dt;memcpy(m+0x10,&x,4);memcpy(m+0x78,&time,8);++ts_calls;
    if(ts_chain) {
        const void *agent;memcpy(&agent,m+ST_MODEL_AGENT,8);
        const float t=((tsc_asin_site)(void*)(LX(LX_SC_ASIN_TARGET_SITE_VA)-4))(v[1]),c=((tsc_asin_site)(void*)(LX(LX_SC_ASIN_CUR_SITE_VA)-4))(v[2]);
        ((tsc_builder_site)(void*)(LX(LX_SC_BUILDER_SITE_VA)-TSC_BUILDER_PREFIX))(ts_rows,agent,c+t,0.5f,0.25f,dt);
    }
}
/* FR21's stand-in banking: 2t + 1. */
static float tsc_bank(void *hw,float t) {(void)hw;return 2.0f*t+1.0f;}
/* FR24's stand-in queue (counts, answers 0x4242), and a caller for the propagation's stub with its frame in rbp and
 * the leader's point in r13 (rdi 0, rcx 0). */
static unsigned tsc_oc_calls;
static uint64_t tsc_oc_queue(void *m,uint64_t s,void *p,uint64_t i) {(void)m;(void)s;(void)p;(void)i;++tsc_oc_calls;return 0x4242;}
__asm__(".text\n"
"test_lx_oc_fm_call:\n push %rbp\n push %r13\n push %rbx\n mov %rdi,%rax\n mov %rcx,%rbp\n mov %r8,%r13\n xor %edi,%edi\n xor %ecx,%ecx\n"
" call *%rax\n pop %rbx\n pop %r13\n pop %rbp\n ret\n");
extern uint64_t test_lx_oc_fm_call(const void *stub,uint64_t ship,void *point,const void *frame,const void *leader);
/* FR25's stand-ins: the variable store (counts, answers 0x99), the transform (2m + 1, flag 0x5a, time 1.25), the zone's
 * and the sector's GetSafePos (run the solver through its entry), the solver's rest after its prologue (p.x + 1, 0x77);
 * callers that put the context in r12 (GetSafePos: rdi boxes, rsi p, rdx dir, rcx 0) or rbp (the transform: rdi self,
 * rsi mat, rdx anc, ecx 0x5a, xmm0 time) and call a site's wrapper. */
static unsigned tsc_setvar_calls;
static uint64_t tsc_setvar(void *var,void *ctx,void *name,void *value) {(void)var;(void)ctx;(void)name;(void)value;++tsc_setvar_calls;return 0x99;}
static void tsc_gtr(void *self,void *mat,void *anc,uint32_t flag,double time) {
    (void)self;(void)anc;assert((flag&0xff)==0x5a && time==1.25);float m[16];memcpy(m,mat,0x40);
    for(unsigned i=0;i<16;i++) m[i]=2.0f*m[i]+1.0f;
    memcpy(mat,m,0x40);
}
static uint64_t tsc_solver_rest(void *boxes,float *p,float *dir,uint32_t xflag,uint32_t yflag,float *move,float radius,float a5,float a9) {
    (void)boxes;(void)dir;(void)move;assert(xflag==1 && !yflag && radius==50.0f && a5==1.5f && a9==2.5f);p[0]+=1.0f;return 0x77;
}
static uint64_t tsc_zone_gsp(void *boxes,float *p,float *dir,float *move) {
    return ((lxc_solver_fn)(void*)LX(LX_SC_SP_SOLVER_VA))(boxes,p,dir,1,0,move,50.0f,1.5f,2.5f);
}
__asm__(".text\n"
"test_lx_sp_gs_call:\n push %r12\n push %rbx\n mov %rdi,%rax\n mov %rsi,%r12\n mov %rdx,%rdi\n mov %rcx,%rsi\n mov %r8,%rdx\n xor %ecx,%ecx\n"
" sub $8,%rsp\n call *%rax\n add $8,%rsp\n pop %rbx\n pop %r12\n ret\n"
"test_lx_sp_tr_call:\n push %rbp\n push %rbx\n mov %rdi,%rax\n mov %r8,%rbp\n mov %rsi,%rdi\n mov %rdx,%rsi\n mov %rcx,%rdx\n mov $0x5a,%ecx\n"
" sub $8,%rsp\n call *%rax\n add $8,%rsp\n pop %rbx\n pop %rbp\n ret\n");
extern uint64_t test_lx_sp_gs_call(const void *wrapper,const void *ctx,void *boxes,float *p,float *dir);
extern void test_lx_sp_tr_call(const void *wrapper,void *self,void *mat,void *anc,const void *ctx,double time);
static void tsc_stand_ins(void) {
    unsigned char call[14]={0x48,0x83,0xec,0x08,0xe8,0,0,0,0,0x48,0x83,0xc4,0x08,0xc3};
    const uint32_t asin_sites[2]={LX_SC_ASIN_TARGET_SITE_VA,LX_SC_ASIN_CUR_SITE_VA};
    for(unsigned i=0;i<2;i++) {
        const int32_t rel=(int32_t)((intptr_t)LX(LX_SC_ASINF_VA)-(intptr_t)(LX(asin_sites[i])+5));memcpy(call+5,&rel,4);
        tl_write(LX(asin_sites[i])-4,call,sizeof(call));
    }
    unsigned char b[TSC_BUILDER_PREFIX+5+10]={0x41,0x54,0x49,0x89,0xf4,0x48,0x81,0xec,0x90,0,0,0,0xf3,0x0f,0x11,0x9c,0x24,0x80,0,0,0,
        0xe8,0,0,0,0,0x48,0x81,0xc4,0x90,0,0,0,0x41,0x5c,0xc3};
    const int32_t rel=(int32_t)((intptr_t)LX(LX_SC_BUILDER_VA)-(intptr_t)(LX(LX_SC_BUILDER_SITE_VA)+5));memcpy(b+TSC_BUILDER_PREFIX+1,&rel,4);
    tl_write(LX(LX_SC_BUILDER_SITE_VA)-TSC_BUILDER_PREFIX,b,sizeof(b));
    unsigned char jump[14]={0xff,0x25,0,0,0,0};void *target=(void*)tsc_asinf;memcpy(jump+6,&target,8);tl_write(LX(LX_SC_ASINF_VA),jump,14);
    target=(void*)tsc_builder;memcpy(jump+6,&target,8);tl_write(LX(LX_SC_BUILDER_VA),jump,14);
    /* FR17: each formation call site inside sub rsp,8; call; add rsp,8; ret, the target a stand-in. */
    const uint32_t fm_sites[2]={LX_SC_FM_AGENT_SITE_VA,LX_SC_FM_OTHER_SITE_VA};
    for(unsigned i=0;i<2;i++) {
        const int32_t r=(int32_t)((intptr_t)LX(LX_SC_FM_VA)-(intptr_t)(LX(fm_sites[i])+5));memcpy(call+5,&r,4);
        tl_write(LX(fm_sites[i])-4,call,sizeof(call));
    }
    target=(void*)tsc_fm;memcpy(jump+6,&target,8);tl_write(LX(LX_SC_FM_VA),jump,14);
    /* FR18 */
    const uint32_t pt_sites[3]={LX_SC_PT_LOCAL_SITE_VA,LX_SC_PT_CONVERT_A_SITE_VA,LX_SC_PT_CONVERT_B_SITE_VA};
    for(unsigned i=0;i<3;i++) {
        unsigned char pc[11]={0x53,0x48,0x89,0xcb,0xe8,0,0,0,0,0x5b,0xc3};
        const int32_t r=(int32_t)(int64_t)((uintptr_t)LX(i?LX_SC_PT_CONVERT_VA:LX_SC_PT_LOCAL_VA)-(uintptr_t)(LX(pt_sites[i])+5)); /* targets below their sites */memcpy(pc+5,&r,4);
        tl_write(LX(pt_sites[i])-4,pc,sizeof(pc));
    }
    {const int32_t r=(int32_t)(int64_t)((uintptr_t)LX(LX_SC_PT_MATH_VA)-(uintptr_t)(LX(LX_SC_PT_MATH_SITE_VA)+5));memcpy(call+5,&r,4);
     tl_write(LX(LX_SC_PT_MATH_SITE_VA)-4,call,sizeof(call));}
    target=(void*)tsc_pt_local;memcpy(jump+6,&target,8);tl_write(LX(LX_SC_PT_LOCAL_VA),jump,14);
    target=(void*)tsc_pt_convert;memcpy(jump+6,&target,8);tl_write(LX(LX_SC_PT_CONVERT_VA),jump,14);
    target=(void*)tsc_pt_math;memcpy(jump+6,&target,8);tl_write(LX(LX_SC_PT_MATH_VA),jump,14);
    /* FR20: 0x143235b and 0x1432611 inside push rbx; mov rbx,rcx; call; pop rbx; ret, the hierarchy function a stand-in. */
    {const uint32_t s20[2]={LX_SC_PT_CONVERT_C_SITE_VA,LX_SC_PT_OBJECT_SITE_VA},t20[2]={LX_SC_PT_CONVERT_VA,LX_SC_HIER_VA};
     for(unsigned i=0;i<2;i++) {
         unsigned char pc[11]={0x53,0x48,0x89,0xcb,0xe8,0,0,0,0,0x5b,0xc3};
         const int32_t r=(int32_t)(int64_t)((uintptr_t)LX(t20[i])-(uintptr_t)(LX(s20[i])+5));memcpy(pc+5,&r,4);
         tl_write(LX(s20[i])-4,pc,sizeof(pc));
     }}
    target=(void*)tsc_hier;memcpy(jump+6,&target,8);tl_write(LX(LX_SC_HIER_VA),jump,14);
    /* FR24: the queue's four calls inside sub rsp,8; call; add rsp,8; ret, the queue a stand-in. */
    {const uint32_t oc[4]={LX_SC_OC1_SITE_VA,LX_SC_OC_FM_SITE_VA,LX_SC_OC3_SITE_VA,LX_SC_OC4_SITE_VA};
     for(unsigned i=0;i<4;i++) {
         const int32_t r=(int32_t)(int64_t)((uintptr_t)LX(LX_SC_OC_VA)-(uintptr_t)(LX(oc[i])+5));memcpy(call+5,&r,4);
         tl_write(LX(oc[i])-4,call,sizeof(call));
     }}
    target=(void*)tsc_oc_queue;memcpy(jump+6,&target,8);tl_write(LX(LX_SC_OC_VA),jump,14);
    /* FR25: the five sites inside sub rsp,8; call; add rsp,8; ret, their targets stand-ins; the solver's prologue in
     * place, then a jump to the stand-in rest. */
    {const uint32_t s25[5]={LX_SC_SP_CV_SITE_VA,LX_SC_SP_GV_SITE_VA,LX_SC_SP_TR_SITE_VA,LX_SC_SP_GZ_SITE_VA,LX_SC_SP_GS_SITE_VA},
                    t25[5]={LX_SC_SETVAR_VA,LX_SC_SETVAR_VA,LX_SC_SP_TR_VA,LX_SC_SP_ZONE_VA,LX_SC_SP_SECTOR_VA};
     for(unsigned i=0;i<5;i++) {
         const int32_t r=(int32_t)(int64_t)((uintptr_t)LX(t25[i])-(uintptr_t)(LX(s25[i])+5));memcpy(call+5,&r,4);
         tl_write(LX(s25[i])-4,call,sizeof(call));
     }}
    target=(void*)tsc_setvar;memcpy(jump+6,&target,8);tl_write(LX(LX_SC_SETVAR_VA),jump,14);
    target=(void*)tsc_gtr;memcpy(jump+6,&target,8);tl_write(LX(LX_SC_SP_TR_VA),jump,14);
    target=(void*)tsc_zone_gsp;memcpy(jump+6,&target,8);tl_write(LX(LX_SC_SP_ZONE_VA),jump,14);tl_write(LX(LX_SC_SP_SECTOR_VA),jump,14);
    tl_write(LX(LX_SC_SP_SOLVER_VA),lxc_sp_entry_original,LX_SC_SP_SOLVER_PROLOGUE);
    target=(void*)tsc_solver_rest;memcpy(jump+6,&target,8);tl_write(LX(LX_SC_SP_SOLVER_VA)+LX_SC_SP_SOLVER_PROLOGUE,jump,14);
    /* FR19 */
    target=(void*)tsc_settarget;memcpy(jump+6,&target,8);tl_write(LX(LX_SG_FN_VA),jump,14);
    {unsigned char sg[15]={0x48,0x83,0xec,0x08,0xff,0x15,0,0,0,0,0x48,0x83,0xc4,0x08,0xc3};
     const int32_t d=(int32_t)(LX_SG_SLOT_VA-(TSC_SG_CALLER+10));memcpy(sg+6,&d,4);tl_write(LX(TSC_SG_CALLER),sg,sizeof(sg));}
    /* FR21: the banking's two call sites call the stand-in. */
    target=(void*)tsc_bank;memcpy(jump+6,&target,8);tl_write(LX(LX_SC_HW_BANK_VA),jump,14);
    {const uint32_t s21[2]={LX_SC_HW_BANK_SITE_VA,LX_SC_HW_BANK2_SITE_VA};
     for(unsigned i=0;i<2;i++) {
         unsigned char c[5]={0xe8,0,0,0,0};
         const int32_t r=(int32_t)(int64_t)((uintptr_t)LX(LX_SC_HW_BANK_VA)-(uintptr_t)(LX(s21[i])+5));memcpy(c+1,&r,4);tl_write(LX(s21[i]),c,5);
     }}
}
static void test_step_trace_linux(void) {
    /* jmp [rip] -> ts_step at the step's address; the entry holds the step. */
    unsigned char code[14]={0xff,0x25,0,0,0,0};void *target=(void*)ts_step;memcpy(code+6,&target,8);
    tl_write(LX(LX_ST_STEP_VA),code,sizeof(code));
    void *step=LX(LX_ST_STEP_VA),*saved_entry;memcpy(&saved_entry,LX(LX_ST_SLOT_VA),8);
    {uintptr_t page=(uintptr_t)LX(LX_ST_SLOT_VA)&~(uintptr_t)(GATE_PAGE-1);assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE));
     memcpy(LX(LX_ST_SLOT_VA),&step,8);assert(!mprotect((void*)page,GATE_PAGE,PROT_READ));}
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();
    world_identities=calloc(WORLD_IDENTITY_CAPACITY,sizeof(*world_identities));assert(world_identities);world_identity_installed=1;
    world_identity_count=0;world_identity_sealed=0;assert(world_identity_record(9101,0x7dcd0) && world_identity_record(9102,0x7dd7c));
    st_id_count=0;st_installed=0;
    assert(!st_arm(3) && !strcmp(st_stage,"refused")); /* nothing watched */
    assert(st_watch(0x7dcd0) && st_watch(0x7dcd0) && st_id_count==1);
    boot_phase=2;assert(!st_arm(3));boot_phase=0;
    assert(st_arm(3) && st_installed && st_arm(3) && !st_arm(4));
    assert(st_watch(0x7dcd0) && !st_watch(0x7dd7c) && st_id_count==1); /* the reload's rerun: the same ID again, no new one */
    void *entry;memcpy(&entry,LX(LX_ST_SLOT_VA),8);assert(entry==(void*)st_step && st_native==step);
    /* Two ships, the watched one with a saved ID, the other without being watched. */
    static __attribute__((aligned(16))) unsigned char ships[2][0x20],agents[2][0x1000],models[2][ST_MODEL_BYTES];
    const uint64_t runtime[2]={9101,9102};
    for(unsigned i=0;i<2;i++) {
        memset(models[i],0,sizeof(models[i]));memset(agents[i],0x11*(i+1),sizeof(agents[i]));
        memcpy(ships[i]+8,&runtime[i],8);void *ship=ships[i],*agent=agents[i];
        memcpy(agents[i]+ST_AGENT_OWNER,&ship,8);memcpy(models[i]+ST_MODEL_AGENT,&agent,8);
    }
    const float vector[4]={1.5f,2.0f,3.0f,0.0f};
    st_step_fn call=(st_step_fn)entry;
    boot_phase=3;ts_calls=0;
    for(uint64_t tick=1;tick<=4;tick++) {stats.issued=tick;for(unsigned i=0;i<2;i++) call(models[i],vector,0.25f,10.0+tick);}
    assert(ts_calls==8 && st_calls==8 && st_watched_calls==3 && st_records==3 && !st_dropped);
    /* The record of tick 2: the model before and after, the agent, the arguments. */
    char row[1024];
    assert(st_row(1*ST_CHUNKS+0,row,sizeof(row)) && strstr(row,"tick=2 key=000000000007dcd0 ordinal=0 thread=ui part=args offset=0 dt=3e800000 time=4028000000000000 bytes=0000c03f0000004000004040"));
    assert(st_row(1*ST_CHUNKS+1,row,sizeof(row)) && strstr(row,"part=model_pre offset=0 "));
    assert(st_row(1*ST_CHUNKS+3,row,sizeof(row)) && strstr(row,"part=model_post offset=0 ") && strstr(row,"bytes=0000000000000000")); /* +0..+7 the vtable slot, zero here */
    const char *post=strstr(row,"bytes=")+6;assert(!strncmp(post+0x10*2,"0000e03f",8)); /* +0x10 = 1.75f */
    assert(st_row(1*ST_CHUNKS+10,row,sizeof(row)) && strstr(row,"part=agent_pre offset=500 ") && strstr(row,"bytes=")+6+0x40*2==strstr(row," banks="));
    /* The pose banks (agent+0xaf0, both): before and after, the last chunk 0x40 bytes; the bank indices at the end. */
    assert(ST_CHUNKS==25 && st_row(1*ST_CHUNKS+17,row,sizeof(row)) && strstr(row,"part=pose_pre offset=0 ") && strstr(row,"bytes=11111111"));
    uint32_t banks[2];st_banks(banks);char want[64];snprintf(want,sizeof(want)," banks=%x,%x/%x,%x",banks[0],banks[1],banks[0],banks[1]);
    assert(banks[0]<=1 && banks[1]<=1 && banks[0]+banks[1]==1); /* the synthetic image's read and written bank */
    assert(st_row(1*ST_CHUNKS+24,row,sizeof(row)) && strstr(row,"part=pose_post offset=300 ") && strstr(row,want) &&
           strstr(row,"bytes=")+6+0x40*2==strstr(row," banks="));
    assert(!st_row(3*ST_CHUNKS,row,sizeof(row))); /* tick 4 is past the window */
    char status[384];assert(st_status(status,sizeof(status)) && strstr(status,"installed=1 restored=0 ids=1 ticks=3 calls=8 watched_calls=3 records=3 dropped=0"));
    st_restore();memcpy(&entry,LX(LX_ST_SLOT_VA),8);assert(!st_installed && st_restored && entry==step);
    {uintptr_t page=(uintptr_t)LX(LX_ST_SLOT_VA)&~(uintptr_t)(GATE_PAGE-1);assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE));
     memcpy(LX(LX_ST_SLOT_VA),&saved_entry,8);assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE));}
    st_id_count=0;world_identity_installed=0;world_identity_count=0;
    printf("step trace PASS\n");
}
/* FR8 with the step trace: refused on a foreign site, then armed (sites before the entry), the stand-in step calling
 * the three sites: only the watched ship's calls in ticks 1-3 recorded (26 rows a record), every call counted; the
 * chain row's words; ownership; the restoration byte-exact. */
static void test_step_chain_linux(void) {
    tsc_stand_ins();
    unsigned char lines[ST_CHAIN_SITE_COUNT][16];
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) memcpy(lines[i],(void*)((uintptr_t)LX(lxc_sites[i].site+1)&~(uintptr_t)15),16);
    void *step=LX(LX_ST_STEP_VA),*saved_entry;memcpy(&saved_entry,LX(LX_ST_SLOT_VA),8);
    void *saved_sg,*sg_fn=LX(LX_SG_FN_VA),*saved_hw,*hw_fn=LX(LX_HW_FN_VA);
    {uintptr_t page=(uintptr_t)LX(LX_ST_SLOT_VA)&~(uintptr_t)(GATE_PAGE-1);assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE));
     memcpy(LX(LX_ST_SLOT_VA),&step,8);memcpy(&saved_sg,LX(LX_SG_SLOT_VA),8);memcpy(LX(LX_SG_SLOT_VA),&sg_fn,8);assert(!mprotect((void*)page,GATE_PAGE,PROT_READ));}
    {uintptr_t page=(uintptr_t)LX(LX_HW_SLOT_VA)&~(uintptr_t)(GATE_PAGE-1);assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE));
     memcpy(&saved_hw,LX(LX_HW_SLOT_VA),8);memcpy(LX(LX_HW_SLOT_VA),&hw_fn,8);assert(!mprotect((void*)page,GATE_PAGE,PROT_READ));}
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();
    world_identities=calloc(WORLD_IDENTITY_CAPACITY,sizeof(*world_identities));assert(world_identities);world_identity_installed=1;
    world_identity_count=0;world_identity_sealed=0;assert(world_identity_record(9101,0x7dcd0) && world_identity_record(9102,0x7dd7c));
    st_id_count=0;st_installed=0;st_restored=0;st_chain_wanted=0;st_chain_installed=0;st_chain_restored=0;
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) st_chain_hits[i]=0;
    st_chain_recorded=st_chain_no_slot=0;
    thunk=make_thunk();assert(thunk);
    assert(st_watch(0x7dd7c) && st_chain_want() && st_chain_wanted);
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) assert(lxc_site_state(i)==0);
    /* A foreign call target at one site: refused, nothing changed. */
    unsigned char *cur=LX(LX_SC_ASIN_CUR_SITE_VA);int32_t rel;memcpy(&rel,cur+1,4);const int32_t off=rel+16;tl_write(cur+1,&off,4);
    assert(!st_arm(3) && !strcmp(st_stage,"chain") && !strcmp(st_chain_stage,"pins") && !st_installed && !st_chain_installed);
    tl_write(cur+1,&rel,4);
    assert(st_arm(3) && st_installed && st_chain_installed && st_chunks==ST_CHUNKS+1+ST_SPLINE_CHUNKS+ST_FM_CHUNKS+ST_PT_CHUNKS+ST_SG_CHUNKS+ST_PS_CHUNKS+ST_HW_CHUNKS+ST_SP_CHUNKS && st_arm(3) && st_sg_installed && st_chain_want());
    /* FR21: the Highway's slot swapped with the chain; its banking sites lead to the stand-in, nothing kept outside an entry. */
    assert(st_hw_installed && *(void**)LX(LX_HW_SLOT_VA)==(void*)st_hw_transform && st_hw_native==hw_fn);
    {const LONG64 b0=st_chain_hits[11],b1=st_chain_hits[12];
     assert(((float (*)(void *,float))(void*)lxc_stub(11))(NULL,0.25f)==1.5f && ((float (*)(void *,float))(void*)lxc_stub(12))(NULL,1.0f)==3.0f);
     assert(st_chain_hits[11]==b0+1 && st_chain_hits[12]==b1+1 && !st_hw_banking);}
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) assert(lxc_site_state(i)==1);
    assert(!memcmp(thunk+LXC_STUBS+0x40,"\x48\x8d\x74\x24\x08\x4c\x89\xe2\xff\x25",10) && st_chain_owned());
    assert(!memcmp(thunk+LXC_STUBS+0x60,"\xff\x25",2) && !memcmp(thunk+LXC_STUBS+0x80,"\xff\x25",2));
    for(unsigned i=5;i<8;i++) assert(!memcmp(thunk+LXC_STUBS+0x20*i,"\x48\x89\xd9\xff\x25\0\0\0\0",9));
    for(unsigned i=9;i<11;i++) assert(!memcmp(thunk+LXC_STUBS+0x20*i,"\x48\x89\xd9\xff\x25\0\0\0\0",9));
    assert(!memcmp(thunk+LXC_STUBS+0x100,"\xff\x25\0\0\0\0",6));
    void *entry;memcpy(&entry,LX(LX_ST_SLOT_VA),8);assert(entry==(void*)st_step);
    static __attribute__((aligned(16))) unsigned char ships[2][0x80],agents[2][0x1000],models[2][ST_MODEL_BYTES]; /* +0x70 (parent) 0 */
    const uint64_t runtime[2]={9101,9102};
    for(unsigned i=0;i<2;i++) {
        memset(models[i],0,sizeof(models[i]));memset(agents[i],0x11*(i+1),sizeof(agents[i]));
        memcpy(ships[i]+8,&runtime[i],8);void *ship=ships[i],*agent=agents[i];
        memcpy(agents[i]+ST_AGENT_OWNER,&ship,8);memcpy(models[i]+ST_MODEL_AGENT,&agent,8);
    }
    /* FR9: the watched model's spline, two segments (0xa0 each) and their starts. */
    static __attribute__((aligned(16))) unsigned char segs[2][ST_SPLINE_SEGMENT],starts[2][8];
    {unsigned char *p=(unsigned char*)segs,*q=(unsigned char*)starts;
     for(unsigned i=0;i<sizeof(segs);i++) p[i]=(unsigned char)(i*7+1);
     for(unsigned i=0;i<sizeof(starts);i++) q[i]=(unsigned char)(0xa0+i);}
    {const uint64_t b=(uintptr_t)segs,e=b+sizeof(segs),s=(uintptr_t)starts;const float k=1.0f;unsigned char *h=models[1]+ST_MODEL_SPLINE;
     memcpy(h+8,&k,4);memcpy(h+0x10,&b,8);memcpy(h+0x18,&e,8);memcpy(h+0x28,&s,8);}
    const float vector[4]={1.5f,2.0f,3.0f,0.0f};
    st_step_fn call=(st_step_fn)entry;
    /* FR17: a formation of the two agents (leader agents[0], unwatched; wingman agents[1], watched), its shape, read bank 1. */
    static __attribute__((aligned(16))) unsigned char formation[0x80],shape[0x80],fm_out[0x40];
    {void *members[2]={agents[0],agents[1]};static void *member_store[2];memcpy(member_store,members,sizeof(members));
     const uint64_t b=(uintptr_t)member_store,e=b+sizeof(member_store);memset(formation,0,sizeof(formation));
     memcpy(formation+ST_FM_MEMBERS,&b,8);memcpy(formation+ST_FM_MEMBERS+8,&e,8);
     for(unsigned i=0;i<sizeof(shape);i++) shape[i]=(unsigned char)(0x40+i);
     void *vt=LX(0x3b00000u),*sp=shape;memcpy(shape,&vt,8);memcpy(formation+ST_FM_SHAPE,&sp,8);
     float pose[31];for(unsigned i=0;i<31;i++) pose[i]=1.0f+(float)i;memcpy(agents[0]+ST_AGENT_POSE+ST_BANK_STRIDE,pose,sizeof(pose));
     const uint32_t bank=1;tl_write(ST_BANK_READ,&bank,4);}
    /* FR18: the path point (reference runtime 9103 at +0x8, a mat4 at +0x20), its reference object and the frame object
     * (runtime ids 9103/9104, controllers, the reference's parent the first ship). */
    static __attribute__((aligned(16))) unsigned char point[0x80],ref[0x300],frame[0x300],ref_controller[0x60],frame_controller[0x60],pt_out[0x40];
    float m[16];
    {assert(world_identity_record(9103,0x1234) && world_identity_record(9104,0x5678));
     const uint64_t ref_runtime=9103,frame_runtime=9104;void *rc=ref_controller,*fc=frame_controller,*parent=ships[0];
     for(unsigned i=0;i<sizeof(point);i++) point[i]=(unsigned char)(0x30+i);
     for(unsigned i=0;i<16;i++) m[i]=0.5f*(float)i-3.0f;
     memcpy(point+0x20,m,0x40);memcpy(point+ST_PT_REF,&ref_runtime,8);
     memset(ref,0,sizeof(ref));memset(frame,0,sizeof(frame));memcpy(ref+8,&ref_runtime,8);memcpy(frame+8,&frame_runtime,8);
     memcpy(ref+ST_OBJ_CONTROLLER,&rc,8);memcpy(frame+ST_OBJ_CONTROLLER,&fc,8);memcpy(ref+ST_OBJ_PARENT,&parent,8);
     for(unsigned i=0;i<0x60;i++) {ref_controller[i]=(unsigned char)(0x80+i);frame_controller[i]=(unsigned char)(0xe0-i);}
     tsc_pt_ref=ref;void *pp=point;memcpy(agents[1]+ST_AGENT_POINT,&pp,8);}
    float sg_target[16];for(unsigned i=0;i<16;i++) sg_target[i]=7.0f*(float)i-1.5f;
    static __attribute__((aligned(16))) unsigned char obj_mat[0x40];float om[16];for(unsigned i=0;i<16;i++) om[i]=(float)i-7.5f;
    boot_phase=3;ts_calls=0;ts_chain=1;tsc_hier_calls=0;st_pt_levels_probed=0;
    for(uint64_t tick=1;tick<=4;tick++) {
        stats.issued=tick;
        if(tick==3) { /* FR20: the frame conversion at 0x143235b and the object path at 0x1432611, the watched agent in rbx */
            memset(pt_out,0x5a,sizeof(pt_out));assert(((tsc_pt_site)(void*)(LX(LX_SC_PT_CONVERT_C_SITE_VA)-4))(pt_out,point,frame,agents[1])==pt_out);
            memcpy(obj_mat,om,0x40);((tsc_pt_site)(void*)(LX(LX_SC_PT_OBJECT_SITE_VA)-4))(ref,obj_mat,frame,agents[1]);
        }
        if(tick==2 || tick==4) {
            memset(fm_out,0x5a,sizeof(fm_out));
            assert((((tsc_fm_site)(void*)(LX(LX_SC_FM_AGENT_SITE_VA)-4))(formation,fm_out,1,12.5)&0xff)==1);
            assert((((tsc_fm_site)(void*)(LX(LX_SC_FM_OTHER_SITE_VA)-4))(formation,fm_out,0,12.5)&0xff)==1); /* the leader: not kept */
            if(tick==2) assert((((tsc_fm_site)(void*)(LX(LX_SC_FM_OTHER_SITE_VA)-4))(formation,fm_out,1,13.5)&0xff)==1);
            /* FR18: tick 2 the wingman's local and frame conversions kept, the leader's (unwatched) not; tick 4 past. */
            memset(pt_out,0x5a,sizeof(pt_out));
            if(tick==2) {
                assert(((tsc_pt_site)(void*)(LX(LX_SC_PT_LOCAL_SITE_VA)-4))(pt_out,point,NULL,agents[1])==pt_out);
                assert(((tsc_pt_site)(void*)(LX(LX_SC_PT_CONVERT_B_SITE_VA)-4))(pt_out,point,frame,agents[0])==pt_out);
            }
            assert(((tsc_pt_site)(void*)(LX(LX_SC_PT_CONVERT_A_SITE_VA)-4))(pt_out,point,frame,agents[1])==pt_out);
            ((tsc_sg_site)(void*)LX(TSC_SG_CALLER))(models[1],sg_target,tick==2?(void*)ships[0]:NULL,tick==2?(void*)1:NULL);
            if(tick==2) ((tsc_sg_site)(void*)LX(TSC_SG_CALLER))(models[0],sg_target,NULL,NULL);
        }
        for(unsigned i=0;i<2;i++) call(models[i],vector,0.25f,10.0+tick);
    }
    {void *r=ref,*f=frame;((tsc_pt_math_site)(void*)(LX(LX_SC_PT_MATH_SITE_VA)-4))(pt_out,NULL,&r,&f,1);} /* outside any call */
    ts_chain=0;
    assert(ts_calls==8 && st_watched_calls==3 && st_records==3 && !st_dropped && !st_live_open && !st_live());
    assert(st_chain_hits[0]==8 && st_chain_hits[1]==8 && st_chain_hits[2]==8 && st_chain_recorded==9 && !st_chain_no_slot);
    assert(st_chain_hits[3]==2 && st_chain_hits[4]==3 && st_fm_kept==2);
    assert(st_chain_hits[9]==1 && st_chain_hits[10]==1 && tsc_hier_calls==4 && st_pt_levels_probed==3);
    assert(st_chain_hits[5]==1 && st_chain_hits[6]==2 && st_chain_hits[7]==1 && st_chain_hits[8]==5 && st_pt_kept==4 && st_pt_conversions==3 &&
           !st_pt_no_slot && !st_pt_open && !st_pt_live());
    /* FR19: the tick-2 record carries the watched ship's SetTarget call (caller 0x1431b62, a3 the first ship, a4 1, the
     * mat4, its point and bank, the target after); the unwatched ship's and the tick-4 call are not kept. */
    assert(st_sg_installed && st_sg_calls==3 && st_sg_kept==1);
    {const uint32_t *g=st_ring[1].settarget;uint64_t k;
     assert(g[0]==ST_SG_TAG && g[1]==2 && g[2]==0x1431b62u-LX_IMAGE_BASE && g[3]==0 && g[4]==1 && g[5]==0 && g[14]==0 && g[127]==1);
     memcpy(&k,g+6,8);assert(k==0x7dd7c);memcpy(&k,g+8,8);assert(k==(uint64_t)(uintptr_t)ships[0]);memcpy(&k,g+10,8);assert(k==0x7dcd0);
     memcpy(&k,g+12,8);assert(k==1);
     assert(!memcmp(g+16,sg_target,0x40) && !memcmp(g+52,point,0x80) && g[84]==0x22222222u && !memcmp(g+100,sg_target,0x40));
     for(unsigned i=0;i<ST_SG_WORDS;i++) assert(!g[ST_SG_WORDS+i] && !st_ring[0].settarget[i] && !st_ring[2].settarget[i]);}
    /* FR18: the tick-2 record carries both kept entries (the local one: no conversion, no frame; the frame one: the
     * conversion's matrix before and after, both objects, their controllers and parents), two calls; the others none. */
    {const uint32_t *p=st_ring[1].path,*q=p+ST_PT_WORDS;float plus[16];uint64_t k;
     for(unsigned i=0;i<16;i++) plus[i]=m[i]+100.0f;
     assert(p[0]==ST_PT_TAG && p[1]==2 && p[2]==0 && p[3]==1 && p[4]==0 && p[5]==1 && !p[16] && p[17]==0 && p[18]==0 && p[ST_PT_WORDS-1]==2);
     memcpy(&k,p+6,8);assert(k==0x7dd7c);memcpy(&k,p+8,8);assert(k==0x1234);memcpy(&k,p+10,8);assert(!k);memcpy(&k,p+19,8);assert(!k);
     assert(!memcmp(p+24,point,0x80) && !memcmp(p+56,m,0x40) && !p[72] && !p[104] && p[152]==0x22222222u);
     assert(q[0]==ST_PT_TAG && q[1]==2 && q[2]==1 && q[3]==1 && q[16]==1 && q[17]==1 && q[18]==1 && q[ST_PT_WORDS-1]==2);
     memcpy(&k,q+8,8);assert(k==0x1234);memcpy(&k,q+10,8);assert(k==0x5678);memcpy(&k,q+19,8);assert(k==0x1234);
     memcpy(&k,q+21,8);assert(k==0x5678);memcpy(&k,q+168,8);assert(k==0x7dcd0);memcpy(&k,q+170,8);assert(!k);
     assert(!memcmp(q+56,plus,0x40) && !memcmp(q+72,m,0x40) && !memcmp(q+88,plus,0x40));
     assert(!memcmp(q+104,ref_controller,0x60) && !memcmp(q+128,frame_controller,0x60));
     for(unsigned i=0;i<ST_PT_WORDS;i++) assert(!p[2*ST_PT_WORDS+i] && !st_ring[0].path[i]);
     /* FR20: the frame entry's reference chain ref (probed) -> ships[0] (top), the frame's frame (top). */
     float lv[16]={0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0};for(unsigned i=0;i<16;i++) lv[i]=2.0f*lv[i]+(float)i+0.25f;
     memcpy(&k,q+ST_PT_REF_LEVELS,8);assert(k==0x1234 && q[ST_PT_REF_LEVELS+2]==1 && !memcmp(q+ST_PT_REF_LEVELS+4,lv,0x40));
     memcpy(&k,q+ST_PT_REF_LEVELS+ST_PT_LEVEL_WORDS,8);assert(k==0x7dcd0 && !q[ST_PT_REF_LEVELS+ST_PT_LEVEL_WORDS+2]);
     memcpy(&k,q+ST_PT_FRAME_LEVELS,8);assert(k==0x5678 && !q[ST_PT_FRAME_LEVELS+2] && !q[ST_PT_SCRATCH] && !q[ST_PT_SCRATCH+3]);
     /* FR20: tick 3, 0x143235b (which 3, the conversion inside) and the object path (which 4: no point, the call itself). */
     const uint32_t *a=st_ring[2].path,*b=a+ST_PT_WORDS;float o2[16];for(unsigned i=0;i<16;i++) o2[i]=2.0f*om[i]+(float)i+0.25f;
     assert(a[0]==ST_PT_TAG && a[1]==3 && a[2]==3 && a[16]==1 && a[17]==0 && !memcmp(a+56,plus,0x40) && !memcmp(a+88,plus,0x40) && a[ST_PT_WORDS-1]==2);
     memcpy(&k,a+ST_PT_REF_LEVELS,8);assert(k==0x1234 && a[ST_PT_REF_LEVELS+2]==1 && !memcmp(a+ST_PT_REF_LEVELS+4,lv,0x40));
     assert(b[0]==ST_PT_TAG && b[1]==3 && b[2]==4 && b[16]==1 && b[17]==1 && b[18]==1 && b[24]==0xffffffffu && !memcmp(b+72,om,0x40) && !memcmp(b+88,o2,0x40) && !memcmp(b+56,o2,0x40));
     memcpy(&k,b+8,8);assert(!k);memcpy(&k,b+19,8);assert(k==0x1234);memcpy(&k,b+21,8);assert(k==0x5678);
     memcpy(&k,b+ST_PT_REF_LEVELS,8);assert(k==0x1234 && b[ST_PT_REF_LEVELS+2]==1);
     for(unsigned i=0;i<ST_PS_ENTRIES*ST_PS_WORDS;i++) assert(!st_ring[1].pathset[i]);}
    /* FR17: the tick-2 record carries both kept calls of the wingman (ordinals 0 and 1, two calls), the others none. */
    {const uint32_t *f=st_ring[1].formation;float want[16];
     assert(f[0]==ST_FM_TAG && f[1]==2 && f[2]==0 && f[3]==1 && f[4]==2 && f[7]==1 && f[8]==0 && f[9]==1 && f[15]==0 && f[127]==2);
     uint64_t k;memcpy(&k,f+10,8);assert(k==0x7dd7c);memcpy(&k,f+12,8);assert(k==0x7dcd0);
     double t;memcpy(&t,f+5,8);assert(t==12.5 && f[14]==0x3b00000u-LX_IMAGE_BASE);
     for(unsigned i=0;i<16;i++) want[i]=1.0f+(float)i;
     assert(!memcmp(f+16,want,0x40) && !memcmp(f+32+2,shape+8,0x78) && f[112]==st_bits(17.0f) && f[126]==st_bits(31.0f));
     for(unsigned i=0;i<16;i++) want[i]=(float)(10+i)+12.5f;
     assert(!memcmp(f+64,want,0x40) && f[80]==0x5a5a5a5au && f[96]==0x22222222u);
     const uint32_t *g=f+ST_FM_WORDS;assert(g[0]==ST_FM_TAG && g[2]==1 && g[15]==1 && g[127]==2);
     for(unsigned i=0;i<16;i++) want[i]=(float)(10+i)+13.5f;
     assert(!memcmp(g+64,want,0x40));
     for(unsigned i=0;i<ST_FM_WORDS;i++) assert(!f[2*ST_FM_WORDS+i] && !st_ring[0].formation[i] && !st_ring[2].formation[i]);}
    /* The watched ship (agent bytes 0x22): asinf 2.0 -> 1.0 (target), 3.0 -> 1.5 (current); the builder's pitch 2.5, yaw
     * 0.5, roll 0.25, rows yaw*i + pitch - roll, dt 0.25 from the step's frame, the agent's rates. */
    const st_record_t *r=&st_ring[1];const uint32_t *w=r->chain,*s=w+ST_CHAIN_HEAD;
    float rows[12];for(unsigned i=0;i<12;i++) rows[i]=0.5f*(float)i+2.5f-0.25f;
    assert(r->tick==2 && w[0]==ST_CHAIN_TAG && w[1]==1 && w[2]==0x3f000000u && w[3]==0x40200000u && w[4]==0x3e800000u && !memcmp(w+5,rows,0x30));
    assert(w[17]==0x3e800000u && w[18]==0x22222222u && w[19]==0x22222222u && w[20]==0x22222222u);
    assert(!s[0] && !s[7] && !s[14] && s[21]==1 && s[22]==0x40400000u && s[27]==0x3fc00000u && s[28]==1 && s[29]==0x40000000u && s[34]==0x3f800000u);
    char row[1024];
    assert(st_chunks==ST_CHUNKS+1+ST_SPLINE_CHUNKS+ST_FM_CHUNKS+ST_PT_CHUNKS+ST_SG_CHUNKS+ST_PS_CHUNKS+ST_HW_CHUNKS+ST_SP_CHUNKS && st_chunks==212);
    assert(st_row(1*st_chunks+ST_CHUNKS+1+ST_SPLINE_CHUNKS,row,sizeof(row)) && strstr(row,"tick=2 key=000000000007dd7c ordinal=0 thread=ui part=formation offset=0 ") &&
           strstr(row,"bytes=0117524602000000000000000100000002000000"));
    assert(st_row(1*st_chunks+ST_CHUNKS+ST_SPLINE_CHUNKS+ST_FM_CHUNKS,row,sizeof(row)) && strstr(row,"part=formation offset=500 ") && strstr(row,"bytes=")+6+0x100*2==strstr(row," banks="));
    assert(st_row(1*st_chunks+ST_CHUNKS+1+ST_SPLINE_CHUNKS+ST_FM_CHUNKS,row,sizeof(row)) && strstr(row,"tick=2 key=000000000007dd7c ordinal=0 thread=ui part=path offset=0 ") &&
           strstr(row,"bytes=0218524602000000000000000100000000000000"));
    assert(st_row(1*st_chunks+ST_CHUNKS+ST_SPLINE_CHUNKS+ST_FM_CHUNKS+ST_PT_CHUNKS,row,sizeof(row)) && strstr(row,"part=path offset=1700 ") && strstr(row,"bytes=")+6+0x100*2==strstr(row," banks="));
    assert(st_row(1*st_chunks+ST_CHUNKS+1+ST_SPLINE_CHUNKS+ST_FM_CHUNKS+ST_PT_CHUNKS,row,sizeof(row)) && strstr(row,"tick=2 key=000000000007dd7c ordinal=0 thread=ui part=settarget offset=0 ") &&
           strstr(row,"bytes=0119524602000000621b0301"));
    assert(st_row(1*st_chunks+st_chunks-1-ST_SP_CHUNKS-ST_HW_CHUNKS-ST_PS_CHUNKS,row,sizeof(row)) && strstr(row,"part=settarget offset=700 ") && strstr(row,"bytes=")+6+0x100*2==strstr(row," banks="));
    assert(st_row(1*st_chunks+st_chunks-1-ST_SP_CHUNKS-ST_HW_CHUNKS,row,sizeof(row)) && strstr(row,"part=pathset offset=2f00 ") && strstr(row,"bytes=")+6+0x100*2==strstr(row," banks="));
    assert(st_row(1*st_chunks+st_chunks-ST_SP_CHUNKS-ST_HW_CHUNKS,row,sizeof(row)) && strstr(row,"part=hwcap offset=0 ") && strstr(row,"bytes=00000000"));
    assert(st_row(1*st_chunks+st_chunks-1-ST_SP_CHUNKS,row,sizeof(row)) && strstr(row,"part=hwcap offset=1f00 ") && strstr(row,"bytes=")+6+0x100*2==strstr(row," banks="));
    assert(st_row(1*st_chunks+st_chunks-ST_SP_CHUNKS,row,sizeof(row)) && strstr(row,"part=spcap offset=0 ") && strstr(row,"bytes=00000000"));
    assert(st_row(1*st_chunks+st_chunks-1,row,sizeof(row)) && strstr(row,"part=spcap offset=3f00 ") && strstr(row,"bytes=")+6+0x100*2==strstr(row," banks="));
    assert(st_row(1*st_chunks+0,row,sizeof(row)) && strstr(row,"tick=2 key=000000000007dd7c ordinal=0 thread=ui part=args "));
    assert(st_row(1*st_chunks+ST_CHUNKS,row,sizeof(row)) && strstr(row,"tick=2 key=000000000007dd7c ordinal=0 thread=ui part=chain offset=0 ") &&
           strstr(row,"bytes=0138524601000000")); /* the tag, one builder call */
    assert(strstr(row,"bytes=")+6+ST_CHAIN_WORDS*8==strstr(row," banks="));
    /* FR9: n = 2, the header (k 1.0), the two starts, the two segments, the rest zero; the last chunk 0x80 bytes. */
    assert(!memcmp(r->spline,"\x02\0\0\0\0\0\0\0",8) && !memcmp(r->spline+8+8,"\0\0\x80\x3f",4) && !memcmp(r->spline+0x38,starts,16) &&
           !memcmp(r->spline+0x38+ST_SPLINE_SEGMENTS*8u,segs,sizeof(segs)) && !r->spline[ST_SPLINE_BYTES-1]);
    assert(st_row(1*st_chunks+ST_CHUNKS+1,row,sizeof(row)) && strstr(row,"part=spline offset=0 ") && strstr(row,"bytes=0200000000000000"));
    assert(st_row(1*st_chunks+ST_CHUNKS+4,row,sizeof(row)) && strstr(row,"part=spline offset=300 ") && strstr(row,"bytes=")+6+0x80*2==strstr(row," banks="));
    assert(!st_row(3*st_chunks,row,sizeof(row)));
    char status[1280];assert(st_status(status,sizeof(status)) && strstr(status,"installed=1 restored=0 ids=1 ticks=3 calls=8 watched_calls=3 records=3 dropped=0 stage=armed "
        "chain=armed chain_installed=1 chain_restored=0 chain_owned=1 chain_recorded=9 chain_no_slot=0 chain_hits=asin_target:8,asin_cur:8,builder:8"));
    /* FR24: the queue's calls through stub 13 (plain) and 14 (the propagation: frame rbp, leader r13): the watched ship's
     * kept (point, keys, site, O at frame-0x140, P), the other ship's not; the stand-in queue once each. */
    {static unsigned char pt[0x90] __attribute__((aligned(16))),lead[0x90] __attribute__((aligned(16))),frm[0x200] __attribute__((aligned(16)));
     for(unsigned i=0;i<0x90;i++) {pt[i]=(unsigned char)(0x20+i);lead[i]=(unsigned char)(0x60+i);}
     const uint64_t ref=9101;memcpy(pt+8,&ref,8);memcpy(lead+8,&ref,8);
     for(unsigned i=0;i<sizeof(frm);i++) frm[i]=(unsigned char)(0x90+i);
     const uint64_t si=stats.issued;const unsigned sp=boot_phase;stats.issued=2;boot_phase=3;memset((void*)st_fm,0,sizeof(st_fm));st_oc_kept=0;tsc_oc_calls=0;
     assert(((lxc_oc_fn)(void*)lxc_stub(13))(NULL,9102,pt,9101)==0x4242 && ((lxc_oc_fn)(void*)lxc_stub(13))(NULL,9101,pt,0)==0x4242);
     assert(test_lx_oc_fm_call(lxc_stub(14),9102,pt,frm+0x180,lead)==0x4242);
     assert(tsc_oc_calls==3 && st_oc_kept==2 && st_fm[0].ps_calls==2);
     static st_record_t orr;st_fm_take(0,&orr);const uint32_t *o=orr.pathset,*o2=orr.pathset+ST_PS_WORDS;uint64_t k;
     assert(o[0]==ST_OC_TAG && o[1]==2 && o[3]==0 && o[7]==1 && o[8]==LX_SC_OC1_SITE_VA && o[9]==0xffffffffu && o[44]==1);
     assert(!memcmp(o+48,pt+0x20,0x40) && !memcmp(o+64,pt,0x90));
     memcpy(&k,o+4,8);assert(k==0x7dd7c);memcpy(&k,o+40,8);assert(k==0x7dcd0);memcpy(&k,o+42,8);assert(k==0x7dcd0);
     assert(o2[0]==ST_OC_TAG && o2[3]==1 && o2[8]==LX_SC_OC_FM_SITE_VA && !memcmp(o2+100,frm+0x40,0x40) && !memcmp(o2+116,lead+0x20,0x40));
     memcpy(&k,o2+132,8);assert(k==0x7dcd0);memcpy(&k,o2+42,8);assert(k==0);
     assert(st_chain_hits[13]==2 && st_chain_hits[14]==1);
     stats.issued=si;boot_phase=sp;st_oc_kept=0;}
    /* FR25: the stores through their sites (the watched ship's pilot kept with the Value's position, another ship's and
     * an MD context not), the transform with the context in rbp (kept with the matrices and the object's level), the
     * zone's GetSafePos with the context in r12 reaching the solver through its entry (the capture: point before/after,
     * boxes, arguments; the other ship's gated only), the solver outside a gate counted only. */
    {assert(lxc_sp_entry_state()==1 && lxc_sp_entry_done && *LX(LX_SC_SP_SOLVER_VA)==0xe9);
     {int32_t d;memcpy(&d,LX(LX_SC_SP_SOLVER_VA)+1,4);assert(LX(LX_SC_SP_SOLVER_VA)+5+d==thunk+LXC_SP_ENTRY && LXC_SP_ENTRY==0xcc0 && LXC_SP_TRAMP==0xce0);}
     assert(!memcmp(thunk+LXC_SP_TRAMP,lxc_sp_entry_original,10) && !memcmp(thunk+LXC_SP_TRAMP+10,"\xff\x25\0\0\0\0",6));
     assert(!memcmp(lxc_stub(19),"\x49\x89\xe8\xff\x25",5) && !memcmp(lxc_stub(17),"\xff\x25",2) && !memcmp(lxc_stub(18),"\xff\x25",2));
     for(unsigned i=20;i<22;i++) {const void *g=(const void*)&st_sp_gate_gs;const unsigned char *s=lxc_stub(i);
         assert(!memcmp(s,"\x49\xbb",2) && !memcmp(s+2,&g,8) && !memcmp(s+10,"\x4d\x89\x23\x49\x89\x63\x08\xff\x25",9));}
     assert(lxc_sp_gs_ret[0]==(uintptr_t)LX(LX_SC_SP_GZ_SITE_VA+5) && lxc_sp_gs_ret[1]==(uintptr_t)LX(LX_SC_SP_GS_SITE_VA+5));
     assert(world_identity_record(9103,0x1234) && world_identity_record(9104,0x5678) && world_identity_record(9201,0x9201));
     static unsigned char ctxs[2][16] __attribute__((aligned(16))),contexts[2][0x200],pilots[2][0x80] __attribute__((aligned(16))),ctx_md[16];
     static unsigned char sval[16] __attribute__((aligned(16))),sobj[0x40] __attribute__((aligned(16))),obj[0x80] __attribute__((aligned(16))),anc[0x80] __attribute__((aligned(16)));
     for(unsigned i=0;i<2;i++) {
         memset(contexts[i],0,sizeof(contexts[i]));memset(pilots[i],0,sizeof(pilots[i]));
         const uint32_t tag=0x66;const uint64_t payload=(uint64_t)(uintptr_t)contexts[i]+ST_SP_AI_VALUE,pr=9200+i;
         memcpy(ctxs[i],&tag,4);memcpy(ctxs[i]+8,&payload,8);void *pe=pilots[i],*sh=ships[i];
         memcpy(contexts[i]+ST_SP_ENTITY,&pe,8);memcpy(pilots[i]+8,&pr,8);memcpy(pilots[i]+ST_OBJ_PARENT,&sh,8);
     }
     {const uint32_t tag=0x5d;memcpy(ctx_md,&tag,4);const uint64_t pl=(uint64_t)(uintptr_t)contexts[1]+ST_SP_AI_VALUE;memcpy(ctx_md+8,&pl,8);}
     const float spos[4]={-12450.6787f,-3728.83f,-34665.39f,0.0f};memset(sobj,0,sizeof(sobj));memcpy(sobj+0x20,spos,16);
     {const uint32_t tag=0x63;const uint64_t pl=(uint64_t)(uintptr_t)sobj;memset(sval,0,16);memcpy(sval,&tag,4);memcpy(sval+8,&pl,8);}
     memset(obj,0,sizeof(obj));memset(anc,0,sizeof(anc));{const uint64_t a=9103,b2=9104;void *par=ships[0];memcpy(obj+8,&a,8);memcpy(anc+8,&b2,8);memcpy(obj+ST_OBJ_PARENT,&par,8);}
     const uint64_t si=stats.issued;const unsigned sp=boot_phase;stats.issued=2;boot_phase=3;memset((void*)st_fm,0,sizeof(st_fm));
     st_sp_kept=st_sp_caps=st_sp_solver=st_sp_gated=st_sp_sequence=0;tsc_setvar_calls=0;tsc_hier_calls=0;
     const LONG64 h17=st_chain_hits[17],h18=st_chain_hits[18],h19=st_chain_hits[19];
     const lxc_setvar_fn cv=(lxc_setvar_fn)(void*)(LX(LX_SC_SP_CV_SITE_VA)-4),gv=(lxc_setvar_fn)(void*)(LX(LX_SC_SP_GV_SITE_VA)-4);
     assert(cv(NULL,ctxs[1],NULL,sval)==0x99 && cv(NULL,ctxs[0],NULL,sval)==0x99 && cv(NULL,ctx_md,NULL,sval)==0x99 && gv(NULL,ctxs[1],NULL,sval)==0x99);
     assert(tsc_setvar_calls==4 && st_chain_hits[17]==h17+3 && st_chain_hits[18]==h18+1 && st_sp_kept==2);
     float tm[16],t2[16];static unsigned char tmat[0x40] __attribute__((aligned(16)));
     for(unsigned i=0;i<16;i++) {tm[i]=0.5f*(float)i+0.25f;t2[i]=2.0f*tm[i]+1.0f;}
     memcpy(tmat,tm,0x40);test_lx_sp_tr_call(LX(LX_SC_SP_TR_SITE_VA)-4,obj,tmat,anc,ctxs[1],1.25);
     assert(!memcmp(tmat,t2,0x40) && st_chain_hits[19]==h19+1 && st_sp_kept==3 && tsc_hier_calls==1);
     test_lx_sp_tr_call(LX(LX_SC_SP_TR_SITE_VA)-4,obj,tmat,anc,ctxs[0],1.25);assert(st_sp_kept==3);  /* not watched */
     static unsigned char boxes[2*0x70] __attribute__((aligned(16))),bvec[0x18];static float spt[4] __attribute__((aligned(16))),sdir[4] __attribute__((aligned(16)));
     for(unsigned i=0;i<sizeof(boxes);i++) boxes[i]=(unsigned char)(i*5+3);
     {uint64_t bv[3]={(uint64_t)(uintptr_t)boxes,(uint64_t)(uintptr_t)(boxes+sizeof(boxes)),(uint64_t)(uintptr_t)(boxes+sizeof(boxes))};memcpy(bvec,bv,0x18);}
     const float p0[4]={12.5f,-3.25f,7.75f,0.0f},p1[4]={13.5f,-3.25f,7.75f,0.0f},d0[4]={0.6f,0.0f,0.8f,0.0f};memcpy(spt,p0,16);memcpy(sdir,d0,16);
     assert(tsc_zone_gsp(bvec,spt,sdir,NULL)==0x77 && spt[0]==13.5f && st_sp_solver==1 && !st_sp_gated);  /* no gate */
     memcpy(spt,p0,16);assert(test_lx_sp_gs_call(LX(LX_SC_SP_GZ_SITE_VA)-4,ctxs[0],bvec,spt,sdir)==0x77);    /* not watched */
     memcpy(spt,p0,16);assert(test_lx_sp_gs_call(LX(LX_SC_SP_GZ_SITE_VA)-4,ctxs[1],bvec,spt,sdir)==0x77 && !memcmp(spt,p1,16));
     assert(st_sp_solver==3 && st_sp_gated==2 && st_sp_caps==1 && st_fm[0].sp_calls==1);
     static st_record_t spr;st_fm_take(0,&spr);const uint32_t *v0=spr.pathset,*v1=v0+ST_PS_WORDS,*v2=v0+2*ST_PS_WORDS,*c0=spr.spcap;uint64_t k;
     assert(v0[0]==ST_SP_TAG && v0[1]==2 && v0[2]==0 && v0[3]==0 && v0[6]==0 && v0[7]==1 && v0[11]==1 && v0[16]==0x63 && !memcmp(v0+20,spos,16) && v0[ST_PS_WORDS-1]==3);
     memcpy(&k,v0+4,8);assert(k==0x7dd7c);memcpy(&k,v0+8,8);assert(k==0x9201);
     assert(v1[0]==ST_SP_TAG && v1[3]==1 && v1[6]==1 && v1[10]==v0[10]+1 && !memcmp(v1+20,spos,16));
     {double tt;memcpy(&tt,v2+20,8);assert(v2[0]==ST_SP_TAG && v2[3]==2 && v2[6]==2 && v2[22]==0x5a && tt==1.25 && !memcmp(v2+24,tm,0x40) && !memcmp(v2+40,t2,0x40));
      memcpy(&k,v2+16,8);assert(k==0x1234);memcpy(&k,v2+18,8);assert(k==0x5678);memcpy(&k,v2+56,8);assert(k==0x1234 && v2[58]==1);
      float lv[16]={0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0};for(unsigned i=0;i<16;i++) lv[i]=2.0f*lv[i]+(float)i+0.25f;
      assert(!memcmp(v2+60,lv,0x40));memcpy(&k,v2+76,8);assert(k==0x7dcd0 && !v2[78]);}
     for(unsigned i=3*ST_PS_WORDS;i<ST_PS_ENTRIES*ST_PS_WORDS;i++) assert(!v0[i]);
     {float r50=50.0f,a15=1.5f,a25=2.5f;uint32_t u;
      assert(c0[0]==ST_SP_CAP_TAG && c0[1]==2 && c0[3]==0 && c0[6]==0 && c0[7]==2 && c0[8]==2 && c0[9]==1 && c0[30]==0 && c0[31]==1 && c0[33]==0);
      memcpy(&u,&r50,4);assert(c0[28]==u);memcpy(&u,&a15,4);assert(c0[29]==u);memcpy(&u,&a25,4);assert(c0[32]==u);
      assert(!memcmp(c0+12,p0,16) && !memcmp(c0+16,p1,16) && !memcmp(c0+20,d0,16) && c0[24]==0xffffffffu && !memcmp(c0+ST_SP_BOX_FIRST,boxes,sizeof(boxes)));
      memcpy(&k,c0+4,8);assert(k==0x7dd7c);memcpy(&k,c0+34,8);assert(k==0x9201 && c0[36]==1 && c0[ST_SP_WORDS-1]==1);
      for(unsigned i=ST_SP_WORDS;i<ST_SP_CAPS*ST_SP_WORDS;i++) assert(!c0[i]);}
     stats.issued=si;boot_phase=sp;st_sp_kept=st_sp_caps=st_sp_solver=st_sp_gated=0;}
    /* Ownership: a changed rel32 is seen; then the restoration, byte-exact, the entry first. */
    unsigned char *b=LX(LX_SC_BUILDER_SITE_VA);memcpy(&rel,b+1,4);const int32_t moved=rel+1;tl_write(b+1,&moved,4);assert(!st_chain_owned());tl_write(b+1,&rel,4);
    assert(st_chain_owned());
    st_restore();memcpy(&entry,LX(LX_ST_SLOT_VA),8);
    assert(!st_installed && st_restored && entry==step && !st_chain_installed && st_chain_restored && !strcmp(st_chain_stage,"restored"));
    assert(!st_sg_installed && *(void**)LX(LX_SG_SLOT_VA)==sg_fn && !st_hw_installed && *(void**)LX(LX_HW_SLOT_VA)==hw_fn);
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) assert(lxc_site_state(i)==0 && !memcmp(lines[i],(void*)((uintptr_t)LX(lxc_sites[i].site+1)&~(uintptr_t)15),16));
    assert(!lxc_sp_entry_done && lxc_sp_entry_state()==0 && !memcmp(LX(LX_SC_SP_SOLVER_VA),lxc_sp_entry_original,10));
    {uintptr_t page=(uintptr_t)LX(LX_ST_SLOT_VA)&~(uintptr_t)(GATE_PAGE-1);assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE));
     memcpy(LX(LX_ST_SLOT_VA),&saved_entry,8);memcpy(LX(LX_SG_SLOT_VA),&saved_sg,8);assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE));}
    {uintptr_t page=(uintptr_t)LX(LX_HW_SLOT_VA)&~(uintptr_t)(GATE_PAGE-1);assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE));
     memcpy(LX(LX_HW_SLOT_VA),&saved_hw,8);assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE));}
    st_id_count=0;st_chain_wanted=0;st_chain_restored=0;st_chain_stage="off";st_chunks=ST_CHUNKS;world_identity_installed=0;world_identity_count=0;
    printf("step chain (FR8) sites/flow/rows/restore PASS\n");
}
/* The executable's bytes: the three calls and the facts their hooks rely on, the swaps and their exact restoration. */
static void test_step_chain_elf(void) {
    unsigned char lines[ST_CHAIN_SITE_COUNT][16];
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) {
        const uintptr_t at=(uintptr_t)LX(lxc_sites[i].site+1);
        assert(lxc_site_state(i)==0 && (at&~(uintptr_t)15)==((at+3)&~(uintptr_t)15));memcpy(lines[i],(void*)(at&~(uintptr_t)15),16);
    }
    /* The current pitch kept at [rsp+0x100] and the target at [rsp+0x130]; the pitch approach: dt [rsp+0x80] times the
     * rate [r12+0x37c]; the builder's rows lea rdi,[r15+0x10]; the builder writes rsi before it reads it. */
    assert(!memcmp(LX(0x13edcd2),"\xf3\x0f\x11\x84\x24\x00\x01\x00\x00",9) && !memcmp(LX(0x13edc46),"\xf3\x0f\x11\x84\x24\x30\x01\x00\x00",9));
    assert(!memcmp(LX(0x13eddf3),"\xf3\x0f\x10\x84\x24\x80\x00\x00\x00",9) && !memcmp(LX(0x13ede00),"\xf3\x41\x0f\x59\x84\x24\x7c\x03\x00\x00",10));
    assert(!memcmp(LX(0x13edeb0),"\x49\x8d\x7f\x10",4) && !memcmp(LX(0x1e02226),"\x4c\x89\xe6",3));
    /* FR17: the members at formation+0x48/+0x50 and the shape at +0x60; the leader pose a copy of the read bank at
     * agent+0xaf0 (the bank index times 0x1a0); the wrapper's formation agent+0xf40 and index +0xf48. */
    assert(!memcmp(LX(0x1e592e4),"\x48\x8b\x4f\x48\x48\x8b\x47\x50",8) && !memcmp(LX(0x1e59367),"\x48\x8b\x6d\x60",4));
    assert(!memcmp(LX(0x1e5931c),"\x4c\x8b\x28\x4c\x89\xee\xe8",7) && !memcmp(LX(0x1427924),"\x66\x0f\x6f\x80\xf0\x0a\x00\x00",8));
    assert(!memcmp(LX(0x14285e8),"\x48\x8b\xbe\x40\x0f\x00\x00",7) && !memcmp(LX(0x14285f8),"\x48\x8b\x96\x48\x0f\x00\x00",7));
    /* FR18: each converter call takes the point [rbx+0x6d0] (rbx the agent) and out rbp, the frame calls r14 as the frame;
     * the local transform reads the mat4 at +0x20 and the sub id at +0x10; the point's reference id at +0x8 (0x14312d0);
     * the frame conversion calls the local transform first, then the conversion (rdi out, esi 0, rdx &ref, rcx &frame,
     * r8d 1). */
    assert(!memcmp(LX(0x1431ac0),"\x48\x8b\xb3\xd0\x06\x00\x00\x48\x89\xef",10));
    assert(!memcmp(LX(0x14326c7),"\x48\x8b\xb3\xd0\x06\x00\x00\x4c\x89\xf2\x48\x89\xef",13) && !memcmp(LX(0x14326de),"\x48\x8b\xb3\xd0\x06\x00\x00\x4c\x89\xf2\x48\x89\xef",13));
    assert(!memcmp(LX(0x14bd2db),"\x66\x0f\x6f\x46\x20\x48\x83\x7e\x10\x00",10) && !memcmp(LX(0x14312d0),"\x4d\x8b\x6e\x08",4));
    /* FR19: the Linear vtable's SetTarget entry and SetTarget's copy of the mat4 to +0x10, a3 to +0x50. */
    {uint64_t v;memcpy(&v,LX(LX_SG_SLOT_VA),8);assert(v==LX_SG_FN_VA && !memcmp(LX(LX_SG_FN_VA),"\x0f\x28\x06\x48\x85\xc9\x0f\x29\x47\x10",10) && !memcmp(LX(0x141d161),"\x48\x89\x57\x50",4));}
    assert(!memcmp(LX(0x141fa61),"\xe8\x6a\xd8\x09\x00",5) && !memcmp(LX(0x141fa91),"\x31\xf6\x48\x89\xef\x48\x8d\x4c\x24\x08\x48\x8d\x54\x24\x10\x41\xb8\x01\x00\x00\x00",21));
    /* FR20: 0x143235b takes the point [rbx+0x6d0], frame r14, out rbp; 0x1432611 the object r15, &mat [rsp+8], frame
     * r14, ecx 0; the hierarchy function begins push r15; push r14. */
    assert(!memcmp(LX(0x143234e),"\x48\x8b\xb3\xd0\x06\x00\x00\x4c\x89\xf2\x48\x89\xef",13));
    assert(!memcmp(LX(0x1432604),"\x48\x8b\x74\x24\x08\x31\xc9\x4c\x89\xf2\x4c\x89\xff",13) && !memcmp(LX(LX_SC_HIER_VA),"\x41\x57\x41\x56",4));
    /* FR21: the Highway vtable's entry holds the point map; the map's banking call takes t [rbx+8] in xmm0 and the
     * highway (rbp) in rdi; the map reads the spline at +0x5e8. */
    {uint64_t v;memcpy(&v,LX(LX_HW_SLOT_VA),8);assert(v==LX_HW_FN_VA && !memcmp(LX(0x15cf8e0),"\x48\x8b\xbf\xe8\x05\x00\x00",7));}
    assert(!memcmp(LX(0x15cfa40),"\xf3\x0f\x10\x43\x08\x48\x89\xef",8));
    /* FR25: the stores take (rdi variable, rsi the context: rbp from rdx, rdx name, rcx the Value); the CreatePosition
     * evaluation keeps the context in rbp (from rsi) and calls the transform (rdi r15, rsi the matrix, rdx r13, ecx 0,
     * xmm0 the time); the GetSafePos evaluation keeps it in r12 (from rsi); the solver begins with the movabs the
     * trampoline carries, its entry line aligned. */
    assert(!memcmp(LX(0xde10c3),"\x48\x89\xd5",3) && !memcmp(LX(0xde1102),"\x48\x8d\xbb\xf8\x00\x00\x00\x4c\x89\xe1\x48\x89\xee\x48\x8d\x93\x08\x01\x00\x00\xe8",21));
    assert(!memcmp(LX(0xde5643),"\x48\x89\xd5",3) && !memcmp(LX(0xde56a3),"\x48\x8d\xbb\xb8\x01\x00\x00\x48\x89\xe1\x48\x89\xee\x48\x8d\x93\xc8\x01\x00\x00\xe8",21));
    assert(!memcmp(LX(0xd04a5c),"\x48\x89\xf5",3) && !memcmp(LX(0xd04bf2),"\x48\x8d\x74\x24\x50\x31\xc9\x4c\x89\xea\x4c\x89\xff",13));
    assert(!memcmp(LX(0xd05ae8),"\x49\x89\xf4",3) && !memcmp(LX(LX_SC_SP_SOLVER_VA),lxc_sp_entry_original,10) && !((uintptr_t)LX(LX_SC_SP_SOLVER_VA)&15));
    thunk=make_thunk();assert(thunk);st_chain_installed=0;st_chain_restored=0;
    assert(st_chain_arm() && st_chain_installed && st_chain_owned());
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) assert(lxc_site_state(i)==1);
    assert(lxc_sp_entry_state()==1 && *LX(LX_SC_SP_SOLVER_VA)==0xe9 && !memcmp(LX(LX_SC_SP_SOLVER_VA)+5,lxc_sp_entry_original+5,5));
    st_chain_restore();assert(!st_chain_installed && st_chain_restored && !lxc_sp_entry_done && !memcmp(LX(LX_SC_SP_SOLVER_VA),lxc_sp_entry_original,10));
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) assert(lxc_site_state(i)==0 && !memcmp(lines[i],(void*)((uintptr_t)LX(lxc_sites[i].site+1)&~(uintptr_t)15),16));
    st_chain_restored=0;st_chain_stage="off";
    printf("step chain (FR8) on the executable's bytes PASS\n");
}
