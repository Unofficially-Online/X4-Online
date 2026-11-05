/* lockstep_join_specdrop.h: the joiner clears the flags of its kind 7 entries only on a posed component whose session record holds
 * no kind 7 entry; other kinds, the list's count and shape, unposed components, the session's own kind 7 and an unarmed joiner are
 * left alone. */
#define TSD_OBJECTS 4u
static unsigned char *tsd_object[TSD_OBJECTS];
static uint64_t tsd_runtime[TSD_OBJECTS];
static void *tsd_find(void *map,uint64_t runtime,unsigned mode) {
    assert(map==(void *)tsd_object && mode==4);
    for(unsigned i=0;i<TSD_OBJECTS;i++) if(tsd_runtime[i]==runtime) return tsd_object[i];
    return NULL;
}
static void tsd_list(unsigned char *object,unsigned char *list,const join_spec_entry *e,uint64_t count) {
    const uint64_t cap=4;memset(list,0,0x90);memcpy(list,e,(size_t)count*sizeof(*e));
    memcpy(list+0x80,&cap,8);memcpy(list+0x88,&count,8);memcpy(object+JOIN_SPEC_LIST,&list,8);
}
static void test_join_specdrop(void) {
    static __declspec(align(16)) unsigned char objects[TSD_OBJECTS][0x200],lists[TSD_OBJECTS][0x90],pose[16];
    const join_find_fn saved_find=join_world_find;void *const saved_map=join_world_map;const int saved_on=join_specdrop_on;
    join_id_rec ids[TSD_OBJECTS];
    for(unsigned i=0;i<TSD_OBJECTS;i++) {
        memset(objects[i],0,sizeof(objects[i]));tsd_object[i]=objects[i];tsd_runtime[i]=11u+i;
        memcpy(objects[i]+8,&tsd_runtime[i],8);
        unsigned char *p=pose;memcpy(objects[i]+JOIN_PEND_VIEW_POSE,&p,8);memcpy(objects[i]+JOIN_PEND_VIEW_POSE+0x80u,&p,8);
        ids[i].runtime=tsd_runtime[i];ids[i].portable=0x101u+i;ids[i].klass=0;ids[i].pad=0;
    }
    /* 0: the load's kind 7 and a kind 2 of its own; 1: kind 7 that the session holds too; 2: kind 7 with an unpublished view;
     * 3: no list. */
    const join_spec_entry load7={7,0x19,0x121,0,0,0},own2={2,0x4f,0x13a,1,0,0};
    const join_spec_entry e0[2]={load7,own2};
    tsd_list(objects[0],lists[0],e0,2);tsd_list(objects[1],lists[1],&load7,1);tsd_list(objects[2],lists[2],&load7,1);
    memset(objects[2]+JOIN_PEND_VIEW_POSE+0x80u,0,8);
    join_spec_rec session[2]={{0x101,2,0x4f,0x13a,1,0,0},{0x102,7,0x19,0x121,0,0,0}};
    join_tables s,mine;memset(&s,0,sizeof(s));memset(&mine,0,sizeof(mine));
    s.version=13;s.spec=session;s.nspec=2;mine.ids=ids;mine.nids=TSD_OBJECTS;
    join_world_find=tsd_find;join_world_map=tsd_object;
    /* Off: nothing written, nothing counted. */
    join_specdrop_on=0;join_admit_specdrop(&s,&mine);
    assert(!join_specdrop_lists && !join_specdrop_cleared && ((join_spec_entry *)lists[0])[0].flags==0x19);
    join_specdrop_on=1;join_admit_specdrop(&s,&mine);
    const join_spec_entry *l0=(const join_spec_entry *)lists[0];uint64_t count=0;
    assert(l0[0].kind==7 && l0[0].flags==0 && l0[0].klass==0x121 && l0[1].kind==2 && l0[1].flags==0x4f && l0[1].klass==0x13a);
    memcpy(&count,lists[0]+0x88,8);assert(count==2);
    assert(((const join_spec_entry *)lists[1])[0].flags==0x19 && ((const join_spec_entry *)lists[2])[0].flags==0x19);
    assert(join_specdrop_lists==3 && join_specdrop_cleared==1 && join_specdrop_session==1 && join_specdrop_unposed==1 && !join_specdrop_unread);
    char out[96];const int n=join_specdrop_status(out,sizeof(out));
    assert(n>0 && !strcmp(out," specdrop=1:3:1:1:1:0"));
    /* Again: the cleared entry is no longer due. */
    join_admit_specdrop(&s,&mine);assert(join_specdrop_lists==2 && !join_specdrop_cleared);
    /* An older package: nothing. */
    s.version=12;join_admit_specdrop(&s,&mine);assert(!join_specdrop_lists);
    join_world_find=saved_find;join_world_map=saved_map;join_specdrop_on=saved_on;
}
