/* The impulses on fixture bodies (gate_impulse.h): a planted fixture ship's second body, found by its BodyID, through
 * the wrapper (the impulse, the point, the pose and motion before, the velocities after, the shared order; another
 * body passes through untraced; the row text); on the real executable the pin, the entry installed (jmp [rip] -> the
 * wrapper, bytes 14..15 kept; the trampoline the 14 bytes and a jmp back) and restored. Included by test_gate_linux.c. */
static unsigned char tai_object[0x300],tai_record[2][0x80],tai_xbody[2][0x10],tai_scene[0x28],tai_system[0x150];
static unsigned char tai_iface[0x10],tai_manager[0x18],tai_body[2][0x80] __attribute__((aligned(16))),tai_motion[2][0x80] __attribute__((aligned(16)));
static void *tai_bodies[2];
static int tai_native_calls;
static void tai_native(void *iface,const void *id,lxai_vec impulse,lxai_vec point) {
    (void)iface;(void)id;(void)point;tai_native_calls++;
    float v[4];memcpy(v,tai_motion[1],16);for(unsigned i=0;i<3;i++) v[i]+=impulse.v[i];memcpy(tai_motion[1],v,16); /* the step's linear part */
}
static void test_impulse_linux(void) {
    static lxb_actor saved_actors[LXB_ACTORS];static unsigned saved_slots[LXB_ACTORS];int saved_bound=lxb_bound;
    memcpy(saved_actors,lxb_actors,sizeof(saved_actors));memcpy(saved_slots,lxb_body_slot,sizeof(saved_slots));
    memset(lxb_actors,0,sizeof(lxb_actors));memset(lxb_body_slot,0,sizeof(lxb_body_slot));memset(lxai_traces,0,sizeof(lxai_traces));
    lxai_calls=lxai_recorded=lxai_failures=0;
    reset_session();stats.active=1;stats.issued=28;ui_thread=GetCurrentThreadId();
    lxb_bound=LXB_ACTORS;lxb_actors[1].object=tai_object;lxb_body_slot[1]=0x258u;
    void *p;
    for(unsigned w=0;w<2;w++) {
        p=tai_record[w];memcpy(tai_object+0x258+w*8,&p,8);p=tai_object;memcpy(tai_record[w],&p,8);
        p=tai_xbody[w];memcpy(tai_record[w]+0x10,&p,8);const uint32_t id=w;memcpy(tai_xbody[w]+8,&id,4);memcpy(tai_body[w]+0x70,&id,4);
        p=tai_scene;memcpy(tai_record[w]+0x18,&p,8);
        tai_bodies[w]=tai_body[w];p=tai_motion[w];memcpy(tai_body[w]+0x48,&p,8);
        for(unsigned i=0;i<0x20;i++) tai_body[w][i]=(unsigned char)(0x10*w+i);
        for(unsigned i=0;i<0x60;i++) tai_motion[w][i]=(unsigned char)(0x40+i);
    }
    p=tai_system;memcpy(tai_scene+0x20,&p,8);p=tai_iface;memcpy(tai_system+LXB_PHYS_LOCK_IFACE,&p,8);
    p=tai_manager;memcpy(tai_iface+8,&p,8);const uint64_t count=2;memcpy(tai_manager,&count,8);p=tai_bodies;memcpy(tai_manager+0x10,&p,8);
    {const float zero[4]={0};memcpy(tai_motion[1],zero,16);}
    lxai_original_override=tai_native;
    const lxai_vec impulse={{1.0f,2.0f,3.0f,0.0f}},point={{-394.5f,36000.0f,600.0f,0.0f}};
    unsigned char motion_before[0x60];memcpy(motion_before,tai_motion[1],0x60);
    const uint32_t id1=1,id9=9;
    const uint64_t seq=lx_phys_seq;
    lxai_add(NULL,&id1,impulse,point);
    lxai_add(NULL,&id9,impulse,point);                                               /* not a fixture body */
    assert(tai_native_calls==2 && lxai_calls==2 && lxai_recorded==1 && !lxai_failures && lxai_traces[1].tick==28 && lxai_traces[1].count==1);
    const lxai_record *r=&lxai_traces[1].records[0];
    assert(r->which==1 && r->seq==seq+1 && r->source && !memcmp(r->impulse,impulse.v,16) && !memcmp(r->point,point.v,16));
    assert(!memcmp(r->pose,tai_body[1],32));
    assert(!memcmp(r->motion,motion_before,0x60));
    {float after[4];memcpy(after,r->after,16);assert(after[0]==1.0f && after[1]==2.0f && after[2]==3.0f);}
    lxai_installed=1;char text[1024];
    assert(lxai_text(2,1,text,sizeof(text))>0 && !strncmp(text,"which=1 tick=28 seq=",20) && strstr(text," impulse=3f80000040000000404000000000000 ")==NULL &&
           strstr(text," impulse=3f8000004000000040400000") && strstr(text," after=3f8000004000000040400000"));
    assert(!lxai_text(2,2,text,sizeof(text)) && !lxai_text(1,1,text,sizeof(text)));
    lxai_installed=0;lxai_original_override=NULL;tai_native_calls=0;
    memcpy(lxb_actors,saved_actors,sizeof(saved_actors));memcpy(lxb_body_slot,saved_slots,sizeof(saved_slots));lxb_bound=saved_bound;
    memset(lxai_traces,0,sizeof(lxai_traces));lxai_calls=lxai_recorded=lxai_failures=0;stats.active=0;
    printf("impulse diag wrapper PASS\n");
}
static void test_impulse_elf(void) {
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();lxai_installed=lxai_restored=0;
    unsigned char before[16];memcpy(before,LX(LXAI_VA),16);
    assert(!memcmp(before,lxai_prologue,14));
    assert(lxai_pins() && lxai_state()==0 && lxai_arm() && lxai_installed && lxai_state()==1 && lxai_check() && lxai_pins());
    assert(LX(LXAI_VA)[0]==0xff && LX(LXAI_VA)[1]==0x25 && !memcmp(LX(LXAI_VA)+14,before+14,2) &&
           !memcmp(lxai_page,before,14) && lxai_page[14]==0xff && lxai_page[15]==0x25);
    {const void *back;memcpy(&back,lxai_page+20,8);assert(back==(const void*)LX(LXAI_VA+14u));}
    lxai_restore();assert(!lxai_installed && lxai_restored && !memcmp(LX(LXAI_VA),before,16) && lxai_state()==0 && lxai_pins());
    munmap(lxai_page,GATE_PAGE);lxai_page=NULL;lxai_restored=0;stats.active=0;
    printf("impulse diag ELF PASS\n");
}
