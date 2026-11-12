/* native_gate_speech_local: the decision (catch-up, another sector, unseated), the site's CAS both ways, arm and restore. */
static int test_spl_catching;
static int test_spl_catching_up(void) {return test_spl_catching;}
static int test_spl_is_a(void *p,int klass) {return klass==0x58 && ((unsigned char*)p)[0x200]==0x58;}
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
     * slot 1 is, and the step's answer is back at its end; a nested frame changes nothing; another slot's frame and a
     * catch-up do not play; slot 1 and nobody are no owners. */
    {const uint64_t toggles=spl_toggles;
     spl_owner_enter(2);assert(!spl_muted && speak_mute_site()==SPEAK_MUTE_ORIGINAL && spl_owner_depth==1 && spl_owner_played==1);
     spl_owner_enter(2);spl_owner_leave();assert(!spl_muted && spl_owner_depth==1);
     spl_owner_leave();assert(spl_muted==1 && speak_mute_site()==SPEAK_MUTE_PATCHED && !spl_owner_depth && spl_toggles==toggles+2);
     spl_owner_leave();assert(spl_muted==1 && !spl_owner_depth);   /* an unpaired leave */
     spl_owner_enter(3);assert(spl_muted==1 && spl_owner_muted==1 && spl_toggles==toggles+2);spl_owner_leave();
     test_spl_catching=1;spl_owner_enter(2);assert(spl_muted==1 && spl_owner_muted==2);spl_owner_leave();test_spl_catching=0;
     spl_owner_enter(1);spl_owner_enter(0);spl_owner_enter(PILOT_SLOTS+1);assert(!spl_owner_depth && spl_owner_frames==3);
     /* Slot 1's client plays by default and does not play slot 2's line. */
     stats.slot=1;assert(spl_frame()==0 && speak_mute_site()==SPEAK_MUTE_ORIGINAL);
     spl_owner_enter(2);assert(spl_muted==1 && speak_mute_site()==SPEAK_MUTE_PATCHED);spl_owner_leave();
     assert(!spl_muted && speak_mute_site()==SPEAK_MUTE_ORIGINAL);
     stats.slot=2;assert(spl_frame()==1);
     assert(spl_status((char[320]){0},320)>0);}
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
