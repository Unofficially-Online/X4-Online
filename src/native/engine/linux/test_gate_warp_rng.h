/* Warps on a keyed word and the warp watch (gate_warp_rng.h): the shared dispatch against a stand-in on the thread's
 * word and game time ('split' keyed and restored, 'observe' native, the watch row of a listed listener), arming
 * refusals; on the real executable the pins, the vtable slot as found, swapped to wr_dispatch and back. Included by
 * test_gate_linux.c. */
static uint64_t twr_seen;static unsigned twr_calls,twr_draws;static double twr_time;
static uint64_t twr_step(uint64_t s) {s=s*0x5851f42d4c957f2dULL+0x14057b7ef767814fULL;return (s>>30)|(s<<34);}
static void twr_original(void *event,void *listener,void *arg,double time) {
    (void)event;(void)listener;(void)arg;++twr_calls;twr_time=time;
    uint64_t *word=gate_tls_rng(gate_tls_self());twr_seen=*word;for(unsigned i=0;i<twr_draws;i++) *word=twr_step(*word);
}
static unsigned twr_ctor_calls,twr_ctor_flags;static void *twr_ctor_args[4];
static void *twr_ctor(void *event,void *space,const void *transform,void *record,unsigned a,unsigned b) {
    ++twr_ctor_calls;twr_ctor_args[0]=event;twr_ctor_args[1]=space;twr_ctor_args[2]=(void*)transform;twr_ctor_args[3]=record;
    twr_ctor_flags=(a&1)|((b&1)<<0);return event;
}
static void test_warp_rng_linux(void) {
    reset_session();DWORD self=GetCurrentThreadId();
    world_identities=calloc(WORLD_IDENTITY_CAPACITY,sizeof(*world_identities));assert(world_identities);
    world_identity_count=0;world_identity_sealed=0;world_identity_installed=1;
    assert(world_identity_record(0x7a0001,0xa5d7a) && world_identity_record(0x7a0100,0x124));
    wr_mode=WR_OFF;wr_installed=wr_restored=0;wr_active=0;wr_calls=wr_failures=wr_rows=wr_watches=0;
    wr_admitted=wr_keyed=wr_unmapped=wr_foreign=wr_outside=wr_full=wr_drew=wr_draws=wr_unknown=0;wr_tick=UINT64_MAX;
    memset(wr_slots,0,sizeof(wr_slots));wr_original_override=twr_original;
    static unsigned char ship[0x40],event[0x40],record[0x40],transform[0x40],pointed[16];
    uint64_t id=0x7a0001;memcpy(ship+8,&id,8);id=0x7a0100;memcpy(event+0x18,&id,8);
    void *p=record;memcpy(event+0x28,&p,8);record[0]=1;p=transform;memcpy(event+0x20,&p,8);
    for(unsigned i=0;i<0x40;i++) transform[i]=(unsigned char)i;
    for(unsigned i=0;i<16;i++) pointed[i]=(unsigned char)(0xf0+i);
    uint64_t *word=gate_tls_rng(gate_tls_self());double *now=gate_tls_time(gate_tls_self());double t=1862.5;memcpy(now,&t,8);
    stats.active=1;boot_mode=1;boot_phase=3;stats.issued=1862;ui_thread=self;twr_draws=2;
    /* Inactive: native, outside. */
    *word=0x1234;wr_dispatch(event,ship,ship,2.5);assert(twr_calls==1 && twr_seen==0x1234 && wr_outside==1 && !wr_rows && twr_time==2.5);
    /* Split: keyed by (tick, key, ordinal), the thread's word restored, the row's game time from fs:-0x2728. */
    wr_mode=WR_SPLIT;wr_active=1;*word=0x1234;wr_dispatch(event,ship,ship,2.5);
    uint64_t seed=lockstep_seed(WR_DOMAIN,1862,0xa5d7a,1,0,WORLD_KEY_SAVED);
    assert(twr_seen==seed && *word==0x1234 && wr_keyed==1 && wr_drew==1 && wr_draws==2 && wr_rows==1);
    char row[1280];assert(wr_row(0,row,sizeof(row))>0 && strstr(row,"index=0 tick=1862 key=s:00000000000a5d7a ordinal=1 space=0000000000000124 flag=1 ") &&
        strstr(row," time=4004000000000000 now=409d1a0000000000 site=0 thread=none posted=0 zone=00000000007a0100"));
    /* Observe with the watch: the listed listener's transform, record and pointed bytes first, then the warp row. */
    wr_mode=WR_OBSERVE;st_ids[0]=0xa5d7a;st_id_count=1;p=pointed;memcpy(record+0x30,&p,8);stats.issued=1863;*word=0x77;
    LONG64 before=wr_rows;wr_dispatch(event,ship,ship,2.5);assert(twr_seen==0x77 && *word==twr_step(twr_step(0x77)) && wr_rows==before+2 && wr_watches==1);
    char t_hex[0x81],r_hex[0x81],p_hex[0x21],f_hex[2*WR_FRAME_BYTES+1],expect[1152];wr_hex(t_hex,transform,0x40);wr_hex(r_hex,record,0x40);wr_hex(p_hex,pointed,16);
    snprintf(expect,sizeof(expect),"index=%lld tick=1863 kind=watch key=s:00000000000a5d7a ordinal=1 space=0000000000000124 zone=00000000007a0100 "
        "transform=%s record=%s pointed=%s frame=-",(long long)before,t_hex,r_hex,p_hex);
    assert(wr_row((uint64_t)before,row,sizeof(row))>0 && !strcmp(row,expect));
    assert(wr_row((uint64_t)before+1,row,sizeof(row))>0 && strstr(row," keyed=0 natural=0000000000000077 word=0000000000000077 draws=2 ") && strstr(row," direction=1 "));
    /* The poster's redirected constructor call: the WR_FRAME_BYTES below its transform noted, the constructor called with
     * every argument; the watched dispatch takes the frame once. */
    {static unsigned char stack[WR_FRAME_BYTES+0x40];for(unsigned i=0;i<sizeof(stack);i++) stack[i]=(unsigned char)(0x30+i);
     static unsigned char space[0x40];lxwr_ctor_override=twr_ctor;wr_frame_ok=1;LONG64 framed=wr_framed;
     assert(lxwr_frame_ctor(event,space,stack+WR_FRAME_BYTES,NULL,0,1)==event && twr_ctor_calls==1 && twr_ctor_args[0]==event &&
            twr_ctor_args[1]==space && twr_ctor_args[2]==stack+WR_FRAME_BYTES && !twr_ctor_args[3] && twr_ctor_flags==1 && wr_framed==framed+1);
     before=wr_rows;stats.issued=1864;wr_dispatch(event,ship,ship,2.5);wr_hex(f_hex,stack,WR_FRAME_BYTES);
     char want[0x1a0];snprintf(want,sizeof(want)," frame=%s",f_hex);
     assert(wr_row((uint64_t)before,row,sizeof(row))>0 && strstr(row,want) && strlen(row)<1280);
     before=wr_rows;wr_dispatch(event,ship,ship,2.5);assert(wr_row((uint64_t)before,row,sizeof(row))>0 && strstr(row," frame=-"));
     wr_frame_ok=0;lxwr_frame_ctor(event,space,stack+WR_FRAME_BYTES,NULL,0,1);assert(wr_framed==framed+1 && twr_ctor_calls==2); /* unpinned: no note */
     lxwr_ctor_override=NULL;}
    st_id_count=0;before=wr_rows;wr_dispatch(event,ship,ship,2.5);assert(wr_rows==before+1); /* not listed: no watch row */
    /* Another thread: native, foreign. */
    ui_thread=self+1;wr_dispatch(event,ship,ship,2.5);assert(wr_foreign==1);ui_thread=self;
    /* Arming: a mode, before the first admitted frame, on the MoveWorker, the identities; the synthetic image's pins are
     * its own hashes, so the slot (zero there, not Dispatch) refuses. */
    wr_active=0;wr_mode=WR_OFF;stats.issued=0;boot_phase=0;
    assert(!wr_arm(WR_OFF) && !wr_arm(7));stats.issued=1;assert(!wr_arm(WR_SPLIT));stats.issued=0;
    ui_thread=self+1;assert(!wr_arm(WR_SPLIT));ui_thread=self;
    world_identity_installed=0;assert(!wr_arm(WR_SPLIT) && !strcmp(wr_stage,"needs"));world_identity_installed=1;
    assert(!wr_arm(WR_OBSERVE) && !strcmp(wr_stage,"entry") && !wr_installed && !wr_active);
    char status[640];assert(wr_status(status,sizeof(status))>0 && strstr(status," keyed=1 ") && strstr(status," framed=1 frame_pin=0"));
    wr_original_override=NULL;memset(record,0,sizeof(record));memset(wr_ring,0,sizeof(wr_ring));wr_rows=0;
    world_identity_installed=0;world_identity_count=0;free(world_identities);world_identities=NULL;
    printf("warp RNG split/observe/watch/arming PASS\n");
}
/* The real executable: pins, the slot holding Dispatch, swapped to wr_dispatch and back. */
static void test_warp_rng_elf(void) {
    assert(wr_pins());
    void *raw=NULL;memcpy(&raw,LX(LX_WR_ENTRY_VA),8);assert(raw==(void*)(uintptr_t)LX_WR_DISPATCH_VA);
    uintptr_t page=(uintptr_t)LX(LX_WR_ENTRY_VA)&~(uintptr_t)(GATE_PAGE-1);
    assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE));put64(LX(LX_WR_ENTRY_VA),(uint64_t)(uintptr_t)LX(LX_WR_DISPATCH_VA));
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();world_identity_installed=1;
    wr_installed=0;wr_active=0;
    /* The poster's frame: its call of the constructor and the product pinned, the call as found. */
    assert(lxwr_frame_pins() && lxwr_frame_state()==0 && ((LX_WR_FRAME_CALL_VA+1)&15u)<=12u);
    {int32_t d;memcpy(&d,LX(LX_WR_FRAME_CALL_VA+1),4);assert(*LX(LX_WR_FRAME_CALL_VA)==0xe8 && LX(LX_WR_FRAME_CALL_VA)+5+d==LX(LX_WR_CTOR_VA));}
    unsigned char line[16];memcpy(line,LX(LXWR_FRAME_BLOCK),16);
    assert(wr_arm(WR_OBSERVE) && wr_installed && wr_owned() && wr_check() && wr_arm(WR_OBSERVE) && !wr_arm(WR_SPLIT));
    assert(wr_frame_ok && lxwr_frame_installed && lxwr_frame_state()==1 && lxwr_frame_owned() && lxwr_frame_pins());
    void *now=NULL;memcpy(&now,LX(LX_WR_ENTRY_VA),8);assert(now==(void*)wr_dispatch);
    wr_restore();assert(!wr_installed && wr_restored);memcpy(&now,LX(LX_WR_ENTRY_VA),8);assert(now==LX(LX_WR_DISPATCH_VA));
    assert(!lxwr_frame_installed && !wr_frame_ok && lxwr_frame_state()==0 && !memcmp(line,LX(LXWR_FRAME_BLOCK),16));
    world_identity_installed=0;
    printf("warp RNG ELF PASS\n");
}
