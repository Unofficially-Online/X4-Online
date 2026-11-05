/* The region object search's camera test from the player's ship (gate_mining_camera.h): the shared distance core
 * around 50 km; lxmc_near against stand-in transforms; the latch from a fake universe; the emitted thunk through a
 * register harness (near and far, r15+0x38 handed on); on the synthetic image the arming refusals, then with the
 * native bytes planted: armed, owned, the patched site run end to end, a changed thunk or block stops it, the
 * restore; on the real executable the pin, armed and restored. Included by test_gate_linux.c. */
#include "test_lockstep_mining_near.h"
typedef struct {uint64_t entry,rbp,r12,r15,rdi,which;} tmc_block_t;
static volatile uint64_t tmc_block __attribute__((used));
/* tmc_run(block): rbp, r12 and r15 as the search has them at its camera block, then jmp to block->entry; the near
 * and far continuations end at tmc_near / tmc_far, which keep rdi and which one ran. */
__asm__(".text\n.p2align 4\ntmc_run:\n"
" push %rbp\n push %rbx\n push %r12\n push %r13\n push %r14\n push %r15\n sub $8,%rsp\n mov %rdi,tmc_block(%rip)\n"
" mov 0x8(%rdi),%rbp\n mov 0x10(%rdi),%r12\n mov 0x18(%rdi),%r15\n jmp *(%rdi)\n"
"tmc_near:\n mov tmc_block(%rip),%rax\n mov %rdi,0x20(%rax)\n movq $1,0x28(%rax)\n jmp tmc_done\n"
"tmc_far:\n mov tmc_block(%rip),%rax\n mov %rdi,0x20(%rax)\n movq $2,0x28(%rax)\n"
"tmc_done:\n add $8,%rsp\n pop %r15\n pop %r14\n pop %r13\n pop %r12\n pop %rbx\n pop %rbp\n ret\n"
"tmc_near_addr:\n lea tmc_near(%rip),%rax\n ret\n"
"tmc_far_addr:\n lea tmc_far(%rip),%rax\n ret\n");
extern void tmc_run(tmc_block_t *block);
extern void *tmc_near_addr(void);
extern void *tmc_far_addr(void);
/* Fake components: a vtable whose IsClass (+0x11e8) answers the class at +0x10; +0x20 the position in the cluster. */
typedef struct {void *vtable;uint64_t id;int klass;int pad;float at[4];unsigned char rest[0x48];void *parent;} tmc_object_t;
_Static_assert(__builtin_offsetof(tmc_object_t,parent)==0x70,"+0x70 parent");
static char tmc_is_class(void *object,int klass) {return ((tmc_object_t*)object)->klass==klass;}
static void *tmc_vtable[0x11e8/8+1];
static void tmc_gtr(void *self,float *out,double time,void *ancestor,unsigned char flag) {
    (void)time;(void)flag;(void)ancestor;memcpy(out,((tmc_object_t*)self)->at,12); /* the cluster at the origin, no turn */
}
static void tmc_init(tmc_object_t *o,int klass,void *parent,float x,float y,float z) {
    memset(o,0,sizeof(*o));o->vtable=tmc_vtable;o->klass=klass;o->parent=parent;o->at[0]=x;o->at[1]=y;o->at[2]=z;
}
static void test_mining_camera_linux(void) {
    test_mining_near_core();
    tmc_vtable[0x11e8/8]=(void*)tmc_is_class;
    static tmc_object_t cluster,other,sector,frame,ship,player,stray;
    tmc_init(&cluster,0xf,NULL,0,0,0);tmc_init(&other,0xf,NULL,0,0,0);tmc_init(&sector,0x58,&cluster,0,0,0);
    tmc_init(&frame,0x5f,&sector,1000,0,0);tmc_init(&stray,0x5f,&other,0,0,0);
    tmc_init(&ship,0x49,&sector,1000+49999.0f,0,0);tmc_init(&player,0x2a,&ship,1000+49999.0f,0,0);
    lxmc_gtr_override=tmc_gtr;
    /* The latch: the player object's class-0x49 ancestor and its cluster, from g_Universe +0x238 (the synthetic image). */
    static unsigned char universe[0x400];void *p=&player;memcpy(universe+0x238,&p,8);p=universe;
    unsigned char saved_universe[8];memcpy(saved_universe,LX(LX_UNIVERSE_VA),8);memcpy(LX(LX_UNIVERSE_VA),&p,8);
    lxmc_point=lxmc_cluster=NULL;lxmc_calls=lxmc_near_count=lxmc_foreign=0;
    lxmc_latch();assert(lxmc_point==&ship && lxmc_cluster==&cluster);
    /* Near: the ship 49999 m from the frame's origin; far at 50001; a frame outside the cluster is foreign. */
    const float origin[3]={0,0,0};
    assert(lxmc_near(&cluster,origin,&frame)==1 && lxmc_near_count==1);
    ship.at[0]=1000+50001.0f;assert(lxmc_near(&cluster,origin,&frame)==0);
    ship.at[0]=1000+49999.0f;assert(lxmc_near(&cluster,origin,&stray)==0 && lxmc_foreign==1);
    assert(lxmc_near(&other,origin,&stray)==0 && lxmc_foreign==1); /* the point is not in that cluster: not near, not foreign */
    assert(!lxmc_near(NULL,origin,&frame) && !lxmc_near(&cluster,NULL,&frame) && lxmc_calls==6);
    /* Without a ship ancestor the player object itself. */
    player.parent=&sector;lxmc_latch();assert(lxmc_point==&player && lxmc_cluster==&cluster);player.parent=&ship;lxmc_latch();
    /* The emitted thunk through the harness: [rbp-0x178] the cluster, [rbp-0x168] the position, r12 the frame. */
    {unsigned char *page=gate_near_page((const void*)tmc_run);assert(page);
     lxmc_stub_bytes(page,(void*)lxmc_near,tmc_near_addr(),tmc_far_addr());assert(!mprotect(page,GATE_PAGE,PROT_READ|PROT_EXEC));
     static unsigned char stack[0x200] __attribute__((aligned(16)));unsigned char *rbp=stack+0x200;
     void *c=&cluster;const float *pos=origin;memcpy(rbp-0x178,&c,8);memcpy(rbp-0x168,&pos,8);
     tmc_block_t blk={(uint64_t)(uintptr_t)page,(uint64_t)(uintptr_t)rbp,(uint64_t)(uintptr_t)&frame,0x1000,0,0};
     tmc_run(&blk);assert(blk.which==1 && blk.rdi==0x1038);
     ship.at[0]=1000+50001.0f;blk.which=0;tmc_run(&blk);assert(blk.which==2 && blk.rdi==0x1038);ship.at[0]=1000+49999.0f;
     munmap(page,GATE_PAGE);}
    /* Arming on the synthetic image: timing, then the site (zero there), then with the native bytes planted. */
    reset_session();DWORD self=GetCurrentThreadId();
    stats.active=1;boot_mode=1;boot_phase=0;ui_thread=self;stats.issued=1;lxmc_installed=lxmc_restored=0;lxmc_failures=0;lxmc_stage="idle";
    assert(!lxmc_arm());stats.issued=0;ui_thread=self+1;assert(!lxmc_arm());ui_thread=self;
    assert(lxmc_state()<0 && !lxmc_arm() && !strcmp(lxmc_stage,"entry"));
    unsigned i=0;while(i<gate_site_count && strcmp(gate_sites[i].name,"mining_search")) ++i;
    assert(i<gate_site_count && gate_sites==synth_sites && lx_sites[i].va==LX_MC_FUNC_VA);
    unsigned char *pg=(unsigned char*)((uintptr_t)LX(LX_MC_SITE_VA)&~(uintptr_t)(GATE_PAGE-1));
    static unsigned char saved[0x6d0];memcpy(saved,LX(LX_MC_FUNC_VA),sizeof(saved));
    memcpy(LX(LX_MC_SITE_VA),lxmc_original,5);
    assert(lxmc_state()==0 && !lxmc_arm() && !strcmp(lxmc_stage,"pins")); /* the synthetic pin is the zeros' */
    /* near and far continue at harness labels (far is on the next page, made executable): the site runs end to end */
    unsigned char *far_pg=(unsigned char*)((uintptr_t)LX(LX_MC_FAR_VA)&~(uintptr_t)(GATE_PAGE-1));
    assert(!mprotect(far_pg,GATE_PAGE,PROT_READ|PROT_WRITE|PROT_EXEC));
    twx_plant_jmp(LX(LX_MC_NEAR_VA),tmc_near_addr());twx_plant_jmp(LX(LX_MC_FAR_VA),tmc_far_addr());
    char hex[65];assert(digest_hex(LX(lx_sites[i].va),lx_sites[i].length,hex));memcpy(synth_hashes[i],hex,65);
    {unsigned g=0;while(g<gate_site_count && strcmp(gate_sites[g].name,"warp_gtrt")) ++g; /* earlier tests leave that region changed */
     assert(g<gate_site_count && digest_hex(LX(lx_sites[g].va),lx_sites[g].length,hex));memcpy(synth_hashes[g],hex,65);}
    unsigned char line[16];memcpy(line,LX(LXMC_LINE),16);
    assert(lxmc_pins());assert(lxmc_arm() || (printf("arm stage=%s\n",lxmc_stage),0));
    assert(lxmc_installed && lxmc_state()==1 && lxmc_owned() && lxmc_check() && lxmc_arm());
    assert(!lxmc_point);lxmc_latch();assert(lxmc_point==&ship); /* arming clears the latch; the frame latches every tick */
    {unsigned char e[16];memcpy(e,LX(LXMC_LINE),16);int32_t d;memcpy(&d,e+1,4);
     assert(e[0]==0xe9 && !memcmp(e+5,line+5,11) && LX(LX_MC_SITE_VA)+5+d==lxmc_page);}
    {static unsigned char stack[0x200] __attribute__((aligned(16)));unsigned char *rbp=stack+0x200;
     void *c=&cluster;const float *pos=origin;memcpy(rbp-0x178,&c,8);memcpy(rbp-0x168,&pos,8);
     tmc_block_t blk={(uint64_t)(uintptr_t)LX(LX_MC_SITE_VA),(uint64_t)(uintptr_t)rbp,(uint64_t)(uintptr_t)&frame,0x2000,0,0};
     tmc_run(&blk);assert(blk.which==1 && blk.rdi==0x2038);
     ship.at[0]=1000+50001.0f;tmc_run(&blk);assert(blk.which==2);ship.at[0]=1000+49999.0f;}
    char status[384];
    assert(lxmc_status(status,sizeof(status))>0 && strstr(status," installed=1 restored=0 groups=mining ") && strstr(status," state=1 pin=1 stage=installed"));
    /* A changed thunk or block stops it (stop reason 166 in the tick). */
    assert(!mprotect(lxmc_page,GATE_PAGE,PROT_READ|PROT_WRITE));lxmc_page[3]^=1;assert(!lxmc_check() && !strcmp(lxmc_stage,"entry_changed"));
    lxmc_page[3]^=1;assert(!mprotect(lxmc_page,GATE_PAGE,PROT_READ|PROT_EXEC));
    assert(!mprotect(pg,GATE_PAGE,PROT_READ|PROT_WRITE|PROT_EXEC));*LX(LX_MC_FUNC_VA+0x40)^=1;assert(!lxmc_check() && !strcmp(lxmc_stage,"block_changed"));
    *LX(LX_MC_FUNC_VA+0x40)^=1;lxmc_failures=0;lxmc_stage="installed";assert(lxmc_check());
    lxmc_restore();assert(!lxmc_installed && lxmc_restored && lxmc_state()==0 && !memcmp(line,LX(LXMC_LINE),16));

    assert(!mprotect(pg,GATE_PAGE,PROT_READ|PROT_WRITE) && !mprotect(far_pg,GATE_PAGE,PROT_READ|PROT_WRITE));
    memcpy(LX(LX_MC_FUNC_VA),saved,sizeof(saved));
    assert(digest_hex(LX(lx_sites[i].va),lx_sites[i].length,hex));memcpy(synth_hashes[i],hex,65);
    memcpy(LX(LX_UNIVERSE_VA),saved_universe,8);
    munmap(lxmc_page,GATE_PAGE);lxmc_page=NULL;lxmc_gtr_override=NULL;lxmc_restored=0;lxmc_failures=0;lxmc_stage="idle";
    lxmc_point=lxmc_cluster=NULL;stats.active=0;
    printf("mining camera latch/near/thunk/arming/check/restore PASS\n");
}
/* The real executable: the pin and the site as found, armed (the line changes in its first 5 bytes only), restored. */
static void test_mining_camera_elf(void) {
    assert(lxmc_pins() && lxmc_state()==0);
    {int32_t d;memcpy(&d,LX(0x1f47b9e)+2,4);assert(*LX(0x1f47b9e)==0x0f && *(LX(0x1f47b9e)+1)==0x84 && LX(0x1f47b9e)+6+d==LX(LX_MC_SITE_VA));}
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();lxmc_installed=lxmc_restored=0;
    unsigned char line[16];memcpy(line,LX(LXMC_LINE),16);
    assert(lxmc_arm() && lxmc_installed && lxmc_state()==1 && lxmc_owned() && lxmc_check() && !memcmp(LX(LXMC_LINE)+5,line+5,11));
    lxmc_restore();assert(!lxmc_installed && lxmc_restored && lxmc_state()==0 && !memcmp(line,LX(LXMC_LINE),16) && lxmc_pins());
    munmap(lxmc_page,GATE_PAGE);lxmc_page=NULL;lxmc_restored=0;stats.active=0;
    printf("mining camera ELF PASS\n");
}
