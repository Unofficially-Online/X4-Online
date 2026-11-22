/* Island order (lockstep_island_order.h), synthetic: three contact constraints between keyed and unkeyed bodies. The keys
 * do not depend on keyed bodies' BodyIDs, do on their owners, records, normals and points; an unkeyed body adds its BodyID;
 * the stub rekeys the range and then calls the original sort; the layout counters see a pointer and a bad point count. */
static _Alignas(16) unsigned char tio_constraints[3][IORDER_STRIDE],tio_bodies[4][0x80],tio_records[4][0x20],tio_owners[3][0x280];
static _Alignas(16) unsigned char tio_manager[0x100];
static unsigned tio_original_calls;
static uint64_t tio_owner_key(uint64_t runtime) {return runtime==0x51u?0x1000051ull:runtime==0x52u?0x8000000000000052ull:0;}
static void tio_original(uint32_t *begin,uint32_t *end,void *manager) {
    unsigned char *array=NULL;memcpy(&array,(unsigned char *)manager+IORDER_ARRAY,8);++tio_original_calls;
    if(!array) return;
    for(uint32_t *i=begin+1;i<end;i++)   /* insertion sort on the key, as the comparator's first test */
        for(uint32_t *j=i;j>begin;j--) {
            uint64_t a,b;memcpy(&a,array+(size_t)j[-1]*IORDER_STRIDE+0x10,8);memcpy(&b,array+(size_t)j[0]*IORDER_STRIDE+0x10,8);
            if(a<=b) break;
            const uint32_t t=j[-1];j[-1]=j[0];j[0]=t;
        }
}
static void tio_constraint(unsigned i,unsigned b1,unsigned b2,float nx,uint32_t count) {
    unsigned char *c=tio_constraints[i],*p1=tio_bodies[b1],*p2=tio_bodies[b2];
    memset(c,0,IORDER_STRIDE);memcpy(c,&p1,8);memcpy(c+8,&p2,8);
    const float normal[8]={nx,0.5f,0.25f,0.4f,1.0f,0.5f,0.0f,0.0f};memcpy(c+0x18,normal,0x20);memcpy(c+0x38,&count,4);
    for(uint32_t k=0;k<count && k<IORDER_POINTS;k++) {const float r[6]={1.0f+k,2.0f,3.0f,-1.0f,-2.0f-k,-3.0f};memcpy(c+0x40+IORDER_POINT*k,r,24);}
}
static void tio_keys(uint64_t out[3]) {for(unsigned i=0;i<3;i++) memcpy(&out[i],tio_constraints[i]+0x10,8);}
static void test_island_order(void) {
    uint64_t (*owner_key)(uint64_t)=iorder_owner_key;iorder_owner_key=tio_owner_key;iorder_original=tio_original;
    memset(tio_bodies,0,sizeof(tio_bodies));memset(tio_records,0,sizeof(tio_records));memset(tio_owners,0,sizeof(tio_owners));
    /* owners: 0x51 (a ship, records 0 and 1 = bodies 0 and 1), 0x52 (a station, record 0 = body 2), 0x53 unkeyed (body 3) */
    for(unsigned o=0;o<3;o++) {uint64_t rt=0x51u+o;memcpy(tio_owners[o]+8,&rt,8);}
    const unsigned owner_of[4]={0,0,1,2},slot_of[4]={0,1,0,0};
    for(unsigned b=0;b<4;b++) {
        unsigned char *rec=tio_records[b],*own=tio_owners[owner_of[b]];uint32_t id=0x00800010u+b;
        memcpy(rec,&own,8);memcpy(tio_bodies[b]+0x50,&rec,8);memcpy(tio_bodies[b]+0x70,&id,4);
        memcpy(own+0x260+8u*slot_of[b],&rec,8);
    }
    {unsigned char *array=(unsigned char *)tio_constraints;memset(tio_manager,0,sizeof(tio_manager));memcpy(tio_manager+IORDER_ARRAY,&array,8);}
    tio_constraint(0,0,2,0.9f,2);tio_constraint(1,1,2,0.3f,1);tio_constraint(2,0,3,0.6f,3);
    uint32_t order[3]={0,1,2};
    iorder_sorts=iorder_constraints=iorder_unkeyed=iorder_ties=iorder_pointerish=iorder_badcount=0;iorder_failures=0;iorder_rekey=1;
    iorder_sort(order,order+3,tio_manager);
    uint64_t k1[3];tio_keys(k1);
    assert(k1[0]==0x40890a03abd7bd5dull);   /* the golden key the Linux gate's test checks too (linux/test_gate_island_order.h) */
    assert(tio_original_calls==1 && iorder_sorts==1 && iorder_constraints==3 && iorder_unkeyed==1 && !iorder_ties && !iorder_pointerish && !iorder_badcount);
    assert(k1[0] && k1[1] && k1[2] && k1[0]!=k1[1] && k1[0]!=k1[2] && k1[1]!=k1[2]);
    for(unsigned i=1;i<3;i++) {uint64_t a,b;memcpy(&a,tio_constraints[order[i-1]]+0x10,8);memcpy(&b,tio_constraints[order[i]]+0x10,8);assert(a<b);}
    /* other BodyIDs for the keyed bodies (a joiner's): the same keys; another BodyID for the unkeyed body: constraint 2 only */
    for(unsigned b=0;b<3;b++) {uint32_t id=0x00800030u+b;memcpy(tio_bodies[b]+0x70,&id,4);}
    order[0]=2;order[1]=0;order[2]=1;iorder_sort(order,order+3,tio_manager);
    uint64_t k2[3];tio_keys(k2);assert(k2[0]==k1[0] && k2[1]==k1[1] && k2[2]==k1[2]);
    {uint32_t id=0x00800099u;memcpy(tio_bodies[3]+0x70,&id,4);}
    iorder_sort(order,order+3,tio_manager);tio_keys(k2);assert(k2[0]==k1[0] && k2[1]==k1[1] && k2[2]!=k1[2]);
    /* the other record of the same owner, another normal, another point: other keys */
    {unsigned char *c=tio_constraints[0],*p=tio_bodies[1];memcpy(c,&p,8);}
    iorder_sort(order,order+3,tio_manager);tio_keys(k2);assert(k2[0]!=k1[0]);
    tio_constraint(0,0,2,0.9f,2);{float x=0.95f;memcpy(tio_constraints[1]+0x18,&x,4);}
    iorder_sort(order,order+3,tio_manager);tio_keys(k2);assert(k2[0]==k1[0] && k2[1]!=k1[1]);
    tio_constraint(1,1,2,0.3f,1);{float x=7.0f;memcpy(tio_constraints[2]+0x40+IORDER_POINT*2u,&x,4);}
    {uint32_t id=0x00800013u;memcpy(tio_bodies[3]+0x70,&id,4);}
    iorder_sort(order,order+3,tio_manager);tio_keys(k2);assert(k2[1]==k1[1] && k2[2]!=k1[2]);
    /* layout counters: a pointer-like word among the fields, a point count past four */
    {uint64_t pointer=0x000001a012345678ull;memcpy(tio_constraints[1]+0x28,&pointer,8);uint32_t bad=7;memcpy(tio_constraints[0]+0x38,&bad,4);}
    const uint64_t before=iorder_pointerish,bad_before=iorder_badcount;iorder_sort(order,order+3,tio_manager);
    assert(iorder_pointerish==before+1 && iorder_badcount==bad_before+1);
    /* a single constraint: no rekey, the original still called */
    const unsigned calls=tio_original_calls;const uint64_t sorts=iorder_sorts;iorder_sort(order,order+1,tio_manager);
    assert(tio_original_calls==calls+1 && iorder_sorts==sorts);
    /* an unreadable manager: counted as a failure, the original still called */
    {unsigned char empty[0x100];memset(empty,0,sizeof(empty));iorder_sort(order,order+3,empty);assert(iorder_failures==1 && tio_original_calls==calls+2);}
    {char status[512];assert(iorder_status(status,sizeof(status)) && strstr(status,"installed=0 sorts=") && strstr(status," failures=1"));}
    iorder_failures=0;
    /* the CONSTRAINT trace with the rekey off: constraint 2 (body 3 = actor 11) kept, its cache point read at the text */
    tio_constraint(0,0,2,0.9f,2);tio_constraint(1,1,2,0.3f,1);tio_constraint(2,0,3,0.6f,1);
    {static _Alignas(16) unsigned char cached[0x40];unsigned char *p=cached;const float cache[9]={1,2,3,4,5,6,0.5f,0.25f,-0.125f};
     memcpy(cached,cache,36);memcpy(tio_constraints[2]+0x40+0xc0,&p,8);
     const float part[16]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};memcpy(tio_constraints[2]+0x40,part,64);
     const uint64_t jolt_key=0x1122334455667788ull;memcpy(tio_constraints[2]+0x10,&jolt_key,8);
     void *component=tio_owners[2];const uint64_t component_id=0x53u;void *saved_component=actor_components[10];const uint64_t saved_id=actor_ids[10];
     actor_components[10]=component;actor_ids[10]=component_id;
     iorder_rekey=0;ctrace_actor=iorder_trace_actor;ctrace_on=1;ctrace_clear();ctrace_failures=ctrace_seen=ctrace_ordinal=0;
     const uint64_t sorts0=iorder_sorts,issued=stats.issued;stats.issued=133;{uint32_t id=0x00800099u;memcpy(tio_bodies[3]+0x70,&id,4);}
     order[0]=0;order[1]=1;order[2]=2;iorder_sort(order,order+3,tio_manager);
     assert(iorder_sorts==sorts0 && ctrace_rows_held()==1 && ctrace_seen==3 && !ctrace_failures);
     char out[4096];const int n=ctrace_row_text(1,out,sizeof(out));
     assert(n && strstr(out,"tick=133 ordinal=1 index=2 size=3 body1=0:00800030 body2=11:00800099 key=1122334455667788 count=1 head=3f19999a3f0000003e800000"));
     assert(strstr(out," point0=3f80000040000000") && strstr(out," cached0=3f800000400000004040000040800000"
                                                                "40a0000040c000003f0000003e800000be000000") && !strstr(out,"point1"));
     ctrace_clear();assert(!ctrace_rows_held());
     ctrace_on=0;ctrace_actor=NULL;ctrace_seen=ctrace_ordinal=0;stats.issued=issued;actor_components[10]=saved_component;actor_ids[10]=saved_id;}
    iorder_owner_key=owner_key;iorder_original=NULL;iorder_rekey=0;
    puts("island order: keys from owners, records, normals and points (not BodyIDs, golden as on Linux), unkeyed bodies by BodyID, layout counters, the CONSTRAINT trace PASS");
}
