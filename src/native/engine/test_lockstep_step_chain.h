/* FR8 on the executable's bytes (lockstep_step_chain_win.h), in a private copy: the pins (a flipped byte refuses),
 * the six swaps and the view (raw bytes ours, hash_ok/read_local the file's, the world trace's pin of the whole step
 * still passing), then the hooks entered through their stubs: outside a watched step only counted; inside one (this
 * thread marked) recorded, with the approach helper 0xeb1db0 run on its own bytes against the reading (period 0: no
 * wrap) and asinf and the builder as stand-ins; then the exact restoration. Included by test_lockstep_gate.c. */
static float tsc_asinf(float x) {return x*0.5f;}
static void tsc_builder(void *bank,float yaw,float pitch,float roll) {
    float rows[12];for(unsigned i=0;i<12;i++) rows[i]=yaw*(float)i+pitch-roll;
    memcpy((unsigned char*)bank+0x10,rows,sizeof(rows));
}
static void tsc_jump(uint32_t rva,const void *target) {unsigned char code[14]={0xff,0x25,0,0,0,0};memcpy(code+6,&target,8);memcpy(image+rva,code,14);}
/* FR17's stand-in GetTargetPositionForWingMan: out[i] = 10*index + i + time; returns 1 in al (garbage above). */
static uint64_t tsc_fm(void *formation,void *out,uint64_t index,double time) {
    (void)formation;float o[16];for(unsigned i=0;i<16;i++) o[i]=(float)(10*index+i)+(float)time;
    memcpy(out,o,sizeof(o));return 0x7700u|1u;
}
/* FR18's stand-ins: the converter copies the point's mat4 (+0x20) to out and runs the conversion through its stub with
 * the reference object tsc_pt_ref and its frame argument; the conversion adds 100*flag to every lane. */
static void *tsc_pt_rcx;static unsigned char *tsc_pt_ref;
static uint64_t tsc_pt(void *rcx,void *out,void *point,void *frame,void *a5,void *a6) {
    (void)a5;(void)a6;tsc_pt_rcx=rcx;memcpy(out,(unsigned char*)point+0x20,0x40);
    void *r=tsc_pt_ref,*f=frame;((stc_pt_math_fn)(void*)(stc_page+stc_stub(9)))(out,NULL,&r,&f,0x7700u|1u);
    return 0x5500u|1u;
}
static uint64_t tsc_pt_math(void *out,void *velocity,void *ref,void *frame,uint64_t flag) {
    (void)velocity;(void)ref;(void)frame;float m[16];memcpy(m,out,0x40);
    for(unsigned i=0;i<16;i++) m[i]+=100.0f*(float)(flag&0xff);
    memcpy(out,m,0x40);return 0;
}
/* FR20's stand-ins: 0xe8bd20 copies the point's mat4 to out and runs the conversion through stub 9 (reference
 * tsc_pt_ref); the hierarchy function 0x3dde10 maps m to 2m + i + 0.25 lane by lane; the path builder 0xe86bb0 points
 * the agent at tsc_ps_point and returns 0x77. */
static void *tsc_convert(void *out,void *point,void *frame) {
    memcpy(out,(unsigned char*)point+0x20,0x40);void *r=tsc_pt_ref,*f=frame;
    ((stc_pt_math_fn)(void*)(stc_page+stc_stub(9)))(out,NULL,&r,&f,1);return out;
}
static unsigned tsc_hier_calls;
static void tsc_hier(void *from,void *inout,void *to,uint64_t zero) {
    (void)from;(void)to;assert(!zero);++tsc_hier_calls;float m[16];memcpy(m,inout,0x40);
    for(unsigned i=0;i<16;i++) m[i]=2.0f*m[i]+(float)i+0.25f;
    memcpy(inout,m,0x40);
}
static unsigned char *tsc_ps_point;
static uint64_t tsc_ps(void *agent,void *desc,void *a3,void *a4) {(void)desc;(void)a3;(void)a4;memcpy((unsigned char*)agent+ST_AGENT_POINT,&tsc_ps_point,8);return 0x77;}
/* 0xe8c380's side of FR20's calls: push r15; mov r15,r9; xor r9d,r9d; sub rsp,0x20; mov rax,stub; call rax; add rsp,0x20;
 * pop r15; ret (the agent only in r15). */
typedef void *(*tsc_w_caller_fn)(void *,void *,void *,void *agent);
static unsigned char *tsc_w_caller(void *stub) {
    static const unsigned char head[]={0x41,0x57,0x4d,0x89,0xcf,0x45,0x31,0xc9,0x48,0x83,0xec,0x20,0x48,0xb8},tail[]={0xff,0xd0,0x48,0x83,0xc4,0x20,0x41,0x5f,0xc3};
    unsigned char *p=VirtualAlloc(NULL,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);assert(p);
    memcpy(p,head,sizeof(head));memcpy(p+sizeof(head),&stub,8);memcpy(p+sizeof(head)+8,tail,sizeof(tail));
    FlushInstructionCache(GetCurrentProcess(),p,0x1000);return p;
}
/* 0xe8c380's side of the call: push r15; mov r15,rcx; sub rsp,0x30; the caller's fifth and sixth arguments to
 * [rsp+0x20]/[rsp+0x28]; mov rax,stub; call rax; add rsp,0x30; pop r15; ret. */
typedef uint64_t (*tsc_pt_caller_fn)(void *agent,void *out,void *point,void *frame,void *a5,void *a6);
static unsigned char *tsc_pt_caller(void *stub) {
    static const unsigned char head[]={0x41,0x57,0x49,0x89,0xcf,0x48,0x83,0xec,0x30,0x48,0x8b,0x44,0x24,0x60,0x48,0x89,0x44,0x24,0x20,
        0x48,0x8b,0x44,0x24,0x68,0x48,0x89,0x44,0x24,0x28,0x48,0xb8},tail[]={0xff,0xd0,0x48,0x83,0xc4,0x30,0x41,0x5f,0xc3};
    unsigned char *p=VirtualAlloc(NULL,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);assert(p);
    memcpy(p,head,sizeof(head));memcpy(p+sizeof(head),&stub,8);memcpy(p+sizeof(head)+8,tail,sizeof(tail));
    FlushInstructionCache(GetCurrentProcess(),p,0x1000);return p;
}
/* The helper as read: |cur-t| < 1e-4 -> t; cur >= t -> max(cur - rate*dt, t); else min(rate*dt + cur, t). */
/* FR21's stand-ins: the Highway's map calls the banking through stub 15 and adds its result to the position; the
 * banking returns 2t + 1; the path clear moves the agent's point to tsc_ps_point. */
static float tsc_bank(void *hw,float t) {(void)hw;return 2.0f*t+1.0f;}
static void tsc_hw(void *hw,void *mat) {
    float m[16];memcpy(m,mat,0x40);
    m[0]+=((float (*)(void *,float))(void*)(stc_page+stc_stub(STC_HW_SITE)))(hw,0.25f);memcpy(mat,m,0x40);
}
static uint64_t tsc_clear(void *agent,void *flag,void *a3,void *a4,void *a5,void *a6) {
    (void)flag;(void)a3;(void)a4;(void)a5;(void)a6;memcpy((unsigned char*)agent+ST_AGENT_POINT,&tsc_ps_point,8);return 0x66;
}
/* FR22: 0xe89ed0's stand-in appends the point to the vector it is handed (the agent's CURRENT points, +0x6a8). */
static __declspec(align(16)) unsigned char tsc_vec_store[3*0x90];
static uint64_t tsc_vec(void *vec,void *point,void *a3,void *a4,void *a5,void *a6) {
    (void)a3;(void)a4;(void)a5;(void)a6;unsigned char *begin,*end;memcpy(&begin,vec,8);memcpy(&end,(unsigned char*)vec+8,8);
    memcpy(end,point,0x90);end+=0x90;memcpy((unsigned char*)vec+8,&end,8);return 0x55;
}
static float tsc_approach(float cur,float t,float rate,float dt) {
    if(!(fabsf(cur-t)>=1e-4f)) return t;
    volatile float step=rate*dt;
    if(cur>=t) {const float x=cur-step;return x>t?x:t;}
    const float x=step+cur;return x<t?x:t;
}
/* FR24: callers for the creation stubs: test_oc_q_call(stub, dst, src, ship, issuer) puts the ship in rdi and the
 * issuer in rbp as the queue has them at 0xeadf86; test_oc_fm_call(stub, dst, ship, frame) copies 0x210 bytes of frame
 * to its rsp and puts the ship in r12 as the propagation has them at 0xe99ce9 (rdx 0). */
__asm__(
".text\n"
"test_oc_q_call:\n push %rbp\n push %rdi\n push %rbx\n mov %rsp,%rbx\n mov 0x40(%rsp),%rbp\n mov %r9,%rdi\n"
" mov %rcx,%rax\n mov %rdx,%rcx\n mov %r8,%rdx\n and $-16,%rsp\n sub $0x20,%rsp\n call *%rax\n mov %rbx,%rsp\n pop %rbx\n pop %rdi\n pop %rbp\n ret\n"
"test_oc_fm_call:\n push %rbp\n push %r12\n push %rsi\n push %rdi\n mov %rsp,%rbp\n sub $0x240,%rsp\n and $-16,%rsp\n mov %r8,%r12\n"
" mov %rcx,%rax\n mov %r9,%rsi\n mov %rsp,%rdi\n mov $0x42,%ecx\n rep movsq\n mov %rdx,%rcx\n xor %edx,%edx\n call *%rax\n"
" mov %rbp,%rsp\n pop %rdi\n pop %rsi\n pop %r12\n pop %rbp\n ret\n"
);
extern void *test_oc_q_call(const void *stub,void *dst,const void *src,uint64_t ship,uint64_t issuer);
extern void *test_oc_fm_call(const void *stub,void *dst,uint64_t ship,const void *frame);
/* FR25: callers for the gate stubs: test_sp_tr_call(stub, self, mat, anc, ctx, flag, time) puts the context in r12 and
 * calls the transform's stub with rcx self, rdx mat, xmm2 time, r9 anc, [rsp+0x20] flag (the call returns to
 * test_sp_tr_ret); test_sp_gs_call(stub, ctx, boxes, p, dir, move) puts the context in r12 and calls a GetSafePos stub
 * with rcx boxes, rdx p, r8 dir, r9 move (returns to test_sp_gs_ret). */
__asm__(
".text\n"
".globl test_sp_tr_ret\n.globl test_sp_gs_ret\n"
"test_sp_tr_call:\n push %r12\n push %rbx\n mov %rcx,%rax\n mov %rdx,%rcx\n mov %r8,%rdx\n mov 0x38(%rsp),%r12\n mov 0x40(%rsp),%rbx\n"
" movsd 0x48(%rsp),%xmm2\n sub $0x28,%rsp\n mov %rbx,0x20(%rsp)\n call *%rax\n"
"test_sp_tr_ret:\n add $0x28,%rsp\n pop %rbx\n pop %r12\n ret\n"
"test_sp_gs_call:\n push %r12\n push %rbx\n mov %rcx,%rax\n mov %rdx,%r12\n mov %r8,%rcx\n mov %r9,%rdx\n mov 0x38(%rsp),%r8\n"
" mov 0x40(%rsp),%r9\n sub $0x28,%rsp\n call *%rax\n"
"test_sp_gs_ret:\n add $0x28,%rsp\n pop %rbx\n pop %r12\n ret\n"
);
extern void test_sp_tr_call(const void *stub,void *self,void *mat,void *anc,const void *ctx,uint64_t flag,double time);
extern void test_sp_gs_call(const void *stub,const void *ctx,void *boxes,float *p,float *dir,float *move);
extern char test_sp_tr_ret[],test_sp_gs_ret[];
/* FR25's stand-ins: the variable store counts and answers 0x99; the transform maps m to 2m + 1 when the flag is 0x5a;
 * Zone::GetSafePos (reached through stub 31's jump) runs the solver on its own bytes through the entry jump. */
static unsigned tsc_setvar_calls;
static uint64_t tsc_setvar(void *var,void *ctx,void *name,void *value) {(void)var;(void)ctx;(void)name;(void)value;++tsc_setvar_calls;return 0x99;}
static void tsc_gtr(void *self,void *mat,double time,void *anc,uint64_t flag) {
    (void)self;(void)anc;assert((flag&0xff)==0x5a && time==1.25);float m[16];memcpy(m,mat,0x40);
    for(unsigned i=0;i<16;i++) m[i]=2.0f*m[i]+1.0f;
    memcpy(mat,m,0x40);
}
static uint64_t tsc_zone_gsp(void *boxes,float *p,float *dir,float *move) {
    return ((stc_solver_fn)(void*)(image+STC_SP_SOLVER))(boxes,p,50.0f,dir,1.5f,0,1,move,2.5f);
}
/* FR24's stand-in copies: the queue's copies the point (0x90 bytes), the propagation's leaves it. */
static void *tsc_oc_copy(void *dst,const void *src) {memcpy(dst,src,0x90);return dst;}
static void *tsc_oc_finish(void *dst,const void *tmpl) {(void)tmpl;return dst;}
static void test_step_chain_native_image(const char *path) {
    FILE *file=fopen(path,"rb");assert(file);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    assert(fread(&dos,1,sizeof(dos),file)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && !fseek(file,dos.e_lfanew,SEEK_SET));
    assert(fread(&nt,1,sizeof(nt),file)==sizeof(nt) && nt.OptionalHeader.SizeOfImage==0x6fa9000);
    unsigned char *prior=image;image=VirtualAlloc(NULL,nt.OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(image);
    for(unsigned i=0;i<nt.FileHeader.NumberOfSections;i++) {
        IMAGE_SECTION_HEADER section;assert(fread(&section,1,sizeof(section),file)==sizeof(section));long next=ftell(file);
        if(section.SizeOfRawData) {assert(!fseek(file,section.PointerToRawData,SEEK_SET) && fread(image+section.VirtualAddress,1,section.SizeOfRawData,file)==section.SizeOfRawData);}
        assert(!fseek(file,next,SEEK_SET));
    }
    fclose(file);
    unsigned char *saved_page=stc_page;stc_page=NULL;stc_done=0;st_chain_installed=0;st_chain_restored=0;
    static unsigned char block[STC_BLOCK_SIZE],seen[STC_BLOCK_SIZE];memcpy(block,image+STC_BLOCK,STC_BLOCK_SIZE);
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) assert(stc_site_state(i)==0);
    image[STC_BLOCK+0x20]^=1;assert(!st_chain_arm() && !strcmp(st_chain_stage,"pins"));image[STC_BLOCK+0x20]^=1;
    image[STC_HELPER+0x30]^=1;assert(!st_chain_arm() && !strcmp(st_chain_stage,"pins"));image[STC_HELPER+0x30]^=1;
    image[stc_sites[3].site+2]^=1;assert(!st_chain_arm() && !strcmp(st_chain_stage,"pins"));image[stc_sites[3].site+2]^=1;
    image[STC_FM+0x70]^=1;assert(!st_chain_arm() && !strcmp(st_chain_stage,"pins"));image[STC_FM+0x70]^=1;
    image[STC_FM_WRAPPER+0x10]^=1;assert(!st_chain_arm() && !strcmp(st_chain_stage,"pins"));image[STC_FM_WRAPPER+0x10]^=1;
    image[stc_sites[7].site+3]^=1;assert(!st_chain_arm() && !strcmp(st_chain_stage,"pins"));image[stc_sites[7].site+3]^=1;
    image[STC_PT+0x100]^=1;assert(!st_chain_arm() && !strcmp(st_chain_stage,"pins"));image[STC_PT+0x100]^=1;
    image[STC_PT_CONVERT+0x20]^=1;assert(!st_chain_arm() && !strcmp(st_chain_stage,"pins"));image[STC_PT_CONVERT+0x20]^=1;
    image[STC_PT_CALLER+0x10]^=1;assert(!st_chain_arm() && !strcmp(st_chain_stage,"pins"));image[STC_PT_CALLER+0x10]^=1;
    image[stc_sites[8].site+2]^=1;assert(!st_chain_arm() && !strcmp(st_chain_stage,"pins"));image[stc_sites[8].site+2]^=1;
    image[stc_sites[9].site+4]^=1;assert(!st_chain_arm() && !strcmp(st_chain_stage,"pins"));image[stc_sites[9].site+4]^=1;
    image[STC_PT_CALLER2+0x40]^=1;assert(!st_chain_arm() && !strcmp(st_chain_stage,"pins"));image[STC_PT_CALLER2+0x40]^=1;
    image[STC_HIER+0x200]^=1;assert(!st_chain_arm() && !strcmp(st_chain_stage,"pins"));image[STC_HIER+0x200]^=1;
    image[STC_PS+0x300]^=1;assert(!st_chain_arm() && !strcmp(st_chain_stage,"pins"));image[STC_PS+0x300]^=1;
    image[STC_PS_CALLER+0x8]^=1;assert(!st_chain_arm() && !strcmp(st_chain_stage,"pins"));image[STC_PS_CALLER+0x8]^=1;
    for(unsigned i=10;i<ST_CHAIN_SITE_COUNT;i++) {image[stc_sites[i].site+1]^=1;assert(!st_chain_arm() && !strcmp(st_chain_stage,"pins"));image[stc_sites[i].site+1]^=1;}
    for(unsigned i=0;i<5u;i++) {image[stc_pc_pins[i].rva+stc_pc_pins[i].size-1]^=1;assert(!st_chain_arm() && !strcmp(st_chain_stage,"pins"));image[stc_pc_pins[i].rva+stc_pc_pins[i].size-1]^=1;}
    for(unsigned i=0;i<4u;i++) {image[stc_oc_pins[i].rva+stc_oc_pins[i].size-2]^=1;assert(!st_chain_arm() && !strcmp(st_chain_stage,"pins"));image[stc_oc_pins[i].rva+stc_oc_pins[i].size-2]^=1;}
    for(unsigned i=0;i<6u;i++) {image[stc_sp_pins[i].rva+stc_sp_pins[i].size-2]^=1;assert(!st_chain_arm() && !strcmp(st_chain_stage,"pins"));image[stc_sp_pins[i].rva+stc_sp_pins[i].size-2]^=1;}
    assert(stc_sp_entry_state()==0);
    assert(!stc_page && !st_chain_installed);
    /* The stand-ins (called, never read: not pinned) and the helper executable in this copy. */
    DWORD old;
    assert(VirtualProtect(image+(STC_ASINF&~0xfffu),0x2000,PAGE_EXECUTE_READWRITE,&old) && VirtualProtect(image+(STC_BUILDER&~0xfffu),0x2000,PAGE_EXECUTE_READWRITE,&old));
    tsc_jump(STC_ASINF,(void*)tsc_asinf);tsc_jump(STC_BUILDER,(void*)tsc_builder);
    assert(VirtualProtect(image+(STC_HELPER&~0xfffu),0x1000,PAGE_EXECUTE_READWRITE,&old));
    FlushInstructionCache(GetCurrentProcess(),image,nt.OptionalHeader.SizeOfImage);
    assert(st_chain_arm() && st_chain_installed && stc_done==(1ull<<ST_CHAIN_SITE_COUNT)-1ull && stc_sp_entry_done && !strcmp(st_chain_stage,"armed") && stc_page);
    /* FR25: the solver's entry jumps to its stub (raw), the view shows the file's bytes (the pin passes), the stubs. */
    assert(stc_sp_entry_state()==1 && image[STC_SP_SOLVER]==0xe9 && hash_ok(image+stc_sp_pins[5].rva,stc_sp_pins[5].size,stc_sp_pins[5].sha));
    {unsigned char seen5[5];assert(read_local(image+STC_SP_SOLVER,seen5,5) && !memcmp(seen5,stc_sp_entry_original,5));}
    assert(hash_ok(image+stc_sp_pins[2].rva,stc_sp_pins[2].size,stc_sp_pins[2].sha) && hash_ok(image+stc_sp_pins[4].rva,stc_sp_pins[4].size,stc_sp_pins[4].sha));
    for(unsigned i=STC_SP_TR_SITE;i<=STC_SP_GC_SITE;i++) {
        const void *gate=i==STC_SP_TR_SITE?(const void*)&st_sp_gate_tr:(const void*)&st_sp_gate_gs,*to=i==STC_SP_TR_SITE?(const void*)stc_sp_tr:(const void*)(image+stc_sites[i].target);
        const unsigned char *s=stc_page+stc_stub(i);
        assert(!memcmp(s,"\x49\xbb",2) && !memcmp(s+2,&gate,8) && !memcmp(s+10,"\x4d\x89\x23\x49\x89\x63\x08\xff\x25\0\0\0\0",13) && !memcmp(s+23,&to,8));
    }
    assert(!memcmp(stc_page+stc_stub(STC_SP_CV_SITE),"\xff\x25\0\0\0\0",6) && !memcmp(stc_page+stc_stub(STC_SP_GV_SITE),"\xff\x25\0\0\0\0",6));
    {void *h=(void*)stc_sp_solver,*resume=image+STC_SP_SOLVER_RESUME;
     assert(!memcmp(stc_page+STC_SP_ENTRY_STUB,"\xff\x25\0\0\0\0",6) && !memcmp(stc_page+STC_SP_ENTRY_STUB+6,&h,8));
     assert(!memcmp(stc_page+STC_SP_TRAMP,"\x48\x8b\xc4\x48\x89\x58\x18\xff\x25\0\0\0\0",13) && !memcmp(stc_page+STC_SP_TRAMP+13,&resume,8));
     int32_t d;memcpy(&d,image+STC_SP_SOLVER+1,4);assert(image+STC_SP_SOLVER+5+d==stc_page+STC_SP_ENTRY_STUB);}
    /* FR25's facts: the variable stores take (rcx var, rdx the context, r8 name, r9 the Value); the evaluations keep the
     * context in r12 (0xc6f7ed mov r12,r8; 0xc723b8 likewise, 0xc72b15 reloads it from its home); the solver's eighth
     * argument is the move direction (r12 = [rbp+0xf8]), its first instructions the two the trampoline carries. */
    assert(!memcmp(image+0xbe0c77,"\x4c\x8d\x46\x10\x4c\x8d\x4c\x24\x30\x48\x8b\xd5\x48\x8b\xce\xe8",16) && !memcmp(image+0xbe0bb4,"\x49\x8b\xe8",3));
    assert(!memcmp(image+0xbe239b,"\x4c\x8d\x43\x10\x4c\x8d\x4c\x24\x40\x48\x8b\xd7\x48\x8b\xcb\xe8",16) && !memcmp(image+0xbe232f,"\x49\x8b\xf8",3));
    assert(!memcmp(image+0xc6f7ed,"\x4d\x8b\xe0",3) && !memcmp(image+0xc723b8,"\x4d\x8b\xe0",3) && !memcmp(image+0xc72b15,"\x4c\x8b\xa5\x40\x01\x00\x00",7));
    assert(!memcmp(image+0x14e4353,"\x4c\x8b\xa5\xf8\x00\x00\x00",7) && !memcmp(image+STC_SP_SOLVER+5,"\x58\x18\x4c\x89\x48\x20",6));
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) assert(stc_site_state(i)==1);
    assert(memcmp(image+STC_BLOCK,block,STC_BLOCK_SIZE));                                  /* raw: ours */
    assert(hash_ok(image+STC_BLOCK,STC_BLOCK_SIZE,STC_BLOCK_SHA));                          /* the view: the file's */
    assert(read_local(image+STC_BLOCK,seen,STC_BLOCK_SIZE) && !memcmp(seen,block,STC_BLOCK_SIZE));
    assert(hash_ok(image+0xeb27c0,5212,"7920e6e4ff99bf0d0c348eeab202b0a21f5e9d69c4005ba75d302eac7ea0c8a4")); /* the world trace's pin */
    assert(st_chain_owned() && !memcmp(stc_page+stc_stub(8),"\x4c\x89\xf9\xff\x25\0\0\0\0",9) && !memcmp(stc_page+stc_stub(9),"\xff\x25\0\0\0\0",6));
    assert(stc_stub(7)==0x70 && stc_stub(8)==0x80 && stc_stub(9)==0xa0 && stc_stub(14)==0x140 && stc_stub(25)==0x2a0 && stc_stub(27)==0x2e0 && stc_stub(32)==0x380 &&
           STC_SP_ENTRY_STUB==0x3a0 && STC_SP_TRAMP==0x3c0 && STC_CODE_BYTES==0x3e0);
    for(unsigned i=15;i<STC_OC_Q_SITE;i++) assert(!memcmp(stc_page+stc_stub(i),"\xff\x25\0\0\0\0",6));
    assert(!memcmp(stc_page+stc_stub(26),"\x49\x89\xf8\x49\x89\xe9\xff\x25\0\0\0\0",12) && !memcmp(stc_page+stc_stub(27),"\x4d\x89\xe0\x4c\x8d\x4c\x24\x08\xff\x25\0\0\0\0",14));
    assert(!memcmp(image+0xeadf4a,"\x49\x8b\xe9\x4d\x8b\xf0\x48\x8b\xfa",9) && !memcmp(image+0xeadf7f,"\x48\x8d\x4b\x10\x49\x8b\xd6",7) && !memcmp(image+0xeadf8b,"\x48\x89\xab\xa0\x00\x00\x00",7));
    assert(!memcmp(image+0xe99c57,"\x4c\x8b\x60\x08",4) && !memcmp(image+0xe99ce4,"\x48\x8d\x54\x24\x70",5) && !memcmp(image+0xe99957,"\x48\x89\x50\x10",4) && !memcmp(image+0xe99ab7,"\x44\x0f\x29\x4c\x24\x60",6));
    for(unsigned i=10;i<14;i++) assert(!memcmp(stc_page+stc_stub(i),"\x4d\x89\xf9\xff\x25\0\0\0\0",9));
    assert(!memcmp(stc_page+stc_stub(14),"\xff\x25\0\0\0\0",6));
    /* Outside a watched step: the original's result, counted, nothing recorded. */
    static st_record_t rec;memset(&rec,0,sizeof(rec));rec.chain[0]=ST_CHAIN_TAG;
    const LONG64 hits_pitch=st_chain_hits[3],recorded=st_chain_recorded;
    float a=0.25f;((stc_approach_fn)(void*)(stc_page+0x30))(&a,0.1f,0.5f,0.02f,0.0f);
    assert(a==tsc_approach(0.25f,0.1f,0.5f,0.02f) && st_chain_hits[3]==hits_pitch+1 && st_chain_recorded==recorded && !st_live());
    /* Inside: the approach helper on its own bytes against the reading, 20,000 inputs through the pitch stub. */
    st_record_t *was=NULL;const int live=st_live_begin(&rec,&was);assert(live>=0 && st_live()==&rec && !was);
    float first_in=0,first_t=0,first_out=0;unsigned at_target=0;
    for(unsigned i=0;i<20000;i++) {
        const float cur=tfr_range(-1.5f,1.5f),t=(i%5==0)?cur+tfr_range(-2e-4f,2e-4f):tfr_range(-1.5f,1.5f);
        const float rate=tfr_range(0.0f,3.0f),dt=tfr_range(0.0f,0.05f);
        float x=cur;((stc_approach_fn)(void*)(stc_page+0x30))(&x,t,rate,dt,0.0f);
        const float want=tsc_approach(cur,t,rate,dt);
        if(fr_bits(x)!=fr_bits(want)) {printf("step chain approach %u: cur=%08x t=%08x rate=%08x dt=%08x native %08x read %08x\n",i,fr_bits(cur),fr_bits(t),fr_bits(rate),fr_bits(dt),fr_bits(x),fr_bits(want));assert(0);}
        if(x==t) ++at_target;
        if(!i) {first_in=cur;first_t=t;first_out=x;}
    }
    float y=-0.5f;((stc_approach_fn)(void*)(stc_page+0x10))(&y,-0.25f,1.0f,0.1f,0.0f);           /* yaw */
    float r=0.75f;((stc_approach_fn)(void*)(stc_page+0x40))(&r,0.70f,2.0f,0.01f,0.0f);           /* roll */
    assert(((stc_asin_fn)(void*)stc_page)(0.5f)==0.25f && ((stc_asin_fn)(void*)(stc_page+0x20))(-0.2f)==-0.1f);
    unsigned char bank[0x60];memset(bank,0,sizeof(bank));((stc_builder_fn)(void*)(stc_page+0x50))(bank,1.0f,2.0f,3.0f);
    st_live_end(live,was);assert(!st_live());
    const uint32_t *w=rec.chain,*s=w+ST_CHAIN_HEAD;
    assert(w[0]==ST_CHAIN_TAG && w[1]==1 && w[2]==fr_bits(1.0f) && w[3]==fr_bits(2.0f) && w[4]==fr_bits(3.0f) && !memcmp(w+5,bank+0x10,0x30) && !w[17] && !w[18]);
    assert(s[0]==1 && s[1]==fr_bits(-0.5f) && s[2]==fr_bits(-0.25f) && s[3]==fr_bits(1.0f) && s[4]==fr_bits(0.1f) && !s[5] && s[6]==fr_bits(y) && y==tsc_approach(-0.5f,-0.25f,1.0f,0.1f));
    assert(s[7]==20000 && s[8]==fr_bits(first_in) && s[9]==fr_bits(first_t) && s[13]==fr_bits(first_out));
    assert(s[14]==1 && s[15]==fr_bits(0.75f) && s[20]==fr_bits(r) && r==tsc_approach(0.75f,0.70f,2.0f,0.01f));
    assert(s[21]==1 && s[22]==fr_bits(0.5f) && s[27]==fr_bits(0.25f) && s[28]==1 && s[29]==fr_bits(-0.2f) && s[34]==fr_bits(-0.1f));
    assert(at_target>3000 && st_chain_recorded==recorded+20005);
    /* FR17: GetTargetPositionForWingMan behind a stand-in (written after the pins), entered through its two stubs with a
     * formation of two agents (the leader unwatched, the wingman watched) in tick 2 of 3, read bank 1. */
    {assert(VirtualProtect(image+(STC_FM&~0xfffu),0x2000,PAGE_EXECUTE_READWRITE,&old));tsc_jump(STC_FM,(void*)tsc_fm);
     FlushInstructionCache(GetCurrentProcess(),image+STC_FM,14);
     world_identity_entry *prior_identities=world_identities;const LONG64 prior_count=world_identity_count;
     world_identities=VirtualAlloc(NULL,sizeof(world_identity_entry)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
     assert(world_identities);world_identity_count=0;assert(world_identity_record(9101,0x7dcd0) && world_identity_record(9102,0x7dd7c));
     const unsigned prior_ids=st_id_count;const uint64_t prior_ticks=st_ticks,prior_issued=stats.issued;const int prior_active=stats.active,prior_phase=boot_phase;
     const DWORD prior_ui=ui_thread;
     st_id_count=1;st_ids[0]=0x7dd7c;st_ticks=3;stats.active=1;boot_phase=3;stats.issued=2;ui_thread=GetCurrentThreadId();
     memset(st_fm,0,sizeof(st_fm));st_fm_kept=0;
     static __declspec(align(16)) unsigned char ships[2][0x80],agents[2][0x1000],formation[0x80],shape[0x80],out[0x40]; /* ships' +0x70 (parent) 0 */
     static void *members[2];
     for(unsigned i=0;i<2;i++) {
         memset(agents[i],0x11*(i+1),sizeof(agents[i]));const uint64_t runtime=9101+i;memcpy(ships[i]+8,&runtime,8);
         void *ship=ships[i];memcpy(agents[i]+ST_AGENT_OWNER,&ship,8);members[i]=agents[i];
     }
     const uint64_t b=(uintptr_t)members,e=b+sizeof(members);memset(formation,0,sizeof(formation));
     memcpy(formation+ST_FM_MEMBERS,&b,8);memcpy(formation+ST_FM_MEMBERS+8,&e,8);
     for(unsigned i=0;i<sizeof(shape);i++) shape[i]=(unsigned char)(0x40+i);
     void *vt=image+0x2c4f610u,*sp=shape;memcpy(shape,&vt,8);memcpy(formation+ST_FM_SHAPE,&sp,8);
     float pose[31];for(unsigned i=0;i<31;i++) pose[i]=1.0f+(float)i;memcpy(agents[0]+ST_AGENT_POSE+ST_BANK_STRIDE,pose,sizeof(pose));
     const uint32_t prior_bank=*(uint32_t*)ST_BANK_READ;*(uint32_t*)ST_BANK_READ=1;
     const LONG64 hits6=st_chain_hits[6],hits7=st_chain_hits[7];
     memset(out,0x5a,sizeof(out));
     assert((((st_fm_fn)(void*)(stc_page+0x60))(formation,out,1,12.5)&0xff)==1);
     assert((((st_fm_fn)(void*)(stc_page+0x70))(formation,out,0,12.5)&0xff)==1); /* the leader: not kept */
     assert((((st_fm_fn)(void*)(stc_page+0x70))(formation,out,1,13.5)&0xff)==1);
     stats.issued=4;assert((((st_fm_fn)(void*)(stc_page+0x60))(formation,out,1,14.5)&0xff)==1); /* past the window */
     assert(st_chain_hits[6]==hits6+2 && st_chain_hits[7]==hits7+2 && st_fm_kept==2 && st_fm[0].calls==2);
     static st_record_t fr;st_fm_take(0,&fr);assert(!st_fm[0].calls);
     const uint32_t *f=fr.formation,*g=f+ST_FM_WORDS;float want[16];uint64_t k;double t;
     assert(f[0]==ST_FM_TAG && f[1]==2 && f[2]==0 && f[3]==1 && f[4]==2 && f[7]==1 && f[8]==0 && f[9]==1 && f[14]==0x2c4f610u && f[15]==0 && f[127]==2);
     memcpy(&k,f+10,8);assert(k==0x7dd7c);memcpy(&k,f+12,8);assert(k==0x7dcd0);memcpy(&t,f+5,8);assert(t==12.5);
     for(unsigned i=0;i<16;i++) want[i]=1.0f+(float)i;
     assert(!memcmp(f+16,want,0x40) && !memcmp(f+34,shape+8,0x78) && f[112]==st_bits(17.0f) && f[126]==st_bits(31.0f));
     for(unsigned i=0;i<16;i++) want[i]=(float)(10+i)+12.5f;
     assert(!memcmp(f+64,want,0x40) && f[80]==0x5a5a5a5au && f[96]==0x22222222u);
     for(unsigned i=0;i<16;i++) want[i]=(float)(10+i)+13.5f;
     assert(g[0]==ST_FM_TAG && g[2]==1 && g[15]==1 && g[127]==2 && !memcmp(g+64,want,0x40));
     for(unsigned i=0;i<ST_FM_WORDS;i++) assert(!f[2*ST_FM_WORDS+i]);
     /* FR18: the converter and the conversion behind stand-ins, entered as 0xe8c380 does with the agent in r15: the
      * watched wingman's call kept complete with its conversion; the other agent's, a conversion outside any call and a
      * call past the window not. */
     assert(VirtualProtect(image+(STC_PT&~0xfffu),0x2000,PAGE_EXECUTE_READWRITE,&old) && VirtualProtect(image+(STC_PT_MATH&~0xfffu),0x1000,PAGE_EXECUTE_READWRITE,&old));
     assert(VirtualProtect(image+(STC_HIER&~0xfffu),0x1000,PAGE_EXECUTE_READWRITE,&old) && VirtualProtect(image+(STC_PS&~0xfffu),0x1000,PAGE_EXECUTE_READWRITE,&old));
     tsc_jump(STC_PT,(void*)tsc_pt);tsc_jump(STC_PT_MATH,(void*)tsc_pt_math);tsc_jump(STC_PT_CONVERT,(void*)tsc_convert);
     tsc_jump(STC_HIER,(void*)tsc_hier);tsc_jump(STC_PS,(void*)tsc_ps);FlushInstructionCache(GetCurrentProcess(),image,nt.OptionalHeader.SizeOfImage);
     tsc_hier_calls=0;
     assert(world_identity_record(9103,0x1234) && world_identity_record(9104,0x5678));
     static __declspec(align(16)) unsigned char point[0x80],ref[0x300],frame[0x300],ref_controller[0x60],frame_controller[0x60],pt_out[0x40];
     const uint64_t ref_runtime=9103,frame_runtime=9104;void *rc=ref_controller,*fc=frame_controller,*parent=ships[0];
     for(unsigned i=0;i<sizeof(point);i++) point[i]=(unsigned char)(0x30+i);
     float m[16];for(unsigned i=0;i<16;i++) m[i]=0.5f*(float)i-3.0f;memcpy(point+0x20,m,0x40);memcpy(point+ST_PT_REF,&ref_runtime,8);
     memset(ref,0,sizeof(ref));memset(frame,0,sizeof(frame));memcpy(ref+8,&ref_runtime,8);memcpy(frame+8,&frame_runtime,8);
     memcpy(ref+ST_OBJ_CONTROLLER,&rc,8);memcpy(frame+ST_OBJ_CONTROLLER,&fc,8);memcpy(ref+ST_OBJ_PARENT,&parent,8);
     for(unsigned i=0;i<0x60;i++) {ref_controller[i]=(unsigned char)(0x80+i);frame_controller[i]=(unsigned char)(0xe0-i);}
     tsc_pt_ref=ref;memset(st_fm,0,sizeof(st_fm));st_pt_kept=st_pt_conversions=st_pt_no_slot=0;
     const LONG64 hits8=st_chain_hits[8],hits9=st_chain_hits[9];
     unsigned char *caller=tsc_pt_caller(stc_page+stc_stub(8));
     stats.issued=2;memset(pt_out,0x5a,sizeof(pt_out));
     assert((((tsc_pt_caller_fn)(void*)caller)(agents[1],pt_out,point,frame,ships[0],NULL)&0xff)==1 && tsc_pt_rcx==agents[1]);
     assert((((tsc_pt_caller_fn)(void*)caller)(agents[0],pt_out,point,frame,NULL,NULL)&0xff)==1 && tsc_pt_rcx==agents[0]); /* not watched */
     {void *r=ref,*fr=frame;((stc_pt_math_fn)(void*)(stc_page+stc_stub(9)))(pt_out,NULL,&r,&fr,1);} /* outside any call */
     stats.issued=4;assert((((tsc_pt_caller_fn)(void*)caller)(agents[1],pt_out,point,frame,NULL,NULL)&0xff)==1); /* past the window */
     assert(st_chain_hits[8]==hits8+3 && st_chain_hits[9]==hits9+4 && st_pt_kept==1 && st_pt_conversions==1 && !st_pt_no_slot && st_fm[0].pt_calls==1);
     assert(!st_pt_open && !st_pt_live());
     static st_record_t pr;st_fm_take(0,&pr);assert(!st_fm[0].pt_calls && !st_fm[0].lock);
     const uint32_t *p=pr.path;float plus[16];for(unsigned i=0;i<16;i++) plus[i]=m[i]+100.0f;
     assert(p[0]==ST_PT_TAG && p[1]==2 && p[2]==0 && p[3]==1 && p[4]==0 && p[5]==1 && p[16]==1 && p[17]==0 && p[18]==0 && p[ST_PT_WORDS-1]==1);
     /* FR20: the reference's chain ref -> ships[0] (top), the frame's frame (top); one probe, identity in. */
     {const uint32_t *l=p+ST_PT_REF_LEVELS;float want[16]={0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0};for(unsigned i=0;i<16;i++) want[i]=2.0f*want[i]+(float)i+0.25f;
      memcpy(&k,l,8);assert(k==0x1234 && l[2]==1 && !memcmp(l+4,want,0x40) && tsc_hier_calls==1 && st_pt_levels_probed==1);
      l+=ST_PT_LEVEL_WORDS;memcpy(&k,l,8);assert(k==0x7dcd0 && !l[2]);for(unsigned i=4;i<20;i++) assert(!l[i]);
      for(unsigned i=ST_PT_REF_LEVELS+2*ST_PT_LEVEL_WORDS;i<ST_PT_FRAME_LEVELS;i++) assert(!p[i]);
      l=p+ST_PT_FRAME_LEVELS;memcpy(&k,l,8);assert(k==0x5678 && !l[2]);for(unsigned i=ST_PT_FRAME_LEVELS+ST_PT_LEVEL_WORDS;i<ST_PT_WORDS-1;i++) assert(!p[i]);}
     memcpy(&k,p+6,8);assert(k==0x7dd7c);memcpy(&k,p+8,8);assert(k==0x1234);memcpy(&k,p+10,8);assert(k==0x5678);
     memcpy(&k,p+12,8);assert(k==0x7dcd0);memcpy(&k,p+14,8);assert(!k);memcpy(&k,p+19,8);assert(k==0x1234);memcpy(&k,p+21,8);assert(k==0x5678);
     memcpy(&k,p+168,8);assert(k==0x7dcd0);memcpy(&k,p+170,8);assert(!k);
     assert(!memcmp(p+24,point,0x80) && !memcmp(p+56,plus,0x40) && !memcmp(p+72,m,0x40) && !memcmp(p+88,plus,0x40));
     assert(!memcmp(p+104,ref_controller,0x60) && !memcmp(p+128,frame_controller,0x60) && p[152]==0x22222222u && !p[172] && !p[190]);
     for(unsigned i=ST_PT_WORDS;i<ST_PT_ENTRIES*ST_PT_WORDS;i++) assert(!p[i]);
     assert(VirtualFree(caller,0,MEM_RELEASE));st_pt_kept=st_pt_conversions=0;
     /* FR20: 0xe8bd20 at 0xe8d502 and the object path at 0xe8d5ea entered with the agent only in r15 (the watched one,
      * then the other), the path builder at 0xeac498 for the watched agent: both conversions kept with their levels,
      * the path set with its stack, description and the point before / after. */
     {unsigned char *wc=tsc_w_caller(stc_page+stc_stub(10)),*oc=tsc_w_caller(stc_page+stc_stub(13));
      static __declspec(align(16)) unsigned char obj_mat[0x40],desc[0x200],point2[0x80];
      const LONG64 h10=st_chain_hits[10],h13=st_chain_hits[13],h14=st_chain_hits[14];
      memset(st_fm,0,sizeof(st_fm));st_pt_kept=st_pt_conversions=st_pt_levels_probed=st_ps_kept=0;tsc_hier_calls=0;stats.issued=2;
      memset(pt_out,0x5a,sizeof(pt_out));
      assert(((tsc_w_caller_fn)(void*)wc)(pt_out,point,frame,agents[1])==pt_out && !memcmp(pt_out,plus,0x40));
      assert(((tsc_w_caller_fn)(void*)wc)(pt_out,point,frame,agents[0])==pt_out);                 /* not watched */
      float om[16];for(unsigned i=0;i<16;i++) om[i]=(float)i-7.5f;memcpy(obj_mat,om,0x40);
      ((tsc_w_caller_fn)(void*)oc)(ref,obj_mat,frame,agents[1]);
      float o2[16];for(unsigned i=0;i<16;i++) o2[i]=2.0f*om[i]+(float)i+0.25f;assert(!memcmp(obj_mat,o2,0x40));
      for(unsigned i=0;i<sizeof(desc);i++) desc[i]=(unsigned char)(i*7+1);
      for(unsigned i=0;i<sizeof(point2);i++) point2[i]=(unsigned char)(0xa0+i);
      tsc_ps_point=point2;void *pp=point;memcpy(agents[1]+ST_AGENT_POINT,&pp,8);
      assert(((stc_ps_fn)(void*)(stc_page+stc_stub(14)))(agents[1],desc,NULL,NULL)==0x77);
      assert(((stc_ps_fn)(void*)(stc_page+stc_stub(14)))(agents[0],desc,NULL,NULL)==0x77);       /* not watched */
      assert(st_chain_hits[10]==h10+2 && st_chain_hits[13]==h13+1 && st_chain_hits[14]==h14+2 && st_pt_kept==2 && st_pt_conversions==2 && st_ps_kept==1);
      assert(tsc_hier_calls==3 && st_pt_levels_probed==2 && !st_pt_open && !st_pt_live()); /* one probe each, one object-path call */
      static st_record_t wr;st_fm_take(0,&wr);assert(!st_fm[0].pt_calls && !st_fm[0].ps_calls && !st_fm[0].lock);
      const uint32_t *a=wr.path,*b=a+ST_PT_WORDS,*s=wr.pathset;
      assert(a[0]==ST_PT_TAG && a[2]==1 && a[16]==1 && a[17]==0 && !memcmp(a+24,point,0x80) && !memcmp(a+56,plus,0x40) && !memcmp(a+88,plus,0x40) && a[ST_PT_WORDS-1]==2);
      memcpy(&k,a+19,8);assert(k==0x1234);memcpy(&k,a+21,8);assert(k==0x5678);memcpy(&k,a+ST_PT_REF_LEVELS,8);assert(k==0x1234 && a[ST_PT_REF_LEVELS+2]==1);
      assert(b[0]==ST_PT_TAG && b[2]==4 && b[16]==1 && b[17]==1 && b[18]==1 && b[24]==0xffffffffu && !memcmp(b+72,om,0x40) && !memcmp(b+88,o2,0x40) && !memcmp(b+56,o2,0x40));
      memcpy(&k,b+8,8);assert(!k);memcpy(&k,b+19,8);assert(k==0x1234);memcpy(&k,b+21,8);assert(k==0x5678);
      memcpy(&k,b+ST_PT_REF_LEVELS+ST_PT_LEVEL_WORDS,8);assert(k==0x7dcd0 && !b[ST_PT_REF_LEVELS+ST_PT_LEVEL_WORDS+2] && !b[ST_PT_SCRATCH] && !b[ST_PT_SCRATCH+3]);
      assert(s[0]==ST_PS_TAG && s[1]==2 && s[2]==0 && s[3]==0x77 && s[6]==0 && s[7]>=2 && s[8]==0xfffffffeu && s[232]==1 && s[233]==0 && s[ST_PS_WORDS-1]==1);
      memcpy(&k,s+4,8);assert(k==0x7dd7c);
      assert(!memcmp(s+40,desc,0x200) && !memcmp(s+168,point,0x80) && !memcmp(s+200,point2,0x80));
      for(unsigned i=ST_PS_WORDS;i<ST_PS_ENTRIES*ST_PS_WORDS;i++) assert(!s[i]);
      assert(VirtualFree(wc,0,MEM_RELEASE) && VirtualFree(oc,0,MEM_RELEASE));st_pt_kept=st_pt_conversions=st_pt_levels_probed=st_ps_kept=0;}
     /* FR19: SetTarget's slot holds 0xeaf620 in the file; relocated as the game would, a flipped pinned byte refuses,
      * then armed; the real SetTarget runs on its own bytes, called through a caller written into the int3 padding after
      * it (sub rsp,0x28; call [slot]; add rsp,0x28; ret: returns to 0xeaf65b): the watched ship's call kept, the other
      * ship's and one past the window not; then the slot restored. */
     {uint64_t file_slot;memcpy(&file_slot,image+ST_SG_SLOT,8);assert(file_slot==nt.OptionalHeader.ImageBase+ST_SG_FN);
      for(unsigned i=0x31;i<0x40;i++) assert(image[ST_SG_FN+i]==0xcc);
      void *fn=image+ST_SG_FN;memcpy(image+ST_SG_SLOT,&fn,8);
      image[ST_SG_FN+0x25]^=1;assert(!st_sg_arm() && !st_sg_installed);image[ST_SG_FN+0x25]^=1;
      assert(VirtualProtect(image+(ST_SG_FN&~0xfffu),0x1000,PAGE_EXECUTE_READWRITE,&old));
      assert(st_sg_arm() && st_sg_installed && *(void**)(image+ST_SG_SLOT)==(void*)st_settarget && st_sg_native==fn);
      unsigned char sg_code[15]={0x48,0x83,0xec,0x28,0xff,0x15,0,0,0,0,0x48,0x83,0xc4,0x28,0xc3};
      const int32_t disp=(int32_t)(ST_SG_SLOT-(ST_SG_FN+0x31+10));memcpy(sg_code+6,&disp,4);memcpy(image+ST_SG_FN+0x31,sg_code,15);
      FlushInstructionCache(GetCurrentProcess(),image+ST_SG_FN,0x60);
      typedef void (*tsg_fn)(void *,const void *,void *,void *);const tsg_fn set=(tsg_fn)(void*)(image+ST_SG_FN+0x31);
      static __declspec(align(16)) unsigned char sg_models[2][0x200];
      for(unsigned i=0;i<2;i++) {memset(sg_models[i],0,0x200);void *a=agents[i];memcpy(sg_models[i]+ST_MODEL_AGENT,&a,8);}
      void *pp=point;memcpy(agents[1]+ST_AGENT_POINT,&pp,8);
      memset((void*)st_fm,0,sizeof(st_fm));st_sg_calls=st_sg_kept=0;
      float target[16];for(unsigned i=0;i<16;i++) target[i]=7.0f*(float)i-1.5f;
      stats.issued=2;set(sg_models[1],target,ships[0],(void*)1);set(sg_models[0],target,NULL,NULL);
      stats.issued=4;set(sg_models[1],target,NULL,NULL);
      assert(st_sg_calls==3 && st_sg_kept==1 && st_fm[0].sg_calls==1 && !memcmp(sg_models[0]+ST_MODEL_TARGET,target,0x40));
      void *a3;memcpy(&a3,sg_models[1]+0x60,8);assert(a3==NULL && sg_models[1][0x6c]==0 && sg_models[1][0x6d]==1); /* the last call's */
      static st_record_t sr;st_fm_take(0,&sr);assert(!st_fm[0].sg_calls && !st_fm[0].lock);
      const uint32_t *g=sr.settarget;
      assert(g[0]==ST_SG_TAG && g[1]==2 && g[2]==ST_SG_FN+0x31+10 && g[3]==0 && g[4]==1 && g[5]==0 && g[14]==0 && g[127]==1);
      memcpy(&k,g+6,8);assert(k==0x7dd7c);memcpy(&k,g+8,8);assert(k==(uint64_t)(uintptr_t)ships[0]);memcpy(&k,g+10,8);assert(k==0x7dcd0);
      memcpy(&k,g+12,8);assert(k==1);
      assert(!memcmp(g+16,target,0x40) && !g[32] && !g[47] && !memcmp(g+52,point,0x80) && g[84]==0x22222222u && !memcmp(g+100,target,0x40));
      for(unsigned i=ST_SG_WORDS;i<ST_SG_ENTRIES*ST_SG_WORDS;i++) assert(!g[i]);
      st_sg_restore();assert(!st_sg_installed && *(void**)(image+ST_SG_SLOT)==fn);st_sg_calls=st_sg_kept=0;}
     /* FR21: the Highway's slot (file value, a flipped pinned byte refuses, armed); its map and the banking replaced by
      * stand-ins after arming; inside the watched ship's open entry the map's call is kept with the banking's result and
      * the spline memory (affine, its knots, composite, its pairs, two segments); outside it nothing. Then the path
      * clear through stub 19: the watched agent's call kept with its stack and points, the other's not. */
     {uint64_t file_slot;memcpy(&file_slot,image+ST_HW_SLOT,8);assert(file_slot==nt.OptionalHeader.ImageBase+ST_HW_FN);
      void *fn=image+ST_HW_FN;memcpy(image+ST_HW_SLOT,&fn,8);
      image[ST_HW_FN+0x100]^=1;assert(!st_hw_arm() && !st_hw_installed);image[ST_HW_FN+0x100]^=1;
      assert(st_hw_arm() && st_hw_installed && *(void**)(image+ST_HW_SLOT)==(void*)st_hw_transform && st_hw_native==fn);
      static unsigned char hw_code[14],bank_code[14],clear_code[14];
      memcpy(hw_code,image+ST_HW_FN,14);memcpy(bank_code,image+STC_HW_BANK,14);memcpy(clear_code,image+0xe89f10,14);
      assert(VirtualProtect(image+(ST_HW_FN&~0xfffu),0x2000,PAGE_EXECUTE_READWRITE,&old) && VirtualProtect(image+(STC_HW_BANK&~0xfffu),0x1000,PAGE_EXECUTE_READWRITE,&old));
      assert(VirtualProtect(image+(0xe89f10u&~0xfffu),0x1000,PAGE_EXECUTE_READWRITE,&old));
      static unsigned char vec_code[14];memcpy(vec_code,image+0xe89ed0,14);
      tsc_jump(ST_HW_FN,(void*)tsc_hw);tsc_jump(STC_HW_BANK,(void*)tsc_bank);tsc_jump(0xe89f10u,(void*)tsc_clear);tsc_jump(0xe89ed0u,(void*)tsc_vec);
      FlushInstructionCache(GetCurrentProcess(),image,nt.OptionalHeader.SizeOfImage);
      static __declspec(align(16)) unsigned char hw[0x700],aff[0x40],comp[0x40],knots[12],pairs[24],segs[2*0xa0],hmat[0x40];
      const uint64_t hw_runtime=9103;memset(hw,0,sizeof(hw));memcpy(hw+8,&hw_runtime,8);for(unsigned i=0x500;i<0x640;i++) hw[i]=(unsigned char)i;
      void *q=aff;memcpy(hw+ST_HW_SPLINE,&q,8);
      for(unsigned i=0;i<sizeof(aff);i++) aff[i]=(unsigned char)(0x11*i);
      for(unsigned i=0;i<sizeof(comp);i++) comp[i]=(unsigned char)(0x13*i);
      for(unsigned i=0;i<sizeof(knots);i++) knots[i]=(unsigned char)(i+1);
      for(unsigned i=0;i<sizeof(pairs);i++) pairs[i]=(unsigned char)(i+0x40);
      for(unsigned i=0;i<sizeof(segs);i++) segs[i]=(unsigned char)(i*3);
      uint64_t v[2]={(uint64_t)(uintptr_t)knots,(uint64_t)(uintptr_t)(knots+12)};memcpy(aff+0x20,v,16);q=comp;memcpy(aff+0x10,&q,8);
      v[0]=(uint64_t)(uintptr_t)segs;v[1]=v[0]+sizeof(segs);memcpy(comp+0x10,v,16);
      v[0]=(uint64_t)(uintptr_t)pairs;v[1]=v[0]+sizeof(pairs);memcpy(comp+0x28,v,16);
      float hm[16];for(unsigned i=0;i<16;i++) hm[i]=0.125f*(float)i;memcpy(hmat,hm,0x40);
      const st_hw_fn slot=*(st_hw_fn*)(image+ST_HW_SLOT);
      memset((void*)st_fm,0,sizeof(st_fm));st_hw_calls=st_hw_kept=st_hw_banking=0;stats.issued=2;
      slot(hw,hmat);                                                                       /* outside any entry */
      {st_pt_ctx c;assert(st_pt_begin(&c,10,1,agents[1],point,frame,NULL,NULL));slot(hw,hmat);slot(hw,hmat);st_pt_end(&c,hmat,1);}
      {st_pt_ctx c;assert(!st_pt_begin(&c,10,1,agents[0],point,frame,NULL,NULL));slot(hw,hmat);} /* not watched */
      assert(st_hw_calls==4 && st_hw_kept==1 && st_hw_banking==2 && st_fm[0].hw_calls==2 && !st_pt_open && !st_pt_live());
      static st_record_t hr;st_fm_take(0,&hr);assert(!st_fm[0].hw_calls && !st_fm[0].lock);
      const uint32_t *h=hr.hwcap;
      memcpy(&k,h+8,8);assert(k==(uint64_t)(uintptr_t)hw);memcpy(&k,h+10,8);assert(k==0x1234);memcpy(&k,h+6,8);assert(k==(uint64_t)(uintptr_t)image);
      memcpy(&k,h+4,8);assert(k==0x7dd7c);
      float t13,t14;const float before0=hm[0]+1.5f;memcpy(&t13,h+13,4);memcpy(&t14,h+14,4); /* one stand-in call before the kept one */
      assert(h[0]==ST_HW_TAG && h[1]==2 && h[2]==0 && h[3]==1 && h[12]==1 && t13==0.25f && t14==1.5f && h[15]==5 && h[ST_HW_WORDS-1]==2);
      float hb[16];memcpy(hb,h+16,0x40);assert(hb[0]==before0 && !memcmp(hb+1,hm+1,0x3c));
      float ha[16];memcpy(ha,h+32,0x40);assert(ha[0]==before0+1.5f && !memcmp(ha+1,hm+1,0x3c));
      assert(!memcmp(h+48,hw+0x500,0x140));
      const uint32_t *b=h+ST_HW_BLOCKS;memcpy(&k,b,8);assert(k==(uint64_t)(uintptr_t)aff && b[2]==0x40 && !memcmp(b+4,aff,0x40));b+=4+0x10;
      memcpy(&k,b,8);assert(k==(uint64_t)(uintptr_t)knots && b[2]==12 && !memcmp(b+4,knots,12));b+=4+4;
      memcpy(&k,b,8);assert(k==(uint64_t)(uintptr_t)comp && b[2]==0x40 && !memcmp(b+4,comp,0x40));b+=4+0x10;
      memcpy(&k,b,8);assert(k==(uint64_t)(uintptr_t)pairs && b[2]==24 && !memcmp(b+4,pairs,24));b+=4+8;
      memcpy(&k,b,8);assert(k==(uint64_t)(uintptr_t)segs && b[2]==0x140 && !memcmp(b+4,segs,0x140));b+=4+0x50;
      for(const uint32_t *z=b;z<h+ST_HW_WORDS-1;z++) assert(!*z);
      static __declspec(align(16)) unsigned char point3[0x80];for(unsigned i=0;i<sizeof(point3);i++) point3[i]=(unsigned char)(0xc0+i);
      tsc_ps_point=point3;void *pp=point;memcpy(agents[1]+ST_AGENT_POINT,&pp,8);st_pc_kept=0;
      const LONG64 h19=st_chain_hits[19];
      assert(((stc_any_fn)(void*)(stc_page+stc_stub(19)))(agents[1],(void*)1,NULL,NULL,NULL,NULL)==0x66);
      assert(((stc_any_fn)(void*)(stc_page+stc_stub(19)))(agents[0],(void*)1,NULL,NULL,NULL,NULL)==0x66);  /* not watched */
      assert(st_chain_hits[19]==h19+2 && st_pc_kept==1 && st_fm[0].ps_calls==1);
      static st_record_t cr;st_fm_take(0,&cr);const uint32_t *s=cr.pathset;
      assert(s[0]==ST_PC_TAG && s[1]==2 && s[3]==4 && s[7]>=2 && s[232]==1 && !memcmp(s+168,point,0x80) && !memcmp(s+200,point3,0x80));
      memcpy(&k,s+4,8);assert(k==0x7dd7c);memcpy(&k,s+46,8);assert(k==(uint64_t)(uintptr_t)point);
      /* FR22: the current points' append through stub 18 hands over agent+0x6a8 (FR21 looked at -0x6c0 and kept
       * nothing): the watched agent's call kept with the added point, the last point and the header after; the
       * other agent's vector not; a vector address below the offset not. */
      {static __declspec(align(16)) unsigned char added[0x90];for(unsigned i=0;i<sizeof(added);i++) added[i]=(unsigned char)(0x70+i);
       for(unsigned i=0;i<0x90;i++) tsc_vec_store[i]=(unsigned char)(0x10+i);
       unsigned char *vb=tsc_vec_store,*ve=tsc_vec_store+0x90,*vc=tsc_vec_store+sizeof(tsc_vec_store);
       for(unsigned a=0;a<2;a++) {memcpy(agents[a]+ST_AGENT_POINT,&vb,8);memcpy(agents[a]+ST_AGENT_POINT+8,&ve,8);memcpy(agents[a]+ST_AGENT_POINT+16,&vc,8);}
       const LONG64 h18=st_chain_hits[18];st_pc_kept=0;
       assert(((stc_any_fn)(void*)(stc_page+stc_stub(18)))(agents[1]+ST_AGENT_POINT,added,NULL,NULL,NULL,NULL)==0x55);
       memcpy(agents[0]+ST_AGENT_POINT+8,&ve,8);
       assert(((stc_any_fn)(void*)(stc_page+stc_stub(18)))(agents[0]+ST_AGENT_POINT,added,NULL,NULL,NULL,NULL)==0x55); /* not watched */
       assert(st_chain_hits[18]==h18+2 && st_pc_kept==1 && st_fm[0].ps_calls==1);
       static st_record_t vr;st_fm_take(0,&vr);const uint32_t *v=vr.pathset;
       assert(v[0]==ST_PC_TAG && v[1]==2 && v[3]==3 && v[7]>=2 && v[232]==0 && !memcmp(v+88,added,0x90) && !memcmp(v+124,added,0x90));
       assert(!memcmp(v+168,tsc_vec_store,0x80) && !memcmp(v+200,tsc_vec_store,0x80));
       memcpy(&k,v+4,8);assert(k==0x7dd7c);memcpy(&k,v+46,8);assert(k==(uint64_t)(uintptr_t)vb);
       memcpy(&k,v+234,8);assert(k==(uint64_t)(uintptr_t)vb);memcpy(&k,v+236,8);assert(k==(uint64_t)(uintptr_t)(vb+0x120));
       memcpy(&k,v+48,8);assert(k==(uint64_t)(uintptr_t)(vb+0x90));}
      /* FR24: the creations through stubs 26 (the queue: ship in rdi, issuer in rbp) and 27 (the propagation: ship in
       * r12, O at the caller's rsp+0x30, the leader's pending point at [rsp+0x208]), their targets stand-ins: the watched
       * ship's kept with the point, stack, keys, O and P; the other ship's not. */
      {static unsigned char q_code[14],f_code[14];memcpy(q_code,image+0xeaa6f0,14);memcpy(f_code,image+0xeaa980,14);
       assert(VirtualProtect(image+0xeaa000u,0x1000,PAGE_EXECUTE_READWRITE,&old));
       tsc_jump(0xeaa6f0u,(void*)tsc_oc_copy);tsc_jump(0xeaa980u,(void*)tsc_oc_finish);FlushInstructionCache(GetCurrentProcess(),image,nt.OptionalHeader.SizeOfImage);
       static __declspec(align(16)) unsigned char osrc[0x90],odst[0x90],oframe[0x210],olead[0x90];
       for(unsigned i=0;i<0x90;i++) {osrc[i]=(unsigned char)(0x20+i);olead[i]=(unsigned char)(0x60+i);}
       const uint64_t ref=9101;memcpy(osrc+ST_PT_REF,&ref,8);memcpy(olead+ST_PT_REF,&ref,8);
       for(unsigned i=0;i<sizeof(oframe);i++) oframe[i]=(unsigned char)(0x90+i);
       void *lp=olead;memcpy(oframe+0x208,&lp,8);
       memset((void*)st_fm,0,sizeof(st_fm));st_oc_kept=0;stats.issued=2;
       const LONG64 h26=st_chain_hits[26],h27=st_chain_hits[27];
       memset(odst,0,sizeof(odst));assert(test_oc_q_call(stc_page+stc_stub(26),odst,osrc,9102,9101)==odst && !memcmp(odst,osrc,0x90));
       assert(test_oc_q_call(stc_page+stc_stub(26),odst,osrc,9101,0)==odst);                     /* not watched */
       memset(odst,0,sizeof(odst));memcpy(odst,osrc,0x90);
       assert(test_oc_fm_call(stc_page+stc_stub(27),odst,9102,oframe)==odst);
       assert(test_oc_fm_call(stc_page+stc_stub(27),odst,9101,oframe)==odst);                    /* not watched */
       assert(st_chain_hits[26]==h26+2 && st_chain_hits[27]==h27+2 && st_oc_kept==2 && st_fm[0].ps_calls==2);
       static st_record_t orr;st_fm_take(0,&orr);const uint32_t *o=orr.pathset,*o2=orr.pathset+ST_PS_WORDS;
       assert(o[0]==ST_OC_TAG && o[1]==2 && o[3]==0 && o[6]==0 && o[7]>=2 && o[44]==0 && !memcmp(o+48,osrc+0x20,0x40) && !memcmp(o+64,osrc,0x90));
       memcpy(&k,o+4,8);assert(k==0x7dd7c);memcpy(&k,o+40,8);assert(k==0x7dcd0);memcpy(&k,o+42,8);assert(k==0x7dcd0);
       for(unsigned i=100;i<134;i++) assert(o[i]==(i<132?0xffffffffu:0u));
       assert(o2[0]==ST_OC_TAG && o2[3]==1 && o2[6]==1 && o2[7]>=2 && !memcmp(o2+64,osrc,0x90) && !memcmp(o2+100,oframe+0x30,0x40) && !memcmp(o2+116,olead+0x20,0x40));
       memcpy(&k,o2+132,8);assert(k==0x7dcd0);memcpy(&k,o2+42,8);assert(k==0);
       assert(o[ST_PS_WORDS-1]==2 && o2[ST_PS_WORDS-1]==2);
       memcpy(image+0xeaa6f0,q_code,14);memcpy(image+0xeaa980,f_code,14);FlushInstructionCache(GetCurrentProcess(),image,nt.OptionalHeader.SizeOfImage);
       st_oc_kept=0;}
      /* FR25: the results through stubs 28/29 (the store a stand-in): the watched ship's pilot (entity under the ship,
       * depth 1) kept with the Value and its position, another ship's not, a foreign Value tag not; the transform
       * through stub 30 called as from 0xc6fb48 (context in r12) kept with the matrix before/after and the object's
       * level, not when called from elsewhere; GetSafePos's stub 31 (gate) into a stand-in that runs the solver on its
       * own bytes through the entry jump: the capture (point unchanged: no box holds it), boxes, arguments; the solver
       * outside a gate counted only. */
      {static unsigned char sv_code[14],tr_code[14],gz_code[14];memcpy(sv_code,image+0xcc6710,14);memcpy(tr_code,image+0x3db8a0,14);memcpy(gz_code,image+0x835b80,14);
       assert(VirtualProtect(image+0xcc6000u,0x1000,PAGE_EXECUTE_READWRITE,&old) && VirtualProtect(image+0x3db000u,0x1000,PAGE_EXECUTE_READWRITE,&old));
       assert(VirtualProtect(image+0x835000u,0x1000,PAGE_EXECUTE_READWRITE,&old) && VirtualProtect(image+0x14e4000u,0x1000,PAGE_EXECUTE_READWRITE,&old));
       tsc_jump(0xcc6710u,(void*)tsc_setvar);tsc_jump(0x3db8a0u,(void*)tsc_gtr);tsc_jump(0x835b80u,(void*)tsc_zone_gsp);
       FlushInstructionCache(GetCurrentProcess(),image,nt.OptionalHeader.SizeOfImage);
       static __declspec(align(16)) unsigned char ctxs[2][16],contexts[2][0x200],pilots[2][0x80],sval[16],sobj[0x40],ctx_md[16];
       for(unsigned i=0;i<2;i++) {
           memset(contexts[i],0,sizeof(contexts[i]));memset(pilots[i],0,sizeof(pilots[i]));
           const uint32_t tag=0x66;const uint64_t payload=(uint64_t)(uintptr_t)contexts[i]+ST_SP_AI_VALUE,pr=9200+i;
           memcpy(ctxs[i],&tag,4);memcpy(ctxs[i]+8,&payload,8);void *pe=pilots[i],*sh=ships[i];
           memcpy(contexts[i]+ST_SP_ENTITY,&pe,8);memcpy(pilots[i]+8,&pr,8);memcpy(pilots[i]+ST_OBJ_PARENT,&sh,8);
       }
       {const uint32_t tag=0x5d;memcpy(ctx_md,&tag,4);const uint64_t pl=(uint64_t)(uintptr_t)contexts[1]+ST_SP_AI_VALUE;memcpy(ctx_md+8,&pl,8);}
       float spos[4]={-12450.6875f,-3728.83f,-34665.38f,0.0f};memset(sobj,0,sizeof(sobj));memcpy(sobj+0x20,spos,16);
       {const uint32_t tag=0x63;const uint64_t pl=(uint64_t)(uintptr_t)sobj;memset(sval,0,16);memcpy(sval,&tag,4);memcpy(sval+8,&pl,8);}
       assert(world_identity_record(9200,0x9200) && world_identity_record(9201,0x9201));
       memset((void*)st_fm,0,sizeof(st_fm));st_sp_kept=st_sp_caps=st_sp_solver=st_sp_gated=st_sp_sequence=0;tsc_setvar_calls=0;stats.issued=2;
       const LONG64 h28=st_chain_hits[28],h29=st_chain_hits[29],h30=st_chain_hits[30];
       assert(((stc_setvar_fn)(void*)(stc_page+stc_stub(28)))(NULL,ctxs[1],NULL,sval)==0x99);
       assert(((stc_setvar_fn)(void*)(stc_page+stc_stub(28)))(NULL,ctxs[0],NULL,sval)==0x99);   /* not watched */
       assert(((stc_setvar_fn)(void*)(stc_page+stc_stub(28)))(NULL,ctx_md,NULL,sval)==0x99);    /* not an AI context */
       assert(((stc_setvar_fn)(void*)(stc_page+stc_stub(29)))(NULL,ctxs[1],NULL,sval)==0x99);
       assert(tsc_setvar_calls==4 && st_chain_hits[28]==h28+3 && st_chain_hits[29]==h29+1 && st_sp_kept==2);
       /* the transform: self = ref (9103 -> 0x1234, its parent ships[0]), anc = frame (9104 -> 0x5678) */
       static __declspec(align(16)) unsigned char tmat[0x40];float tm[16];for(unsigned i=0;i<16;i++) tm[i]=0.5f*(float)i+0.25f;memcpy(tmat,tm,0x40);
       const uintptr_t tr_ret=stc_sp_tr_ret[0],gs_ret0=stc_sp_gs_ret[0];
       test_sp_tr_call(stc_page+stc_stub(30),ref,tmat,frame,ctxs[1],0x5a,1.25);                    /* from elsewhere: not kept */
       stc_sp_tr_ret[0]=(uintptr_t)test_sp_tr_ret;tsc_hier_calls=0;
       memcpy(tmat,tm,0x40);test_sp_tr_call(stc_page+stc_stub(30),ref,tmat,frame,ctxs[1],0x5a,1.25);
       float t2[16];for(unsigned i=0;i<16;i++) t2[i]=2.0f*tm[i]+1.0f;assert(!memcmp(tmat,t2,0x40) && st_chain_hits[30]==h30+2 && st_sp_kept==3 && tsc_hier_calls==1);
       test_sp_tr_call(stc_page+stc_stub(30),ref,tmat,frame,ctxs[0],0x5a,1.25);                    /* not watched */
       assert(st_sp_kept==3);
       /* the solver: two boxes far from the point */
       static __declspec(align(16)) unsigned char boxes[2*0x70],bvec[0x18];static __declspec(align(16)) float sp[4],sdir[4];
       for(unsigned b=0;b<2;b++) {float bx[28];memset(bx,0,sizeof(bx));bx[0]=bx[1]=bx[2]=10.0f;bx[4]=1000.0f*(float)(b+1); /* +0 half size, +0x10 centre */
           bx[16]=1.0f;bx[21]=1.0f;bx[26]=1.0f;memcpy(boxes+b*0x70,bx,0x70);}
       {uint64_t bv[3]={(uint64_t)(uintptr_t)boxes,(uint64_t)(uintptr_t)(boxes+sizeof(boxes)),(uint64_t)(uintptr_t)(boxes+sizeof(boxes))};memcpy(bvec,bv,0x18);}
       const float p0[4]={12.5f,-3.25f,7.75f,0.0f},d0[4]={0.6f,0.0f,0.8f,0.0f};memcpy(sp,p0,16);memcpy(sdir,d0,16);
       tsc_zone_gsp(bvec,sp,sdir,NULL);tsc_zone_gsp(bvec,sp,sdir,NULL);assert(!memcmp(sp,p0,16));        /* no gate: counted only */
       assert(st_sp_solver==2 && !st_sp_gated && !st_sp_caps);
       stc_sp_gs_ret[0]=(uintptr_t)test_sp_gs_ret;
       test_sp_gs_call(stc_page+stc_stub(31),ctxs[0],bvec,sp,sdir,NULL);                            /* not watched */
       test_sp_gs_call(stc_page+stc_stub(31),ctxs[1],bvec,sp,sdir,NULL);
       assert(!memcmp(sp,p0,16) && st_sp_solver==4 && st_sp_gated==2 && st_sp_caps==1 && st_fm[0].sp_calls==1);
       stc_sp_tr_ret[0]=tr_ret;stc_sp_gs_ret[0]=gs_ret0;
       static st_record_t spr;st_fm_take(0,&spr);assert(!st_fm[0].sp_calls && !st_fm[0].ps_calls && !st_fm[0].lock);
       const uint32_t *v0=spr.pathset,*v1=v0+ST_PS_WORDS,*v2=v0+2*ST_PS_WORDS,*c0=spr.spcap;
       assert(v0[0]==ST_SP_TAG && v0[1]==2 && v0[2]==0 && v0[3]==0 && v0[7]==1 && v0[11]==0 && v0[16]==0x63 && !memcmp(v0+20,spos,16) && v0[ST_PS_WORDS-1]==3);
       memcpy(&k,v0+4,8);assert(k==0x7dd7c);memcpy(&k,v0+8,8);assert(k==0x9201);memcpy(&k,v0+18,8);assert(k==(uint64_t)(uintptr_t)sobj);
       assert(v1[0]==ST_SP_TAG && v1[3]==1 && v1[6]==1 && v1[10]==v0[10]+1 && !memcmp(v1+20,spos,16));
       {double tt;memcpy(&tt,v2+20,8);assert(v2[0]==ST_SP_TAG && v2[3]==2 && v2[6]==2 && v2[22]==0x5a && tt==1.25 && !memcmp(v2+24,tm,0x40) && !memcmp(v2+40,t2,0x40));
        memcpy(&k,v2+16,8);assert(k==0x1234);memcpy(&k,v2+18,8);assert(k==0x5678);
        memcpy(&k,v2+56,8);assert(k==0x1234 && v2[58]==1);float lv[16]={0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0};for(unsigned i=0;i<16;i++) lv[i]=2.0f*lv[i]+(float)i+0.25f;
        assert(!memcmp(v2+60,lv,0x40));memcpy(&k,v2+76,8);assert(k==0x7dcd0 && !v2[78]);}
       for(unsigned i=3*ST_PS_WORDS;i<ST_PS_ENTRIES*ST_PS_WORDS;i++) assert(!v0[i]);
       float r50=50.0f,a15=1.5f,a25=2.5f;uint32_t u;
       assert(c0[0]==ST_SP_CAP_TAG && c0[1]==2 && c0[2]==0 && c0[3]==0 && c0[6]==0 && c0[7]==2 && c0[8]==2 && c0[9]==0 && c0[30]==1 && c0[31]==0xffffffffu && c0[33]==0);
       memcpy(&u,&r50,4);assert(c0[28]==u);memcpy(&u,&a15,4);assert(c0[29]==u);memcpy(&u,&a25,4);assert(c0[32]==u);
       assert(!memcmp(c0+12,p0,16) && !memcmp(c0+16,p0,16) && !memcmp(c0+20,d0,16) && c0[24]==0xffffffffu && !memcmp(c0+ST_SP_BOX_FIRST,boxes,sizeof(boxes)));
       memcpy(&k,c0+4,8);assert(k==0x7dd7c);memcpy(&k,c0+34,8);assert(k==0x9201 && c0[36]==1 && c0[ST_SP_WORDS-1]==1);
       for(unsigned i=ST_SP_WORDS;i<ST_SP_CAPS*ST_SP_WORDS;i++) assert(!c0[i]);
       memcpy(image+0xcc6710,sv_code,14);memcpy(image+0x3db8a0,tr_code,14);memcpy(image+0x835b80,gz_code,14);
       FlushInstructionCache(GetCurrentProcess(),image,nt.OptionalHeader.SizeOfImage);
       st_sp_kept=st_sp_caps=st_sp_solver=st_sp_gated=0;}
      memcpy(image+0xe89ed0,vec_code,14);
      memcpy(image+ST_HW_FN,hw_code,14);memcpy(image+STC_HW_BANK,bank_code,14);memcpy(image+0xe89f10,clear_code,14);
      FlushInstructionCache(GetCurrentProcess(),image,nt.OptionalHeader.SizeOfImage);
      st_hw_restore();assert(!st_hw_installed && *(void**)(image+ST_HW_SLOT)==fn);st_hw_calls=st_hw_kept=st_hw_banking=st_pc_kept=0;}
     *(uint32_t*)ST_BANK_READ=prior_bank;st_id_count=prior_ids;st_ticks=prior_ticks;stats.issued=prior_issued;stats.active=prior_active;boot_phase=prior_phase;
     ui_thread=prior_ui;st_fm_kept=0;
     VirtualFree(world_identities,0,MEM_RELEASE);world_identities=prior_identities;world_identity_count=prior_count;}
    /* Ownership, then the exact restoration. */
    image[stc_sites[5].site+1]^=1;assert(!st_chain_owned());image[stc_sites[5].site+1]^=1;assert(st_chain_owned());
    st_chain_restore();
    assert(!st_chain_installed && st_chain_restored && !stc_done && !stc_sp_entry_done && stc_sp_entry_state()==0 && !memcmp(image+STC_BLOCK,block,STC_BLOCK_SIZE));
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) assert(stc_site_state(i)==0);
    assert(VirtualFree(stc_page,0,MEM_RELEASE));stc_page=saved_page;st_chain_restored=0;st_chain_stage="off";
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) st_chain_hits[i]=0;
    st_chain_recorded=0;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    printf("step chain native image: pins, thirty-three swaps and the solver's entry, view, approach helper 0xeb1db0 = the reading on 20000 (%u at the target), rows, "
           "FR17 formation calls kept (watched wingman, tick window, leader not kept), FR18 path point call kept complete "
           "(agent through r15, conversion inside), FR19 SetTarget slot (pin, real bytes, caller 0xeaf65b) kept, FR20 conversions "
           "(0xe8d502, object path) with level probes and the path set (stack, description, point) kept, FR21 Highway slot (pin, "
           "capture with banking and spline memory inside the watched entry only) and path clear (stack, points) kept, FR22 current-points append (agent at +0x6a8, added and last point) kept, FR24 order creations (queue copy 0xeadf86, propagation 0xe99ce9 with O and P) kept, FR25 position chain (results 0xbe0c86/0xbe23aa, "
           "transform 0xc6fb48 with its gate and levels, the solver on its own bytes inside GetSafePos's gate) kept, restoration PASS\n",at_target);
}
