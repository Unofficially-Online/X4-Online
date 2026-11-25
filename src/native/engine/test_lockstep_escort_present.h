/* A ship that protects a player's own ship drawn in the player's colour on that player's client
 * (lockstep_escort_present.h): the set, the two answers, the pins of this build. */
static unsigned char tesc_owned_calls,tesc_rel_calls;
static unsigned char tesc_rel_stock(unsigned char *component) {tesc_rel_calls++;return component && component[0]==1;}
static unsigned char tesc_owned_stock(unsigned char *component) {tesc_owned_calls++;return component && component[0]==1;}
static unsigned char *tesc_component(unsigned char *at,uint64_t id,unsigned char own) {memset(at,0,32);at[0]=own;memcpy(at+8,&id,8);return at;}
static void test_escort_present(void) {
    unsigned char a[32],b[32],c[32];
    unsigned char *wingmate=tesc_component(a,0x163c1fu,0),*other=tesc_component(b,0x3d848u,0),*mine=tesc_component(c,0x3d85bu,1);
    esc_rel_test=tesc_rel_stock;esc_owned_test=tesc_owned_stock;esc_count=0;esc_rel_hits=esc_map_hits=esc_sets=0;
    /* No set: X4's answers for everything (the protector of another player is blue, a player's own ship green). */
    assert(esc_rel_own(wingmate)==0 && esc_rel_own(mine)==1 && esc_rel_own(NULL)==0 && tesc_rel_calls==3);
    assert(esc_map_own(wingmate)==0 && esc_map_own(mine)==1 && tesc_owned_calls==2);
    /* This client's escorts: own property in both answers, without asking X4; every other object as before. */
    {const uint64_t ids[2]={0x163c1fu,0x9999u};assert(esc_set(ids,2) && esc_count==2 && esc_sets==1);}
    tesc_rel_calls=tesc_owned_calls=0;
    assert(esc_rel_own(wingmate)==1 && esc_map_own(wingmate)==1 && tesc_rel_calls==0 && tesc_owned_calls==0 && esc_rel_hits==1 && esc_map_hits==1);
    assert(esc_rel_own(other)==0 && esc_map_own(other)==0 && esc_rel_own(mine)==1 && tesc_rel_calls==2 && tesc_owned_calls==1);
    /* A new list replaces the old one; an empty one clears it; more than ESC_MAX is refused and the old list stays. */
    {const uint64_t ids[1]={0x3d848u};assert(esc_set(ids,1) && esc_rel_own(other)==1 && esc_rel_own(wingmate)==0);}
    {uint64_t many[ESC_MAX+1];for(unsigned i=0;i<=ESC_MAX;i++) many[i]=i+1;assert(!esc_set(many,ESC_MAX+1) && esc_count==1 && esc_rel_own(other)==1);}
    assert(esc_set(NULL,0) && esc_count==0 && esc_rel_own(other)==0 && esc_map_own(other)==0);
    {char status[160];assert(esc_status(status,sizeof(status))>0 && !strcmp(status,"armed=0 set=0 sets=3 rel=3 map=1 why=-"));}
    esc_rel_test=NULL;esc_owned_test=NULL;esc_rel_hits=esc_map_hits=esc_sets=0;tesc_rel_calls=tesc_owned_calls=0;
    printf("escort present: this client's escorts are own property in the HUD's and the map's answers, every other object X4's; tests PASS\n");
}
/* The pinned image: the test's head and bytes, its three callers (the UI exports), the map colour's two calls of 0x3e88d0
 * (each inside one 16-byte block, the object in rcx); one changed byte refuses. */
static void test_escort_present_native_image(const char *path) {
    unsigned char *prior=image,*prior_base=pnat_base;
    image=test_pmath_map(path);pnat_base=image;
    assert(esc_pins());
    image[ESC_REL+1]^=1;assert(!esc_pins());image[ESC_REL+1]^=1;
    image[ESC_REL+0x80]^=1;assert(!esc_pins());image[ESC_REL+0x80]^=1;
    image[ESC_MAP+0x100]^=1;assert(!esc_pins());image[ESC_MAP+0x100]^=1;
    for(unsigned i=0;i<3;i++) {image[esc_rel_callers[i]+2]^=1;assert(!esc_pins());image[esc_rel_callers[i]+2]^=1;}
    assert(esc_pins());
    /* the object in rcx at both calls: mov rcx,rbx before each (the first one ahead of a movzx ebp,al) */
    {static const uint32_t moves[ESC_SITES]={0xdb681du,0xdb6862u};
     static const unsigned char mov_rcx_rbx[3]={0x48,0x8b,0xcb};
     for(unsigned i=0;i<ESC_SITES;i++) assert(!memcmp(image+moves[i],mov_rcx_rbx,3) && moves[i]<esc_map_sites[i] && esc_map_sites[i]-moves[i]<=6);}
    /* The test asks "the player faction owns it and it is not the player's ship" (0x3e88d0) of the component in rcx. */
    {int32_t rel;memcpy(&rel,image+0x8c7283u,4);assert(image[0x8c7282u]==0xe8 && 0x8c7287u+(uint32_t)rel==ESC_OWNED);}
    assert(ESC_REL_TRAMPOLINE>=ESC_REL_STUB+14 && ESC_REL_TRAMPOLINE+sizeof(esc_rel_head)+14<=ESC_MAP_STUB && ESC_MAP_STUB+14<=0x60);
    VirtualFree(image,0,MEM_RELEASE);image=prior;pnat_base=prior_base;
    printf("escort present: the pinned colour test and map colour of this build; tests PASS\n");
}
