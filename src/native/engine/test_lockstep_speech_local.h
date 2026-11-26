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
static void *test_splend_data;static unsigned test_splend_calls,test_splend_flag;
static void test_splend_stub(void *data,unsigned char interrupted) {test_splend_data=data;test_splend_flag=interrupted;++test_splend_calls;}   /* X4's 0x562590 */
static void *test_cut_got;static unsigned test_cut_calls;
static unsigned char test_cut_stub(void *data,int priority) {(void)priority;test_cut_got=data;++test_cut_calls;return 1;}   /* X4's 0x562ba0 */
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
    {   static unsigned char data[0x98];const uint64_t id=0x4242;const unsigned scope=pstk_scope_slot,depth=mpp_depth,depth1=mpp_depth1;
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
        /* the rows: the starts above and this request (slot 2's frame in its scope), each once */
        {static char rows[SPL_ROWS*420+16];spl_rows_read=spl_rows_written>SPL_ROWS?spl_rows_written-SPL_ROWS:0u;
         assert(spl_rows_text(rows,sizeof(rows))>0 && strstr(rows,"SPEECH_START tick=") && strstr(rows," owner=2 scope=2 state=") && strstr(rows," result=1 cue=") &&
                strstr(rows,"SPEECH_REQUEST tick=") && !spl_rows_text(rows,sizeof(rows)));}
        spl_queue_original=NULL;
        /* A line's end: its SPEECH_END row (the flag, the state, the line, the clock against its start), then X4's own. */
        spl_finish_original=test_splend_stub;test_splend_calls=0;spl_finish_calls=0;
        {const uint32_t index=1;const double start=65536.5,duration=2.6;memcpy(data+0x50,&index,4);memcpy(data+0x20,&start,8);memcpy(data+0x18,&duration,8);}
        spl_finish(data,1);assert(test_splend_calls==1 && test_splend_data==data && test_splend_flag==1 && spl_finish_calls==1);
        {static char rows[SPL_ROWS*420+16];
         assert(spl_rows_text(rows,sizeof(rows))>0 && strstr(rows,"SPEECH_END tick=") && strstr(rows," speaker=4242 owner=2 ") &&
                strstr(rows," interrupted=1 state=0 line=1/0 ") && strstr(rows," start=40f0000800000000 duration=4004cccccccccccd") && !spl_rows_text(rows,sizeof(rows)));}
        spl_armed=0;spl_finish(data,0);assert(test_splend_calls==2 && !test_splend_flag && spl_finish_calls==1);spl_armed=1;   /* unarmed: X4's alone */
        spl_finish_original=NULL;memset(data+0x18,0,0x40);
        /* A duration that changes under the same start: one SPEECH_DUR row from the frame's scan. */
        {static unsigned char node[0x28],header[0x28];static char rows[SPL_ROWS*420+16];
         memset(node,0,sizeof(node));memset(header,0,sizeof(header));memset(spl_durs,0,sizeof(spl_durs));
         *(unsigned char **)(node+0x20)=data;*(unsigned char **)node=header;*(unsigned char **)(header+8)=node;
         const double start=65540.0,text=4.2,audio=2.833;memcpy(data+0x20,&start,8);memcpy(data+0x18,&text,8);
         spl_dur_changes=0;spl_dur_scan(node,header);assert(!spl_dur_changes);
         memcpy(data+0x18,&audio,8);spl_dur_scan(node,header);assert(spl_dur_changes==1);spl_dur_scan(node,header);assert(spl_dur_changes==1);
         assert(spl_rows_text(rows,sizeof(rows))>0 && strstr(rows,"SPEECH_DUR tick=") && strstr(rows," old=4010cccccccccccd new=4006a9fbe76c8b44 ") && !spl_rows_text(rows,sizeof(rows)));
         memset(data+0x18,0,0x40);memset(spl_durs,0,sizeof(spl_durs));}
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
    /* Subtitles: the picker walks X4's set of text lines in order (left, root, right: nodes +0 parent, +8 left, +0x10 right,
     * +0x20 the speak data) and chooses as X4 does; a speak data whose last start here was muted by this module is left out. */
    {   static _Alignas(16) unsigned char header[0x40],nodes[3][0x40],sds[3][0x80];
        unsigned char *const A=nodes[0],*const B=nodes[1],*const C=nodes[2];void *p;
        memset(header,0,sizeof(header));memset(nodes,0,sizeof(nodes));memset(sds,0,sizeof(sds));
        p=header;memcpy(B,&p,8);p=A;memcpy(B+8,&p,8);p=C;memcpy(B+0x10,&p,8);p=B;memcpy(A,&p,8);memcpy(C,&p,8);memcpy(header+8,&p,8);
        for(unsigned i=0;i<3;i++) {p=sds[i];memcpy(nodes[i]+0x20,&p,8);const uint32_t state=1;memcpy(sds[i]+0x10,&state,4);sds[i][0x5e]=1;}
        int32_t prio[3]={5,9,7};for(unsigned i=0;i<3;i++) memcpy(sds[i]+0x58,&prio[i],4);
        memset(spl_started,0,sizeof(spl_started));spl_pick_hidden=0;
        assert(spl_pick_in(A,header,1)==sds[1] && spl_pick_in(A,header,0)==sds[1]);               /* the highest priority */
        assert(!spl_pick_in(header,header,1));                                                       /* an empty set */
        spl_started_note(sds[1],1);assert(spl_pick_in(A,header,1)==sds[2] && spl_pick_hidden==1);   /* muted here: left out */
        assert(spl_pick_in(A,header,0)==sds[1]);                                                     /* ...not without the filter */
        spl_started_note(sds[1],0);assert(spl_pick_in(A,header,1)==sds[1]);                         /* its next line was heard */
        sds[0][0x5e]=0;assert(spl_pick_in(A,header,1)==sds[0]);                                     /* +0x5e clear: the first at once */
        spl_started_note(sds[0],1);assert(spl_pick_in(A,header,1)==sds[1]);sds[0][0x5e]=1;spl_started_note(sds[0],0);
        sds[2][0x60]=1;spl_started_note(sds[1],1);assert(spl_pick_in(A,header,1)==sds[0]);sds[2][0x60]=0;   /* +0x60: no candidate */
        {const uint32_t two=2;memcpy(sds[0]+0x10,&two,4);}assert(spl_pick_in(A,header,1)==sds[2]);        /* state 2 left out */
        {const uint32_t four=4;memcpy(sds[0]+0x10,&four,4);sds[0][0x61]=1;prio[0]=20;memcpy(sds[0]+0x58,&prio[0],4);}
        assert(spl_pick_in(A,header,1)==sds[0]);                                                     /* +0x61 counts only outside state 4 */
        {const uint32_t one=1;memcpy(sds[0]+0x10,&one,4);}assert(spl_pick_in(A,header,1)==sds[2]);
        memset(spl_started,0,sizeof(spl_started));spl_pick_hidden=0;}
    /* A request's cut of another line (spl_cut): of the lines X4's walk accepts, the requesting player's, the speaker with
     * the lowest key; another player's line is never ended; without a request of ours X4's own first line. */
    {   static _Alignas(16) unsigned char header[0x40],nodes[3][0x40],sds[4][0x80],lines[4][0x60];
        unsigned char *const A=nodes[0],*const B=nodes[1],*const C=nodes[2];void *p;
        memset(header,0,sizeof(header));memset(nodes,0,sizeof(nodes));memset(sds,0,sizeof(sds));
        p=header;memcpy(B,&p,8);p=A;memcpy(B+8,&p,8);p=C;memcpy(B+0x10,&p,8);p=B;memcpy(A,&p,8);memcpy(C,&p,8);memcpy(header+8,&p,8);
        const uint64_t ids[4]={0x5eec0003u,0x5eec0001u,0x5eec0002u,0x5eec0004u};   /* in address order: player 2's, slot 1's, player 2's */
        for(unsigned i=0;i<4;i++) {
            memcpy(sds[i]+8,&ids[i],8);const uint32_t state=4;memcpy(sds[i]+0x10,&state,4);
            void *b=lines[i],*e=lines[i]+0x60;memcpy(sds[i]+0x38,&b,8);memcpy(sds[i]+0x40,&e,8);   /* two lines, the first now */
            if(i<3) {p=sds[i];memcpy(nodes[i]+0x20,&p,8);}
        }
        memset(spl_speakers,0,sizeof(spl_speakers));
        spl_speaker_note(ids[0],2,SPL_KIND_CUE);spl_speaker_note(ids[2],2,SPL_KIND_CUE);spl_speaker_note(ids[3],2,SPL_KIND_CUE);
        const int saved_armed=spl_armed;const uint32_t saved_active2=stats.active;const DWORD saved_ui2=ui_thread;
        spl_armed=1;stats.active=1;ui_thread=GetCurrentThreadId();spl_cut_begin=A;spl_cut_end=header;spl_cut_original=test_cut_stub;
        spl_cut_calls=spl_cut_ended=spl_cut_none=spl_cut_plain=0;spl_rows_read=spl_rows_written;
        spl_request_data=sds[3];spl_request_owner=2;test_cut_calls=0;
        assert(spl_cut(sds[0],0x7fffffff)==1 && test_cut_calls==1 && test_cut_got==sds[2]);   /* player 2's lowest key, not X4's first */
        spl_request_owner=1;assert(spl_cut(sds[0],0x7fffffff)==1 && test_cut_got==sds[1]);    /* an unnoted speaker is slot 1's */
        spl_request_owner=3;test_cut_calls=0;assert(spl_cut(sds[0],0x7fffffff)==1 && !test_cut_calls && spl_cut_none==1);   /* nobody's of 3 */
        spl_request_owner=2;{const uint32_t zero=0;memcpy(sds[2]+0x10,&zero,4);}
        assert(spl_cut(sds[0],0x7fffffff)==1 && test_cut_got==sds[0]);                         /* no state: not a candidate */
        {const uint32_t four=4,two=2;memcpy(sds[2]+0x10,&four,4);memcpy(sds[2]+0x50,&two,4);}
        assert(spl_cut(sds[0],0x7fffffff)==1 && test_cut_got==sds[0]);                         /* no line left: neither */
        {const uint32_t zero=0;memcpy(sds[2]+0x50,&zero,4);}sds[2][0x5e]=1;
        assert(spl_cut(sds[0],0x7fffffff)==1 && test_cut_got==sds[0]);sds[2][0x5e]=0;          /* NPC to NPC: neither */
        spl_request_data=sds[2];assert(spl_cut(sds[1],0x7fffffff)==1 && test_cut_got==sds[0]); /* never the requester itself */
        spl_request_data=NULL;assert(spl_cut(sds[1],0x7fffffff)==1 && test_cut_got==sds[1] && spl_cut_plain==1);   /* not ours: X4's */
        assert(spl_cut_calls==7 && spl_cut_ended==6 && spl_cut_none==1);
        char cut_rows[SPL_ROWS*420];assert(spl_rows_text(cut_rows,sizeof(cut_rows))>0 && strstr(cut_rows,"SPEECH_CUT tick=") &&
            strstr(cut_rows," owner=2 scope=0 first=5eec0003 ended=5eec0002 candidates=3 own=2") && strstr(cut_rows," ended=0 candidates=3 own=0"));
        spl_cut_begin=spl_cut_end=NULL;spl_cut_original=NULL;spl_request_data=NULL;spl_request_owner=0;memset(spl_speakers,0,sizeof(spl_speakers));
        spl_armed=saved_armed;stats.active=saved_active2;ui_thread=saved_ui2;spl_cut_calls=spl_cut_ended=spl_cut_none=spl_cut_plain=0;
    }
    printf("speech local decision, owned frames, site CAS, arm, failure and restore tests, subtitles, cuts PASS\n");
}
/* The subtitle picker against X4's image: as shipped it is pinned; one byte changed in it is refused. */
static void test_speech_pick_native_image(const char *path) {
    FILE *file=fopen(path,"rb");assert(file);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    assert(fread(&dos,1,sizeof(dos),file)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0 && dos.e_lfanew<4096);
    assert(!fseek(file,dos.e_lfanew,SEEK_SET) && fread(&nt,1,sizeof(nt),file)==sizeof(nt) && nt.OptionalHeader.SizeOfImage==0x6fa9000);
    unsigned char *at=VirtualAlloc(NULL,nt.OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(at);
    for(unsigned i=0;i<nt.FileHeader.NumberOfSections;i++) {
        IMAGE_SECTION_HEADER section;assert(fread(&section,1,sizeof(section),file)==sizeof(section));long next=ftell(file);
        if(section.SizeOfRawData) {
            assert(!fseek(file,section.PointerToRawData,SEEK_SET));
            assert(fread(at+section.VirtualAddress,1,section.SizeOfRawData,file)==section.SizeOfRawData);
        }
        assert(!fseek(file,next,SEEK_SET));
    }
    fclose(file);
    assert(spl_pick_pins(at));
    /* the set's first node and its end are the picker's own two operands */
    int32_t rel=0;memcpy(&rel,at+SPL_PICK+3,4);assert((uint32_t)(SPL_PICK+7u+(uint32_t)rel)==SPL_PICK_BEGIN);
    memcpy(&rel,at+SPL_PICK+10,4);assert((uint32_t)(SPL_PICK+14u+(uint32_t)rel)==SPL_PICK_END);
    at[SPL_PICK+0x30]^=0x10;assert(!spl_pick_pins(at));at[SPL_PICK+0x30]^=0x10;
    at[SPL_PICK+1]^=0x01;assert(!spl_pick_pins(at));at[SPL_PICK+1]^=0x01;assert(spl_pick_pins(at));
    assert(((SPL_PICK+4u)&~15u)==(SPL_PICK&~15u));   /* the patched five bytes lie in one 16-byte block */
    /* The request's cut (spl_cut): the walk, its successor block with the call, 0x562ba0 pinned; the call's target; the
     * acceptance spl_cut_accepts restates, read from the walk's own bytes; ours at the site still passes, a change refuses. */
    assert(spl_cut_pins(at) && ((SPL_CUT_SITE+4u)&~15u)==(SPL_CUT_SITE&~15u));   /* one 16-byte block for the CAS */
    int32_t cut_rel=0;memcpy(&cut_rel,at+SPL_CUT_SITE+1,4);assert(at[SPL_CUT_SITE]==0xe8 && SPL_CUT_SITE+5u+(uint32_t)cut_rel==SPL_CUT);
    assert(!memcmp(at+0x56220a,"\xba\xff\xff\xff\x7f\x49\x8b\xc9",8));                 /* mov edx,0x7fffffff; mov rcx,r9 */
    assert(!memcmp(at+0x5620a1,"\x4d\x8b\x48\x20\x4d\x3b\xcd",7));                     /* r9 = node+0x20; the requester (r13) left out */
    assert(!memcmp(at+0x5620aa,"\x41\x38\x79\x5e",4) && !memcmp(at+0x5620b0,"\x41\x39\x79\x10",4));   /* +0x5e clear, a state */
    assert(!memcmp(at+0x5620b6,"\x49\x8b\x49\x40",4) && !memcmp(at+0x5620bd,"\x49\x2b\x49\x38",4) && !memcmp(at+0x5620d2,"\x41\x8b\x41\x50",4));
    {unsigned char saved[5];memcpy(saved,at+SPL_CUT_SITE,5);at[SPL_CUT_SITE+1]^=0x55;assert(spl_cut_pins(at));memcpy(at+SPL_CUT_SITE,saved,5);}
    at[SPL_CUT_WALK+0x10]^=0x01;assert(!spl_cut_pins(at));at[SPL_CUT_WALK+0x10]^=0x01;
    at[SPL_CUT_NEXT+2]^=0x01;assert(!spl_cut_pins(at));at[SPL_CUT_NEXT+2]^=0x01;
    at[SPL_CUT+0x20]^=0x01;assert(!spl_cut_pins(at));at[SPL_CUT+0x20]^=0x01;assert(spl_cut_pins(at));
    VirtualFree(at,0,MEM_RELEASE);
    printf("speech pick native image: the subtitle picker and its two set operands pinned; tests PASS\n");
}
