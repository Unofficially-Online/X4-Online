/* lockstep_pilot_foot_transport.h: the place at the target transporter and the noted travels (synthetic; the carry
 * itself runs X4's routines and is checked live: tools/dev/station_walk_test.py --scenario transport). */
static void test_pilot_foot_transport(void) {
    /* A player 1 m in front of (+z) and 0.5 m right of (+x) the source transporter, facing along +z. The target
     * transporter stands at (10, 2, -4) turned a quarter to the right (its x axis along -z, its z axis along +x). */
    float from[16],to[16],entity[16],out[16];
    pftr_identity(from);pftr_identity(to);pftr_identity(entity);
    from[0]=3.0f;from[1]=1.0f;from[2]=5.0f;
    entity[0]=3.5f;entity[1]=1.0f;entity[2]=6.0f;entity[3]=7.0f;entity[7]=9.0f;
    to[0]=10.0f;to[1]=2.0f;to[2]=-4.0f;
    to[4]=0.0f;to[5]=0.0f;to[6]=-1.0f;      /* row 1: the transporter's x axis in its room */
    to[12]=1.0f;to[13]=0.0f;to[14]=0.0f;    /* row 3: its z axis */
    pftr_place(entity,from,to,out);
    assert(fabsf(out[0]-11.0f)<1e-6f && fabsf(out[1]-2.0f)<1e-6f && fabsf(out[2]-(-4.5f))<1e-6f && out[3]==7.0f);
    assert(fabsf(out[4]-0.0f)<1e-6f && fabsf(out[6]-(-1.0f))<1e-6f && out[7]==9.0f);      /* its x axis turned with the pad */
    assert(fabsf(out[12]-1.0f)<1e-6f && fabsf(out[14]-0.0f)<1e-6f && fabsf(out[9]-1.0f)<1e-6f);
    /* The same transporter twice: the place is the entity's own. */
    pftr_place(entity,from,from,out);
    for(unsigned i=0;i<16;i++) assert(fabsf(out[i]-entity[i])<1e-6f);
    /* Noted travels: the oldest to a target first, a slot once, an old one forgotten; nothing while not installed. */
    const int was=pftr_installed;const uint64_t issued=stats.issued;
    memset(pftr_waits,0,sizeof(pftr_waits));pftr_installed=0;stats.issued=100;
    pftr_note(2,0x55);assert(!pftr_take(0x55));
    pftr_installed=1;
    pftr_note(1,0x55);stats.issued=101;pftr_note(2,0x55);pftr_note(3,0x66);
    assert(pftr_take(0x77)==0 && pftr_take(0x55)==1 && pftr_take(0x55)==2 && pftr_take(0x55)==0 && pftr_take(0x66)==3);
    pftr_note(2,0x88);stats.issued=101+PFTR_WAIT_TICKS+1;assert(pftr_take(0x88)==0);
    for(unsigned i=0;i<PFTR_WAITS+3;i++) {stats.issued=2000+i;pftr_note(2,0x1000+i);}      /* full: the oldest go */
    assert(pftr_take(0x1000)==0 && pftr_take(0x1000+PFTR_WAITS+2)==2);
    memset(pftr_waits,0,sizeof(pftr_waits));pftr_installed=was;stats.issued=issued;pftr_notes=0;
    puts("pilot foot transport: the place at the target transporter, noted travels PASS");
}
