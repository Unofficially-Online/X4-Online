/* An animation's waiting listeners are served by the simulation only (lockstep_anim_listeners.h): which calls go to
 * X4's function and which are held, and the counters. No native code runs; the scope is given by the test. */
static unsigned test_animl_served,test_animl_scope;
static void *test_animl_seen_controller,*test_animl_seen_state;
static void test_animl_original(void *controller,void *state) {++test_animl_served;test_animl_seen_controller=controller;test_animl_seen_state=state;}
static unsigned test_animl_scope_of(void) {return test_animl_scope;}
static void test_anim_listeners(void) {
    static unsigned char controller[0x60];static void *listeners[2];uint64_t state[2]={0x245a3f0c292fea37ULL,0};
    const uint64_t saved_issued=stats.issued;
    animl_calls=animl_waiting=0;memset((void*)animl_held,0,sizeof(animl_held));animl_last_tick=animl_last_state=0;animl_last_scope=0;
    animl_scope_test=test_animl_scope_of;test_animl_served=0;stats.issued=77;
    /* Nobody waits: X4's function, whatever part of the frame the call comes from; the scope is not even asked. */
    for(unsigned scope=VIEW_UI_NONE;scope<VIEW_UI_SCOPES;scope++) {
        test_animl_scope=scope;assert(animl_serve_impl(test_animl_original,controller,state)==1);
    }
    assert(test_animl_served==VIEW_UI_SCOPES && test_animl_seen_controller==controller && test_animl_seen_state==state);
    assert(animl_calls==VIEW_UI_SCOPES && !animl_waiting);
    /* Listeners wait and the call is the simulation's: served. */
    {void *begin=listeners,*end=listeners+2;memcpy(controller+ANIML_WAIT_BEGIN,&begin,8);memcpy(controller+ANIML_WAIT_END,&end,8);}
    test_animl_scope=VIEW_UI_NONE;test_animl_served=0;
    assert(animl_serve_impl(test_animl_original,controller,state)==1 && test_animl_served==1 && animl_waiting==1);
    /* Listeners wait and the call comes from one of the client's own parts of the frame: held, counted by its part,
     * the last one named with its tick and the state. */
    for(unsigned scope=VIEW_UI_PRESENT;scope<VIEW_UI_SCOPES;scope++) {
        test_animl_scope=scope;assert(animl_serve_impl(test_animl_original,controller,state)==0 && animl_held[scope]==1);
    }
    assert(test_animl_served==1 && animl_waiting==1+(VIEW_UI_SCOPES-VIEW_UI_PRESENT));
    assert(animl_last_tick==77 && animl_last_scope==VIEW_UI_SCOPES-1 && animl_last_state==state[0]);
    /* A value the scope answer cannot be: X4's function. No controller: X4's function. */
    test_animl_scope=VIEW_UI_SCOPES;assert(animl_serve_impl(test_animl_original,controller,state)==1 && test_animl_served==2);
    test_animl_scope=VIEW_UI_RENDER;assert(animl_serve_impl(test_animl_original,NULL,state)==1 && test_animl_served==3);
    /* The wrapper with the test's function in X4's place; the status. */
    animl_original_test=test_animl_original;test_animl_scope=VIEW_UI_NONE;animl_serve(controller,state);assert(test_animl_served==4);
    test_animl_scope=VIEW_UI_RENDER;animl_serve(controller,state);assert(test_animl_served==4 && animl_held[VIEW_UI_RENDER]==2);
    char status[200];assert(animl_status(status,sizeof(status))>0);
    assert(!strncmp(status,"installed=0 why=- calls=",24) && strstr(status," held=1/1/1/2/1/1 last=77:4:245a3f0c292fea37"));
    /* The entry's five bytes lie in one 16-byte block (the atomic patch writes no more), the stubs in their slots. */
    assert(ANIML_SITE>ANIML_PIN && ANIML_SITE+5<=ANIML_PIN+ANIML_PIN_SIZE && ((ANIML_TARGET+4)&~15u)==(ANIML_TARGET&~15u));
    assert(ANIML_TRAMPOLINE==ANIML_STUB+0x20 && ANIML_TRAMPOLINE+sizeof(animl_head)+14<=0x900 && sizeof(animl_head)==5);
    animl_original_test=NULL;animl_scope_test=NULL;stats.issued=saved_issued;
    animl_calls=animl_waiting=0;memset((void*)animl_held,0,sizeof(animl_held));animl_last_tick=animl_last_state=0;animl_last_scope=0;
    printf("animation listeners: served by the simulation, held in a client's own parts of the frame; tests PASS\n");
}
