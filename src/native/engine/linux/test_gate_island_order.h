/* The island order and the CONSTRAINT trace on the Linux gate (gate_island_order.h, ../lockstep_constraint_trace.h).
 * Synthetic: Windows' three constraints with the owners' records 8 lower; the key of the first is the golden value
 * Windows' test checks too (the same bytes hashed on both gates), keys do not depend on keyed bodies' BodyIDs, the stub
 * rekeys and then sorts; the trace keeps the fixture's constraints and prints the cache point's lambdas. On the real
 * executable: the pins, the sort call to the page (jmp to lxio_sort) and back, the trace alone without rekeying.
 * Included by test_gate_linux.c. */
#define TLIO_GOLDEN 0x40890a03abd7bd5dull   /* Windows' test_island_order checks the same key */
static _Alignas(16) unsigned char tlio_constraints[3][LXIO_STRIDE],tlio_bodies[4][0x80],tlio_records[4][0x20],tlio_owners[3][0x280];
static _Alignas(16) unsigned char tlio_manager[0x100],tlio_cached[0x40];
static unsigned tlio_original_calls;
static uint64_t tlio_owner_key(uint64_t runtime) {return runtime==0x51u?0x1000051ull:runtime==0x52u?0x8000000000000052ull:0;}
static unsigned tlio_actor(const unsigned char *body) {return body==tlio_bodies[3]?11u:0u;}
static void tlio_original(uint32_t *begin,uint32_t *end,void *manager) {
    unsigned char *array=NULL;memcpy(&array,(unsigned char *)manager+LXIO_ARRAY,8);++tlio_original_calls;
    if(!array) return;
    for(uint32_t *i=begin+1;i<end;i++)
        for(uint32_t *j=i;j>begin;j--) {
            uint64_t a,b;memcpy(&a,array+(size_t)j[-1]*LXIO_STRIDE+0x10,8);memcpy(&b,array+(size_t)j[0]*LXIO_STRIDE+0x10,8);
            if(a<=b) break;
            const uint32_t t=j[-1];j[-1]=j[0];j[0]=t;
        }
}
static void tlio_constraint(unsigned i,unsigned b1,unsigned b2,float nx,uint32_t count) {
    unsigned char *c=tlio_constraints[i],*p1=tlio_bodies[b1],*p2=tlio_bodies[b2];
    memset(c,0,LXIO_STRIDE);memcpy(c,&p1,8);memcpy(c+8,&p2,8);
    const float normal[8]={nx,0.5f,0.25f,0.4f,1.0f,0.5f,0.0f,0.0f};memcpy(c+0x18,normal,0x20);memcpy(c+0x38,&count,4);
    for(uint32_t k=0;k<count && k<LXIO_POINTS;k++) {const float r[6]={1.0f+k,2.0f,3.0f,-1.0f,-2.0f-k,-3.0f};memcpy(c+0x40+LXIO_POINT*k,r,24);}
}
static void tlio_keys(uint64_t out[3]) {for(unsigned i=0;i<3;i++) memcpy(&out[i],tlio_constraints[i]+0x10,8);}
static void test_island_order_linux(void) {
    uint64_t (*owner_key)(uint64_t)=lxio_owner_key;lxio_owner_key=tlio_owner_key;lxio_original_override=tlio_original;
    memset(tlio_bodies,0,sizeof(tlio_bodies));memset(tlio_records,0,sizeof(tlio_records));memset(tlio_owners,0,sizeof(tlio_owners));
    for(unsigned o=0;o<3;o++) {uint64_t rt=0x51u+o;memcpy(tlio_owners[o]+8,&rt,8);}
    const unsigned owner_of[4]={0,0,1,2},slot_of[4]={0,1,0,0};
    for(unsigned b=0;b<4;b++) {
        unsigned char *rec=tlio_records[b],*own=tlio_owners[owner_of[b]];uint32_t id=0x00800010u+b;
        memcpy(rec,&own,8);memcpy(tlio_bodies[b]+0x50,&rec,8);memcpy(tlio_bodies[b]+0x70,&id,4);
        memcpy(own+LXIO_RECORDS+8u*slot_of[b],&rec,8);
    }
    {unsigned char *array=(unsigned char *)tlio_constraints;memset(tlio_manager,0,sizeof(tlio_manager));memcpy(tlio_manager+LXIO_ARRAY,&array,8);}
    tlio_constraint(0,0,2,0.9f,2);tlio_constraint(1,1,2,0.3f,1);tlio_constraint(2,0,3,0.6f,3);
    uint32_t order[3]={0,1,2};
    lxio_sorts=lxio_constraints=lxio_unkeyed=lxio_ties=lxio_pointerish=lxio_badcount=0;lxio_failures=0;lxio_rekey=1;
    lxio_sort(order,order+3,tlio_manager);
    uint64_t k1[3];tlio_keys(k1);
    assert(k1[0]==TLIO_GOLDEN);
    assert(tlio_original_calls==1 && lxio_sorts==1 && lxio_constraints==3 && lxio_unkeyed==1 && !lxio_ties && !lxio_pointerish && !lxio_badcount);
    assert(k1[1] && k1[2] && k1[0]!=k1[1] && k1[0]!=k1[2] && k1[1]!=k1[2]);
    for(unsigned i=1;i<3;i++) {uint64_t a,b;memcpy(&a,tlio_constraints[order[i-1]]+0x10,8);memcpy(&b,tlio_constraints[order[i]]+0x10,8);assert(a<b);}
    /* a joiner's BodyIDs for the keyed bodies: the same keys; the unkeyed body's: constraint 2 only */
    for(unsigned b=0;b<3;b++) {uint32_t id=0x00800030u+b;memcpy(tlio_bodies[b]+0x70,&id,4);}
    order[0]=2;order[1]=0;order[2]=1;lxio_sort(order,order+3,tlio_manager);
    uint64_t k2[3];tlio_keys(k2);assert(k2[0]==k1[0] && k2[1]==k1[1] && k2[2]==k1[2]);
    {uint32_t id=0x00800099u;memcpy(tlio_bodies[3]+0x70,&id,4);}
    lxio_sort(order,order+3,tlio_manager);tlio_keys(k2);assert(k2[0]==k1[0] && k2[1]==k1[1] && k2[2]!=k1[2]);
    /* Windows' record offsets (+0x260/+0x268) are not Linux's: the record found one slot on is the other record */
    {unsigned char *own=tlio_owners[0],*rec=tlio_records[0],*none=NULL;memcpy(own+LXIO_RECORDS,&none,8);memcpy(own+LXIO_RECORDS+0x10,&rec,8);
     lxio_sort(order,order+3,tlio_manager);tlio_keys(k2);assert(k2[0]!=k1[0]);
     memcpy(own+LXIO_RECORDS,&rec,8);memset(own+LXIO_RECORDS+0x10,0,8);lxio_sort(order,order+3,tlio_manager);tlio_keys(k2);assert(k2[0]==k1[0]);}
    /* layout counters, a single constraint, an unreadable manager: as Windows */
    {uint64_t pointer=0x000001a012345678ull;memcpy(tlio_constraints[1]+0x28,&pointer,8);uint32_t bad=7;memcpy(tlio_constraints[0]+0x38,&bad,4);}
    const uint64_t before=lxio_pointerish,bad_before=lxio_badcount;lxio_sort(order,order+3,tlio_manager);
    assert(lxio_pointerish==before+1 && lxio_badcount==bad_before+1);
    const unsigned calls=tlio_original_calls;const uint64_t sorts=lxio_sorts;lxio_sort(order,order+1,tlio_manager);
    assert(tlio_original_calls==calls+1 && lxio_sorts==sorts);
    {unsigned char empty[0x100];memset(empty,0,sizeof(empty));lxio_sort(order,order+3,empty);assert(lxio_failures==1 && tlio_original_calls==calls+2);}
    {char status[512];assert(lxio_status(status,sizeof(status)) && strstr(status,"installed=0 sorts=") && strstr(status," failures=1"));}
    lxio_failures=0;
    /* the trace: the rekey off, constraint 2 (body 3 = actor 11) kept, its cache point's lambdas read at the text */
    tlio_constraint(0,0,2,0.9f,2);tlio_constraint(1,1,2,0.3f,1);tlio_constraint(2,0,3,0.6f,1);
    {unsigned char *p=tlio_cached;const float cache[9]={1,2,3,4,5,6,0.5f,0.25f,-0.125f};memset(tlio_cached,0,sizeof(tlio_cached));
     memcpy(tlio_cached,cache,36);memcpy(tlio_constraints[2]+0x40+0xc0,&p,8);
     const float part[16]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};memcpy(tlio_constraints[2]+0x40,part,64);
     const uint64_t jolt_key=0x1122334455667788ull;memcpy(tlio_constraints[2]+0x10,&jolt_key,8);}
    lxio_rekey=0;ctrace_actor=tlio_actor;ctrace_on=1;ctrace_clear();ctrace_failures=ctrace_seen=ctrace_ordinal=0;
    uint64_t sorts0=lxio_sorts;stats.issued=133;
    order[0]=0;order[1]=1;order[2]=2;lxio_sort(order,order+3,tlio_manager);
    assert(lxio_sorts==sorts0 && ctrace_rows_held()==1 && ctrace_seen==3 && !ctrace_failures);
    {char out[4096];const int n=ctrace_row_text(1,out,sizeof(out));
     assert(n && strstr(out,"tick=133 ordinal=1 index=2 size=3 body1=0:00800030 body2=11:00800099 key=1122334455667788 count=1 head=3f19999a3f0000003e800000"));
     assert(strstr(out," point0=3f80000040000000") && strstr(out," cached0=3f800000400000004040000040800000"
                                                                "40a0000040c000003f0000003e800000be000000") && !strstr(out,"point1"));
     assert(!ctrace_row_text(2,out,sizeof(out)) && !ctrace_row_text(0,out,sizeof(out)));}
    {char status[200];assert(ctrace_status(status,sizeof(status)) && !strcmp(status,"on=1 seen=3 claimed=1 failures=0"));}
    ctrace_clear();assert(!ctrace_rows_held());
    ctrace_on=0;ctrace_actor=NULL;ctrace_seen=ctrace_ordinal=0;stats.issued=0;
    lxio_owner_key=owner_key;lxio_original_override=NULL;lxio_rekey=0;
    puts("island order (Linux): Windows' key bytes (golden), owners' records 8 lower, BodyIDs left out; the CONSTRAINT trace PASS");
}
/* W36s / lx74a tick 133, actors 4 and 11, key ad064e8c9f9ab649: the head (+0x18..+0x3c), the four points' three parts */
static const uint32_t tlio_mass_head[9]={0x3baddd98u,0x3f7ffdf4u,0xbbbffc2bu,0x3dcccccdu,0x3e2aaaabu,0x3f800000u,0x3e1d5845u,0x3f800000u,0x00000004u};
static const uint32_t tlio_mass_linux[4][48]={{0x4093f383u,0x3ce2e424u,0x410ea0b4u,0xc0c900e7u,0x3dbde030u,0x412229bbu,0xba28f668u,0xbaa9f2a9u,0x3ba4b396u,0x3ab29a92u,0xb98eef28u,0x3b925eddu,0x40207b1au,0xbf3803bau,0x00000000u,0x00000000u,0xc0697cdbu,0x410eefebu,0xbd566550u,0x40b52e4au,0x4121b010u,0xbd728531u,0xb963a428u,0x3b4e7b0fu,0xba7c44e4u,0x3ae1cd0eu,0x3a92ba48u,0x3ac7534cu,0x402cd685u,0x00000000u,0x00000000u,0x00000000u,0x3cc8f7fau,0x4094a43du,0x406ac7c9u,0xbd088417u,0xc0ca0d33u,0xc0b2481au,0xb9c7212cu,0x3a8fbc0cu,0x3acfb355u,0xbaeec380u,0xba0e52b0u,0xbb5fbea2u,0x4034edb1u,0x00000000u,0x00000000u,0x00000000u},{0x408fd8abu,0x3ce1127au,0x410c29a5u,0xc0cd1bbfu,0x3dbd6bc6u,0x411fb2abu,0xba308830u,0xbaa8f7c9u,0x3ba2a660u,0x3aa5303au,0xb988f57au,0x3b8d7fa5u,0x4021b8b3u,0xbf3be285u,0x00000000u,0x00000000u,0xc069f08cu,0x410c7905u,0xbd52b302u,0x40b4f471u,0x411f392au,0xbd6ed2e3u,0xb98e43b8u,0x3b497e52u,0xba6eb0c5u,0x3ae22591u,0x3a902963u,0x3ac808adu,0x402d74e1u,0x00000000u,0x00000000u,0x00000000u,0x3cc36492u,0x409089b7u,0x406b3a65u,0xbd0b4dcbu,0xc0ce27b9u,0xc0b20eccu,0xb9de32f0u,0x3a8763d2u,0x3ad5af9du,0xbaedee9eu,0xba12b790u,0xbb5f0e08u,0x403501dau,0x00000000u,0x00000000u,0x00000000u},{0x40ae058du,0x3c919c47u,0x40fead01u,0xc0aeeeddu,0x3da98e39u,0x4112df86u,0x3a0553c0u,0xba45685eu,0x3b7b047au,0x3aac66ceu,0xb9853cd4u,0x3b87337eu,0x402691e6u,0xbf20bc0fu,0x00000000u,0x00000000u,0xc062cb09u,0x40ff4691u,0xbd3f72a2u,0x40b88733u,0x4112636du,0xbd5b9283u,0xb9e1e342u,0x3b328c44u,0xba3b8de4u,0x3aeb0af9u,0x3a822e37u,0x3ad2be25u,0x40305410u,0x00000000u,0x00000000u,0x00000000u,0x3cec619fu,0x40aeb0e7u,0x406374b0u,0xbced9e8au,0xc0b00089u,0xc0b5f1a7u,0xb888d360u,0x3ac78986u,0x3aa21f9eu,0xbaf75ab5u,0xb9e31b3bu,0xbb672cd4u,0x4034045fu,0x00000000u,0x00000000u,0x00000000u},{0x40b3c312u,0x3c8f1f03u,0x41011b7cu,0xc0a93159u,0x3da8eee8u,0x4114a483u,0x3a1db1d0u,0xba3fbfa2u,0x3b7b17ccu,0x3aba34d7u,0xb98b02acu,0x3b8bbb28u,0x402581fdu,0xbf1b6492u,0x00000000u,0x00000000u,0xc0621c41u,0x41016804u,0xbd4219b4u,0x40b8de97u,0x41142829u,0xbd5e3995u,0xb9cb0ad4u,0x3b363db3u,0xba46097cu,0x3aeb0ffbu,0x3a83ffa4u,0x3ad27fc7u,0x402fe99bu,0x00000000u,0x00000000u,0x00000000u,0x3cf42dbbu,0x40b46de9u,0x4062be4cu,0xbce5d26eu,0xc0aa4387u,0xc0b64cd8u,0xb64b9e00u,0x3ad342b5u,0x3a999b84u,0xbaf894bfu,0xb9d6c89cu,0xbb6832d6u,0x4033c8f0u,0x00000000u,0x00000000u,0x00000000u}};
static const uint32_t tlio_mass_windows[4][48]={{0x4093f383u,0x3ce2e424u,0x410ea0b4u,0xc0c900e7u,0x3dbde030u,0x412229bbu,0xba28f668u,0xbaa9f2a9u,0x3ba4b396u,0x3ab29a92u,0xb98eef28u,0x3b925eddu,0x40207b1au,0xbf3803bau,0x00000000u,0x00000000u,0xc0697cdbu,0x410eefebu,0xbd566550u,0x40b52e4au,0x4121b010u,0xbd728531u,0xb963a428u,0x3b4e7b0fu,0xba7c44e4u,0x3ae1cd0eu,0x3a92ba48u,0x3ac7534cu,0x402cd686u,0x00000000u,0x00000000u,0x00000000u,0x3cc8f7fau,0x4094a43du,0x406ac7c9u,0xbd088417u,0xc0ca0d33u,0xc0b2481au,0xb9c7212cu,0x3a8fbc0cu,0x3acfb355u,0xbaeec380u,0xba0e52b0u,0xbb5fbea2u,0x4034edb1u,0x00000000u,0x00000000u,0x00000000u},{0x408fd8abu,0x3ce1127au,0x410c29a5u,0xc0cd1bbfu,0x3dbd6bc6u,0x411fb2abu,0xba308830u,0xbaa8f7c9u,0x3ba2a660u,0x3aa5303au,0xb988f57au,0x3b8d7fa5u,0x4021b8b4u,0xbf3be285u,0x00000000u,0x00000000u,0xc069f08cu,0x410c7905u,0xbd52b302u,0x40b4f471u,0x411f392au,0xbd6ed2e3u,0xb98e43b8u,0x3b497e52u,0xba6eb0c5u,0x3ae22591u,0x3a902963u,0x3ac808adu,0x402d74e2u,0x00000000u,0x00000000u,0x00000000u,0x3cc36492u,0x409089b7u,0x406b3a65u,0xbd0b4dcbu,0xc0ce27b9u,0xc0b20eccu,0xb9de32f0u,0x3a8763d2u,0x3ad5af9du,0xbaedee9eu,0xba12b790u,0xbb5f0e08u,0x403501dau,0x00000000u,0x00000000u,0x00000000u},{0x40ae058du,0x3c919c47u,0x40fead01u,0xc0aeeeddu,0x3da98e39u,0x4112df86u,0x3a0553c0u,0xba45685eu,0x3b7b047au,0x3aac66ceu,0xb9853cd4u,0x3b87337eu,0x402691e6u,0xbf20bc0fu,0x00000000u,0x00000000u,0xc062cb09u,0x40ff4691u,0xbd3f72a2u,0x40b88733u,0x4112636du,0xbd5b9283u,0xb9e1e342u,0x3b328c44u,0xba3b8de4u,0x3aeb0af9u,0x3a822e37u,0x3ad2be25u,0x4030540fu,0x00000000u,0x00000000u,0x00000000u,0x3cec619fu,0x40aeb0e7u,0x406374b0u,0xbced9e8au,0xc0b00089u,0xc0b5f1a7u,0xb888d360u,0x3ac78986u,0x3aa21f9eu,0xbaf75ab5u,0xb9e31b3bu,0xbb672cd4u,0x40340460u,0x00000000u,0x00000000u,0x00000000u},{0x40b3c312u,0x3c8f1f03u,0x41011b7cu,0xc0a93159u,0x3da8eee8u,0x4114a483u,0x3a1db1d0u,0xba3fbfa2u,0x3b7b17ccu,0x3aba34d7u,0xb98b02acu,0x3b8bbb28u,0x402581fdu,0xbf1b6492u,0x00000000u,0x00000000u,0xc0621c41u,0x41016804u,0xbd4219b4u,0x40b8de97u,0x41142829u,0xbd5e3995u,0xb9cb0ad4u,0x3b363db3u,0xba46097cu,0x3aeb0ffbu,0x3a83ffa4u,0x3ad27fc7u,0x402fe99bu,0x00000000u,0x00000000u,0x00000000u,0x3cf42dbbu,0x40b46de9u,0x4062be4cu,0xbce5d26eu,0xc0aa4387u,0xc0b64cd8u,0xb64b9e00u,0x3ad342b5u,0x3a999b84u,0xbaf894bfu,0xb9d6c89cu,0xbb6832d6u,0x4033c8f0u,0x00000000u,0x00000000u,0x00000000u}};
/* The contact mass: Linux's tick-133 constraint (Jolt's order) through the fix gives Windows' words for every part; a
 * kinematic body leaves the constraint (other); inv 0 deactivates the part (mass 0, total lambda 0). */
static void test_contact_mass_linux(void) {
    static _Alignas(16) unsigned char c[LXIO_STRIDE],b[2][0x80],manager[0x100];
    unsigned char *pc=c,*p0=b[0],*p1=b[1];const uint32_t count=4;unsigned apart=0;
    memset(c,0,sizeof(c));memset(b,0,sizeof(b));memset(manager,0,sizeof(manager));
    memcpy(c,&p0,8);memcpy(c+8,&p1,8);memcpy(c+0x18,tlio_mass_head,36);memcpy(c+0x38,&count,4);
    for(unsigned k=0;k<4;k++) memcpy(c+0x40+LXIO_POINT*k,tlio_mass_linux[k],0xc0);
    for(unsigned k=0;k<4;k++) apart+=memcmp(tlio_mass_linux[k],tlio_mass_windows[k],0xc0)!=0;
    assert(apart==3);   /* points 0, 1, 2 hold a part 1 ulp apart */
    b[0][0x78]=2;b[1][0x78]=2;memcpy(manager+LXIO_ARRAY,&pc,8);
    uint32_t order[1]={0};lxio_mass_constraints=lxio_mass_changed=lxio_mass_other=0;
    assert(lxio_mass_range(order,order+1,manager) && lxio_mass_constraints==1 && lxio_mass_changed==5 && !lxio_mass_other);
    for(unsigned k=0;k<4;k++) assert(!memcmp(c+0x40+LXIO_POINT*k,tlio_mass_windows[k],0xc0));
    assert(lxio_mass_range(order,order+1,manager) && lxio_mass_changed==5);   /* again: nothing to change */
    b[1][0x78]=1;for(unsigned k=0;k<4;k++) memcpy(c+0x40+LXIO_POINT*k,tlio_mass_linux[k],0xc0);
    assert(lxio_mass_range(order,order+1,manager) && lxio_mass_other==1 && !memcmp(c+0x40,tlio_mass_linux[0],0xc0));
    b[1][0x78]=2;memset(c+0x40,0,0x40);{const float lambda=3.0f;memcpy(c+0x40+0x3c,&lambda,4);}
    {const float zero=0.0f;memcpy(c+0x28,&zero,4);memcpy(c+0x30,&zero,4);}
    assert(lxio_mass_range(order,order+1,manager));{float mass,lambda;memcpy(&mass,c+0x40+0x30,4);memcpy(&lambda,c+0x40+0x3c,4);assert(mass==0.0f && lambda==0.0f);}
    {unsigned char empty[0x100];memset(empty,0,sizeof(empty));assert(!lxio_mass_range(order,order+1,empty));}
    lxio_mass_constraints=lxio_mass_changed=lxio_mass_other=0;
    puts("contact mass (Linux): lx74a's tick-133 constraint in MSVC's order equals W36s's words, other motion types left, inv 0 deactivates PASS");
}
/* GetContactsFromCache's copy: W119's and lx132a's tick-1198 constraint 0 (the Argon and actor 9, taken from the body
 * pair cache), Linux's words through AddContactConstraint's order (lx132a, the fix before) and Windows' words. */
static const uint32_t tlio_cache_head[9]={0xbe12393eu,0x3f7c11deu,0xbdcd9938u,0x3dcccccdu,0x3e2aaaabu,0x3f800000u,0x3e1d5845u,0x3f800000u,0x00000004u};
static const uint32_t tlio_cache_linux[4][48]={{0x3fc8fa24u,0x3e8d9b4au,0x3ef56a90u,0x4088ce92u,0xbd891488u,0xc0d79ad3u,0x3a06bcd8u,0x3982de8au,0x38a697c0u,0xb87017c0u,0xb9e58c10u,0xbab1a035u,0x4041d975u,0x00000000u,0x00000000u,0x00000000u,0xc181d4f2u,0xbfe93285u,0x3e3e3494u,0x40dbbd02u,0xc0b84672u,0x3f164d6du,0xbbaf1391u,0xbb4d1b8du,0x3a8168c1u,0x3afc2511u,0x3896948eu,0x3a85f9e5u,0x40161fa6u,0x00000000u,0x00000000u,0x00000000u,0xbe67f7c1u,0x404df033u,0x4180a551u,0xbf1de70eu,0x4069730au,0xc0cbed5eu,0xb931c026u,0xbae71684u,0x3bb9ce6cu,0xbaa8ff91u,0xb9e478d8u,0xbaf44a53u,0x40197e5cu,0x00000000u,0x00000000u,0x80000000u},{0x3fe503e1u,0x3e9f34a4u,0x3f014084u,0x408fd101u,0xbd055e40u,0xc0d6c96au,0x3a19a298u,0x399a6b88u,0x389c0784u,0x387ced80u,0xb9c095a8u,0xbaa6bb1eu,0x4041c001u,0x00000000u,0x00000000u,0x00000000u,0xc1819a9eu,0xbfe4f427u,0x3e3abe7du,0x40dca652u,0xc0b736dbu,0x3f156fe7u,0xbbaebc1au,0xbb49f720u,0x3a7eff6eu,0x3afed168u,0x38b91edeu,0x3a87e03bu,0x40164479u,0x00000000u,0x00000000u,0x00000000u,0xbe842a26u,0x405babcfu,0x418034b0u,0xbf25fe30u,0x40772ea6u,0xc0cdafe3u,0xb934342eu,0xbabdb278u,0x3bb656d9u,0xbaa617d7u,0xb9cc7f5cu,0xbaf229f5u,0x4019eea8u,0x00000000u,0x00000000u,0x80000000u},{0x3f63e274u,0x3e6cc795u,0x3f80399eu,0x406618b0u,0xbde5f284u,0xc0c6e314u,0x39953a72u,0xb81ac990u,0x39acf60fu,0xb96d9708u,0xba049a43u,0xbab2fdd9u,0x4042bfa9u,0x00000000u,0x00000000u,0x00000000u,0xc182b768u,0xbfa8f3c8u,0x3e09cdf0u,0x40d8332du,0xc0a836c3u,0x3f0933c4u,0xbbafadc0u,0xbb1ebbf0u,0x3a4ac857u,0x3b01e13eu,0x3943bbddu,0x3a8d7612u,0x4016de24u,0x00000000u,0x00000000u,0x00000000u,0xbe03831fu,0x4022e359u,0x41817ec3u,0xbf04c9e5u,0x403e6630u,0xc0c88796u,0xb9298207u,0xbb32c08fu,0x3bc3be5au,0xbab4c3e4u,0xba1bafe8u,0xbafe5f2du,0x4017f1bfu,0x00000000u,0x00000000u,0x80000000u},{0x3fa42e1cu,0x3e792985u,0x3f0fe4fau,0x407f3720u,0xbdcd2ea8u,0xc0d4f4dcu,0x39db4edfu,0x392a1da3u,0x3909d13eu,0xb93627a0u,0xba0362e8u,0xbab9a6dcu,0x4042150du,0x00000000u,0x00000000u,0x00000000u,0xc182290cu,0xbfdfcb00u,0x3e3688e7u,0x40da6c9eu,0xc0b5ec91u,0x3f146282u,0xbbaf6e05u,0xbb465fd5u,0x3a7ab2d9u,0x3afbc031u,0x38ace42eu,0x3a861848u,0x401630beu,0x00000000u,0x00000000u,0x00000000u,0xbe3d7f26u,0x403bd47eu,0x41811e5bu,0xbf1348e7u,0x40575755u,0xc0ca0937u,0xb92e6764u,0xbb0e7ebcu,0x3bbe307au,0xbaad5e2eu,0xba02d3c0u,0xbaf7c942u,0x4018dd9fu,0x00000000u,0x00000000u,0x80000000u}};
static const uint32_t tlio_cache_windows[4][48]={{0x3fc8fa24u,0x3e8d9b4au,0x3ef56a90u,0x4088ce92u,0xbd891488u,0xc0d79ad3u,0x3a06bcd8u,0x3982de8au,0x38a697c0u,0xb87017c0u,0xb9e58c10u,0xbab1a035u,0x4041d976u,0x00000000u,0x00000000u,0x00000000u,0xc181d4f2u,0xbfe93285u,0x3e3e3494u,0x40dbbd02u,0xc0b84672u,0x3f164d6du,0xbbaf1391u,0xbb4d1b8du,0x3a8168c1u,0x3afc2511u,0x3896948eu,0x3a85f9e5u,0x40161fa6u,0x00000000u,0x00000000u,0x00000000u,0xbe67f7c1u,0x404df033u,0x4180a551u,0xbf1de70eu,0x4069730au,0xc0cbed5eu,0xb931c026u,0xbae71684u,0x3bb9ce6cu,0xbaa8ff91u,0xb9e478d8u,0xbaf44a53u,0x40197e5cu,0x00000000u,0x00000000u,0x80000000u},{0x3fe503e1u,0x3e9f34a4u,0x3f014084u,0x408fd101u,0xbd055e40u,0xc0d6c96au,0x3a19a298u,0x399a6b88u,0x389c0784u,0x387ced80u,0xb9c095a8u,0xbaa6bb1eu,0x4041c001u,0x00000000u,0x00000000u,0x00000000u,0xc1819a9eu,0xbfe4f427u,0x3e3abe7du,0x40dca652u,0xc0b736dbu,0x3f156fe7u,0xbbaebc1au,0xbb49f720u,0x3a7eff6eu,0x3afed168u,0x38b91edeu,0x3a87e03bu,0x40164479u,0x00000000u,0x00000000u,0x00000000u,0xbe842a26u,0x405babcfu,0x418034b0u,0xbf25fe30u,0x40772ea6u,0xc0cdafe3u,0xb934342eu,0xbabdb278u,0x3bb656d9u,0xbaa617d7u,0xb9cc7f5cu,0xbaf229f5u,0x4019eea8u,0x00000000u,0x00000000u,0x80000000u},{0x3f63e274u,0x3e6cc795u,0x3f80399eu,0x406618b0u,0xbde5f284u,0xc0c6e314u,0x39953a72u,0xb81ac990u,0x39acf60fu,0xb96d9708u,0xba049a43u,0xbab2fdd9u,0x4042bfa9u,0x00000000u,0x00000000u,0x00000000u,0xc182b768u,0xbfa8f3c8u,0x3e09cdf0u,0x40d8332du,0xc0a836c3u,0x3f0933c4u,0xbbafadc0u,0xbb1ebbf0u,0x3a4ac857u,0x3b01e13eu,0x3943bbddu,0x3a8d7612u,0x4016de24u,0x00000000u,0x00000000u,0x00000000u,0xbe03831fu,0x4022e359u,0x41817ec3u,0xbf04c9e5u,0x403e6630u,0xc0c88796u,0xb9298207u,0xbb32c08fu,0x3bc3be5au,0xbab4c3e4u,0xba1bafe8u,0xbafe5f2du,0x4017f1c0u,0x00000000u,0x00000000u,0x80000000u},{0x3fa42e1cu,0x3e792985u,0x3f0fe4fau,0x407f3720u,0xbdcd2ea8u,0xc0d4f4dcu,0x39db4edfu,0x392a1da3u,0x3909d13eu,0xb93627a0u,0xba0362e8u,0xbab9a6dcu,0x4042150du,0x00000000u,0x00000000u,0x00000000u,0xc182290cu,0xbfdfcb00u,0x3e3688e7u,0x40da6c9eu,0xc0b5ec91u,0x3f146282u,0xbbaf6e05u,0xbb465fd5u,0x3a7ab2d9u,0x3afbc031u,0x38ace42eu,0x3a861848u,0x401630beu,0x00000000u,0x00000000u,0x00000000u,0xbe3d7f26u,0x403bd47eu,0x41811e5bu,0xbf1348e7u,0x40575755u,0xc0ca0937u,0xb92e6764u,0xbb0e7ebcu,0x3bbe307au,0xbaad5e2eu,0xba02d3c0u,0xbaf7c942u,0x4018dd9fu,0x00000000u,0x00000000u,0x80000000u}};
static unsigned char tlio_cache_creates;
static void tlio_cache_fake(void *m,void *a,void *b1,void *b2,unsigned char *handled,unsigned char *created) {
    (void)m;(void)a;(void)b1;(void)b2;*handled=1;*created=tlio_cache_creates;
}
static void test_contact_mass_cache(void) {
    static _Alignas(16) unsigned char c[LXIO_STRIDE],b[2][0x80],manager[0x100];
    unsigned char *pc=c,*p0=b[0],*p1=b[1];const uint32_t count=4;
    memset(c,0,sizeof(c));memset(b,0,sizeof(b));memset(manager,0,sizeof(manager));
    memcpy(c,&p0,8);memcpy(c+8,&p1,8);memcpy(c+0x18,tlio_cache_head,36);memcpy(c+0x38,&count,4);
    b[0][0x78]=2;b[1][0x78]=2;memcpy(manager+LXIO_ARRAY,&pc,8);
    uint32_t order[1]={0};const uint64_t tick=stats.issued;
    lxio_mass_constraints=lxio_mass_changed=lxio_mass_other=lxio_mass_cached=lxio_cache_pairs=0;
    lxio_cache_head=0;memset(lxio_cache_ring,0,sizeof(lxio_cache_ring));
    /* not from the cache: AddContactConstraint's order keeps Linux's words (two of them 1 ulp from Windows') */
    for(unsigned k=0;k<4;k++) memcpy(c+0x40+LXIO_POINT*k,tlio_cache_linux[k],0xc0);
    assert(lxio_mass_range(order,order+1,manager) && !lxio_mass_cached);
    for(unsigned k=0;k<4;k++) assert(!memcmp(c+0x40+LXIO_POINT*k,tlio_cache_linux[k],0xc0));
    assert(memcmp(tlio_cache_linux[0],tlio_cache_windows[0],0xc0) && memcmp(tlio_cache_linux[2],tlio_cache_windows[2],0xc0));
    /* a pair noted at an older tick does not count; noted now (either order), the cache copy gives Windows' words */
    lxio_cache_note(p0,p1,tick+5u);
    assert(lxio_cache_made(p0,p1,tick)==0 && lxio_mass_range(order,order+1,manager) && !lxio_mass_cached);
    lxio_cache_note(p1,p0,tick);
    assert(lxio_cache_made(p0,p1,tick)==1 && lxio_mass_range(order,order+1,manager) && lxio_mass_cached==1);
    for(unsigned k=0;k<4;k++) assert(!memcmp(c+0x40+LXIO_POINT*k,tlio_cache_windows[k],0xc0));
    /* the wrapper runs the call and notes the pair only when it made a constraint */
    {unsigned char handled=0,created=0;const uint64_t before=lxio_cache_pairs;lxio_cache_override=tlio_cache_fake;
     tlio_cache_creates=0;lxio_cache_call(NULL,NULL,p0,p1,&handled,&created);assert(handled==1 && !created && lxio_cache_pairs==before);
     tlio_cache_creates=1;lxio_cache_call(NULL,NULL,p0,p1,&handled,&created);assert(created==1 && lxio_cache_pairs==before+1);
     lxio_cache_override=NULL;}
    /* a ring full of this tick's other pairs: unknown, the range fails */
    {static unsigned char other[2];lxio_cache_head=0;memset(lxio_cache_ring,0,sizeof(lxio_cache_ring));
     for(unsigned i=0;i<LXIO_CACHE_RING;i++) lxio_cache_note(&other[0],&other[1],tick);
     assert(lxio_cache_made(p0,p1,tick)==-1 && !lxio_mass_range(order,order+1,manager));}
    lxio_cache_head=0;memset(lxio_cache_ring,0,sizeof(lxio_cache_ring));
    lxio_mass_constraints=lxio_mass_changed=lxio_mass_other=lxio_mass_cached=lxio_cache_pairs=0;
    puts("contact mass (Linux): a constraint from the body pair cache takes GetContactsFromCache's order (W119's tick-1198 words), others AddContactConstraint's PASS");
}
static void test_island_order_elf(void) {
    for(unsigned i=0;i<sizeof(lxio_pin_names)/sizeof(*lxio_pin_names);i++) assert(site_ok(site_named(lxio_pin_names[i])));
    assert(lxio_call_state()==0 && LX(LXIO_SITE_VA)[0]==0xe8);
    assert(site_ok(site_named("cmass_cache_call")) && lxio_cache_state()==0);
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();
    lxio_installed=lxio_restored=lxio_rekey=0;lxio_stage="idle";
    {const int identities=world_identity_installed;world_identity_installed=0;assert(!lxio_arm() && !strcmp(lxio_stage,"needs"));world_identity_installed=identities;}
    /* the trace alone: installed, no rekey; then the island order on the same stub */
    assert(lxio_trace_arm() && lxio_installed && !lxio_rekey && ctrace_on && lxio_call_state()==1 && lxio_page_ok() && lxio_check());
    {int32_t r;const unsigned char *at=LX(LXIO_SITE_VA);void *to;memcpy(&r,at+1,4);assert(at+5+r==lxio_page);
     assert(lxio_page[0]==0xff && lxio_page[1]==0x25);memcpy(&to,lxio_page+6,8);assert(to==(void*)lxio_sort);}
    world_identity_installed=1;assert(lxio_arm() && lxio_rekey && lxio_arm());
    {char status[384];assert(lxio_status(status,sizeof(status)) && strstr(status,"installed=1 sorts=0"));}
    lxio_restore();assert(!lxio_installed && lxio_restored && !ctrace_on && lxio_call_state()==0 && lxio_pins());
    world_identity_installed=0;assert(!lxio_arm());   /* needs the identities */
    world_identity_installed=1;stats.issued=5;assert(!lxio_arm() && !strcmp(lxio_stage,"guard"));stats.issued=0;
    assert(lxio_arm() && lxio_installed && lxio_rekey && lxio_check());
    lxio_restore();assert(!lxio_installed && lxio_call_state()==0);
    munmap(lxio_page,GATE_PAGE);lxio_page=NULL;lxio_restored=lxio_rekey=0;lxio_stage="idle";world_identity_installed=0;stats.active=0;ctrace_actor=NULL;
    puts("island order ELF: pins, the sort call to the page and back, the trace alone, the identities needed PASS");
}
