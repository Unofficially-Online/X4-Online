/* Player sound (stop 221): lockstep_pilot_present.h's sound entry and sound source, lockstep_pilot_death.h's ship
 * computer forward, on synthetic memory, every rule per slot k with slots 2 and 3 at once; the stubs run for real (a
 * launcher loads every register, the stub runs, a landing stores them all); and the pins and sites on the real image. */

/* ---- the stubs, executed ---- */
/* Registers by index: 0 rax, 1 rbx, 2 rcx, 3 rdx, 4 rsi, 5 rdi, 6 r8, 7 r9, 8 r10, 9 r11, 10 r12, 11 r13, 12 r14, 13 r15. */
enum {TPS_RAX,TPS_RBX,TPS_RCX,TPS_RDX,TPS_RSI,TPS_RDI,TPS_R8,TPS_R9,TPS_R10,TPS_R11,TPS_R12,TPS_R13,TPS_R14,TPS_R15,TPS_REGS};
static volatile uint64_t tpsnd_in[TPS_REGS] __attribute__((used)),tpsnd_out[TPS_REGS] __attribute__((used));
static volatile unsigned char tpsnd_xin[96] __attribute__((used)),tpsnd_xout[96] __attribute__((used));
static volatile uint64_t tpsnd_flags_in __attribute__((used)),tpsnd_flags_out __attribute__((used)),tpsnd_saved __attribute__((used)),
    tpsnd_target __attribute__((used)),tpsnd_at __attribute__((used)),tpsnd_land_rsp __attribute__((used)),tpsnd_fn_rcx __attribute__((used)),
    tpsnd_fn_rdx __attribute__((used)),tpsnd_fn_rsp __attribute__((used)),tpsnd_fn_answer __attribute__((used)),tpsnd_fn_xmm2 __attribute__((used)),
    tpsnd_fn_calls __attribute__((used)),tpsnd_ret __attribute__((used));
/* tpsnd_launch(stub, entry): every register from tpsnd_in/xin/flags_in, rsp 16-aligned (entry 0) or 8 mod 16 (entry 1:
 * a function's first instruction, tpsnd_ret where its return address would be), then a jump to the stub; tpsnd_land (the stub's resume) stores them all and returns
 * to the launcher's caller. tpsnd_fn is the stub's call: it notes rcx, rdx, xmm2 and rsp, and clobbers every volatile
 * register and the flags before it answers tpsnd_fn_answer. */
__asm__(".text\n.p2align 4\ntpsnd_launch:\n"
" push %rbp\n push %rbx\n push %rsi\n push %rdi\n push %r12\n push %r13\n push %r14\n push %r15\n sub $0x48,%rsp\n"
" mov %rsp,tpsnd_saved(%rip)\n mov %rcx,tpsnd_target(%rip)\n test %edx,%edx\n jz 1f\n pushq tpsnd_ret(%rip)\n1:\n mov %rsp,tpsnd_at(%rip)\n"
" movdqu tpsnd_xin+0x00(%rip),%xmm0\n movdqu tpsnd_xin+0x10(%rip),%xmm1\n movdqu tpsnd_xin+0x20(%rip),%xmm2\n"
" movdqu tpsnd_xin+0x30(%rip),%xmm3\n movdqu tpsnd_xin+0x40(%rip),%xmm4\n movdqu tpsnd_xin+0x50(%rip),%xmm5\n"
" mov tpsnd_in+0x08(%rip),%rbx\n mov tpsnd_in+0x10(%rip),%rcx\n mov tpsnd_in+0x18(%rip),%rdx\n mov tpsnd_in+0x20(%rip),%rsi\n"
" mov tpsnd_in+0x28(%rip),%rdi\n mov tpsnd_in+0x30(%rip),%r8\n mov tpsnd_in+0x38(%rip),%r9\n mov tpsnd_in+0x40(%rip),%r10\n"
" mov tpsnd_in+0x48(%rip),%r11\n mov tpsnd_in+0x50(%rip),%r12\n mov tpsnd_in+0x58(%rip),%r13\n mov tpsnd_in+0x60(%rip),%r14\n"
" mov tpsnd_in+0x68(%rip),%r15\n mov tpsnd_in+0x00(%rip),%rax\n"
" pushq tpsnd_flags_in(%rip)\n popfq\n jmp *tpsnd_target(%rip)\n"
".p2align 4\ntpsnd_land:\n pushfq\n popq tpsnd_flags_out(%rip)\n"
" mov %rax,tpsnd_out+0x00(%rip)\n mov %rbx,tpsnd_out+0x08(%rip)\n mov %rcx,tpsnd_out+0x10(%rip)\n mov %rdx,tpsnd_out+0x18(%rip)\n"
" mov %rsi,tpsnd_out+0x20(%rip)\n mov %rdi,tpsnd_out+0x28(%rip)\n mov %r8,tpsnd_out+0x30(%rip)\n mov %r9,tpsnd_out+0x38(%rip)\n"
" mov %r10,tpsnd_out+0x40(%rip)\n mov %r11,tpsnd_out+0x48(%rip)\n mov %r12,tpsnd_out+0x50(%rip)\n mov %r13,tpsnd_out+0x58(%rip)\n"
" mov %r14,tpsnd_out+0x60(%rip)\n mov %r15,tpsnd_out+0x68(%rip)\n"
" movdqu %xmm0,tpsnd_xout+0x00(%rip)\n movdqu %xmm1,tpsnd_xout+0x10(%rip)\n movdqu %xmm2,tpsnd_xout+0x20(%rip)\n"
" movdqu %xmm3,tpsnd_xout+0x30(%rip)\n movdqu %xmm4,tpsnd_xout+0x40(%rip)\n movdqu %xmm5,tpsnd_xout+0x50(%rip)\n"
" mov %rsp,tpsnd_land_rsp(%rip)\n mov tpsnd_saved(%rip),%rsp\n add $0x48,%rsp\n"
" pop %r15\n pop %r14\n pop %r13\n pop %r12\n pop %rdi\n pop %rsi\n pop %rbx\n pop %rbp\n ret\n"
".p2align 4\ntpsnd_fn:\n mov %rcx,tpsnd_fn_rcx(%rip)\n mov %rdx,tpsnd_fn_rdx(%rip)\n movsd %xmm2,tpsnd_fn_xmm2(%rip)\n mov %rsp,tpsnd_fn_rsp(%rip)\n"
" addq $1,tpsnd_fn_calls(%rip)\n"
" mov $0x1111,%ecx\n mov $0x2222,%edx\n mov $0x4444,%r8d\n mov $0x5555,%r9d\n mov $0x6666,%r10d\n mov $0x7777,%r11d\n"
" xorps %xmm0,%xmm0\n xorps %xmm1,%xmm1\n xorps %xmm2,%xmm2\n xorps %xmm3,%xmm3\n xorps %xmm4,%xmm4\n xorps %xmm5,%xmm5\n"
" xor %eax,%eax\n mov tpsnd_fn_answer(%rip),%rax\n ret\n");
extern void tpsnd_launch(void *stub,int entry);
extern char tpsnd_land[],tpsnd_fn[];
#define TPS_FLAGS 0x8d5u   /* CF PF AF ZF SF OF: what popfq sets and a stub must give back */
static void tpsnd_fill(void) {
    for(unsigned r=0;r<TPS_REGS;r++) tpsnd_in[r]=0x5100510051000000ull+r;
    for(unsigned i=0;i<96;i++) tpsnd_xin[i]=(unsigned char)(0x61+i);
    {const double t=1234.5;memcpy((void*)(tpsnd_xin+0x20),&t,8);}   /* xmm2's low lane: a time */
    tpsnd_flags_in=0x2u|TPS_FLAGS;tpsnd_fn_calls=0;tpsnd_fn_rsp=0;
}
/* Every register back as it came but the ones listed (a mask of indices), the xmm registers, the flags, rsp. */
static int tpsnd_kept(unsigned except) {
    for(unsigned r=0;r<TPS_REGS;r++) if(!(except>>r&1u) && tpsnd_out[r]!=tpsnd_in[r]) {printf("register %u changed\n",r);return 0;}
    for(unsigned i=0;i<96;i++) if(tpsnd_xout[i]!=tpsnd_xin[i]) {printf("xmm byte %u changed\n",i);return 0;}
    return (tpsnd_flags_out&TPS_FLAGS)==(tpsnd_flags_in&TPS_FLAGS) && tpsnd_land_rsp==tpsnd_at;
}
static void test_player_sound_stubs(void) {
    unsigned char *page=VirtualAlloc(NULL,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);assert(page);
    unsigned char *const a=page,*const b=page+0x200,*const c=page+0x400;
    /* The sound start's entry (lockstep_pilot_present.h, its own load): rcx = r9, rdx = the return address, the answer
     * into r9, X4's two first instructions run in the stub (mov rax,rsp; mov [rax+0x10],rbx), entered as a function is. */
    {   const unsigned n=psnd_stub(a,PSND_STUB_ROOM,pres_source_load,sizeof(pres_source_load),tpsnd_fn,PSND_OUT_R9,1,pres_source_head,PRES_SOURCE_HEAD,tpsnd_land);
        assert(n && n<=PSND_STUB_ROOM);
        tpsnd_fill();tpsnd_fn_answer=0xabcdef;tpsnd_ret=0x56179e1234ull;FlushInstructionCache(GetCurrentProcess(),page,0x1000);
        tpsnd_launch(a,1);
        assert(tpsnd_fn_calls==1 && tpsnd_fn_rcx==tpsnd_in[TPS_R9] && tpsnd_fn_rdx==0x56179e1234ull && (tpsnd_fn_rsp&15u)==8u);   /* its arguments, the call 16-aligned */
        assert(tpsnd_out[TPS_R9]==0xabcdef && tpsnd_out[TPS_RAX]==tpsnd_at && *(uint64_t*)(uintptr_t)(tpsnd_at+0x10)==tpsnd_in[TPS_RBX] &&
               *(uint64_t*)(uintptr_t)tpsnd_at==0x56179e1234ull);
        assert(tpsnd_kept(1u<<TPS_R9|1u<<TPS_RAX));}
    /* The computer's engaged site (lockstep_pilot_death.h): rcx = rsi, rdx = r14, the time in xmm2; no answer; X4's
     * two instructions after it (mov rdx,r14; mov rcx,rsi); inside a body (rsp 16-aligned). */
    {   static const unsigned char load[]={0x48,0x89,0xf1,0x4c,0x89,0xf2};
        const unsigned n=psnd_stub(b,PSND_STUB_ROOM,load,sizeof(load),tpsnd_fn,PSND_OUT_NONE,0,pvehk_computer_movs,6,tpsnd_land);
        assert(n && n<=PSND_STUB_ROOM);
        tpsnd_fill();tpsnd_fn_answer=0x77;FlushInstructionCache(GetCurrentProcess(),page,0x1000);
        tpsnd_launch(b,0);
        double t=0;memcpy(&t,(const void*)&tpsnd_fn_xmm2,8);
        assert(tpsnd_fn_calls==1 && tpsnd_fn_rcx==tpsnd_in[TPS_RSI] && tpsnd_fn_rdx==tpsnd_in[TPS_R14] && t==1234.5 && (tpsnd_fn_rsp&15u)==8u);
        assert(tpsnd_out[TPS_RCX]==tpsnd_in[TPS_RSI] && tpsnd_out[TPS_RDX]==tpsnd_in[TPS_R14] && tpsnd_kept(1u<<TPS_RCX|1u<<TPS_RDX));}
    /* The r12 form, inside a body: the answer in r12, nothing else changed. */
    {   static const unsigned char load[]={0x48,0x8d,0x4f,0x30};   /* lea rcx,[rdi+0x30] */
        const unsigned n=psnd_stub(c,PSND_STUB_ROOM,load,sizeof(load),tpsnd_fn,PSND_OUT_R12,0,NULL,0,tpsnd_land);
        assert(n && n<=PSND_STUB_ROOM);
        tpsnd_fill();tpsnd_fn_answer=0x1200;FlushInstructionCache(GetCurrentProcess(),page,0x1000);
        tpsnd_launch(c,0);
        assert(tpsnd_fn_calls==1 && tpsnd_fn_rcx==tpsnd_in[TPS_RDI]+0x30 && tpsnd_out[TPS_R12]==0x1200 && tpsnd_kept(1u<<TPS_R12));}
    /* What does not fit is refused. */
    {   unsigned char big[17]={0};assert(!psnd_stub(c,PSND_STUB_ROOM,big,sizeof(big),tpsnd_fn,PSND_OUT_NONE,0,NULL,0,tpsnd_land));
        assert(!psnd_stub(c,0x40,big,4,tpsnd_fn,PSND_OUT_NONE,0,NULL,0,tpsnd_land) && !psnd_stub(c,PSND_STUB_ROOM,big,4,tpsnd_fn,PSND_OUT_RAX+1,0,NULL,0,tpsnd_land));}
    VirtualFree(page,0,MEM_RELEASE);
    printf("player sound stubs: registers, flags, xmm0..5, the stack and the call's alignment kept, answers in r9/r12, X4's own instructions run; tests PASS\n");
}

/* ---- the rules, slots 2 and 3 at once ---- */
static unsigned char tpsnd_p1[0x80],tpsnd_p[2][0x80],tpsnd_child[2][0x80],tpsnd_grandchild[2][0x80],tpsnd_other[0x80];
static unsigned char tpsnd_computer[3][0x40];   /* the ship computers of players 1, 2, 3 */
static void *tpsnd_computer_of(void *player,void **out) {
    *out=player==tpsnd_p1?tpsnd_computer[0]:player==tpsnd_p[0]?tpsnd_computer[1]:player==tpsnd_p[1]?tpsnd_computer[2]:NULL;
    return out;
}
static unsigned tpsnd_cue_calls,tpsnd_cue_slot[4];static void *tpsnd_cue_source[4];
static unsigned tpsnd_event_type(void *event) {(void)event;return 91u;}
static void tpsnd_cue(void *cue,void *source,double now,unsigned type,void *event) {
    (void)cue;(void)event;assert(type==91u && now==7.0);
    if(tpsnd_cue_calls<4) {tpsnd_cue_source[tpsnd_cue_calls]=source;tpsnd_cue_slot[tpsnd_cue_calls]=pvehk_forward_slot;}
    ++tpsnd_cue_calls;
}
static unsigned char tpsnd_entry[0x18];static unsigned char *tpsnd_lookup_source;
static unsigned char *tpsnd_lookup(void *source,unsigned type) {assert(type==91u);tpsnd_lookup_source=source;return tpsnd_entry;}
static unsigned tpsnd_owner(const unsigned char *cue) {(void)cue;return 0;}
static unsigned tpsnd_posts;
static uintptr_t tpsnd_post(void *source,void *event,double time,unsigned char immediate) {
    assert(source==tpsnd_computer[2] && event && time==7.0 && immediate==1);++tpsnd_posts;return 0x55;
}
static void test_player_sound_rules(void) {
    const pslot_slot kept2=pslots[2],kept3=pslots[3];unsigned char *const first=pslot_p1,*const base=pnat_base,*const page_was=pslot_page;
    const unsigned slot_was=pres_slot,scope_was=pstk_scope_slot;const DWORD ui_was=ui_thread;const int active_was=pres_active;
    memset(&pslots[2],0,sizeof(pslots[2]));memset(&pslots[3],0,sizeof(pslots[3]));
    memset(tpsnd_p1,0,sizeof(tpsnd_p1));memset(tpsnd_p,0,sizeof(tpsnd_p));memset(tpsnd_child,0,sizeof(tpsnd_child));memset(tpsnd_grandchild,0,sizeof(tpsnd_grandchild));
    for(unsigned i=0;i<2;i++) {
        unsigned char *up=tpsnd_p[i];memcpy(tpsnd_child[i]+0x70,&up,8);up=tpsnd_child[i];memcpy(tpsnd_grandchild[i]+0x70,&up,8);
        pslots[2+i].player=tpsnd_p[i];pslots[2+i].seated=1;
    }
    {unsigned char *up=tpsnd_p1;memcpy(tpsnd_other+0x70,&up,8);}   /* below player 1: nobody's slot */
    pslot_p1=tpsnd_p1;
    /* The sound's source: the emitter (an object's +0x30) of this client's own slot's player, or of something below it,
     * becomes X4's own player's; another slot's stays as X4 had it; X4's own player's (or below it), started outside
     * every scope and not as a voice line, is slot 1's: it gets this client's own slot player's (no place); the world's
     * stays. */
    {   const uintptr_t p1e=(uintptr_t)(tpsnd_p1+0x30),oe=(uintptr_t)(tpsnd_other+0x30),ui=0x1234,speech=(uintptr_t)(pnat_base+PRES_SOURCE_SPEECH);
        const uintptr_t e2=(uintptr_t)(tpsnd_p[0]+0x30),e3=(uintptr_t)(tpsnd_p[1]+0x30),c2=(uintptr_t)(tpsnd_child[0]+0x30),g3=(uintptr_t)(tpsnd_grandchild[1]+0x30);
        static unsigned char world[0x80];const uintptr_t we=(uintptr_t)(world+0x30);memset(world,0,sizeof(world));
        pres_source_starts=pres_source_moved=pres_source_kept=pres_source_parked=0;pstk_scope_slot=0;pres_open=0;pres_active=1;
        assert(pres_source_slot(tpsnd_p[0])==2 && pres_source_slot(tpsnd_grandchild[1])==3 && pres_source_slot(tpsnd_other)==1 && pres_source_slot(tpsnd_p1)==1 &&
               !pres_source_slot(world) && !pres_source_slot(NULL));
        pres_source_installed=0;pres_slot=3;assert(pres_source_rule(e3,ui)==e3 && pres_source_rule(p1e,ui)==p1e);   /* not installed: X4's */
        pres_source_installed=1;
        pres_slot=3;   /* client 3 */
        assert(pres_source_rule(e3,ui)==p1e && pres_source_rule(g3,speech)==p1e && pres_source_rule(e2,ui)==e2 && pres_source_rule(c2,ui)==c2);
        assert(pres_source_rule(p1e,ui)==e3 && pres_source_rule(oe,ui)==e3 && pres_source_rule(p1e,speech)==p1e);   /* slot 1's: parked; a voice line: the mute's */
        assert(pres_source_rule(we,ui)==we && pres_source_rule(0x30,ui)==0x30 && !pres_source_rule(0,ui));
        pstk_scope_slot=3;assert(pres_source_rule(p1e,ui)==p1e);pstk_scope_slot=2;assert(pres_source_rule(p1e,ui)==p1e);pstk_scope_slot=0;   /* in a slot's scope: not slot 1's */
        pres_open=1;assert(pres_source_rule(p1e,ui)==p1e);pres_open=0;                            /* this client's presentation */
        pres_active=0;assert(pres_source_rule(p1e,ui)==p1e);pres_active=1;                        /* not presenting: its camera is slot 1's */
        pslots[3].seated=0;assert(pres_source_rule(p1e,ui)==p1e);pslots[3].seated=1;
        assert(pres_source_moved==2 && pres_source_kept==2 && pres_source_parked==2 && pres_source_starts==17);
        pres_slot=2;   /* client 2, the same world */
        assert(pres_source_rule(e2,ui)==p1e && pres_source_rule(c2,ui)==p1e && pres_source_rule(e3,ui)==e3 && pres_source_rule(g3,ui)==g3 && pres_source_rule(p1e,ui)==e2);
        assert(pres_source_moved==4 && pres_source_kept==4 && pres_source_parked==3);
        pres_slot=0;assert(pres_source_rule(e2,ui)==e2 && pres_source_rule(p1e,ui)==p1e);         /* client 1: never */
        pslot_p1=NULL;pres_slot=2;assert(pres_source_rule(e2,ui)==e2);pslot_p1=tpsnd_p1;
        {unsigned char *const own=pslots[2].player;pslots[2].player=NULL;assert(pres_source_rule(p1e,ui)==p1e);pslots[2].player=own;}   /* no own player: X4's */
        pres_source_installed=0;pres_active=active_was;}
    /* The ship computer: slot k's computer's signal goes to the cues nobody owns on player 1's computer, inside slot k's
     * scope, once (the object form), with the forward's slot k; X4's post after it. Slots 2 and 3 each their own. */
    {   static void *event_vt[2],*event[4],*cue[2],*listeners[1];void *slot_was=NULL;
        const int on_was=pvehk_forward_on;memcpy(&slot_was,image+SCRIPT_RNG_CUE_VT+0x20,8);
        pnat_base=image;ui_thread=GetCurrentThreadId();pvehk_computer_fn=tpsnd_computer_of;pvehk_computer_on=1;pvehk_computer_installed=1;
        for(unsigned i=0;i<3;i++) {memset(tpsnd_computer[i],0,sizeof(tpsnd_computer[i]));const uint64_t id=0x9100+i;memcpy(tpsnd_computer[i]+8,&id,8);}
        event_vt[1]=(void*)tpsnd_event_type;event[0]=event_vt;
        cue[0]=image+SCRIPT_RNG_CUE_VT;{void *fn=(void*)tpsnd_cue;memcpy(image+SCRIPT_RNG_CUE_VT+0x20,&fn,8);}
        listeners[0]=cue;{void *b0=listeners,*b1=listeners+1;memcpy(tpsnd_entry+8,&b0,8);memcpy(tpsnd_entry+0x10,&b1,8);}
        pvehk_forward_lookup_test=tpsnd_lookup;pvehk_forward_owner_test=tpsnd_owner;pvehk_forward_on=1;
        pvehk_computer_seen=pvehk_computer_forwarded=pvehk_computer_unscoped=0;
        assert(pvehk_computer_slot(tpsnd_computer[1])==2 && pvehk_computer_slot(tpsnd_computer[2])==3 && !pvehk_computer_slot(tpsnd_computer[0]) && !pvehk_computer_slot(NULL));
        tpsnd_cue_calls=0;pstk_scope_slot=3;pvehk_forward_slot=0;
        pvehk_computer_note(tpsnd_computer[2],(unsigned char*)event,7.0);
        assert(tpsnd_cue_calls==1 && tpsnd_cue_source[0]==tpsnd_computer[0] && tpsnd_cue_slot[0]==3 && tpsnd_lookup_source==tpsnd_computer[0] &&
               pvehk_computer_forwarded==1 && !pvehk_forward_slot && pstk_scope_slot==3);
        pstk_scope_slot=2;
        pvehk_computer_note(tpsnd_computer[1],(unsigned char*)event,7.0);
        assert(tpsnd_cue_calls==2 && tpsnd_cue_source[1]==tpsnd_computer[0] && tpsnd_cue_slot[1]==2 && pvehk_computer_forwarded==2);
        /* another slot's scope open (slot 3's computer in slot 2's scope): its scope cannot open here, nothing forwarded */
        pvehk_computer_note(tpsnd_computer[2],(unsigned char*)event,7.0);
        assert(tpsnd_cue_calls==2 && pvehk_computer_unscoped==1 && pstk_scope_slot==2);
        /* player 1's computer, nobody's, no event, a slot off its seat, the rule off: nothing */
        pvehk_computer_note(tpsnd_computer[0],(unsigned char*)event,7.0);pvehk_computer_note(tpsnd_other,(unsigned char*)event,7.0);
        pvehk_computer_note(tpsnd_computer[1],NULL,7.0);
        pslots[2].seated=0;pvehk_computer_note(tpsnd_computer[1],(unsigned char*)event,7.0);pslots[2].seated=1;
        pvehk_computer_installed=0;pvehk_computer_note(tpsnd_computer[1],(unsigned char*)event,7.0);pvehk_computer_installed=1;
        assert(tpsnd_cue_calls==2 && pvehk_computer_forwarded==2 && pvehk_computer_seen==6);
        /* the disengaged site's call: the note, then X4's post with every argument, its answer back */
        pstk_scope_slot=3;pvehk_computer_post_test=tpsnd_post;tpsnd_posts=0;
        assert(pvehk_computer_post(tpsnd_computer[2],(unsigned char*)event,7.0,1)==0x55 && tpsnd_posts==1 && tpsnd_cue_calls==3 && tpsnd_cue_slot[2]==3);
        pvehk_computer_post_test=NULL;
        /* The notices' rule reads a computer's ID as its slot's (lockstep_mission_owners.h): 1, 2, 3; not with the rule off. */
        assert(mpp_slot_by_id(0x9100)==1 && mpp_slot_by_id(0x9101)==2 && mpp_slot_by_id(0x9102)==3 && !mpp_slot_by_id(0x9103) && !mpp_slot_by_id(0));
        assert(pvehk_computer_slot_by_id(0x9102)==3);
        pslots[3].seated=0;assert(!pvehk_computer_slot_by_id(0x9102));pslots[3].seated=1;
        pvehk_computer_installed=0;assert(!mpp_slot_by_id(0x9101) && !mpp_slot_by_id(0x9100));
        char status[128];assert(pvehk_computer_status(status,sizeof(status))>0 && !strcmp(status," computer=1/0/7/3/1"));
        pvehk_forward_lookup_test=NULL;pvehk_forward_owner_test=NULL;pvehk_forward_on=on_was;pvehk_computer_fn=NULL;pvehk_computer_on=0;
        memcpy(image+SCRIPT_RNG_CUE_VT+0x20,&slot_was,8);pvehk_computer_seen=pvehk_computer_forwarded=pvehk_computer_unscoped=0;}
    /* The sites on a region of the image's shape: each one ours by CAS, the stub on the near page, the tick check names
     * a lost one (221), each back by CAS. */
    {   unsigned char *const block=VirtualAlloc(NULL,0x20000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(block);
        unsigned char *const region=block-0x72b000u;   /* the RVAs 0x72b000.. in the block's first 64 KiB */
        unsigned char *const npage=block+0x10000;
        pnat_base=region;pslot_page=npage;psnd_why="-";
        memcpy(region+PVEHK_COMPUTER_ENGAGED,pvehk_computer_movs,6);
        {const int32_t rel=(int32_t)(PVEHK_EVENT_POST_NOW-(PVEHK_COMPUTER_DISENGAGED+5u));region[PVEHK_COMPUTER_DISENGAGED]=0xe8;memcpy(region+PVEHK_COMPUTER_DISENGAGED+1,&rel,4);}
        unsigned char disengaged[5];memcpy(disengaged,region+PVEHK_COMPUTER_DISENGAGED,5);
        pvehk_computer_on=1;pvehk_computer_install();
        assert(pvehk_computer_installed && psnd_installed() && psnd_check() && pvehk_computer_intact());
        {int32_t rel;memcpy(&rel,region+PVEHK_COMPUTER_ENGAGED+1,4);
         assert(region[PVEHK_COMPUTER_ENGAGED]==0xe9 && region[PVEHK_COMPUTER_ENGAGED+5]==0x90 && region+PVEHK_COMPUTER_ENGAGED+5+rel==npage+PVEHK_COMPUTER_ENGAGED_STUB);
         memcpy(&rel,region+PVEHK_COMPUTER_DISENGAGED+1,4);assert(region+PVEHK_COMPUTER_DISENGAGED+5+rel==npage+PVEHK_COMPUTER_DISENGAGED_STUB);
         void *to=NULL;memcpy(&to,npage+PVEHK_COMPUTER_DISENGAGED_STUB+6,8);assert(to==(void*)pvehk_computer_post);
         assert(!memcmp(npage+PVEHK_COMPUTER_ENGAGED_STUB,"\x50\x51\x52",3));}
        region[PVEHK_COMPUTER_ENGAGED+5]=0xcc;assert(!psnd_check() && !strcmp(psnd_why,"computer_site_lost"));region[PVEHK_COMPUTER_ENGAGED+5]=0x90;psnd_why="-";
        pvehk_computer_restore();
        assert(!pvehk_computer_installed && !memcmp(region+PVEHK_COMPUTER_ENGAGED,pvehk_computer_movs,6) && !memcmp(region+PVEHK_COMPUTER_DISENGAGED,disengaged,5));
        pvehk_computer_on=0;pvehk_computer_install();assert(!pvehk_computer_installed);   /* not armed: nothing */
        /* the sound start's entry */
        pnat_base=block-0x14e8000u;memcpy(pnat_base+PRES_SOURCE,pres_source_head,PRES_SOURCE_HEAD);
        assert(pres_source_install() && pres_source_installed && psnd_check());
        {int32_t rel;memcpy(&rel,pnat_base+PRES_SOURCE+1,4);
         assert(pnat_base[PRES_SOURCE]==0xe9 && pnat_base[PRES_SOURCE+5]==0x90 && pnat_base[PRES_SOURCE+6]==0x90 && pnat_base+PRES_SOURCE+5+rel==npage+PRES_SOURCE_STUB);}
        pnat_base[PRES_SOURCE+6]=0x48;assert(!psnd_check() && !strcmp(psnd_why,"source_entry_lost"));pnat_base[PRES_SOURCE+6]=0x90;psnd_why="-";
        pres_source_uninstall();assert(!pres_source_installed && !memcmp(pnat_base+PRES_SOURCE,pres_source_head,PRES_SOURCE_HEAD) && !psnd_installed());
        pnat_base[PRES_SOURCE]=0xcc;assert(!pres_source_install() && !pres_source_installed);   /* not X4's bytes: refused */
        VirtualFree(block,0,MEM_RELEASE);}
    pslots[2]=kept2;pslots[3]=kept3;pslot_p1=first;pnat_base=base;pslot_page=page_was;pres_slot=slot_was;pstk_scope_slot=scope_was;ui_thread=ui_was;
    pres_source_starts=pres_source_moved=pres_source_kept=pres_source_parked=0;psnd_why="-";
    printf("player sound rules: the source for clients 2 and 3, the computers of slots 2 and 3 forwarded in their scopes, the notices' IDs, the sites and the tick check; tests PASS\n");
}

/* ---- a sound's first play and the own ship's sound set (stop 223), slots 2 and 3 ---- */
/* Every register as it came but the listed ones, xmm0..5, rsp; not the flags (the play's tail tests the answer). */
static int tpsnd_regs_kept(unsigned except) {
    for(unsigned r=0;r<TPS_REGS;r++) if(!(except>>r&1u) && tpsnd_out[r]!=tpsnd_in[r]) {printf("register %u changed\n",r);return 0;}
    for(unsigned i=0;i<96;i++) if(tpsnd_xout[i]!=tpsnd_xin[i]) {printf("xmm byte %u changed\n",i);return 0;}
    return tpsnd_land_rsp==tpsnd_at;
}
#define TPSND_STATION_ID 0x7790u
static void *tpsnd_vt_room[1146],*tpsnd_vt_station[1146];
static unsigned char tpsnd_ships[3][0x100],tpsnd_rooms[4][0x100],tpsnd_station[0x100];   /* room i in ship i; room 3 in the station */
static unsigned tpsnd_ship_starts,tpsnd_ship_stops,tpsnd_station_calls;static void *tpsnd_started,*tpsnd_stopped,*tpsnd_name_seen;
static uint32_t tpsnd_kind_seen;static unsigned char tpsnd_flag_seen;static int tpsnd_gone=-1;
/* A station as X4 has it: an 'object' (class 0x49) as a ship is, and no sound function 1143 slots in (live: what lies
 * behind U::Station's vtable there faulted). */
static unsigned char tpsnd_is_object(void *p,int cls) {(void)p;return cls==0x49;}
static void tpsnd_station_slot(void) {++tpsnd_station_calls;*(volatile int*)(uintptr_t)8=1;}
static void tpsnd_ship_start(void *ship,psctx_list *list,void *name,unsigned char flag) {
    assert(list->capacity==4 && list->count==1 && !pres_open);
    tpsnd_kind_seen=list->inline_[0];tpsnd_name_seen=name;tpsnd_flag_seen=flag;tpsnd_started=ship;++tpsnd_ship_starts;
}
static void tpsnd_ship_stop(void *ship,psctx_list *list) {
    assert(list->capacity==4 && list->count==1 && list->inline_[0]==tpsnd_kind_seen);tpsnd_stopped=ship;++tpsnd_ship_stops;
}
static void tpsnd_ship_fault(void *ship,psctx_list *list,void *name,unsigned char flag) {(void)ship;(void)list;(void)name;(void)flag;*(volatile int*)(uintptr_t)8=1;}
static void *tpsnd_find_ship(uint64_t id) {
    for(int i=0;i<3;i++) {uint64_t own=0;memcpy(&own,tpsnd_ships[i]+8,8);if(own==id) return i==tpsnd_gone?NULL:tpsnd_ships[i];}
    return id==TPSND_STATION_ID?tpsnd_station:NULL;
}
static void test_player_sound_play(void) {
    const pslot_slot kept2=pslots[2],kept3=pslots[3],kept8=pslots[8];unsigned char *const base=pnat_base,*const page_was=pslot_page;
    const unsigned slot_was=pres_slot;const DWORD ui_was=ui_thread;const int active_was=pres_active,sound_was=pres_sound_installed,open_was=pres_open;
    static unsigned char player8[0x80];
    /* The stub, executed: the rule's answer in rax; 1: X4's two instructions (rdx 0, rcx the voice in r13) and on to the
     * call; 0: on past it with every register as it came. Inside a body (rsp 16-aligned), the rule's call 16-aligned. */
    {   unsigned char *const page=VirtualAlloc(NULL,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);assert(page);
        const unsigned n=pres_play_stub(page,PSND_STUB_ROOM,tpsnd_fn,tpsnd_land,tpsnd_land);
        assert(n && n<=PSND_STUB_ROOM && !pres_play_stub(page+0x200,0x40,tpsnd_fn,tpsnd_land,tpsnd_land));
        FlushInstructionCache(GetCurrentProcess(),page,0x1000);
        tpsnd_fill();tpsnd_fn_answer=1;tpsnd_launch(page,0);
        assert(tpsnd_fn_calls==1 && tpsnd_fn_rcx==tpsnd_in[TPS_R13] && (tpsnd_fn_rsp&15u)==8u);
        assert(tpsnd_out[TPS_RAX]==1 && tpsnd_out[TPS_RDX]==0 && tpsnd_out[TPS_RCX]==tpsnd_in[TPS_R13] && tpsnd_regs_kept(1u<<TPS_RAX|1u<<TPS_RCX|1u<<TPS_RDX));
        tpsnd_fill();tpsnd_fn_answer=0;tpsnd_launch(page,0);
        assert(tpsnd_fn_calls==1 && tpsnd_out[TPS_RAX]==0 && tpsnd_regs_kept(1u<<TPS_RAX));
        /* the two jumps: the call's, then the one past it */
        void *to=NULL;memcpy(&to,page+n-8,8);assert(to==(void*)tpsnd_land && page[n-14]==0xff && page[n-13]==0x25);
        memcpy(&to,page+n-22,8);assert(to==(void*)tpsnd_land && page[n-28]==0xff && page[n-27]==0x25);
        assert(!memcmp(page+n-28-sizeof(pres_play_tail),pres_play_tail,sizeof(pres_play_tail)));
        VirtualFree(page,0,MEM_RELEASE);}
    /* The rule, clients 3, 2 and 8: a positional voice with an emitter started outside the presentation scope (no
     * scope open, or another thread while one is) waits for X4's sound update; every other voice plays at once. */
    {   static unsigned char voice[0x100],emitter[0x80];unsigned char *e=emitter;
        memset(voice,0,sizeof(voice));{const uint32_t f=1;memcpy(voice+PRES_VOICE_FLAGS,&f,4);}voice[PRES_VOICE_PLACED]=1;memcpy(voice+PRES_VOICE_EMITTER,&e,8);
        pres_play_installed=1;pres_active=1;pres_sound_installed=1;ui_thread=GetCurrentThreadId();pres_open=0;
        pres_play_starts=pres_play_deferred=0;
        static const unsigned clients[3]={3,2,8};
        for(unsigned c=0;c<3;c++) {
            const unsigned k=clients[c];
            pres_slot=k;pslots[k].seated=1;
            assert(pres_play_rule(voice)==0);                                                       /* the simulation's start: later */
            pres_open=1;assert(pres_play_rule(voice)==1);                                           /* in the scope: now */
            ui_thread=GetCurrentThreadId()+1;assert(pres_play_rule(voice)==0);ui_thread=GetCurrentThreadId();   /* another thread */
            pres_open=0;
            voice[PRES_VOICE_PLACED]=0;assert(pres_play_rule(voice)==1);voice[PRES_VOICE_PLACED]=1;  /* no place (2D): now */
            {void *none=NULL;memcpy(voice+PRES_VOICE_EMITTER,&none,8);assert(pres_play_rule(voice)==1);memcpy(voice+PRES_VOICE_EMITTER,&e,8);}
            pslots[k].seated=0;assert(pres_play_rule(voice)==1);pslots[k].seated=1;                 /* not seated: X4's */
            {const uint32_t f=0x20;memcpy(voice+PRES_VOICE_FLAGS,&f,4);assert(pres_play_rule(voice)==0);const uint32_t g=1;memcpy(voice+PRES_VOICE_FLAGS,&g,4);}   /* no play bit: X4's je */
        }
        assert(pres_play_starts==18 && pres_play_deferred==6);
        pres_slot=0;assert(pres_play_rule(voice)==1);pres_slot=2;                                  /* client 1: never */
        pres_active=0;assert(pres_play_rule(voice)==1);pres_active=1;
        pres_sound_installed=0;assert(pres_play_rule(voice)==1);pres_sound_installed=1;            /* the update not in the scope */
        pres_play_installed=0;assert(pres_play_rule(voice)==1);
        assert(!pres_play_rule(NULL) && pres_play_deferred==6);
        char status[96];pres_play=1;assert(pres_play_status(status,sizeof(status))>0 && !strcmp(status," play=1/0/22/6"));pres_play=0;
        pres_play_starts=pres_play_deferred=0;}
    /* The site on a region of the image's shape: ours by CAS (a jump, two nops), the stub on the near page with its two
     * jumps, the tick check names a lost one (223), X4's bytes back; other bytes refused. */
    {   unsigned char *const block=VirtualAlloc(NULL,0x20000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(block);
        unsigned char *const region=block-0x14e9000u,*const npage=block+0x10000;
        pnat_base=region;pslot_page=npage;psctx_check_why="-";psctx_why="-";
        memcpy(region+PRES_PLAY_SITE,pres_play_head,PRES_PLAY_SIZE);
        assert(((PRES_PLAY_SITE+PRES_PLAY_SIZE-1u)&~15u)==(PRES_PLAY_SITE&~15u));
        assert(pres_play_install() && pres_play_installed && psctx_check());
        {int32_t rel;memcpy(&rel,region+PRES_PLAY_SITE+1,4);
         assert(region[PRES_PLAY_SITE]==0xe9 && region[PRES_PLAY_SITE+5]==0x90 && region[PRES_PLAY_SITE+6]==0x90 && region+PRES_PLAY_SITE+5+rel==npage+PRES_PLAY_STUB);
         assert(!memcmp(npage+PRES_PLAY_STUB,"\x50\x51\x52",3));
         unsigned at=0;for(unsigned i=0;i+14<=PSND_STUB_ROOM;i++) if(npage[PRES_PLAY_STUB+i]==0xff && npage[PRES_PLAY_STUB+i+1]==0x25) {
             void *to=NULL;memcpy(&to,npage+PRES_PLAY_STUB+i+6,8);
             if(to==(void*)(region+PRES_PLAY_CALL)) {void *next=NULL;memcpy(&next,npage+PRES_PLAY_STUB+i+20,8);assert(next==(void*)(region+PRES_PLAY_AFTER));at=i;break;}
         }
         assert(at);}
        region[PRES_PLAY_SITE+6]=0xcc;assert(!psctx_check() && !strcmp(psctx_check_why,"play_site_lost"));region[PRES_PLAY_SITE+6]=0x90;psctx_check_why="-";
        assert(psctx_check());
        pres_play_uninstall();assert(!pres_play_installed && !memcmp(region+PRES_PLAY_SITE,pres_play_head,PRES_PLAY_SIZE) && psctx_check());
        region[PRES_PLAY_SITE]=0xcc;assert(!pres_play_install() && !pres_play_installed);
        VirtualFree(block,0,MEM_RELEASE);}
    /* The own ship's sound set for clients 2, 3 and 8: X4's start (slot 1143, its list, name and inside flag) on the
     * ship slot k's player is in (each of the seven ship classes, by its vtable), its stop (slot 1144) on the one it
     * left, found again by its ID (a destroyed one not); a room of a station (an 'object' too, with other functions
     * there: the live stop 223 of 26 Nov) and an unseated slot have none; the end of presenting stops the last; a fault
     * ends the module. The region holds the seven vtables and the list's entry where the image has them. */
    {   const unsigned first=0x2b78000u,vt_end=0x2b9f000u,kind_page=0x395d000u,end=0x395f000u;
        unsigned char *const block=VirtualAlloc(NULL,end-first,MEM_RESERVE,PAGE_NOACCESS);assert(block);
        unsigned char *const region=block-first;pnat_base=region;
        assert(VirtualAlloc(region+first,vt_end-first,MEM_COMMIT,PAGE_READWRITE) && VirtualAlloc(region+kind_page,end-kind_page,MEM_COMMIT,PAGE_READWRITE));
        {const uint32_t kind=0x5a;memcpy(region+PSCTX_KIND,&kind,4);}
        for(unsigned i=0;i<SS_VTABLES;i++) {
            assert(ss_vtables[i]>=first && ss_vtables[i]+PSCTX_STOP_SLOT+8u<=vt_end);
            void *fn=(void*)tpsnd_ship_start;memcpy(region+ss_vtables[i]+PSCTX_START_SLOT,&fn,8);
            fn=(void*)tpsnd_ship_stop;memcpy(region+ss_vtables[i]+PSCTX_STOP_SLOT,&fn,8);
        }
        tpsnd_vt_station[0x11e8/8]=(void*)tpsnd_is_object;tpsnd_vt_station[PSCTX_START_SLOT/8]=tpsnd_vt_station[PSCTX_STOP_SLOT/8]=(void*)tpsnd_station_slot;
        {void *vt=tpsnd_vt_station;const uint64_t id=TPSND_STATION_ID;memset(tpsnd_station,0,sizeof(tpsnd_station));memcpy(tpsnd_station,&vt,8);memcpy(tpsnd_station+8,&id,8);}
        for(int i=0;i<4;i++) {
            void *vt=tpsnd_vt_room;memset(tpsnd_rooms[i],0,sizeof(tpsnd_rooms[i]));memcpy(tpsnd_rooms[i],&vt,8);
            unsigned char *up=i<3?tpsnd_ships[i]:tpsnd_station;memcpy(tpsnd_rooms[i]+0x70,&up,8);
            if(i<3) {memset(tpsnd_ships[i],0,sizeof(tpsnd_ships[i]));const uint64_t id=0x7700+(uint64_t)i;memcpy(tpsnd_ships[i]+8,&id,8);}
        }
        psctx_find=tpsnd_find_ship;tpsnd_gone=-1;tpsnd_station_calls=0;
        static const unsigned clients[3]={2,3,8};
        for(unsigned c=0;c<3;c++) {
            const unsigned k=clients[c];pres_slot=k;
            for(unsigned i=0;i<3;i++) {   /* nine ships over the seven classes */
                unsigned char *const vt=region+ss_vtables[(c*3u+i)%SS_VTABLES];memcpy(tpsnd_ships[i],&vt,8);
                assert(psctx_is_ship(tpsnd_ships[i]) && psctx_ship_of(tpsnd_rooms[i])==tpsnd_ships[i] && psctx_ship_of(tpsnd_ships[i])==tpsnd_ships[i]);
            }
            assert(!psctx_is_ship(tpsnd_station) && !psctx_is_ship(tpsnd_rooms[3]) && !psctx_is_ship(NULL) && !psctx_ship_of(tpsnd_rooms[3]) && !psctx_ship_of(NULL));
            pslot_slot *s=&pslots[k];memset(s,0,sizeof(*s));s->player=k==8?player8:tpsnd_p[k-2];s->seated=1;s->room=tpsnd_rooms[0];
            psctx_installed=1;psctx_ship_id=0;psctx_why="-";tpsnd_ship_starts=tpsnd_ship_stops=0;psctx_starts=psctx_stops=psctx_faults=0;pres_open=0;
            psctx_follow(s);
            assert(tpsnd_ship_starts==1 && tpsnd_started==tpsnd_ships[0] && tpsnd_kind_seen==0x5a && tpsnd_name_seen==(void*)(region+PSCTX_NAME) &&
                   tpsnd_flag_seen==1 && psctx_ship_id==0x7700 && !tpsnd_ship_stops);
            psctx_follow(s);assert(tpsnd_ship_starts==1 && !tpsnd_ship_stops);                       /* the same ship: nothing */
            s->room=tpsnd_rooms[1];psctx_follow(s);                                                  /* a ship change */
            assert(tpsnd_ship_stops==1 && tpsnd_stopped==tpsnd_ships[0] && tpsnd_ship_starts==2 && tpsnd_started==tpsnd_ships[1] && psctx_ship_id==0x7701);
            s->walk_room=tpsnd_rooms[3];psctx_follow(s);                                             /* out of it, on foot on a station's dock */
            assert(tpsnd_ship_stops==2 && tpsnd_stopped==tpsnd_ships[1] && tpsnd_ship_starts==2 && !psctx_ship_id);
            assert(!tpsnd_station_calls && !psctx_faults && psctx_installed && psctx_check());       /* the station: no call, no stop */
            psctx_follow(s);assert(tpsnd_ship_stops==2 && tpsnd_ship_starts==2 && !tpsnd_station_calls);   /* walking on there: nothing */
            s->walk_room=tpsnd_rooms[2];psctx_follow(s);assert(tpsnd_ship_starts==3 && tpsnd_started==tpsnd_ships[2]);   /* on foot in another ship */
            s->walk_room=NULL;tpsnd_gone=2;psctx_follow(s);                                          /* that one destroyed: no stop; back in its own */
            assert(tpsnd_ship_stops==2 && tpsnd_ship_starts==4 && tpsnd_started==tpsnd_ships[1] && psctx_ship_id==0x7701);tpsnd_gone=-1;
            s->seated=0;psctx_follow(s);assert(tpsnd_ship_stops==3 && tpsnd_stopped==tpsnd_ships[1] && !psctx_ship_id);   /* unseated */
            s->seated=1;psctx_follow(s);assert(tpsnd_ship_starts==5 && psctx_ship_id==0x7701);
            psctx_end();assert(tpsnd_ship_stops==4 && tpsnd_stopped==tpsnd_ships[1] && !psctx_ship_id && !psctx_installed);
            psctx_follow(s);assert(tpsnd_ship_starts==5);                                            /* ended: nothing */
            assert(psctx_starts==5 && psctx_stops==4 && psctx_check());
        }
        {char status[96];psctx=1;psctx_installed=1;psctx_ship_id=0x7701;assert(psctx_status(status,sizeof(status))>0 && !strcmp(status," context=1/1/5/4/0/7701"));psctx=0;}
        /* the one it left is no ship (found by an ID that is a station's): no stop there, on to the new one; the end too */
        pslots[2].room=tpsnd_rooms[0];pslots[2].walk_room=NULL;psctx_installed=1;psctx_ship_id=TPSND_STATION_ID;psctx_why="-";psctx_check_why="-";
        tpsnd_ship_starts=tpsnd_ship_stops=0;psctx_starts=psctx_stops=psctx_faults=0;
        psctx_follow(&pslots[2]);
        assert(!tpsnd_station_calls && !tpsnd_ship_stops && !psctx_stops && tpsnd_ship_starts==1 && tpsnd_started==tpsnd_ships[0] && psctx_ship_id==0x7700);
        psctx_ship_id=TPSND_STATION_ID;psctx_end();assert(!tpsnd_station_calls && !tpsnd_ship_stops && !psctx_faults && !psctx_ship_id && !psctx_installed && psctx_check());
        /* the stop row names the tick check's why (pilot_why_text) */
        {const int armed_was=pilot_armed,on_was=psctx;char why[1024];pilot_armed=1;psctx=1;psctx_check_why="context_fault";
         assert(pilot_why_text(why,sizeof(why)) && strstr(why," sound_context=context_fault"));
         psctx_check_why="-";assert(pilot_why_text(why,sizeof(why)) && strstr(why," sound_context=-"));
         psctx=0;assert(!psctx_armed_any() && pilot_why_text(why,sizeof(why)) && !strstr(why," sound_context="));pilot_armed=armed_was;psctx=on_was;}
        /* a fault in X4's start: counted, the module ends, the tick check stops (223, context_fault) */
        {unsigned char *const vt=region+ss_vtables[0];void *fn=(void*)tpsnd_ship_fault;memcpy(tpsnd_ships[0],&vt,8);memcpy(vt+PSCTX_START_SLOT,&fn,8);}
        pslots[2].room=tpsnd_rooms[0];pslots[2].walk_room=NULL;psctx_installed=1;psctx_ship_id=0;psctx_why="-";psctx_check_why="-";
        psctx_starts=psctx_stops=psctx_faults=0;
        psctx_follow(&pslots[2]);
        assert(psctx_faults==1 && !psctx_installed && !psctx_ship_id && !strcmp(psctx_why,"context_fault") && !psctx_check() && !strcmp(psctx_check_why,"context_fault"));
        psctx_why="-";psctx_check_why="-";psctx_find=NULL;psctx_starts=psctx_stops=psctx_faults=0;
        VirtualFree(block,0,MEM_RELEASE);}
    pslots[2]=kept2;pslots[3]=kept3;pslots[8]=kept8;pnat_base=base;pslot_page=page_was;pres_slot=slot_was;ui_thread=ui_was;pres_active=active_was;
    pres_sound_installed=sound_was;pres_open=open_was;pres_play_installed=0;
    printf("player sound play: the stub run with both answers, the first play for clients 3, 2 and 8, its site and tick check, the own ship's sound set for slots 2, 3 and 8 (seven ship classes, none on a station), the stop row's why; tests PASS\n");
}

/* ---- the sound update in the presentation scope (called from test_lockstep_pilot_slots.h's client 3 section) ---- */
static unsigned char *tpsnd_manager,*tpsnd_room_seen,*tpsnd_expect_room;static unsigned tpsnd_moves,tpsnd_listens;static int tpsnd_scoped,tpsnd_check_listener=1;
static void tpsnd_listener(void *player,unsigned char flag) {
    assert(player==pslot_p1 && !flag && pres_open);
    unsigned char *room=NULL;memcpy(&room,pslot_p1+PRES_PARENT,8);
    unsigned char *const at=room+0x30;memcpy(tpsnd_manager+PRES_LISTENER_ROOM,&at,8);   /* X4: the camera anchor's room */
    ++tpsnd_listens;
}
static void tpsnd_move(void *self) {
    unsigned char *room=NULL,*listener=NULL;
    assert(self==(void*)tpsnd_manager);
    memcpy(&room,pslot_p1+PRES_PARENT,8);memcpy(&listener,tpsnd_manager+PRES_LISTENER_ROOM,8);
    tpsnd_scoped=pres_open!=0;tpsnd_room_seen=room;
    if(tpsnd_scoped) assert(room==tpsnd_expect_room && (!tpsnd_check_listener || listener==tpsnd_expect_room+0x30));
    ++tpsnd_moves;
}
static void test_player_sound_present(unsigned char *region,unsigned char *p1,unsigned char *room2,unsigned char *room3) {
    static unsigned char manager[0x100],kept_room[0x40];
    void (*const original_was)(void*)=pres_sound_original;void (*const listener_was)(void*,unsigned char)=pres_listener_fn;
    const unsigned slot_was=pres_slot;const int active_was=pres_active;unsigned char *parent_was=NULL,*manager_was=NULL;
    memcpy(&parent_was,p1+PRES_PARENT,8);memcpy(&manager_was,region+PRES_SOUND_MANAGER,8);
    tpsnd_manager=manager;memset(manager,0,sizeof(manager));
    {unsigned char *m=manager,*k=kept_room;memcpy(region+PRES_SOUND_MANAGER,&m,8);memcpy(manager+PRES_LISTENER_ROOM,&k,8);}
    pres_sound_original=tpsnd_move;pres_listener_fn=tpsnd_listener;
    pres_sound_scopes=pres_sound_plain=pres_sound_listener=pres_sound_faults=0;tpsnd_moves=tpsnd_listens=0;
    /* Client 3, then client 2 (the same world): X4's update in that slot's scope, the listener's room that slot's room
     * for it, the simulation's player and the sound manager's room back after it. */
    unsigned char *const rooms[2]={room2,room3};
    for(unsigned k=3;k>=2;k--) {
        pres_slot=k;pres_active=1;pres_open=0;tpsnd_expect_room=rooms[k-2];
        pres_sound_move(manager);
        unsigned char *now=NULL,*listener=NULL;memcpy(&now,p1+PRES_PARENT,8);memcpy(&listener,manager+PRES_LISTENER_ROOM,8);
        assert(tpsnd_scoped && tpsnd_room_seen==rooms[k-2] && !pres_open && now==parent_was && listener==kept_room);
    }
    assert(tpsnd_moves==2 && tpsnd_listens==2 && pres_sound_scopes==2 && pres_sound_listener==2 && !pres_sound_plain && !pres_sound_faults);
    /* Not presenting (slot 1's client, the window over, a slot off its seat): X4's update alone, as before. */
    pres_active=0;pres_sound_move(manager);
    assert(tpsnd_moves==3 && !tpsnd_scoped && tpsnd_listens==2 && pres_sound_plain==1);
    /* No sound manager: the update in the scope all the same, no listener's call. */
    {void *none=NULL;memcpy(region+PRES_SOUND_MANAGER,&none,8);}
    pres_active=1;pres_slot=3;pres_open=0;tpsnd_expect_room=room3;tpsnd_check_listener=0;
    pres_sound_move(manager);
    assert(tpsnd_moves==4 && tpsnd_scoped && tpsnd_room_seen==room3 && tpsnd_listens==2 && pres_sound_scopes==3 && pres_sound_listener==2 && !pres_open);
    tpsnd_check_listener=1;{unsigned char *m=manager;memcpy(region+PRES_SOUND_MANAGER,&m,8);}
    /* The vtable entry: ours by CAS, checked at the gate tick (221), X4's again at the unhook. */
    {   void *entry=(void*)tpsnd_move;memcpy(region+PRES_SOUND_ENTRY,&entry,8);psnd_why="-";
        assert(pres_sound_install() && pres_sound_installed && *(void**)(region+PRES_SOUND_ENTRY)==(void*)pres_sound_move && psnd_check());
        void (*const through)(void*)=*(void(**)(void*))(region+PRES_SOUND_ENTRY);
        tpsnd_expect_room=room3;pres_open=0;through(manager);assert(tpsnd_moves==5 && tpsnd_scoped && pres_sound_scopes==4 && pres_sound_listener==3);
        memcpy(region+PRES_SOUND_ENTRY,&entry,8);assert(!psnd_check() && !strcmp(psnd_why,"sound_entry_lost"));psnd_why="-";
        void *ours=(void*)pres_sound_move;memcpy(region+PRES_SOUND_ENTRY,&ours,8);assert(psnd_check());
        pres_sound_uninstall();assert(!pres_sound_installed && *(void**)(region+PRES_SOUND_ENTRY)==(void*)tpsnd_move && !psnd_installed());
        char status[160];assert(pres_sound_status(status,sizeof(status))>0 && strstr(status," sound=0/0/4/1/3/0"));}
    pres_active=active_was;pres_slot=slot_was;pres_open=0;pres_sound_original=original_was;pres_listener_fn=listener_was;
    memcpy(region+PRES_SOUND_MANAGER,&manager_was,8);
    pres_sound_scopes=pres_sound_plain=pres_sound_listener=pres_sound_faults=0;
    printf("player sound present: X4's sound update in slot 3's and slot 2's scopes, the listener's room theirs for it, everything back; the entry's CAS and tick check; tests PASS\n");
}

/* ---- the real image ---- */
static uint32_t tpsnd_call_target(const unsigned char *at,uint32_t rva) {int32_t rel;memcpy(&rel,at+rva+1,4);return rva+5u+(uint32_t)rel;}
static void test_player_sound_native_image(const char *path) {
    unsigned char *prior=image;
    image=test_pmath_map(path);
    /* The sound update: XSfx::Base (its RTTI) slot 1, the voices' update in it, the listener routine's store. */
    assert(pres_sound_pins());
    {   unsigned char *col=NULL;memcpy(&col,image+PRES_SOUND_ENTRY-0x10,8);const uint32_t col_rva=(uint32_t)(col-image);
        uint32_t td=0;memcpy(&td,image+col_rva+12,4);assert(!strcmp((const char*)image+td+16,".?AVBase@XSfx@@"));}
    assert(image[0x14ea597]==0xe8 && tpsnd_call_target(image,0x14ea597)==0x14f6700u);                         /* Move -> the voices */
    assert(!memcmp(image+0x70bea0,"\x48\x89\xb0\x88\x00\x00\x00",7));                                          /* mov [manager+0x88],rsi */
    {int32_t rel;memcpy(&rel,image+0x70be99+3,4);assert(0x70bea0u+(uint32_t)rel==PRES_SOUND_MANAGER);}
    /* The placement's cluster test reads the camera's bank component, which the refill writes from the camera's anchor. */
    assert(!memcmp(image+0x3e8723,"\x48\x69\xc8\xb0\x02\x00\x00\x4a\x8b\x4c\x29\x30",12));                   /* imul rcx,rax,0x2b0; mov rcx,[rcx+r13+0x30] */
    assert(!memcmp(image+0x979c40,"\x49\x8b\xbd\x70\x07\x00\x00",7) && !memcmp(image+0x979c66,"\x48\x8b\x0e\x48\x89\x3e",6));   /* [camera+0x770] into the block */
    /* The ship's sounds have its +0x30 interface as their emitter, slot 2 the placement, slot 5 the curves (in the update). */
    assert(!memcmp(image+0x7c8c0b,"\x48\x8d\x56\x30",4));
    {void *slot2=NULL;memcpy(&slot2,image+0x2b86e90+0x10,8);assert(slot2==(void*)(image+0x75a190u));}
    assert(image[0x75a1e1]==0xe9 && tpsnd_call_target(image,0x75a1e1)==0x3e8470u);
    assert(!memcmp(image+0x14f1c51,"\x48\x8b\x01\x4c\x8b\x58\x28",7));                                          /* the update calls the emitter's slot 5 */
    /* The sound start: pinned, its first seven bytes in one block, the emitter in r9; a call's line hands its r12 there. */
    assert(pres_source_pins() && !(PRES_SOURCE&15u) && !memcmp(image+PRES_SOURCE+7,"\x4c\x89\x48\x20",4));
    assert(!memcmp(image+0x561781,"\x4d\x8b\xcc",3) && tpsnd_call_target(image,0x561799)==PRES_SOURCE);        /* mov r9,r12; call */
    assert(!memcmp(image+0x56160d,"\x48\x8b\x05",3) && !memcmp(image+0x561619,"\x48\x05\x30\x02\x00\x00",6)); /* the comm line: the universe's player */
    assert(!memcmp(image+0xd9c993,"\x48\x05\x30\x02\x00\x00",6) && !memcmp(image+0xd9c9b5,"\x4c\x8d\x48\x30",4));   /* the autopilot's UI sound: the same */
    /* The computer: pinned behind the UI replay's entries; the sites in one block each; the engaged post right after them. */
    assert(pvehk_computer_pins() && !memcmp(image+PVEHK_COMPUTER_ENGAGED,pvehk_computer_movs,6) && ((PVEHK_COMPUTER_ENGAGED+5u)&~15u)==(PVEHK_COMPUTER_ENGAGED&~15u));
    assert(image[PVEHK_COMPUTER_ENGAGED_RESUME]==0xe8 && tpsnd_call_target(image,PVEHK_COMPUTER_ENGAGED_RESUME)==PVEHK_EVENT_POST_NOW);
    assert(((PVEHK_COMPUTER_ENGAGED_RESUME+4u)&~15u)!=(PVEHK_COMPUTER_ENGAGED_RESUME&~15u));                  /* why the movs: the call crosses */
    assert(image[PVEHK_COMPUTER_DISENGAGED]==0xe8 && tpsnd_call_target(image,PVEHK_COMPUTER_DISENGAGED)==PVEHK_EVENT_POST_NOW &&
           ((PVEHK_COMPUTER_DISENGAGED+4u)&~15u)==(PVEHK_COMPUTER_DISENGAGED&~15u));
    {int32_t rel;memcpy(&rel,image+0x72c02e+3,4);assert(!strcmp((const char*)image+0x72c035u+(uint32_t)rel,"autopilot_engaged"));
     memcpy(&rel,image+0x72bc27+3,4);assert(!strcmp((const char*)image+0x72bc2eu+(uint32_t)rel,"autopilot_disengaged"));}
    assert(tpsnd_call_target(image,0x72bfc8)==SPL_SPEAKER && tpsnd_call_target(image,0x72bb6f)==SPL_SPEAKER);  /* the computer: 0x703ac0(player) */
    assert(PVEHK_COMPUTER_ENGAGED_PIN>=0x72bf20u+7u && PVEHK_COMPUTER_DISENGAGED_PIN>=0x72bb10u+5u);             /* behind the UI replay's entry patches */
    /* The animated shot: pinned to its site, the shot in r14 from the entry on, its target and cutscene where the camera shot has them. */
    assert(hash_ok(image+VCAM_ANIMATED_FN,VCAM_ANIMATED_SIZE,VCAM_ANIMATED_SHA) && VCAM_ANIMATED_FN+VCAM_ANIMATED_SIZE==VCAM_SITE_ANIMATED);
    assert(!memcmp(image+0x10e2361,"\x4c\x8b\xf1\x48\x83\xc1\x20\xe8",8) && tpsnd_call_target(image,0x10e2368)==0x1102ae0u);
    assert(!memcmp(image+0x10e2379,"\x4d\x8b\x7e\x50",4) && !memcmp(image+0x10e2394,"\x4d\x8b\x7f\x70",4));
    assert(!memcmp(image+0x10e2af8,"\x48\x8b\xce\xe8",4) && tpsnd_call_target(image,0x10e2afb)==VIEW_SET_ATTENTION && view_trigger_match(VCAM_SITE_ANIMATED)==8);
    image[VCAM_ANIMATED_FN+0x71]^=0x01;assert(!hash_ok(image+VCAM_ANIMATED_FN,VCAM_ANIMATED_SIZE,VCAM_ANIMATED_SHA));image[VCAM_ANIMATED_FN+0x71]^=0x01;
    image[PRES_SOUND_MOVE+3]^=0x01;assert(!pres_sound_pins());image[PRES_SOUND_MOVE+3]^=0x01;
    /* A sound's first play: the site in one block, the call it jumps to and past; the voice update plays a voice in
     * state 1..3 with its play bit, the loop of every voice calls it, the play calls it after alSourcePlay. */
    assert(pres_play_pins() && ((PRES_PLAY_SITE+PRES_PLAY_SIZE-1u)&~15u)==(PRES_PLAY_SITE&~15u) && PRES_PLAY_SITE+PRES_PLAY_SIZE==PRES_PLAY_CALL &&
           tpsnd_call_target(image,PRES_PLAY_CALL)==PRES_PLAY_FN && PRES_PLAY_CALL+5u==PRES_PLAY_AFTER);
    assert(!memcmp(image+PRES_PLAY_SITE-8,"\x41\xf6\x85\xe8\x00\x00\x00\x01",8) && !memcmp(image+PRES_PLAY_AFTER,"\x41\x8b\x45\x08",4));   /* the play bit; the answer */
    assert(!memcmp(image+0x14f166f,"\xf6\x83\xe8\x00\x00\x00\x01",7) && tpsnd_call_target(image,0x14f1695)==PRES_PLAY_FN);
    assert(tpsnd_call_target(image,0x14f67ac)==PRES_VOICE_UPDATE && tpsnd_call_target(image,0x14eff2a)==PRES_VOICE_UPDATE);
    assert(tpsnd_call_target(image,0xd7994a)==PRES_SOURCE);                                                   /* an effect's sound */
    image[PRES_PLAY_FN+9]^=0x01;assert(!pres_play_pins());image[PRES_PLAY_FN+9]^=0x01;
    /* The own ship's sound set: the camera handlers' slots, list entry and name; every ship class's slots. */
    assert(psctx_pins() && !memcmp(image+0x7cdde3,"\x48\x8b\x98\xb8\x23\x00\x00",7) && !memcmp(image+0x7cde93,"\x48\x8b\x98\xc0\x23\x00\x00",7));
    {int32_t rel;memcpy(&rel,image+0x7cddea+2,4);assert(0x7cddf0u+(uint32_t)rel==PSCTX_KIND);
     memcpy(&rel,image+0x7cde1f+3,4);assert(0x7cde26u+(uint32_t)rel==PSCTX_NAME);}
    for(unsigned i=0;i<SS_VTABLES;i++) {
        void *start=NULL,*stop=NULL,*entered=NULL,*left=NULL;
        memcpy(&start,image+ss_vtables[i]+PSCTX_START_SLOT,8);memcpy(&stop,image+ss_vtables[i]+PSCTX_STOP_SLOT,8);
        memcpy(&entered,image+ss_vtables[i]+366u*8u,8);memcpy(&left,image+ss_vtables[i]+367u*8u,8);
        assert(start==(void*)(image+SS_TARGET) && stop==(void*)(image+PSCTX_STOP_FN) && entered==(void*)(image+PSCTX_ENTERED) && left==(void*)(image+PSCTX_LEFT));
        memcpy(image+ss_vtables[i]+PSCTX_STOP_SLOT,&start,8);assert(!psctx_pins());memcpy(image+ss_vtables[i]+PSCTX_STOP_SLOT,&stop,8);   /* a stop slot is pinned */
    }
    /* A station is no ship: U::Station (its RTTI) has X4's empty function (ret 0) as its camera handlers, and 1143 slots
     * in, behind its vtable, lies 0x34fe40 (mov rcx,[rcx+0x30]; call 0x7ad890: the live fault of 26 Nov, stop 223). */
    {   const uint32_t station=0x2b764d8u;unsigned char *col=NULL;void *entered=NULL,*left=NULL,*behind=NULL;
        memcpy(&col,image+station-8,8);uint32_t td=0;memcpy(&td,col+12,4);assert(!strcmp((const char*)image+td+16,".?AVStation@U@@"));
        for(unsigned i=0;i<SS_VTABLES;i++) assert(ss_vtables[i]!=station);
        memcpy(&entered,image+station+366u*8u,8);memcpy(&left,image+station+367u*8u,8);memcpy(&behind,image+station+PSCTX_START_SLOT,8);
        assert(entered==(void*)(image+0x963b0u) && left==entered && !memcmp(image+0x963b0,"\xc2\x00\x00",3));
        assert(behind==(void*)(image+0x34fe40u) && !memcmp(image+0x34fe44,"\x48\x8b\x49\x30\xe8",5) && tpsnd_call_target(image,0x34fe48)==0x7ad890u);}
    image[PSCTX_LEFT+5]^=0x01;assert(!psctx_pins());image[PSCTX_LEFT+5]^=0x01;
    image=prior;
    printf("pinned native image player sound verified (the sound update and its listener, the placement's anchor, the sound start, the autopilot posts, the animated shot)\n");
}
