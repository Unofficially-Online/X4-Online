/* native_gate_character_play (lockstep_character_play.h): the DF2 wharf patrol's
 * class race on a mock of slot 17 that draws from the thread word only when its
 * character is resident. 'observe' shows two clients parting on residency alone;
 * 'sync' loads first (the engine's synchronous load: queued with 0x21 under its
 * lock, the loader's semaphore waited for) and runs the play keyed, so both
 * clients end with the same frame word and the same draws. Pair ordinals per
 * tick, arguments passed on bit for bit, pass-through (other threads, boot
 * frames, not installed), fail-closed (load timeout, exception, unreadable
 * entry, no word), loads that end without the asset, rejected keys, the entry
 * swap, rows and status. Native image: pins, the vtable entry, the manager
 * object's initializer, slot 17's arguments to slot 15 and to the play, the two
 * GetAssetIntern calls, the two draws on TLS+0x318 and BodyClass's call sites. */
#define TEST_CP_CHARACTER 0x0000000500000123ULL
#define TEST_CP_MOTION 0x0000000600000456ULL
static __attribute__((aligned(16))) unsigned char test_cp_entries[2][SORDER_ENTRY_SIZE],test_cp_self[16];
static unsigned char *test_cp_manager;static uint64_t test_cp_limits[16];
static CRITICAL_SECTION test_cp_lock;static HANDLE test_cp_done,test_cp_thread;
static unsigned test_cp_queue_calls,test_cp_delay,test_cp_fill=1,test_cp_calls,test_cp_throw;
static uint64_t test_cp_queued,*test_cp_word; /* the word the mock draws from (TLS+0x318 natively) */
static const cplay_args test_cp_args={{0x44,0x101,0x7f00,0x1,0xff01,0xab,0xdeadbeef3f800000ULL,0x4059000000000000ULL,0xc000000000000000ULL}};
static DWORD WINAPI test_cp_loader(LPVOID unused) {
    (void)unused;Sleep(test_cp_delay);
    if(test_cp_fill) {void *asset=(void *)(uintptr_t)0x5a5a0;memcpy(test_cp_entries[test_cp_queued==TEST_CP_MOTION?1:0],&asset,8);}
    ReleaseSemaphore(test_cp_done,1,NULL);return 0;
}
static uint8_t test_cp_queue(void *manager,const uint64_t *key,uint32_t flags) {
    assert(manager==test_cp_manager && flags==SORDER_SYNC_FLAGS);
    assert((DWORD)(uintptr_t)test_cp_lock.OwningThread==GetCurrentThreadId()); /* queued under the engine's lock */
    ++test_cp_queue_calls;test_cp_queued=*key;
    if(test_cp_delay==UINT32_MAX) return 1; /* never finishes */
    test_cp_thread=CreateThread(NULL,0,test_cp_loader,NULL,0,NULL);assert(test_cp_thread);return 1;
}
static void test_cp_join(void) {if(test_cp_thread) {WaitForSingleObject(test_cp_thread,1000);CloseHandle(test_cp_thread);test_cp_thread=NULL;}}
/* Slot 17 as X4 runs it: slot 15 gets the character instance (NULL while the loader has not finished it: no play, no
 * draw); the play draws its start offset from the thread word (a missing motion is queued with a draw of its own). */
static uint64_t test_cp_original(void *self,uint64_t character,uint64_t motion,uint64_t a4,uint64_t a5,uint64_t a6,uint64_t a7,uint64_t a8,
                                 uint64_t a9,uint64_t a10,uint64_t a11,uint64_t a12) {
    const uint64_t got[9]={a4,a5,a6,a7,a8,a9,a10,a11,a12};
    assert(self==test_cp_self && !memcmp(got,test_cp_args.a,sizeof(got)));(void)character;(void)motion;
    ++test_cp_calls;
    void *instance=NULL;memcpy(&instance,test_cp_entries[0],8);
    if(!instance) return 0;
    *test_cp_word=render_rng_step(*test_cp_word);
    if(test_cp_throw) RaiseException(0xe0435031,0,0,NULL);
    return 1;
}
static uint64_t test_cp_call(const sorder_loader *loader,uint64_t *word,uint64_t character,uint64_t motion,unsigned timeout_ms) {
    test_cp_word=word;
    uint64_t result=cplay_execute(test_cp_original,loader,word,test_cp_self,character,motion,&test_cp_args,timeout_ms);
    test_cp_join();return result;
}
static int test_cp_call_throwing(const sorder_loader *loader,uint64_t *word) {
    int caught=0;test_cp_word=word;
    __try {cplay_execute(test_cp_original,loader,word,test_cp_self,TEST_CP_CHARACTER,TEST_CP_MOTION,&test_cp_args,1000);}
    __except(EXCEPTION_EXECUTE_HANDLER) {caught=1;}
    return caught;
}
static void test_cp_resident(int character,int motion) {
    void *a=character?(void *)(uintptr_t)0x1000:NULL,*b=motion?(void *)(uintptr_t)0x2000:NULL;
    memcpy(test_cp_entries[0],&a,8);memcpy(test_cp_entries[1],&b,8);
}
static void test_cp_reset(int mode) {
    cplay_installed=1;cplay_mode=mode;cplay_failures=cplay_rows=cplay_foreign=0;cplay_stage="armed";
    cplay_calls=cplay_frame_calls=cplay_native=cplay_resident=cplay_absent=cplay_invalid=cplay_loads=cplay_unloaded=0;
    cplay_load_us=cplay_load_max_us=cplay_keyed=cplay_declined=cplay_draws=cplay_uncounted=cplay_overflow=0;
    cplay_tick=UINT64_MAX;cplay_pair_count=0;test_cp_calls=test_cp_queue_calls=0;
}
static void test_character_play(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    stats.active=1;boot_phase=3;ui_thread=GetCurrentThreadId();stats.issued=22;
    InitializeCriticalSection(&test_cp_lock);test_cp_done=CreateSemaphoreW(NULL,0,1,NULL);assert(test_cp_done);
    test_cp_manager=VirtualAlloc(NULL,(SORDER_ENTRY_TABLE+16u)*24u,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(test_cp_manager);
    memset(test_cp_limits,0,sizeof(test_cp_limits));test_cp_limits[5]=test_cp_limits[6]=0x100000;
    uintptr_t table5=(uintptr_t)test_cp_entries[0]-0x123u*SORDER_ENTRY_SIZE,table6=(uintptr_t)test_cp_entries[1]-0x456u*SORDER_ENTRY_SIZE;
    memcpy(test_cp_manager+(SORDER_ENTRY_TABLE+5u)*24u,&table5,8);memcpy(test_cp_manager+(SORDER_ENTRY_TABLE+6u)*24u,&table6,8);
    const sorder_loader loader={test_cp_queue,test_cp_manager,&test_cp_lock,test_cp_done,(const unsigned char *)test_cp_limits};
    const uint64_t frame=0x2e818130e22bee84ULL; /* frame 22's word before the play (any value) */
    char text[400];

    /* 'observe': the race as the census saw it. Client A finds the character resident and draws; client C's loader is
     * not done: slot 17 returns 0, no draw. The two frame words part by one draw (classes A and B, or B and C). */
    test_cp_reset(CPLAY_OBSERVE);
    uint64_t word_a=frame,word_c=frame;
    test_cp_resident(1,1);assert(test_cp_call(&loader,&word_a,TEST_CP_CHARACTER,TEST_CP_MOTION,1000)==1);
    test_cp_resident(0,1);assert(test_cp_call(&loader,&word_c,TEST_CP_CHARACTER,TEST_CP_MOTION,1000)==0);
    assert(word_a==render_rng_step(frame) && word_c==frame && word_a!=word_c);
    assert(!test_cp_queue_calls && cplay_calls==2 && cplay_resident==3 && cplay_absent==1 && !cplay_keyed && cplay_declined==1 && cplay_draws==1);
    assert(cplay_row_text(0,text,sizeof(text)) && strstr(text,"tick=22 mode=observe ordinal=0 pair=0 character=0000000500000123 "
        "motion=0000000600000456 before=rr loaded=-- load_us=0 result=1 caller=2e818130e22bee84 seed=0000000000000000 ") && strstr(text," draws=1"));
    assert(cplay_row_text(1,text,sizeof(text)) && strstr(text," ordinal=1 pair=1 ") && strstr(text," before=ar ") && strstr(text," result=0 ") &&
           strstr(text," draws=0"));

    /* 'sync': C's character is loaded first, both plays run keyed; the frame words stay equal and the plays draw alike. */
    test_cp_reset(CPLAY_SYNC);stats.issued=22;word_a=word_c=frame;
    test_cp_resident(1,1);assert(test_cp_call(&loader,&word_a,TEST_CP_CHARACTER,TEST_CP_MOTION,1000)==1);
    char row_a[400];assert(cplay_row_text(0,row_a,sizeof(row_a)));
    cplay_tick=UINT64_MAX; /* C is another process: its own first call of the tick */
    test_cp_resident(0,1);test_cp_delay=5;test_cp_fill=1;
    assert(test_cp_call(&loader,&word_c,TEST_CP_CHARACTER,TEST_CP_MOTION,1000)==1);
    assert(word_a==frame && word_c==frame && test_cp_queue_calls==1 && test_cp_queued==TEST_CP_CHARACTER && cplay_loads==1 && test_cp_calls==2);
    const uint64_t seed=lockstep_seed(CPLAY_DOMAIN,22,TEST_CP_CHARACTER,TEST_CP_MOTION,0,0);
    assert(cplay_ring[0].seed==seed && cplay_ring[1].seed==seed && cplay_ring[0].exit==render_rng_step(seed) && cplay_ring[1].exit==cplay_ring[0].exit);
    assert(cplay_ring[0].before[0]==CPLAY_RESIDENT && cplay_ring[1].before[0]==CPLAY_ABSENT && cplay_ring[1].loaded[0]==1 && !cplay_ring[1].loaded[1]);
    assert(cplay_keyed==2 && cplay_draws==2 && !cplay_declined && !cplay_failures);
    assert(cplay_row_text(1,text,sizeof(text)) && strstr(text,"tick=22 mode=sync ordinal=0 pair=0 ") && strstr(text," before=ar loaded=l- ") &&
           strstr(text," result=1 caller=2e818130e22bee84 ") && strstr(text," draws=1"));
    assert(!strcmp(strstr(row_a," result="),strstr(text," result="))); /* the same play on both clients */
    /* A missing motion is loaded too (its queue would have drawn natively). */
    test_cp_resident(1,0);stats.issued=23;word_c=frame;test_cp_delay=1;
    assert(test_cp_call(&loader,&word_c,TEST_CP_CHARACTER,TEST_CP_MOTION,1000)==1);
    assert(word_c==frame && test_cp_queue_calls==2 && test_cp_queued==TEST_CP_MOTION && cplay_loads==2);
    /* The pair's ordinal: a second call with the same keys in the tick is another key; another motion counts on its own;
     * the next tick starts again. */
    test_cp_resident(1,1);
    assert(test_cp_call(&loader,&word_c,TEST_CP_CHARACTER,TEST_CP_MOTION,1000)==1 && cplay_ring[3].pair==1 && cplay_ring[3].ordinal==1);
    assert(cplay_ring[3].seed==lockstep_seed(CPLAY_DOMAIN,23,TEST_CP_CHARACTER,TEST_CP_MOTION,1,0) && cplay_ring[3].seed!=cplay_ring[2].seed);
    assert(test_cp_call(&loader,&word_c,TEST_CP_CHARACTER,0x0000000600200000ULL,1000)==1 && cplay_ring[4].pair==0 && cplay_ring[4].before[1]==CPLAY_INVALID);
    stats.issued=24;assert(test_cp_call(&loader,&word_c,TEST_CP_CHARACTER,TEST_CP_MOTION,1000)==1 && !cplay_ring[5].pair && !cplay_ring[5].ordinal);
    assert(word_c==frame && test_cp_queue_calls==2);
    /* A load that ends without the asset: counted, the original as X4 runs it (no play), no failure. */
    test_cp_resident(0,1);test_cp_fill=0;test_cp_delay=1;
    assert(test_cp_call(&loader,&word_c,TEST_CP_CHARACTER,TEST_CP_MOTION,1000)==0);
    assert(cplay_unloaded==1 && test_cp_queue_calls==3 && cplay_ring[6].loaded[0]==2 && !cplay_failures && word_c==frame);
    assert(cplay_row_text(6,text,sizeof(text)) && strstr(text," before=ar loaded=u- ") && strstr(text," draws=0"));
    test_cp_fill=1;
    /* Keys the manager rejects (type 0, beyond the table) and no manager: not queued, the original asks as usual. */
    test_cp_resident(1,1);unsigned queued=test_cp_queue_calls;
    test_cp_call(&loader,&word_c,0x0000000000000123ULL,TEST_CP_MOTION,1000);test_cp_call(&loader,&word_c,0x0000000500200000ULL,TEST_CP_MOTION,1000);
    sorder_loader unmanaged=loader;unmanaged.manager=NULL;test_cp_call(&unmanaged,&word_c,TEST_CP_CHARACTER,TEST_CP_MOTION,1000);
    assert(test_cp_queue_calls==queued && cplay_invalid==5 && !cplay_failures && word_c==frame);
    /* Pass-through: another thread, a boot frame, not installed: the original on the caller's word, no row. */
    LONG64 rows=cplay_rows;uint64_t word=frame;
    ui_thread=GetCurrentThreadId()+1;assert(test_cp_call(&loader,&word,TEST_CP_CHARACTER,TEST_CP_MOTION,1000)==1);ui_thread=GetCurrentThreadId();
    boot_phase=2;assert(test_cp_call(&loader,&word,TEST_CP_CHARACTER,TEST_CP_MOTION,1000)==1);boot_phase=3;
    cplay_installed=0;assert(test_cp_call(&loader,&word,TEST_CP_CHARACTER,TEST_CP_MOTION,1000)==1);cplay_installed=1;
    assert(cplay_rows==rows && cplay_foreign==1 && cplay_native==2 && word==render_rng_step(render_rng_step(render_rng_step(frame))));
    /* Fail closed: a load that never ends (the original then runs natively once, the tick check stops the gate). */
    test_cp_resident(0,1);test_cp_delay=UINT32_MAX;word=frame;rows=cplay_rows;
    assert(test_cp_call(&loader,&word,TEST_CP_CHARACTER,TEST_CP_MOTION,20)==0);
    assert(cplay_failures==1 && !strcmp(cplay_stage,"sync_timeout") && cplay_rows==rows && !cplay_check());
    test_cp_call(&loader,&word,TEST_CP_CHARACTER,TEST_CP_MOTION,20); /* failed: passes through, nothing queued */
    assert(test_cp_queue_calls==queued+1 && cplay_native==3);
    /* An exception through the keyed call: the caller's word comes back, the module fails. */
    test_cp_reset(CPLAY_SYNC);test_cp_resident(1,1);test_cp_throw=1;word=frame;
    assert(test_cp_call_throwing(&loader,&word) && word==frame && cplay_failures==1 && !strcmp(cplay_stage,"exception"));test_cp_throw=0;
    /* No thread word, an unreadable asset entry: fail closed, the original still runs. */
    uint64_t spare=frame;test_cp_reset(CPLAY_SYNC);test_cp_resident(1,1);test_cp_word=&spare;
    assert(cplay_execute(test_cp_original,&loader,NULL,test_cp_self,TEST_CP_CHARACTER,TEST_CP_MOTION,&test_cp_args,1000)==1);
    assert(cplay_failures==1 && !strcmp(cplay_stage,"word") && test_cp_calls==1 && !cplay_rows && spare==render_rng_step(frame));
    test_cp_reset(CPLAY_SYNC);uintptr_t bad=(uintptr_t)0x10-0x123u*SORDER_ENTRY_SIZE;memcpy(test_cp_manager+(SORDER_ENTRY_TABLE+5u)*24u,&bad,8);word=frame;
    assert(test_cp_call(&loader,&word,TEST_CP_CHARACTER,TEST_CP_MOTION,1000)==1 && cplay_failures==1 && !strcmp(cplay_stage,"asset_entry") && test_cp_calls==1);
    memcpy(test_cp_manager+(SORDER_ENTRY_TABLE+5u)*24u,&table5,8);
    /* Status. */
    test_cp_reset(CPLAY_SYNC);stats.issued=30;word=frame;test_cp_resident(1,1);test_cp_call(&loader,&word,TEST_CP_CHARACTER,TEST_CP_MOTION,1000);
    char status[1024];assert(cplay_status(status,sizeof(status)) && strstr(status,"mode=sync installed=1 frame=30 calls=1 frame_calls=1 ") &&
                            strstr(status," keyed=1 declined=0 draws=1 ") && strstr(status," stage=armed failures=0"));
    cplay_calls=cplay_native=cplay_resident=cplay_absent=cplay_invalid=cplay_loads=cplay_unloaded=UINT64_MAX;
    cplay_load_us=cplay_load_max_us=cplay_keyed=cplay_declined=cplay_draws=cplay_uncounted=cplay_overflow=UINT64_MAX;
    cplay_foreign=cplay_rows=cplay_failures=INT64_MAX;cplay_stage="asset_entry";
    int n=cplay_status(status,sizeof(status));assert(n>0 && n<(int)sizeof(status));
    test_cp_reset(CPLAY_SYNC);
    /* Rows evicted from the ring. */
    cplay_rows=CPLAY_RING+3;assert(cplay_row_text(1,text,sizeof(text)) && !strcmp(text,"index=1 evicted=1") && !cplay_row_text(CPLAY_RING+3,text,sizeof(text)));
    cplay_rows=0;
    CloseHandle(test_cp_done);DeleteCriticalSection(&test_cp_lock);VirtualFree(test_cp_manager,0,MEM_RELEASE);test_cp_manager=NULL;

    /* The entry on the zero image: arming refusals, the swap, ownership, the tick check, restore (a foreign value stays). */
    cplay_installed=0;cplay_mode=CPLAY_OFF;stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;
    void *native=image+CPLAY_PLAY;memcpy(image+CPLAY_SLOT,&native,8);void *vtable=image+CPLAY_VTABLE;memcpy(image+CPLAY_OBJECT,&vtable,8);
    assert(!cplay_arm(CPLAY_OFF) && !strcmp(cplay_stage,"guard") && !cplay_arm(CPLAY_SYNC+1));
    stats.issued=1;assert(!cplay_arm(CPLAY_SYNC) && !strcmp(cplay_stage,"guard"));stats.issued=0;
    boot_phase=2;assert(!cplay_arm(CPLAY_SYNC));boot_phase=0;boot_mode=0;assert(!cplay_arm(CPLAY_SYNC));boot_mode=1;
    ui_thread=GetCurrentThreadId()+1;assert(!cplay_arm(CPLAY_SYNC));ui_thread=GetCurrentThreadId();
    assert(!cplay_arm(CPLAY_SYNC) && !strcmp(cplay_stage,"pins") && !cplay_installed); /* not the X4 image */
    assert(cplay_object_ok() && cplay_swap(image+CPLAY_PLAY,(void *)cplay_play));cplay_installed=1;cplay_failures=0;cplay_mode=CPLAY_SYNC;
    /* A loading-UI reload reruns the arming: the same mode is accepted, another refused. */
    assert(cplay_owned() && cplay_arm(CPLAY_SYNC)==1 && cplay_arm(CPLAY_OBSERVE)==0 && *(void **)(image+CPLAY_SLOT)==(void *)cplay_play);
    assert(!cplay_check() && !strcmp(cplay_stage,"block_changed"));cplay_failures=0; /* the zero image has no pinned bytes */
    memset(image+CPLAY_OBJECT,0,8);assert(!cplay_object_ok());memcpy(image+CPLAY_OBJECT,&vtable,8);
    void *other=image+0x10;memcpy(image+CPLAY_SLOT,&other,8);assert(!cplay_owned() && !cplay_check() && !strcmp(cplay_stage,"slot_changed"));
    cplay_restore();assert(cplay_installed && *(void **)(image+CPLAY_SLOT)==other); /* a foreign value stays */
    void *ours=(void *)cplay_play;memcpy(image+CPLAY_SLOT,&ours,8);
    cplay_restore();assert(!cplay_installed && cplay_restored && *(void **)(image+CPLAY_SLOT)==(void *)(image+CPLAY_PLAY) && cplay_owned());
    memset(image+CPLAY_SLOT,0,8);memset(image+CPLAY_OBJECT,0,8);
    cplay_restored=0;cplay_failures=0;cplay_stage="idle";cplay_mode=CPLAY_OFF;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    printf("character play: observe/sync race, loads, keys, pass-through, fail-closed and entry swap tests PASS\n");
}
static void test_character_play_native_image(const char *path) {
    FILE *file=fopen(path,"rb");assert(file);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    assert(fread(&dos,1,sizeof(dos),file)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0 && dos.e_lfanew<4096);
    assert(!fseek(file,dos.e_lfanew,SEEK_SET) && fread(&nt,1,sizeof(nt),file)==sizeof(nt) && nt.OptionalHeader.SizeOfImage==0x6fa9000);
    assert(nt.OptionalHeader.ImageBase==0x140000000ULL);
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
    assert(cplay_pins_ok());
    /* The vtable: slot 15 gets the asset, slot 17 plays (preferred-base pointers in the file); the static initializer
     * 0x8f090 gives the manager object 0x31bf8d0 this vtable. */
    assert(*(uint64_t *)(image+CPLAY_VTABLE+15u*8u)==0x140000000ULL+CPLAY_LOOKUP && *(uint64_t *)(image+CPLAY_SLOT)==0x140000000ULL+CPLAY_PLAY);
    assert(!memcmp(image+0x8f090,"\x48\x8d\x05",3) && 0x8f097u+*(int32_t *)(image+0x8f093)==CPLAY_VTABLE);
    assert(!memcmp(image+0x8f09d,"\x48\x89\x05",3) && 0x8f0a4u+*(int32_t *)(image+0x8f0a0)==CPLAY_OBJECT);
    /* Slot 17: motion (r8) kept in rbx; slot 15 (vtable +0x78) asked for the character (rdx) with r8b=0 and flags 1;
     * the play 0x12a4ab0 then gets the instance and the motion. */
    assert(!memcmp(image+0x129fc8e,"\x49\x8b\xd8",3));
    assert(!memcmp(image+0x129fc94,"\xc7\x44\x24\x28\x01\x00\x00\x00\xc6\x44\x24\x20\x00\x45\x33\xc9\x4c\x8b\xc2\x48\x8d\x54\x24\x70\xff\x50\x78",27));
    assert(!memcmp(image+0x129fd29,"\x48\x8b\xd3\x48\x8b\xc8\xe8",7) && 0x129fd34u+*(int32_t *)(image+0x129fd30)==CPLAY_START);
    /* Slot 15: GetAssetIntern(*0x391bd78, r8, r9b, flags). */
    assert(!memcmp(image+0xe68976,"\x48\x8b\x0d",3) && 0xe6897du+*(int32_t *)(image+0xe68979)==SORDER_ASSET_MANAGER);
    assert(!memcmp(image+0xe68986,"\x49\x8b\xc0",3) && !memcmp(image+0xe68990,"\x48\x8b\xd0\xe8",4) && 0xe68998u+*(int32_t *)(image+0xe68994)==SORDER_ASSET);
    /* The play: GetAssetIntern(*0x391bd78, motion, 0, 1); a resident motion's start offset is FloatState on TLS+0x318
     * (0x12a4caf); a missing one goes to the queue 0x12a4d20, which draws FloatState on TLS+0x318 too (0x12a4e76). */
    assert(!memcmp(image+0x12a4acc,"\x48\x8b\xda",3) && !memcmp(image+0x12a4b7e,"\x41\xbe\x01\x00\x00\x00",6));
    assert(!memcmp(image+0x12a4c1a,"\x45\x8b\xce\x45\x33\xc0\x48\x8b\xd3\x48\x8b\x0d",12) && 0x12a4c2au+*(int32_t *)(image+0x12a4c26)==SORDER_ASSET_MANAGER);
    assert(image[0x12a4c2a]==0xe8 && 0x12a4c2fu+*(int32_t *)(image+0x12a4c2b)==SORDER_ASSET);
    assert(!memcmp(image+0x12a4c96,"\xb9\x18\x03\x00\x00\x65\x48\x8b\x14\x25\x58\x00\x00\x00\x48\x03\x0a",17));
    assert(image[0x12a4caf]==0xe8 && 0x12a4cb4u+*(int32_t *)(image+0x12a4cb0)==0x14ddc90u);
    assert(image[0x12a4ce0]==0xe8 && 0x12a4ce5u+*(int32_t *)(image+0x12a4ce1)==CPLAY_QUEUED);
    assert(!memcmp(image+0x12a4e62,"\x65\x48\x8b\x04\x25\x58\x00\x00\x00\xb9\x18\x03\x00\x00\x48\x8b\x38\x48\x03\xcf\xe8",21) &&
           0x12a4e7bu+*(int32_t *)(image+0x12a4e77)==0x14ddc90u);
    /* GetAssetIntern during play: r8b=0 with the loading flag clear queues (|0x40) and returns NULL. */
    assert(!memcmp(image+0x14bad02,"\x80\xbd\x60\x46\x10\x00\x00",7) && !memcmp(image+0x14bad10,"\x83\xcf\x40",3));
    /* BodyClass's calls of slot 17 on the manager object: 0x37b6b0 (twice, mov r10,[vtable+0x88]) and StartSequenceAuto. */
    assert(!memcmp(image+0x37bc69,"\x48\x8b\x05",3) && 0x37bc70u+*(int32_t *)(image+0x37bc6c)==CPLAY_OBJECT && !memcmp(image+0x37bc70,"\x4c\x8b\x90\x88\x00\x00\x00",7));
    assert(!memcmp(image+0x37bcce,"\x48\x8d\x0d",3) && 0x37bcd5u+*(int32_t *)(image+0x37bcd1)==CPLAY_OBJECT && !memcmp(image+0x37bcd5,"\x41\xff\xd2",3));
    assert(!memcmp(image+0x37bf59,"\x48\x8b\x05",3) && 0x37bf60u+*(int32_t *)(image+0x37bf5c)==CPLAY_OBJECT && !memcmp(image+0x37bf60,"\x4c\x8b\x90\x88\x00\x00\x00",7));
    assert(!memcmp(image+0x37e924,"\x48\x8d\x0d",3) && 0x37e92bu+*(int32_t *)(image+0x37e927)==CPLAY_OBJECT && !memcmp(image+0x37e92b,"\xff\x90\x88\x00\x00\x00",6));
    assert(!memcmp(image+0x37ef21,"\x48\x8d\x0d",3) && 0x37ef28u+*(int32_t *)(image+0x37ef24)==CPLAY_OBJECT && !memcmp(image+0x37ef28,"\xff\x90\x88\x00\x00\x00",6));
    for(unsigned i=0;i<sizeof(cplay_pins)/sizeof(*cplay_pins);i++) {
        unsigned at=cplay_pins[i].rva+cplay_pins[i].size/2;image[at]^=1;assert(!cplay_pins_ok());image[at]^=1;
    }
    assert(cplay_pins_ok());
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("character play native pins, vtable entry, manager object, asset lookups, draws and BodyClass call sites verified");
}
