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
    pvehk_forward_lookup_test=NULL;pvehk_forward_owner_test=NULL;pvehk_forward_on=on;pvehk_events_more=more;
    memcpy(image+SCRIPT_RNG_CUE_VT+0x20,&slot_was,8);pslots[2]=kept;pnat_base=base;pslot_p1=first;pstk_scope_slot=scope_was;
    InterlockedExchange64(&pvehk_delivered_inside,inside);InterlockedExchange64(&pvehk_delivered_unseated,unseated);
    printf("pilot deliver scope answers and forward tests PASS\n");
}
