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
    twr_ctor_flags=(a&1)|((b&1)<<1);return event;
}
/* twr_stub_go(event, space, transform, record, a, b): rbx and r13 from twr_stub_set (the poster's callee-saved ship
 * and gate), then a call of the emitted stub at twr_stub_entry, both restored after. */
static volatile uint64_t twr_stub_set[2] __attribute__((used)),twr_stub_entry __attribute__((used));
__asm__(".text\n.p2align 4\ntwr_stub_go:\n"
" push %rbx\n push %r13\n push %r12\n mov twr_stub_set(%rip),%rbx\n mov twr_stub_set+8(%rip),%r13\n"
" call *twr_stub_entry(%rip)\n pop %r12\n pop %r13\n pop %rbx\n ret\n");
extern void *twr_stub_go(void *event,void *space,const void *transform,void *record,unsigned a,unsigned b);
static unsigned twr_gtrt_calls,twr_gtrt_misaligned;static void *twr_gtrt_a,*twr_gtrt_b[2];static double twr_gtrt_time;
static void *twr_gtrt(void *a,void *out,double time,void *b,unsigned char flag) {
    (void)flag;twr_gtrt_a=a;twr_gtrt_b[twr_gtrt_calls<2?twr_gtrt_calls:1]=b;twr_gtrt_time=time;if((uintptr_t)out&15u) ++twr_gtrt_misaligned;
    memset(out,(int)(++twr_gtrt_calls),0x40);return out;
}
static void test_warp_rng_linux(void) {
    reset_session();DWORD self=GetCurrentThreadId();
    world_identities=calloc(WORLD_IDENTITY_CAPACITY,sizeof(*world_identities));assert(world_identities);
    world_identity_count=0;world_identity_sealed=0;world_identity_installed=1;
    assert(world_identity_record(0x7a0001,0xa5d7a) && world_identity_record(0x7a0100,0x124));
    wr_mode=WR_OFF;wr_installed=wr_restored=0;wr_active=0;wr_calls=wr_failures=wr_rows=wr_watches=0;
    wr_admitted=wr_keyed=wr_unmapped=wr_foreign=wr_outside=wr_full=wr_drew=wr_draws=wr_unknown=0;wr_tick=UINT64_MAX;
    memset(wr_slots,0,sizeof(wr_slots));wr_original_override=twr_original;
    static unsigned char ship[0x80],event[0x40],record[0x40],transform[0x40],pointed[16];
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
    char row[2048];assert(wr_row(0,row,sizeof(row))>0 && strstr(row,"index=0 tick=1862 key=s:00000000000a5d7a ordinal=1 space=0000000000000124 flag=1 ") &&
        strstr(row," time=4004000000000000 now=409d1a0000000000 site=0 thread=none posted=0 zone=00000000007a0100"));
    /* Observe with the watch: the listed listener's transform, record and pointed bytes first, then the warp row. */
    wr_mode=WR_OBSERVE;st_ids[0]=0xa5d7a;st_id_count=1;p=pointed;memcpy(record+0x30,&p,8);stats.issued=1863;*word=0x77;
    LONG64 before=wr_rows;wr_dispatch(event,ship,ship,2.5);assert(twr_seen==0x77 && *word==twr_step(twr_step(0x77)) && wr_rows==before+2 && wr_watches==1);
    char t_hex[0x81],r_hex[0x81],p_hex[0x21],f_hex[2*WR_FRAME_BYTES+1],expect[2048];wr_hex(t_hex,transform,0x40);wr_hex(r_hex,record,0x40);wr_hex(p_hex,pointed,16);
    snprintf(expect,sizeof(expect),"index=%lld tick=1863 kind=watch key=s:00000000000a5d7a ordinal=1 space=0000000000000124 zone=00000000007a0100 "
        "transform=%s record=%s pointed=%s frame=- own=- up=- parent=0000000000000000 grand=0000000000000000 rel=- mask=0 ids=0000000000000000,0000000000000000,0000000000000000,0000000000000000,0000000000000000,0000000000000000",(long long)before,t_hex,r_hex,p_hex);
    assert(wr_row((uint64_t)before,row,sizeof(row))>0 && !strcmp(row,expect));
    assert(wr_row((uint64_t)before+1,row,sizeof(row))>0 && strstr(row," keyed=0 natural=0000000000000077 word=0000000000000077 draws=2 ") && strstr(row," direction=1 "));
    /* The poster's redirected constructor call: the WR_FRAME_BYTES below its transform noted, the constructor called with
     * every argument; the watched dispatch takes the frame once. */
    {static unsigned char stack[WR_FRAME_BYTES+0x40];for(unsigned i=0;i<sizeof(stack);i++) stack[i]=(unsigned char)(0x30+i);
     static unsigned char space[0x40];lxwr_ctor_override=twr_ctor;wr_frame_ok=1;LONG64 framed=wr_framed;
     assert(lxwr_frame_ctor(event,space,stack+WR_FRAME_BYTES,NULL,0,1)==event && twr_ctor_calls==1 && twr_ctor_args[0]==event &&
            twr_ctor_args[1]==space && twr_ctor_args[2]==stack+WR_FRAME_BYTES && !twr_ctor_args[3] && twr_ctor_flags==2 && wr_framed==framed+1);
     before=wr_rows;stats.issued=1864;wr_dispatch(event,ship,ship,2.5);wr_hex(f_hex,stack,WR_FRAME_BYTES);
     char want[0x1a0];snprintf(want,sizeof(want)," frame=%s",f_hex);
     assert(wr_row((uint64_t)before,row,sizeof(row))>0 && strstr(row,want) && strlen(row)<2048);
     before=wr_rows;wr_dispatch(event,ship,ship,2.5);assert(wr_row((uint64_t)before,row,sizeof(row))>0 && strstr(row," frame=-"));
     wr_frame_ok=0;lxwr_frame_ctor(event,space,stack+WR_FRAME_BYTES,NULL,0,1);assert(wr_framed==framed+1 && twr_ctor_calls==2); /* unpinned: no note */
     lxwr_ctor_override=NULL;}
    /* 'observe' with GetTransformRelativeTo pinned: the listener against its parent (own) and grandparent (up) at the
     * event's time, into aligned memory. */
    {static unsigned char parent_obj[0x80],grand_obj[0x80];uint64_t pid=0x7a0300,gid=0x7a0400;memcpy(parent_obj+8,&pid,8);memcpy(grand_obj+8,&gid,8);
     void *pp=parent_obj,*gp=grand_obj;memcpy(ship+0x70,&pp,8);memcpy(parent_obj+0x70,&gp,8);
     wr_gtrt_override=twr_gtrt;twr_gtrt_calls=0;wr_gtrt_ok=1;before=wr_rows;stats.issued=1865;wr_dispatch(event,ship,ship,2.5);
     assert(twr_gtrt_calls==2 && twr_gtrt_b[0]==parent_obj && twr_gtrt_b[1]==grand_obj && twr_gtrt_a==ship && twr_gtrt_time==2.5 && !twr_gtrt_misaligned);
     unsigned char o1[0x40],o2[0x40];memset(o1,0x01,0x40);memset(o2,0x02,0x40);char h1[0x81],h2[0x81],want[0x140];wr_hex(h1,o1,0x40);wr_hex(h2,o2,0x40);
     snprintf(want,sizeof(want)," own=%s up=%s parent=00000000007a0300 grand=00000000007a0400",h1,h2);
     assert(wr_row((uint64_t)before,row,sizeof(row))>0 && strstr(row,want));
     wr_mode=WR_SPLIT;twr_gtrt_calls=0;wr_dispatch(event,ship,ship,2.5);assert(!twr_gtrt_calls);wr_mode=WR_OBSERVE;
     memset(ship+0x70,0,8);wr_gtrt_ok=0;wr_gtrt_override=NULL;}
    /* rel: the poster's ship and gate as the stub kept them, then with GetTransformRelativeTo pinned five positions at
     * the construction's game time (fs:-0x2728) into aligned memory; only under 'observe' and for the listed ship. */
    {static unsigned char ship_o[0x80],gate_o[0x80],sp[0x80],gp[0x80],sg[0x80],gg[0x80],stack[WR_FRAME_BYTES+0x40],space[0x40],data[16];
     unsigned char *objs[6]={ship_o,gate_o,sp,gp,sg,gg};for(unsigned i=0;i<6;i++) {uint64_t oid=i?0x7b0000+i:0x7a0001;memcpy(objs[i]+8,&oid,8);}
     void *q=sp;memcpy(ship_o+0x70,&q,8);q=gp;memcpy(gate_o+0x70,&q,8);q=sg;memcpy(sp+0x70,&q,8);q=gg;memcpy(gp+0x70,&q,8);
     void *regs[2]={ship_o,gate_o};memcpy(data,regs,16);wr_regs[0]=wr_regs[1]=NULL;
     lxwr_data=data;lxwr_ctor_override=twr_ctor;wr_frame_ok=1;wr_gtrt_ok=1;wr_gtrt_override=twr_gtrt;twr_gtrt_calls=0;twr_gtrt_misaligned=0;
     wr_mode=WR_SPLIT;lxwr_frame_ctor(event,space,stack+WR_FRAME_BYTES,NULL,0,1);assert(!twr_gtrt_calls && wr_regs[0]==ship_o && wr_regs[1]==gate_o);
     wr_mode=WR_OBSERVE;uint64_t oid=0x7a0100;memcpy(ship_o+8,&oid,8);lxwr_frame_ctor(event,space,stack+WR_FRAME_BYTES,NULL,0,1);assert(!twr_gtrt_calls); /* not listed */
     oid=0x7a0001;memcpy(ship_o+8,&oid,8);wr_gtrt_ok=0;lxwr_frame_ctor(event,space,stack+WR_FRAME_BYTES,NULL,0,1);assert(!twr_gtrt_calls);wr_gtrt_ok=1; /* unpinned */
     lxwr_frame_ctor(event,space,stack+WR_FRAME_BYTES,NULL,0,1);
     assert(twr_gtrt_calls==5 && !twr_gtrt_misaligned && twr_gtrt_b[0]==gate_o && twr_gtrt_b[1]==gg && twr_gtrt_a==gate_o && twr_gtrt_time==1862.5);
     before=wr_rows;stats.issued=1866;wr_dispatch(event,ship,ship,2.5);
     assert(wr_row((uint64_t)before,row,sizeof(row))>0 && strstr(row," mask=1f ids=00000000007a0001,00000000007b0001,00000000007b0002,00000000007b0003,00000000007b0004,00000000007b0005") &&
            strstr(row," rel=01010101") && strlen(row)<2048);
     lxwr_data=NULL;lxwr_ctor_override=NULL;wr_regs[0]=wr_regs[1]=NULL;wr_frame_ok=0;wr_gtrt_ok=0;wr_gtrt_override=NULL;}
    /* The emitted stub on its own pages near the code: rbx and r13 into the data page, then lxwr_frame_ctor with every
     * argument, its result returned; the registers the caller set are unchanged after. */
    {unsigned char *page=gate_near_page((const void*)test_warp_rng_linux),*data=page?gate_near_page(page):NULL;assert(page && data);
     memset(page,0xcc,GATE_PAGE);lxwr_stub_bytes(page,page,data,(void*)lxwr_frame_ctor);assert(!mprotect(page,GATE_PAGE,PROT_READ|PROT_EXEC));
     static unsigned char stack[WR_FRAME_BYTES+0x40],space[0x40];
     lxwr_data=data;lxwr_ctor_override=twr_ctor;wr_regs[0]=wr_regs[1]=NULL;twr_stub_set[0]=0x5a5a000011112222ULL;twr_stub_set[1]=0x6b6b000033334444ULL;
     twr_stub_entry=(uint64_t)(uintptr_t)page;unsigned calls=twr_ctor_calls;
     assert(twr_stub_go(event,space,stack+WR_FRAME_BYTES,record,1,0)==event && twr_ctor_calls==calls+1 && twr_ctor_args[0]==event && twr_ctor_args[1]==space &&
            twr_ctor_args[2]==stack+WR_FRAME_BYTES && twr_ctor_args[3]==record && twr_ctor_flags==1);
     uint64_t kept[2];memcpy(kept,data,16);assert(kept[0]==0x5a5a000011112222ULL && kept[1]==0x6b6b000033334444ULL);
     assert(wr_regs[0]==(void*)(uintptr_t)0x5a5a000011112222ULL && wr_regs[1]==(void*)(uintptr_t)0x6b6b000033334444ULL);
     lxwr_data=NULL;lxwr_ctor_override=NULL;wr_regs[0]=wr_regs[1]=NULL;munmap(page,GATE_PAGE);munmap(data,GATE_PAGE);}
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
    char status[640];assert(wr_status(status,sizeof(status))>0 && strstr(status," keyed=2 ") && strstr(status," framed=5 frame_pin=0") /* the own/up block's split dispatch keyed one more */);
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
    assert(wr_frame_ok && wr_gtrt_ok && lxwr_frame_installed && lxwr_frame_state()==1 && lxwr_frame_owned() && lxwr_frame_pins());
    void *now=NULL;memcpy(&now,LX(LX_WR_ENTRY_VA),8);assert(now==(void*)wr_dispatch);
    wr_restore();assert(!wr_installed && wr_restored);memcpy(&now,LX(LX_WR_ENTRY_VA),8);assert(now==LX(LX_WR_DISPATCH_VA));
    assert(!lxwr_frame_installed && !wr_frame_ok && !wr_gtrt_ok && lxwr_frame_state()==0 && !memcmp(line,LX(LXWR_FRAME_BLOCK),16));
    world_identity_installed=0;
    printf("warp RNG ELF PASS\n");
}
