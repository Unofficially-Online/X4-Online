/* lockstep_pilot_foot_doors.h: the answer about a trigger with walking slots (synthetic: X4's own test and the walking
 * slots are stood in for; the doors themselves are checked live: tools/dev/action_tests.py rooms). */
static unsigned char test_pfdoor_players[4][8];
static int test_pfdoor_calls;
static const void *test_pfdoor_at;            /* the one entity that stands at the trigger */
static unsigned test_pfdoor_walking;          /* bit k: slot k walks */
static uint64_t test_pfdoor_native(void *unused,void *entity,void *component,void *trigger) {
    (void)unused;(void)component;(void)trigger;++test_pfdoor_calls;
    return 0x1200u|(entity==test_pfdoor_at?1u:0u);
}
static unsigned char *test_pfdoor_walker(unsigned k) {return k<4 && (test_pfdoor_walking>>k&1u)?test_pfdoor_players[k]:NULL;}
static int test_pfdoor_slot(unsigned char *player,void *component,void *trigger) {return (int)(test_pfdoor_native(NULL,player,component,trigger)&1u);}
static void test_pilot_foot_doors(void) {
    const int was=pfdoor_installed;const pfdoor_present_fn native=pfdoor_native;unsigned char *(*const walker)(unsigned)=pfdoor_walker_of;
    int (*const slot_at)(unsigned char *,void *,void *)=pfdoor_slot_at;pfdoor_slot_at=test_pfdoor_slot;
    const uint64_t issued=stats.issued;
    unsigned char npc[8],door[8],box[8],box2[8];
    pfdoor_native=test_pfdoor_native;pfdoor_walker_of=test_pfdoor_walker;pfdoor_installed=1;memset(&pfdoor_last,0,sizeof(pfdoor_last));
    /* X4's own yes stands, and nobody's slot is asked then */
    stats.issued=500;test_pfdoor_walking=1u<<2;test_pfdoor_at=npc;test_pfdoor_calls=0;
    assert((pfdoor_present(NULL,npc,door,box)&0xffu)==1 && test_pfdoor_calls==1);
    /* nobody there and nobody walks: no */
    test_pfdoor_walking=0;test_pfdoor_at=NULL;test_pfdoor_calls=0;
    assert((pfdoor_present(NULL,npc,door,box)&0xffu)==0 && test_pfdoor_calls==1);
    /* slot 2 walks elsewhere, slot 3 stands at the trigger: yes, X4's upper bytes kept (the entity, slot 2, slot 3) */
    stats.issued=501;test_pfdoor_walking=(1u<<2)|(1u<<3);test_pfdoor_at=test_pfdoor_players[3];test_pfdoor_calls=0;
    const uint64_t yes=pfdoor_present(NULL,npc,door,box);
    assert((yes&0xffu)==1 && (yes&~(uint64_t)0xffu)==0x1200u && test_pfdoor_calls==3);
    /* the same trigger in the same tick (the next entity of X4's walk): the slots' answer is kept */
    test_pfdoor_calls=0;assert((pfdoor_present(NULL,npc,door,box)&0xffu)==1 && test_pfdoor_calls==1);
    /* another trigger, and the next tick: asked again */
    test_pfdoor_calls=0;assert((pfdoor_present(NULL,npc,door,box2)&0xffu)==1 && test_pfdoor_calls==3);
    stats.issued=502;test_pfdoor_at=NULL;test_pfdoor_calls=0;
    assert((pfdoor_present(NULL,npc,door,box2)&0xffu)==0 && test_pfdoor_calls==3);
    /* not installed: X4's answer alone */
    pfdoor_installed=0;stats.issued=503;test_pfdoor_at=test_pfdoor_players[2];test_pfdoor_calls=0;
    assert((pfdoor_present(NULL,npc,door,box)&0xffu)==0 && test_pfdoor_calls==1);
    /* X4's cylinder: inside the half sizes, within the radius across (outside the box), the height up and down */
    {const float box[14]={1.0f,2.0f,0.5f,0,10.0f,0.0f,-3.0f,0,0,0,0,0,1.5f,0.25f};
     const float inside[3]={10.5f,1.0f,-3.2f},across[3]={12.4f,0.0f,-3.0f},beyond[3]={12.6f,0.0f,-3.0f},above[3]={10.0f,2.3f,-3.0f},high[3]={10.0f,2.2f,-3.0f};
     assert(pfdoor_in_cylinder(inside,box) && pfdoor_in_cylinder(across,box) && !pfdoor_in_cylinder(beyond,box));
     assert(!pfdoor_in_cylinder(above,box) && pfdoor_in_cylinder(high,box));}
    pfdoor_installed=was;pfdoor_native=native;pfdoor_walker_of=walker;pfdoor_slot_at=slot_at;stats.issued=issued;memset(&pfdoor_last,0,sizeof(pfdoor_last));
    pfdoor_asks=pfdoor_slot_asks=pfdoor_opened=pfdoor_other_room=pfdoor_far=0;test_pfdoor_walking=0;test_pfdoor_at=NULL;
    puts("pilot foot doors: X4's answer, walking slots at a trigger, kept for a tick's walk PASS");
}
/* The pinned image: both functions as studied, the entry's first instruction the trampoline's, the entry inside one
 * 16-byte block (pview_cas writes the jump there), and 0x73a0f0's four calls of it. */
static void test_pilot_foot_doors_native_image(const char *path) {
    unsigned char *mapped=test_pmath_map(path);
    assert(hash_ok(mapped+PFDOOR_PRESENT,PFDOOR_PRESENT_SIZE,PFDOOR_PRESENT_SHA));
    assert(hash_ok(mapped+PFDOOR_PASS,PFDOOR_PASS_SIZE,PFDOOR_PASS_SHA));
    assert(!memcmp(mapped+PFDOOR_PRESENT,pfdoor_head,sizeof(pfdoor_head)) && ((PFDOOR_PRESENT+4u)&~15u)==(PFDOOR_PRESENT&~15u));
    assert(hash_ok(mapped+PFDOOR_ROOM,PFDOOR_ROOM_SIZE,PFDOOR_ROOM_SHA) && hash_ok(mapped+PFDOOR_FILTER,PFDOOR_FILTER_SIZE,PFDOOR_FILTER_SHA));
    static const unsigned calls[4]={0x73a4feu,0x73a5e0u,0x73a7f3u,0x73a89cu};
    for(unsigned i=0;i<4;i++) {
        int32_t rel=0;memcpy(&rel,mapped+calls[i]+1,4);
        assert(mapped[calls[i]]==0xe8 && (unsigned)(calls[i]+5u+(unsigned)rel)==PFDOOR_PRESENT);
    }
    {unsigned char keep=mapped[PFDOOR_PRESENT+0x20];mapped[PFDOOR_PRESENT+0x20]^=1;
     assert(!hash_ok(mapped+PFDOOR_PRESENT,PFDOOR_PRESENT_SIZE,PFDOOR_PRESENT_SHA));mapped[PFDOOR_PRESENT+0x20]=keep;}
    printf("pinned native image pilot foot doors verified (the presence test 0x739e30 and its entry, the trigger pass 0x73a0f0 and its four calls)\n");
}
