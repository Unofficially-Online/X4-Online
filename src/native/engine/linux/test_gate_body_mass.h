/* The second body's mass properties (gate_body_mass.h): a dynamic body over X4's defined-mass shape and a mutable compound
 * gets the empty compound's fallback (2.5 * inverse mass on four lanes, the identity rotation) whatever the compound
 * held; one that already has it is counted as such; a kinematic body, another shape class or another inner shape stay
 * as they are; the allocation wrapper applies it to what AllocateBody returns; on the real executable the pins, the
 * shape's GetMassProperties in its vtable slot, the entry installed and restored. Included by test_gate_linux.c. */
static unsigned char tbm_body[0x80] __attribute__((aligned(16))),tbm_motion[0x80] __attribute__((aligned(16)));
static unsigned char tbm_shape[0x30],tbm_inner[0x60],tbm_settings[0x100],tbm_record[0x40];
static void *tbm_alloc_native(void *manager,const void *settings) {(void)manager;(void)settings;return tbm_body;}
static void tbm_plant(unsigned char kind,int defined,unsigned char inner_type,unsigned char inner_sub,int already) {
    memset(tbm_body,0,sizeof(tbm_body));memset(tbm_motion,0,sizeof(tbm_motion));memset(tbm_shape,0,sizeof(tbm_shape));
    memset(tbm_inner,0,sizeof(tbm_inner));memset(tbm_settings,0,sizeof(tbm_settings));memset(tbm_record,0,sizeof(tbm_record));
    tbm_record[0x3a]=1;{void *r=tbm_record;memcpy(tbm_settings+0x40,&r,8);memcpy(tbm_body+0x50,&r,8);} /* the second body's record */
    void *p=tbm_shape;memcpy(tbm_body+0x40,&p,8);p=tbm_motion;memcpy(tbm_body+0x48,&p,8);
    p=defined?LX(LXBM_DEFINED_MASS_VTABLE):LX(LXBM_DEFINED_MASS_VTABLE+8u);memcpy(tbm_shape,&p,8);
    p=tbm_inner;memcpy(tbm_shape+0x20,&p,8);tbm_inner[0x18]=inner_type;tbm_inner[0x19]=inner_sub;tbm_settings[0x60]=kind;
    const float im=1.0f/6.0f;memcpy(tbm_motion+0x58,&im,4);
    /* the hull's tensor as Linux had it, or already the fallback */
    const uint32_t hull[8]={0x39704e20u,0x39a5fc21u,0x3a2cadd6u,0x7f800000u,0x3f06b05fu,0x3ef1dde2u,0x3f06b1f0u,0x3ef1e2e4u};
    if(already) {const float f=2.5f*im,w[8]={f,f,f,f,0,0,0,1.0f};memcpy(tbm_motion+0x20,w,32);} else memcpy(tbm_motion+0x20,hull,32);
}
static void test_body_mass_linux(void) {
    lxbm_seen=lxbm_already=lxbm_changed=lxbm_failures=0;
    const float im=1.0f/6.0f,f=2.5f*im;uint32_t fb;memcpy(&fb,&f,4);assert(fb==0x3ed55556u); /* Windows' body1 words */
    tbm_plant(2,1,1,8,0);lxbm_apply(tbm_body,tbm_settings);
    {uint32_t w[8];memcpy(w,tbm_motion+0x20,32);
     assert(w[0]==fb && w[1]==fb && w[2]==fb && w[3]==fb && !w[4] && !w[5] && !w[6] && w[7]==0x3f800000u && lxbm_seen==1 && lxbm_changed==1);}
    tbm_plant(2,1,1,8,1);lxbm_apply(tbm_body,tbm_settings);assert(lxbm_seen==2 && lxbm_already==1 && lxbm_changed==1);
    uint32_t before[8];
    tbm_plant(1,1,1,8,0);memcpy(before,tbm_motion+0x20,32);lxbm_apply(tbm_body,tbm_settings); /* kinematic */
    assert(!memcmp(before,tbm_motion+0x20,32) && lxbm_seen==2);
    tbm_plant(2,0,1,8,0);lxbm_apply(tbm_body,tbm_settings);assert(!memcmp(before,tbm_motion+0x20,32) && lxbm_seen==2); /* another class */
    tbm_plant(2,1,0,6,0);lxbm_apply(tbm_body,tbm_settings);assert(!memcmp(before,tbm_motion+0x20,32) && lxbm_seen==2); /* a convex hull inside */
    tbm_plant(2,1,1,8,0);tbm_record[0x3a]=0;lxbm_apply(tbm_body,tbm_settings);                /* the first body: Jolt updates its mass */
    assert(!memcmp(before,tbm_motion+0x20,32) && lxbm_seen==2 && lxbm_updated==1 && !lxbm_failures);
    tbm_plant(2,1,1,8,0);memset(tbm_settings+0x40,0,8);lxbm_apply(tbm_body,tbm_settings);    /* no record: a failure, fail closed */
    assert(!memcmp(before,tbm_motion+0x20,32) && lxbm_seen==2 && lxbm_failures==1);lxbm_failures=0;
    /* The wrapper: AllocateBody's result gets the rule. */
    lxbm_original_override=tbm_alloc_native;tbm_plant(2,1,1,8,0);
    assert(lxbm_allocate(NULL,tbm_settings)==tbm_body && lxbm_changed==2 && lxbm_is_fallback(tbm_motion));
    lxbm_original_override=NULL;
    char status[256];assert(lxbm_status(status,sizeof(status))>0 && strstr(status," seen=3 already=1 changed=2 updated=1 failures=0 "));
    lxbm_seen=lxbm_already=lxbm_changed=lxbm_updated=lxbm_failures=0;
    /* The shared census (lockstep_body_mass.h) with this gate's reader: the planted body (dynamic, now the fallback),
     * a kinematic one, a free slot; then a -0 in the rotation is not the identity's words. */
    {static unsigned char kin[0x80] __attribute__((aligned(16))),manager[0x18];void *array[3];
     tbm_plant(2,1,1,8,0);lxbm_apply(tbm_body,tbm_settings);tbm_body[0x77]=1;tbm_body[0x78]=2;tbm_shape[0x18]=0x02;tbm_shape[0x19]=0x11;
     memcpy(kin,tbm_body,sizeof(kin));kin[0x77]=2;kin[0x78]=1;        /* +0x78 is the motion type, +0x77 the broadphase layer */
     array[0]=tbm_body;array[1]=kin;array[2]=(void*)(uintptr_t)0x0000000000000203ULL;
     const uint64_t count=3;memcpy(manager,&count,8);void *p=array;memcpy(manager+0x10,&p,8);
     body_mass_census_t census;char text[256];
     assert(body_mass_census(gate_read_local,manager,&census) && census.bodies==2 && census.dynamic==1 && census.seen==1 && census.fallback==1 && !census.differ);
     assert(body_mass_census_text(&census,text,sizeof(text))>0 && !strcmp(text,"bodies=2 dynamic=1 seen=1 fallback=1 differ=0 updated=0 unreadable=0"));
     const uint32_t negative_zero=0x80000000u;memcpy(tbm_motion+0x30,&negative_zero,4);
     assert(body_mass_census(gate_read_local,manager,&census) && census.fallback==0 && census.differ==1 && !lxbm_is_fallback(tbm_motion));
     tbm_record[0x3a]=0;assert(body_mass_census(gate_read_local,manager,&census) && census.seen==0 && census.updated==1);
     lxbm_seen=lxbm_already=lxbm_changed=lxbm_updated=lxbm_failures=0;}
    printf("body1 mass fallback rule/wrapper PASS\n");
}
/* The real executable: the pins, XDefinedMassShape's GetMassProperties in its vtable slot 8, the entry installed (jmp
 * [rip] -> the wrapper; bytes 14..15 kept; the trampoline the 14 bytes and a jmp back) and restored. */
static void test_body_mass_elf(void) {
    uint64_t slot;memcpy(&slot,LX(LXBM_DEFINED_MASS_VTABLE+8u*8u),8);assert(slot==0xed4cf0u);
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();lxbm_installed=lxbm_restored=0;
    unsigned char before[16];memcpy(before,LX(LXBM_ALLOCATE_VA),16);
    assert(lxbm_pins() && lxbm_state()==0 && lxbm_arm() && lxbm_installed && lxbm_state()==1 && lxbm_check() && lxbm_pins());
    assert(LX(LXBM_ALLOCATE_VA)[0]==0xff && LX(LXBM_ALLOCATE_VA)[1]==0x25 && !memcmp(LX(LXBM_ALLOCATE_VA)+14,before+14,2) &&
           !memcmp(lxbm_page,before,14) && lxbm_page[14]==0xff && lxbm_page[15]==0x25);
    lxbm_restore();assert(!lxbm_installed && lxbm_restored && !memcmp(LX(LXBM_ALLOCATE_VA),before,16) && lxbm_state()==0 && lxbm_pins());
    munmap(lxbm_page,GATE_PAGE);lxbm_page=NULL;lxbm_restored=0;stats.active=0;
    printf("body1 mass ELF PASS\n");
}
