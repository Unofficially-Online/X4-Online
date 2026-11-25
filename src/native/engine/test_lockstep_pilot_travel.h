/* Players 2..N's travel drive tells its player (lockstep_pilot_travel.h): X4's start, stop and disrupt of a seated slot
 * 2..N ship run inside that slot's scope (slots 2, 3 and 8), a disrupt with no travel activity also stops the drive
 * there, the slot's own scope open, another scope, a scope that cannot open, another ship and the module off run X4's
 * routine as it is; a fault leaves the scope; the sites by CAS, the tick check, a site that is not X4's. */
static unsigned tptrv_scope,tptrv_enters,tptrv_leaves,tptrv_calls,tptrv_kinds[8],tptrv_scopes[8];
static void *tptrv_ships_called[8];static double tptrv_durations[8];
static int tptrv_enter_answer=1,tptrv_fault;
static unsigned char *tptrv_ships[PILOT_SLOTS+1];
static unsigned char *tptrv_slot(unsigned k) {return k<=PILOT_SLOTS?tptrv_ships[k]:NULL;}
static int tptrv_enter(unsigned k) {++tptrv_enters;if(!tptrv_enter_answer) return 0;tptrv_scope=k;return 1;}
static void tptrv_leave(unsigned k) {assert(tptrv_scope==k);tptrv_scope=0;++tptrv_leaves;}
static void tptrv_set_activity(unsigned k,int activity) {memcpy(pslots[k].player+UIREP_PLAYER_ACTIVITY,&activity,4);}
static unsigned tptrv_resumes;
static void tptrv_resume(unsigned k) {++tptrv_resumes;tptrv_set_activity(k,UIREP_ACTIVITY_TRAVEL);}
/* X4's routines as the slot's scope makes them act: the start sets the travel activity, the disrupt and the stop end it */
static uintptr_t tptrv_target(unsigned kind,void *ship,double duration) {
    if(tptrv_calls<8) {tptrv_kinds[tptrv_calls]=kind;tptrv_scopes[tptrv_calls]=tptrv_scope;tptrv_ships_called[tptrv_calls]=ship;tptrv_durations[tptrv_calls]=duration;}
    ++tptrv_calls;
    if(tptrv_fault) {tptrv_fault=0;RaiseException(0xe0000238u,0,0,NULL);}
    const unsigned k=tptrv_scope;
    assert(kind!=PTRV_KIND_START || !k || uirep_travel_defer);   /* a start in the scope: the adapter's travel start held */
    if(k) {int activity=0;memcpy(&activity,pslots[k].player+UIREP_PLAYER_ACTIVITY,4);
           if(kind==PTRV_KIND_START) uirep_adapter_start(&pslots[k],UIREP_ACTIVITY_TRAVEL);   /* U::Player's answer: held */
           else if(activity==UIREP_ACTIVITY_TRAVEL) tptrv_set_activity(k,0);}
    return 0x5150u+kind;
}
/* X4's post and the tell after it (the post rule), the setter and its tell (lockstep_ui_replay.h) */
static unsigned tptrv_seq,tptrv_post_seq,tptrv_tell_seq,tptrv_posts,tptrv_tells,tptrv_tell_slot;
static unsigned char tptrv_told[0x20];static void *tptrv_post_source;
static uintptr_t tptrv_post(void *source,void *event,double time,unsigned char immediate) {
    (void)event;(void)time;(void)immediate;tptrv_post_source=source;++tptrv_posts;tptrv_post_seq=++tptrv_seq;return 0x99u;
}
static void tptrv_tell(unsigned k,const unsigned char *event,double time) {
    (void)time;tptrv_tell_slot=k;memcpy(tptrv_told,event,0x20);++tptrv_tells;tptrv_tell_seq=++tptrv_seq;
}
static void tptrv_activity_tell(unsigned k,const unsigned char *event) {tptrv_tell_slot=k;memcpy(tptrv_told,event,0x20);++tptrv_tells;}
static void tptrv_setter(void *player,int activity) {
    if(activity!=7) memcpy((unsigned char*)player+UIREP_PLAYER_ACTIVITY,&activity,4);   /* 7: the setter refuses it */
}
static void test_pilot_travel(void) {
    static unsigned char players[PILOT_SLOTS+1][0x6b00],ships[PILOT_SLOTS+1][0x100],npc[0x100];
    pslot_slot kept[PILOT_SLOTS+1];memcpy(kept,pslots,sizeof(kept));const unsigned scope_was=pstk_scope_slot;
    for(unsigned k=0;k<=PILOT_SLOTS;k++) {memset(&pslots[k],0,sizeof(pslots[k]));memset(players[k],0,sizeof(players[k]));pslots[k].player=players[k];}
    memset(tptrv_ships,0,sizeof(tptrv_ships));
    ptrv_target_test=tptrv_target;ptrv_slot_ship_test=tptrv_slot;ptrv_enter_test=tptrv_enter;ptrv_leave_test=tptrv_leave;
    ptrv_resume_test=tptrv_resume;tptrv_resumes=0;ptrv_resumed=0;
    /* the travel drive's events on a player go to the scripts nobody owns once, as the slot's player's */
    {static const unsigned once_events[4]={0x2baaf50u,0x2b48e40u,0x2b48dd0u,0x2b48e08u};
     for(unsigned i=0;i<4;i++) assert(pvehk_fwd_once((void**)(void*)(pnat_base+once_events[i])));
     assert(pvehk_fwd_once((void**)(void*)(pnat_base+PVEHK_FWD_UI_EVENT)) && !pvehk_fwd_once((void**)(void*)(pnat_base+UIREP_ACTIVITY_EVENT)));}
    ptrv_installed=1;ptrv_row_count=0;
    for(unsigned i=0;i<3;i++) ptrv_calls[i]=ptrv_slot_calls[i]=ptrv_scoped[i]=0;
    ptrv_inside=ptrv_busy=ptrv_refused=ptrv_stops=ptrv_told=0;ptrv_calls[3]=0;
    const DWORD ui_was=ui_thread;ui_thread=GetCurrentThreadId();
    const unsigned slots[3]={2,3,8};
    for(unsigned j=0;j<3;j++) {
        const unsigned k=slots[j],other=j==2?2:k+1;
        memset(tptrv_ships,0,sizeof(tptrv_ships));tptrv_ships[k]=ships[k];tptrv_ships[other]=ships[other];
        tptrv_set_activity(k,0);pstk_scope_slot=0;tptrv_enter_answer=1;
        /* the ship's StartTravelMode event (site 0): X4's start inside slot k's scope, its activity travel after */
        tptrv_calls=0;tptrv_enters=tptrv_leaves=0;
        assert(ptrv_rule(0,ships[k],0.0)==0x5150u+PTRV_KIND_START && tptrv_calls==1 && tptrv_kinds[0]==PTRV_KIND_START);
        assert(tptrv_scopes[0]==k && tptrv_ships_called[0]==ships[k] && tptrv_enters==1 && tptrv_leaves==1 && !tptrv_scope);
        assert(ptrv_activity(k)==UIREP_ACTIVITY_TRAVEL && !uirep_travel_defer && !uirep_travel_held && tptrv_resumes==j+1 && ptrv_resumed==(LONG64)(j+1));
        /* while held, the adapter's travel start only notes its slot (resumed after the routine) */
        {const LONG64 deferred=uirep_travel_deferred;tptrv_set_activity(k,0);uirep_travel_defer=1;
         uirep_adapter_start(&pslots[k],UIREP_ACTIVITY_TRAVEL);
         assert(uirep_travel_held==&pslots[k] && uirep_travel_deferred==deferred+1 && ptrv_activity(k)==0);
         uirep_travel_defer=0;uirep_travel_held=NULL;tptrv_set_activity(k,UIREP_ACTIVITY_TRAVEL);}
        /* the superhighway's TravelDisabledEvent (site 7) while in travel: X4's disrupt in the scope (its duration as given),
         * which ends the activity; no extra stop */
        {const LONG64 stops=ptrv_stops;tptrv_calls=0;
         assert(ptrv_rule(7,ships[k],0.25)==0x5150u+PTRV_KIND_DISRUPT && tptrv_calls==1 && tptrv_kinds[0]==PTRV_KIND_DISRUPT);
         assert(tptrv_scopes[0]==k && tptrv_durations[0]==0.25 && ptrv_activity(k)==0 && ptrv_stops==stops);}
        /* a disrupt without the travel activity (damage, site 8): X4's disrupt, then X4's stop of the drive, both in the scope */
        {const LONG64 stops=ptrv_stops;tptrv_calls=0;
         ptrv_rule(8,ships[k],1.5);
         assert(tptrv_calls==2 && tptrv_kinds[0]==PTRV_KIND_DISRUPT && tptrv_kinds[1]==PTRV_KIND_STOP && tptrv_scopes[0]==k && tptrv_scopes[1]==k);
         assert(tptrv_ships_called[1]==ships[k] && ptrv_stops==stops+1 && !tptrv_scope);}
        /* the ship's StopTravelMode event (site 2) after a start: in the scope, the activity none */
        tptrv_set_activity(k,UIREP_ACTIVITY_TRAVEL);tptrv_calls=0;
        ptrv_rule(2,ships[k],0.0);assert(tptrv_calls==1 && tptrv_kinds[0]==PTRV_KIND_STOP && tptrv_scopes[0]==k && ptrv_activity(k)==0);
        /* slot k's scope open already (its own input's replay): X4's routine there, no scope opened */
        {const LONG64 inside=ptrv_inside;pstk_scope_slot=k;tptrv_calls=0;tptrv_enters=0;
         ptrv_rule(1,ships[k],0.0);assert(tptrv_calls==1 && !tptrv_enters && ptrv_inside==inside+1);pstk_scope_slot=0;}
        /* another slot's scope open: X4's routine as it is, counted */
        {const LONG64 busy=ptrv_busy;pstk_scope_slot=other;tptrv_calls=0;tptrv_enters=0;tptrv_enter_answer=0;
         ptrv_rule(3,ships[k],0.0);assert(tptrv_calls==1 && tptrv_scopes[0]==0 && ptrv_busy==busy+1);pstk_scope_slot=0;tptrv_enter_answer=1;}
        /* a scope that cannot open: X4's routine as it is, counted */
        {const LONG64 refused=ptrv_refused;tptrv_enter_answer=0;tptrv_calls=0;
         ptrv_rule(9,ships[k],0.0);assert(tptrv_calls==1 && tptrv_scopes[0]==0 && ptrv_refused==refused+1);tptrv_enter_answer=1;}
        /* another ship (an NPC's, slot 1's): X4's routine, no scope */
        {const LONG64 slot_calls=ptrv_slot_calls[PTRV_KIND_DISRUPT];tptrv_calls=0;tptrv_enters=0;
         ptrv_rule(10,npc,0.0);ptrv_rule(10,ships[1],0.0);assert(tptrv_calls==2 && !tptrv_enters && ptrv_slot_calls[PTRV_KIND_DISRUPT]==slot_calls);}
        /* a fault in X4's routine: the scope is left, the fault goes on */
        {int faulted=0;tptrv_fault=1;tptrv_leaves=0;
         __try {ptrv_rule(4,ships[k],0.0);} __except(EXCEPTION_EXECUTE_HANDLER) {faulted=1;}
         assert(faulted && tptrv_leaves==1 && !tptrv_scope);}
    }
    /* the routines' posts on the player (sites 11..14): inside slot k's scope, on P_k, at once: X4's post, then a copy of
     * the event to the scripts nobody owns as slot k's; not slot k's player, not at once, a later time, no scope, off:
     * X4's post alone. (In the tests a time <= 0 is due now.) */
    ptrv_installed=1;ptrv_post_test=tptrv_post;ptrv_tell_test=tptrv_tell;
    {   static unsigned char event[0x20];
        for(unsigned i=0;i<sizeof(event);i++) event[i]=(unsigned char)(0x40+i);
        const unsigned post_slots[3]={2,3,8};
        for(unsigned j=0;j<3;j++) {
            const unsigned k=post_slots[j],site=11+j;const LONG64 told=ptrv_told;
            pstk_scope_slot=k;tptrv_posts=tptrv_tells=0;
            assert(ptrv_post_rule(site,pslots[k].player,event,0.0,1)==0x99u && tptrv_posts==1 && tptrv_post_source==pslots[k].player);
            assert(tptrv_tells==1 && tptrv_tell_slot==k && tptrv_post_seq<tptrv_tell_seq && ptrv_told==told+1);
            assert(!memcmp(tptrv_told,event,ptrv_sites[site].event_size));
            tptrv_tells=0;
            ptrv_post_rule(site,pslots[k==2?3:2].player,event,0.0,1);   /* another slot's player */
            ptrv_post_rule(site,pslots[k].player,event,0.0,0);          /* not at once */
            ptrv_post_rule(site,pslots[k].player,event,5.0,1);          /* due later */
            pstk_scope_slot=0;ptrv_post_rule(site,pslots[k].player,event,0.0,1);   /* no scope */
            pstk_scope_slot=k;ptrv_installed=0;ptrv_post_rule(site,pslots[k].player,event,0.0,1);ptrv_installed=1;
            assert(!tptrv_tells && tptrv_posts==6);
        }
        pstk_scope_slot=0;}
    ptrv_post_test=NULL;ptrv_tell_test=NULL;
    /* the setter in slot k's scope (the UI replay's adapter): the activity's event to the scripts nobody owns after it, with
     * the new and the old activity; a refused change, the same activity, outside the scope: none */
    {   void (*const setter_was)(void*,int)=uirep_fn.set_activity;uirep_fn.set_activity=tptrv_setter;uirep_activity_tell_test=tptrv_activity_tell;
        const unsigned tell_slots[3]={2,3,8};
        for(unsigned j=0;j<3;j++) {
            const unsigned k=tell_slots[j];const LONG64 told=uirep_activity_told;
            tptrv_set_activity(k,0);pstk_scope_slot=k;tptrv_tells=0;
            uirep_set_activity(&pslots[k],UIREP_ACTIVITY_TRAVEL);
            {void *vt=NULL;int now=0,was=-1;memcpy(&vt,tptrv_told,8);memcpy(&now,tptrv_told+0x18,4);memcpy(&was,tptrv_told+0x1c,4);
             assert(tptrv_tells==1 && tptrv_tell_slot==k && vt==(void*)(pnat_base+UIREP_ACTIVITY_EVENT) && now==UIREP_ACTIVITY_TRAVEL && !was);}
            uirep_set_activity(&pslots[k],0);assert(tptrv_tells==2 && uirep_activity_told==told+2);
            uirep_set_activity(&pslots[k],7);assert(tptrv_tells==2);                       /* refused by the setter */
            uirep_set_activity(&pslots[k],0);assert(tptrv_tells==2);                       /* the same */
            pstk_scope_slot=0;uirep_set_activity(&pslots[k],UIREP_ACTIVITY_TRAVEL);assert(tptrv_tells==2);   /* outside the scope */
        }
        pstk_scope_slot=0;uirep_fn.set_activity=setter_was;uirep_activity_tell_test=NULL;}
    /* off: X4's routine, no scope */
    ptrv_installed=0;tptrv_calls=0;tptrv_enters=0;ptrv_rule(0,ships[2],0.0);assert(tptrv_calls==1 && !tptrv_enters);
    {char row[1024];assert(ptrv_status(row,sizeof(row)) && !strncmp(row,"installed=0 calls=",18) && strstr(row," slot=6/9/9 scoped=3/3/6 ") &&
                            strstr(row," inside=3 busy=3 refused=3 stops=3 told=3 activity=") && strstr(row," why=- check=- last=") && strstr(row,":0:8:0>1:S"));}
    /* the sites on a region of the image's shape: ours by CAS (a call, a jump, a conditional jump to the trampoline), the
     * trampolines (the rule), the tick check, X4's bytes back, a site that is not X4's refused */
    {   unsigned char *const block=VirtualAlloc(NULL,0xe00000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(block);
        unsigned char *const region=block-0x440000u,*const saved_image=image,*const saved_page=ptrv_page;
        image=region;ptrv_page=NULL;ptrv_installed=0;ptrv_check_why="-";ptrv_why="-";
        for(unsigned i=0;i<PTRV_SITES;i++) {
            assert(ptrv_sites[i].site>=0x440000u && ptrv_sites[i].site+6<=0x440000u+0xe00000u);
            unsigned char o[6];ptrv_original(i,o);memcpy(region+ptrv_sites[i].site,o,ptrv_length(i));
        }
        assert(ptrv_install() && ptrv_installed && ptrv_check());
        for(unsigned i=0;i<PTRV_SITES;i++) {
            const unsigned char *at=region+ptrv_sites[i].site;const unsigned n=ptrv_length(i);int32_t rel;memcpy(&rel,at+n-4,4);
            const unsigned char *t=at+n+rel;void *to=NULL;
            assert(t==ptrv_page+i*PTRV_TRAMP && t[0]==0xff && t[1]==0x25);memcpy(&to,t+6,8);assert(to==ptrv_rules[i]);
            assert(ptrv_sites[i].op==PTRV_OP_JCC?at[0]==0x0f && at[1]==0x84:at[0]==(ptrv_sites[i].op==PTRV_OP_CALL?0xe8:0xe9));
        }
        region[ptrv_sites[0].site+2]^=1;assert(!ptrv_check() && !strcmp(ptrv_check_why,"site_lost"));region[ptrv_sites[0].site+2]^=1;ptrv_check_why="-";
        assert(ptrv_check());
        ptrv_uninstall();assert(!ptrv_installed);
        for(unsigned i=0;i<PTRV_SITES;i++) {unsigned char o[6];ptrv_original(i,o);assert(!memcmp(region+ptrv_sites[i].site,o,ptrv_length(i)));}
        region[ptrv_sites[5].site]=0xcc;ptrv_why="-";assert(!ptrv_install() && !ptrv_installed && !strcmp(ptrv_why,"site"));
        for(unsigned i=0;i<PTRV_SITES;i++) if(i!=5) {unsigned char o[6];ptrv_original(i,o);assert(!memcmp(region+ptrv_sites[i].site,o,ptrv_length(i)));}
        VirtualFree(ptrv_page,0,MEM_RELEASE);ptrv_page=saved_page;image=saved_image;VirtualFree(block,0,MEM_RELEASE);}
    ptrv_installed=0;ptrv_why="-";ptrv_check_why="-";ptrv_row_count=0;
    ptrv_target_test=NULL;ptrv_slot_ship_test=NULL;ptrv_enter_test=NULL;ptrv_leave_test=NULL;ptrv_resume_test=NULL;
    memcpy(pslots,kept,sizeof(kept));pstk_scope_slot=scope_was;ui_thread=ui_was;
    printf("pilot travel: X4's start, stop and disrupt of a slot's ship inside its scope (slots 2, 3, 8), the disrupt's stop "
           "without travel, its own scope, another scope, refused, another ship, a fault, off, the sites, the check tests PASS\n");
}
static void test_pilot_travel_image(const char *path) {
    FILE *f=fopen(path,"rb");assert(f);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;IMAGE_SECTION_HEADER sections[32];
    assert(fread(&dos,1,sizeof(dos),f)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && !fseek(f,dos.e_lfanew,SEEK_SET));
    assert(fread(&nt,1,sizeof(nt),f)==sizeof(nt) && nt.FileHeader.TimeDateStamp==0x6a1eaded && nt.FileHeader.NumberOfSections<=32);
    unsigned count=nt.FileHeader.NumberOfSections;
    assert(fread(sections,sizeof(*sections),count,f)==count);
    static unsigned char bytes[0x600];
    for(unsigned i=0;i<sizeof(ptrv_fn_pins)/sizeof(*ptrv_fn_pins);i++)
        assert(ptrv_fn_pins[i].size<=sizeof(bytes) && test_pnat_file_bytes(f,sections,count,ptrv_fn_pins[i].rva,ptrv_fn_pins[i].size,bytes) &&
               hash_ok(bytes,ptrv_fn_pins[i].size,ptrv_fn_pins[i].sha));
    for(unsigned i=0;i<PTRV_SITES;i++) {
        const ptrv_site *s=&ptrv_sites[i];const unsigned n=ptrv_length(i);
        assert(s->block<=s->site && s->site+n<=s->block+s->block_size && s->block_size<=sizeof(bytes));
        assert(test_pnat_file_bytes(f,sections,count,s->block,s->block_size,bytes) && hash_ok(bytes,s->block_size,s->block_sha));
        const unsigned char *at=bytes+(s->site-s->block);int32_t r;memcpy(&r,at+n-4,4);
        assert(s->op==PTRV_OP_JCC?at[0]==0x0f && at[1]==0x84:at[0]==(s->op==PTRV_OP_CALL?0xe8:0xe9));
        assert((unsigned)(s->site+n)+(unsigned)r==s->target);
    }
    /* X4's player test in each routine: 0x9f7860's answer against the ship (rdi = the routine's this) */
    static const struct {unsigned rva;unsigned char size,code[8];} tests[]={
        {0x6d9622u,8,{0xe8,0x39,0xe2,0x31,0x00,0x48,0x39,0x38}},   /* the disrupt: call 0x9f7860; cmp [rax], rdi */
        {0x6d9236u,8,{0xe8,0x25,0xe6,0x31,0x00,0x48,0x39,0x38}},   /* the stop's charge abort */
        {0x6d93ffu,8,{0xe8,0x5c,0xe4,0x31,0x00,0x48,0x39,0x38}},   /* the stop's travel stop */
        {0x6d9634u,5,{0xe8,0xf7,0x3c,0x32,0x00}}};                 /* the disrupt's StopPlayerActivity 0x9fd330 */
    for(unsigned i=0;i<sizeof(tests)/sizeof(*tests);i++)
        assert(test_pnat_file_bytes(f,sections,count,tests[i].rva,tests[i].size,bytes) && !memcmp(bytes,tests[i].code,tests[i].size));
    fclose(f);
    printf("pilot travel image: pins, the 11 calls of the travel routines, their 4 posts on the player and the player tests PASS\n");
}
