/* native_gate_speech_local: the decision (catch-up, another sector, unseated), the site's CAS both ways, arm and restore. */
static int test_spl_catching;
static int test_spl_catching_up(void) {return test_spl_catching;}
static int test_spl_is_a(void *p,int klass) {return klass==0x58 && ((unsigned char*)p)[0x200]==0x58;}
static int test_spl_next_seen;
static void test_spl_next(void *data) {(void)data;test_spl_next_seen=spl_muted;}   /* the mute state the line's sound start finds */
static unsigned char test_spl_option_value=1,test_spl_entity[0x20];
static unsigned char test_spl_option(void *config,const char *key,unsigned char fallback) {(void)config;(void)key;(void)fallback;return test_spl_option_value;}
static void *test_spl_speaker(void *player,void **out) {*out=player?test_spl_entity:NULL;return out;}
static void *test_spl_player_at(void) {return test_spl_entity;}
static unsigned test_spl_queue_calls;
static unsigned char test_spl_queue(void *data,void *lines,int priority,unsigned char interrupt,void *recipient,void *source,unsigned flags,void *text) {
    ++test_spl_queue_calls;
    return data && lines==(void*)(uintptr_t)2 && priority==-7 && interrupt==1 && recipient==(void*)(uintptr_t)5 && source==(void*)(uintptr_t)6 &&
        flags==0x8u && text==(void*)(uintptr_t)9;   /* every argument as it came */
}
static void test_speech_local(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active,saved_slot=stats.slot;
    unsigned char *saved_p1=pslot_p1;pslot_slot saved=pslots[2];
    static void *vt[0x11e8/8+1];vt[0x11e8/8]=(void*)test_spl_is_a;
    static unsigned char sector_a[0x300],sector_b[0x300],ship1[0x300],room1[0x300],player1[0x300],ship2[0x300];
    unsigned char *all[]={sector_a,sector_b,ship1,room1,player1,ship2};
    for(unsigned i=0;i<6;i++) {memset(all[i],0,0x300);*(void ***)all[i]=vt;}
    sector_a[0x200]=sector_b[0x200]=0x58;
    *(unsigned char **)(ship1+0x70)=sector_a;*(unsigned char **)(room1+0x70)=ship1;*(unsigned char **)(player1+0x70)=room1;
    *(unsigned char **)(ship2+0x70)=sector_b;
    assert(spl_sector_of(player1)==sector_a && spl_sector_of(ship2)==sector_b && spl_sector_of(sector_a)==sector_a && !spl_sector_of(NULL));
    const uint16_t original=SPEAK_MUTE_ORIGINAL;memcpy(image+SPEAK_MUTE_SITE,&original,2);
    spl_armed=spl_muted=0;spl_catching_up=test_spl_catching_up;test_spl_catching=0;
    stats.active=1;stats.issued=0;ui_thread=GetCurrentThreadId();stats.slot=2;pslot_p1=player1;
    memset(&pslots[2],0,sizeof(pslots[2]));pslots[2].ship=ship2;pslots[2].seated=1;
    /* Arming needs the speak timer in place and the native je. */
    speak_timer_installed=speak_timer_patched=0;assert(!spl_arm() && !strcmp(spl_stage,"refused") && !spl_frame());
    speak_timer_installed=speak_timer_patched=1;
    {const uint16_t foreign=0xcccc;memcpy(image+SPEAK_MUTE_SITE,&foreign,2);assert(!spl_arm() && !strcmp(spl_stage,"site"));memcpy(image+SPEAK_MUTE_SITE,&original,2);}
    assert(spl_arm() && spl_armed && spl_arm());
    /* Another sector than slot 1's: muted; the same sector: the native je again. */
    assert(spl_frame()==1 && speak_mute_site()==SPEAK_MUTE_PATCHED && spl_far==1 && spl_toggles==1);
    assert(spl_frame()==1 && spl_toggles==1);
    *(unsigned char **)(ship2+0x70)=sector_a;assert(spl_frame()==0 && speak_mute_site()==SPEAK_MUTE_ORIGINAL && spl_near==1 && spl_toggles==2);
    /* A joiner's catch-up mutes whatever the sectors say; its end gives the sectors' answer. */
    test_spl_catching=1;assert(spl_frame()==1 && spl_catchup==1 && speak_mute_site()==SPEAK_MUTE_PATCHED);
    test_spl_catching=0;assert(spl_frame()==0 && speak_mute_site()==SPEAK_MUTE_ORIGINAL);
    /* An unseated or walking slot keeps the last answer; slot 1's client never mutes outside a catch-up. */
    *(unsigned char **)(ship2+0x70)=sector_b;assert(spl_frame()==1);
    pslots[2].seated=0;*(unsigned char **)(ship2+0x70)=sector_a;assert(spl_frame()==1 && spl_unknown==1);
    pslots[2].seated=1;pslots[2].walking=1;assert(spl_frame()==1 && spl_unknown==2);pslots[2].walking=0;
    assert(spl_frame()==0);
    *(unsigned char **)(ship2+0x70)=sector_b;stats.slot=1;assert(spl_frame()==0);stats.slot=2;assert(spl_frame()==1);
    /* An owned MD frame (lockstep_mission_owners.h's frame rule): slot 2's line plays on client 2 while the frame runs, wherever
     * slot 1 is, and the step's answer is back at its end; a nested frame changes nothing; another slot's frame (slot 1's
     * too) and a catch-up do not play; nobody is no owner. */
    {const uint64_t toggles=spl_toggles;
     spl_owner_enter(2);assert(!spl_muted && speak_mute_site()==SPEAK_MUTE_ORIGINAL && spl_owner_depth==1 && spl_owner_played==1);
     spl_owner_enter(2);spl_owner_leave();assert(!spl_muted && spl_owner_depth==1);
     spl_owner_leave();assert(spl_muted==1 && speak_mute_site()==SPEAK_MUTE_PATCHED && !spl_owner_depth && spl_toggles==toggles+2);
     spl_owner_leave();assert(spl_muted==1 && !spl_owner_depth);   /* an unpaired leave */
     spl_owner_enter(3);assert(spl_muted==1 && spl_owner_muted==1 && spl_toggles==toggles+2);spl_owner_leave();
     test_spl_catching=1;spl_owner_enter(2);assert(spl_muted==1 && spl_owner_muted==2);spl_owner_leave();test_spl_catching=0;
     spl_owner_enter(0);spl_owner_enter(PILOT_SLOTS+1);assert(!spl_owner_depth && spl_owner_frames==3);
     spl_owner_enter(1);assert(spl_muted==1 && spl_owner_depth==1 && spl_owner_muted==3 && spl_owner_frames==4);spl_owner_leave();assert(!spl_owner_depth);
     /* Slot 1's client plays by default and does not play slot 2's line. */
     stats.slot=1;assert(spl_frame()==0 && speak_mute_site()==SPEAK_MUTE_ORIGINAL);
     spl_owner_enter(2);assert(spl_muted==1 && speak_mute_site()==SPEAK_MUTE_PATCHED);spl_owner_leave();
     assert(!spl_muted && speak_mute_site()==SPEAK_MUTE_ORIGINAL);
     spl_owner_enter(1);assert(!spl_muted && spl_owner_depth==1);spl_owner_leave();   /* its own mission's line */
     stats.slot=2;assert(spl_frame()==1);
     assert(spl_status((char[320]){0},320)>0);}
    /* The target readout: the option check notes the player's speaker (by its ID) for the scope's slot (slot 1 outside
     * every scope) when the option is on; that speaker's next line is voiced on that slot's client only, the step's
     * answer back after it; another speaker is nobody's. */
    {   static unsigned char speaker[0x40],other[0x40];uint64_t id=0x1234,id2=0x77;const unsigned scope=pstk_scope_slot;
        memcpy(test_spl_entity+8,&id,8);memcpy(speaker+8,&id,8);memcpy(other+8,&id2,8);
        spl_option_original=test_spl_option;spl_speaker_original=test_spl_speaker;spl_player=test_spl_player_at;spl_next_original=test_spl_next;test_spl_next_seen=-1;
        spl_lines_noted=spl_lines_played=spl_lines_muted=0;memset(spl_speakers,0,sizeof(spl_speakers));
        assert(stats.slot==2 && spl_muted==1);
        test_spl_option_value=0;pstk_scope_slot=2;assert(!spl_option(NULL,"k",1) && !spl_speaker_owner(speaker) && !spl_lines_noted);   /* the option is off */
        test_spl_option_value=1;assert(spl_option(NULL,"k",1)==1 && spl_speaker_owner(speaker)==2 && !spl_speaker_owner(other) && spl_lines_noted==1);
        spl_next(speaker);assert(test_spl_next_seen==0 && spl_muted==1 && spl_lines_played==1);   /* its own slot's: voiced */
        spl_next(other);assert(test_spl_next_seen==1 && spl_lines_played==1);                     /* not noted: the step's answer */
        pstk_scope_slot=0;(void)spl_option(NULL,"k",1);assert(spl_speaker_owner(speaker)==1);    /* slot 1's selection */
        assert(spl_set(0));spl_next(speaker);assert(test_spl_next_seen==1 && !spl_muted && spl_lines_muted==1);   /* muted here, the step's answer back */
        test_spl_catching=1;pstk_scope_slot=2;(void)spl_option(NULL,"k",1);spl_next(speaker);assert(test_spl_next_seen==1 && spl_lines_muted==2);test_spl_catching=0;
        assert(spl_set(1));pstk_scope_slot=scope;spl_option_original=NULL;spl_speaker_original=NULL;spl_player=NULL;spl_next_original=NULL;memset(spl_speakers,0,sizeof(spl_speakers));
    }
    /* A request to speak made in a player's own cue notes the speaker for that player: the lines its queue starts later, in
     * nobody's frame, are voiced on that player's client only. A request in nobody's frame leaves the note while the
     * speaker speaks and takes it away once it is silent; the readout's note is not a cue's. */
    {   static unsigned char data[0x40];const uint64_t id=0x4242;const unsigned scope=pstk_scope_slot,depth=mpp_depth,depth1=mpp_depth1;
        memset(data,0,sizeof(data));memcpy(data+8,&id,8);memset(spl_speakers,0,sizeof(spl_speakers));spl_cues_noted=spl_cues_cleared=0;
        spl_next_original=test_spl_next;test_spl_next_seen=-1;
        assert(stats.slot==2 && spl_muted==1);
        pstk_scope_slot=0;mpp_depth=0;mpp_depth1=0;spl_queue_note(data);assert(!spl_speaker_owner(data) && !spl_cues_noted);
        pstk_scope_slot=2;mpp_depth=1;spl_queue_note(data);assert(spl_speaker_owner(data)==2 && spl_cues_noted==1);
        pstk_scope_slot=0;mpp_depth=0;
        {const int state=4;memcpy(data+0x10,&state,4);spl_queue_note(data);assert(spl_speaker_owner(data)==2 && !spl_cues_cleared);}
        spl_next(data);assert(test_spl_next_seen==0 && spl_muted==1);                                  /* a queued line on client 2: voiced */
        stats.slot=1;assert(spl_set(0));spl_next(data);assert(test_spl_next_seen==1 && !spl_muted);    /* on client 1: not */
        stats.slot=2;assert(spl_set(1));
        {const int state=0;memcpy(data+0x10,&state,4);spl_queue_note(data);assert(!spl_speaker_owner(data) && spl_cues_cleared==1);}
        spl_next(data);assert(test_spl_next_seen==1);                                                  /* nobody's again: the step's answer */
        mpp_depth1=1;spl_queue_note(data);assert(spl_speaker_owner(data)==1);mpp_depth1=0;            /* a cue of slot 1's own */
        memset(spl_speakers,0,sizeof(spl_speakers));spl_speaker_note(id,3,SPL_KIND_READOUT);spl_queue_note(data);assert(spl_speaker_owner(data)==3);
        spl_armed=0;memset(spl_speakers,0,sizeof(spl_speakers));pstk_scope_slot=2;mpp_depth=1;spl_queue_note(data);assert(!spl_speaker_owner(data));spl_armed=1;
        /* The request's entry: the note first, then X4's own request with its eight arguments and its answer. */
        spl_queue_original=test_spl_queue;test_spl_queue_calls=0;
        assert(spl_queue(data,(void*)(uintptr_t)2,-7,1,(void*)(uintptr_t)5,(void*)(uintptr_t)6,0x8u,(void*)(uintptr_t)9)==1 && test_spl_queue_calls==1 &&
            spl_speaker_owner(data)==2);
        spl_queue_original=NULL;
        pstk_scope_slot=scope;mpp_depth=depth;mpp_depth1=depth1;spl_next_original=NULL;memset(spl_speakers,0,sizeof(spl_speakers));
    }
    /* A site that holds neither value: the speak timer's failure; nothing else moves. */
    {const uint16_t foreign=0xcccc;memcpy(image+SPEAK_MUTE_SITE,&foreign,2);*(unsigned char **)(ship2+0x70)=sector_a;speak_timer_failures=0;
     assert(spl_frame()==1 && speak_timer_failures==1 && !strcmp(speak_timer_stage,"mute_site") && speak_mute_site()==foreign);
     const uint16_t patched=SPEAK_MUTE_PATCHED;memcpy(image+SPEAK_MUTE_SITE,&patched,2);speak_timer_failures=0;*(unsigned char **)(ship2+0x70)=sector_b;}
    char status[320];assert(spl_status(status,sizeof(status))>0 && strstr(status,"armed=1 muted=1 site=9090 "));
    /* Stop puts the je back. */
    spl_restore();assert(!spl_armed && !spl_muted && speak_mute_site()==SPEAK_MUTE_ORIGINAL && !spl_frame());
    memset(image+SPEAK_MUTE_SITE,0,2);
    spl_catching_up=join_catching_up;speak_timer_installed=speak_timer_patched=0;speak_timer_failures=0;speak_timer_stage="idle";spl_stage="idle";
    pslots[2]=saved;pslot_p1=saved_p1;stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;stats.slot=saved_slot;
    printf("speech local decision, owned frames, site CAS, arm, failure and restore tests PASS\n");
}
