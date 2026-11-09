/* The impulses on fixture bodies (gate_impulse.h): a planted fixture ship's second body, found by its BodyID, through
 * the wrapper (the impulse, the point, the pose and motion before, the velocities after, the shared order; another
 * body passes through untraced; the row text); on the real executable the pin, the entry installed (jmp [rip] -> the
 * wrapper, bytes 14..15 kept; the trampoline the 14 bytes and a jmp back) and restored. Included by test_gate_linux.c. */
static unsigned char tai_object[0x300],tai_record[2][0x80],tai_xbody[2][0x10],tai_scene[0x28],tai_system[0x180];
static unsigned char tai_iface[0x10],tai_manager[0x18],tai_body[2][0x80] __attribute__((aligned(16))),tai_motion[2][0x80] __attribute__((aligned(16)));
static void *tai_bodies[2];
static int tai_native_calls;
static void tai_native(void *iface,const void *id,lxai_vec impulse,lxai_vec point) {
    (void)iface;(void)id;(void)point;tai_native_calls++;
    float v[4];memcpy(v,tai_motion[1],16);for(unsigned i=0;i<3;i++) v[i]+=impulse.v[i];memcpy(tai_motion[1],v,16); /* the step's linear part */
}
static void tai_prepare_fixture(void) {
    lxb_bound = LXB_ACTORS;
    lxb_actors[1].object = tai_object;
    lxb_body_slot[1] = 0x258u;
    for (unsigned which = 0; which < 2; which++) {
        void *pointer = tai_record[which];
        memcpy(tai_object + 0x258 + which * 8, &pointer, 8);
        pointer = tai_object;
        memcpy(tai_record[which], &pointer, 8);
        pointer = tai_xbody[which];
        memcpy(tai_record[which] + 0x10, &pointer, 8);
        const uint32_t id = which;
        memcpy(tai_xbody[which] + 8, &id, 4);
        memcpy(tai_body[which] + 0x70, &id, 4);
        pointer = tai_scene;
        memcpy(tai_record[which] + 0x18, &pointer, 8);
        tai_bodies[which] = tai_body[which];
        pointer = tai_motion[which];
        memcpy(tai_body[which] + 0x48, &pointer, 8);
        for (unsigned i = 0; i < 0x20; i++) tai_body[which][i] = (unsigned char)(0x10 * which + i);
        for (unsigned i = 0; i < 0x60; i++) tai_motion[which][i] = (unsigned char)(0x40 + i);
    }
    void *pointer = tai_system;
    memcpy(tai_scene + 0x20, &pointer, 8);
    pointer = tai_iface;
    memcpy(tai_system + LXB_PHYS_LOCK_IFACE, &pointer, 8);
    pointer = tai_manager;
    memcpy(tai_iface + 8, &pointer, 8);
    const uint64_t count = 2;
    memcpy(tai_manager, &count, 8);
    pointer = tai_bodies;
    memcpy(tai_manager + 0x10, &pointer, 8);
}
static void test_impulse_linux(void) {
    static lxb_actor saved_actors[LXB_ACTORS];static unsigned saved_slots[LXB_ACTORS];int saved_bound=lxb_bound;
    memcpy(saved_actors,lxb_actors,sizeof(saved_actors));memcpy(saved_slots,lxb_body_slot,sizeof(saved_slots));
    memset(lxb_actors,0,sizeof(lxb_actors));memset(lxb_body_slot,0,sizeof(lxb_body_slot));memset(lxai_traces,0,sizeof(lxai_traces));
    lxai_calls=lxai_recorded=lxai_failures=0;
    reset_session();stats.active=1;stats.issued=28;ui_thread=GetCurrentThreadId();
    tai_prepare_fixture();
    {const float zero[4]={0};memcpy(tai_motion[1],zero,16);}
    lxai_original_override=tai_native;
    const lxai_vec impulse={{1.0f,2.0f,3.0f,0.0f}},point={{-394.5f,36000.0f,600.0f,0.0f}};
    unsigned char motion_before[0x60];memcpy(motion_before,tai_motion[1],0x60);
    const uint32_t id1=1,id9=9;
    const uint64_t seq=lx_phys_seq;
    lxai_add(NULL,&id1,impulse,point);
    lxai_add(NULL,&id9,impulse,point);                                               /* not a fixture body */
    assert(tai_native_calls==2 && lxai_calls==2 && lxai_recorded==1 && !lxai_failures && lxai_traces[1].tick==28 && lxai_traces[1].count==1);
    const lxai_record *r=&lxai_traces[1].records[0];
    assert(r->which==1 && r->seq==seq+1 && r->source && !memcmp(r->impulse,impulse.v,16) && !memcmp(r->point,point.v,16));
    assert(r->caller == 0 && r->caller_state == LXAI_CALLER_OTHER_SOURCE);
    assert(!memcmp(r->pose,tai_body[1],32));
    assert(!memcmp(r->motion,motion_before,0x60));
    {float after[4];memcpy(after,r->after,16);assert(after[0]==1.0f && after[1]==2.0f && after[2]==3.0f);}
    lxai_installed=1;char text[1024];
    assert(lxai_text(2,1,text,sizeof(text))>0 && !strncmp(text,"which=1 tick=28 seq=",20) && strstr(text," impulse=3f80000040000000404000000000000 ")==NULL &&
           strstr(text," impulse=3f8000004000000040400000") && strstr(text," after=3f8000004000000040400000"));
    assert(strstr(text, " caller=0 caller_state=source"));
    assert(!lxai_text(2,2,text,sizeof(text)) && !lxai_text(1,1,text,sizeof(text)));
    lxai_installed=0;lxai_original_override=NULL;tai_native_calls=0;
    memcpy(lxb_actors,saved_actors,sizeof(saved_actors));memcpy(lxb_body_slot,saved_slots,sizeof(saved_slots));lxb_bound=saved_bound;
    memset(lxai_traces,0,sizeof(lxai_traces));lxai_calls=lxai_recorded=lxai_failures=0;stats.active=0;
    printf("impulse diag wrapper PASS\n");
}
static void test_impulse_elf(void) {
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();lxai_installed=lxai_restored=0;
    unsigned char before[16];memcpy(before,LX(LXAI_VA),16);
    assert(!memcmp(before,lxai_prologue,14));
    assert(lxai_pins() && lxai_state()==0 && lxai_arm() && lxai_installed && lxai_state()==1 && lxai_check() && lxai_pins());
    assert(LX(LXAI_VA)[0]==0xff && LX(LXAI_VA)[1]==0x25 && !memcmp(LX(LXAI_VA)+14,before+14,2) &&
           !memcmp(lxai_page,before,14) && lxai_page[14]==0xff && lxai_page[15]==0x25);
    {const void *back;memcpy(&back,lxai_page+20,8);assert(back==(const void*)LX(LXAI_VA+14u));}
    lxai_restore();assert(!lxai_installed && lxai_restored && !memcmp(LX(LXAI_VA),before,16) && lxai_state()==0 && lxai_pins());
    munmap(lxai_page,GATE_PAGE);lxai_page=NULL;lxai_restored=0;stats.active=0;
    printf("impulse diag ELF PASS\n");
}

static void test_impulse_caller_guards_elf(void) {
    uintptr_t frame[8] = {0};
    uint64_t caller = UINT64_MAX;
    frame[1] = (uintptr_t)LX(LXAI_HELPER_RETURN);
    frame[7] = (uintptr_t)LX(LX_CALL_WINDOW + 9u);
    lxai_caller_counters_reset();
    assert(lxai_helper_pin());
    assert(lxai_outer_return(LXAI_HELPER_RETURN, frame, &caller) == LXAI_CALLER_KNOWN);
    assert(caller == LX_CALL_WINDOW + 9u);
    lxai_record record = {0};
    record.source = LXAI_HELPER_RETURN;
    lxai_capture_caller(&record, frame);
    assert(record.caller_state == LXAI_CALLER_KNOWN && lxai_caller_known == 1);
    assert(lxai_outer_return(0, NULL, &caller) == LXAI_CALLER_OTHER_SOURCE && !caller);
    assert(lxai_outer_return(LXAI_HELPER_RETURN, NULL, &caller) == LXAI_CALLER_FRAME && !caller);
    assert(lxai_outer_return(LXAI_HELPER_RETURN, (void *)UINTPTR_MAX, &caller) == LXAI_CALLER_FRAME);
    frame[1]++;
    assert(lxai_outer_return(LXAI_HELPER_RETURN, frame, &caller) == LXAI_CALLER_FRAME && !caller);
    lxai_capture_caller(&record, frame);
    assert(!record.caller && record.caller_state == LXAI_CALLER_FRAME);
    assert(lxai_caller_unknown == 1 && lxai_caller_rejected == 1);
    frame[1]--;
    frame[7] = (uintptr_t)LX(LX_TEXT_END_VA);
    assert(lxai_outer_return(LXAI_HELPER_RETURN, frame, &caller) == LXAI_CALLER_OUTSIDE_TEXT && !caller);

    unsigned char *pages = mmap(NULL, GATE_PAGE * 2, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(pages != MAP_FAILED);
    assert(!mprotect(pages + GATE_PAGE, GATE_PAGE, PROT_NONE));
    unsigned char *edge = pages + GATE_PAGE - 16;
    memcpy(edge + 8, &frame[1], 8);
    assert(lxai_outer_return(LXAI_HELPER_RETURN, edge, &caller) == LXAI_CALLER_READ_FAILED && !caller);
    lxai_capture_caller(&record, edge);
    assert(!record.caller && record.caller_state == LXAI_CALLER_READ_FAILED);
    assert(lxai_caller_unknown == 2 && lxai_caller_read_failed == 1 && lxai_caller_rejected == 1);
    assert(lxai_outer_return(LXAI_HELPER_RETURN, pages + GATE_PAGE, &caller) == LXAI_CALLER_READ_FAILED);
    assert(!munmap(pages, GATE_PAGE * 2));

    /* A changed fallback byte rejects the whole helper, not just its call path. */
    unsigned char *fallback = LX(LXAI_HELPER_VA + 0xcfu);
    assert(!mprotect((void *)((uintptr_t)fallback & ~(uintptr_t)(GATE_PAGE - 1)),
        GATE_PAGE, PROT_READ | PROT_WRITE));
    unsigned char saved = *fallback;
    *fallback ^= 1;
    assert(!lxai_helper_pin());
    assert(lxai_outer_return(LXAI_HELPER_RETURN, frame, &caller) == LXAI_CALLER_HELPER_PIN && !caller);
    *fallback = saved;
    assert(lxai_helper_pin());

    char status[384];
    lxai_calls = lxai_recorded = lxai_failures = INT64_MAX;
    lxai_caller_known = lxai_caller_unknown = INT64_MAX;
    lxai_caller_read_failed = lxai_caller_rejected = INT64_MAX;
    stats.issued = UINT64_MAX;
    lxai_stage = "entry_changed";
    assert(lxai_status(status, sizeof(status)) > 0);
    assert(strstr(status, "caller_rejected=9223372036854775807"));
    lxai_calls = lxai_recorded = lxai_failures = 0;
    lxai_caller_counters_reset();
    stats.issued = 0;
    lxai_stage = "idle";
    printf("impulse caller guards and maximum status PASS\n");
}

static int tai_helper_guard(void *object) {
    assert(object == tai_object);
    return 1;
}

static void test_impulse_caller_stack_elf(void) {
    /* Execute only the pinned helper in an anonymous ELF copy. Its virtual
     * guard, AddImpulse body and copy-back endpoint are controlled test doubles;
     * no game entry point or initializer is executed. The bridge gives a known
     * outer return while preserving the helper's real pushes and stack locals. */
    static lxb_actor saved_actors[LXB_ACTORS];
    static unsigned saved_slots[LXB_ACTORS];
    static void *vtable[0x1c30 / 8];
    static unsigned char table[0x5000] __attribute__((aligned(16)));
    int saved_bound = lxb_bound;
    memcpy(saved_actors, lxb_actors, sizeof(saved_actors));
    memcpy(saved_slots, lxb_body_slot, sizeof(saved_slots));
    memset(lxb_actors, 0, sizeof(lxb_actors));
    memset(lxb_body_slot, 0, sizeof(lxb_body_slot));
    tai_prepare_fixture();
    void *saved_vtable;
    memcpy(&saved_vtable, tai_object, 8);
    vtable[0x1be0 / 8] = (void *)tai_helper_guard;
    void *pointer = vtable;
    memcpy(tai_object, &pointer, 8);
    void *saved_table;
    memcpy(&saved_table, LX(0x3db83c8u), 8);
    pointer = table;
    memcpy(LX(0x3db83c8u), &pointer, 8);
    const float scale = 1.0f;
    memcpy(table + 0x4fa4, &scale, 4);

    unsigned char saved_bridge[16];
    unsigned char *bridge = LX(LX_CALL_WINDOW);
    const uintptr_t mask = ~(uintptr_t)(GATE_PAGE - 1);
    assert(!mprotect((void *)((uintptr_t)bridge & mask), GATE_PAGE, PROT_READ | PROT_WRITE));
    memcpy(saved_bridge, bridge, sizeof(saved_bridge));
    unsigned char code[16] = {0x48, 0x83, 0xec, 8, 0xe8, 0, 0, 0, 0,
        0x48, 0x83, 0xc4, 8, 0xc3, 0x90, 0x90};
    const int32_t relative = (int32_t)(LXAI_HELPER_VA - (LX_CALL_WINDOW + 9u));
    memcpy(code + 5, &relative, 4);
    memcpy(bridge, code, sizeof(code));
    unsigned char *copy_back = LX(0xee8150u);
    assert(!mprotect((void *)((uintptr_t)copy_back & mask), GATE_PAGE, PROT_READ | PROT_WRITE));
    const unsigned char saved_copy_back = *copy_back;
    *copy_back = 0xc3;
    assert(!mprotect((void *)((uintptr_t)bridge & mask), GATE_PAGE, PROT_READ | PROT_EXEC));
    assert(!mprotect((void *)((uintptr_t)copy_back & mask), GATE_PAGE, PROT_READ | PROT_EXEC));
    assert(!mprotect((void *)((uintptr_t)LX(LXAI_HELPER_VA) & mask), GATE_PAGE, PROT_READ | PROT_EXEC));

    reset_session();
    stats.active = 1;
    boot_mode = 1;
    boot_phase = 0;
    ui_thread = GetCurrentThreadId();
    assert(lxai_arm());
    stats.issued = 27;
    tai_native_calls = 0;
    lxai_original_override = tai_native;
    const float impulse[4] __attribute__((aligned(16))) = {1.0f, 2.0f, 3.0f, 4.0f};
    const float point[4] __attribute__((aligned(16))) = {5.0f, 6.0f, 7.0f, 8.0f};
    typedef void (*helper_fn)(void *, const float *, const float *);
    ((helper_fn)(void *)bridge)(tai_object, impulse, point);
    assert(tai_native_calls == 1 && lxai_calls == 1 && lxai_recorded == 1 && !lxai_failures);
    const lxai_record *record = &lxai_traces[1].records[0];
    assert(lxai_traces[1].count == 1 && record->which == 0);
    assert(record->source == LXAI_HELPER_RETURN);
    assert(record->caller_state == LXAI_CALLER_KNOWN && record->caller == LX_CALL_WINDOW + 9u);
    assert(!memcmp(record->impulse, impulse, 12) && record->impulse[3] == record->impulse[2]);
    assert(!memcmp(record->point, point, 12) && record->point[3] == record->point[2]);
    assert(lxai_caller_known == 1 && !lxai_caller_unknown);
    printf("impulse pinned helper stack caller=%llx original_calls=%d PASS\n",
        (unsigned long long)record->caller, tai_native_calls);
    lxai_restore();
    assert(lxai_restored && !lxai_installed && lxai_pins());
    assert(!munmap(lxai_page, GATE_PAGE));
    lxai_page = NULL;
    lxai_restored = 0;
    lxai_original_override = NULL;
    assert(!mprotect((void *)((uintptr_t)bridge & mask), GATE_PAGE, PROT_READ | PROT_WRITE));
    assert(!mprotect((void *)((uintptr_t)copy_back & mask), GATE_PAGE, PROT_READ | PROT_WRITE));
    assert(!mprotect((void *)((uintptr_t)LX(LXAI_HELPER_VA) & mask), GATE_PAGE, PROT_READ | PROT_WRITE));
    memcpy(bridge, saved_bridge, sizeof(saved_bridge));
    *copy_back = saved_copy_back;
    memcpy(LX(0x3db83c8u), &saved_table, 8);
    memcpy(tai_object, &saved_vtable, 8);
    memcpy(lxb_actors, saved_actors, sizeof(saved_actors));
    memcpy(lxb_body_slot, saved_slots, sizeof(saved_slots));
    lxb_bound = saved_bound;
    stats.active = 0;
    tai_native_calls = 0;
    memset(lxai_traces, 0, sizeof(lxai_traces));
    lxai_calls = lxai_recorded = lxai_failures = 0;
    lxai_caller_counters_reset();
}
