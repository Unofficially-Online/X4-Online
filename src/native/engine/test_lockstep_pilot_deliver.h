/* A due event of a slot's player (lockstep_pilot_death.h): what the delivery answers for the scope, and that an event
 * delivered while the slot's scope is open already is forwarded to the cues nobody owns like any other. */
static unsigned test_pdl_calls;static void *test_pdl_sources[4];
static unsigned char test_pdl_entry[0x18];
static unsigned test_pdl_type(void *event) {(void)event;return 77u;}
static void test_pdl_listener(void *cue,void *source,double now,unsigned type,void *event) {
    (void)cue;(void)now;(void)event;assert(type==77u);
    if(test_pdl_calls<4) test_pdl_sources[test_pdl_calls]=source;
    ++test_pdl_calls;
}
static unsigned char *test_pdl_lookup(void *source,unsigned type) {(void)source;assert(type==77u);return test_pdl_entry;}
static unsigned test_pdl_owner(const unsigned char *cue) {(void)cue;return 0;}
/* The listeners of player 1's object and of the slot's player, as the due pass asks for them. */
static unsigned char *test_pdl_first,*test_pdl_first_entry,*test_pdl_own_entry;
static unsigned char *test_pdl_lookup_due(void *source,unsigned type) {(void)type;return source==test_pdl_first?test_pdl_first_entry:test_pdl_own_entry;}
static void test_pilot_deliver(void) {
    static unsigned char p1[0x40],p2[0x40],other[0x40];static void *event_vt[2],*event[4],*cue[2],*listeners[1];
    const pslot_slot kept=pslots[2];unsigned char *const base=pnat_base,*const first=pslot_p1;const unsigned scope_was=pstk_scope_slot;
    const int more=pvehk_events_more,on=pvehk_forward_on;void *slot_was=NULL;memcpy(&slot_was,image+SCRIPT_RNG_CUE_VT+0x20,8);
    const LONG64 inside=pvehk_delivered_inside,unseated=pvehk_delivered_unseated;
    pnat_base=image;pslot_p1=p1;memset(&pslots[2],0,sizeof(pslots[2]));pslots[2].player=p2;pvehk_events_more=1;pstk_scope_slot=0;
    /* Nobody's player; a slot that is not in its seat: no scope, nothing forwarded. */
    assert(!pvehk_deliver_enter(other) && !pvehk_deliver_enter(NULL) && pvehk_delivered_unseated==unseated);
    assert(!pvehk_deliver_enter(p2) && pvehk_delivered_unseated==unseated+1);
    /* Its scope is open already: minus the slot, and there is nothing to leave. */
    pslots[2].seated=1;pstk_scope_slot=2;
    assert(pvehk_deliver_enter(p2)==-2 && pvehk_delivered_inside==inside+1);
    pvehk_deliver_leave(-2);assert(pstk_scope_slot==2);
    /* Forwarded with that answer: the cue nobody owns is called for player 1's object and for the slot's player. */
    event_vt[1]=(void*)test_pdl_type;event[0]=event_vt;
    cue[0]=image+SCRIPT_RNG_CUE_VT;{void *fn=(void*)test_pdl_listener;memcpy(image+SCRIPT_RNG_CUE_VT+0x20,&fn,8);}
    listeners[0]=cue;{void *begin=listeners,*end=listeners+1;memcpy(test_pdl_entry+8,&begin,8);memcpy(test_pdl_entry+0x10,&end,8);}
    pvehk_forward_lookup_test=test_pdl_lookup;pvehk_forward_owner_test=test_pdl_owner;pvehk_forward_on=1;test_pdl_calls=0;
    pvehk_deliver_forward(-2,event,1.0);
    assert(test_pdl_calls==2 && test_pdl_sources[0]==p1 && test_pdl_sources[1]==p2);
    pvehk_deliver_forward(2,event,1.0);assert(test_pdl_calls==4);
    pvehk_deliver_forward(0,event,1.0);pvehk_deliver_forward(-1,event,1.0);assert(test_pdl_calls==4);
    /* A UI event (by its class) is handed to the cue once, as the slot's player's. */
    {
        static void *ui_event[4];void *ui_vt=image+PVEHK_FWD_UI_EVENT,*type_was=NULL;memcpy(&type_was,image+PVEHK_FWD_UI_EVENT+8,8);
        {void *fn=(void*)test_pdl_type;memcpy(image+PVEHK_FWD_UI_EVENT+8,&fn,8);}
        ui_event[0]=ui_vt;test_pdl_calls=0;pvehk_deliver_forward(2,ui_event,1.0);
        assert(test_pdl_calls==1 && test_pdl_sources[0]==p2);
        memcpy(image+PVEHK_FWD_UI_EVENT+8,&type_was,8);test_pdl_calls=4;
    }
    /* The due pass's question about a slot's player: nobody on it listens and player 1's object has listeners of
     * the type: an entry that is not empty, with nothing behind it; counted. Everything else is X4's answer. */
    {
        static unsigned char own[0x30],empty[0x30];const LONG64 batched=pvehk_due_batched;void *a=own,*b=own+8,*behind=(void*)0x1234;
        memset(own,0,sizeof(own));memcpy(own+8,&a,8);memcpy(own+0x10,&b,8);          /* one listener */
        memset(empty,0,sizeof(empty));memcpy(empty+0x20,&behind,8);                   /* none, something behind */
        pvehk_forward_lookup_test=test_pdl_lookup_due;test_pdl_first=p1;test_pdl_first_entry=test_pdl_entry;test_pdl_own_entry=NULL;
        unsigned char *got=pvehk_due_lookup(p2,77u,0);
        assert(got==pvehk_due_fake && pvehk_due_batched==batched+1 && !pvehk_due_none(got) && !*(void**)(got+0x20));
        test_pdl_own_entry=empty;got=pvehk_due_lookup(p2,77u,0);
        assert(got==pvehk_due_fake && pvehk_due_batched==batched+2 && *(void**)(got+0x20)==behind);
        test_pdl_own_entry=own;assert(pvehk_due_lookup(p2,77u,0)==own && pvehk_due_batched==batched+2);        /* it listens itself */
        test_pdl_own_entry=NULL;
        test_pdl_first_entry=NULL;assert(!pvehk_due_lookup(p2,77u,0));test_pdl_first_entry=empty;assert(!pvehk_due_lookup(p2,77u,0)); /* nobody to hear it */
        test_pdl_first_entry=test_pdl_entry;
        pslots[2].seated=0;assert(!pvehk_due_lookup(p2,77u,0));pslots[2].seated=1;                                  /* not in its seat */
        assert(!pvehk_due_lookup(other,77u,0) && pvehk_due_lookup(p1,77u,0)==test_pdl_entry);                       /* not a slot's player */
        pvehk_forward_on=0;assert(!pvehk_due_lookup(p2,77u,0));pvehk_forward_on=1;
        assert(pvehk_due_batched==batched+2);InterlockedExchange64(&pvehk_due_batched,batched);
    }
    pvehk_forward_lookup_test=NULL;pvehk_forward_owner_test=NULL;pvehk_forward_on=on;pvehk_events_more=more;
    memcpy(image+SCRIPT_RNG_CUE_VT+0x20,&slot_was,8);pslots[2]=kept;pnat_base=base;pslot_p1=first;pstk_scope_slot=scope_was;
    InterlockedExchange64(&pvehk_delivered_inside,inside);InterlockedExchange64(&pvehk_delivered_unseated,unseated);
    printf("pilot deliver scope answers, forward and due lookup tests PASS\n");
}
