/* The fixture bodies around Jolt's step (gate_body_step.h): a planted fixture ship's two bodies through the wrapper
 * (copied before and after the native step, the step's delta time and collision steps kept, another physics system
 * left alone, the step count cleared by a read); on the real executable the pin, the entry installed (jmp [rip] -> the
 * wrapper, two int3; the trampoline the 16 bytes and a jmp back) and restored. Included by test_gate_linux.c. */
static unsigned char tbs_object[0x300],tbs_record[2][0x80],tbs_xbody[2][0x10],tbs_scene[0x28],tbs_system[0x150],tbs_other[0x150];
static unsigned char tbs_iface[0x10],tbs_manager[0x18],tbs_body[2][0x80] __attribute__((aligned(16))),tbs_motion[2][0x80] __attribute__((aligned(16)));
static void *tbs_bodies[2];
static int tbs_native_calls;
static uint32_t tbs_native(void *system,float dt,int steps,void *temp,void *jobs) {
    (void)system;(void)dt;(void)steps;(void)temp;(void)jobs;tbs_native_calls++;
    tbs_motion[1][0x10]^=0x01u;tbs_body[0][0x00]^=0x02u; /* the step moves body0 and turns body1 */
    return 7u;
}
static void test_body_step_linux(void) {
    static lxb_actor saved_actors[LXB_ACTORS];static unsigned saved_slots[LXB_ACTORS];int saved_bound=lxb_bound;
    memcpy(saved_actors,lxb_actors,sizeof(saved_actors));memcpy(saved_slots,lxb_body_slot,sizeof(saved_slots));
    memset(lxb_actors,0,sizeof(lxb_actors));memset(lxb_body_slot,0,sizeof(lxb_body_slot));memset(lxbs_samples,0,sizeof(lxbs_samples));
    lxbs_updates=lxbs_captured=lxbs_failures=0;
    lxb_bound=LXB_ACTORS;lxb_actors[0].object=tbs_object;lxb_body_slot[0]=0x258u;
    void *p;
    for(unsigned w=0;w<2;w++) {
        p=tbs_record[w];memcpy(tbs_object+0x258+w*8,&p,8);p=tbs_object;memcpy(tbs_record[w],&p,8);
        p=tbs_xbody[w];memcpy(tbs_record[w]+0x10,&p,8);const uint32_t id=w;memcpy(tbs_xbody[w]+8,&id,4);
        p=tbs_scene;memcpy(tbs_record[w]+0x18,&p,8);
        tbs_bodies[w]=tbs_body[w];p=tbs_motion[w];memcpy(tbs_body[w]+0x48,&p,8);
        for(unsigned i=0;i<0x40;i++) {tbs_body[w][i]=(unsigned char)(0x10*w+i);tbs_motion[w][i]=(unsigned char)(0x80+0x10*w+i);}
    }
    p=tbs_system;memcpy(tbs_scene+0x20,&p,8);p=tbs_iface;memcpy(tbs_system+LXB_PHYS_LOCK_IFACE,&p,8);
    p=tbs_manager;memcpy(tbs_iface+8,&p,8);const uint64_t count=2;memcpy(tbs_manager,&count,8);p=tbs_bodies;memcpy(tbs_manager+0x10,&p,8);
    lxbs_original_override=tbs_native;
    const float dt=1.0f/30.0f;uint32_t dt_bits;memcpy(&dt_bits,&dt,4);
    unsigned char motion1_before[0x80],body0_before[0x80];memcpy(motion1_before,tbs_motion[1],0x80);memcpy(body0_before,tbs_body[0],0x80);
    assert(lxbs_update(tbs_system,dt,1,NULL,NULL)==7u && tbs_native_calls==1 && lxbs_updates==1 && lxbs_captured==2 && !lxbs_failures);
    const lxbs_sample *s0=&lxbs_samples[0][0],*s1=&lxbs_samples[0][1];
    assert(s0->valid && s1->valid && s1->steps==1 && s1->dt==dt_bits && s1->collision==1);
    assert(!memcmp(s1->motion[0],motion1_before,0x80) && !memcmp(s1->motion[1],tbs_motion[1],0x80) && s1->motion[1][0x10]!=motion1_before[0x10]);
    assert(!memcmp(s0->body[0],body0_before,0x80) && !memcmp(s0->body[1],tbs_body[0],0x80));
    assert(lxbs_update(tbs_other,dt,1,NULL,NULL)==7u && lxbs_captured==2 && s1->steps==1); /* another physics system */
    char text[2400];
    assert(lxbs_text(1,1,text,sizeof(text))>0 && !strncmp(text,"steps=1 dt=3d088889 collision=1 body0=",38) && strstr(text," motion1="));
    assert(lxbs_text(1,1,text,sizeof(text))>0 && !strncmp(text,"steps=0 ",8));
    assert(!lxbs_text(2,0,text,sizeof(text)) && !lxbs_text(1,2,text,sizeof(text)));
    lxbs_original_override=NULL;tbs_native_calls=0;
    memcpy(lxb_actors,saved_actors,sizeof(saved_actors));memcpy(lxb_body_slot,saved_slots,sizeof(saved_slots));lxb_bound=saved_bound;
    memset(lxbs_samples,0,sizeof(lxbs_samples));lxbs_updates=lxbs_captured=lxbs_failures=0;
    printf("body step capture/wrapper PASS\n");
}
static void test_body_step_elf(void) {
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();lxbs_installed=lxbs_restored=0;
    unsigned char before[16];memcpy(before,LX(LXBS_UPDATE_VA),16);
    assert(!memcmp(before,lxbs_prologue,16));
    assert(lxbs_pins() && lxbs_state()==0 && lxbs_arm() && lxbs_installed && lxbs_state()==1 && lxbs_check() && lxbs_pins());
    assert(LX(LXBS_UPDATE_VA)[0]==0xff && LX(LXBS_UPDATE_VA)[1]==0x25 && LX(LXBS_UPDATE_VA)[14]==0xcc && LX(LXBS_UPDATE_VA)[15]==0xcc &&
           !memcmp(lxbs_page,before,16) && lxbs_page[16]==0xff && lxbs_page[17]==0x25);
    {const void *back;memcpy(&back,lxbs_page+22,8);assert(back==(const void*)LX(LXBS_UPDATE_VA+16u));}
    char status[256];assert(lxbs_status(status,sizeof(status))>0 && strstr(status," state=1 pin=1 stage=armed"));
    lxbs_restore();assert(!lxbs_installed && lxbs_restored && !memcmp(LX(LXBS_UPDATE_VA),before,16) && lxbs_state()==0 && lxbs_pins());
    munmap(lxbs_page,GATE_PAGE);lxbs_page=NULL;lxbs_restored=0;stats.active=0;
    printf("body step ELF PASS\n");
}
