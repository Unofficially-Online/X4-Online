/* Contact orientation (lockstep_contact_orient.h). Synthetic: the portable order of fake bodies (keyed before unkeyed, owner
 * and record, unkeyed by BodyID), other BodyIDs giving the same answers but another agree digest, observe keeping the
 * BodyID order; the generated stubs run on two fake places (a branch and a cmov one) in an executable page. Native image:
 * the five places' pins, their stubs fit, the arm patches and the restore puts the bytes back. */
static _Alignas(16) unsigned char tco_bodies[6][0x80],tco_records[6][0x20],tco_owners[5][0x280];
static uint64_t tco_owner_key(uint64_t runtime) {return runtime==0x61u?0x2000061ull:runtime==0x62u?0x1000062ull:runtime==0x64u?0x8000000000000064ull:0;}
static void tco_body_id(unsigned b,uint32_t id) {memcpy(tco_bodies[b]+0x70,&id,4);}
static void tco_setup(void) {
    memset(tco_bodies,0,sizeof(tco_bodies));memset(tco_records,0,sizeof(tco_records));memset(tco_owners,0,sizeof(tco_owners));
    /* owners: 0x61 (a ship, records 0 and 1 = bodies 0 and 1), 0x62 (a station, body 2), 0x63 without a portable ID (body 3),
     * 0x64 (body 4), 0x65 without a portable ID (body 5, the lowest BodyID) */
    for(unsigned o=0;o<5;o++) {uint64_t rt=0x61u+o;memcpy(tco_owners[o]+8,&rt,8);}
    const unsigned owner_of[6]={0,0,1,2,3,4},slot_of[6]={0,1,0,0,0,0};
    for(unsigned b=0;b<6;b++) {
        unsigned char *rec=tco_records[b],*own=tco_owners[owner_of[b]];
        memcpy(rec,&own,8);memcpy(tco_bodies[b]+0x50,&rec,8);memcpy(own+0x260+8u*slot_of[b],&rec,8);tco_body_id(b,0x00800010u+b);
    }
    tco_body_id(5,0x00800001u);
}
static void tco_reset_counts(void) {
    for(unsigned i=0;i<CORIENT_SITES;i++) corient_calls[i]=corient_flips[i]=0;
    corient_unkeyed=corient_bodyid=corient_agree=corient_failures=0;
}
typedef void *(*tco_fake_fn)(void *,void *);
static void test_contact_orient(void) {
    uint64_t (*owner_key)(uint64_t)=iorder_owner_key;iorder_owner_key=tco_owner_key;
    const int saved_mode=corient_mode;const uint64_t saved_issued=stats.issued;
    tco_setup();tco_reset_counts();corient_mode=CORIENT_ON;stats.issued=500;
    unsigned char *b0=tco_bodies[0],*b1=tco_bodies[1],*b2=tco_bodies[2],*b3=tco_bodies[3],*b4=tco_bodies[4],*b5=tco_bodies[5];
    /* keyed by owner key: station 0x1000062 before ship 0x2000061 although the ship's BodyIDs are lower */
    assert(!corient_cf(b0,b2,0) && corient_cf(b2,b0,0) && corient_flips[0]==2 && corient_calls[0]==2);
    /* one owner's two records: record 0 first */
    assert(corient_cf(b0,b1,1) && !corient_cf(b1,b0,1) && corient_flips[1]==0);
    /* a portable owner before one with only a runtime ID (body 3 has the lower BodyID than body 4 = keyed 0x...64) */
    assert(corient_cf(b4,b3,2) && !corient_cf(b3,b4,2) && corient_unkeyed==2 && corient_flips[2]==2);
    assert(corient_last_owner==0x63 && !corient_bodyid);
    /* two owners with runtime IDs only: by runtime ID (0x63 before 0x65), not by BodyID (body 5's is the lowest) */
    assert(corient_cf(b3,b5,2) && !corient_cf(b5,b3,2) && corient_flips[2]==4 && corient_unkeyed==4);
    /* an ownerless body after any owner's; two ownerless bodies by their BodyIDs */
    {static _Alignas(16) unsigned char lone[2][0x80];memset(lone,0,sizeof(lone));const uint32_t id[2]={0x00800005u,0x00800007u};
     memcpy(lone[0]+0x70,&id[0],4);memcpy(lone[1]+0x70,&id[1],4);
     assert(!corient_cf(lone[0],b3,3) && corient_cf(b3,lone[0],3) && corient_flips[3]==2 && corient_bodyid==2);
     assert(corient_cf(lone[0],lone[1],3) && !corient_cf(lone[1],lone[0],3) && corient_flips[3]==2 && corient_bodyid==4);}
    /* a joiner's BodyIDs (the ship's above the station's now, the records swapped): the same answers; the agree digest of
     * the cache place, equal for equal IDs, differs once the native order of a pair differs */
    tco_reset_counts();assert(!corient_cf(b0,b2,1) && corient_cf(b0,b1,1));const LONG64 session=corient_agree;
    tco_reset_counts();assert(!corient_cf(b0,b2,1) && corient_cf(b0,b1,1) && corient_agree==session);
    tco_body_id(0,0x00800040u);tco_body_id(1,0x00800030u);tco_body_id(2,0x00800020u);
    tco_reset_counts();assert(!corient_cf(b0,b2,1) && corient_cf(b0,b1,1) && corient_flips[1]==1 && corient_agree!=session);
    /* the pair the other way round at the call: the same digest (the pair is hashed in the portable order) */
    {const LONG64 joiner=corient_agree;tco_reset_counts();assert(corient_cf(b2,b0,1) && !corient_cf(b1,b0,1) && corient_agree==joiner);}
    /* observe: the native BodyID order answered, the same counts */
    corient_mode=CORIENT_OBSERVE;tco_reset_counts();
    assert(!corient_cf(b0,b2,0) && corient_cf(b2,b0,0) && corient_flips[0]==0);
    assert(!corient_cf(b0,b1,0) && corient_flips[0]==1);
    /* reverse: the portable order turned round */
    corient_mode=CORIENT_REVERSE;assert(corient_cf(b0,b2,0) && !corient_cf(b2,b0,0) && !corient_cf(b0,b1,0) && corient_cf(b1,b0,0));
    corient_mode=CORIENT_OBSERVE;
    /* an unreadable body: a failure, the native answer */
    assert(!corient_cf(NULL,b0,0) && corient_failures==1);
    /* The stubs on fake places. A branch place (rcx x, rdx y): jae to `taken` keeps (returns y), falling through to `resume`
     * swaps (returns x). A cmov place (r8 x, r9 y; post: mov r10,r9; mov r11,r8; cmovae r11,r9; cmovae r10,r8) returns r11. */
    {unsigned char *page=VirtualAlloc(NULL,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);assert(page);memset(page,0xcc,0x1000);
     static const unsigned char branch[]={0x48,0x83,0xec,0x28, 0xe9,0,0,0,0, 0x48,0x8b,0xc1,0x48,0x83,0xc4,0x28,0xc3, 0x48,0x8b,0xc2,0x48,0x83,0xc4,0x28,0xc3};
     static const unsigned char cmov[]={0x48,0x83,0xec,0x28, 0x4c,0x8b,0xc1, 0x4c,0x8b,0xca, 0xe9,0,0,0,0, 0x49,0x8b,0xc3,0x48,0x83,0xc4,0x28,0xc3};
     memcpy(page,branch,sizeof(branch));memcpy(page+0x40,cmov,sizeof(cmov));
     const corient_site fake[2]={
         {"branch",0x04u,0x09u,0x11u,CR_RCX,CR_RDX,0,0,{0},{0},{0},0,0,NULL},
         {"cmov",0x4au,0x4fu,0,CR_R8,CR_R9,0,14,{0},{0x4d,0x8b,0xd1,0x4d,0x8b,0xd8,0x4d,0x0f,0x43,0xd9,0x4d,0x0f,0x43,0xd0},{0},0,0,NULL}};
     for(unsigned i=0;i<2;i++) {
         unsigned char *stub=page+0x400+CORIENT_STUB*i;assert(corient_write_stub(stub,stub,&fake[i],0,page,(void *)corient_cf));
         /* written elsewhere to run there: the same bytes (the tick check compares a fresh copy) */
         {unsigned char copy[CORIENT_STUB];assert(corient_write_stub(copy,stub,&fake[i],0,page,(void *)corient_cf) && !memcmp(copy,stub,CORIENT_STUB));}
         const int32_t d=(int32_t)((intptr_t)stub-(intptr_t)(page+fake[i].rva+5));memcpy(page+fake[i].rva+1,&d,4);
     }
     FlushInstructionCache(GetCurrentProcess(),page,0x1000);
     tco_fake_fn branch_fn=(tco_fake_fn)(void *)page,cmov_fn=(tco_fake_fn)(void *)(page+0x40);
     for(int mode=CORIENT_OBSERVE;mode<=CORIENT_ON;mode++) {
         corient_mode=mode;tco_reset_counts();
         /* bodies 0 (0x00800040, ship record 0) and 2 (0x00800020, station): native says 2 first, portable says 2 first */
         assert(branch_fn(b2,b0)==b2 && branch_fn(b0,b2)==b2 && cmov_fn(b2,b0)==b2 && cmov_fn(b0,b2)==b2);
         /* bodies 0 and 1 (0x00800030, ship record 1): native says 1 first, portable says 0 first */
         void *want=mode==CORIENT_ON?b0:b1;
         assert(branch_fn(b0,b1)==want && branch_fn(b1,b0)==want && cmov_fn(b0,b1)==want && cmov_fn(b1,b0)==want);
         assert(corient_calls[0]==8 && corient_flips[0]==4 && !corient_failures);
     }
     VirtualFree(page,0,MEM_RELEASE);}
    {char status[512];assert(corient_status(status,sizeof(status)) && strstr(status,"installed=0 mode=on calls=8/0/0/0/0 flips=4/0/0/0/0 unkeyed=0 bodyid=0 last="));}
    iorder_owner_key=owner_key;corient_mode=saved_mode;stats.issued=saved_issued;tco_reset_counts();
    puts("contact orient: pairs in the owners' portable order (keyed first, records, unkeyed by BodyID), the same for other BodyIDs, agree digest, observe, the stubs on fake places PASS");
}
static void test_contact_orient_native_image(const char *path) {
    unsigned char *prior=image,*prior_page=corient_page;
    const int prior_active=stats.active,prior_boot=boot_mode,prior_phase=boot_phase,prior_world=world_identity_installed;
    const uint64_t prior_issued=stats.issued;const DWORD prior_ui=ui_thread;
    image=test_pmath_map(path);corient_page=NULL;
    for(unsigned i=0;i<CORIENT_SITES;i++) assert(corient_state_of(i)==0);
    assert(corient_pins());
    /* each place's compare: the BodyIDs at +0x70 (or the pair's IDs at ProcessBodyPair) right where the jump goes */
    assert(!memcmp(image+0x13d5ff2,"\x45\x3b\xc2\x73\x09",5) && !memcmp(image+0x13e1e41,"\x39\x41\x70\x73\x2c",5));
    /* armed: five jumps into the page, owned, the pins still good; a changed byte in a block stops it; restored */
    stats.active=1;boot_mode=0;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();world_identity_installed=1;
    assert(corient_arm(CORIENT_ON) && corient_installed && corient_mode==CORIENT_ON && corient_arm(CORIENT_ON) && !corient_arm(CORIENT_OBSERVE));
    for(unsigned i=0;i<CORIENT_SITES;i++) assert(corient_state_of(i)==1 && image[corient_sites[i].rva]==0xe9);
    assert(corient_owned() && corient_pins() && corient_check());
    image[corient_sites[3].pin+2]^=1;assert(!corient_check() && !strcmp(corient_stage,"block_changed"));image[corient_sites[3].pin+2]^=1;
    corient_failures=0;assert(corient_check());
    corient_restore();
    assert(!corient_installed && corient_restored);
    for(unsigned i=0;i<CORIENT_SITES;i++) assert(corient_state_of(i)==0);
    assert(corient_pins());
    VirtualFree(corient_page,0,MEM_RELEASE);VirtualFree(image,0,MEM_RELEASE);
    corient_page=prior_page;image=prior;corient_mode=CORIENT_OFF;corient_restored=0;corient_done=0;corient_stage="idle";
    stats.active=prior_active;boot_mode=prior_boot;boot_phase=prior_phase;stats.issued=prior_issued;ui_thread=prior_ui;world_identity_installed=prior_world;
    puts("contact orient native image: five pinned places, stubs near the image, the jumps in and the bytes back PASS");
}
