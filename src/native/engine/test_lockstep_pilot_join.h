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
    pslot_mask=2;static unsigned char fake_player[8];pslots[2].player=fake_player;pslots[2].length=4;memcpy(pslots[2].store,"\x01\x02\x03\x04",4);
    pslots[2].throttle=0.5f;pslots[2].assist[0]=1;pslots[2].authority=1;
    const int n=pjoin_tables_state(text,sizeof(text),40);
    assert(n>0 && !strncmp(text,"v1/t28|stock=",13) && strstr(text,"|store2=01020304"));
    /* Clear what the text carries, read it back, unpack. */
    memset(pstk_slot_frames,0,sizeof(pstk_slot_frames));memset(pstk_held_states,0,sizeof(pstk_held_states));memset(pstk_fire_on,0,sizeof(pstk_fire_on));
    memset(pstk_fire_tick,0,sizeof(pstk_fire_tick));memset(pview_target_replay,0,sizeof(pview_target_replay));pstk_flight=0;pstk_flight_known=0;pstk_frame_tick=0;
    pslots[2].throttle=0;pslots[2].assist[0]=0;pslots[2].authority=0;
    assert(pjoin_tables_parse(text) && pjoin_tables_t==40);
    pjoin_stock_unpack();pjoin_seat_unpack(2);
    assert(pstk_slot_frames[1].tick==40 && pstk_slot_frames[1].hash==0x1234 && pstk_slot_frames[1].r.events[0].id==0xd && pstk_held_states[1][0]==1);
    assert(pstk_fire_on[1]==1 && pstk_fire_tick[1]==39 && pview_target_replay[1]==0x77 && pstk_flight==1 && pstk_flight_known==1 && pstk_frame_tick==40);
    assert(pslots[2].throttle==0.5f && pslots[2].assist[0]==1 && pslots[2].authority==1 && pslots[2].length==4);
    assert(pjoin_store_rx_len[2]==4 && !memcmp(pjoin_store_rx[2],"\x01\x02\x03\x04",4));
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

