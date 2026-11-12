/* lockstep_md_presentation.h on synthetic memory: the vtable slots, the owner rule, the refusals and the restore. */
static unsigned test_mdp_ran[MDP_ACTIONS];
#define TEST_MDP_STOCK(i) static void test_mdp_stock_##i(void *node,void *exec,void *frame) {(void)node;(void)exec;(void)frame;++test_mdp_ran[i];}
TEST_MDP_STOCK(0) TEST_MDP_STOCK(1) TEST_MDP_STOCK(2) TEST_MDP_STOCK(3) TEST_MDP_STOCK(4) TEST_MDP_STOCK(5) TEST_MDP_STOCK(6) TEST_MDP_STOCK(7)
TEST_MDP_STOCK(8) TEST_MDP_STOCK(9) TEST_MDP_STOCK(10)
static void test_md_presentation(void) {
    static const mdp_fn stock[MDP_ACTIONS]={test_mdp_stock_0,test_mdp_stock_1,test_mdp_stock_2,test_mdp_stock_3,test_mdp_stock_4,test_mdp_stock_5,
        test_mdp_stock_6,test_mdp_stock_7,test_mdp_stock_8,test_mdp_stock_9,test_mdp_stock_10};
    const uint64_t issued_was=stats.issued;const DWORD ui_was=ui_thread;const uint32_t active_was=stats.active,slot_was=stats.slot;
    const unsigned scope_was=pstk_scope_slot,depth_was=mpp_depth;
    void *slot_was_at[MDP_ACTIONS];
    for(unsigned i=0;i<MDP_ACTIONS;i++) {
        memcpy(&slot_was_at[i],image+mdp_actions[i].vtable+MDP_SLOT,8);
        void *stock_at=image+mdp_actions[i].execute;memcpy(image+mdp_actions[i].vtable+MDP_SLOT,&stock_at,8);
        for(unsigned j=0;j<i;j++) assert(mdp_actions[j].vtable!=mdp_actions[i].vtable && mdp_actions[j].execute!=mdp_actions[i].execute);
    }
    mdp_armed=0;memset(mdp_installed,0,sizeof(mdp_installed));
    ui_thread=GetCurrentThreadId();stats.active=1;stats.slot=1;pstk_scope_slot=0;mpp_depth=0;
    /* Before the first tick, on the frame thread, every slot the stock function: else nothing is swapped. */
    stats.issued=1;assert(!mdp_arm() && !strcmp(mdp_why,"late_or_thread"));stats.issued=0;
    stats.active=0;assert(!mdp_arm() && !strcmp(mdp_why,"not_started"));stats.active=1;   /* before the gate's start */
    {unsigned char *const found=image;image=NULL;assert(!mdp_arm() && !strcmp(mdp_why,"not_started"));image=found;}
    ui_thread=GetCurrentThreadId()+1;assert(!mdp_arm());ui_thread=GetCurrentThreadId();
    {void *foreign=image+1;memcpy(image+mdp_actions[6].vtable+MDP_SLOT,&foreign,8);
     assert(!mdp_arm() && !strcmp(mdp_why,"open_menu") && !mdp_armed);
     for(unsigned i=0;i<MDP_ACTIONS;i++) {void *now;memcpy(&now,image+mdp_actions[i].vtable+MDP_SLOT,8);assert(i==6 || now==(void*)(image+mdp_actions[i].execute));}
     void *stock_at=image+mdp_actions[6].execute;memcpy(image+mdp_actions[6].vtable+MDP_SLOT,&stock_at,8);}
    assert(mdp_arm() && mdp_armed && mdp_arm());
    for(unsigned i=0;i<MDP_ACTIONS;i++) {
        void *now;memcpy(&now,image+mdp_actions[i].vtable+MDP_SLOT,8);
        assert(now==(void*)mdp_wrappers[i] && mdp_installed[i] && mdp_original[i]==(mdp_fn)(image+mdp_actions[i].execute));
        mdp_original[i]=stock[i];   /* the test's stand-in for the stock function */
    }
    memset(test_mdp_ran,0,sizeof(test_mdp_ran));
    /* Nobody's frame (the session's story, the world): runs on every client. */
    for(unsigned i=0;i<MDP_ACTIONS;i++) mdp_wrappers[i](NULL,NULL,NULL);
    for(unsigned i=0;i<MDP_ACTIONS;i++) assert(test_mdp_ran[i]==1);
    assert(mdp_runs==MDP_ACTIONS);
    /* An owned frame of slot 2: runs on client 2, not on client 1 or 3. */
    pstk_scope_slot=2;mpp_depth=1;
    stats.slot=2;mdp_wrappers[0](NULL,NULL,NULL);assert(test_mdp_ran[0]==2 && !mdp_skips[0]);
    stats.slot=1;mdp_wrappers[0](NULL,NULL,NULL);mdp_wrappers[10](NULL,NULL,NULL);assert(test_mdp_ran[0]==2 && test_mdp_ran[10]==1 && mdp_skips[0]==1 && mdp_skips[10]==1);
    stats.slot=3;mdp_wrappers[5](NULL,NULL,NULL);assert(test_mdp_ran[5]==1 && mdp_skips[5]==1);
    /* Slot 2's scope without an owned frame (a replayed call, the pilot's update): not a cue's presentation, runs. */
    mpp_depth=0;stats.slot=1;mdp_wrappers[5](NULL,NULL,NULL);assert(test_mdp_ran[5]==2 && mdp_skips[5]==1);
    /* Another thread, a stopped session: the stock function. */
    mpp_depth=1;ui_thread=GetCurrentThreadId()+1;mdp_wrappers[1](NULL,NULL,NULL);assert(test_mdp_ran[1]==2);ui_thread=GetCurrentThreadId();
    stats.active=0;mdp_wrappers[1](NULL,NULL,NULL);assert(test_mdp_ran[1]==3 && !mdp_skips[1]);stats.active=1;
    {char status[512];assert(mdp_status(status,sizeof(status))>0 && strstr(status,"armed=1 installed=11/11 ") && strstr(status,"skipped=3 "));}
    /* The end: every slot holds what it held; a slot somebody else changed stays and is named. */
    for(unsigned i=0;i<MDP_ACTIONS;i++) mdp_original[i]=(mdp_fn)(image+mdp_actions[i].execute);
    mdp_restore();assert(!mdp_armed);
    for(unsigned i=0;i<MDP_ACTIONS;i++) {void *now;memcpy(&now,image+mdp_actions[i].vtable+MDP_SLOT,8);assert(now==(void*)(image+mdp_actions[i].execute) && !mdp_installed[i]);}
    assert(mdp_arm());
    {void *foreign=image+1;memcpy(image+mdp_actions[2].vtable+MDP_SLOT,&foreign,8);
     mdp_restore();assert(!mdp_armed && mdp_installed[2] && !strcmp(mdp_why,"restore"));
     void *now;memcpy(&now,image+mdp_actions[2].vtable+MDP_SLOT,8);assert(now==foreign);mdp_installed[2]=0;}
    for(unsigned i=0;i<MDP_ACTIONS;i++) memcpy(image+mdp_actions[i].vtable+MDP_SLOT,&slot_was_at[i],8);
    mdp_why="-";mdp_runs=0;memset((void*)mdp_skips,0,sizeof(mdp_skips));
    pstk_scope_slot=scope_was;mpp_depth=depth_was;stats.issued=issued_was;ui_thread=ui_was;stats.active=active_was;stats.slot=slot_was;
    printf("md presentation: slots swapped and restored, an owned frame's actions on its owner's client only, refusals PASS\n");
}
static int test_mdm_pin_answer;
static int test_mdm_pin(void) {return test_mdm_pin_answer;}
static void test_md_menu_state(void) {
    const uint64_t issued_was=stats.issued;const DWORD ui_was=ui_thread;const uint32_t active_was=stats.active;unsigned char site_was[5];
    memcpy(site_was,image+MDM_SITE,5);memcpy(image+MDM_SITE,mdm_stock,5);
    mdm_armed=0;mdm_pin=test_mdm_pin;test_mdm_pin_answer=1;ui_thread=GetCurrentThreadId();stats.active=1;
    stats.issued=1;assert(!mdm_arm() && !strcmp(mdm_why,"late_or_thread"));stats.issued=0;
    /* Before the gate's start: no image, nothing read, nothing written, and the status answers without one. */
    stats.active=0;assert(!mdm_arm() && !strcmp(mdm_why,"not_started"));stats.active=1;
    {unsigned char *const found=image;char status[64];image=NULL;
     assert(!mdm_arm() && !strcmp(mdm_why,"not_started") && mdm_status(status,sizeof(status))>0 && !strcmp(status,"armed=0 site=0000 why=not_started"));
     image=found;}
    test_mdm_pin_answer=0;assert(!mdm_arm() && !strcmp(mdm_why,"pin") && !memcmp(image+MDM_SITE,mdm_stock,5));test_mdm_pin_answer=1;
    image[MDM_SITE+1]^=1;assert(!mdm_arm() && !strcmp(mdm_why,"site"));image[MDM_SITE+1]^=1;
    assert(mdm_arm() && mdm_armed && mdm_arm() && !memcmp(image+MDM_SITE,mdm_patched,5));
    {char status[64];assert(mdm_status(status,sizeof(status))>0 && !strcmp(status,"armed=1 site=31c0 why=-"));}
    mdm_restore();assert(!mdm_armed && !memcmp(image+MDM_SITE,mdm_stock,5));
    assert(mdm_arm());image[MDM_SITE+4]=0xcc;mdm_restore();assert(!mdm_armed && !strcmp(mdm_why,"restore") && image[MDM_SITE+4]==0xcc);   /* a foreign byte stays */
    memcpy(image+MDM_SITE,site_was,5);mdm_pin=mdm_pin_image;mdm_why="-";stats.issued=issued_was;ui_thread=ui_was;stats.active=active_was;
    printf("md menu state: the property's call replaced and restored, pin, site and timing refusals PASS\n");
}
/* The real image: the site is the property evaluator's call of 0x8cd710, the pin holds, the vtables hold the stock functions. */
static void test_md_presentation_native_image(const char *path) {
    unsigned char *prior=image;
    image=test_pmath_map(path);
    assert(mdm_pin_image() && !memcmp(image+MDM_SITE,mdm_stock,5));
    {int32_t rel=0;memcpy(&rel,image+MDM_SITE+1,4);assert(MDM_SITE+5u+(uint32_t)rel==0x8cd710u);}
    for(unsigned i=0;i<MDP_ACTIONS;i++) {
        void *now=NULL;memcpy(&now,image+mdp_actions[i].vtable+MDP_SLOT,8);
        assert(now==(void*)(image+mdp_actions[i].execute));   /* the map is relocated: the slot points into it */
    }
    image=prior;
    printf("pinned native image md presentation verified (eleven action vtables, the full-screen menu property's call)\n");
}
