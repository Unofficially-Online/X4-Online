/* A conversation's owner slot (lockstep_conversation_owner.h) on stand-ins: taking (idle and free only), who may answer,
 * the release (idle again after it left idle, or the grace when the start never took), the dispatch wrapper (no owner
 * and slot 1: X4's own; an owner inside its own scope: the generation opened and closed; an owner without a scope to
 * enter: X4's own, counted), the four vtable slots swapped on a private region and put back, the join state. On the
 * pinned image: the four vtables' type and dispatch entries. */
static int tcvo_state_value=1,tcvo_current_value;
static int tcvo_state(int *state,int *current) {*state=tcvo_state_value;*current=tcvo_current_value;return 1;}
static unsigned tcvo_calls,tcvo_generating_seen,tcvo_scope_seen;
static uint64_t tcvo_original(void *e,void *l,void *s,double t) {
    (void)e;(void)l;(void)s;++tcvo_calls;tcvo_generating_seen=mpp_generating;tcvo_scope_seen=pstk_scope_slot;return (uint64_t)t+7u;
}
static unsigned tcvo_dialogs;static double tcvo_dialog_time;
static void tcvo_dialog(void *l,void *s,double t) {(void)l;(void)s;++tcvo_dialogs;tcvo_dialog_time=t;}
static void test_conversation_owner(void) {
    const int armed=cvo_armed,installed=cvo_installed,mpp=mpp_armed;const unsigned owner=cvo_owner,scope=pstk_scope_slot,generating=mpp_generating;
    int (*const state)(int *,int *)=cvo_state;cvo_dispatch_fn originals[CVO_EVENTS];memcpy(originals,cvo_original,sizeof(originals));
    cvo_state=tcvo_state;cvo_owner=0;cvo_owner_left_idle=0;cvo_installed=0;
    cvo_taken=cvo_busy=cvo_released=cvo_expired=cvo_dispatches=cvo_scoped=cvo_inside=cvo_unscoped=cvo_leave_failed=cvo_refused=0;
    /* not armed: slot 1 alone may, nothing is taken */
    cvo_armed=0;assert(cvo_may(1) && !cvo_may(2) && !cvo_take(2,10) && !cvo_frame(10) && !cvo_refused);
    cvo_armed=1;tcvo_state_value=1;tcvo_current_value=0;
    /* nobody owns it: slot 1 as before, the others not */
    assert(cvo_may(1) && !cvo_may(2) && cvo_refused==1);
    /* a take: idle and free only */
    assert(!cvo_take(0,100) && !cvo_take(PILOT_SLOTS+1,100));
    assert(cvo_take(2,100) && cvo_owner==2 && cvo_taken==1 && cvo_may(2) && !cvo_may(1) && !cvo_may(3));
    assert(!cvo_take(3,101) && !cvo_take(2,101) && cvo_busy==2);                       /* somebody owns it */
    /* the start takes: the manager leaves idle, the owner stays; idle again: released */
    assert(cvo_frame(101)==2);tcvo_state_value=4;tcvo_current_value=1;
    assert(cvo_frame(102)==2 && cvo_owner_left_idle && cvo_frame(500)==2);              /* a long conversation stays its owner's */
    tcvo_state_value=1;tcvo_current_value=0;assert(cvo_frame(501)==0 && !cvo_owner && cvo_released==1);
    /* a conversation of nobody's (MD started it): no take while the manager is busy */
    tcvo_state_value=5;tcvo_current_value=1;assert(!cvo_take(2,600) && cvo_busy==3 && cvo_may(1) && !cvo_may(2));
    tcvo_state_value=1;tcvo_current_value=1;assert(!cvo_take(2,601));                    /* state idle but a conversation held */
    tcvo_current_value=0;
    /* a start that never took: the grace */
    assert(cvo_take(3,700) && cvo_frame(700+CVO_GRACE)==3 && cvo_frame(701+CVO_GRACE)==0 && cvo_expired==1);
    assert(cvo_take(1,900) && cvo_may(1) && !cvo_may(2));                                /* slot 1 takes too */
    /* the join state */
    {char text[64];int n=cvo_join_state(text,sizeof(text));assert(n>0 && !strcmp(text,"v1|1:900:0"));
     cvo_owner=0;assert(cvo_join_restore(text,(size_t)n) && cvo_owner==1 && cvo_owner_tick==900 && !cvo_owner_left_idle);
     assert(cvo_join_restore("v1|2:1234:1",11) && cvo_owner==2 && cvo_owner_tick==1234 && cvo_owner_left_idle==1);
     assert(!cvo_join_restore("v2|2:1:1",8) && !cvo_join_restore("v1|9:1:0",8) && !cvo_join_restore("v1|2:1:2",8) && !cvo_join_restore("v1|2:1:0x",9) &&
            !cvo_join_restore("v1|",3) && cvo_owner==2);
     assert(cvo_join_restore("v1|0:0:0",8) && !cvo_owner);}
    /* the dispatch */
    for(unsigned i=0;i<CVO_EVENTS;i++) cvo_original[i]=tcvo_original;
    tcvo_calls=0;pstk_scope_slot=0;mpp_generating=0;mpp_armed=1;
    assert(cvo_wrappers[0]((void*)1,(void*)2,(void*)3,5.0)==12 && tcvo_calls==1 && !tcvo_generating_seen && !cvo_scoped && !cvo_inside && !cvo_unscoped);   /* nobody's */
    cvo_owner=1;assert(cvo_wrappers[1]((void*)1,(void*)2,(void*)3,6.0)==13 && tcvo_calls==2 && !tcvo_generating_seen && !cvo_unscoped);                     /* slot 1's */
    cvo_owner=2;pstk_scope_slot=2;                                                       /* slot 2's, inside its scope: the generation, opened and closed */
    assert(cvo_wrappers[2]((void*)1,(void*)2,(void*)3,7.0)==14 && tcvo_calls==3 && tcvo_generating_seen==2 && tcvo_scope_seen==2 && cvo_inside==1 && !mpp_generating);
    mpp_generating=2;assert(cvo_wrappers[3]((void*)1,(void*)2,(void*)3,8.0)==15 && tcvo_generating_seen==2 && mpp_generating==2 && cvo_inside==2);   /* open already: left open */
    mpp_generating=0;pstk_scope_slot=3;                                                  /* inside another slot's scope: as it is */
    assert(cvo_wrappers[0]((void*)1,(void*)2,(void*)3,9.0)==16 && !tcvo_generating_seen && tcvo_scope_seen==3 && cvo_unscoped==1);
    {unsigned char *const player=pslots[2].player;pslots[2].player=NULL;pstk_scope_slot=0;    /* no scope to enter (no live slot here): X4's own, counted */
     assert(cvo_wrappers[1]((void*)1,(void*)2,(void*)3,10.0)==17 && tcvo_calls==6 && cvo_unscoped==2 && !cvo_scoped && !pstk_scope_slot);
     pslots[2].player=player;}
    assert(cvo_dispatches==6 && !cvo_leave_failed);
    /* the four slots on a private region */
    {   static unsigned char *region;if(!region) region=VirtualAlloc(NULL,0x2bb0000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        assert(region);
        for(unsigned i=0;i<CVO_EVENTS;i++) {
            uintptr_t slots[3]={0,(uintptr_t)region+cvo_events[i].type,(uintptr_t)region+cvo_events[i].dispatch};memcpy(region+cvo_events[i].vtable,slots,sizeof(slots));
        }
        assert(cvo_pins(region,(uintptr_t)region) && !cvo_intact_at(region));
        cvo_installed=0;assert(cvo_install_at(region) && cvo_installed && cvo_intact_at(region));
        for(unsigned i=0;i<CVO_EVENTS;i++) assert(*cvo_slot(region,i)==(void*)cvo_wrappers[i] && (uintptr_t)cvo_original[i]==(uintptr_t)region+cvo_events[i].dispatch);
        {void **slot=cvo_slot(region,2);void *keep=*slot;*slot=(void*)tcvo_original;assert(!cvo_intact_at(region));*slot=keep;}
        cvo_owner=2;cvo_restore_at(region);assert(!cvo_installed && !cvo_owner && cvo_pins(region,(uintptr_t)region));
        {uintptr_t bad=1;memcpy(region+cvo_events[3].vtable+8,&bad,8);assert(!cvo_pins(region,(uintptr_t)region));}
    }
    {char text[512];cvo_owner=2;cvo_owner_tick=77;assert(cvo_status(text,sizeof(text))>0 && strstr(text,"armed=1") && strstr(text,"owner=2 since=77") && strstr(text,"dispatches=6"));}
    /* the event check the capture (gate.ui_missions_push) and the apply ask: slot 2's start passes with owners armed, not
     * without (the first live run, crew --conversations of 19 Nov, was refused at the push: it asked for slot 1 alone);
     * the answer to a ship's question stays slot 1's either way */
    {const unsigned families=uirep_families;const char *why="-";uirep_families|=UIREP_FAMILY_CONVERSATION;
     const unsigned char start[2]={UIMIS_C_START,0},answer[11]={UIMIS_C_ANSWER,1,3,0,0,0,0,0,0,0xf0,0x3f};
     cvo_armed=1;assert(uimis_event_ok(2,UIMIS_OP_CONVERSATION,start,2,&why) && uimis_event_ok(1,UIMIS_OP_CONVERSATION,answer,11,&why));
     assert(!uimis_event_ok(2,UIMIS_OP_CONVERSATION,answer,11,&why));
     cvo_armed=0;assert(!uimis_event_ok(2,UIMIS_OP_CONVERSATION,start,2,&why) && uimis_event_ok(1,UIMIS_OP_CONVERSATION,start,2,&why));
     uirep_families=families;}
    /* the owner's screen: the dialog and the conversation branch's menu are held back on a client of another slot */
    {const unsigned own=stats.slot;cvo_dialog_fn dialog=cvo_dialog_original;
     cvo_dialog_original=tcvo_dialog;tcvo_dialogs=0;cvo_dialogs_shown=cvo_dialogs_hidden=0;cvo_armed=1;
     stats.slot=1;cvo_owner=0;assert(!cvo_foreign());cvo_dialog_start(NULL,NULL,1.5);assert(tcvo_dialogs==1 && tcvo_dialog_time==1.5);   /* nobody's: shown */
     cvo_owner=1;assert(!cvo_foreign());cvo_dialog_start(NULL,NULL,2.0);assert(tcvo_dialogs==2);                                        /* its own */
     cvo_owner=2;assert(cvo_foreign());cvo_dialog_start(NULL,NULL,3.0);assert(tcvo_dialogs==2 && cvo_dialogs_hidden==1 && cvo_dialogs_shown==2);
     assert(cvo_menu_hide((uintptr_t)image+CVO_MENU_RET) && !cvo_menu_hide((uintptr_t)image+CVO_MENU_RET+1) && !cvo_menu_hide((uintptr_t)image+0xbf4600u));
     stats.slot=2;assert(!cvo_foreign() && !cvo_menu_hide((uintptr_t)image+CVO_MENU_RET));cvo_dialog_start(NULL,NULL,4.0);assert(tcvo_dialogs==3);
     cvo_armed=0;stats.slot=1;assert(!cvo_foreign() && !cvo_menu_hide((uintptr_t)image+CVO_MENU_RET));                                    /* not armed: as X4 */
     stats.slot=own;cvo_dialog_original=dialog;cvo_owner=0;cvo_dialogs_shown=cvo_dialogs_hidden=0;}
    /* the screen pins on a private region: the call site, the holder's reference, the dialog listener's slot */
    {static unsigned char *region;if(!region) region=VirtualAlloc(NULL,0x3900000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
     assert(region);memset(region+0xbf4540u,0xcc,0x40);
     const unsigned char mov[3]={0x48,0x8b,0x0d},call[6]={0xff,0x90,0xd8,0x01,0x00,0x00};int32_t rel=(int32_t)(CVO_MENU_HOLDER-0xbf4556u);
     memcpy(region+0xbf454fu,mov,3);memcpy(region+0xbf4552u,&rel,4);memcpy(region+CVO_MENU_CALL,call,6);
     uintptr_t fn=(uintptr_t)region+CVO_DIALOG_FN;memcpy(region+CVO_DIALOG_VTABLE+8u*CVO_DIALOG_SLOT,&fn,8);
     assert(cvo_screen_pins(region,(uintptr_t)region));
     region[CVO_MENU_CALL+2]^=1;assert(!cvo_screen_pins(region,(uintptr_t)region));region[CVO_MENU_CALL+2]^=1;
     rel+=8;memcpy(region+0xbf4552u,&rel,4);assert(!cvo_screen_pins(region,(uintptr_t)region));rel-=8;memcpy(region+0xbf4552u,&rel,4);
     fn+=16;memcpy(region+CVO_DIALOG_VTABLE+8u*CVO_DIALOG_SLOT,&fn,8);assert(!cvo_screen_pins(region,(uintptr_t)region));
     fn-=16;memcpy(region+CVO_DIALOG_VTABLE+8u*CVO_DIALOG_SLOT,&fn,8);assert(cvo_screen_pins(region,(uintptr_t)region));}
    cvo_armed=armed;cvo_installed=installed;cvo_owner=owner;cvo_owner_left_idle=0;cvo_state=state;memcpy(cvo_original,originals,sizeof(originals));
    pstk_scope_slot=scope;mpp_generating=generating;mpp_armed=mpp;
    cvo_taken=cvo_busy=cvo_released=cvo_expired=cvo_dispatches=cvo_scoped=cvo_inside=cvo_unscoped=cvo_leave_failed=cvo_refused=0;
    printf("conversation owner: take, may, release, grace, join state, the dispatch (nobody, slot 1, inside, another scope, no scope), the four slots PASS\n");
}
static void test_conversation_owner_native_image(const char *path) {
    unsigned char *mapped=test_pmath_map(path);
    assert(cvo_pins(mapped,(uintptr_t)mapped));
    {unsigned char copy[8];memcpy(copy,mapped+cvo_events[0].vtable+16,8);mapped[cvo_events[0].vtable+16]^=1;assert(!cvo_pins(mapped,(uintptr_t)mapped));
     memcpy(mapped+cvo_events[0].vtable+16,copy,8);}
    assert(cvo_pins(mapped,(uintptr_t)mapped));
    assert(cvo_screen_pins(mapped,(uintptr_t)mapped));   /* the conversation branch's menu call, the holder, the dialog listener's slot */
    {unsigned char keep=mapped[CVO_MENU_CALL+2];mapped[CVO_MENU_CALL+2]^=1;assert(!cvo_screen_pins(mapped,(uintptr_t)mapped));mapped[CVO_MENU_CALL+2]=keep;}
    printf("pinned native image conversation owner verified (the four conversation events' vtables: type and dispatch; the owner's screen: the dialog listener's slot, the menu call)\n");
}
