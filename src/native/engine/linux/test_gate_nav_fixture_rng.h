/* Fixture NAV_UPDT parity: injected Move only; never executes X4 code. */
static uint64_t nav_fixture_test_seen;
static uint64_t *nav_fixture_test_rng;
static unsigned nav_fixture_test_calls;
static uint64_t nav_fixture_test_outer, nav_fixture_test_nested, nav_fixture_test_after_nested;
static void nav_fixture_test_no_draw(void *controller, double time) {
    (void)controller;
    (void)time;
    nav_fixture_test_seen = *nav_fixture_test_rng;
}
static void nav_fixture_test_native(void *self,double time) {
    (void)self;(void)time;
    ++nav_fixture_test_calls;
    if(nav_fixture_test_rng) {nav_fixture_test_seen=*nav_fixture_test_rng;*nav_fixture_test_rng^=0x55;}
}
static void nav_fixture_test_outer_call(void *controller, double time) {
    nav_fixture_test_outer = *nav_fixture_test_rng;
    nav_fixture_execute(nav_fixture_test_no_draw, controller, time, 2, nav_fixture_test_rng);
    nav_fixture_test_nested = nav_fixture_test_seen;
    nav_fixture_test_after_nested = *nav_fixture_test_rng;
}
static void test_nav_fixture_rng(void) {
    unsigned char ships[2][0x210]={{0}},controllers[2][0x40]={{0}};
    uint64_t ids[2]={0x1234,0x9876};
    lxb_actor saved[LXB_ACTORS];memcpy(saved,lxb_actors,sizeof(saved));
    unsigned saved_bound=lxb_bound,saved_configured=lxb_configured;
    for(unsigned i=0;i<2;i++) {
        void *ship=ships[i],*controller=controllers[i];
        memcpy(ships[i]+8,&ids[i],8);
        memcpy(ships[i]+LXW_OBJ_CONTROLLER,&controller,8);
        memcpy(controllers[i]+LXW_NAV_OWNER,&ship,8);
        lxb_actors[i].object=ship;lxb_actors[i].runtime=ids[i];
    }
    lxb_bound=LXB_ACTORS;lxb_configured=1;
    assert(lxb_nav_fixture_ready() && lxb_nav_fixture(controllers[0])==1 && lxb_nav_fixture(controllers[1])==2);
    {void *wrong=controllers[1];memcpy(ships[0]+LXW_OBJ_CONTROLLER,&wrong,8);assert(lxb_nav_fixture(controllers[0])==-1);
     void *right=controllers[0];memcpy(ships[0]+LXW_OBJ_CONTROLLER,&right,8);}
    {uint64_t wrong=1;memcpy(ships[0]+8,&wrong,8);assert(lxb_nav_fixture(controllers[0])==-1);memcpy(ships[0]+8,&ids[0],8);}
    {void *foreign=ships[1]+32;memcpy(controllers[0]+LXW_NAV_OWNER,&foreign,8);assert(lxb_nav_fixture(controllers[0])==-1);
     void *right=ships[0];memcpy(controllers[0]+LXW_NAV_OWNER,&right,8);}
    reset_session();stats.active=1;stats.issued=1;boot_mode=1;boot_phase=3;ui_thread=GetCurrentThreadId();
    memset(nav_fixture_actors,0,sizeof(nav_fixture_actors));nav_fixture_keyed=nav_fixture_bad_time=nav_fixture_missing_rng=nav_fixture_bad_binding=0;nav_cadence_failures=0;
    uint64_t rng=0xfedcba9876543210ULL;nav_fixture_test_rng=&rng;nav_fixture_test_calls=0;
    nav_fixture_execute(nav_fixture_test_native,controllers[0],1.0,1,&rng);
    assert(nav_fixture_test_seen==0x77923eff96f5da13ULL && rng==0xfedcba9876543210ULL);
    nav_fixture_execute(nav_fixture_test_native,controllers[0],1.0,1,&rng);
    assert(nav_fixture_test_seen==0x600298835654401eULL && rng==0xfedcba9876543210ULL);
    nav_fixture_execute(nav_fixture_test_native,controllers[1],1.0,2,&rng);
    assert(nav_fixture_test_seen==0xceb720e5986bcf76ULL && rng==0xfedcba9876543210ULL);
    stats.issued=2;nav_fixture_execute(nav_fixture_test_native,controllers[0],2.0,1,&rng);
    assert(nav_fixture_test_seen==0x47f7b687ffc3830dULL && rng==0xfedcba9876543210ULL);
    stats.issued=60;nav_fixture_execute(nav_fixture_test_native,controllers[0],3.0,12,&rng);
    assert(nav_fixture_test_seen==0x9eea74e7a1be0a21ULL && rng==0xfedcba9876543210ULL);
    uint64_t calls_before=nav_fixture_actors[0].calls;
    nav_fixture_execute(nav_fixture_test_native,controllers[0],NAN,1,&rng);
    assert(nav_fixture_bad_time==1 && nav_cadence_failures==1 && nav_fixture_actors[0].calls==calls_before);
    nav_fixture_test_rng=NULL;nav_fixture_execute(nav_fixture_test_native,controllers[0],3.0,1,NULL);
    assert(nav_fixture_missing_rng==1 && nav_cadence_failures==2 && nav_fixture_actors[0].tick==60 && nav_fixture_actors[0].calls==1);
    assert(nav_fixture_test_calls==7 && nav_fixture_keyed==5);
    /* Fixture scope must override nested ship/agent scopes, restore the outer
     * keyed word after a nested fixture call, and count a call with no draw. */
    memset(nav_fixture_actors, 0, sizeof(nav_fixture_actors));
    stats.issued = 1;
    rng = 0xfedcba9876543210ULL;
    nav_fixture_test_rng = &rng;
    actor_all_ship_depth = 2;
    actor_all_agent_depth = 3;
    nav_fixture_execute(nav_fixture_test_outer_call, controllers[0], 1.0, 1, &rng);
    assert(nav_fixture_test_outer == 0x77923eff96f5da13ULL);
    assert(nav_fixture_test_nested == 0xceb720e5986bcf76ULL);
    assert(nav_fixture_test_after_nested == nav_fixture_test_outer && rng == 0xfedcba9876543210ULL);
    assert(actor_all_ship_depth == 2 && actor_all_agent_depth == 3);
    nav_fixture_execute(nav_fixture_test_no_draw, controllers[0], 1.0, 1, &rng);
    assert(nav_fixture_test_seen == 0x600298835654401eULL && rng == 0xfedcba9876543210ULL);
    actor_all_ship_depth = 0;
    actor_all_agent_depth = 0;
    /* The vtable wrapper keys only a verified fixture on the admitted UI
     * thread; nonfixtures, other threads and load phases keep their word. */
    nav_fixture_native_override=nav_fixture_test_native;
    uint64_t *tls_rng=gate_tls_rng(gate_tls_self());nav_fixture_test_rng=tls_rng;
    nav_cadence_active=1;nav_fixture_rng_armed=1;nav_probe_armed=nav_params_armed=0;lxk_armed=0;
    stats.issued=1;memset(nav_fixture_actors,0,sizeof(nav_fixture_actors));*tls_rng=0x1234;
    lxv_move(controllers[0],1.0);
    assert(nav_fixture_test_seen==0x77923eff96f5da13ULL && *tls_rng==0x1234 && nav_fixture_actors[0].calls==1);
    DWORD actual_ui=ui_thread;ui_thread=actual_ui+1;*tls_rng=0x4321;
    lxv_move(controllers[0],1.0);assert(*tls_rng==(0x4321^0x55) && nav_fixture_actors[0].calls==1);
    ui_thread=actual_ui;boot_phase=2;*tls_rng=0x4321;
    lxv_move(controllers[0],1.0);assert(*tls_rng==(0x4321^0x55) && nav_fixture_actors[0].calls==1);
    boot_phase=3;unsigned char foreign_controller[0x40]={0};*tls_rng=0x4321;
    lxv_move(foreign_controller,1.0);assert(*tls_rng==(0x4321^0x55) && nav_fixture_actors[0].calls==1);
    {void *wrong=controllers[1];memcpy(ships[0]+LXW_OBJ_CONTROLLER,&wrong,8);*tls_rng=0x4321;
     lxv_move(controllers[0],1.0);
     assert(*tls_rng==(0x4321^0x55));
     assert(nav_fixture_bad_binding==1);
     assert(nav_cadence_failures==3);
     void *right=controllers[0];memcpy(ships[0]+LXW_OBJ_CONTROLLER,&right,8);}
    {void *right=ships[0];memcpy(controllers[0]+LXW_NAV_OWNER,&right,8);}
    nav_fixture_rng_armed=0;nav_fixture_native_override=NULL;nav_cadence_active=0;
    /* Arm is explicitly opt-in, idempotent for a live UI reload, and never
     * combines the fixture stream with the unrelated Linux LXNAVKEY stream. */
    stats.active=1;stats.issued=0;boot_mode=1;boot_phase=0;
    world_identity_installed=1;path_installed=1;path_options=1;nav_cadence_installed=1;
    lxk_armed=1;assert(!nav_fixture_arm());lxk_armed=0;
    assert(nav_fixture_arm() && !lxk_arm());
    assert(boot_gate(1,1,"",0,"",131072)==LX_STOP_UNPORTED_OPTION);
    stats.issued=1;assert(nav_fixture_arm());
    stats.active=0;assert(!nav_fixture_arm());
    nav_fixture_rng_armed=0;nav_cadence_installed=0;world_identity_installed=0;path_installed=0;path_options=0;
    memcpy(lxb_actors,saved,sizeof(saved));lxb_bound=saved_bound;lxb_configured=saved_configured;
    nav_cadence_failures=0;nav_fixture_test_rng=NULL;reset_session();
    printf("fixture NAV_UPDT: binding, seed vectors, ordinals and RNG restoration PASS\n");
}
