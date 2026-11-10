/* lockstep_pilot_aim.h on synthetic memory (owned controllers, a made-up transform, stand-ins for X4's functions): the
 * replayed aim's rules. Whether X4's guns then turn is the live run's question (reports/LOCKSTEP-UI-MISSIONS-20261101.txt). */
static unsigned test_paim_original_calls;
static unsigned char test_paim_manual_value;
static _Alignas(16) unsigned char test_paim_ship[64],test_paim_space[64];
static unsigned char test_paim_original(void *controller,float *first,float *second,void *space,void *weapon,void *transform,float a7,float a8,
                                        unsigned char a9,unsigned char a10,unsigned char a11,unsigned char a12,unsigned char a13) {
    (void)controller;(void)first;(void)second;(void)space;(void)weapon;(void)transform;(void)a7;(void)a8;(void)a9;(void)a10;(void)a11;(void)a12;(void)a13;
    ++test_paim_original_calls;return 0;
}
static unsigned char test_paim_manual(void *self) {assert(!self);return test_paim_manual_value;}
/* The ship in the asked frame: at (10,20,30), its x along y, its y along z, its z along x. */
static void test_paim_transform(void *from,float *t,void *to,uint64_t flag) {
    assert(from==(void*)test_paim_ship && to==(void*)test_paim_space && !flag);
    memset(t,0,64);t[0]=10.0f;t[1]=20.0f;t[2]=30.0f;t[5]=1.0f;t[10]=1.0f;t[12]=1.0f;
}
#define TEST_PAIM_CALL(c) paim_aim(c,first,second,test_paim_space,NULL,NULL,0.0f,0.0f,0,0,0,0,0)
/* The assist: X4's aim function asks the controller's vtable slot 17 for the target, as 7e72b0 does. */
static unsigned char test_paim_target_object[64],*test_paim_target_value;
static void *test_paim_seen_target;
static void **test_paim_no_target(void *controller,void **out,void *weapon) {(void)controller;(void)weapon;*out=NULL;return out;}
static unsigned char test_paim_not_manual(void *controller) {(void)controller;return 0;}
static unsigned char *test_paim_target(unsigned slot) {assert(slot==2);return test_paim_target_value;}
static unsigned char test_paim_original_asks(void *controller,float *first,float *second,void *space,void *weapon,void *transform,float a7,float a8,
                                             unsigned char a9,unsigned char a10,unsigned char a11,unsigned char a12,unsigned char a13) {
    (void)first;(void)second;(void)space;(void)transform;(void)a7;(void)a8;(void)a9;(void)a10;(void)a11;(void)a12;(void)a13;
    void *target=(void*)1;paim_target_get(controller,&target,weapon);test_paim_seen_target=target;
    ++test_paim_original_calls;return target!=NULL;
}
static void test_pilot_aim(void) {
    static unsigned char c1[0x100],c2[0x100],c3[0x100];
    pstk_fns saved_pstk=pstk_fn;pfire_slot saved_fire[PILOT_SLOTS];memcpy(saved_fire,pfire_slots,sizeof(saved_fire));
    pilot_slot saved_slots[2]={pilot_slots[0],pilot_slots[1]};const unsigned saved_count=pilot_slot_count,saved_pres_slot=pres_slot;
    pstk_slot_frame saved_frame=pstk_slot_frames[1];const uint64_t saved_issued=stats.issued,saved_aim_tick=pstk_aim_tick;
    const DWORD saved_thread=ui_thread;const int saved_armed=paim_armed,saved_valid=pstk_aim_valid_replay,saved_focus=pstk_focus_forced;
    const int saved_paused=pres_paused;const LONG saved_open=pres_open;
    float saved_replay[3];memcpy(saved_replay,pstk_aim_replay,12);const unsigned char saved_steer=pstk_aim_steer_replay;
    const pstk_aim_fn saved_original=paim_original;
    const float point[3]={1.0f,2.0f,3.0f};float first[4],second[4];
    pstk_fn.transform=test_paim_transform;paim_original=test_paim_original;paim_manual_override=test_paim_manual;test_paim_original_calls=0;
    memset(pfire_slots,0,sizeof(pfire_slots));pilot_slot_count=2;pilot_slots[0].object=test_paim_ship;pilot_slots[1].object=test_paim_ship;
    pfire_slots[0].leased=1;pfire_slots[0].count=1;pfire_slots[0].guns[0].controller=c1;
    pfire_slots[1].leased=1;pfire_slots[1].count=2;pfire_slots[1].guns[0].controller=c1+8;pfire_slots[1].guns[1].controller=c2;
    ui_thread=GetCurrentThreadId();stats.issued=50;paim_armed=1;
    pstk_slot_frame *f=&pstk_slot_frames[1];memset(f,0,sizeof(*f));
    f->tick=50;f->r.flags=PSTK_FLAG_AIM;memcpy(f->r.aim,point,12);f->r.steer=1u|PAIM_STEER_MANUAL;
    /* Slot 2's record of this tick with a manual aim: the point in the asked frame, in both outputs. */
    {   const LONG64 aimed=paim_aimed;memset(first,0x55,16);memset(second,0x55,16);
        assert(TEST_PAIM_CALL(c2)==1 && first[0]==13.0f && first[1]==21.0f && first[2]==32.0f && first[3]==0.0f && !memcmp(first,second,16));
        assert(paim_aimed==aimed+1 && !test_paim_original_calls);}
    /* No manual aim, another tick's record, a record without an aim, no ship: X4's own answer (at rest), outputs untouched. */
    {   const LONG64 rest=paim_rest;memset(first,0x55,16);
        f->r.steer=1;assert(!TEST_PAIM_CALL(c2) && test_paim_original_calls==1);f->r.steer=PAIM_STEER_MANUAL;
        f->tick=49;assert(!TEST_PAIM_CALL(c2));f->tick=50;
        f->r.flags=0;assert(!TEST_PAIM_CALL(c2));f->r.flags=PSTK_FLAG_AIM;
        pilot_slots[1].object=NULL;assert(!TEST_PAIM_CALL(c2));pilot_slots[1].object=test_paim_ship;
        assert(!paim_aim(c2,first,second,NULL,NULL,NULL,0.0f,0.0f,0,0,0,0,0));
        unsigned char same[16];memset(same,0x55,16);
        assert(paim_rest==rest+5 && test_paim_original_calls==5 && !memcmp(first,same,16));
        assert(TEST_PAIM_CALL(c2)==1 && first[0]==13.0f);}   /* the manual bit alone, without mouse steering's */
    /* Slot 1: the stock record's replayed aim. */
    {   pstk_aim_tick=50;pstk_aim_valid_replay=1;memcpy(pstk_aim_replay,point,12);pstk_aim_steer_replay=1u|PAIM_STEER_MANUAL;memset(first,0,16);
        assert(TEST_PAIM_CALL(c1)==1 && first[0]==13.0f && first[2]==32.0f);
        pstk_aim_steer_replay=1;assert(!TEST_PAIM_CALL(c1));pstk_aim_steer_replay=1u|PAIM_STEER_MANUAL;
        pstk_aim_valid_replay=0;assert(!TEST_PAIM_CALL(c1));pstk_aim_valid_replay=1;
        pstk_aim_tick=51;assert(!TEST_PAIM_CALL(c1));pstk_aim_tick=50;}
    /* A Mindless controller that is no lease of ours (the AI's <shoot>), one the simulation replaced, a slot whose lease
     * ended, another thread, not armed: X4's own answer. */
    {   const LONG64 other=paim_other,foreign=paim_foreign;const unsigned calls=test_paim_original_calls;
        assert(!TEST_PAIM_CALL(c3) && paim_other==other+1);
        pfire_slots[1].guns[1].foreign=1;assert(!TEST_PAIM_CALL(c2) && paim_other==other+2);pfire_slots[1].guns[1].foreign=0;
        pfire_slots[1].leased=0;assert(!TEST_PAIM_CALL(c2) && paim_other==other+3);pfire_slots[1].leased=1;
        ui_thread=saved_thread+1;assert(!TEST_PAIM_CALL(c2) && paim_foreign==foreign+1);ui_thread=GetCurrentThreadId();
        paim_armed=0;assert(!TEST_PAIM_CALL(c2) && paim_other==other+4);paim_armed=1;
        assert(test_paim_original_calls==calls+5 && TEST_PAIM_CALL(c2)==1);}
    /* Slot 15, "manual aim?": yes for a leased controller whose slot's record of this tick has the manual bit. */
    {   const paim_manual_fn saved_manual=paim_manual_original;const LONG64 answers=paim_manual_answers;
        paim_manual_original=test_paim_not_manual;
        f->r.steer=1u|PAIM_STEER_MANUAL;assert(paim_manual_get(c2)==1 && paim_manual_get(c1)==1 && paim_manual_answers==answers+2);
        f->r.steer=1;assert(!paim_manual_get(c2));f->r.steer=PAIM_STEER_ASSIST;assert(!paim_manual_get(c2));f->r.steer=PAIM_STEER_MANUAL;
        f->tick=49;assert(!paim_manual_get(c2));f->tick=50;
        assert(!paim_manual_get(c3));                                                            /* no lease of ours */
        ui_thread=saved_thread+1;assert(!paim_manual_get(c2));ui_thread=GetCurrentThreadId();    /* another thread */
        paim_armed=0;assert(!paim_manual_get(c2));paim_armed=1;
        assert(paim_manual_get(c2)==1 && paim_manual_answers==answers+3);
        paim_manual_original=saved_manual;f->r.steer=1u|PAIM_STEER_MANUAL;}
    /* The aim assist: no manual bit, the assist bit and a target of the slot: slot 17 answers the target for the slot's
     * leased controllers, and X4's own aim runs with it. No target, the slot's own ship, a manual aim, the assist not
     * armed: none, X4's answer. */
    {   const LONG64 assisted=paim_assisted,missed=paim_assist_missed,answers=paim_target_answers;const int assist=paim_assist_on;
        const paim_target_fn saved_target=paim_target_original;void *out=(void*)1;
        paim_original=test_paim_original_asks;paim_target_original=test_paim_no_target;paim_target_override=test_paim_target;
        paim_assist_on=1;f->r.steer=PAIM_STEER_ASSIST;test_paim_target_value=test_paim_target_object;test_paim_seen_target=NULL;
        paim_target_get(c2,&out,NULL);assert(out==(void*)test_paim_target_object && paim_target_answers==answers+1);
        assert(TEST_PAIM_CALL(c2)==1 && test_paim_seen_target==(void*)test_paim_target_object && paim_assisted==assisted+1);
        test_paim_target_value=NULL;assert(!TEST_PAIM_CALL(c2) && !test_paim_seen_target && paim_assist_missed==missed+1);
        test_paim_target_value=test_paim_ship;out=(void*)1;paim_target_get(c2,&out,NULL);assert(!out);   /* its own ship */
        test_paim_target_value=test_paim_target_object;
        out=(void*)1;paim_target_get(c3,&out,NULL);assert(!out);                                          /* no lease of ours */
        f->tick=49;out=(void*)1;paim_target_get(c2,&out,NULL);assert(!out);f->tick=50;                    /* another tick's record */
        paim_assist_on=0;out=(void*)1;paim_target_get(c2,&out,NULL);assert(!out);paim_assist_on=1;        /* the assist not armed */
        ui_thread=saved_thread+1;out=(void*)1;paim_target_get(c2,&out,NULL);assert(!out);ui_thread=GetCurrentThreadId();
        f->r.steer=PAIM_STEER_ASSIST|PAIM_STEER_MANUAL;out=(void*)1;paim_target_get(c2,&out,NULL);assert(!out);   /* manual first */
        test_paim_seen_target=NULL;
        assert(TEST_PAIM_CALL(c2)==1 && !test_paim_seen_target && first[0]==13.0f && paim_assisted==assisted+1);
        paim_original=test_paim_original;paim_target_original=saved_target;paim_target_override=NULL;paim_assist_on=assist;
        f->r.steer=1u|PAIM_STEER_MANUAL;}
    /* The record: a slot's aim is accepted only while this is armed; the steering byte carries the manual bit. */
    {   pstk_record r;memset(&r,0,sizeof(r));r.flags=PSTK_FLAG_AIM;r.steer=1u|PAIM_STEER_MANUAL;memcpy(r.aim,point,12);r.capture16=7;
        unsigned char bytes[PSTK_BYTES_MAX];pstk_record back;const unsigned n=pstk_encode(&r,bytes);
        assert(n && pstk_decode(bytes,n,&back) && back.steer==(1u|PAIM_STEER_MANUAL) && back.aim[2]==3.0f);
        bytes[28]=8;assert(!pstk_decode(bytes,n,&back));bytes[28]=PAIM_STEER_ASSIST;assert(pstk_decode(bytes,n,&back));
        assert(pstk_slot_record_ok(&r,1) && pstk_slot_record_ok(&r,2));
        paim_armed=0;assert(pstk_slot_record_ok(&r,1) && !pstk_slot_record_ok(&r,2));paim_armed=1;}
    /* The capture: no aim without the focus; slots 2..N only inside their own presentation scope. */
    {   float aim[3];unsigned char steer=0;const LONG64 unfocused=pstk_aim_unfocused,no_scope=paim_no_scope;
        pstk_focus_forced=-1;assert(paim_take(2,aim,&steer)==-1 && pstk_aim_unfocused==unfocused+1);pstk_focus_forced=1;
        pres_open=0;pres_paused=0;pres_slot=2;assert(!paim_take(2,aim,&steer) && paim_no_scope==no_scope+1);   /* no scope to open */
        pres_slot=3;pres_paused=1;assert(!paim_take(2,aim,&steer) && paim_no_scope==no_scope+2 && pres_paused==1); /* another slot's */
        paim_armed=0;assert(!paim_take(2,aim,&steer) && paim_no_scope==no_scope+2);paim_armed=1;
        /* A scripted aim (runs without people): the point as a manual aim, whatever the focus and the scope. */
        paim_script_on=1;paim_script_point[0]=4.0f;paim_script_point[1]=5.0f;paim_script_point[2]=6.0f;pstk_focus_forced=-1;steer=0;
        assert(paim_take(2,aim,&steer)==1 && aim[0]==4.0f && aim[2]==6.0f && steer==(1u|PAIM_STEER_MANUAL) && paim_no_scope==no_scope+2);
        paim_script_on=2;steer=0;assert(paim_take(2,aim,&steer)==1 && steer==PAIM_STEER_ASSIST);
        paim_script_on=0;pstk_focus_forced=1;}
    /* Arming: only with the record's fire lease, on the UI thread. */
    {   const int fire=pfire_armed,mode=pfire_mode;paim_armed=0;
        pfire_armed=0;assert(!paim_arm(0) && !strcmp(paim_why,"fire_record"));
        pfire_armed=1;pfire_mode=PFIRE_SCRIPT;assert(!paim_arm(1) && !strcmp(paim_why,"fire_record"));
        pfire_mode=PFIRE_RECORD;ui_thread=saved_thread+1;assert(!paim_arm(0) && !strcmp(paim_why,"thread"));
        paim_armed=1;paim_assist_on=1;assert(paim_arm(1) && !paim_arm(0));paim_armed=0;paim_assist_on=0;   /* armed: the same form only */
        pfire_armed=fire;pfire_mode=mode;paim_why="-";}
    char text[512];paim_armed=1;assert(paim_status(text,sizeof(text))>0 && strstr(text,"armed=1") && strstr(text,"no_scope="));
    pstk_fn=saved_pstk;memcpy(pfire_slots,saved_fire,sizeof(saved_fire));pilot_slots[0]=saved_slots[0];pilot_slots[1]=saved_slots[1];
    pilot_slot_count=saved_count;pres_slot=saved_pres_slot;pstk_slot_frames[1]=saved_frame;stats.issued=saved_issued;pstk_aim_tick=saved_aim_tick;
    ui_thread=saved_thread;paim_armed=saved_armed;pstk_aim_valid_replay=saved_valid;pstk_focus_forced=saved_focus;pres_paused=saved_paused;
    pres_open=saved_open;memcpy(pstk_aim_replay,saved_replay,12);pstk_aim_steer_replay=saved_steer;paim_original=saved_original;
    paim_manual_override=NULL;
}
/* The pinned image: the two functions' bytes and the Mindless vtable's slot 21. */
static void test_pilot_aim_native_image(const char *path) {
    HANDLE file=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    assert(file!=INVALID_HANDLE_VALUE);
    HANDLE mapping=CreateFileMappingA(file,NULL,PAGE_READONLY|SEC_IMAGE_NO_EXECUTE,0,0,NULL);
    assert(mapping);
    const unsigned char *view=MapViewOfFile(mapping,FILE_MAP_READ,0,0,0);
    assert(view);
    for(unsigned i=0;i<sizeof(paim_pins_list)/sizeof(*paim_pins_list);i++)
        assert(hash_ok(view+paim_pins_list[i].rva,paim_pins_list[i].size,paim_pins_list[i].sha));
    uint64_t entry=0,base=0;const IMAGE_DOS_HEADER *dos=(const void*)view;const IMAGE_NT_HEADERS64 *nt=(const void*)(view+dos->e_lfanew);
    base=nt->OptionalHeader.ImageBase;memcpy(&entry,view+PFIRE_MINDLESS_VTABLE+PAIM_VTABLE_SLOT,8);
    /* mapped as an image without relocation applied at its preferred base or at the view: the entry is one of the two */
    assert(entry==base+PAIM_MINDLESS_AIM || entry==(uint64_t)(uintptr_t)view+PAIM_MINDLESS_AIM);
    for(unsigned i=0;i<PAIM_ENTRIES;i++) {
        memcpy(&entry,view+PFIRE_MINDLESS_VTABLE+paim_entries[i].slot,8);
        assert(entry==base+paim_entries[i].target || entry==(uint64_t)(uintptr_t)view+paim_entries[i].target);
    }
    memcpy(&entry,view+PFIRE_MINDLESS_VTABLE+0x90u,8);   /* slot 18: slot 17's answer */
    assert(entry==base+PAIM_TARGET_VIA || entry==(uint64_t)(uintptr_t)view+PAIM_TARGET_VIA);
    UnmapViewOfFile(view);CloseHandle(mapping);CloseHandle(file);
    puts("pilot aim: pinned image (Mindless aim, manual aim predicate, vtable slots 15/17/18/21) PASS");
}
