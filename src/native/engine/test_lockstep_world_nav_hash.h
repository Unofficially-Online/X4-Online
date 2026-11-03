/* native_gate_world_nav_stride: the nav-only hash over the indexed navigation parts, on the hot join tests' world
 * (test_lockstep_join.h tw_world: a ship with a NavigationController, its agent and both banks). */
static uint64_t tnh_resolve(uint64_t runtime) {unsigned klass;return runtime?tw_key(runtime,&klass):0;}
static void tnh_hash(unsigned char out[32]) {assert(world_nav_hash_core(tw_find,&tw_session,tnh_resolve,out));}
static void test_world_nav_hash(void) {
    world_hash_item *saved_items=world_hash_items;unsigned saved_count=world_hash_count,saved_created=world_hash_created_count;
    unsigned saved_schema=world_hash_schema;uint64_t saved_scans=world_hash_scans;
    if(!world_sha_algorithm) assert(BCryptOpenAlgorithmProvider(&world_sha_algorithm,BCRYPT_SHA256_ALGORITHM,NULL,0)>=0);
    tw_build(&tw_session,0);tw_find_world=&tw_session;tw_phase(0);
    /* The capture marks the items whose navigation part it read (nav_raw_length); the index keeps exactly those. */
    world_hash_item items[3]={{.runtime=tw_session.runtime[TW_SHIP],.saved=0x2001,.nav_raw_length=WORLD_NAV_RAW_BYTES},
                              {.runtime=tw_session.runtime[TW_CARGO],.saved=0x2003},
                              {.runtime=tw_session.runtime[TW_TURRET],.saved=0x2004,.nav_raw_length=1}};
    world_hash_items=items;world_hash_count=3;world_hash_created_count=0;
    assert(world_nav_index_rebuild(3) && world_nav_index_count==1 && world_nav_index[0]==0 && !strcmp(world_hash_stage,"complete"));
    /* The same state hashes the same; one navigation bank lane more changes it; restored, it is back. */
    unsigned char a[32],b[32],c[32];
    tnh_hash(a);tnh_hash(b);assert(!memcmp(a,b,32) && !world_nav_gone && !world_nav_moved && !strcmp(world_nav_stage,"complete"));
    float lane;memcpy(&lane,tw_session.agent+0xad0+4,4);float other=lane+0.5f;memcpy(tw_session.agent+0xad0+4,&other,4);
    tnh_hash(c);assert(memcmp(a,c,32));
    memcpy(tw_session.agent+0xad0+4,&lane,4);tnh_hash(c);assert(!memcmp(a,c,32));
    /* A changed reference in the pending entry (to the cargo bay's portable key) changes it too. */
    uint64_t pending;memcpy(&pending,tw_session.agent+0xed0,8);memcpy(tw_session.agent+0xed0,&tw_session.runtime[TW_TURRET],8);
    tnh_hash(c);assert(memcmp(a,c,32));memcpy(tw_session.agent+0xed0,&pending,8);
    /* Another controller: counted moved, hashed as marker 1. Gone: counted, marker 0. */
    void *nav;memcpy(&nav,tw_session.object[TW_SHIP]+0x200,8);
    void *dock=tw_session.controller[0];memcpy(tw_session.object[TW_SHIP]+0x200,&dock,8);
    tnh_hash(c);assert(world_nav_moved==1 && memcmp(a,c,32));
    memcpy(tw_session.object[TW_SHIP]+0x200,&nav,8);
    uint64_t runtime=tw_session.runtime[TW_SHIP];tw_session.runtime[TW_SHIP]=0x7777;
    tnh_hash(c);assert(world_nav_gone==1 && !world_nav_moved && memcmp(a,c,32));
    tw_session.runtime[TW_SHIP]=runtime;tnh_hash(c);assert(!memcmp(a,c,32) && !world_nav_gone);
    /* A bank phase that is not the one the navigation part names fails closed. */
    tw_phase(1);assert(!world_nav_hash_core(tw_find,&tw_session,tnh_resolve,c) && !strcmp(world_nav_stage,"nav_phase_changed"));tw_phase(0);
    /* The outer hash needs a completed schema-2 capture before its tick. */
    world_hash_schema=1;world_hash_scans=1;
    char out[128];assert(!world_nav_hash(5,out,sizeof(out)) && !strcmp(world_nav_stage,"no_capture"));
    char status[256];assert(world_nav_status(status,sizeof(status))>0 && strstr(status,"index=1 gone=0 moved=0 ") && strstr(status," stage=no_capture"));
    world_hash_items=saved_items;world_hash_count=saved_count;world_hash_created_count=saved_created;
    world_hash_schema=saved_schema;world_hash_scans=saved_scans;world_nav_index_count=0;world_nav_stage="idle";
    printf("world nav hash index, determinism, changes, moved/gone markers, phase and capture refusals tests PASS\n");
}
