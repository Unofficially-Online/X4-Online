/* native_gate_join_ids (lockstep_join_ids.h) on the stand-in maps of test_lockstep_runtime_ids.h: a session that loaded and
 * played into block 1, a joiner with another pre-load history that loads the session's saved objects in another order,
 * mixed with allocations of its own; after the skip both hand out the same IDs. */
typedef struct {char text[16];uint64_t length,capacity;} test_jids_string;   /* std::string, short form */
static test_jids_string *test_jids_value;
static const void *test_jids_attribute(void *node,uint64_t key,const void *fallback) {
    assert(key==JOIN_IDS_KEY && fallback==image+JOIN_IDS_EMPTY);return node?node:fallback;
}
static test_jids_string test_jids_id(uint64_t id) {
    test_jids_string s;memset(&s,0,sizeof(s));s.length=(uint64_t)snprintf(s.text,sizeof(s.text),"[0x%llx]",(unsigned long long)id);s.capacity=15;return s;
}
static unsigned char *test_jids_map;static unsigned test_jids_blocks;
static unsigned char test_jids_ensure(void *map) {test_rid_append_block(map,test_jids_blocks++);return 1;}
/* InsertAtSlot as read from 0xcc500: the slot off the list wherever it is, used, the counters. */
static uint64_t test_jids_insert(void *mapv,void *object,uint32_t slot,void *ref) {
    unsigned char *map=mapv;struct {unsigned char *bucket;uint32_t entry,pad;} r;memcpy(&r,ref,sizeof(r));
    unsigned entry;assert(rid_bucket(map,slot,&entry)==r.bucket && entry==r.entry);
    uint64_t *e=test_rid_entry(map,slot);const uint32_t prev=(uint32_t)*e,next=(uint32_t)(*e>>32);
    if(!prev) *(uint32_t*)(map+0x50)=next;
    else {uint64_t *q=test_rid_entry(map,prev);*q=(*q&0xffffffffULL)|((uint64_t)next<<32);}
    if(!next) *(uint32_t*)(map+0x54)=prev;
    else {uint64_t *q=test_rid_entry(map,next);*q=(*q&0xffffffff00000000ULL)|prev;}
    *e=(uint64_t)(uintptr_t)object;*(uint32_t*)(r.bucket+0x1c)|=1u<<(entry*4u+16u);
    *(uint32_t*)(map+0x40)+=1u;*(uint32_t*)(map+0x44)-=1u;*(uint64_t*)(map+0x38)+=1u;
    return ((uint64_t)rid_slot_generation(map,slot)<<25)|slot;
}
/* The native AssignNewID's contract: the ID at object+8, nothing meaningful in rax. */
static uint64_t test_jids_assign_void(void *object) {(void)test_rid_assign_stub(object);return 0xdeadbeefull;}
#define TEST_JIDS_SAVED 0x30000u
static void test_join_ids(void) {
    void *saved_map=*(void**)(image+RID_MAP);uint32_t saved_active=stats.active;DWORD saved_ui=ui_thread;int saved_created=created_installed;
    test_rid_process s=test_rid_new(),j=test_rid_new();uint64_t old[8];
    test_rid_preload(&s,old,8,300);test_rid_preload(&j,old,8,1700);
    assert(rid_apply(s.map,0x10000) && rid_apply(j.map,0x10000));
    rid_base=0x10000;rid_mode=RID_SET;rid_failures=0;rid_dropped=0;rid_sealed=0;
    rid_remove_native=test_rid_remove_native;rid_assign_native=test_rid_assign_stub;InterlockedExchange(&rid_active,1);
    stats.active=1;ui_thread=GetCurrentThreadId();
    /* The session: its load and play run into block 1; every fourth object is saved in the join save. */
    static uint64_t saved[TEST_JIDS_SAVED];unsigned nsaved=0;
    for(uint32_t i=0;i<0xf4000u;i++) {const uint64_t id=test_rid_insert(&s);if(!(i%4) && nsaved<TEST_JIDS_SAVED) saved[nsaved++]=id;}
    assert(s.blocks==2u && nsaved==TEST_JIDS_SAVED);
    const rid_counts_t session=rid_counts(s.map);const uint64_t next=rid_next(s.map);
    assert(((session.head-1u)>>20)==1u && !(next>>25));
    /* The refusals of the arming: off is armed, then not set mode, a next slot past the limit, no hooks, then the image's pins. */
    {
        *(void**)(image+RID_MAP)=j.map;join_ids_on=0;assert(join_ids_arm(next));join_ids_on=1;
        rid_mode=RID_OBSERVE;assert(!join_ids_arm(next) && !strcmp(join_ids_stage,"rid"));rid_mode=RID_SET;
        assert(!join_ids_arm(0x1c00001u) && !strcmp(join_ids_stage,"next"));assert(!join_ids_arm(0x2000000u|0x20000u) && !strcmp(join_ids_stage,"next"));
        assert(!join_ids_arm(0x10000u) && !strcmp(join_ids_stage,"next"));
        created_installed=0;assert(!join_ids_arm(next) && !strcmp(join_ids_stage,"hooks"));created_installed=1;
        const int world=world_identity_installed;world_identity_installed=1;
        assert(!join_ids_arm(next) && (!strcmp(join_ids_stage,"thunk") || !strcmp(join_ids_stage,"pins")));   /* the plain image */
        world_identity_installed=world;join_ids_on=0;
        assert(!InterlockedCompareExchange(&join_ids_active,0,0) && !join_ids_built);
    }
    /* The joiner's map before its load: block 1 appended as on the session, the load list in block 29. */
    join_ids_ensure_native=test_jids_ensure;join_ids_insert_native=test_jids_insert;join_ids_attribute_native=test_jids_attribute;
    test_jids_blocks=1;test_jids_map=j.map;
    {
        void *taken=(void*)1;memcpy(j.map+0x58+RID_JOIN_BLOCK*8u,&taken,8);assert(!join_ids_prepare(j.map,(uint32_t)next,NULL) && !strcmp(join_ids_stage,"block"));
        taken=NULL;memcpy(j.map+0x58+RID_JOIN_BLOCK*8u,&taken,8);
        void *block=test_rid_allocate(RID_BLOCK_BYTES);assert(block && join_ids_prepare(j.map,(uint32_t)next,block));
        const rid_counts_t c=rid_counts(j.map);
        assert(c.tail==session.tail && join_ids_appended==1 && test_jids_blocks==2u && join_ids_built && rid_walk(j.map));
        assert(join_ids_list.head==0x1d00001u && join_ids_list.tail==0x1e00000u && join_ids_list.free==0x100000u);
        assert(rid_local_of(0x1d00001u)==&join_ids_list && rid_local_of(0x1e00000u)==&join_ids_list && !rid_local_of(0x1e00001u) && !rid_local_of(0x1d00000u));
        j.blocks=test_jids_blocks;
    }
    join_ids_want=(uint32_t)next;InterlockedExchange(&join_ids_active,1);
    test_rid_current=&j;
    /* The joiner's load, in reverse save order, with allocations of its own between the saved objects. */
    {
        static unsigned char objects[TEST_JIDS_SAVED][16];unsigned char own[16],conn[16];
        uint64_t first_own=0;
        for(unsigned k=0;k<nsaved;k++) {
            const unsigned i=nsaved-1u-k;test_jids_string text=test_jids_id(saved[i]);
            const int connection=(i%7)==3;
            join_ids_note(&text,connection?JOIN_IDS_CONN:JOIN_IDS_COMPONENT);
            /* What comes between the node and its constructor takes the load list, the pending ID stays. */
            const uint64_t a=rid_assign(own,image+(connection?JOIN_IDS_COMPONENT_A:JOIN_IDS_CONNECTION));
            const uint64_t b=rid_assign(conn,image+0x997168u);   /* the connection container's constructor */
            assert(((uint32_t)a-1u)>>20==RID_JOIN_BLOCK && ((uint32_t)b-1u)>>20==RID_JOIN_BLOCK);
            if(!first_own) first_own=a;
            const uint64_t id=rid_assign(objects[i],image+(connection?JOIN_IDS_CONNECTION:(i&1u)?JOIN_IDS_COMPONENT_A:JOIN_IDS_COMPONENT_B));
            uint64_t at;memcpy(&at,objects[i]+8,8);assert(id==saved[i] && at==saved[i]);
            join_ids_imported(objects[i],saved[i]);
        }
        assert(join_ids_placed==(LONG64)nsaved && !join_ids_refused && join_ids_equal==(LONG64)nsaved && !join_ids_other && !join_ids_noid);
        assert(join_ids_nodes+join_ids_conns==(LONG64)nsaved && join_ids_conns>0 && join_ids_list.allocs==2u*nsaved && first_own==0x1d00001u);
        /* A pending ID nothing took is dropped by the import; an ID taken twice, one past the session's next slot or of another
         * generation: the load list, refused; a node without an ID: nothing pending. */
        test_jids_string text=test_jids_id(saved[5]);join_ids_note(&text,JOIN_IDS_COMPONENT);join_ids_imported(own,saved[5]);
        assert(join_ids_pending.kind==JOIN_IDS_NONE && join_ids_other==1);
        const uint64_t refused[3]={saved[5],next,((uint64_t)1<<25)|(uint32_t)(saved[0]+1u)};
        for(unsigned r=0;r<3;r++) {
            text=test_jids_id(refused[r]);join_ids_note(&text,JOIN_IDS_COMPONENT);
            assert((((uint32_t)rid_assign(own,image+JOIN_IDS_COMPONENT_A)-1u)>>20)==RID_JOIN_BLOCK && join_ids_refused==(LONG64)r+1);
        }
        join_ids_note(NULL,JOIN_IDS_COMPONENT);assert(join_ids_noid==1 && join_ids_pending.kind==JOIN_IDS_NONE);
        assert(rid_local_of((uint32_t)saved[0])==NULL);
        /* A frees on the load list go back on it; a shared slot comes off the tail as before. */
        const uint32_t tail=join_ids_list.tail;assert(rid_remove(j.map,first_own)&1u);
        assert(join_ids_list.tail==(uint32_t)first_own && join_ids_list.frees==1u && (*test_rid_entry(j.map,tail)>>32)==(uint32_t)first_own);
        const rid_counts_t c=rid_counts(j.map);assert(c.head==0x10001u && rid_walk(j.map));   /* 0x10000 is the session's first saved object */
    }
    /* The admission: the head over the run to the session's next slot; then both hand out the same IDs, into block 2 alike. */
    {
        const char *why="";InterlockedExchange(&join_ids_active,0);
        assert(!join_ids_skip(j.map,(uint32_t)next+0x200000u,&why) && !strcmp(why,"ids_target"));
        assert(join_ids_skip(j.map,(uint32_t)next,&why));
        const rid_counts_t c=rid_counts(j.map);
        assert(c.head==session.head && c.tail==session.tail && c.free==session.free && rid_next(j.map)==next && rid_walk(j.map));
        assert(join_ids_skipped==(LONG64)(session.head-0x10000u)-(LONG64)nsaved);
        for(unsigned i=0;i<0xe8000u;i++) assert(test_rid_insert(&s)==test_rid_insert(&j));
        assert(s.blocks==3u && j.blocks==3u);
        /* Replay (W3): holes below the head (unsaved slots of the session) for this thread's next shared allocations, in the
         * order given, the shared list untouched; one allocation too many, or an ID it cannot take, refuses. */
        unsigned char o[3][16];const uint64_t holes[3]={0x10003u,0x10001u,0x10006u};
        const rid_counts_t before=rid_counts(j.map);
        /* As the native AssignNewID: the ID stored at +8 only, rax garbage (w1u: wrong=2 from comparing the return value). */
        rid_assign_native=test_jids_assign_void;
        assert(join_ids_replay(holes,3) && !join_ids_replay(holes,3));
        for(unsigned i=0;i<3;i++) {uint64_t at;(void)rid_assign(o[i],image+0x997000u);memcpy(&at,o[i]+8,8);assert(at==holes[i] && join_ids_replay_got[i]==holes[i]);}
        const rid_counts_t mid=rid_counts(j.map);
        assert(mid.head==before.head && mid.tail==before.tail && mid.free==before.free && rid_slot_used(j.map,0x10001u) && rid_walk(j.map));
        assert(!join_ids_replay_wrong && join_ids_replay_end() && !join_ids_replay_end());
        rid_assign_native=test_rid_assign_stub;
        const uint64_t one[1]={0x10002u};assert(join_ids_replay(one,1) && rid_assign(o[0],image+0x997000u)==one[0]);
        assert(rid_assign(o[1],image+0x997000u)==before.head && !join_ids_replay_end());   /* past the one: the shared head */
        const uint64_t above[1]={rid_counts(j.map).head},used[1]={0x10003u},generation[1]={((uint64_t)1<<25)|0x10005u},twice[2]={0x10005u,0x10005u};
        assert(!join_ids_replay(above,1) && !join_ids_replay(used,1) && !join_ids_replay(generation,1) && !join_ids_replay(twice,2) && !join_ids_replay(one,0));
        assert(join_ids_replays==2 && join_ids_replay_failures==5);   /* the end past the one, the four refused lists */
        /* Off, nothing routes: the original's list. */
        assert(join_ids_route(rid_assign_original(),JOIN_IDS_COMPONENT_A)==rid_assign_original());
    }
    char text[160];assert(join_ids_status(text,sizeof(text))>0);
    {
        char want[96];snprintf(want,sizeof(want),",%u/%u/1,%u/3,%u/1,",(unsigned)join_ids_nodes,(unsigned)join_ids_conns,nsaved,nsaved);
        assert(strstr(text,want) && strstr(text,",1,"));
    }
    join_ids_ensure_native=NULL;join_ids_insert_native=NULL;join_ids_attribute_native=NULL;join_ids_built=0;join_ids_want=0;join_ids_stage="idle";
    memset(&join_ids_list,0,sizeof(join_ids_list));join_ids_nodes=join_ids_conns=join_ids_noid=join_ids_placed=join_ids_refused=join_ids_equal=join_ids_other=0;
    join_ids_appended=join_ids_skipped=0;join_ids_on=0;join_ids_replays=join_ids_replay_failures=0;
    memset(rid_callers,0,sizeof(rid_callers));rid_caller_count=0;rid_caller_overflow=0;
    rid_remove_native=NULL;rid_assign_native=NULL;InterlockedExchange(&rid_active,0);
    rid_mode=RID_OFF;rid_base=0;rid_sealed=0;rid_failures=0;rid_dropped=0;rid_stage="idle";rid_head_before=rid_cut=rid_skipped=0;
    test_rid_free_process(&s);test_rid_free_process(&j);
    *(void**)(image+RID_MAP)=saved_map;stats.active=saved_active;ui_thread=saved_ui;created_installed=saved_created;
    printf("hot join saved objects at the session's runtime IDs: placement, load list, refusals, skip, same IDs after PASS\n");
}
