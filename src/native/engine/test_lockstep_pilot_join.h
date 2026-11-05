/* lockstep_pilot_join.h, the joiner's side: the package's pilot_players text (live w1q, slot 2 built, slot 3 absent). */
static void test_pilot_join_players(void) {
    const char *w1q="v1/m6|2:16467c-16467e,16469c-16469d,16469d-1646ae;16467c:8001273562e1e15e:127,16467d:800127c8e4f05e6e:127,16469c:0:0|3:none";
    assert(pjoin_parse_players(w1q) && pjoin_rx_mask==2 && pjoin_rx_slots==6);
    const pjoin_slot_rx *r=&pjoin_rx[2];
    assert(r->n==3 && r->listed && r->ids[0]==0x16467cull && r->ids[1]==0x16467dull && r->ids[2]==0x16469cull);
    assert(r->created[0]==0x8001273562e1e15eull && r->created[1]==0x800127c8e4f05e6eull && !r->created[2] && r->tick[0]==0x127 && !r->tick[2]);
    assert(r->range[PJOIN_CONNECT][0]==0x16469d && r->range[PJOIN_CONNECT][1]==0x1646ae && !pjoin_rx[3].listed);
    /* A generation in the ID's top bits: the slot part must still follow the ranges. */
    assert(pjoin_parse_players("v1/m2|2:10-11,20-21,0-0;20000010:0:0,20000020:0:0") && pjoin_rx[2].ids[0]==0x20000010ull);
    /* Refusals, each named. */
    static const struct {const char *text,*why;} bad[]={
        {"v2/m6|2:none","players_head"},{"v1/m7","players_head"},{"v1/m4|2:none","players_slot"},{"v1/m6|5:none","players_slot"},
        {"v1/m6|2:10-11,20-21","players_range"},{"v1/m6|2:10-11,20-21,0-0;10:0:0","players_order"},
        {"v1/m6|2:10-11,20-21,0-0;10:0:0,21:0:0","players_order"},{"v1/m6|2:10-11,20-21,0-0;10:0:0,20:0:0,30:0:0","players_count"},
        {"v1/m6|2:10-11,20-21,0-0;10:0:0,zz","players_ids"},{"v1/m6|2:0-0,0-0,0-0","players_count"}};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++) {
        pjoin_resume_why="-";
        if(pjoin_parse_players(bad[i].text) || strcmp(pjoin_resume_why,bad[i].why)) {
            printf("pilot join players: %s gave %s, not %s\n",bad[i].text,pjoin_resume_why,bad[i].why);assert(0);
        }
    }
    pjoin_parse_players("v1/m0");memset(pjoin_rx,0,sizeof(pjoin_rx));pjoin_rx_mask=0;pjoin_rx_slots=0;pjoin_resume_why="-";
    printf("pilot join: the package's slot players parsed (w1q's slot 2: 3 IDs, 2 created), every malformed text refused by name PASS\n");
}
/* 'pilot_state': the tables a joiner needs at T+1 round-trip through the text; ring entries only after T; refusals named. */
static void test_pilot_join_tables(void) {
    static char text[1u<<17];
    const unsigned saved_mask=pslot_mask;unsigned char *saved_player=pslots[2].player;unsigned saved_length=pslots[2].length;
    memset(pstk_ring,0,sizeof(pstk_ring));memset(pilot_ring,0,sizeof(pilot_ring));
    pstk_ring[1][41%PSTK_RING]=(pstk_staged){41,5,1,{1,2,3,4,5}};      /* applied after T=40: travels */
    pstk_ring[1][40%PSTK_RING]=(pstk_staged){40,3,1,{9,9,9}};          /* applied at T: does not */
    pilot_ring[42%PILOT_RING].tick=42;pilot_ring[42%PILOT_RING].present=1;pilot_ring[42%PILOT_RING].bytes[1][0]=7;
    pstk_slot_frames[1].tick=40;pstk_slot_frames[1].hash=0x1234;pstk_slot_frames[1].r.count=1;pstk_slot_frames[1].r.events[0]=(pstk_event){1,0xd,1};
    pstk_held_states[1][0]=1;pstk_fire_on[1]=1;pstk_fire_tick[1]=39;pview_target_replay[1]=0x77;pstk_flight=1;pstk_flight_known=1;pstk_frame_tick=40;
    /* A store of the real layout (pslot_store_init's regions), a pattern everywhere but the zone pointer (0: no zone). */
    unsigned layout=0,zone=0;
    for(unsigned i=0;i<VPL_REGION_COUNT;i++) {
        const vpl_region *g=&vpl_regions[i];
        if(g->base!=VPL_BASE_IMAGE || !(PSLOT_CLASSES>>g->cls&1u)) continue;
        if(g->pointer) zone=layout;
        layout+=g->size;
    }
    assert(layout && layout<=VPL_BLOCK_MAX && zone+8<=layout);
    pslot_mask=2;static unsigned char fake_player[8];pslots[2].player=fake_player;pslots[2].length=layout;
    for(unsigned i=0;i<layout;i++) pslots[2].store[i]=(unsigned char)(i*7+1);
    memset(pslots[2].store+zone,0,8);
    pslots[2].throttle=0.5f;pslots[2].assist[0]=1;pslots[2].authority=1;
    const int n=pjoin_tables_state(text,sizeof(text),40);
    assert(n>0 && !strncmp(text,"v1/t28|stock=",13) && strstr(text,"|store2=01080f16"));
    /* Clear what the text carries, read it back, unpack. */
    memset(pstk_slot_frames,0,sizeof(pstk_slot_frames));memset(pstk_held_states,0,sizeof(pstk_held_states));memset(pstk_fire_on,0,sizeof(pstk_fire_on));
    memset(pstk_fire_tick,0,sizeof(pstk_fire_tick));memset(pview_target_replay,0,sizeof(pview_target_replay));pstk_flight=0;pstk_flight_known=0;pstk_frame_tick=0;
    pslots[2].throttle=0;pslots[2].assist[0]=0;pslots[2].authority=0;
    assert(pjoin_tables_parse(text) && pjoin_tables_t==40);
    pjoin_stock_unpack();pjoin_seat_unpack(2);
    assert(pstk_slot_frames[1].tick==40 && pstk_slot_frames[1].hash==0x1234 && pstk_slot_frames[1].r.events[0].id==0xd && pstk_held_states[1][0]==1);
    assert(pstk_fire_on[1]==1 && pstk_fire_tick[1]==39 && pview_target_replay[1]==0x77 && pstk_flight==1 && pstk_flight_known==1 && pstk_frame_tick==40);
    assert(pslots[2].throttle==0.5f && pslots[2].assist[0]==1 && pslots[2].authority==1 && pslots[2].length==layout);
    assert(pjoin_store_rx_len[2]==layout && !memcmp(pjoin_store_rx[2],pslots[2].store,layout));
    /* A zone pointer travels as a portable ID: one without a readable runtime ID refuses the state (never a raw address). */
    {   static unsigned char bad_zone[16];uint64_t pointer=(uint64_t)(uintptr_t)bad_zone;memcpy(pslots[2].store+zone,&pointer,8);
        assert(!pjoin_tables_state(text,sizeof(text),40));memset(pslots[2].store+zone,0,8);}
    assert(pjoin_pstk_ring[1][41%PSTK_RING].tick==41 && pjoin_pstk_ring[1][41%PSTK_RING].bytes[4]==5 && !pjoin_pstk_ring[1][40%PSTK_RING].present);
    assert(pjoin_pilot_ring[42%PILOT_RING].tick==42 && pjoin_pilot_ring[42%PILOT_RING].bytes[1][0]==7);
    /* Refusals: a wrong head, a blob of the wrong size, one missing, a stray name. */
    pjoin_resume_why="-";assert(!pjoin_tables_parse("v2|stock=0") && !strcmp(pjoin_resume_why,"state_head"));
    assert(!pjoin_tables_parse("v1/t1|stock=00") && !strcmp(pjoin_resume_why,"state_blob"));
    assert(!pjoin_tables_parse("v1/t1|stock=0") && !strcmp(pjoin_resume_why,"state_missing"));
    assert(!pjoin_tables_parse("v1/t1|nonsense=0") && !strcmp(pjoin_resume_why,"state_store"));
    memset(pstk_ring,0,sizeof(pstk_ring));memset(pilot_ring,0,sizeof(pilot_ring));memset(pstk_slot_frames,0,sizeof(pstk_slot_frames));
    memset(pstk_held_states,0,sizeof(pstk_held_states));memset(pstk_fire_on,0,sizeof(pstk_fire_on));memset(pstk_fire_tick,0,sizeof(pstk_fire_tick));
    memset(pview_target_replay,0,sizeof(pview_target_replay));pstk_flight=0;pstk_flight_known=0;pstk_frame_tick=0;
    pslots[2].player=saved_player;pslots[2].length=saved_length;pslots[2].throttle=0;pslots[2].assist[0]=0;pslots[2].authority=0;pslot_mask=saved_mask;
    pjoin_resume_why="-";
    printf("pilot join: the pilot tables at T round-trip through 'pilot_state' (rings after T only, frames, held keys, fire, view, seats, stores), refusals named PASS\n");
}

/* R5 (lockstep_pilot_join_modules.h): the per-slot modules' state at T round-trips through 'pilot_state' (the UI replay's
 * staged sections and the hit claims after T only, the order aliases, the death records, the consumed keys); the checks
 * refuse by name, before anything changes, what is not carried yet. */
static void test_pilot_join_modules(void) {
    static char text[1u<<18];static pfire_missile_slot missiles[PILOT_SLOTS];
    const int saved_death=pdeath_armed,saved_ui=uirep_armed,saved_foot_slots=pfslot_armed;
    const unsigned saved_mask=pslot_mask,saved_dead=pdeath_dead,saved_foot=pfoot_state,saved_consumed=hit_consumed_count;
    memcpy(missiles,pfire_missiles,sizeof(missiles));memset(pfire_missiles,0,sizeof(pfire_missiles));
    pslot_mask=0;pdeath_armed=1;uirep_armed=1;pfslot_armed=0;pdeath_dead=0;pfoot_state=PFOOT_SEATED;   /* no seats: test_pilot_join_tables */
    memset(uirep_ring,0,sizeof(uirep_ring));memset(hit_stage_tick,0,sizeof(hit_stage_tick));memset(hit_stage_count,0,sizeof(hit_stage_count));
    memset(pdeath_slots,0,sizeof(pdeath_slots));memset(pdeath_held,0,sizeof(pdeath_held));memset(pfire_slots,0,sizeof(pfire_slots));
    uirep_staged *ui=&uirep_ring[1][45%UIREP_RING];ui->tick=45;ui->present=1;ui->length=3;ui->bytes[2]=9;
    uirep_ring[1][40%UIREP_RING].tick=40;uirep_ring[1][40%UIREP_RING].present=1;   /* applied at T: does not travel */
    uirep_q_alias_count=1;uirep_q_aliases[0]=(uirep_q_alias){2,0x99,3,4,1};uirep_q_applied[1]=1;
    pdeath_slots[1].deaths=2;pdeath_slots[1].respawns=2;pdeath_held[0][5]=1;pdeath_released=1;
    hit_stage_tick[44%HIT_RING]=44;hit_stage_count[44%HIT_RING]=1;hit_stage_ring[44%HIT_RING][0].slot=2;hit_stage_ring[44%HIT_RING][0].apply=44;
    hit_stage_tick[39%HIT_RING]=39;hit_stage_count[39%HIT_RING]=1;   /* applied before T: does not travel */
    hit_consumed[0]=0x1111;hit_consumed[1]=0x2222;hit_consumed_count=2;
    assert(pjoin_tables_state(text,sizeof(text),40)>0 && strstr(text,"|m5=") && strstr(text,"|dh=") && strstr(text,"|ur1_45=") && !strstr(text,"|ur1_40="));
    assert(strstr(text,"|hr44=") && !strstr(text,"|hr39=") && strstr(text,"|hc=11110000000000002222"));
    /* The joiner: nothing of it yet, but its own catch-up staged a claim for 44 already (the carried one goes first). */
    memset(uirep_ring,0,sizeof(uirep_ring));uirep_q_alias_count=0;memset(uirep_q_aliases,0,sizeof(uirep_q_aliases));memset(uirep_q_applied,0,sizeof(uirep_q_applied));
    memset(pdeath_slots,0,sizeof(pdeath_slots));memset(pdeath_held,0,sizeof(pdeath_held));pdeath_released=0;
    memset(hit_stage_tick,0,sizeof(hit_stage_tick));memset(hit_stage_count,0,sizeof(hit_stage_count));hit_consumed_count=0;
    hit_stage_tick[44%HIT_RING]=44;hit_stage_count[44%HIT_RING]=1;hit_stage_ring[44%HIT_RING][0].slot=3;
    pjoin_resume_why="-";
    assert(pjoin_tables_parse(text) && pjoin_m5_check());
    pjoin_m5_apply();
    assert(uirep_ring[1][45%UIREP_RING].tick==45 && uirep_ring[1][45%UIREP_RING].length==3 && uirep_ring[1][45%UIREP_RING].bytes[2]==9 && !uirep_ring[1][40%UIREP_RING].present);
    assert(uirep_q_alias_count==1 && uirep_q_aliases[0].ctl==0x99 && uirep_q_aliases[0].real==4 && uirep_q_aliases[0].seq==1 && uirep_q_applied[1]==1);
    assert(pdeath_slots[1].deaths==2 && pdeath_slots[1].respawns==2 && pdeath_held[0][5]==1 && pdeath_released==1);
    assert(hit_stage_count[44%HIT_RING]==2 && hit_stage_ring[44%HIT_RING][0].slot==2 && hit_stage_ring[44%HIT_RING][1].slot==3 && !hit_stage_count[39%HIT_RING]);
    assert(hit_consumed_count==2 && hit_consumed[0]==0x1111 && hit_consumed[1]==0x2222);
    /* Refused before anything changes: a slot dead at T, a module armed on one side only, missiles shooting at T. */
    pdeath_slots[1].state=PDEATH_DEAD;
    assert(pjoin_tables_state(text,sizeof(text),40)>0 && pjoin_tables_parse(text) && !pjoin_m5_check() && !strcmp(pjoin_resume_why,"death_at_T"));
    pdeath_slots[1].state=PDEATH_ALIVE;
    assert(pjoin_tables_state(text,sizeof(text),40)>0);uirep_armed=0;
    assert(pjoin_tables_parse(text) && !pjoin_m5_check() && !strcmp(pjoin_resume_why,"modules_armed"));uirep_armed=1;
    pfire_missiles[2].shooting=1;
    assert(pjoin_tables_state(text,sizeof(text),40)>0 && pjoin_tables_parse(text) && !pjoin_m5_check() && !strcmp(pjoin_resume_why,"missiles_at_T"));
    pfire_missiles[2].shooting=0;
    /* Malformed R5 entries refuse the parse by name. */
    assert(pjoin_tables_state(text,sizeof(text),40)>0);
    {const size_t n=strlen(text);strcat(text,"|ur9_0=0");assert(!pjoin_tables_parse(text) && !strcmp(pjoin_resume_why,"state_ui"));text[n]=0;}
    {const size_t n=strlen(text);strcat(text,"|hr3=00");assert(!pjoin_tables_parse(text) && !strcmp(pjoin_resume_why,"state_hit"));text[n]=0;}
    {char *m5=strstr(text,"|m5=");m5[1]='x';assert(!pjoin_tables_parse(text) && !strcmp(pjoin_resume_why,"state_store"));m5[1]='m';}
    memset(uirep_ring,0,sizeof(uirep_ring));uirep_q_alias_count=0;memset(uirep_q_aliases,0,sizeof(uirep_q_aliases));memset(uirep_q_applied,0,sizeof(uirep_q_applied));
    memset(pdeath_slots,0,sizeof(pdeath_slots));memset(pdeath_held,0,sizeof(pdeath_held));pdeath_released=0;
    memset(hit_stage_tick,0,sizeof(hit_stage_tick));memset(hit_stage_count,0,sizeof(hit_stage_count));memset(hit_stage_ring,0,sizeof(hit_stage_ring));
    hit_consumed_count=saved_consumed;memcpy(pfire_missiles,missiles,sizeof(missiles));
    pslot_mask=saved_mask;pdeath_armed=saved_death;uirep_armed=saved_ui;pfslot_armed=saved_foot_slots;pdeath_dead=saved_dead;pfoot_state=saved_foot;
    pjoin_resume_why="-";
    printf("pilot join: R5, the modules' state at T round-trips (UI sections and hit claims after T, order aliases, deaths, consumed keys), refusals named PASS\n");
}

