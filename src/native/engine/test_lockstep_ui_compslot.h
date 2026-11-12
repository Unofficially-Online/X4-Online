/* lockstep_ui_compslot.h on synthetic memory: the class of a container's action (single, multi), the perform hooks in
 * their three modes, the export called as a peek and as an apply through a stand-in, the event's shape, the empty verdict
 * table. The capture and the apply through the replay need the game. */
static unsigned test_uics_originals[2],test_uics_last_index;
static unsigned char test_uics_original_single(void *container,unsigned index) {(void)container;++test_uics_originals[0];test_uics_last_index=index;return 1;}
static unsigned char test_uics_original_multi(void *container,unsigned index) {(void)container;++test_uics_originals[1];test_uics_last_index=index;return 1;}
static unsigned char *test_uics_container;static int test_uics_multi;static unsigned test_uics_exports;static uint64_t test_uics_component;
static char test_uics_connection[UICS_NAME_MAX+1];
/* X4's export as far as this module sees it: the container's perform for the index, its answer back. */
static unsigned char test_uics_export(const uics_slot_arg *slot,uint32_t index) {
    ++test_uics_exports;test_uics_component=slot->component;snprintf(test_uics_connection,sizeof(test_uics_connection),"%s",slot->connection);
    return test_uics_multi?uics_perform_multi(test_uics_container,index):uics_perform_single(test_uics_container,index);
}
static void test_ui_compslot(void) {
    static _Alignas(16) unsigned char region[0x100],single[0x40],multi[0x40],action[3][0x20];static unsigned char *vector[2];
    unsigned char *saved_base=pnat_base;
    memset(single,0,sizeof(single));memset(multi,0,sizeof(multi));memset(action,0,sizeof(action));
    pnat_base=region;
    void *vt;unsigned char *p;
    vt=region+0x10;memcpy(action[0],&vt,8);vt=region+0x20;memcpy(action[1],&vt,8);vt=region+0x30;memcpy(action[2],&vt,8);
    p=action[0];memcpy(single+0x10,&p,8);
    vector[0]=action[1];vector[1]=action[2];
    p=(unsigned char *)&vector[0];memcpy(multi+0x10,&p,8);p=(unsigned char *)&vector[2];memcpy(multi+0x18,&p,8);
    assert(uics_class(single,0,0)==0x10 && !uics_class(single,1,0) && !uics_class(NULL,0,0));
    assert(uics_class(multi,0,1)==0x20 && uics_class(multi,1,1)==0x30 && !uics_class(multi,2,1));
    /* the table: slot 1's airlock and control position; nothing else, nobody else */
    assert(!uics_verdict(1,0x10) && !uics_verdict(2,0x10) && !uics_verdict(1,0));
    assert(uics_verdict(1,UICS_CLASS_AIRLOCK) && uics_verdict(1,UICS_CLASS_CONTROL) && !uics_verdict(2,UICS_CLASS_AIRLOCK) && !uics_verdict(8,UICS_CLASS_CONTROL));
    /* slot 1's vehicle (lockstep_pilot_death.h): another ship is expected with no ship of its own, or inside the window of an
     * applied action; never without the death module */
    {   const int armed=pdeath_armed;const uint64_t until=pveh_allow_until;unsigned char ship[8]={0};
        pdeath_armed=0;pveh_allow_until=0;assert(!pveh_adopt_ok(10,NULL));
        pdeath_armed=1;assert(pveh_adopt_ok(10,NULL) && !pveh_adopt_ok(10,ship));
        pveh_allow(100);assert(pveh_adopt_ok(100+PVEH_WINDOW,ship) && !pveh_adopt_ok(101+PVEH_WINDOW,ship));
        pdeath_armed=armed;pveh_allow_until=until;
    }
    /* the on-foot click: refused until this module is armed, then this client's own */
    {   const int foot=pfoot_armed,on=uics_on;
        pfoot_armed=1;uics_on=0;assert(pfoot_id(1u,0x78)==PFOOT_REFUSE_ID && pstk_verdict(PSTK_STATE,0x78)==PSTK_REFUSE);
        uics_on=1;assert(pfoot_id(1u,0x78)==PFOOT_NONE && pstk_verdict(PSTK_STATE,0x78)==PSTK_LOCAL && pstk_verdict_slot(PSTK_STATE,0x78,2)==PSTK_LOCAL);
        assert(pfoot_id(1u,0x1a)==PFOOT_WALK_ID);
        pfoot_armed=foot;uics_on=on;
    }
    /* slots 2..N: the seat of the own ship while walking is the sit-down; another ship's, another class, seated: not */
    {   static _Alignas(16) unsigned char ship[0x80],room[0x80],seat[0x80],other[0x80];unsigned char *q;
        memset(ship,0,sizeof(ship));memset(other,0,sizeof(other));
        q=ship;memcpy(room+0x70,&q,8);q=room;memcpy(seat+0x70,&q,8);
        assert(uics_under(seat,ship) && uics_under(room,ship) && uics_under(ship,ship) && !uics_under(other,ship) && !uics_under(seat,other) &&
               !uics_under(NULL,ship) && !uics_under(seat,NULL));
        unsigned char *const was_ship=pslots[2].ship;const int armed=pfslot_armed,capturing=pstk_capturing;const unsigned slot=pstk_capture_slot;
        const unsigned char walking=(unsigned char)pfslots[2].walking;
        pslots[2].ship=ship;pfslot_armed=1;pstk_capturing=1;pstk_capture_slot=2;pfslots[2].walking=1;
        assert(uics_slot_seat(2,UICS_CLASS_CONTROL,seat) && !uics_slot_seat(2,UICS_CLASS_AIRLOCK,seat) && !uics_slot_seat(2,UICS_CLASS_CONTROL,other) &&
               !uics_slot_seat(1,UICS_CLASS_CONTROL,seat));
        pfslots[2].walking=0;assert(!uics_slot_seat(2,UICS_CLASS_CONTROL,seat));
        pslots[2].ship=was_ship;pfslot_armed=armed;pstk_capturing=capturing;pstk_capture_slot=slot;pfslots[2].walking=walking;
    }
    uics_perform_test[0]=test_uics_original_single;uics_perform_test[1]=test_uics_original_multi;uics_export_test=test_uics_export;
    test_uics_originals[0]=test_uics_originals[1]=0;test_uics_exports=0;
    const LONG64 passed=uics_passed;
    /* X4's own call: straight through */
    InterlockedExchange(&uics_mode,UICS_PASS);
    assert(uics_perform_single(single,0)==1 && test_uics_originals[0]==1 && uics_passed==passed+1);
    /* a peek: the class noted, nothing performed */
    test_uics_container=single;test_uics_multi=0;
    assert(uics_call(UICS_PEEK,0x1234,"con_airlock",0,0) && test_uics_exports==1 && uics_seen==0x10 && !uics_ran && test_uics_originals[0]==1);
    assert(test_uics_component==0x1234 && !strcmp(test_uics_connection,"con_airlock") && uics_mode==UICS_PASS);
    test_uics_container=multi;test_uics_multi=1;
    assert(uics_call(UICS_PEEK,0x1234,"con_x",1,0) && uics_seen==0x30 && !test_uics_originals[1]);
    /* an apply: the named class performs, another class or a missing action does not */
    assert(uics_call(UICS_APPLY,0x1234,"con_x",1,0x30) && uics_ran && test_uics_originals[1]==1 && test_uics_last_index==1);
    assert(uics_call(UICS_APPLY,0x1234,"con_x",0,0x30) && !uics_ran && uics_seen==0x20 && test_uics_originals[1]==1);
    assert(uics_call(UICS_APPLY,0x1234,"con_x",5,0x30) && !uics_ran && !uics_seen && test_uics_originals[1]==1);
    /* another thread's call during a peek passes: the mode is this thread's */
    {const DWORD mine=uics_thread;InterlockedExchange(&uics_mode,UICS_PEEK);uics_thread=mine+1;
     assert(uics_perform_single(single,0)==1 && test_uics_originals[0]==2);InterlockedExchange(&uics_mode,UICS_PASS);uics_thread=mine;}
    /* the event's shape: reference, index, class, name; not armed and a class nobody may use are refused */
    {   unsigned char data[UIREP_REF+6+4];const char *why="-";const int armed=uics_armed;
        memset(data,0,sizeof(data));data[UIREP_REF]=0;data[UIREP_REF+1]=0x10;data[UIREP_REF+5]=4;memcpy(data+UIREP_REF+6,"con1",4);
        uics_armed=0;assert(!uics_event_ok(1,UICS_OP,data,sizeof(data),&why) && !strcmp(why,"ui_compslot"));
        uics_armed=1;why="-";assert(!uics_event_ok(1,UICS_OP,data,sizeof(data),&why) && !strcmp(why,"ui_compslot_class"));
        why="-";assert(!uics_event_ok(1,UICS_OP,data,sizeof(data)-1,&why) && !strcmp(why,"ui_compslot"));
        data[UIREP_REF+5]=0;why="-";assert(!uics_event_ok(1,UICS_OP,data,UIREP_REF+7,&why) && !strcmp(why,"ui_compslot"));
        uics_armed=armed;
    }
    {char out[256];assert(uics_status(out,sizeof(out)) && strstr(out," compslot=") && strstr(out," last=") && strstr(out," seat="));}
    uics_perform_test[0]=uics_perform_test[1]=NULL;uics_export_test=NULL;uics_seen=uics_expected=0;uics_ran=0;
    InterlockedExchange64(&uics_passed,passed);pnat_base=saved_base;
    puts("ui compslot: the action's class from a single and a multi container, peek and apply through the perform hooks, the event's shape, the click and the own seat of slots 2..N PASS");
}
