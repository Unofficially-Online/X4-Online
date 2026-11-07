/* native_gate_motion_watch: layout reads, partial layouts, references, path, row format, ring,
 * order and target rows. */
static __declspec(align(16)) unsigned char test_mw_object[0x400],test_mw_ctrl[0x200],test_mw_agent[0x1000],test_mw_model[0x400],test_mw_slots[0x400];
static __declspec(align(16)) unsigned char test_mw_turret[0x100],test_mw_module[0x100],test_mw_station[0x100],test_mw_path[0x200];
static uint64_t test_mw_ids[3];static unsigned char *test_mw_objects[3];
static void *test_mw_lookup(uint64_t runtime) {
    for(unsigned i=0;i<3;i++) if(test_mw_ids[i]==runtime) return test_mw_objects[i];
    return NULL;
}
/* turret 0x5ccca -> module 0x5ccaf -> station 0x3edc2 (saved 0x123456). */
static void test_mw_hierarchy(void) {
    memset(test_mw_turret,0,sizeof(test_mw_turret));memset(test_mw_module,0,sizeof(test_mw_module));memset(test_mw_station,0,sizeof(test_mw_station));
    unsigned char *objects[3]={test_mw_turret,test_mw_module,test_mw_station};uint64_t ids[3]={0x5ccca,0x5ccaf,0x3edc2};
    uint32_t types[3]={0x2b6e910u,0x2b518c8u,0x2b00000u};
    for(unsigned i=0;i<3;i++) {
        void *vt=image+types[i];memcpy(objects[i],&vt,8);memcpy(objects[i]+8,&ids[i],8);
        if(i<2) {void *parent=objects[i+1];memcpy(objects[i]+0x70,&parent,8);}
        test_mw_ids[i]=ids[i];test_mw_objects[i]=objects[i];
    }
}
static void test_motion_watch(void) {
    motion_watch_lookup_fn prior_lookup=motion_watch_lookup;motion_watch_lookup=test_mw_lookup;
    world_identity_entry *prior=world_identities;unsigned prior_count=world_identity_count;LONG prior_sealed=world_identity_sealed;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);world_identity_count=0;world_identity_sealed=0;
    assert(world_identity_record(0x5b065,0xa214c) && world_identity_record(0x3edc2,0x123456));
    test_mw_hierarchy();
    /* References: saved directly, through ancestors, unresolved, none. */
    motion_ref_t ref=motion_watch_ref(0x5ccca);
    assert(ref.key==0x123456 && ref.klass==WORLD_KEY_SAVED && ref.depth==2 && ref.type==0x2b6e910u);
    ref=motion_watch_ref(0x3edc2);assert(ref.key==0x123456 && !ref.depth && ref.type==0x2b00000u);
    ref=motion_watch_ref(0x9999);assert(!ref.key && ref.klass==WORLD_KEY_NONE && ref.depth==MOTION_REF_UNRESOLVED && ref.type==0xffffffffu);
    ref=motion_watch_ref(0);assert(!ref.key && ref.klass==WORLD_KEY_NONE && !ref.depth && !ref.type);
    memset(test_mw_module+0x70,0,8);ref=motion_watch_ref(0x5ccca);assert(!ref.key && ref.depth==MOTION_REF_UNRESOLVED && ref.type==0x2b6e910u);
    test_mw_hierarchy();
    char text[48];motion_ref_text(text,motion_watch_ref(0x5ccca));assert(!strcmp(text,"0000000000123456/0/2/2b6e910"));

    memset(test_mw_object,0,sizeof(test_mw_object));memset(test_mw_ctrl,0,sizeof(test_mw_ctrl));memset(test_mw_agent,0,sizeof(test_mw_agent));
    memset(test_mw_model,0,sizeof(test_mw_model));memset(test_mw_slots,0,sizeof(test_mw_slots));
    int attention=5;memcpy(test_mw_object+0x1ec,&attention,4);
    motion_watch_row_t r;void *agent=(void*)1;
    /* No controller: attention only. */
    assert(motion_watch_read(test_mw_object,0xa214c,7,&r,&agent) && r.attention==5 && r.ctrl_vt==0xffffffffu && r.flags==512 && !agent);
    /* A NavigationController with its back pointer, slots, cache and a flight model. */
    void *p=test_mw_ctrl;memcpy(test_mw_object+0x200,&p,8);
    void *vt=image+MOTION_WATCH_NAV_VTABLE;memcpy(test_mw_ctrl,&vt,8);
    p=test_mw_object;memcpy(test_mw_ctrl+0x28,&p,8);p=test_mw_slots;memcpy(test_mw_ctrl+0x30,&p,8);p=test_mw_agent;memcpy(test_mw_ctrl+0x40,&p,8);
    for(unsigned i=0;i<0x40;i++) test_mw_ctrl[0x60+i]=(unsigned char)(0x80+i);
    for(unsigned i=0;i<0x60;i++) {test_mw_slots[i]=(unsigned char)i;test_mw_slots[0x1a0+i]=(unsigned char)(0x40+i);}
    p=test_mw_object;memcpy(test_mw_agent+0xef0,&p,8);p=test_mw_model;memcpy(test_mw_agent+0xf10,&p,8);
    vt=image+0x2c4e9c8;memcpy(test_mw_model,&vt,8);for(unsigned i=0;i<0x50;i++) test_mw_model[0x20+i]=(unsigned char)(0xc0+i);
    uint64_t target_ref=0x5ccca;memcpy(test_mw_model+0x60,&target_ref,8);
    uint32_t buffer=1;memcpy(image+MOTION_WATCH_INDEX,&buffer,4);
    /* Empty path: count 0. */
    assert(motion_watch_read(test_mw_object,0xa214c,8,&r,&agent) && r.flags==(31|64|256|512|1024) && r.ctrl_vt==MOTION_WATCH_NAV_VTABLE && r.model_vt==0x2c4e9c8 && r.index==1);
    assert(!r.formation && !r.leader.key && !r.leader_ext[0]);
    assert(agent==test_mw_agent && !r.path_count && !r.path_ref.key);
    /* The Steering model: an empty avoidance cache (count 0, time 0) and the constructor draws at +0x33c/+0x340. */
    assert(!r.flight_obstacles && !r.flight_avoid_time && !r.flight_angles[0]);
    {
        static __declspec(align(16)) unsigned char contacts[3*0x80];unsigned char *cb=contacts,*ce=contacts+3*0x80;double due=17.25;
        memcpy(test_mw_model+0x88,&due,8);memcpy(test_mw_model+0x90,&cb,8);memcpy(test_mw_model+0x98,&ce,8);memcpy(test_mw_model+0xa0,&ce,8);
        uint32_t draws[2]={0x4063a508u,0x4044805cu};memcpy(test_mw_model+0x33c,draws,8);
        assert(motion_watch_read(test_mw_object,0xa214c,8,&r,NULL) && r.flight_obstacles==3 && !memcmp(&r.flight_avoid_time,&due,8) && !memcmp(r.flight_angles,draws,8));
        for(unsigned b=0x20;b<0x38;b++) assert(!r.model_ext[b]); /* the vector's pointers are zeroed */
        assert(!memcmp(r.model_ext+0x18,&due,8));
        ce=contacts+0x80+8;memcpy(test_mw_model+0x98,&ce,8); /* not a whole contact: unreadable */
        assert(motion_watch_read(test_mw_object,0xa214c,8,&r,NULL) && r.flight_obstacles==0xffffffffu && !r.flight_avoid_time && !r.flight_angles[0]);
        memset(test_mw_model+0x88,0,0x20);memset(test_mw_model+0x33c,0,8);
    }
    assert(r.slot[0][5]==5 && r.slot[1][5]==0x45 && r.cache[3]==0x83 && r.target[0x3f]==0xff);
    /* The target's reference ID is masked and mapped. */
    for(unsigned i=0x40;i<0x48;i++) assert(!r.target[i]);
    assert(r.target[0x48]==0x08 && r.target_ref.key==0x123456 && r.target_ref.depth==2 && r.target_ref.type==0x2b6e910u);
    /* Two FlightPoints: count 2, the first point with its reference masked and mapped and the connection as a flag. */
    memset(test_mw_path,0,sizeof(test_mw_path));
    for(unsigned i=0;i<0x50;i++) test_mw_path[0x10+i]=(unsigned char)(0x30+i);
    uint64_t point_ref=0x5b065,connection=0x1234;memcpy(test_mw_path+0x10,&point_ref,8);memcpy(test_mw_path+0x18,&connection,8);
    unsigned char *begin=test_mw_path,*end=test_mw_path+2*MOTION_PATH_STRIDE;memcpy(test_mw_agent+0x6a8,&begin,8);memcpy(test_mw_agent+0x6b0,&end,8);
    assert(motion_watch_read(test_mw_object,0xa214c,8,&r,NULL) && r.flags==(63|64|256|512|1024) && r.path_count==2 && r.path_conn==1);
    assert(r.path_ref.key==0xa214c && !r.path_ref.depth && r.path_ref.type==0xffffffffu);
    for(unsigned i=0;i<16;i++) assert(!r.path0[i]);
    assert(r.path0[0x10]==0x40 && r.path0[0x4f]==0x7f);
    /* A malformed vector (not a whole number of points): no path fields. */
    end=test_mw_path+MOTION_PATH_STRIDE+8;memcpy(test_mw_agent+0x6b0,&end,8);
    assert(motion_watch_read(test_mw_object,0xa214c,8,&r,NULL) && r.flags==(15|64|256|512|1024) && !r.path_count);
    end=test_mw_path+2*MOTION_PATH_STRIDE;memcpy(test_mw_agent+0x6b0,&end,8);
    /* The occupied ship's PlayerController: the same layout, flag 8192. */
    vt=image+PSLOT_VTABLE;memcpy(test_mw_ctrl,&vt,8);
    assert(motion_watch_read(test_mw_object,0xa214c,8,&r,&agent) && r.flags==(63|64|256|512|1024|8192) && r.ctrl_vt==PSLOT_VTABLE && agent==test_mw_agent);
    assert(r.slot[0][5]==5 && r.model_vt==0x2c4e9c8 && r.path_count==2);
    vt=image+MOTION_WATCH_NAV_VTABLE;memcpy(test_mw_ctrl,&vt,8);
    /* Another controller type or a foreign back pointer: no pathing fields. */
    vt=image+0x2abd888;memcpy(test_mw_ctrl,&vt,8);assert(motion_watch_read(test_mw_object,0xa214c,9,&r,&agent) && r.ctrl_vt==0x2abd888 && r.flags==512 && !agent);
    vt=image+MOTION_WATCH_NAV_VTABLE;memcpy(test_mw_ctrl,&vt,8);p=test_mw_agent;memcpy(test_mw_ctrl+0x28,&p,8);
    assert(motion_watch_read(test_mw_object,0xa214c,9,&r,NULL) && r.flags==512);p=test_mw_object;memcpy(test_mw_ctrl+0x28,&p,8);
    /* An agent of another object: no path or model fields. */
    p=test_mw_ctrl;memcpy(test_mw_agent+0xef0,&p,8);
    assert(motion_watch_read(test_mw_object,0xa214c,9,&r,&agent) && r.flags==(7|512) && r.model_vt==0xffffffffu && !r.path_count && !agent);
    p=test_mw_object;memcpy(test_mw_agent+0xef0,&p,8);
    /* Agent fields, model plan and the formation leader. */
    {
        float speed=123.5f;memcpy(test_mw_agent+0x364,&speed,4);test_mw_agent[0x4d0]=1;test_mw_agent[0x4d2]=2;test_mw_agent[0xf7a]=7;test_mw_agent[0x4d3]=3;
        double decided=-1.0;memcpy(test_mw_agent+0xf40,&decided,8);float limit=96.0f;memcpy(test_mw_object+0x3b0,&limit,4);test_mw_object[0x3c8]=1;
        for(unsigned i=0;i<0x70;i++) test_mw_model[0x70+i]=(unsigned char)(0x10+i);
        static __declspec(align(16)) unsigned char formation[0x80],leader_agent[0x1000];static void *slot;
        memset(formation,0,sizeof(formation));memset(leader_agent,0,sizeof(leader_agent));
        slot=leader_agent;void *ps=&slot;memcpy(formation+0x48,&ps,8);p=formation;memcpy(test_mw_agent+0xf20,&p,8);
        float leader_speed=200.0f;memcpy(leader_agent+0x364,&leader_speed,4);p=test_mw_turret;memcpy(leader_agent+0xef0,&p,8); /* leader object: id 0x5ccca */
        assert(motion_watch_read(test_mw_object,0xa214c,8,&r,NULL) && r.flags==(63|64|128|256|512|1024) && r.formation==1);
        assert(!memcmp(r.agent_ext,&speed,4) && r.agent_ext[4]==1 && r.agent_ext[5]==2 && r.agent_ext[28]==7 && r.agent_ext[MOTION_AGENT_EXT-1]==3);
        assert(!memcmp(r.leader_ext,&leader_speed,4) && r.leader.key==0x123456 && r.leader.depth==2 && r.leader.type==0x2b6e910u);
        /* The Steering class: behaviour pointers (+0x70/+0x78) and the avoidance vector (+0x90..+0xa7) zeroed; this vector is malformed. */
        for(unsigned b=0;b<0x70;b++) assert(r.model_ext[b]==((b<0x10 || (b>=0x20 && b<0x38) || (b>=0x44 && b<0x48))?0:(unsigned char)(0x10+b)));   /* +0xb4..+0xb7 padding */
        assert(r.flight_obstacles==0xffffffffu);
        /* Models smaller than the window: only their own bytes; the zeroed rest cannot differ between clients. */
        {
            void *model_vt;memcpy(&model_vt,test_mw_model,8);
            static const struct {uint32_t vt,own;} small[3]={{0x2c4f7d0u,0},{0x2c4f6f0u,0x30},{0x2c4f8d0u,0x50}};
            for(unsigned k=0;k<3;k++) {
                void *v=image+small[k].vt;memcpy(test_mw_model,&v,8);
                assert(motion_watch_read(test_mw_object,0xa214c,8,&r,NULL) && (r.flags&256) && r.model_vt==small[k].vt);
                for(unsigned b=0;b<0x70;b++) {
                    const int padding=small[k].vt==0x2c4f6f0u && (b==0x17 || (b>=0x28 && b<0x30));
                    assert(r.model_ext[b]==(b<small[k].own && !padding?test_mw_model[0x70+b]:0));
                }
            }
            memcpy(test_mw_model,&model_vt,8);assert(motion_watch_read(test_mw_object,0xa214c,8,&r,NULL) && r.model_ext[0x6f]==0x7f);
        }
        assert(!memcmp(r.agent_ext+37,&decided,8) && !memcmp(r.ship_ext,&limit,4) && r.ship_ext[4]==1 && !r.wingmen && !r.wingman0.key);
        /* Flight parameters: the read slot (index 1), the written slot (index2 0) and the third copy (+0x340). */
        {
            float travel=2413.0f;memcpy(test_mw_agent+0x1a0+0x24,&speed,4);test_mw_agent[0x1a0+0x192]=1;memcpy(test_mw_agent+0x1a0+0x198,&travel,4);
            uint32_t prior_index2=0,zero=0;memcpy(&prior_index2,image+MOTION_PATHING_INDEX2,4);memcpy(image+MOTION_PATHING_INDEX2,&zero,4);
            assert(motion_watch_read(test_mw_object,0xa214c,8,&r,NULL) && (r.flags&1024) && r.index==1);
            assert(!memcmp(r.fp,&speed,4) && r.fp[6]==1 && !memcmp(r.fp+12,&travel,4) && !r.fp[16+6] && !memcmp(r.fp+32,&speed,4) && r.fp[32+6]==2);
            uint32_t two=2;memcpy(image+MOTION_PATHING_INDEX2,&two,4);
            assert(motion_watch_read(test_mw_object,0xa214c,8,&r,NULL) && !(r.flags&1024)); /* an index outside 0/1: no fields */
            memcpy(image+MOTION_PATHING_INDEX2,&prior_index2,4);
            memset(test_mw_agent+0x1a0+0x24,0,4);test_mw_agent[0x1a0+0x192]=0;memset(test_mw_agent+0x1a0+0x198,0,4);
        }
        /* As a formation leader: the wingmen count and the first wingman's ship. */
        static __declspec(align(16)) unsigned char led[0x80],wingman_agent[0x1000];static void *wingmen[2];
        memset(led,0,sizeof(led));memset(wingman_agent,0,sizeof(wingman_agent));wingmen[0]=wingman_agent;wingmen[1]=wingman_agent;
        void *wb=&wingmen[0],*we=&wingmen[2];memcpy(led+0x48,&wb,8);memcpy(led+0x50,&we,8);p=led;memcpy(test_mw_agent+0xf18,&p,8);
        p=test_mw_station;memcpy(wingman_agent+0xef0,&p,8); /* wingman ship: id 0x3edc2 (saved 0x123456) */
        assert(motion_watch_read(test_mw_object,0xa214c,8,&r,NULL) && r.wingmen==2 && r.wingman0.key==0x123456 && !r.wingman0.depth);
        memset(test_mw_agent+0xf18,0,8);
        memset(formation+0x48,0,8);assert(motion_watch_read(test_mw_object,0xa214c,8,&r,NULL) && r.formation==1 && !(r.flags&128) && !r.leader.key);
        memset(formation+0x48,0,8);memcpy(formation+0x48,&ps,8);
    }
    /* Rows through the ring: raw hex, no pointers or runtime IDs; the state slots as cur (slot[index]) and prev. */
    motion_watch_row_t *saved_ring=motion_watch_ring;LONG64 saved_rows=motion_watch_rows;
    static motion_watch_row_t ring[4];motion_watch_ring=ring;motion_watch_rows=1;
    assert(motion_watch_read(test_mw_object,0xa214c,12,&ring[0],NULL));
    char out[3072];assert(motion_watch_row(0,out,sizeof(out))>0);
    assert(strstr(out,"index=0 tick=12 saved=00000000000a214c attention=5 ctrl=02c51158 model=02c4e9c8 flags=2047 cur=404142"));
    assert(strstr(out," prev=000102") && strstr(out," cache=808182") && strstr(out," target=c0c1c2") && !strstr(out,"buffer="));
    assert(strstr(out," model_ext=00000000000000000000000000000000202122") && strstr(out," flight=4294967295/0000000000000000/0000000000000000") && strstr(out," agent_ext=0000f74201020000") && strstr(out,"070000000000000000000000000000f0bf0003 formation=1 leader=0000000000123456/0/2/2b6e910 leader_ext=00004843"));
    ring[0].index=0;assert(motion_watch_row(0,out,sizeof(out))>0 && strstr(out," cur=000102") && strstr(out," prev=404142"));
    assert(motion_watch_read(test_mw_object,0xa214c,12,&ring[0],NULL));
    assert(strstr(out,"fcfdfeff000000000000000008090a"));
    assert(strstr(out," target_ref=0000000000123456/0/2/2b6e910 path=2 path0=0000000000000000000000000000000040414243"));
    assert(strstr(out," path0_ref=00000000000a214c/0/0/ffffffff/1") && strstr(out," ship_ext=0000c04201 wingmen=0 wingman0=0000000000000000/2/0/0") && strlen(out)<2400);
    assert(!motion_watch_row(1,out,sizeof(out)));
    { /* The Steering group: a constructor draw alone parts flight=. */
        char a[512],b[512];ring[1]=ring[0];ring[1].flight_angles[0]^=1;motion_watch_rows=2;
        assert(motion_watch_digest(0,a,sizeof(a))>0 && motion_watch_digest(1,b,sizeof(b))>0 && strlen(a)<400 && !strstr(a," flight=00000000"));
        const char *fa=strstr(a," flight="),*fb=strstr(b," flight="),*ca=strstr(a," cur="),*cb=strstr(b," cur=");
        assert(fa && fb && ca && cb && strcmp(fa,fb) && fa-ca==fb-cb && !strncmp(ca,cb,(size_t)(fa-ca)));
        motion_watch_rows=1;
    }
    motion_watch_rows=MOTION_WATCH_RING+3;assert(motion_watch_row(1,out,sizeof(out))>0 && !strcmp(out,"index=1 evicted=1"));
    motion_watch_ring=saved_ring;motion_watch_rows=saved_rows;memset(image+MOTION_WATCH_INDEX,0,4);
    /* Adding refuses after the first admitted frame, off the MoveWorker, or when the lookup is not the pinned one (zero image). */
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;
    stats.issued=0;ui_thread=GetCurrentThreadId();assert(!motion_watch_add(0xa214c) && !motion_watch_count);
    stats.issued=3;assert(!motion_watch_add(0xa214c));stats.issued=0;assert(!motion_watch_add(0));
    ui_thread=GetCurrentThreadId()+1;assert(!motion_watch_add(0xa214c));
    stats.issued=saved_issued;ui_thread=saved_ui;
    assert(motion_watch_frame(5)); /* nothing armed: nothing to do */
    char status[256];assert(motion_watch_status(status,sizeof(status))>0 && strstr(status,"watched=0 ") && strstr(status," targets=0 "));
    /* Order rows: only watched ships, the point bytes (reference and connection masked) and the site; sites are e8 calls of the queue. */
    {
        motion_order_row_t *saved_ring=motion_order_ring;LONG64 saved_rows=motion_order_rows;static motion_order_row_t ring[4];
        memset(ring,0,sizeof(ring));motion_order_ring=ring;motion_order_rows=0;motion_watch_saved[0]=0xa214c;motion_watch_count=1;
        static unsigned char point[0x80];for(unsigned i=0;i<0x80;i++) point[i]=(unsigned char)i;
        uint64_t point_ref=0x5ccca;memcpy(point+0x10,&point_ref,8);
        motion_order_note(2,0x5b065,point);motion_order_note(1,0x9999,point);
        assert(motion_order_rows==1 && ring[0].ready==1 && ring[0].saved==0xa214c && ring[0].site==0x6e0c72 && !ring[0].point[0] && !ring[0].point[15]);
        assert(ring[0].point[0x10]==0x20 && ring[0].point[0x4f]==0x5f && ring[0].connection==1 && ring[0].ref.key==0x123456 && ring[0].ref.depth==2);
        char row[512];assert(motion_order_row(0,row,sizeof(row))>0 && strstr(row,"tick=") && strstr(row," saved=00000000000a214c site=006e0c72 chain=") &&
                            strstr(row," ref=0000000000123456/0/2/2b6e910/1 point=0000000000000000000000000000000020212223"));
        assert(!motion_order_row(1,row,sizeof(row)));
        ring[0].ready=0;assert(!motion_order_row(0,row,sizeof(row))); /* still being written */
        motion_order_ring=saved_ring;motion_order_rows=saved_rows;
        for(unsigned i=0;i<MOTION_ORDER_SITES;i++) assert(motion_order_state(i)<0); /* zero image: no e8 */
        for(unsigned i=0;i<MOTION_ORDER_SITES;i++) {image[motion_order_sites[i]]=0xe8;int32_t d=motion_order_rel(motion_order_sites[i],image+MOTION_ORDER_QUEUE);memcpy(image+motion_order_sites[i]+1,&d,4);}
        for(unsigned i=0;i<MOTION_ORDER_SITES;i++) assert(motion_order_state(i)==0);
        assert(!motion_order_pins() && !motion_target_pins() && !motion_order_arm() && !motion_order_installed);
        for(unsigned i=0;i<MOTION_ORDER_SITES;i++) memset(image+motion_order_sites[i],0,5);
    }
    /* Target rows: only models whose agent is a watched ship's last read agent; transform and reference as passed. */
    {
        motion_target_row_t *saved_ring=motion_target_ring;LONG64 saved_rows=motion_target_rows;static motion_target_row_t ring[4];
        memset(ring,0,sizeof(ring));motion_target_ring=ring;motion_target_rows=0;
        motion_watch_saved[0]=0xa214c;motion_watch_agent[0]=test_mw_agent;motion_watch_count=1;
        static __declspec(align(16)) unsigned char model[0x80],other[0x80],transform[0x40];
        memset(model,0,sizeof(model));memset(other,0,sizeof(other));
        vt=image+0x2c4f610;memcpy(model,&vt,8);memcpy(other,&vt,8);p=test_mw_agent;memcpy(model+0x10,&p,8);p=test_mw_ctrl;memcpy(other+0x10,&p,8);
        for(unsigned i=0;i<0x40;i++) transform[i]=(unsigned char)(0x50+i);
        motion_target_note(other,transform,0x5ccca,NULL);assert(!motion_target_rows);
        motion_target_note(model,transform,0x5ccca,(void*)transform);
        assert(motion_target_rows==1 && ring[0].ready==1 && ring[0].saved==0xa214c && ring[0].model_vt==0x2c4f610u && ring[0].flag==1);
        assert(ring[0].transform[0]==0x50 && ring[0].transform[0x3f]==0x8f && ring[0].ref.key==0x123456 && ring[0].ref.depth==2);
        motion_target_note(model,NULL,0,NULL);assert(motion_target_rows==2 && ring[1].flag==0x80000000u && !ring[1].ref.type);
        char row[512];assert(motion_target_row(0,row,sizeof(row))>0 && strstr(row,"index=0 tick=") &&
            strstr(row," saved=00000000000a214c model=02c4f610 flag=1 chain=") &&
            strstr(row," ref=0000000000123456/0/2/2b6e910 transform=505152") && strstr(row,"8d8e8f"));
        assert(!motion_target_row(2,row,sizeof(row)));
        ring[1].ready=0;assert(!motion_target_row(1,row,sizeof(row)));
        motion_target_ring=saved_ring;motion_target_rows=saved_rows;
        assert(motion_target_owned()); /* nothing swapped */
    }
    /* Event rows: only events whose target is a watched ship (runtime compare); time, the thread's game time, +0x18. */
    {
        motion_event_row_t *saved_ring=motion_event_ring;LONG64 saved_rows=motion_event_rows;static motion_event_row_t ring[4];
        memset(ring,0,sizeof(ring));motion_event_ring=ring;motion_event_rows=0;
        motion_watch_saved[0]=0xa214c;motion_watch_runtime[0]=0x5b065;motion_watch_count=1;
        static __declspec(align(16)) unsigned char ship[0x40],other[0x40],event[0x40];memset(ship,0,sizeof(ship));memset(other,0,sizeof(other));
        uint64_t id=0x5b065,other_id=0x9999,data=0x1122334455667788ULL;memcpy(ship+8,&id,8);memcpy(other+8,&other_id,8);memcpy(event+0x18,&data,8);
        motion_event_note(1,event,other,12.5);assert(!motion_event_rows);
        motion_event_note(1,event,ship,12.5);
        double t=12.5;uint64_t bits;memcpy(&bits,&t,8);
        assert(motion_event_rows==1 && ring[0].ready==1 && ring[0].saved==0xa214c && ring[0].kind==1 && ring[0].time==bits && ring[0].data==0x88);
        char row[512];assert(motion_event_row(0,row,sizeof(row))>0 && strstr(row," saved=00000000000a214c event=stop_travel time=4029000000000000 now=") &&
            strstr(row," data=0000000000000088 ui=") && strstr(row," chain="));
        /* +0x18 is the charge end time for charge-started; the other events end before it (no data). */
        motion_event_note(2,event,ship,12.5);motion_event_note(3,event,ship,12.5);
        assert(motion_event_rows==3 && ring[1].data==data && !ring[2].data);
        ring[0].ready=0;assert(!motion_event_row(0,row,sizeof(row)));assert(!motion_event_row(3,row,sizeof(row)));
        motion_event_ring=saved_ring;motion_event_rows=saved_rows;motion_watch_runtime[0]=0;
        assert(!motion_event_pins() && motion_event_owned()); /* zero image; nothing swapped */
    }
    /* Refresh rows: the controller's ship decides; rows print the call site, the slot and the fields. */
    {
        motion_watch_saved[0]=0xa214c;motion_watch_runtime[0]=0x5b065;motion_watch_count=1;
        static __declspec(align(16)) unsigned char nav[0x80],ship[0x40];memset(nav,0,sizeof(nav));memset(ship,0,sizeof(ship));
        uint64_t id=0x5b065;memcpy(ship+8,&id,8);void *q=ship;memcpy(nav+0x28,&q,8);
        assert(motion_refresh_watched(nav)==0);id=0x9999;memcpy(ship+8,&id,8);assert(motion_refresh_watched(nav)<0);assert(motion_refresh_watched(NULL)<0);
        motion_refresh_row_t *saved_ring=motion_refresh_ring;LONG64 saved_rows=motion_refresh_rows;static motion_refresh_row_t ring[2];
        memset(ring,0,sizeof(ring));motion_refresh_ring=ring;motion_refresh_rows=1;
        ring[0].tick=574;ring[0].saved=0x78855;ring[0].site=0xea1d9a;ring[0].flag=0;ring[0].slot=1;ring[0].ui=1;ring[0].after[6]=1;ring[0].ready=1;
        char row[512];assert(motion_refresh_row(0,row,sizeof(row))>0 &&
            strstr(row,"index=0 tick=574 saved=0000000000078855 site=00ea1d9a flag=0 slot=written ui=1 chain=") &&
            strstr(row," before=00000000000000000000000000000000 after=00000000000001000000000000000000"));
        ring[0].ready=0;assert(!motion_refresh_row(0,row,sizeof(row)));assert(!motion_refresh_row(1,row,sizeof(row)));
        motion_refresh_ring=saved_ring;motion_refresh_rows=saved_rows;motion_watch_runtime[0]=0;
        for(unsigned i=0;i<MOTION_REFRESH_SITES;i++) assert(motion_refresh_state(i)<0); /* zero image */
        assert(!motion_refresh_pins());
    }
    motion_watch_count=0;motion_watch_saved[0]=0;motion_watch_agent[0]=NULL;
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=prior;world_identity_count=prior_count;world_identity_sealed=prior_sealed;
    motion_watch_lookup=prior_lookup;
    printf("motion watch layout/reference/path/row/ring/order/target/refusal tests PASS\n");
}
/* Zone members: a capture item joins when its parent chain reaches a watched zone, only ships, once; it leaves
 * when a later capture does not find it, and its slot is reused; a full list counts. Rows carry the parent and the
 * dock controller state; digests name the field group. */
static void test_motion_zone(void) {
    motion_watch_lookup_fn prior_lookup=motion_watch_lookup;motion_watch_lookup=test_mw_lookup;
    world_identity_entry *prior=world_identities;unsigned prior_count=world_identity_count;LONG prior_sealed=world_identity_sealed;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);world_identity_count=0;world_identity_sealed=0;
    assert(world_identity_record(0x700,0x72c) && world_identity_record(0x59126,0x59a26) && world_identity_record(0x5b100,0xa1885));
    DWORD saved_ui=ui_thread;ui_thread=GetCurrentThreadId();
    uint64_t saved_issued=stats.issued;
    /* Refusals: a zero image has no pinned lookup; after the first admitted frame; a zero key. */
    stats.issued=0;assert(!motion_watch_zone_add(0x72c) && !motion_zone_count && !world_hash_member_note);
    stats.issued=1;assert(!motion_watch_zone_add(0x72c));stats.issued=0;assert(!motion_watch_zone_add(0));
    stats.issued=saved_issued;
    motion_watch_row_t *saved_ring=motion_watch_ring;LONG64 saved_rows=motion_watch_rows;
    static motion_watch_row_t ring[4];memset(ring,0,sizeof(ring));motion_watch_ring=ring;motion_watch_rows=0;
    motion_zone_saved[0]=0x72c;motion_zone_count=1;motion_zone_resolved=0;motion_zone_unresolved=0;
    motion_zone_capture=motion_zone_joins=motion_zone_leaves=motion_zone_gone=motion_zone_full=0;
    /* ship -> dock 0x59126 -> dock area -> station -> zone 0x700; another ship in a zone 0x701. */
    static __declspec(align(16)) unsigned char ship[0x400],dock[0x80],area[0x80],station[0x80],zone[0x80],other_zone[0x80],stray[0x400];
    unsigned char *chain[5]={ship,dock,area,station,zone};uint64_t ids[5]={0x5b100,0x59126,0x590a0,0x3d876,0x700};
    for(unsigned i=0;i<5;i++) {memset(chain[i],0,i?0x80:0x400);memcpy(chain[i]+8,&ids[i],8);}
    for(unsigned i=1;i<5;i++) memcpy(chain[i-1]+0x70,&chain[i],8);
    memset(other_zone,0,sizeof(other_zone));memset(stray,0,sizeof(stray));uint64_t id=0x701;memcpy(other_zone+8,&id,8);
    id=0x5b200;memcpy(stray+8,&id,8);void *p=other_zone;memcpy(stray+0x70,&p,8);
    world_hash_item item;memset(&item,0,sizeof(item));item.runtime=0x5b100;item.saved=0xa1885;item.type=0x2b7feb8u;
    /* Not a ship (a station's type) and a ship elsewhere: nothing joins; the zone resolves once. */
    item.type=0x2b764d8u;motion_zone_note(600,&item,ship);assert(!motion_watch_count && !motion_zone_resolved);
    item.type=0x2b7feb8u;item.runtime=0x5b200;item.saved=0xa2000;motion_zone_note(600,&item,stray);
    assert(!motion_watch_count && motion_zone_resolved && motion_zone_runtime[0]==0x700 && !motion_zone_unresolved && motion_zone_capture==600);
    item.runtime=0x5b100;item.saved=0xa1885;motion_zone_note(600,&item,ship);motion_zone_note(600,&item,ship);
    assert(motion_watch_count==1 && motion_watch_member[0] && motion_watch_saved[0]==0xa1885 && motion_watch_runtime[0]==0x5b100 && motion_watch_seen[0]==600);
    assert(motion_zone_joins==1 && motion_zone_members()==1);
    /* A created ship (high-bit key) right in the zone joins after it. */
    world_hash_item created=item;created.runtime=0x5b300;created.saved=0x8040520b9ad7a7f0ULL;created.type=0x2b82440u;
    static __declspec(align(16)) unsigned char ship2[0x400];memset(ship2,0,sizeof(ship2));id=0x5b300;memcpy(ship2+8,&id,8);p=zone;memcpy(ship2+0x70,&p,8);
    motion_zone_note(600,&created,ship2);assert(motion_watch_count==2 && motion_watch_saved[1]==0x8040520b9ad7a7f0ULL && motion_zone_joins==2);
    /* The next capture finds only the created ship: the first leaves at the next frame, its slot is reused. */
    motion_zone_note(1200,&created,ship2);motion_zone_expire();
    assert(!motion_watch_saved[0] && !motion_watch_runtime[0] && motion_watch_saved[1] && motion_zone_leaves==1 && motion_zone_members()==1);
    motion_zone_note(1200,&item,ship);assert(motion_watch_count==2 && motion_watch_saved[0]==0xa1885 && motion_zone_joins==3);
    motion_zone_expire();assert(motion_zone_members()==2);
    /* A static entry with the same key stays static. */
    motion_watch_member[0]=0;motion_zone_note(1800,&item,ship);assert(motion_watch_seen[0]==1200 && !motion_watch_member[0]);motion_watch_member[0]=1;
    /* Full list. */
    unsigned saved_count=motion_watch_count;motion_watch_count=MOTION_WATCH_MAX;
    world_hash_item extra=item;extra.saved=0xa3000;extra.runtime=0x5b400;motion_zone_note(1800,&extra,ship);assert(motion_zone_full==1);
    motion_watch_count=saved_count;
    char status[512];assert(motion_watch_status(status,sizeof(status))>0 && strstr(status," zones=1 unresolved=0 members=2 joins=3 leaves=1 gone=0 full=1 capture=1800"));
    /* Rows: parent (the dock, keyed) and the dock controller state while one moves the ship. */
    int attention=6;memcpy(ship+0x1ec,&attention,4);
    static __declspec(align(16)) unsigned char ctrl[0x200];memset(ctrl,0,sizeof(ctrl));
    void *vt=image+DOCK_CONTROLLER_VTABLE;memcpy(ctrl,&vt,8);p=ship;memcpy(ctrl+0x70,&p,8);p=ctrl;memcpy(ship+0x200,&p,8);
    double start=17493.5;memcpy(ctrl+0x60,&start,8);uint32_t anim=3;memcpy(ctrl+0x120,&anim,4);
    assert(motion_watch_read(ship,0xa1885,17494,&ring[0],NULL) && (ring[0].flags&2048) && ring[0].ctrl_vt==DOCK_CONTROLLER_VTABLE);
    assert(ring[0].parent.key==0x59a26 && !ring[0].parent.depth && !strncmp(ring[0].dock,"DOCKCTL_1:",10));
    motion_watch_rows=1;char out[3072];assert(motion_watch_row(0,out,sizeof(out))>0);
    assert(strstr(out," parent=0000000000059a26/0/0/ffffffff dock=DOCKCTL_1:40d1156000000000") && strstr(out,":00000003:0000:"));
    /* Digests: one line per row; equal rows give equal digests, a pose byte moves cur only. */
    char d1[512],d2[512];assert(motion_watch_digest(0,d1,sizeof(d1))>0);
    assert(strstr(d1,"index=0 tick=17494 saved=00000000000a1885 attention=6 ctrl=02c3fdc0 model=ffffffff flags=2560 parent=0000000000059a26/0/0/ffffffff cur="));
    assert(strstr(d1," fp=") && strstr(d1," dock=") && !strstr(d1," dock=811c9dc5") && strlen(d1)<380);
    assert(strstr(d1," segments=00000000 spline=00000000 requests=00000000 obstacles=00000000 flight=00000000")); /* no spline or Steering model: the groups are 0 */
    ring[1]=ring[0];ring[1].slot[0][7]^=1;motion_watch_rows=2;assert(motion_watch_digest(1,d2,sizeof(d2))>0);
    char *c1=strstr(d1," cur="),*c2=strstr(d2," cur=");assert(c1 && c2 && strncmp(c1,c2,13) && !strcmp(strstr(d1," prev="),strstr(d2," prev=")));
    /* Without a dock controller: dock= is the empty digest and the row prints '-'. */
    memset(ship+0x200,0,8);assert(motion_watch_read(ship,0xa1885,17495,&ring[2],NULL) && !(ring[2].flags&2048) && !ring[2].dock[0]);
    motion_watch_rows=3;assert(motion_watch_digest(2,d2,sizeof(d2))>0 && strstr(d2," dock=811c9dc5"));
    assert(motion_watch_row(2,out,sizeof(out))>0 && strstr(out," dock=-"));
    motion_watch_rows=MOTION_WATCH_RING+5;assert(motion_watch_digest(1,d2,sizeof(d2))>0 && !strcmp(d2,"index=1 evicted=1"));
    motion_watch_ring=saved_ring;motion_watch_rows=saved_rows;
    for(unsigned i=0;i<MOTION_WATCH_MAX;i++) {motion_watch_saved[i]=motion_watch_runtime[i]=motion_watch_seen[i]=0;motion_watch_member[i]=0;motion_watch_agent[i]=NULL;}
    motion_watch_count=0;motion_zone_count=0;motion_zone_saved[0]=motion_zone_runtime[0]=0;motion_zone_resolved=0;
    motion_zone_capture=motion_zone_joins=motion_zone_leaves=motion_zone_gone=motion_zone_full=0;ui_thread=saved_ui;
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=prior;world_identity_count=prior_count;world_identity_sealed=prior_sealed;
    motion_watch_lookup=prior_lookup;
    printf("motion watch zone members, parent/dock rows and digests PASS\n");
}
/* Dock door rows: a test row only when the door's state, result, ships or deadline changed; listener rows. */
static void test_motion_doors(void) {
    motion_watch_lookup_fn prior_lookup=motion_watch_lookup;motion_watch_lookup=test_mw_lookup;
    world_identity_entry *prior=world_identities;unsigned prior_count=world_identity_count;LONG prior_sealed=world_identity_sealed;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);world_identity_count=0;world_identity_sealed=0;
    assert(world_identity_record(0x59130,0x59a2f) && world_identity_record(0x5b100,0xa1885));
    motion_door_row_t *saved_ring=motion_door_ring;LONG64 saved_rows=motion_door_rows;
    static motion_door_row_t ring[8];memset(ring,0,sizeof(ring));motion_door_ring=ring;motion_door_rows=0;
    memset(motion_door_table,0,sizeof(motion_door_table));motion_door_checks=motion_door_full=0;
    /* node: key +0x20 (the door), entry +0x28: inline ships (capacity 4), count, deadline, state. */
    static __declspec(align(16)) unsigned char node[0x80];memset(node,0,sizeof(node));
    unsigned char *entry=node+0x28;uint64_t door=0x59130,ship=0x5b100,capacity=4,count=1;double deadline=0.0;uint32_t state=2;
    memcpy(node+0x20,&door,8);memcpy(entry,&ship,8);memcpy(entry+0x20,&capacity,8);memcpy(entry+0x28,&count,8);
    memcpy(entry+0x30,&deadline,8);memcpy(entry+0x38,&state,4);
    motion_door_note(0,entry,1);motion_door_note(0,entry,1);assert(motion_door_rows==1 && motion_door_checks==2);
    assert(ring[0].ready==1 && !ring[0].kind && ring[0].site==0x41a62eu && ring[0].state==2 && ring[0].result==1 && ring[0].count==1);
    assert(ring[0].door.key==0x59a2f && ring[0].ships[0].key==0xa1885 && !ring[0].ships[1].key);
    /* The ship leaves the list: a row; the same door at the other site keeps its own last row. */
    count=0;memcpy(entry+0x28,&count,8);motion_door_note(0,entry,0);assert(motion_door_rows==2 && !ring[1].result && !ring[1].count && !ring[1].ships[0].key);
    motion_door_note(1,entry,0);assert(motion_door_rows==3 && ring[2].site==0x3f7c04u);
    /* A deadline moves: a row. */
    deadline=425.5;memcpy(entry+0x30,&deadline,8);motion_door_note(0,entry,0);assert(motion_door_rows==4);
    char row[512];assert(motion_door_row(3,row,sizeof(row))>0);
    assert(strstr(row,"index=3 tick=") && strstr(row," door=0000000000059a2f/0/0/ffffffff kind=test site=0041a62e state=2 result=0 count=0 ships=0000000000000000/2/0/0,"));
    assert(strstr(row," deadline=407a980000000000 now=") && strstr(row," ui="));
    assert(motion_door_row(0,row,sizeof(row))>0 && strstr(row," result=1 count=1 ships=00000000000a1885/0/0/ffffffff,0000000000000000/2/0/0,"));
    /* Listener callbacks: door key +0x18, open/close flag +0x28. */
    static __declspec(align(16)) unsigned char listener[0x40];memset(listener,0,sizeof(listener));memcpy(listener+0x18,&door,8);listener[0x28]=1;
    motion_door_listener_note(2,listener);assert(motion_door_rows==5 && ring[4].kind==2 && ring[4].flag==1 && ring[4].door.key==0x59a2f);
    assert(motion_door_row(4,row,sizeof(row))>0 && strstr(row," door=0000000000059a2f/0/0/ffffffff kind=finished flag=1 now="));
    ring[4].ready=0;assert(!motion_door_row(4,row,sizeof(row)));assert(!motion_door_row(5,row,sizeof(row)));
    motion_door_rows=MOTION_DOOR_RING+9;assert(motion_door_row(1,row,sizeof(row))>0 && !strcmp(row,"index=1 evicted=1"));
    /* Zero image: no e8 at either call, no pins, nothing to arm or own. */
    for(unsigned i=0;i<MOTION_DOOR_SITES;i++) assert(motion_door_state(i)<0);
    assert(!motion_door_pins() && !motion_door_arm() && motion_door_owned());
    motion_door_ring=saved_ring;motion_door_rows=saved_rows;memset(motion_door_table,0,sizeof(motion_door_table));motion_door_checks=0;
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=prior;world_identity_count=prior_count;world_identity_sealed=prior_sealed;
    motion_watch_lookup=prior_lookup;
    printf("motion watch dock door rows PASS\n");
}
/* Spline fields and request rows (cause B): two rigs stand for two clients, the same spline state at other
 * addresses and with other bytes wherever the model holds pointers, padding or unused vector lanes. */
typedef struct {
    unsigned char object[0x400],ctrl[0x100],agent[0x1000],slots[0x400],model[0x4c0],entry[3][0x50],segment[3][0x50];
    unsigned char request[0xc0],head[0x50],node[2][0x50],obstacles[2][0x80],comp[2][0x10];
    void *map_a[4],*map_b[2],*req_block[2],*req_map[2],*comps[3];
} test_sp_rig;
static __declspec(align(16)) test_sp_rig test_sp[2];
static void test_sp_bytes(unsigned char *at,unsigned n,unsigned seed) {for(unsigned i=0;i<n;i++) at[i]=(unsigned char)(seed+7*i);}
static void test_sp_vec(unsigned char *at,float x,float y,float z) {float v[3]={x,y,z};memcpy(at,v,12);} /* lane 3 untouched */
static void test_sp_fill(test_sp_rig *g,unsigned char noise) {
    memset(g,noise,sizeof(*g));
    unsigned char *m=g->model;void *p;
    /* Object, NavigationController, agent (the motion watch's chain), index 0. */
    memset(g->object,0,sizeof(g->object));memset(g->ctrl,0,sizeof(g->ctrl));memset(g->agent,0,sizeof(g->agent));memset(g->slots,0,sizeof(g->slots));
    p=g->ctrl;memcpy(g->object+0x200,&p,8);p=image+MOTION_WATCH_NAV_VTABLE;memcpy(g->ctrl,&p,8);p=g->object;memcpy(g->ctrl+0x28,&p,8);
    p=g->slots;memcpy(g->ctrl+0x30,&p,8);p=g->agent;memcpy(g->ctrl+0x40,&p,8);p=g->object;memcpy(g->agent+0xef0,&p,8);p=m;memcpy(g->agent+0xf10,&p,8);
    /* The model: vtable, agent, target. */
    p=image+MOTION_SPLINE_VTABLE;memcpy(m,&p,8);p=g->agent;memcpy(m+0x10,&p,8);memset(m+0x20,0,0x50);
    /* Deque A (+0x70): map size 4, offset 3, two entries (map[3], map[0]); deque B (+0x98): map size 2, offset 1, one entry. */
    uint64_t qa[5]={(uintptr_t)g->head+noise,(uintptr_t)g->map_a,4,3,2},qb[5]={(uintptr_t)g->node+noise,(uintptr_t)g->map_b,2,1,1};
    memcpy(m+0x70,qa,40);memcpy(m+0x98,qb,40);
    g->map_a[3]=g->entry[0];g->map_a[0]=g->entry[1];g->map_a[1]=g->map_a[2]=(void*)(uintptr_t)noise;g->map_b[1]=g->entry[2];g->map_b[0]=NULL;
    for(unsigned e=0;e<3;e++) {
        unsigned char *x=g->entry[e],*z=g->segment[e];p=z;memcpy(x,&p,8);
        for(unsigned k=0;k<3;k++) test_sp_vec(x+0x10+16*k,(float)(10*e+k),1.5f,-2.0f);
        uint32_t index=e+1;memcpy(x+0x40,&index,4);x[0x44]=1;x[0x45]=(unsigned char)(e&1);
        p=image+MOTION_SPLINE_SEGMENT;memcpy(z,&p,8);float length=100.0f+(float)e;memcpy(z+8,&length,4);
        for(unsigned k=0;k<4;k++) test_sp_vec(z+0x10+16*k,(float)k,(float)e,3.25f);
    }
    /* The rest of the state: +0xc0 (all four lanes, as the native writes them), +0xd0 progress, +0xe0, flags, times,
     * the embedded BezierSplines, the transforms, +0x424, the request parameters. */
    test_sp_bytes(m+0xc0,16,1);float progress[4]={0.25f,1.0f,1.0f,0.5f};memcpy(m+0xd0,progress,16);test_sp_vec(m+0xe0,4.0f,5.0f,6.0f);
    test_sp_bytes(m+0x110,3,2);double step=12813.0/30.0;memcpy(m+0x118,&step,8);m[0x150]=0;
    for(unsigned b=0;b<2;b++) {
        unsigned char *z=m+0x158+0x50*b;p=image+MOTION_SPLINE_SEGMENT;memcpy(z,&p,8);test_sp_bytes(z+8,4,3+b);
        for(unsigned k=0;k<4;k++) test_sp_vec(z+0x10+16*k,(float)b,(float)k,0.0f);
    }
    test_sp_bytes(m+0x218,4,5);
    for(unsigned k=0;k<4;k++) {test_sp_vec(m+0x220+16*k,1.0f,(float)k,2.0f);test_sp_vec(m+0x450+16*k,3.0f,(float)k,4.0f);}
    m[0x424]=1;test_sp_bytes(m+0x490,0x30,9);
    /* The avoidance cache: two obstacles, published at 12800/30. */
    test_sp_bytes(g->obstacles[0],2*0x80,13);
    p=g->obstacles[0];memcpy(m+0xf0,&p,8);p=(unsigned char*)g->obstacles+2*0x80;memcpy(m+0xf8,&p,8);
    double published=12800.0/30.0;memcpy(m+0x108,&published,8);
    /* No request pending. */
    uint64_t qr[5]={(uintptr_t)g->request+noise,(uintptr_t)g->req_map,2,0,0};memcpy(m+0x428,qr,40);
    g->req_map[0]=g->req_block;g->req_map[1]=NULL;g->req_block[0]=g->request;g->req_block[1]=NULL;
    /* The request: a list of two route segments (sentinel head), its transform, parameters and flags; pending. */
    unsigned char *q=g->request;p=g->head;memcpy(q,&p,8);uint64_t size=2;memcpy(q+8,&size,8);
    for(unsigned k=0;k<4;k++) test_sp_vec(q+0x30+16*k,(float)k,7.0f,8.0f);
    test_sp_bytes(q+0x70,0x36,11);memset(q+0xa8,0,8);
    p=g->node[0];memcpy(g->head,&p,8);p=g->node[1];memcpy(g->node[0],&p,8);p=g->head;memcpy(g->node[1],&p,8);
    for(unsigned n=0;n<2;n++) for(unsigned k=0;k<4;k++) test_sp_vec(g->node[n]+0x10+16*k,(float)n,(float)k,9.0f);
    /* The component vector +0x10: two components (a vtable, an unkeyed runtime ID) at this rig's addresses. */
    g->comps[0]=g->comp[0];g->comps[1]=g->comp[1];g->comps[2]=(void*)&g->comps[2];
    for(unsigned c=0;c<2;c++) {p=image+MOTION_SPLINE_SEGMENT;memcpy(g->comp[c],&p,8);uint64_t id=0x7fff000000000000ull+c;memcpy(g->comp[c]+8,&id,8);}
    {   static void *vec[2][3];void **v=vec[g==&test_sp[0]?0:1];v[0]=&g->comps[0];v[1]=&g->comps[2];v[2]=&g->comps[2];p=v;memcpy(q+0x10,&p,8);}
}
static void test_sp_request(test_sp_rig *g,uint64_t count,uint32_t status,uint32_t flags) {
    memcpy(g->model+0x428+0x20,&count,8);memcpy(g->request+0xa8,&status,4);memcpy(g->request+0xac,&flags,4);
}
static unsigned char tsp_req[MOTION_SPLINE_REQ],tsp_pts[MOTION_SPLINE_PTS];   /* motion_spline_result's raw copies */
static int test_sp_row_body(uint64_t index,char *out,size_t cap) {
    char row[MOTION_SPLINE_ROW_CAP];if(!motion_spline_row(index,row,sizeof(row))) return 0;
    const char *body=strstr(row," tick=");if(!body) return 0;
    snprintf(out,cap,"%s",body);return 1;
}
static void test_motion_spline(void) {
    test_sp_fill(&test_sp[0],0x5a);test_sp_fill(&test_sp[1],0xa7);
    InitializeCriticalSection((CRITICAL_SECTION*)(image+0x3d74938));
    /* The segments: counts and one hash, equal across the rigs; unused lanes, entry +8 and +0x46.. are not state. */
    uint32_t count[2][2],h[2],core[2];
    for(unsigned c=0;c<2;c++) assert(motion_spline_segments(test_sp[c].model,count[c],&h[c]) && motion_spline_core(test_sp[c].model,&core[c]));
    assert(count[0][0]==2 && count[0][1]==1 && !memcmp(count[0],count[1],8) && h[0]==h[1] && core[0]==core[1]);
    test_sp[1].entry[1][0x40]^=1;assert(motion_spline_segments(test_sp[1].model,count[1],&h[1]) && h[0]!=h[1]);test_sp[1].entry[1][0x40]^=1;
    test_sp[1].segment[2][0x30]^=1;assert(motion_spline_segments(test_sp[1].model,count[1],&h[1]) && h[0]!=h[1]);test_sp[1].segment[2][0x30]^=1;
    test_sp[1].entry[0][0x08]^=1;test_sp[1].segment[0][0x1f]^=1;test_sp[1].entry[2][0x47]^=1;
    assert(motion_spline_segments(test_sp[1].model,count[1],&h[1]) && h[0]==h[1]);
    /* Another segment class hashes by its type only. */
    void *other=image+0x2b00000;memcpy(test_sp[1].segment[1],&other,8);assert(motion_spline_segments(test_sp[1].model,count[1],&h[1]) && h[0]!=h[1]);
    other=image+MOTION_SPLINE_SEGMENT;memcpy(test_sp[1].segment[1],&other,8);
    /* A malformed map: no hash. */
    uint64_t bad=3;memcpy(test_sp[1].model+0x70+0x10,&bad,8);assert(!motion_spline_segments(test_sp[1].model,count[1],&h[1]));
    bad=4;memcpy(test_sp[1].model+0x70+0x10,&bad,8);
    /* The core: a progress float is state, +0x120 and the deque pointers are not. */
    test_sp[1].model[0xd4]^=1;assert(motion_spline_core(test_sp[1].model,&core[1]) && core[0]!=core[1]);test_sp[1].model[0xd4]^=1;
    test_sp[1].model[0x120]^=1;test_sp[1].model[0x1fc]^=1;test_sp[1].model[0x1c4]^=1;assert(motion_spline_core(test_sp[1].model,&core[1]) && core[0]==core[1]);
    /* Rows and digests through motion_watch_read: model_ext without the deque pointers, flag 4096, the four groups. */
    motion_watch_row_t *saved_ring=motion_watch_ring;LONG64 saved_rows=motion_watch_rows;
    static motion_watch_row_t ring[4];motion_watch_ring=ring;
    uint32_t buffer=0;memcpy(image+MOTION_WATCH_INDEX,&buffer,4);
    for(unsigned c=0;c<2;c++) assert(motion_watch_read(test_sp[c].object,0xa1885,12813,&ring[c],NULL) && (ring[c].flags&(4096|256|8))==(4096|256|8));
    assert(ring[0].model_vt==MOTION_SPLINE_VTABLE && !memcmp(ring[0].model_ext,ring[1].model_ext,0x70));
    for(unsigned b=0;b<16;b++) assert(!ring[0].model_ext[b] && !ring[0].model_ext[0x28+b]);
    assert(ring[0].model_ext[0x10]==4 && ring[0].model_ext[0x18]==3 && ring[0].model_ext[0x20]==2 && ring[0].model_ext[0x38]==2 && ring[0].model_ext[0x48]==1);
    assert(ring[0].spline_count[0]==2 && ring[0].spline_count[1]==1 && !ring[0].spline_requests && ring[0].spline_obstacles==2);
    char d[2][512],full[3072];motion_watch_rows=2;
    for(unsigned c=0;c<2;c++) assert(motion_watch_digest(c,d[c],sizeof(d[c]))>0 && strlen(d[c])<380);
    assert(!strcmp(strstr(d[0]," tick="),strstr(d[1]," tick=")) && !strstr(d[0]," segments=00000000") && !strstr(d[0]," spline=00000000"));
    assert(motion_watch_row(0,full,sizeof(full))>0 && strstr(full," spline=2/1/") && strstr(full,"/0000803e0000803f0000803f0000003f requests=0 obstacles=2/"));
    /* A pending request is state (requests=), its status is not (wall-clock at slot 6). */
    test_sp_request(&test_sp[0],1,0,0);test_sp_request(&test_sp[1],1,1,0x00000100);
    for(unsigned c=0;c<2;c++) assert(motion_watch_read(test_sp[c].object,0xa1885,12813,&ring[c],NULL) && ring[c].spline_requests==1);
    for(unsigned c=0;c<2;c++) assert(motion_watch_digest(c,d[c],sizeof(d[c]))>0);
    assert(!strcmp(strstr(d[0]," tick="),strstr(d[1]," tick=")));
    /* A one-ulp progress difference parts spline= alone. */
    test_sp[1].model[0xd0]^=1;assert(motion_watch_read(test_sp[1].object,0xa1885,12813,&ring[1],NULL) && motion_watch_digest(1,d[1],sizeof(d[1]))>0);
    assert(strcmp(strstr(d[0]," spline="),strstr(d[1]," spline=")) && !strcmp(strstr(d[0]," requests="),strstr(d[1]," requests=")));
    assert(!strncmp(strstr(d[0]," segments="),strstr(d[1]," segments="),18));
    test_sp[1].model[0xd0]^=1;
    /* The Linear model's padding +0xac..+0xaf (model_ext 0x3c..0x3f) is zeroed. */
    {
        void *linear=image+MOTION_LINEAR_VTABLE;unsigned char kept[8];memcpy(kept,test_sp[0].model,8);memcpy(test_sp[0].model,&linear,8);
        memset(test_sp[0].model+0xa8,0xee,8);assert(motion_watch_read(test_sp[0].object,0xa1885,1,&ring[2],NULL) && ring[2].model_vt==MOTION_LINEAR_VTABLE);
        assert(ring[2].model_ext[0x38]==0xee && !ring[2].model_ext[0x3c] && !ring[2].model_ext[0x3f] && !(ring[2].flags&4096));
        memcpy(test_sp[0].model,kept,8);test_sp_fill(&test_sp[0],0x5a);
    }
    motion_watch_ring=saved_ring;motion_watch_rows=saved_rows;
    /* Request rows: the watch matches the model's agent; one row per update with a request around it. */
    test_sp_fill(&test_sp[0],0x5a);test_sp_fill(&test_sp[1],0xa7);
    motion_spline_row_t *saved_spline_ring=motion_spline_ring;LONG64 saved_spline_rows=motion_spline_rows;unsigned saved_armed=motion_spline_armed;
    static motion_spline_row_t spline_ring[16];memset(spline_ring,0,sizeof(spline_ring));motion_spline_ring=spline_ring;motion_spline_rows=0;
    unsigned saved_count=motion_watch_count;uint64_t saved_issued=stats.issued;
    spline_watch_t w;
    assert(!motion_spline_begin(test_sp[0].model,0,&w)); /* not armed */
    motion_spline_armed=1;motion_watch_count=1;motion_watch_saved[0]=0xa1885;
    assert(!motion_spline_begin(test_sp[0].model,0,&w)); /* armed, but the watch did not read this agent */
    static char rows[2][8][MOTION_SPLINE_ROW_CAP];unsigned nrows[2]={0,0};
    motion_watch_lookup_fn prior_lookup=motion_watch_lookup;motion_watch_lookup=test_mw_lookup;   /* cmp=: no engine lookup */
    for(unsigned c=0;c<2;c++) {
        test_sp_rig *g=&test_sp[c];motion_watch_agent[0]=g->agent;motion_spline_front[0]=NULL;motion_spline_ordinal[0]=0;
        LONG64 first=motion_spline_rows;
        /* 12812: nothing pending, no row. */
        stats.issued=12812;assert(motion_spline_begin(g->model,0,&w) && !w.slot);motion_spline_seen(g->model,&w);motion_spline_end(g->model,&w);
        assert(motion_spline_rows==first && !w.count_in);
        /* 12813: the path's request, still pending: a row (status 0, not consumed). */
        test_sp_request(g,1,0,0);stats.issued=12813;
        assert(motion_spline_begin(g->model,1,&w));motion_spline_seen(g->model,&w);motion_spline_end(g->model,&w);
        assert(motion_spline_rows==first+1 && w.ordinal==1 && !w.status && !w.result && !w.points);
        /* 12814: published; the update consumes it (the deque pops). */
        test_sp_request(g,1,1,0x00000100);stats.issued=12814;
        assert(motion_spline_begin(g->model,0,&w));motion_spline_seen(g->model,&w);
        assert(w.status==1 && w.points==2 && w.result && w.ordinal==1);
        uint64_t zero=0;memcpy(g->model+0x448,&zero,8);motion_spline_end(g->model,&w);
        assert(motion_spline_rows==first+2 && !motion_spline_front[0]);
        /* 12815: nothing again; 12816: a new request at the same address is the next ordinal. */
        stats.issued=12815;assert(motion_spline_begin(g->model,0,&w));motion_spline_seen(g->model,&w);motion_spline_end(g->model,&w);
        assert(motion_spline_rows==first+2);
        test_sp_request(g,1,0,0);stats.issued=12816;
        assert(motion_spline_begin(g->model,0,&w));motion_spline_seen(g->model,&w);motion_spline_end(g->model,&w);
        assert(motion_spline_rows==first+3 && w.ordinal==2);
        for(LONG64 i=first;i<motion_spline_rows;i++) assert(test_sp_row_body((uint64_t)i,rows[c][nrows[c]++],MOTION_SPLINE_ROW_CAP));
    }
    /* The two rigs (clients) write the same rows. */
    motion_watch_lookup=prior_lookup;
    assert(nrows[0]==3 && nrows[1]==3);
    for(unsigned i=0;i<3;i++) assert(!strcmp(rows[0][i],rows[1][i]));
    assert(strstr(rows[0][0],"tick=12813 saved=00000000000a1885 request=1 in=1 status=0 flags=00000000 points=0 result=00000000 out=1 consumed=0 segments="));
    assert(strstr(rows[0][0]," progress=0000803e0000803f0000803f0000003f cheap=1 waited=0"));
    assert(strstr(rows[0][1],"tick=12814 saved=00000000000a1885 request=1 in=1 status=1 flags=00000100 points=2 result=") && strstr(rows[0][1]," out=0 consumed=1 "));
    assert(!strstr(rows[0][1]," result=00000000") && strstr(rows[0][2],"tick=12816 saved=00000000000a1885 request=2 in=1 status=0 "));
    /* inp= (pending only: the input list, node 0's rows (0,k,9)), segs= (deque A's first entry: (0,1.5,-2), ...), obs= (count, time, raw) */
    assert(strstr(rows[0][0]," inp=2/000000000000000000001041000000000000803f00001041") && strstr(rows[0][1]," inp=-1/ "));
    assert(strstr(rows[0][0]," segs=2/1/000000000000c03f000000c0") && strstr(rows[0][0]," obs=2/") && strstr(rows[0][0],"/0d141b22"));
    {char want[96];snprintf(want,sizeof(want)," cmp=2/0000000000000000/%u/%x,0000000000000000/%u/%x",MOTION_REF_UNRESOLVED,MOTION_SPLINE_SEGMENT,MOTION_REF_UNRESOLVED,MOTION_SPLINE_SEGMENT);
     assert(strstr(rows[0][0],want) && strstr(rows[0][1]," cmp=-1/"));}   /* pending only, in order */
    /* The result hash: the route and request transform, not the unused lanes or the request's tail. */
    uint32_t points[2],r0=motion_spline_result(test_sp[0].request,&points[0],tsp_req,tsp_pts),r1=motion_spline_result(test_sp[1].request,&points[1],tsp_req,tsp_pts);
    assert(motion_spline_result(test_sp[0].request,&points[0],tsp_req,tsp_pts)==r0 && !memcmp(tsp_req,test_sp[0].request+0x30,12) && !memcmp(tsp_req+48,test_sp[0].request+0x70,0x36));   /* req= raw */
    assert(r0 && r0==r1 && points[0]==2);
    test_sp[1].node[1][0x1c]^=1;test_sp[1].request[0xb5]^=1;assert(motion_spline_result(test_sp[1].request,&points[1],tsp_req,tsp_pts)==r0);
    test_sp[1].node[1][0x20]^=1;assert(motion_spline_result(test_sp[1].request,&points[1],tsp_req,tsp_pts)!=r0);test_sp[1].node[1][0x20]^=1;
    uint64_t three=3;memcpy(test_sp[1].request+8,&three,8);assert(!motion_spline_result(test_sp[1].request,&points[1],tsp_req,tsp_pts));  /* a list shorter than its size */
    /* Rows: ready word, eviction. */
    spline_ring[0].ready=0;char row[512];assert(!motion_spline_row(0,row,sizeof(row)));
    motion_spline_rows=MOTION_SPLINE_RING+2;assert(motion_spline_row(1,row,sizeof(row))>0 && !strcmp(row,"index=1 evicted=1"));
    /* Arming: needs the watch, the spline hook, before the first frame, on the MoveWorker. */
    motion_spline_armed=0;motion_spline_ring=NULL;{
        DWORD saved_ui=ui_thread;unsigned saved_watch=motion_watch_armed;int saved_installed=spline_installed;
        ui_thread=GetCurrentThreadId();stats.issued=0;motion_watch_armed=1;spline_installed=0;assert(!motion_spline_arm());
        spline_installed=1;motion_watch_armed=0;assert(!motion_spline_arm());
        motion_watch_armed=1;stats.issued=5;assert(!motion_spline_arm());
        stats.issued=0;assert(motion_spline_arm() && motion_spline_armed && motion_spline_ring!=spline_ring);
        VirtualFree(motion_spline_ring,0,MEM_RELEASE);
        ui_thread=saved_ui;motion_watch_armed=saved_watch;spline_installed=saved_installed;
    }
    motion_spline_ring=saved_spline_ring;motion_spline_rows=saved_spline_rows;motion_spline_armed=saved_armed;
    motion_watch_count=saved_count;motion_watch_saved[0]=0;motion_watch_agent[0]=NULL;motion_spline_front[0]=NULL;motion_spline_ordinal[0]=0;
    stats.issued=saved_issued;
    DeleteCriticalSection((CRITICAL_SECTION*)(image+0x3d74938));
    printf("motion watch spline fields (segments, core, requests, obstacles, model_ext pointers) and request rows PASS\n");
}
static void test_motion_watch_native_image(const char *path) {
    FILE *file=fopen(path,"rb");assert(file);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    assert(fread(&dos,1,sizeof(dos),file)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0 && dos.e_lfanew<4096);
    assert(!fseek(file,dos.e_lfanew,SEEK_SET) && fread(&nt,1,sizeof(nt),file)==sizeof(nt) && nt.OptionalHeader.SizeOfImage==0x6fa9000);
    unsigned char *prior=image;image=VirtualAlloc(NULL,nt.OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(image);
    for(unsigned i=0;i<nt.FileHeader.NumberOfSections;i++) {
        IMAGE_SECTION_HEADER section;assert(fread(&section,1,sizeof(section),file)==sizeof(section));long next=ftell(file);
        if(section.SizeOfRawData) {
            assert(!fseek(file,section.PointerToRawData,SEEK_SET));
            assert(fread(image+section.VirtualAddress,1,section.SizeOfRawData,file)==section.SizeOfRawData);
        }
        assert(!fseek(file,next,SEEK_SET));
    }
    fclose(file);
    /* The pinned lookup, the NavigationController vtable (slot 33 is its upkeep 0xea77f0) and the flight model slot. */
    assert(hash_ok(image+0xced70,184,"3209f7be0616de6d0ecc20ce3cfd82fd16d0524c09635d86995210598b0a5f36"));
    assert(*(uint64_t*)(image+MOTION_WATCH_NAV_VTABLE+33*8)==0x140000000ULL+0xea77f0u);
    /* The four AI move point queue calls and their pins. */
    for(unsigned i=0;i<MOTION_ORDER_SITES;i++) assert(motion_order_state(i)==0);
    assert(motion_order_pins());
    image[0x6e0900]^=1;assert(!motion_order_pins());image[0x6e0900]^=1;
    /* Spline, Linear, Forced and MassTraffic keep the base SetTarget in slot 3 (unrelocated file image: absolute VAs). */
    for(unsigned i=0;i<MOTION_TARGET_ENTRIES;i++) assert(*(uint64_t*)(image+motion_target_entries[i])==0x140000000ULL+MOTION_TARGET_SET);
    assert(*(uint64_t*)(image+0x2c4e9c8+0x18)!=0x140000000ULL+MOTION_TARGET_SET); /* Steering overrides it */
    assert(motion_target_pins());
    image[MOTION_TARGET_SET+0x25]^=1;assert(!motion_target_pins());image[MOTION_TARGET_SET+0x25]^=1;
    /* The boost and travel mode events: Dispatch in vtable slot 2, pinned. */
    for(unsigned i=0;i<MOTION_EVENT_KINDS;i++) assert(*(uint64_t*)(image+motion_event_kinds[i].vtable+0x10)==0x140000000ULL+motion_event_kinds[i].dispatch);
    assert(motion_event_pins());
    image[motion_event_kinds[2].dispatch+4]^=1;assert(!motion_event_pins());image[motion_event_kinds[2].dispatch+4]^=1;
    /* The traced calls of the flight parameter refresh 0xea1e80, pinned; none inside another module's pinned code. */
    for(unsigned i=0;i<MOTION_REFRESH_SITES;i++) assert(motion_refresh_state(i)==0);
    assert(motion_refresh_pins());
    /* Dock doors: the two test calls, the test's pin, the listener's slots 569/570 and its constructor's vtable. */
    for(unsigned i=0;i<MOTION_DOOR_SITES;i++) assert(motion_door_state(i)==0);
    assert(motion_door_pins());
    image[MOTION_DOOR_CHECK+0x100]^=1;assert(!motion_door_pins());image[MOTION_DOOR_CHECK+0x100]^=1;
    assert(*(uint64_t*)(image+MOTION_DOOR_VTABLE+569*8)==0x140000000ULL+MOTION_DOOR_FINISHED && *(uint64_t*)(image+MOTION_DOOR_VTABLE+570*8)==0x140000000ULL+MOTION_DOOR_STARTED);
    assert(!memcmp(image+0x116ea02,"\x48\x8d\x0d",3) && 0x116ea09u+*(int32_t*)(image+0x116ea05)==MOTION_DOOR_VTABLE); /* the constructor 0x116e9d0 writes this vtable */
    assert(!memcmp(image+0x41a7f1,"\xe8",1) && 0x41a7f6u+*(int32_t*)(image+0x41a7f2)==0x116e9d0u); /* Container::Move's closing listener */
    /* Zones arm against the pinned lookup before the first frame and hook the captures; the ship types are vtables. */
    {
        DWORD saved_ui=ui_thread;uint64_t saved_issued=stats.issued;motion_watch_row_t *saved_ring=motion_watch_ring;
        ui_thread=GetCurrentThreadId();stats.issued=0;
        assert(motion_watch_zone_add(0x72c) && motion_watch_zone_add(0x72c) && motion_zone_count==1 && world_hash_member_note==motion_zone_note && motion_watch_armed);
        for(unsigned i=0;i<4;i++) assert(*(uint64_t*)(image+motion_zone_ship_types[i]-8)>0x140000000ULL); /* the Complete Object Locator */
        if(motion_watch_ring!=saved_ring) {VirtualFree(motion_watch_ring,0,MEM_RELEASE);motion_watch_ring=saved_ring;}
        motion_zone_count=0;motion_zone_saved[0]=0;world_hash_member_note=NULL;motion_watch_armed=0;ui_thread=saved_ui;stats.issued=saved_issued;
    }
    image[MOTION_REFRESH_FN+0x12e]^=1;assert(!motion_refresh_pins());image[MOTION_REFRESH_FN+0x12e]^=1;
    for(unsigned i=0;i<MOTION_REFRESH_SITES;i++) {
        unsigned at=motion_refresh_sites[i];
        assert(!(at+5>NAV_CADENCE_PIN_A && at<NAV_CADENCE_PIN_A+NAV_CADENCE_PIN_A_SIZE) && !(at+5>NAV_CADENCE_PIN_B && at<NAV_CADENCE_PIN_B+NAV_CADENCE_PIN_B_SIZE));
        for(unsigned p=0;p<sizeof(world_nav_pins)/sizeof(*world_nav_pins);p++)
            assert(!(at+5>world_nav_pins[p].rva && at<world_nav_pins[p].rva+world_nav_pins[p].length));
        assert(!(at+5>0xea8cf0u && at<0xea8cf0u+1332u)); /* creation trace contract and registry origin identity */
    }
    /* The refresh's writes: +0x191 boost, +0x192 the engine's travel byte +0x340, +0x193, +0x194, +0x198. */
    assert(!memcmp(image+0xea1f83,"\x41\x88\x87\x91\x01\x00\x00",7) && !memcmp(image+0xea1fa3,"\x0f\xb6\x88\x40\x03\x00\x00",7) &&
           !memcmp(image+0xea1fae,"\x41\x88\x8f\x92\x01\x00\x00",7));
    /* Cause B, the spline model's layout: the constructor 0xeb4f40 writes the vtable, the two segment deques and the embedded
     * BezierSplines; the segment builder 0xebcad0 allocates a 0x50-byte BezierSpline and appends to +0x70 or +0x98 by its last
     * argument; Update reads the request deque's size +0x448 and the front's status +0xa8 under the result section 0x3d74938;
     * the calculator publishes +0xac and then status 1 under it; the Linear constructor writes +0xa4 as one qword. */
    assert(!memcmp(image+0xeb4f97,"\x48\x8d\x05",3) && 0xeb4f9eu+*(int32_t*)(image+0xeb4f9a)==MOTION_SPLINE_VTABLE);
    assert(!memcmp(image+0xeb4fa1,"\x48\x83\xc1\x70",4) && !memcmp(image+0xeb4fab,"\x48\x8d\x8b\x98\x00\x00\x00",7));
    assert(!memcmp(image+0xeb5060,"\x48\x8d\x05",3) && 0xeb5067u+*(int32_t*)(image+0xeb5063)==MOTION_SPLINE_SEGMENT);
    assert(!memcmp(image+0xeb5067,"\x48\x89\x83\x58\x01\x00\x00",7) && !memcmp(image+0xeb50af,"\x48\x89\x83\xa8\x01\x00\x00",7));
    assert(!memcmp(image+0xebcb3c,"\xb9\x50\x00\x00\x00",5) && !memcmp(image+0xebcb85,"\x48\x8d\x05",3) && 0xebcb8cu+*(int32_t*)(image+0xebcb88)==MOTION_SPLINE_SEGMENT);
    assert(!memcmp(image+0xebcce8,"\xb9\x70\x00\x00\x00\xb8\x98\x00\x00\x00\x0f\x44\xc1",13));
    assert(!memcmp(image+0xeb74d9,"\x49\x39\xbe\x48\x04\x00\x00",7) && 0xeb7508u+*(int32_t*)(image+0xeb7504)==0x3d74938u);
    assert(!memcmp(image+0xeb750e,"\x48\x8d\x8e\xa8\x00\x00\x00",7));
    assert(!memcmp(image+0x1099786,"\xc6\x83\xac\x00\x00\x00\x01\xb8\x01\x00\x00\x00\x87\x83\xa8\x00\x00\x00",18));
    assert(!memcmp(image+0xeb1ac0,"\x48\xc7\x83\xa4\x00\x00\x00",7) && !memcmp(image+0xeb1ad2,"\x0f\x11\x83\xb0\x00\x00\x00",7));
    assert(*(uint64_t*)(image+MOTION_SPLINE_VTABLE+0x20)==0x140000000ULL+0xeb55a0u && *(uint64_t*)(image+MOTION_SPLINE_VTABLE+0x70)==0x140000000ULL+0xeb7230u);
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("motion watch native lookup pin, NavigationController vtable, order sites, SetTarget entries and spline layout verified");
}
