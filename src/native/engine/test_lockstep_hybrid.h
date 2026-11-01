/* native_gate_hybrid (lockstep_hybrid.h), slice H1: arming, the latch split on an owner and a non-owner,
 * the attention split, the H1 boundary, members, the owner's record, the apply side and its refusals,
 * the census split and the private slots. Synthetic objects only (the test image is zero). */
enum {HY_GALAXY,HY_CLUSTER,HY_S1,HY_S2,HY_Z1,HY_Z2,HY_A,HY_B,HY_N,HY_T,HY_CTRL,HY_SLOTS_BUF,HY_COUNT};
static unsigned char test_hy_object[HY_COUNT][0x300];
static void *test_hy_vtable[704];
static unsigned test_hy_refreshed;
static void *test_hy_refresh_log[32];
static unsigned char *test_hy(unsigned i) {return test_hy_object[i];}
static int test_hy_is_a(void *p,int klass) {
    if(klass==0x58) return p==test_hy(HY_S1) || p==test_hy(HY_S2);
    if(klass==0x6d) return p==test_hy(HY_Z1) || p==test_hy(HY_Z2);
    return 0;
}
static unsigned char *test_hy_sector_of(unsigned char *p) {
    for(unsigned depth=0;p && depth<17;depth++) {if(p==test_hy(HY_S1) || p==test_hy(HY_S2)) return p;p=*(unsigned char**)(p+0x70);}
    return NULL;
}
/* Slot 703 stand-in: 9 for an object in the viewer's sector, 3 for any other, 1 without a viewer. */
static int test_hy_native(void *object) {
    unsigned char *universe=*(unsigned char**)(view_page+VIEW_PAGE_UNIVERSE),*viewer=*(unsigned char**)(universe+0x230);
    if(!viewer) return 1;
    unsigned char *a=test_hy_sector_of(object),*b=test_hy_sector_of(viewer);
    return a && a==b?9:3;
}
static void test_hy_set_attention(void *object,int level) {
    assert(level==1);if(test_hy_refreshed<32) test_hy_refresh_log[test_hy_refreshed]=object;++test_hy_refreshed;
}
static int test_hy_logged(void *object) {
    for(unsigned i=0;i<test_hy_refreshed && i<32;i++) if(test_hy_refresh_log[i]==object) return 1;
    return 0;
}
static void *test_hy_find(void *map,uint64_t runtime,unsigned flags) {
    (void)map;(void)flags;
    for(unsigned i=0;i<HY_COUNT;i++) if(*(uint64_t*)(test_hy(i)+8)==runtime) return test_hy(i);
    return NULL;
}
static int test_hy_positional(const unsigned char *object) {return object==test_hy(HY_N) || object==test_hy(HY_A) || object==test_hy(HY_B);}
static void test_hy_parent(unsigned child,unsigned parent) {*(unsigned char**)(test_hy(child)+0x70)=parent<HY_COUNT?test_hy(parent):NULL;}
/* The world: galaxy > cluster > S1 > Z1 > A (slot 1); cluster > S2 > Z2 > B (slot 2), N (an NPC mover), T (a station). */
static void test_hy_world(void) {
    memset(test_hy_object,0,sizeof(test_hy_object));memset(test_hy_vtable,0,sizeof(test_hy_vtable));
    test_hy_vtable[703]=(void*)test_hy_native;test_hy_vtable[0x11e8/8]=(void*)test_hy_is_a;
    for(unsigned i=0;i<HY_COUNT;i++) {*(void***)test_hy(i)=test_hy_vtable;*(uint64_t*)(test_hy(i)+8)=0x4000+i;test_hy(i)[0xd1]=1;}
    test_hy_parent(HY_CLUSTER,HY_GALAXY);test_hy_parent(HY_S1,HY_CLUSTER);test_hy_parent(HY_S2,HY_CLUSTER);
    test_hy_parent(HY_Z1,HY_S1);test_hy_parent(HY_Z2,HY_S2);test_hy_parent(HY_A,HY_Z1);test_hy_parent(HY_B,HY_Z2);
    test_hy_parent(HY_N,HY_Z2);test_hy_parent(HY_T,HY_Z2);
    /* N moves: a NavigationController (vtable RVA HYB_NAV_KIND) whose current pathing slot holds its pose. */
    unsigned char *ctrl=test_hy(HY_CTRL),*slots=test_hy(HY_SLOTS_BUF);uintptr_t nav=(uintptr_t)(image+HYB_NAV_KIND);
    memcpy(ctrl,&nav,8);*(unsigned char**)(ctrl+0x30)=slots;*(unsigned char**)(test_hy(HY_N)+0x200)=ctrl;
    const float pose[24]={1000.5f,-20.25f,3.0f,0, 1,0,0,0, 0,1,0,0, 0,0,1,0, 10.0f,0,-2.5f,0, 0,0,0,0};
    memcpy(slots,pose,sizeof(pose));*(uint32_t*)(image+HYB_BANK)=0;
    *(void**)(image+HYB_MAP)=(void*)test_hy(HY_GALAXY); /* any non-null map */
}
static void test_hy_other_observer(uint64_t tick,const world_hash_item *item,unsigned char *object) {(void)tick;(void)item;(void)object;}
static void test_hy_reset_module(void) {
    hyb_armed=hyb_active=hyb_scanned=0;hyb_owned=hyb_local=hyb_poses=0;hyb_tick=hyb_started=0;
    memset(hyb_sector,0,sizeof(hyb_sector));memset(hyb_sector_runtime,0,sizeof(hyb_sector_runtime));memset(hyb_sector_key,0,sizeof(hyb_sector_key));
    hyb_view_index=-1;hyb_member_count=hyb_next_count=0;hyb_failures=0;hyb_why="none";
    hyb_eval_inside=hyb_eval_outside=0;hyb_rebuilds=hyb_entered=hyb_left=hyb_member_overflow=0;
    hyb_built=hyb_proposed=hyb_squeezed=hyb_bytes_sum=hyb_bytes_max=hyb_entries_out=hyb_pose_misses=0;hyb_cursor_key=0;
    hyb_record_tick=hyb_proposal_tick=UINT64_MAX;hyb_applied_tick=0;memset(hyb_last_state,0,sizeof(hyb_last_state));
    memset(hyb_records_in,0,sizeof(hyb_records_in));hyb_entries_in=hyb_missing=0;
    hyb_proxy_hits=hyb_proxy_unknown=hyb_proxy_parent=hyb_proxy_unread=hyb_err_n=0;hyb_err_sum=hyb_err_max=0;
    hyb_cen_tick=UINT64_MAX;hyb_cen_captures=0;world_hash_after_item=NULL;
}
/* One client's latch at tick: hybrid owners 2 (mask 2) armed at tick 5 on client 'local'. */
static void test_hy_client(unsigned local) {
    test_hy_reset_module();test_vp_reset();
    view_mode=VIEW_PLAYERS;view_installed=1;view_active=1;stats.issued=0;stats.slot=local;
    assert(hyb_arm(2,5,4) && hyb_armed && hyb_local==local && world_hash_after_item==hyb_census_item);
    pilot_slot_count=2;memset(pilot_slots,0,sizeof(pilot_slots));
    pilot_slots[0].resolved=1;pilot_slots[0].object=test_hy(HY_A);pilot_slots[0].runtime=*(uint64_t*)(test_hy(HY_A)+8);
    pilot_slots[1].resolved=1;pilot_slots[1].object=test_hy(HY_B);pilot_slots[1].runtime=*(uint64_t*)(test_hy(HY_B)+8);
    /* Before T: both slots are view points on every client. */
    stats.issued=4;assert(view_frame(4) && view_count==2 && !hyb_active && hyb_view_index==-1);
    assert(view_attention(test_hy(HY_N))==9 && view_attention(test_hy(HY_CLUSTER))==3);
    test_hy_refreshed=0;stats.issued=5;assert(view_frame(5) && hyb_active && hyb_started==5 && hyb_check());
    assert(hyb_sector[1]==test_hy(HY_S2) && !hyb_sector[0] && hyb_sector_key[1]==0x9000+HY_S2);
}
static void test_hybrid(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;unsigned saved_slot=stats.slot;
    unsigned saved_pilots=pilot_slot_count;int saved_trigger=view_trigger_mode,saved_ui_mode=view_ui_mode,saved_rows=view_rows_armed;
    world_identity_entry *prior_identities=world_identities;LONG64 prior_identity_count=world_identity_count;LONG prior_sealed=world_identity_sealed;
    unsigned prior_hash_count=world_hash_count,prior_created_count=world_hash_created_count;uint64_t prior_hash_tick=world_hash_tick,prior_scans=world_hash_scans;
    view_trigger_mode=VIEW_TRIGGER_OFF;view_ui_mode=VIEW_UI_OFF;view_rows_armed=0;
    if(!view_page) view_page=view_page_near();assert(view_page);
    if(!view_lock_ready) {InitializeCriticalSection(&view_lock);view_lock_ready=1;}
    ui_thread=GetCurrentThreadId();stats.active=1;
    world_identities=VirtualAlloc(NULL,sizeof(world_identity_entry)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);world_identity_count=0;world_identity_sealed=0;
    test_hy_world();hyb_find_override=test_hy_find;view_set_attention_override=test_hy_set_attention;
    {hyb_pose pose;assert(!hyb_read_pose(test_hy(HY_N),&pose));} /* the zero image's class bitmap is unknown: no mover */
    hyb_positional_override=test_hy_positional;
    for(unsigned i=0;i<HY_COUNT;i++) if(i!=HY_CTRL && i!=HY_SLOTS_BUF) assert(world_identity_record(0x4000+i,0x9000+i));
    /* Arming: argument ranges, view points 'players' installed, before the first admitted frame, on the MoveWorker. */
    test_hy_reset_module();test_vp_reset();stats.slot=1;stats.issued=0;
    view_mode=VIEW_PLAYERS;assert(!hyb_arm(2,5,4));view_installed=1;
    view_mode=VIEW_OBSERVE;assert(!hyb_arm(2,5,4));view_mode=VIEW_PLAYERS;
    stats.issued=3;assert(!hyb_arm(2,5,4));stats.issued=0;
    ui_thread=GetCurrentThreadId()+1;assert(!hyb_arm(2,5,4));ui_thread=GetCurrentThreadId();
    assert(!hyb_arm(0,5,4) && !hyb_arm(16,5,4) && !hyb_arm(2,1,4) && !hyb_arm(2,5,6) && !hyb_arm(1.5,5,4) && !hyb_arm(NAN,5,4) && !hyb_armed);
    stats.slot=0;assert(!hyb_arm(2,5,4));stats.slot=1;
    world_hash_after_item=test_hy_other_observer;assert(!hyb_arm(2,5,4));world_hash_after_item=NULL; /* another observer holds the capture */
    assert(hyb_arm(2,5,4) && hyb_arm(2,5,4) && !hyb_arm(2,6,4)); /* an identical rerun after a reload only */

    /* Non-owner (client 1): slot 2 leaves the latch at T; slot 2's sector evaluates without it, as does the rest. */
    test_hy_client(1);
    assert(view_count==1 && view_points[0]==test_hy(HY_A) && hyb_view_index==-1);
    assert(test_hy_logged(test_hy(HY_B)) && test_hy_logged(test_hy(HY_A))); /* the former and the new view points' chains */
    assert(view_attention(test_hy(HY_N))==3 && view_attention(test_hy(HY_A))==9 && view_attention(test_hy(HY_CLUSTER))==3);
    assert(!hyb_eval_inside && !hyb_eval_outside && !hyb_owner() && hyb_slot_private(2) && !hyb_slot_private(1));
    /* Owner (client 2): slot 2 stays, counts inside its sector only; the latch at T recomputes its chain too. */
    test_hy_client(2);
    assert(view_count==2 && view_points[1]==test_hy(HY_B) && hyb_view_index==1 && hyb_owner());
    assert(test_hy_logged(test_hy(HY_B)) && test_hy_logged(test_hy(HY_S2)) && test_hy_logged(test_hy(HY_CLUSTER)));
    hyb_eval_inside=hyb_eval_outside=0;
    assert(view_attention(test_hy(HY_N))==9 && view_attention(test_hy(HY_T))==9 && view_attention(test_hy(HY_S2))==9); /* inside: B counts */
    assert(view_attention(test_hy(HY_CLUSTER))==3 && view_attention(test_hy(HY_A))==9 && view_attention(test_hy(HY_GALAXY))==3);
    assert(hyb_eval_inside==3 && hyb_eval_outside==3);
    /* Only the owner's own point is split: with slot 1 masked too the outside has no viewer at all. */
    view_count=1;view_points[0]=test_hy(HY_B);hyb_view_index=0;
    assert(view_attention(test_hy(HY_CLUSTER))==1 && view_attention(test_hy(HY_N))==9);
    stats.issued=6;assert(view_frame(6) && view_count==2 && hyb_view_index==1 && hyb_check());

    /* Members: the identities below S2 (B, N, T, Z2 and S2 itself); N is the only mover. */
    stats.issued=6;assert(hyb_frame(6) && hyb_scanned && hyb_member_count==5 && hyb_member_find(0x9000+HY_N)->mover && !hyb_member_find(0x9000+HY_T)->mover);
    assert(!hyb_member_find(0x9000+HY_A) && hyb_movers_own==1 && hyb_movers_live==1 && hyb_record_tick==6 && hyb_record_entries==1);
    assert(!hyb_frame(7) && !hyb_check() && !strcmp(hyb_why,"frame_guard"));hyb_failures=0;hyb_why="none"; /* not the admitted tick */
    /* The record: header and N's entry (key, parent tag, mm, identity rotation, quarter m/s). */
    char hex[2*HYB_RECORD_MAX+1],rec6[2*HYB_RECORD_MAX+1];int n=hyb_proposal_text(6,2*240,hex,sizeof(hex));
    assert(n==2*(HYB_HEADER+HYB_ENTRY) && hyb_proposed==1 && hyb_bytes_max==HYB_HEADER+HYB_ENTRY+2);memcpy(rec6,hex,(size_t)n+1);
    unsigned char rec[HYB_RECORD_MAX];for(int i=0;i<n/2;i++) assert(hyb_hex_byte(hex+2*i,&rec[i]));
    uint32_t state,tag;uint64_t key;int32_t mm[3];int16_t q[3],v[3];uint16_t movers;
    memcpy(&state,rec+2,4);memcpy(&key,rec+6,8);memcpy(&movers,rec+14,2);
    assert(rec[0]==1 && rec[1]==0 && state==6 && key==0x9000+HY_S2 && movers==1 && rec[24]==1);
    memcpy(&key,rec+25,8);memcpy(&tag,rec+33,4);memcpy(mm,rec+37,12);memcpy(q,rec+49,6);memcpy(v,rec+55,6);
    assert(key==0x9000+HY_N && tag==(uint32_t)lockstep_mix(0x9000+HY_Z2) && mm[0]==1000500 && mm[1]==-20250 && mm[2]==3000);
    assert(!q[0] && !q[1] && !q[2] && v[0]==40 && !v[1] && v[2]==-10 && hyb_cursor_key==0x9000+HY_N);
    assert(hyb_proposal_text(6,2*240,hex,sizeof(hex))==n && hyb_proposed==1); /* idempotent per tick */
    /* Squeezed to the blob's room: the header alone, then nothing below it. */
    stats.issued=7;assert(hyb_frame(7));assert(hyb_proposal_text(7,2*(HYB_HEADER+10),hex,sizeof(hex))==2*HYB_HEADER && hyb_squeezed==1);
    stats.issued=8;assert(hyb_frame(8) && !hyb_proposal_text(8,2*(HYB_HEADER-1),hex,sizeof(hex)) && hyb_squeezed==2);
    /* The owner applies its own record (no proxy) and refuses a second record of the same state tick. */
    char receipt[128],owner8[128],payloads[2*HYB_RECORD_MAX+8];
    memcpy(hex,rec6,sizeof(rec6));n=(int)strlen(hex);snprintf(payloads,sizeof(payloads),",%s",hex);
    assert(hyb_apply_text(8,2,payloads,receipt,sizeof(receipt)) && !strncmp(receipt,"HYB_1|8|1|1|0|",14) && hyb_records_in[1]==1 && !hyb_proxy_hits);
    memcpy(owner8,receipt,sizeof(receipt));
    assert(hyb_apply_text(9,2,payloads,receipt,sizeof(receipt)) && !strcmp(receipt,"HYB_1|9|refused|record_tick") && !strcmp(hyb_why,"record_tick"));

    /* Non-owner apply: the proxy keeps the published pose, the error against the local copy is 0 m. */
    test_hy_client(1);stats.issued=6;assert(hyb_frame(6) && hyb_member_count==5 && hyb_record_tick==UINT64_MAX); /* no record of its own */
    assert(!hyb_proposal_text(6,2*240,hex,sizeof(hex)));
    char first[128];assert(hyb_apply_text(8,2,payloads,first,sizeof(first)) && hyb_proxy_hits==1 && hyb_err_n==1 && hyb_err_max<0.001);
    assert(hyb_member_find(0x9000+HY_N)->proxied && hyb_member_find(0x9000+HY_N)->published==6);
    assert(!strcmp(first,owner8)); /* the receipt is the same on the owner and the other clients */
    /* Missing records count after the grace, deterministically (receipt); an empty payload is no record. */
    assert(hyb_apply_text(5+HYB_GRACE+2,2,",",receipt,sizeof(receipt)) && hyb_missing==1 && strstr(receipt,"|0|0|1|"));
    /* Refusals stop every client alike: a record from a slot that owns nothing, of another sector, of another shape. */
    snprintf(payloads,sizeof(payloads),"%s,",hex);
    assert(hyb_apply_text(30,2,payloads,receipt,sizeof(receipt)) && !strcmp(receipt,"HYB_1|30|refused|record_slot"));hyb_failures=0;
    {   char bad[2*HYB_RECORD_MAX+8];memcpy(bad,hex,(size_t)n+1);bad[12]='f'; /* sector key byte */
        snprintf(payloads,sizeof(payloads),",%s",bad);
        assert(hyb_apply_text(31,2,payloads,receipt,sizeof(receipt)) && !strcmp(receipt,"HYB_1|31|refused|record_sector"));hyb_failures=0;
        memcpy(bad,hex,(size_t)n+1);bad[49]='2'; /* entries 2 with one entry's bytes */
        snprintf(payloads,sizeof(payloads),",%s",bad);
        assert(hyb_apply_text(32,2,payloads,receipt,sizeof(receipt)) && !strcmp(receipt,"HYB_1|32|refused|record_shape"));hyb_failures=0;
        snprintf(payloads,sizeof(payloads),",%s0",hex);
        assert(hyb_apply_text(33,2,payloads,receipt,sizeof(receipt)) && !strcmp(receipt,"HYB_1|33|refused|record_shape"));hyb_failures=0;
        assert(!hyb_apply_text(34,2,",zz",receipt,sizeof(receipt)) && !hyb_apply_text(35,3,",",receipt,sizeof(receipt))); /* malformed arguments */
        assert(!hyb_apply_text(20,2,",",receipt,sizeof(receipt))); /* ticks only increase */
    }

    /* Census: outside items into the outside roots exactly as the capture aggregates them, inside items apart;
     * an absent item is inside when the member table says so; the table is rebuilt from the capture. */
    {   world_hash_item items[5];unsigned char *objects[5]={test_hy(HY_A),test_hy(HY_N),NULL,test_hy(HY_CLUSTER),NULL};
        const uint64_t keys[5]={0x9000+HY_A,0x9000+HY_N,0x9000+HY_T,0x9000+HY_CLUSTER,0x9100};
        lockstep_sha256_ctx expect[4];for(unsigned d=0;d<4;d++) lockstep_sha256_init(&expect[d]);
        for(unsigned i=0;i<5;i++) {
            memset(&items[i],0,sizeof(items[i]));items[i].saved=keys[i];items[i].runtime=objects[i]?*(uint64_t*)(objects[i]+8):0;
            for(unsigned d=0;d<4;d++) memset(items[i].hash[d],(int)(16*i+d),32);
            hyb_census_item(60,&items[i],objects[i]);
            if(i==0 || i==3 || i==4) for(unsigned d=0;d<4;d++) {lockstep_sha256_update(&expect[d],&keys[i],8);lockstep_sha256_update(&expect[d],items[i].hash[d],32);}
        }
        assert(hyb_cen_outside==3 && hyb_cen_inside==2 && hyb_cen_absent==1);
        world_hash_tick=60;world_hash_scans=1;world_hash_count=5;world_hash_created_count=0;
        assert(hyb_census_text(60) && !hyb_census_text(60)); /* once per capture */
        char roots[65];unsigned char raw[32];lockstep_sha256_final(&expect[0],raw);hex32(raw,roots);
        assert(strstr(hyb_cen_state,"HYBRID_OUTSIDE_1:60:3:") && strstr(hyb_cen_state,roots));
        assert(strstr(hyb_cen_summary,"tick=60 outside=3 inside=2 absent_inside=1 members=1 movers=1 entered=0 left=4"));
        assert(hyb_member_count==1 && hyb_member_find(0x9000+HY_N)->proxied && hyb_cen_captures==1); /* the proxy survives the rebuild */
        world_hash_count=6;hyb_census_item(61,&items[0],objects[0]);world_hash_tick=61;
        assert(!hyb_census_text(61) && !strcmp(hyb_why,"census_count"));hyb_failures=0;hyb_why="none";
    }

    /* The H1 boundary at every latch: the owner leaves, another player enters, the owner is gone, two owners share. */
    test_hy_client(2);test_hy_parent(HY_B,HY_Z1);stats.issued=6;assert(view_frame(6) && !hyb_check() && !strcmp(hyb_why,"owner_left"));
    test_hy_parent(HY_B,HY_Z2);
    test_hy_client(1);test_hy_parent(HY_A,HY_Z2);stats.issued=6;assert(view_frame(6) && !hyb_check() && !strcmp(hyb_why,"other_entered"));
    test_hy_parent(HY_A,HY_Z1);
    test_hy_client(1);pilot_slots[1].resolved=0;stats.issued=6;assert(view_frame(6) && !hyb_check() && !strcmp(hyb_why,"owner_lost"));
    test_hy_reset_module();test_vp_reset();test_hy_parent(HY_A,HY_Z2);stats.slot=1;stats.issued=0;
    view_mode=VIEW_PLAYERS;view_installed=1;view_active=1;
    assert(hyb_arm(3,5,4));pilot_slots[1].resolved=1;stats.issued=5;
    assert(view_frame(5) && !hyb_active && !hyb_check() && !strcmp(hyb_why,"shared_sector") && view_count==2); /* the split never starts */
    test_hy_parent(HY_A,HY_Z1);
    char status[1536];assert(hyb_status(status,sizeof(status))>0 && strstr(status,"armed=1 active=0 owners=3 local=1 tick=5") && strstr(status,"why=shared_sector"));

    test_hy_reset_module();hyb_find_override=NULL;hyb_positional_override=NULL;view_set_attention_override=NULL;*(void**)(image+HYB_MAP)=NULL;
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=prior_identities;world_identity_count=prior_identity_count;world_identity_sealed=prior_sealed;
    world_hash_count=prior_hash_count;world_hash_created_count=prior_created_count;world_hash_tick=prior_hash_tick;world_hash_scans=prior_scans;
    pilot_slot_count=saved_pilots;memset(pilot_slots,0,sizeof(pilot_slots));view_active=0;test_vp_reset();
    view_trigger_mode=saved_trigger;view_ui_mode=saved_ui_mode;view_rows_armed=saved_rows;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;stats.slot=saved_slot;
    printf("hybrid sectors tests PASS\n");
}
