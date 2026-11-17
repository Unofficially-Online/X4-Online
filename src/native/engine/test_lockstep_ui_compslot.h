/* lockstep_ui_compslot.h on synthetic memory: the class of a container's action (single, multi), the perform hooks in
 * their three modes, the export called as a peek and as an apply through a stand-in, the event's shape, the empty verdict
 * table. The capture and the apply through the replay need the game. */
static unsigned test_uics_originals[2],test_uics_last_index;
static unsigned char test_uics_original_single(void *container,unsigned index) {(void)container;++test_uics_originals[0];test_uics_last_index=index;return 1;}
static unsigned char test_uics_original_multi(void *container,unsigned index) {(void)container;++test_uics_originals[1];test_uics_last_index=index;return 1;}
static unsigned char *test_uics_container;static int test_uics_multi;static unsigned test_uics_exports;static uint64_t test_uics_component;
static char test_uics_connection[UICS_NAME_MAX+1];
static unsigned char *test_uics_home,*test_uics_around_ship;   /* slot 1 on foot into a ship (pveh_aboard) */
static void *test_uics_find_home(void *map,uint64_t id,unsigned kind) {(void)kind;return map==test_uics_home && id==0x4321?test_uics_home:NULL;}
static unsigned char *test_uics_around(const unsigned char *player) {(void)player;return test_uics_around_ship;}
/* X4's export as far as this module sees it: the container's perform for the index, its answer back. */
static unsigned char test_uics_export(const uics_slot_arg *slot,uint32_t index) {
    ++test_uics_exports;test_uics_component=slot->component;snprintf(test_uics_connection,sizeof(test_uics_connection),"%s",slot->connection);
    return test_uics_multi?uics_perform_multi(test_uics_container,index):uics_perform_single(test_uics_container,index);
}
/* X4's perform of a seat as far as the crosshair matters: the action's own check through its vtable, then its answer. */
static unsigned test_uics_seat_asked;static unsigned char test_uics_seat_answer;
static unsigned char test_uics_possible_no(void *action,void *component) {(void)action;(void)component;return 0;}
static unsigned char test_uics_original_seat(void *container,unsigned index) {
    unsigned char *action=NULL;void **vtable=NULL;(void)index;
    memcpy(&action,(unsigned char *)container+0x10,8);memcpy(&vtable,action,8);
    ++test_uics_seat_asked;test_uics_seat_answer=((unsigned char(*)(void *,void *))vtable[1])(action,NULL);
    return test_uics_seat_answer;
}
/* The census's find (lockstep_world_hash.h): a mapped object that is dead is not there. */
static unsigned char test_census_object[0x100];
static void *test_census_find(void *map,uint64_t id,unsigned mode) {(void)map;(void)mode;return id==7?test_census_object:NULL;}
static void test_ui_compslot(void) {
    {   const world_hash_find_fn raw=world_hash_find_raw;const uint64_t skipped=world_hash_dead_skipped;
        memset(test_census_object,0,sizeof(test_census_object));world_hash_find_raw=test_census_find;
        assert(world_hash_find_live(NULL,7,4)==(void*)test_census_object && !world_hash_find_live(NULL,8,4));   /* alive; not mapped */
        test_census_object[0xd3]=1;assert(world_hash_find_live(NULL,7,4)==(void*)test_census_object);              /* dying: still there */
        test_census_object[0xd3]=2;assert(!world_hash_find_live(NULL,7,4) && world_hash_dead_skipped==skipped+1);  /* dead: gone */
        world_hash_find_raw=raw;world_hash_dead_skipped=skipped;
    }
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
    assert(uics_verdict(1,UICS_CLASS_AIRLOCK) && uics_verdict(1,UICS_CLASS_CONTROL) && uics_verdict(2,UICS_CLASS_AIRLOCK) && !uics_verdict(8,UICS_CLASS_CONTROL));
    /* slot 1's vehicle (lockstep_pilot_death.h): another ship is expected with no ship of its own, or inside the window of an
     * applied action; never without the death module */
    {   const int armed=pdeath_armed;const uint64_t until=pveh_allow_until;unsigned char ship[8]={0};
        pdeath_armed=0;pveh_allow_until=0;assert(!pveh_adopt_ok(10,NULL));
        pdeath_armed=1;assert(pveh_adopt_ok(10,NULL) && !pveh_adopt_ok(10,ship));
        pveh_allow(100);assert(pveh_adopt_ok(100+PVEH_WINDOW,ship) && !pveh_adopt_ok(101+PVEH_WINDOW,ship));
        pdeath_armed=armed;pveh_allow_until=until;
    }
    /* slot 1 out of its suit: X4 took its player off the suit's controls (no PlayerController) while it lives, the suit
     * whole or gone: the slot walks without a ship, no death; still at the controls of a whole suit: nothing; not in a
     * suit, or the player dying with it: not this */
    {   static _Alignas(16) unsigned char player[0x200],seat[8];pilot_slot s;pdeath_slot d;const void *const was=pdeath_player;
        pdeath_player=NULL;   /* as live: the controller's object, none without a controller; the life asked is the player's */
        const int in=pveh_in_suit,foot=pfoot_armed,fire=pfire_armed;const unsigned state=(unsigned)pfoot_state;const uint64_t lefts=pveh_lefts,ptick=pfoot_transition_tick;
        unsigned char *const ship=pnat_ship,*const ctl=pnat_controller;
        memset(player,0,sizeof(player));memset(&s,0,sizeof(s));memset(&d,0,sizeof(d));pveh_player_test=player;pfoot_armed=1;pfire_armed=0;
        pveh_in_suit=0;assert(!pveh_left(&s,&d,7,NULL));                                   /* not in a suit */
        pveh_in_suit=1;assert(!pveh_left(&s,&d,7,seat) && pveh_in_suit && !d.lost);        /* flying it: nothing (the suit not seen dying) */
        player[PDEATH_LIFECYCLE]=1;assert(!pveh_left(&s,&d,7,NULL) && pveh_in_suit);       /* the player goes too: a death, not this */
        player[PDEATH_LIFECYCLE]=0;pfoot_state=PFOOT_SEATED;
        assert(pveh_left(&s,&d,7,NULL) && !pveh_in_suit && d.lost && !pnat_ship && !pnat_controller && pfoot_state==PFOOT_WALKING &&
               pveh_lefts==lefts+1);
        {const char *e=pdeath_take_events();assert(e && strstr(e,"left1:1"));}
        assert(!pveh_left(&s,&d,8,NULL));                                                   /* once */
        /* ...and stands in a ship by then: that ship is the slot's, no controller (as after a stand-up in it); a suit
         * around it, a ship that is going, no ship: still none */
        {   static _Alignas(16) unsigned char home[0x200];const pilot_find_fn find=pdeath_find;void *const map=pdeath_map;const uint64_t aboards=pveh_aboards;
            uint64_t id=0x4321;memset(home,0,sizeof(home));memcpy(home+8,&id,8);
            pdeath_find=test_uics_find_home;pdeath_map=home;test_uics_home=home;pveh_around_test=test_uics_around;
            test_uics_around_ship=NULL;assert(!pveh_aboard(&s,&d) && d.lost);                           /* in no ship */
            test_uics_around_ship=home;home[PDEATH_LIFECYCLE]=1;assert(!pveh_aboard(&s,&d) && d.lost);  /* a ship that is going */
            home[PDEATH_LIFECYCLE]=0;
            assert(pveh_aboard(&s,&d) && !d.lost && s.runtime==id && s.object==home && pnat_ship==home && !pnat_controller && pveh_aboards==aboards+1);
            {const char *e=pdeath_take_events();assert(e && strstr(e,"aboard1:1"));}
            assert(!pveh_aboard(&s,&d));                                                    /* once */
            pdeath_find=find;pdeath_map=map;pveh_around_test=NULL;pveh_aboards=aboards;pveh_aboard_until=0;
        }
        pdeath_player=was;pveh_player_test=NULL;pveh_in_suit=in;pfoot_armed=foot;pfire_armed=fire;pfoot_state=state;pveh_lefts=lefts;pfoot_transition_tick=ptick;pnat_ship=ship;pnat_controller=ctl;
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
        /* the airlock of the own ship: only while the slot may leave (alive, walking, the death module with its suit) */
        {   const int darmed=pdeath_armed,dsuit=pdeath_suit,seated=pslots[2].seated,pwalking=pslots[2].walking;const unsigned count=pilot_slot_count,dead=pilot_slots[1].dead;
            const pdeath_slot was=pdeath_slots[1];
            memset(&pdeath_slots[1],0,sizeof(pdeath_slots[1]));pilot_slot_count=2;pilot_slots[1].dead=0;pslots[2].seated=1;pslots[2].walking=1;
            pdeath_armed=1;pdeath_suit=0;assert(!pvehk_may(2) && !uics_slot_airlock(2,UICS_CLASS_AIRLOCK,seat));
            pdeath_suit=1;assert(pvehk_may(2) && uics_slot_airlock(2,UICS_CLASS_AIRLOCK,seat) && !uics_slot_airlock(2,UICS_CLASS_AIRLOCK,other) &&
                                 !uics_slot_airlock(2,UICS_CLASS_CONTROL,seat) && !pvehk_may(1));
            pslots[2].walking=0;assert(!pvehk_may(2));pslots[2].walking=1;
            pdeath_slots[1].out=2;assert(!pvehk_may(2) && !pvehk_ask(2,50));
            pdeath_slots[1]=was;pdeath_armed=darmed;pdeath_suit=dsuit;pslots[2].seated=seated;pslots[2].walking=pwalking;pilot_slot_count=count;pilot_slots[1].dead=dead;
        }
        pslots[2].ship=was_ship;pfslot_armed=armed;pstk_capturing=capturing;pstk_capture_slot=slot;pfslots[2].walking=walking;
    }
    /* slot 1's seat at the apply: the action's check (this client's crosshair) is answered yes for the call through a
     * copy of the action's vtable, and the action has its own again after; not pinned, or a vtable whose two slots are
     * not the pinned functions: X4's perform as it is (the check says no here: nothing) */
    {   static _Alignas(16) unsigned char cont[0x40],act[0x20];static void *vt[1+UICS_SHADOW_SLOTS+1];void *q;const int pinned=uics_seat_pinned;
        const LONG64 forced=uics_seat_forced,plain=uics_seat_plain;void *now=NULL;
        memset(cont,0,sizeof(cont));memset(act,0,sizeof(act));memset(vt,0,sizeof(vt));
        vt[1+1]=region+UICS_CONTROL_POSSIBLE;vt[1+2]=region+UICS_CONTROL_EXECUTE;   /* compared, never called */
        q=&vt[1];memcpy(act,&q,8);q=act;memcpy(cont+0x10,&q,8);
        test_uics_seat_asked=0;uics_seat_pinned=1;
        assert(uics_perform_seat(test_uics_original_seat,cont,0,0)==1 && test_uics_seat_asked==1 && test_uics_seat_answer==1);
        memcpy(&now,act,8);assert(now==(void *)&vt[1] && !uics_shadow_action && uics_seat_forced==forced+1 && uics_seat_plain==plain);
        assert(vt[1+1]==(void *)(region+UICS_CONTROL_POSSIBLE));                       /* the class's own vtable untouched */
        /* a fault inside the call: uics_call's restore finds the copy and puts the action's own back */
        uics_shadow_vtable=(uint64_t *)&vt[1];uics_shadow_action=act;q=uics_shadow+1;memcpy(act,&q,8);
        uics_shadow_restore();memcpy(&now,act,8);assert(now==(void *)&vt[1] && !uics_shadow_action);
        uics_shadow_restore();memcpy(&now,act,8);assert(now==(void *)&vt[1]);        /* again: nothing */
        /* another function in the slot: left to X4 */
        vt[1+1]=(void *)test_uics_possible_no;
        assert(uics_perform_seat(test_uics_original_seat,cont,0,0)==0 && test_uics_seat_asked==2 && uics_seat_plain==plain+1 && uics_seat_forced==forced+1);
        vt[1+1]=region+UICS_CONTROL_POSSIBLE;uics_seat_pinned=0;
        memcpy(&now,act,8);assert(now==(void *)&vt[1]);
        uics_seat_pinned=pinned;InterlockedExchange64(&uics_seat_forced,forced);InterlockedExchange64(&uics_seat_plain,plain);
        {char out[512];assert(uics_status(out,sizeof(out))>0 && strstr(out," seat=") && strstr(out," instant="));}
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
    /* the target system's instant action: the container's kind by its own perform, the connection's name, the claim's
     * early answers (not armed: not this module's; armed outside a capture: refused) */
    {   static _Alignas(16) unsigned char cs[0x20],cm[0x20],cx[0x20];static void *vs[0x13],*vm[0x13],*vx[0x13];void *q;char name[UICS_NAME_MAX+1];
        static char longname[UICS_NAME_MAX+8];uint64_t frame[12]={0},ret=7;const int armed=uics_armed;const unsigned was=uics_action_index;
        vs[UICS_CONTAINER_PERFORM/8]=region+UICS_SINGLE_PERFORM;vm[UICS_CONTAINER_PERFORM/8]=region+UICS_MULTI_PERFORM;vx[UICS_CONTAINER_PERFORM/8]=region+1;
        q=vs;memcpy(cs,&q,8);q=vm;memcpy(cm,&q,8);q=vx;memcpy(cx,&q,8);
        assert(uics_container_kind(cs)==0 && uics_container_kind(cm)==1 && uics_container_kind(cx)==-1 && uics_container_kind(NULL)==-1);
        assert(uics_name("con_pilot",name)==9 && !strcmp(name,"con_pilot") && !uics_name("",name) && !uics_name(NULL,name));
        memset(longname,'a',sizeof(longname)-1);assert(!uics_name(longname,name));
        uics_armed=0;uics_action_index=5;assert(!uics_action_decide(5,frame,&ret) && !ret);
        uics_armed=1;assert(!uics_action_decide(6,frame,&ret));                      /* another entry's call */
        {const LONG64 refused=uics_refused,instant=uics_instant;ret=7;(void)uics_action_decide(5,frame,&ret);   /* no lua_State, no capture: refused */
         assert(!ret && uics_refused==refused+1 && uics_instant==instant+1);InterlockedExchange64(&uics_refused,refused);InterlockedExchange64(&uics_instant,instant);}
        uics_armed=armed;uics_action_index=was;
    }
    /* the transporter room's terminal: only a walking slot 2..N at a terminal of its own ship; the menu's spacesuit
     * target (a zone) is refused outside a capture and for another entry is not this module's */
    {   static unsigned char ship[0x80],room[0x80],other[0x80];unsigned char *p;uint64_t frame[12]={0},ret=7;
        const int armed=uics_armed,farmed=pfslot_armed,fwalking=pfslots[2].walking,capturing=pstk_capturing;const unsigned slot=pstk_capture_slot,was=uics_transport_index;
        unsigned char *const sship=pslots[2].ship;
        memset(ship,0,sizeof(ship));memset(room,0,sizeof(room));memset(other,0,sizeof(other));p=ship;memcpy(room+0x70,&p,8);
        pslots[2].ship=ship;pfslot_armed=1;pfslots[2].walking=1;pstk_capturing=1;pstk_capture_slot=2;
        assert(uics_slot_terminal(2,UICS_CLASS_TRANSPORTER,room) && uics_slot_terminal(2,UICS_CLASS_TRANSPORTER,ship) &&
               !uics_slot_terminal(2,UICS_CLASS_TRANSPORTER,other) && !uics_slot_terminal(2,UICS_CLASS_CONTROL,room) &&
               uics_slot_terminal(1,UICS_CLASS_TRANSPORTER,room) && uics_slot_terminal(1,UICS_CLASS_TRANSPORTER,other) &&   /* slot 1: X4's own tests */
               !uics_slot_terminal(1,UICS_CLASS_CONTROL,room));
        /* slot 1's command is its own class: slot 1 only, never the action's class as an event */
        assert(uics_verdict(1,UICS_CLASS_TRANSPORT) && uics_verdict(2,UICS_CLASS_TRANSPORT) && !uics_verdict(1,UICS_CLASS_TRANSPORTER) &&
               !uics_verdict(2,UICS_CLASS_TRANSPORTER));
        pfslots[2].walking=0;assert(!uics_slot_terminal(2,UICS_CLASS_TRANSPORTER,room));
        pslots[2].ship=sship;pfslot_armed=farmed;pfslots[2].walking=fwalking;pstk_capturing=capturing;pstk_capture_slot=slot;
        uics_armed=0;uics_transport_index=9;assert(!uics_transport_decide(9,frame,&ret) && !ret);
        uics_armed=1;assert(!uics_transport_decide(8,frame,&ret));                   /* another entry's call */
        {const LONG64 refused=uics_refused;ret=7;(void)uics_transport_decide(9,frame,&ret);   /* no target, no capture: refused */
         assert(!ret && uics_refused==refused+1 && !uics_transports);InterlockedExchange64(&uics_refused,refused);}
        assert(!uics_is_zone(NULL));
        uics_armed=armed;uics_transport_index=was;
    }
    {char out[320];assert(uics_status(out,sizeof(out)) && strstr(out," compslot=") && strstr(out," last=") && strstr(out," seat=") && strstr(out," instant=") && strstr(out," terminal="));}
    uics_perform_test[0]=uics_perform_test[1]=NULL;uics_export_test=NULL;uics_seen=uics_expected=0;uics_ran=0;
    InterlockedExchange64(&uics_passed,passed);pnat_base=saved_base;
    puts("ui compslot: the action's class from a single and a multi container, peek and apply through the perform hooks, the event's shape, the click and the own seat of slots 2..N PASS");
}
