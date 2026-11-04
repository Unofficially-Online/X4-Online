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
