/* Personal missions per player (lockstep_mission_owners.h): planted MD cues (a script's static cue, a mission cue under it,
 * its sub-cue and a sub-sub-cue, another tree, a parent loop), the owner of a frame's cue (the cue or its nearest owned
 * ancestor), the ownership table (claims, a takeover, a full table), and the frame rule without a live slot: an unowned
 * cue runs as it was, an owned one inside its own slot's scope continues, inside another slot's scope is a conflict, and a
 * slot without a player refuses (counted, never a guessed scope). The calls' scope refuses without arming, a slot out of
 * range and a slot without a player. */
static unsigned char *tmo_cue(unsigned char *at,uintptr_t vt,uint64_t id,const void *parent) {
    memset(at,0,0x200);memcpy(at,&vt,8);memcpy(at+MPP_CUE_ID,&id,8);memcpy(at+MPP_CUE_PARENT,&parent,8);return at;
}
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
    pstk_scope_slot=2;assert(!mpp_frame_enter(subsub) && mpp_inside==1);           /* already in slot 2's scope */
    pstk_scope_slot=3;assert(!mpp_frame_enter(mission) && mpp_conflicts==1);       /* another slot's scope */
    pstk_scope_slot=0;pslots[2].player=NULL;assert(!mpp_frame_enter(sub) && mpp_refused==1 && mpp_frames==3 && !mpp_entered);
    assert(!mpp_frame_enter(notcue) && mpp_frames==3);
    /* the calls' scope */
    mpp_calls=mpp_call_refused=0;
    assert(!mpp_call_enter(2) && mpp_call_refused==1);                             /* no player */
    assert(!mpp_call_enter(1) && !mpp_call_enter(PILOT_SLOTS+1) && mpp_call_refused==3);
    mpp_armed=0;assert(!mpp_call_enter(2) && mpp_call_refused==4 && !mpp_calls);
    assert(!mpp_claim(2,0x1615a0));                                                /* not armed: no claim */
    {char out[512];mpp_armed=1;assert(mpp_status(out,sizeof(out))>0 && strstr(out,"armed=1 owners=1 ") && strstr(out," refused=1 "));}
    /* The join state: the table as one line, a joiner's restore in the same order (the same hash and owners); refusals. */
    {   static char line[MPP_OWNERS*24+8];mpp_armed=1;mpp_count=0;
        assert(mpp_state(line,sizeof(line))==3 && !strcmp(line,"v1|"));
        mpp_claim_id(2,0x1615a0);mpp_claim_id(3,0x1615b7);mpp_claim_id(8,0xffffffffffffffffULL);
        const uint64_t h=mpp_owners_hash();
        assert(mpp_state(line,sizeof(line))>0 && !strcmp(line,"v1|2:1615a0,3:1615b7,8:ffffffffffffffff"));
        {char small[10];assert(!mpp_state(small,sizeof(small)));}
        assert(!mpp_restore(line,strlen(line)));                                   /* a table already there */
        mpp_count=0;assert(mpp_restore(line,strlen(line)) && mpp_count==3 && mpp_owners_hash()==h && mpp_slot_of(sub)==2);
        static const char *const bad[]={"v2|2:1","v1|1:5","v1|9:5","v1|2:0","v1|2:","v1|2:5,","v1|2:5,2:5","v1|2:5;3:6",
                                        "v1|2:12345678901234567","v1|:5","v1|2:5x","v1"};
        for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++) {mpp_count=0;assert(!mpp_restore(bad[i],strlen(bad[i])) && !mpp_count);}
        mpp_count=0;assert(mpp_restore("v1|",3) && !mpp_count);
        mpp_armed=0;assert(!mpp_restore("v1|",3));}
    pstk_scope_slot=scope;pslots[2].player=player;mpp_armed=0;mpp_count=0;
    mpp_claims=mpp_claim_gone=mpp_full=mpp_frames=mpp_entered=mpp_inside=mpp_conflicts=mpp_refused=mpp_leave_failed=mpp_calls=mpp_call_refused=0;
    printf("missions per player: a cue's owner (descendants, nearest, not ancestors, a parent loop), the ownership table "
           "(takeover, full), the frame rule (unowned, inside, conflict, refused), the calls' refusals, the join state PASS\n");
}
