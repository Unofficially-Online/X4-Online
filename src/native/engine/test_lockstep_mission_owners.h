/* Personal missions per player (lockstep_mission_owners.h): planted MD cues (a script's static cue, a mission cue under it,
 * its sub-cue and a sub-sub-cue, another tree, a parent loop), the owner of a frame's cue (the cue or its nearest owned
 * ancestor), the ownership table (claims, a takeover, a full table), and the frame rule without a live slot: an unowned
 * cue runs as it was, an owned one inside its own slot's scope continues, inside another slot's scope is a conflict, and a
 * slot without a player refuses (counted, never a guessed scope). The calls' scope refuses without arming, a slot out of
 * range and a slot without a player. */
static unsigned char *tmo_cue(unsigned char *at,uintptr_t vt,uint64_t id,const void *parent) {
    memset(at,0,0x200);memcpy(at,&vt,8);memcpy(at+MPP_CUE_ID,&id,8);memcpy(at+MPP_CUE_PARENT,&parent,8);return at;
}
static unsigned char *test_mpp_live;static uint64_t test_mpp_live_id;
static void *test_mpp_find(void *map,uint64_t id,unsigned flags) {(void)map;(void)flags;return id==test_mpp_live_id?test_mpp_live:NULL;}
static void test_mission_owners(void) {
    _Alignas(16) static unsigned char cues[8][0x200];
    const uintptr_t vt=(uintptr_t)image+SCRIPT_RNG_CUE_VT;
    unsigned char *script=tmo_cue(cues[0],vt,0x10,NULL),*mission=tmo_cue(cues[1],vt,0x1615a0,script),
                  *sub=tmo_cue(cues[2],vt,0x1615a1,mission),*subsub=tmo_cue(cues[3],vt,0x1615a2,sub),
                  *other=tmo_cue(cues[4],vt,0x1615b7,script),*notcue=tmo_cue(cues[5],vt+8,0x1615a0,NULL),
                  *loop=tmo_cue(cues[6],vt,0x1615c0,cues[6]);
    /* the owner of a cue */
    mpp_count=0;mpp_claims=mpp_full=0;
    assert(!mpp_slot_of(mission) && !mpp_slot_of(NULL));
    assert(mpp_claim_id(2,0x1615a0));
    assert(mpp_slot_of(mission)==2 && mpp_slot_of(sub)==2 && mpp_slot_of(subsub)==2);      /* the cue and its descendants */
    assert(!mpp_slot_of(script) && !mpp_slot_of(other) && !mpp_slot_of(notcue) && !mpp_slot_of(loop));   /* ancestors, others */
    assert(mpp_claim_id(3,0x1615a2) && mpp_slot_of(subsub)==3 && mpp_slot_of(sub)==2);      /* the nearest owner */
    /* the table */
    assert(mpp_claim_id(4,0x1615a0) && mpp_slot_of(sub)==4 && mpp_count==2 && mpp_claims==3);   /* a takeover, one entry */
    const uint64_t hash=mpp_owners_hash();
    assert(mpp_claim_id(2,0x1615a0) && mpp_owners_hash()!=hash);
    for(unsigned i=mpp_count;i<MPP_OWNERS;i++) assert(mpp_claim_id(2,0x900000+i));
    assert(!mpp_claim_id(2,0x7fffff) && mpp_full==1 && mpp_count==MPP_OWNERS);
    /* the frame rule (no live slot here: the scope itself is the pilot slots' own tested code) */
    mpp_count=0;mpp_claim_id(2,0x1615a0);
    const unsigned scope=pstk_scope_slot;unsigned char *const player=pslots[2].player;
    mpp_armed=0;assert(!mpp_frame_enter(sub));                                     /* not armed: nothing */
    mpp_armed=1;mpp_frames=mpp_inside=mpp_conflicts=mpp_refused=mpp_entered=0;
    assert(!mpp_frame_enter(other) && !mpp_frame_enter(script) && mpp_frames==0);  /* unowned: as it was */
    pstk_scope_slot=2;{unsigned c=mpp_frame_enter(subsub);assert(c==MPP_FRAME_OWNED && mpp_inside==1);mpp_frame_leave(c);}           /* already in slot 2's scope */
    pstk_scope_slot=3;assert(!mpp_frame_enter(mission) && mpp_conflicts==1);       /* another slot's scope */
    pstk_scope_slot=0;pslots[2].player=NULL;assert(!mpp_frame_enter(sub) && mpp_refused==1 && mpp_frames==3 && !mpp_entered);
    assert(!mpp_frame_enter(notcue) && mpp_frames==3);
    /* the calls' scope */
    mpp_calls=mpp_call_refused=0;
    assert(!mpp_call_enter(2) && mpp_call_refused==1);                             /* no player */
    assert(!mpp_call_enter(1) && !mpp_call_enter(PILOT_SLOTS+1) && mpp_call_refused==3);
    mpp_armed=0;assert(!mpp_call_enter(2) && mpp_call_refused==4 && !mpp_calls);
    assert(!mpp_claim(2,0x1615a0));                                                /* not armed: no claim */
    {char out[768];mpp_armed=1;assert(mpp_status(out,sizeof(out))>0 && strstr(out,"armed=1 owners=1 ") && strstr(out," refused=1 "));}
    /* Slot 1's claim: an owner for the lists, never a scope. */
    {   mpp_armed=1;mpp_count=0;mpp_claim_id(1,0x1615a0);const LONG64 frames=mpp_frames;
        assert(mpp_slot_of(sub)==1 && !mpp_frame_enter(sub) && mpp_frames==frames);
        mpp_count=0;}
    /* The active mission per slot: a planted manager with three missions (IDs 0x51, 0x52, 0x53), slot 1's active one 0x52. */
    {   static _Alignas(16) unsigned char manager[0x400],list[UIMIS_MISSION_SIZE*3];
        unsigned char *const base=pnat_base;uint64_t kept=0;void *m=manager;uint64_t v;
        memset(manager,0,sizeof(manager));memset(list,0,sizeof(list));
        pnat_base=image;memcpy(&kept,image+UIMIS_MISSION_MANAGER,8);memcpy(image+UIMIS_MISSION_MANAGER,&m,8);
        for(unsigned i=0;i<3;i++) {v=0x51u+i;memcpy(list+i*UIMIS_MISSION_SIZE,&v,8);}
        {uintptr_t range[2]={(uintptr_t)list,(uintptr_t)list+sizeof(list)};memcpy(manager+UIMIS_MISSIONS,range,16);}
        v=0x52;memcpy(manager+MPP_MGR_ACTIVE,&v,8);v=1;memcpy(manager+MPP_MGR_ACTIVE_INDEX,&v,8);
#define TMO_ACTIVE(id,index) (!memcmp(manager+MPP_MGR_ACTIVE,&(uint64_t){id},8) && !memcmp(manager+MPP_MGR_ACTIVE_INDEX,&(uint64_t){index},8))
        mpp_armed=1;memset(mpp_active,0,sizeof(mpp_active));mpp_active_open=0;mpp_active_gone=mpp_active_shown=mpp_active_shown_changed=0;
        mpp_active_enter(1);assert(!mpp_active_open && TMO_ACTIVE(0x52,1));               /* slot 1: the canonical one itself */
        mpp_active_enter(2);assert(mpp_active_open==2 && TMO_ACTIVE(0,0));                 /* slot 2 has none yet */
        mpp_active_enter(3);assert(mpp_active_open==2);                                    /* one scope at a time */
        v=0x53;memcpy(manager+MPP_MGR_ACTIVE,&v,8);v=2;memcpy(manager+MPP_MGR_ACTIVE_INDEX,&v,8);   /* slot 2's SetActiveMission */
        mpp_active_leave(3,1);assert(mpp_active_open==2);
        mpp_active_leave(2,1);assert(!mpp_active_open && mpp_active[2]==0x53 && TMO_ACTIVE(0x52,1));
        /* The vector moved (mission 0x51 ended): the indices are found again by ID. */
        memmove(list,list+UIMIS_MISSION_SIZE,2*UIMIS_MISSION_SIZE);
        {uintptr_t range[2]={(uintptr_t)list,(uintptr_t)list+2*UIMIS_MISSION_SIZE};memcpy(manager+UIMIS_MISSIONS,range,16);}
        v=0;memcpy(manager+MPP_MGR_ACTIVE_INDEX,&v,8);                                     /* X4's own fix-up of the canonical index */
        mpp_active_enter(2);assert(TMO_ACTIVE(0x53,1));
        mpp_active_leave(2,1);assert(TMO_ACTIVE(0x52,0) && mpp_active[2]==0x53);
        /* Client 2's presentation: its own active mission shown, nothing kept; a change there is counted. */
        mpp_active_enter(2);assert(TMO_ACTIVE(0x53,1));
        mpp_active_leave(2,0);assert(TMO_ACTIVE(0x52,0) && mpp_active[2]==0x53 && mpp_active_shown==1 && !mpp_active_shown_changed);
        mpp_active_enter(2);v=0x52;memcpy(manager+MPP_MGR_ACTIVE,&v,8);
        mpp_active_leave(2,0);assert(mpp_active[2]==0x53 && mpp_active_shown_changed==1 && TMO_ACTIVE(0x52,0));
        /* Slot 2's active mission ended outside its scope: none at its next scope, counted. */
        {uintptr_t range[2]={(uintptr_t)list,(uintptr_t)list+UIMIS_MISSION_SIZE};memcpy(manager+UIMIS_MISSIONS,range,16);}
        mpp_active_enter(2);assert(TMO_ACTIVE(0,0) && mpp_active_gone==1);
        mpp_active_leave(2,1);assert(!mpp_active[2] && TMO_ACTIVE(0x52,0));
        /* Not armed: nothing moves. */
        mpp_armed=0;mpp_active[2]=0x52;mpp_active_enter(2);assert(!mpp_active_open && TMO_ACTIVE(0x52,0));mpp_active[2]=0;
#undef TMO_ACTIVE
        memcpy(image+UIMIS_MISSION_MANAGER,&kept,8);pnat_base=base;}
    /* Offers per slot, and helpers: a root instance made while slot 2's offers are generated, or under a frame of a cue
     * slot 2 owns, is slot 2's from its construction; one made elsewhere, in another scope or off the frame thread is
     * nobody's. The frames' answers keep the depth of owned frames. */
    {   static _Alignas(16) unsigned char roots[4][0x200],child[0x200];
        const unsigned scope_was=pstk_scope_slot;const DWORD thread_was=ui_thread;
        for(unsigned i=0;i<4;i++) tmo_cue(roots[i],vt,0x77700u+i,NULL);
        tmo_cue(child,vt,0x77710,roots[0]);
        ui_thread=GetCurrentThreadId();
        mpp_armed=1;mpp_count=0;mpp_generating=0;mpp_depth=0;mpp_generations=mpp_generated=mpp_created=0;
        pstk_scope_slot=3;assert(!mpp_generate(2) && !mpp_generating);             /* only inside slot 2's scope */
        pstk_scope_slot=2;assert(!mpp_generate(1) && !mpp_generate(PILOT_SLOTS+1));
        mpp_created_root(roots[0]);assert(!mpp_count && !mpp_created);             /* slot 2's scope, but not its own work */
        assert(mpp_generate(2) && mpp_generating==2 && mpp_generations==1);
        mpp_created_root(roots[0]);assert(mpp_owner_of(0x77700)==2 && mpp_generated==1 && mpp_created==1);   /* the generation's */
        mpp_claim_id(3,0x77702);mpp_created_root(roots[2]);assert(mpp_owner_of(0x77702)==3 && mpp_generated==1);   /* owned: kept */
        ui_thread=GetCurrentThreadId()+1;mpp_created_root(roots[1]);assert(!mpp_owner_of(0x77701));       /* another thread */
        ui_thread=GetCurrentThreadId();
        assert(mpp_generate(0) && !mpp_generating);
        mpp_created_root(roots[1]);assert(!mpp_owner_of(0x77701));                 /* the generation is over */
        /* a later frame of the claimed selection: the scope's own work again, by the depth of owned frames */
        unsigned code=mpp_frame_enter(child);assert(code==MPP_FRAME_OWNED && mpp_depth==1);
        mpp_created_root(roots[1]);assert(mpp_owner_of(0x77701)==2 && mpp_generated==2);
        {unsigned inner=mpp_frame_enter(roots[1]);assert(inner==MPP_FRAME_OWNED && mpp_depth==2);mpp_frame_leave(inner);}
        mpp_frame_leave(code);assert(!mpp_depth);
        mpp_frame_leave(MPP_FRAME_OWNED);assert(!mpp_depth);                       /* never below zero */
        mpp_created_root(roots[3]);assert(!mpp_owner_of(0x77703));
        /* an instance already linked under an owned tree needs no claim of its own */
        {const unsigned count=mpp_count;code=mpp_frame_enter(roots[0]);mpp_created_root(child);
         assert(mpp_count==count && !mpp_owner_of(0x77710) && mpp_slot_of(child)==2);mpp_frame_leave(code);}
        /* A start's story: gate.mpp_story(k, path) inside slot k's scope; the next frame of the cue with that path (the script
         * RNG's cue identity of the frame) claims it for slot k, once; another cue's frame, another scope and an owned cue
         * do not answer; the call's end drops an ask nobody answered. */
        {   script_rng_frame frame,*const current_was=script_rng_current;memset(&frame,0,sizeof(frame));
            const uint64_t key=cue_fnv(CUE_FNV_BASIS,"GS_Terran1.Start",16);
            assert(!mpp_story(1,"GS_Terran1.Start") && !mpp_story(2,"") && !mpp_story(2,NULL) && !mpp_story(2,"a b") && !mpp_story(2,"a%20b"));
            pstk_scope_slot=3;assert(!mpp_story(2,"GS_Terran1.Start"));pstk_scope_slot=2;
            assert(mpp_story(0,NULL)==1);                                          /* nothing asked: nothing lost */
            assert(mpp_story(2,"GS_Terran1.Start") && mpp_story_slot==2 && mpp_story_key==key && mpp_story_asked==1);
            script_rng_current=&frame;frame.id.owner=key^1u;
            {unsigned c=mpp_frame_enter(roots[3]);assert(!c && !mpp_owner_of(0x77703) && mpp_story_slot==2);mpp_frame_leave(c);}   /* another cue */
            frame.id.owner=key;pstk_scope_slot=3;
            {unsigned c=mpp_frame_enter(roots[3]);assert(!c && !mpp_owner_of(0x77703) && mpp_story_slot==2);mpp_frame_leave(c);}   /* another scope */
            pstk_scope_slot=2;script_rng_current=NULL;
            {unsigned c=mpp_frame_enter(roots[3]);assert(!c && mpp_story_slot==2);mpp_frame_leave(c);}                          /* no frame identity */
            script_rng_current=&frame;
            {unsigned c=mpp_frame_enter(roots[3]);                                  /* the cue: claimed, and this very frame is slot 2's */
             assert(c==MPP_FRAME_OWNED && mpp_owner_of(0x77703)==2 && !mpp_story_slot && mpp_story_claimed==1 && mpp_depth==1);mpp_frame_leave(c);}
            assert(mpp_story(0,NULL)==1);                                          /* answered */
            assert(mpp_story(2,"GS_Terran1.Start"));
            {unsigned c=mpp_frame_enter(roots[2]);assert(mpp_owner_of(0x77702)==3 && !mpp_story_slot && mpp_story_claimed==1);mpp_frame_leave(c);}  /* slot 3's stays slot 3's */
            assert(mpp_story(0,NULL)==0);                                          /* ...and the ask claimed nothing */
            assert(mpp_story(2,"X.Y") && mpp_story(0,NULL)==0 && !mpp_story_slot);  /* no frame of that cue came */
            assert(mpp_story(0,NULL)==1);
            script_rng_current=current_was;mpp_story_slot=0;mpp_story_key=0;mpp_story_missed=0;mpp_story_asked=mpp_story_claimed=0;
            for(unsigned i=0;i<mpp_count;i++) if(mpp_owners[i].cue==0x77703) {mpp_owners[i]=mpp_owners[--mpp_count];break;}
        }
        /* another slot's scope with slot 2's generation left open (a call that failed): nothing */
        mpp_generating=2;pstk_scope_slot=3;mpp_created_root(roots[3]);assert(!mpp_owner_of(0x77703));
        pstk_scope_slot=0;mpp_created_root(roots[3]);assert(!mpp_owner_of(0x77703));
        mpp_generating=0;ui_thread=thread_was;
        pstk_scope_slot=scope_was;mpp_armed=0;mpp_count=0;mpp_generations=mpp_generated=mpp_created=0;
        mpp_frames=mpp_inside=mpp_conflicts=0;
    }
    /* A full table drops the claims of cues that are gone (with the engine's map at hand) and takes the new one. */
    {   static _Alignas(16) unsigned char live[0x200];const uirep_fns fns_was=uirep_fn;unsigned char *const base=pnat_base;uint64_t kept=0;
        tmo_cue(live,vt,0x88801,NULL);
        test_mpp_live=live;test_mpp_live_id=0x88801;uirep_fn.find=test_mpp_find;
        pnat_base=image;memcpy(&kept,image+UIREP_MAP,8);{void *token=&test_mpp_live;memcpy(image+UIREP_MAP,&token,8);}
        mpp_armed=1;mpp_count=0;mpp_full=mpp_pruned=0;
        assert(mpp_claim_id(2,0x88801));
        for(unsigned i=mpp_count;i<MPP_OWNERS;i++) assert(mpp_claim_id(3,0x990000+i));
        assert(mpp_claim_id(4,0x88802) && mpp_count==2 && mpp_pruned==MPP_OWNERS-1 && !mpp_full &&
               mpp_owner_of(0x88801)==2 && mpp_owner_of(0x88802)==4);
        memcpy(image+UIREP_MAP,&kept,8);pnat_base=base;uirep_fn=fns_was;
        mpp_armed=0;mpp_count=0;mpp_claims=mpp_pruned=0;
    }
    /* The join state: the table and the active missions as one line, a joiner's restore in the same order (the same hash
     * and owners); refusals. */
    {   static char line[MPP_OWNERS*24+256];mpp_armed=1;mpp_count=0;
        assert(mpp_state(line,sizeof(line))==4 && !strcmp(line,"v2||"));
        mpp_claim_id(2,0x1615a0);mpp_claim_id(1,0x1615b7);mpp_claim_id(8,0xffffffffffffffffULL);mpp_active[3]=0x1615b7;mpp_active[8]=0x77;
        const uint64_t h=mpp_owners_hash();
        assert(mpp_state(line,sizeof(line))>0 && !strcmp(line,"v2|2:1615a0,1:1615b7,8:ffffffffffffffff|3:1615b7,8:77"));
        {char small[10];assert(!mpp_state(small,sizeof(small)));}
        assert(!mpp_restore(line,strlen(line)));                                   /* a table already there */
        mpp_count=0;assert(!mpp_restore(line,strlen(line)));                       /* active missions already there */
        memset(mpp_active,0,sizeof(mpp_active));
        assert(mpp_restore(line,strlen(line)) && mpp_count==3 && mpp_active[3]==0x1615b7 && mpp_active[8]==0x77 && mpp_owners_hash()==h &&
               mpp_slot_of(sub)==2);
        static const char *const bad[]={"v1|2:1","v2|2:1","v2|0:5|","v2|9:5|","v2|2:0|","v2|2:|","v2|2:5,|","v2|2:5,2:5|","v2|2:5;3:6|",
                                        "v2|2:12345678901234567|","v2|:5|","v2|2:5x|","v2","v2|","v2||1:5","v2||2:5,2:6","v2||2:5,","v2||2:5|"};
        for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++) {
            mpp_count=0;memset(mpp_active,0,sizeof(mpp_active));
            assert(!mpp_restore(bad[i],strlen(bad[i])) && !mpp_count && !mpp_active[2]);
        }
        mpp_count=0;assert(mpp_restore("v2||",4) && !mpp_count);
        assert(mpp_restore("v2||2:5",7) && mpp_active[2]==5);memset(mpp_active,0,sizeof(mpp_active));
        mpp_armed=0;assert(!mpp_restore("v2||",4));}
    pstk_scope_slot=scope;pslots[2].player=player;mpp_armed=0;mpp_count=0;
    mpp_claims=mpp_claim_gone=mpp_full=mpp_frames=mpp_entered=mpp_inside=mpp_conflicts=mpp_refused=mpp_leave_failed=mpp_calls=mpp_call_refused=0;
    mpp_active_swaps=mpp_active_gone=mpp_active_shown=mpp_active_shown_changed=0;memset(mpp_active,0,sizeof(mpp_active));mpp_active_open=0;
    printf("missions per player: a cue's owner (descendants, nearest, not ancestors, a parent loop), the ownership table "
           "(takeover, full), the frame rule (unowned, inside, conflict, refused), the calls' refusals, slot 1's claims, the active mission per slot, offers per slot and helpers (root instances at their construction, depth, pruning), the join state PASS\n");
}
/* The pinned image: the instance constructor's head and its two calls without a parent (the root instances). */
static void test_mission_owners_native_image(const char *path) {
    unsigned char *prior=image;
    image=test_pmath_map(path);
    assert(mpp_create_pins());
    {int32_t rel=0;assert(image[MPP_CREATE+0x65u]==0xe8);memcpy(&rel,image+MPP_CREATE+0x66u,4);assert(MPP_CREATE+0x6au+(uint32_t)rel==0xcaab00u);}   /* its ID */
    /* the other two callers pass a parent instance in r8 (mov r8,rbp / mov r8,rdi): nested instances, not hooked */
    {static const unsigned char rbp[3]={0x4c,0x8b,0xc5},rdi[3]={0x4c,0x8b,0xc7};
     assert(!memcmp(image+0x111dc7au,rbp,3) && image[0x111dc84u]==0xe8 && !memcmp(image+0x1114b98u,rdi,3) && image[0x1114ba1u]==0xe8);}
    image=prior;
    printf("pinned native image mission owners verified (the cue instance constructor, its two root calls)\n");
}
