/* A player's own faction as the player faction on its own client (lockstep_faction_present.h): the exchange of two
 * nodes of a stand-in faction tree in every relative position, what the lookup then finds, every byte back after the
 * second exchange; the scope's enter and exit with their refusals. No native code runs. */
#define TFP_NODES 9u
#define TFP_NODE 0x60u
static _Alignas(16) unsigned char tfp_arena[0x40+TFP_NODES*TFP_NODE];
static void *volatile tfp_cell;
static void *tfp_slot_faction[4];
static void *tfp_known_set,*tfp_known_by[2];static unsigned tfp_known_asked;
/* X4's "known to" in the test: the set is known to up to two factions. */
static unsigned char tfp_known(void *set,void *faction) {
    ++tfp_known_asked;
    return set==tfp_known_set && faction && (faction==tfp_known_by[0] || faction==tfp_known_by[1]);
}
static void *tfp_owner;static unsigned tfp_info_asked;
static void *tfp_owner_of(void *component) {(void)component;return tfp_owner;}
static unsigned char tfp_info(void *manager,int kind,unsigned char *component) {(void)manager;(void)component;++tfp_info_asked;return kind==7;}
static void *tfp_cast_asked;static unsigned tfp_cast_calls;static unsigned char tfp_cast_flag;static void *tfp_cast_a6;
/* X4's lookup in the test: remembers the one faction it was asked about. */
static void *tfp_cast(void *definitions,void *tags,void *a3,unsigned char *factions,unsigned char flag,void *a6) {
    (void)definitions;(void)tags;(void)a3;uint64_t capacity,size;void *data;
    ++tfp_cast_calls;tfp_cast_flag=flag;tfp_cast_a6=a6;tfp_cast_asked=NULL;
    memcpy(&capacity,factions+0x10,8);memcpy(&size,factions+0x18,8);
    if(size>=1) {if(capacity==2) data=factions;else memcpy(&data,factions,8);memcpy(&tfp_cast_asked,data,8);}
    return tfp_cast_asked;
}
static unsigned char *tfp_list(unsigned char *list,void *faction,int inline_data,void **heap) {
    uint64_t capacity=inline_data?2:4,size=faction?1:0;memset(list,0,0x20);
    if(inline_data) memcpy(list,&faction,8);else {heap[0]=faction;memcpy(list,&heap,8);}
    memcpy(list+0x10,&capacity,8);memcpy(list+0x18,&size,8);return list;
}
static unsigned char *tfp_anchor(void) {return tfp_arena+FPRES_ANCHOR;}
static void *volatile *tfp_cell_of(void) {return &tfp_cell;}
static void *tfp_faction_of(unsigned slot) {return slot<4?tfp_slot_faction[slot]:NULL;}
static unsigned char *tfp_node(unsigned i) {return tfp_arena+0x40+i*TFP_NODE;}
static unsigned char *tfp_faction(unsigned i) {return tfp_node(i)+FPRES_N_FACTION;}
static void tfp_name(unsigned i,const char *id) {
    unsigned char *f=tfp_faction(i);uint64_t h=fpres_hash(id),n=strlen(id),cap=15;
    memset(f,0,TFP_NODE-FPRES_N_FACTION);
    memcpy(f+FPRES_F_HASH,&h,8);memcpy(f+FPRES_F_ID,id,n);memcpy(f+FPRES_F_ID+0x10,&n,8);memcpy(f+FPRES_F_ID+0x18,&cap,8);
    memcpy(tfp_node(i)+FPRES_N_KEY,&h,8);
}
/* A plain insert by key; the colour byte is the node's number, so the test sees it travel with the place. */
static void tfp_insert(unsigned i) {
    unsigned char *anchor=tfp_anchor(),*n=tfp_node(i),*at=anchor,*next=fpres_get(anchor+FPRES_N_LEFT);unsigned side=FPRES_N_LEFT;
    while(next) {at=next;side=fpres_key(at+FPRES_N_KEY)<fpres_key(n+FPRES_N_KEY)?FPRES_N_RIGHT:FPRES_N_LEFT;next=fpres_get(at+side);}
    fpres_put(at+side,n);fpres_put(n+FPRES_N_PARENT,at);fpres_put(n+FPRES_N_LEFT,NULL);fpres_put(n+FPRES_N_RIGHT,NULL);n[0x18]=(unsigned char)(i+1);
}
/* The tree's order and its links hold below a node; counts the nodes. */
static unsigned tfp_check(unsigned char *n,unsigned char *parent,uint64_t low,uint64_t high) {
    if(!n) return 0;
    uint64_t key=fpres_key(n+FPRES_N_KEY);
    assert(fpres_get(n+FPRES_N_PARENT)==parent && key>low && key<high && key==fpres_key(n+FPRES_N_FACTION+FPRES_F_HASH));
    return 1+tfp_check(fpres_get(n+FPRES_N_LEFT),n,low,key)+tfp_check(fpres_get(n+FPRES_N_RIGHT),n,key,high);
}
static unsigned char *tfp_leftmost(void) {
    unsigned char *n=fpres_get(tfp_anchor()+FPRES_N_LEFT);
    while(fpres_get(n+FPRES_N_LEFT)) n=fpres_get(n+FPRES_N_LEFT);
    return n;
}
static const char *const tfp_ids[TFP_NODES]={"player","x4lan_player2","argon","paranid","teladi","xenon","x4lan_player3","khaak","split"};
static void tfp_build(const unsigned order[TFP_NODES]) {
    unsigned char *anchor=tfp_anchor();
    memset(tfp_arena,0,sizeof(tfp_arena));
    fpres_put(anchor+FPRES_N_PARENT,anchor);
    for(unsigned i=0;i<TFP_NODES;i++) tfp_name(i,tfp_ids[i]);
    for(unsigned i=0;i<TFP_NODES;i++) tfp_insert(order[i]);
    fpres_put(anchor+FPRES_BEGIN,tfp_leftmost());
}
static void test_faction_present(void) {
    static unsigned char before[sizeof(tfp_arena)];
    unsigned char *anchor=tfp_anchor();
    /* X4's hash of the two ids, as read from a running client. */
    assert(fpres_hash("player")==0x71c3ba713fbe3c2aULL && fpres_hash("x4lan_player2")==0x9a39ab21d5e005a0ULL);
    /* Every insertion order of a few (the two nodes far apart, parent and child either way, under one parent, at the
     * root, leftmost), every pair of nodes: after the exchange the tree is in order, each id finds the other node,
     * each faction says the other's id, the colour stayed with the place; after the second every byte is back. */
    static const unsigned orders[6][TFP_NODES]={{0,1,2,3,4,5,6,7,8},{8,7,6,5,4,3,2,1,0},{4,0,1,2,3,5,6,7,8},{1,0,2,3,4,5,6,7,8},{2,0,1,3,4,5,6,7,8},{5,3,7,0,1,2,4,6,8}};
    unsigned pairs=0;
    for(unsigned o=0;o<6;o++) for(unsigned i=0;i<TFP_NODES;i++) for(unsigned j=0;j<TFP_NODES;j++) {
        if(i==j) continue;
        tfp_build(orders[o]);memcpy(before,tfp_arena,sizeof(before));
        unsigned char *a=tfp_node(i),*b=tfp_node(j),*root=fpres_get(anchor+FPRES_N_LEFT);
        assert(tfp_check(root,anchor,0,~0ULL)==TFP_NODES && fpres_find(anchor,fpres_hash(tfp_ids[i]))==a && fpres_listed(anchor,tfp_faction(i)));
        unsigned char place_a=a[0x18],place_b=b[0x18];
        fpres_exchange(anchor,a,b);
        root=fpres_get(anchor+FPRES_N_LEFT);
        assert(tfp_check(root,anchor,0,~0ULL)==TFP_NODES && fpres_get(anchor+FPRES_BEGIN)==tfp_leftmost());
        assert(fpres_find(anchor,fpres_hash(tfp_ids[i]))==b && fpres_find(anchor,fpres_hash(tfp_ids[j]))==a);
        assert(fpres_named(tfp_faction(j),tfp_ids[i]) && fpres_named(tfp_faction(i),tfp_ids[j]) && a[0x18]==place_b && b[0x18]==place_a);
        for(unsigned k=0;k<TFP_NODES;k++) if(k!=i && k!=j) assert(fpres_find(anchor,fpres_hash(tfp_ids[k]))==tfp_node(k));
        fpres_exchange(anchor,a,b);
        assert(!memcmp(before,tfp_arena,sizeof(before)));
        ++pairs;
    }
    assert(pairs==6*TFP_NODES*(TFP_NODES-1));
    /* The scope. Not armed: nothing. Slot 1 and coop (the slot's faction is the player faction): nothing. */
    tfp_build(orders[5]);memcpy(before,tfp_arena,sizeof(before));
    fpres_anchor_test=tfp_anchor;fpres_cell_test=tfp_cell_of;fpres_slot_faction_test=tfp_faction_of;
    tfp_cell=tfp_faction(0);tfp_slot_faction[1]=tfp_faction(0);tfp_slot_faction[2]=tfp_faction(1);tfp_slot_faction[3]=NULL;
    fpres_armed=0;fpres_open=0;fpres_swaps=fpres_refused=fpres_changed=fpres_broken=0;fpres_why="-";
    assert(!fpres_enter(2) && !fpres_open && !fpres_swaps);
    fpres_armed=1;
    assert(!fpres_enter(1) && !fpres_open && !fpres_refused && !memcmp(before,tfp_arena,sizeof(before)));
    /* Slot 2 with its own faction: "player" finds it, it is g_PlayerFaction, the simulation's one is kept for the
     * modules that ask (the faction family, the player settings, the research owner). */
    assert(fpres_enter(2)==1 && fpres_open && tfp_cell==tfp_faction(1) && fpres_canonical==tfp_faction(0) && fpres_swaps==1);
    assert(fpres_find(anchor,fpres_hash("player"))==tfp_node(1) && fpres_named(tfp_faction(1),"player") && fpres_named(tfp_faction(0),"x4lan_player2"));
    assert(!fpres_enter(2) && fpres_swaps==1);   /* open already */
    /* The shared map: inside the scope the slot's own faction knows what the simulation's player faction knows and
     * what it knows itself; another faction's knowledge stays its own, and so does an unknown thing. */
    {   static int set_a,set_b;
        const DWORD thread_was=ui_thread;
        fpres_known_test=tfp_known;fpres_known_shared=0;ui_thread=GetCurrentThreadId();
        tfp_known_set=&set_a;tfp_known_by[0]=tfp_faction(0);tfp_known_by[1]=NULL;
        assert(fpres_known_to(&set_a,tfp_faction(1))==1 && fpres_known_shared==1);          /* the player faction knows it */
        assert(fpres_known_to(&set_a,tfp_faction(0))==1 && fpres_known_shared==1);
        assert(fpres_known_to(&set_a,tfp_faction(2))==0 && fpres_known_shared==1);          /* a third faction does not */
        assert(fpres_known_to(&set_b,tfp_faction(1))==0 && fpres_known_shared==1);          /* nobody knows this one */
        tfp_known_by[0]=tfp_faction(1);
        assert(fpres_known_to(&set_a,tfp_faction(1))==1 && fpres_known_shared==1);          /* its own knowledge: no second question */
        tfp_known_by[0]=tfp_faction(0);ui_thread=thread_was;
    }
    /* A player's own property is unlocked for every kind: the slot's own faction inside the scope (its id is
     * "player" there), and the faction named x4lan_player<k> (player 1's, by the exchanged id, inside the scope).
     * Another faction's is X4's answer. */
    {   unsigned char thing[8];
        fpres_owner_test=tfp_owner_of;fpres_info_test=tfp_info;fpres_info_own=0;tfp_info_asked=0;
        tfp_owner=tfp_faction(1);assert(fpres_info(NULL,3,thing)==1 && fpres_info_own==1 && !tfp_info_asked);
        tfp_owner=tfp_faction(0);assert(fpres_info(NULL,3,thing)==1 && fpres_info_own==2 && !tfp_info_asked);
        tfp_owner=tfp_faction(2);assert(fpres_info(NULL,3,thing)==0 && fpres_info(NULL,7,thing)==1 && fpres_info_own==2 && tfp_info_asked==2);
    }
    fpres_leave();
    /* Outside the scope: the pool faction's property is unlocked the same (on every client), the player faction's
     * and the others' are X4's answer. */
    {   unsigned char thing[8];
        tfp_owner=tfp_faction(1);assert(fpres_info(NULL,3,thing)==1 && fpres_info_own==3 && tfp_info_asked==2);
        tfp_owner=tfp_faction(6);assert(fpres_info(NULL,3,thing)==1 && fpres_info_own==4);            /* x4lan_player3 */
        tfp_owner=tfp_faction(0);assert(fpres_info(NULL,3,thing)==0 && fpres_info(NULL,7,thing)==1 && fpres_info_own==4 && tfp_info_asked==4);
        tfp_owner=NULL;assert(fpres_info(NULL,7,thing)==1 && tfp_info_asked==5);
        assert(!fpres_pool_faction(tfp_faction(2)) && !fpres_pool_faction(tfp_faction(0)) && fpres_pool_faction(tfp_faction(1)));
        fpres_owner_test=NULL;fpres_info_test=NULL;fpres_info_own=0;
    }
    /* Outside the scope the question is X4's alone. */
    assert(fpres_known_to(tfp_known_set,tfp_faction(1))==0 && fpres_known_to(tfp_known_set,tfp_faction(0))==1 && fpres_known_shared==1);
    fpres_known_test=NULL;fpres_known_shared=0;
    assert(!fpres_open && !fpres_canonical && tfp_cell==tfp_faction(0) && !memcmp(before,tfp_arena,sizeof(before)) && !fpres_changed && !fpres_broken);
    fpres_leave();assert(!memcmp(before,tfp_arena,sizeof(before)));
    /* g_PlayerFaction changed inside the scope: counted, everything back all the same. */
    assert(fpres_enter(2)==1);tfp_cell=tfp_faction(2);fpres_leave();
    assert(fpres_changed==1 && tfp_cell==tfp_faction(0) && !memcmp(before,tfp_arena,sizeof(before)));
    /* The simulation's update of a container: its owner as the player faction when that is a slot's own faction
     * (x4lan_player<k> and some slot's), and back; nothing for the player faction's own, another faction's, a pool
     * faction no slot has, inside the presentation scope or a scope already open. */
    {   unsigned char box[8];
        fpres_owner_test=tfp_owner_of;ui_thread=GetCurrentThreadId();fpres_sim_scopes=0;
        tfp_owner=tfp_faction(1);
        assert(fpres_sim_enter(box)==1 && fpres_sim_slot==2 && fpres_sim_saved==tfp_faction(0) && tfp_cell==tfp_faction(1) && fpres_sim_scopes==1);
        assert(uifac_player_faction()==tfp_faction(0));          /* the modules that ask get the simulation's own */
        assert(!fpres_sim_enter(box) && !fpres_enter(2) && fpres_sim_scopes==1);
        fpres_sim_leave();assert(!fpres_sim_slot && !fpres_sim_saved && tfp_cell==tfp_faction(0));
        fpres_sim_leave();assert(tfp_cell==tfp_faction(0));
        tfp_owner=tfp_faction(0);assert(!fpres_sim_enter(box));
        tfp_owner=tfp_faction(2);assert(!fpres_sim_enter(box));
        tfp_owner=tfp_faction(6);assert(!fpres_sim_enter(box));  /* x4lan_player3: no slot's */
        tfp_owner=tfp_faction(1);assert(!fpres_sim_enter(NULL));
        assert(fpres_enter(2)==1 && !fpres_sim_enter(box));fpres_leave();
        assert(tfp_cell==tfp_faction(0) && fpres_sim_scopes==1 && !memcmp(before,tfp_arena,sizeof(before)));
        fpres_owner_test=NULL;fpres_sim_scopes=0;
    }
    /* The character lookup: a list of one player's own faction is asked as the simulation's player faction (outside
     * the scope by its id x4lan_player<k>; inside, the slot's own named "player" and player 1's named x4lan_player2
     * swap places in the answer); the player faction's own, another faction's, an empty list or two factions are
     * asked as they are. The other arguments go through unchanged. */
    {   unsigned char list[0x20];void *heap[4];int marker;
        fpres_cast_test=tfp_cast;fpres_cast_player=0;tfp_cast_calls=0;
        assert(fpres_cast(NULL,NULL,NULL,tfp_list(list,tfp_faction(1),1,heap),1,&marker)==tfp_faction(0) && fpres_cast_player==1 && tfp_cast_flag==1 && tfp_cast_a6==&marker);
        assert(fpres_cast(NULL,NULL,NULL,tfp_list(list,tfp_faction(6),0,heap),0,NULL)==tfp_faction(0) && fpres_cast_player==2 && !tfp_cast_flag);
        assert(fpres_cast(NULL,NULL,NULL,tfp_list(list,tfp_faction(0),1,heap),1,NULL)==tfp_faction(0) && fpres_cast_player==2);
        assert(fpres_cast(NULL,NULL,NULL,tfp_list(list,tfp_faction(2),1,heap),1,NULL)==tfp_faction(2) && fpres_cast_player==2);
        assert(fpres_cast(NULL,NULL,NULL,tfp_list(list,NULL,1,heap),1,NULL)==NULL && fpres_cast_player==2);
        {uint64_t two=2;tfp_list(list,tfp_faction(1),1,heap);memcpy(list+8,&heap,8);memcpy(list+0x18,&two,8);
         assert(fpres_cast(NULL,NULL,NULL,list,1,NULL)==tfp_faction(1) && fpres_cast_player==2);}          /* two factions: as asked */
        tfp_cast_calls=0;
        assert(fpres_enter(2)==1);
        assert(fpres_cast(NULL,NULL,NULL,tfp_list(list,tfp_faction(1),1,heap),1,NULL)==tfp_faction(0) && fpres_cast_player==3);   /* the slot's own, named "player" */
        assert(fpres_cast(NULL,NULL,NULL,tfp_list(list,tfp_faction(0),1,heap),1,NULL)==tfp_faction(0) && fpres_cast_player==3);   /* player 1's, named x4lan_player2 */
        fpres_leave();
        fpres_armed=0;assert(fpres_cast(NULL,NULL,NULL,tfp_list(list,tfp_faction(1),1,heap),1,NULL)==tfp_faction(1));fpres_armed=1;
        assert(!memcmp(before,tfp_arena,sizeof(before)));
        fpres_cast_test=NULL;fpres_cast_player=0;
    }
    /* Refused, nothing touched: a slot without a faction; a player faction that is not named "player"; a faction
     * whose node the tree does not find under its key. */
    assert(!fpres_enter(3) && fpres_refused==1 && !strcmp(fpres_why,"no_faction"));
    tfp_cell=tfp_faction(2);assert(!fpres_enter(2) && fpres_refused==2 && !strcmp(fpres_why,"player_name"));tfp_cell=tfp_faction(0);
    tfp_faction(1)[FPRES_F_HASH]^=1;assert(!fpres_enter(2) && fpres_refused==3 && !strcmp(fpres_why,"not_listed"));tfp_faction(1)[FPRES_F_HASH]^=1;
    assert(!fpres_open && !memcmp(before,tfp_arena,sizeof(before)));
    {char status[240];assert(fpres_status(status,sizeof(status))>0 && !strcmp(status,"armed=1 open=0 slot=2 swaps=4 refused=3 changed=1 broken=0 known=0 info=0 sim=0 cast=0 why=not_listed"));}
    fpres_anchor_test=NULL;fpres_cell_test=NULL;fpres_slot_faction_test=NULL;
    fpres_armed=0;fpres_swaps=fpres_refused=fpres_changed=fpres_broken=0;fpres_why="-";fpres_slot_last=0;
    printf("faction present: two factions change places in the tree and back, in every relative position; scope and refusals; tests PASS\n");
}
/* The pinned image: the lookup's bytes and the table it names; one changed byte refuses. */
static void test_faction_present_native_image(const char *path) {
    unsigned char *prior=image,*prior_base=pnat_base;
    image=test_pmath_map(path);pnat_base=image;
    assert(fpres_pins());
    for(unsigned i=0;i<FPRES_PINS;i++) {
        image[fpres_pin_table[i].rva]^=1;assert(!fpres_pins());image[fpres_pin_table[i].rva]^=1;
    }
    image[0x19897au]^=1;assert(!fpres_pins());image[0x19897au]^=1;   /* the table's address */
    assert(fpres_pins());
    /* g_PlayerFaction is the global the owner test of 0xafe053 reads. */
    {int32_t rel;memcpy(&rel,image+0xafe056u,4);assert(image[0xafe053u]==0x48 && 0xafe05au+(uint32_t)rel==UITRADE_PLAYER_FACTION);}
    /* "Known to": the pinned function (the flag for all at +0x72, the owner at +0x60, the list from +0x78), its first
     * two instructions (a compare and a short jump to the "yes" exit, which the trampoline makes as a near jump), the
     * five patched bytes inside one 16-byte block; a space's name asks it directly (0x7edcb4); stub and trampoline in
     * their places. */
    assert(hash_ok(image+FPRES_KNOWN,FPRES_KNOWN_SIZE,FPRES_KNOWN_SHA) && image[FPRES_KNOWN+FPRES_KNOWN_SIZE-1]==0xc3);
    assert(!memcmp(image+FPRES_KNOWN,fpres_known_head,sizeof(fpres_known_head)) && ((FPRES_KNOWN+4)&~15u)==(FPRES_KNOWN&~15u));
    assert(FPRES_KNOWN+6u+fpres_known_head[5]==FPRES_KNOWN_YES && !memcmp(image+FPRES_KNOWN_YES,"\xb0\x01\xc3",3));
    {int32_t rel;memcpy(&rel,image+0x7edcb5u,4);assert(image[0x7edcb4u]==0xe8 && 0x7edcb9u+(uint32_t)rel==FPRES_KNOWN);}
    for(unsigned i=0;i<3;i++) {
        static const uint32_t tails[3]={0x554f07u,0x6d3d57u,0x7ed7c7u};int32_t rel;memcpy(&rel,image+tails[i]+1,4);
        assert(image[tails[i]]==0xe9 && tails[i]+5u+(uint32_t)rel==FPRES_KNOWN);
    }
    assert(FPRES_KNOWN_STUB>=ANIML_TRAMPOLINE+sizeof(animl_head)+14 && FPRES_KNOWN_STUB+14<=FPRES_INFO_STUB);
    assert(FPRES_INFO_TRAMPOLINE+sizeof(fpres_info_head)+14<=FPRES_KNOWN_TRAMPOLINE && FPRES_KNOWN_TRAMPOLINE+24<=FPRES_CAST_STUB);
    /* The character lookup: the pinned function, its first instruction (five bytes, position-independent), the patch in
     * one 16-byte block; the control entity's creation calls it twice (tags of the post, then the entity type). */
    assert(hash_ok(image+FPRES_CAST,FPRES_CAST_SIZE,FPRES_CAST_SHA) && !memcmp(image+FPRES_CAST,fpres_cast_head,sizeof(fpres_cast_head)));
    assert(((FPRES_CAST+4)&~15u)==(FPRES_CAST&~15u) && image[FPRES_CAST+FPRES_CAST_SIZE-1]==0xc3);
    assert(FPRES_CAST_STUB+14<=FPRES_CAST_TRAMPOLINE && FPRES_CAST_TRAMPOLINE+sizeof(fpres_cast_head)+14<=PSLOT_CTL_END_STUB);
    {   static const uint32_t sites[2]={0x44e98au,0x44eb5eu};
        for(unsigned i=0;i<2;i++) {int32_t rel;memcpy(&rel,image+sites[i]+1,4);assert(image[sites[i]]==0xe8 && sites[i]+5u+(uint32_t)rel==FPRES_CAST);}
    }
    /* The info test: the pinned function, its two first instructions (six bytes, nothing in them depends on where
     * they run), the five patched bytes inside one 16-byte block; stub and trampoline in their places. */
    assert(hash_ok(image+FPRES_INFO,FPRES_INFO_SIZE,FPRES_INFO_SHA) && !memcmp(image+FPRES_INFO,fpres_info_head,sizeof(fpres_info_head)));
    assert(image[FPRES_INFO+FPRES_INFO_SIZE-1]==0xc3 && ((FPRES_INFO+4)&~15u)==(FPRES_INFO&~15u));
    assert(FPRES_INFO_STUB+14<=FPRES_INFO_TRAMPOLINE && FPRES_INFO_TRAMPOLINE+sizeof(fpres_info_head)+14<=PSLOT_CTL_END_STUB);
    {int32_t rel;memcpy(&rel,image+0x1b2ff4u,4);assert(image[0x1b2ff3u]==0xe8 && 0x1b2ff8u+(uint32_t)rel==FPRES_INFO);}   /* IsInfoUnlockedForPlayer calls it */
    VirtualFree(image,0,MEM_RELEASE);image=prior;pnat_base=prior_base;
    printf("faction present: the pinned lookup of this build; tests PASS\n");
}
