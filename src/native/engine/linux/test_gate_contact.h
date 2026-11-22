/* The contact diagnostic (gate_contact.h): a planted fixture body against another body through both wrappers (the
 * manifold, the settings before and after the listener, the actor and its record; a pair without a fixture body
 * passes through unrecorded; the row in Windows' text); on the real executable the pin and both listener slots
 * swapped and restored. Included by test_gate_linux.c. */
static unsigned char tct_object[0x300],tct_record[0x80],tct_holder[0x40],tct_body[2][0x80],tct_motion[2][0x80],
                     tct_manifold[0x850],tct_settings[0x40],tct_record_other[0x80] __attribute__((aligned(16)));
static int tct_native_calls;
static void tct_native(void *self,const void *a,const void *b,const void *manifold,void *settings) {
    (void)self;(void)a;(void)b;(void)manifold;tct_native_calls++;
    const uint32_t friction=0x3f000000u;memcpy(settings,&friction,4); /* the listener changes the combined friction */
}
static void test_contact_linux(void) {
    static lxb_actor saved_actors[LXB_ACTORS];static unsigned saved_slots[LXB_ACTORS];int saved_bound=lxb_bound;
    memcpy(saved_actors,lxb_actors,sizeof(saved_actors));memcpy(saved_slots,lxb_body_slot,sizeof(saved_slots));
    memset(lxb_actors,0,sizeof(lxb_actors));memset(lxb_body_slot,0,sizeof(lxb_body_slot));
    reset_session();stats.active=1;stats.issued=133;boot_phase=3;ui_thread=GetCurrentThreadId();
    lxct_claimed=0;lxct_count=0;lxct_calls=lxct_ordinal=lxct_other_thread=0;lxct_failures=0;
    lxb_bound=LXB_ACTORS;lxb_actors[3].object=tct_object;lxb_body_slot[3]=LXB_BODY_SLOT_LOW;
    void *p=tct_record;memcpy(tct_object+LXB_BODY_SLOT_LOW+8u,&p,8);          /* actor 4's second record */
    p=tct_object;memcpy(tct_record,&p,8);p=tct_holder;memcpy(tct_record+0x20,&p,8);
    const uint64_t due=0x40283f94fbfd44f3ull;memcpy(tct_holder+0x28,&due,8);
    p=tct_record;memcpy(tct_body[0]+0x50,&p,8);p=tct_record_other;memcpy(tct_body[1]+0x50,&p,8);
    for(unsigned w=0;w<2;w++) {
        p=tct_motion[w];memcpy(tct_body[w]+0x48,&p,8);
        const uint32_t id=0x0080000bu+w;memcpy(tct_body[w]+0x70,&id,4);
        for(unsigned i=0;i<8;i++) {const uint32_t v=0x43000000u+w*0x100u+i;memcpy(tct_body[w]+i*4u,&v,4);memcpy(tct_motion[w]+i*4u,&v,4);}
    }
    memset(tct_manifold,0,sizeof(tct_manifold));
    for(unsigned i=0;i<12;i++) {const uint32_t v=0x3f800000u+i;memcpy(tct_manifold+i*4u,&v,4);}
    const uint32_t count=2;memcpy(tct_manifold+0x30,&count,4);memcpy(tct_manifold+0x440,&count,4);
    for(unsigned i=0;i<8;i++) {const uint32_t a=0x41000000u+i,b=0x42000000u+i;memcpy(tct_manifold+0x40+i*4u,&a,4);memcpy(tct_manifold+0x450+i*4u,&b,4);}
    for(unsigned i=0;i<16;i++) {const uint32_t v=0x3e000000u+i;memcpy(tct_settings+i*4u,&v,4);}
    lxct_added_override=lxct_persisted_override=tct_native;
    lxct_added(NULL,tct_body[0],tct_body[1],tct_manifold,tct_settings);
    lxct_persisted(NULL,tct_body[1],tct_body[1],tct_manifold,tct_settings);         /* no fixture body: passed through */
    assert(tct_native_calls==2 && lxct_calls==1 && lxct_claimed==1 && !lxct_failures && !lxct_other_thread);
    const lxct_event *e=&lxct_events[0];
    assert(e->tick==133 && e->ordinal==1 && e->added==1 && e->count==2 && e->bodies[0].actor==4 && e->bodies[0].record_kind==1 &&
           e->bodies[0].shape_due==due && e->bodies[1].actor==0 && e->bodies[1].record_kind==2 && e->bodies[1].body_id==0x0080000cu &&
           e->settings[0][0]==0x3e000000u && e->settings[1][0]==0x3f000000u && e->points[1][1][0]==0x42000004u);
    char out[8192];const int n=lxct_text(e,out,sizeof(out));
    static const char head[]="tick=133 ordinal=1 added=1 count=2 subshape=3f800009,3f80000a base=3f8000003f8000013f800002 normal=3f8000043f8000053f800006";
    assert(n>0 && !strncmp(out,head,strlen(head)));
    assert(strstr(out," depth=3f800008 shape_due0=40283f94fbfd44f3 record0=1 body0=4:0080000b:0:0:0:43000000430000014300000243000004"));
    assert(strstr(out," points1=420000004200000142000002420000044200000542000006 settings1=3f0000003e0000013e0000023e0000033e0000043e000005:"));
    lxct_added_override=lxct_persisted_override=NULL;tct_native_calls=0;
    memcpy(lxb_actors,saved_actors,sizeof(saved_actors));memcpy(lxb_body_slot,saved_slots,sizeof(saved_slots));lxb_bound=saved_bound;
    lxct_claimed=0;lxct_count=0;lxct_calls=lxct_ordinal=0;lxct_failures=0;stats.active=0;boot_phase=0;
    printf("contact diag wrapper PASS\n");
}
static void test_contact_elf(void) {
    const uint32_t slots[2]={LXCT_ADDED_SLOT_VA,LXCT_PERSISTED_SLOT_VA},targets[2]={LXCT_ADDED_VA,LXCT_PERSISTED_VA};
    assert(rd64(LX(LXCT_ADDED_SLOT_VA))==LXCT_ADDED_VA && rd64(LX(LXCT_PERSISTED_SLOT_VA))==LXCT_PERSISTED_VA); /* the file's VAs */
    assert(site_ok(site_named("contact_listener_slots")));
    /* Loaded at the executable's base the slots hold the functions' addresses: relocate them for the round, then back. */
    for(unsigned i=0;i<2;i++) {
        unsigned char *page=(unsigned char*)((uintptr_t)LX(slots[i])&~(uintptr_t)(GATE_PAGE-1));
        assert(!mprotect(page,GATE_PAGE,PROT_READ|PROT_WRITE));const void *f=LX(targets[i]);memcpy(LX(slots[i]),&f,8);
    }
    lxct_installed=lxct_restored=0;
    assert(lxct_install() && lxct_installed && lxct_owned() && rd64(LX(LXCT_ADDED_SLOT_VA))==(uint64_t)(uintptr_t)lxct_added &&
           rd64(LX(LXCT_PERSISTED_SLOT_VA))==(uint64_t)(uintptr_t)lxct_persisted);
    lxct_restore();
    assert(!lxct_installed && lxct_restored && rd64(LX(LXCT_ADDED_SLOT_VA))==(uint64_t)(uintptr_t)LX(LXCT_ADDED_VA) &&
           rd64(LX(LXCT_PERSISTED_SLOT_VA))==(uint64_t)(uintptr_t)LX(LXCT_PERSISTED_VA));
    lxct_restored=0;
    for(unsigned i=0;i<2;i++) {
        unsigned char *page=(unsigned char*)((uintptr_t)LX(slots[i])&~(uintptr_t)(GATE_PAGE-1));
        assert(!mprotect(page,GATE_PAGE,PROT_READ|PROT_WRITE));const uint64_t va=targets[i];memcpy(LX(slots[i]),&va,8);
    }
    printf("contact diag ELF PASS\n");
}
