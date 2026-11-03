/* Determinism fix, DEFAULT OFF in the bare gate (a lockstep session's default is
 * 'sync'): native_gate_character_play = 'sync' | 'observe'.
 *
 * DF2 wharf patrol (reports/LOCKSTEP-ON-FOOT-20261101.txt, section 15): the
 * frame word after frame 22 comes in three classes per process (A two draws
 * more than C, B one more), and the class decides the patrol's course. With the
 * draw watch on (lockstep_draw_watch.h, frame mode; df2w1..df2w4) all 8 clients
 * were class A, with identical rows: 151 frame-level draws in frame 22. Three of
 * them are a body starting an animation: NPC::Move 0x6bc420 -> BodyClass::Move
 * 0x380dd0 -> 0x37b6b0 -> slot 17 of the character manager (the static object
 * 0x31bf8d0, vtable SCharacterBaseClass 0x2c90ac8; entry 0x2c90b50 = 0x129fc80)
 * -> SCharacterInstance play 0x12a4ab0 -> Random::FloatState on TLS+0x318
 * (0x12a4caf, the start offset). Slot 17 first gets the character instance
 * through slot 15 0xe68970: AssetManager::GetAssetIntern(*0x391bd78, character,
 * 0, 1). During play that call never loads (lockstep_shape_order.h): a file the
 * loader thread has not finished is queued and NULL comes back, and slot 17
 * returns 0: no play, no draw, no animation event (0x37be06). The play asks
 * the same for the motion; a missing motion is queued with a draw of its own
 * (0x12a4d20, FloatState at 0x12a4e76). So the number of frame-level draws of
 * a frame depends on the loader thread's progress against the MoveWorker: the
 * class. BodyClass calls slot 17 at 0x37bcd5, 0x37bf60 (0x37b6b0) and from
 * StartSequenceAuto 0x37e6e0/0x37ec10 (0x37e92b, 0x37ef28); every call goes
 * through the one vtable entry. The watch makes the MoveWorker slower (an
 * exception per store to the word, about 2260 per frame): the loads finished
 * first on every watched client, all class A.
 *
 * 'sync': slot 17's vtable entry points at a wrapper (data only, no thunk). On
 * the MoveWorker in admitted frames an asset that is not resident (the
 * character, then the motion) is loaded through the engine's synchronous load
 * before the original runs (GetAssetIntern's r8b=1 path, bounded, as
 * shape_order 'sync' does: queue 0x14bb1d0 with 0x21 under the lock 0x6b60ec0,
 * wait on the loader's semaphore *0x6b60ee8), so the play happens the same way
 * on every client; and the original runs with TLS+0x318 keyed from (tick,
 * character, motion, the pair's ordinal in the tick), the caller's word put
 * back afterwards (also when an exception unwinds), so its draws never reach
 * the frame word. Other threads, boot frames and loading pass through.
 * 'observe' keeps the wrapper without loads and without the key: rows only
 * (draws= then counts the play's steps on the caller's word).
 * CHARACTER_PLAY status per tick, CHARACTER_PLAY_ROW per wrapped call: the
 * keys, residency before (r resident, a absent, i a key the manager rejects),
 * the loads (l loaded, u ended without the asset), the result, the words.
 * Pins: slot 17, slot 15, the play and its queue, GetAssetIntern, the load
 * queue and the loader's loop; the object's vtable and the entry are checked
 * every tick. Stop reason 188: the entry or the object no longer ours, a pin
 * change, a load that does not end within CPLAY_WAIT_MS, an unreadable asset
 * entry, no thread word, or an exception through a wrapped call. */
#define CPLAY_OBJECT 0x31bf8d0u
#define CPLAY_VTABLE 0x2c90ac8u
#define CPLAY_SLOT (CPLAY_VTABLE+17u*8u) /* 0x2c90b50 */
#define CPLAY_PLAY 0x129fc80u
#define CPLAY_LOOKUP 0xe68970u
#define CPLAY_START 0x12a4ab0u
#define CPLAY_QUEUED 0x12a4d20u
#define CPLAY_DOMAIN 0x58344c4348415231ULL /* "X4LCHAR1" */
#define CPLAY_WAIT_MS 5000u
#define CPLAY_STOP_REASON 188u
#define CPLAY_RING 4096u
#define CPLAY_PAIRS 256u
enum {CPLAY_OFF,CPLAY_OBSERVE,CPLAY_SYNC};
enum {CPLAY_ABSENT=0,CPLAY_RESIDENT=1,CPLAY_INVALID=2};
/* Slot 17: this, character, motion and nine more (bytes, a float and two doubles on the stack), passed on bit for bit. */
typedef uint64_t (*cplay_fn)(void *,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef struct {uint64_t a[9];} cplay_args;
typedef struct {uint64_t tick,character,motion,caller,seed,exit;uint32_t ordinal,pair,draws,load_us;uint8_t before[2],loaded[2],result,mode;} cplay_row;
static const struct {unsigned rva,size;const char *sha;} cplay_pins[]={
    {CPLAY_PLAY,0xe4u,"ef572ffd5f74eb3c328d1c9ab4de373165a58ce7fa59c024250458d91fd6ddc2"},    /* slot 17 */
    {CPLAY_LOOKUP,0x50u,"e014eab00533d7099da2cb730006f9ca889ee83f32726b5a21f322dd72a13197"},  /* slot 15 */
    {CPLAY_START,0x268u,"90a7b94fe5ed0052f7b4a98239b1459aaeb2299a9b6964b0360cdc57e48ba84b"},  /* the play */
    {CPLAY_QUEUED,0x1cbu,"cb4105d004949042030dc096830562f44b7beb4f2117fb68746aa6e2b02fd814"}, /* the play's queue */
    {0x14bab90u,0x246u,"252b92d6ae56a121c5f2ff3c64296a212541a4b8e647bbbd41450e557a4e4e7d"},  /* GetAssetIntern */
    {0x14bb1d0u,0x158u,"3d8ea0dfd151fa903d557af2356a2483deb04dd2d075de83f51c067733d201c4"},  /* the load queue */
    {0x14dc750u,0x1d0u,"c955b702b116e56312397c65147b13ed9420c56507e809875b0ff523a643b479"}}; /* XLib::AssetLoadThread's loop */
static int cplay_installed,cplay_restored,cplay_mode;
static volatile LONG64 cplay_failures,cplay_rows,cplay_foreign;
static uint64_t cplay_calls,cplay_frame_calls,cplay_native,cplay_resident,cplay_absent,cplay_invalid,cplay_loads,cplay_unloaded,
    cplay_load_us,cplay_load_max_us,cplay_keyed,cplay_declined,cplay_draws,cplay_uncounted,cplay_overflow,cplay_tick=UINT64_MAX;
static struct {uint64_t character,motion;uint32_t count;} cplay_pairs[CPLAY_PAIRS];
static unsigned cplay_pair_count;
static cplay_row cplay_ring[CPLAY_RING];
static const char *cplay_stage="idle";
static int cplay_fail(const char *why) {cplay_stage=why;InterlockedIncrement64(&cplay_failures);return 0;}
/* Admitted frames on the MoveWorker only. */
static int cplay_live(void) {
    return cplay_installed && stats.active && boot_phase==3 && GetCurrentThreadId()==ui_thread && !InterlockedCompareExchange64(&cplay_failures,0,0);
}
/* Key's asset: resident, absent or a key GetAssetIntern rejects (type byte 1..15, index below the type's table size); -1 unreadable. */
static int cplay_residency(const sorder_loader *loader,uint64_t key) {
    unsigned char *entry=loader->manager?sorder_entry(loader,key):NULL;void *asset=NULL;
    if(!entry) return CPLAY_INVALID;
    if(!created_read(entry,&asset,8)) return -1;
    return asset?CPLAY_RESIDENT:CPLAY_ABSENT;
}
/* The engine's synchronous load of key, bounded: 1 resident afterwards, 0 ended without it, -1 failed (stops the gate). */
static int cplay_load(const sorder_loader *loader,uint64_t key,unsigned timeout_ms,uint32_t *spent_us) {
    if(!loader->queue || !loader->lock || !loader->done) return cplay_fail("loader")-1;
    LARGE_INTEGER start,now,frequency;QueryPerformanceCounter(&start);QueryPerformanceFrequency(&frequency);
    EnterCriticalSection(loader->lock);
    uint64_t at=key;DWORD waited=WAIT_FAILED;
    if(loader->queue(loader->manager,&at,SORDER_SYNC_FLAGS)&0xffu) waited=WaitForSingleObject(loader->done,timeout_ms);
    LeaveCriticalSection(loader->lock);
    QueryPerformanceCounter(&now);uint64_t us=frequency.QuadPart?(uint64_t)((now.QuadPart-start.QuadPart)*1000000/frequency.QuadPart):0;
    *spent_us=us>UINT32_MAX?UINT32_MAX:(uint32_t)us;cplay_load_us+=us;if(us>cplay_load_max_us) cplay_load_max_us=us;
    if(waited!=WAIT_OBJECT_0) return cplay_fail(waited==WAIT_TIMEOUT?"sync_timeout":"sync_queue")-1;
    int after=cplay_residency(loader,key);
    if(after<0) return cplay_fail("asset_entry")-1;
    if(after==CPLAY_RESIDENT) {++cplay_loads;return 1;}
    ++cplay_unloaded;return 0;
}
/* How many calls with this character and motion came before in this tick (the key's ordinal). */
static uint32_t cplay_pair(uint64_t character,uint64_t motion) {
    for(unsigned i=0;i<cplay_pair_count;i++) if(cplay_pairs[i].character==character && cplay_pairs[i].motion==motion) return cplay_pairs[i].count++;
    if(cplay_pair_count==CPLAY_PAIRS) {++cplay_overflow;return 0x80000000u|(uint32_t)cplay_frame_calls;}
    cplay_pairs[cplay_pair_count].character=character;cplay_pairs[cplay_pair_count].motion=motion;cplay_pairs[cplay_pair_count].count=1;
    ++cplay_pair_count;return 0;
}
#define CPLAY_ORIGINAL original(self,character,motion,x->a[0],x->a[1],x->a[2],x->a[3],x->a[4],x->a[5],x->a[6],x->a[7],x->a[8])
/* Testable core: the original, the loader and the calling thread's word as arguments. */
static uint64_t cplay_execute(cplay_fn original,const sorder_loader *loader,uint64_t *word,void *self,uint64_t character,uint64_t motion,
                              const cplay_args *x,unsigned timeout_ms) {
    if(!cplay_live()) {
        if(GetCurrentThreadId()!=ui_thread) InterlockedIncrement64(&cplay_foreign);else ++cplay_native;
        return CPLAY_ORIGINAL;
    }
    if(!word) {cplay_fail("word");return CPLAY_ORIGINAL;}
    if(cplay_tick!=stats.issued) {cplay_tick=stats.issued;cplay_frame_calls=0;cplay_pair_count=0;}
    cplay_row r;memset(&r,0,sizeof(r));
    r.tick=stats.issued;r.character=character;r.motion=motion;r.mode=(uint8_t)cplay_mode;
    r.ordinal=(uint32_t)cplay_frame_calls;r.pair=cplay_pair(character,motion);++cplay_calls;++cplay_frame_calls;
    const uint64_t keys[2]={character,motion};
    for(unsigned i=0;i<2;i++) {
        int state=cplay_residency(loader,keys[i]);
        if(state<0) {cplay_fail("asset_entry");return CPLAY_ORIGINAL;}
        r.before[i]=(uint8_t)state;
        if(state==CPLAY_RESIDENT) ++cplay_resident;else if(state==CPLAY_ABSENT) ++cplay_absent;else ++cplay_invalid;
        if(state==CPLAY_ABSENT && cplay_mode==CPLAY_SYNC) {
            uint32_t us=0;int got=cplay_load(loader,keys[i],timeout_ms,&us);
            r.load_us+=us;
            if(got<0) return CPLAY_ORIGINAL; /* the gate stops at the tick check */
            r.loaded[i]=got?1:2;
        }
    }
    const int keyed=cplay_mode==CPLAY_SYNC;
    r.caller=*word;
    if(keyed) {r.seed=lockstep_seed(CPLAY_DOMAIN,r.tick,character,motion,r.pair,0);++cplay_keyed;}
    volatile uint64_t result=0;
    __try {
        if(keyed) *word=r.seed;
        result=CPLAY_ORIGINAL;
    } __finally {
        DWORD error=GetLastError();
        r.exit=*word;
        if(keyed) *word=r.caller;
        if(AbnormalTermination()) cplay_fail("exception");
        SetLastError(error);
    }
    r.result=(uint8_t)(result&0xffu);if(!r.result) ++cplay_declined;
    r.draws=script_rng_steps(keyed?r.seed:r.caller,r.exit);
    if(r.draws==UINT32_MAX) ++cplay_uncounted;else cplay_draws+=r.draws;
    LONG64 at=InterlockedIncrement64(&cplay_rows)-1;cplay_ring[(uint64_t)at%CPLAY_RING]=r;
    return result;
}
#undef CPLAY_ORIGINAL
/* The vtable entry's target. */
static uint64_t cplay_play(void *self,uint64_t character,uint64_t motion,uint64_t a4,uint64_t a5,uint64_t a6,uint64_t a7,uint64_t a8,
                           uint64_t a9,uint64_t a10,uint64_t a11,uint64_t a12) {
    const cplay_args x={{a4,a5,a6,a7,a8,a9,a10,a11,a12}};
    const sorder_loader loader=sorder_native_loader();
    return cplay_execute((cplay_fn)(void *)(image+CPLAY_PLAY),&loader,interrupt_rng(),self,character,motion,&x,CPLAY_WAIT_MS);
}
static int cplay_pins_ok(void) {
    for(unsigned i=0;i<sizeof(cplay_pins)/sizeof(*cplay_pins);i++) if(!hash_ok(image+cplay_pins[i].rva,cplay_pins[i].size,cplay_pins[i].sha)) return 0;
    return 1;
}
/* The manager object still has SCharacterBaseClass's vtable (set by the static initializer 0x8f090). */
static int cplay_object_ok(void) {void *vtable=NULL;return read_local(image+CPLAY_OBJECT,&vtable,8) && vtable==(void *)(image+CPLAY_VTABLE);}
static int cplay_swap(void *expected,void *value) {
    DWORD old,ignored;if(!VirtualProtect(image+CPLAY_SLOT,8,PAGE_READWRITE,&old)) return 0;
    void *actual=InterlockedCompareExchangePointer((void *volatile *)(image+CPLAY_SLOT),value,expected);
    VirtualProtect(image+CPLAY_SLOT,8,old,&ignored);return actual==expected;
}
/* Before the first admitted frame, on the MoveWorker; an identical rerun after a loading-UI reload is accepted. */
static int cplay_arm(int mode) {
    if(cplay_installed) return stats.active && cplay_mode==mode;
    if((mode!=CPLAY_OBSERVE && mode!=CPLAY_SYNC) || !stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread) {
        cplay_stage="guard";return 0;
    }
    if(!cplay_pins_ok()) {cplay_stage="pins";return 0;}
    if(!cplay_object_ok()) {cplay_stage="object";return 0;}
    if(*(void *volatile *)(image+CPLAY_SLOT)!=(void *)(image+CPLAY_PLAY)) {cplay_stage="slot";return 0;}
    cplay_failures=cplay_rows=cplay_foreign=0;
    cplay_calls=cplay_frame_calls=cplay_native=cplay_resident=cplay_absent=cplay_invalid=cplay_loads=cplay_unloaded=0;
    cplay_load_us=cplay_load_max_us=cplay_keyed=cplay_declined=cplay_draws=cplay_uncounted=cplay_overflow=0;
    cplay_tick=UINT64_MAX;cplay_pair_count=0;cplay_mode=mode;cplay_restored=0;
    if(!cplay_swap(image+CPLAY_PLAY,(void *)cplay_play)) {cplay_stage="swap";cplay_mode=CPLAY_OFF;return 0;}
    cplay_installed=1;cplay_stage="armed";return 1;
}
static int cplay_owned(void) {return !cplay_installed || *(void *volatile *)(image+CPLAY_SLOT)==(void *)cplay_play;}
/* Main-thread tick check; any failure stops the gate with reason 188. */
static int cplay_check(void) {
    if(InterlockedCompareExchange64(&cplay_failures,0,0)) return 0;
    const char *why=!cplay_owned()?"slot_changed":!cplay_object_ok()?"object_changed":!cplay_pins_ok()?"block_changed":NULL;
    return why?cplay_fail(why):1;
}
static void cplay_restore(void) {
    if(!cplay_installed) return;
    if(cplay_swap((void *)cplay_play,image+CPLAY_PLAY)) {cplay_installed=0;cplay_restored=1;} /* a foreign value stays */
}
static const char *cplay_mode_name(void) {return cplay_mode==CPLAY_SYNC?"sync":cplay_mode==CPLAY_OBSERVE?"observe":"off";}
static int cplay_row_text(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&cplay_rows,0,0);int n;
    if((LONG64)index>=count) return 0;
    if(count>(LONG64)CPLAY_RING && index<(uint64_t)(count-(LONG64)CPLAY_RING))
        return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    const cplay_row *r=&cplay_ring[index%CPLAY_RING];
    static const char state[3]={'a','r','i'},load[3]={'-','l','u'};
    n=snprintf(out,cap,"index=%llu tick=%llu mode=%s ordinal=%u pair=%u character=%016llx motion=%016llx before=%c%c loaded=%c%c load_us=%u "
        "result=%u caller=%016llx seed=%016llx exit=%016llx draws=%lld",(unsigned long long)index,(unsigned long long)r->tick,
        r->mode==CPLAY_SYNC?"sync":"observe",r->ordinal,r->pair,(unsigned long long)r->character,(unsigned long long)r->motion,
        state[r->before[0]%3],state[r->before[1]%3],load[r->loaded[0]%3],load[r->loaded[1]%3],r->load_us,r->result,
        (unsigned long long)r->caller,(unsigned long long)r->seed,(unsigned long long)r->exit,r->draws==UINT32_MAX?-1LL:(long long)r->draws);
    return n>0 && (size_t)n<cap?n:0;
}
static int cplay_status(char *out,size_t cap) {
    int n=snprintf(out,cap,"mode=%s installed=%d frame=%llu calls=%llu frame_calls=%llu native=%llu foreign=%lld resident=%llu absent=%llu "
        "invalid=%llu loads=%llu unloaded=%llu load_us=%llu max_load_us=%llu keyed=%llu declined=%llu draws=%llu uncounted=%llu overflow=%llu "
        "rows=%lld restored=%d stage=%s failures=%lld",cplay_mode_name(),cplay_installed,(unsigned long long)stats.issued,
        (unsigned long long)cplay_calls,(unsigned long long)(cplay_tick==stats.issued?cplay_frame_calls:0),(unsigned long long)cplay_native,
        (long long)cplay_foreign,(unsigned long long)cplay_resident,(unsigned long long)cplay_absent,(unsigned long long)cplay_invalid,
        (unsigned long long)cplay_loads,(unsigned long long)cplay_unloaded,(unsigned long long)cplay_load_us,(unsigned long long)cplay_load_max_us,
        (unsigned long long)cplay_keyed,(unsigned long long)cplay_declined,(unsigned long long)cplay_draws,(unsigned long long)cplay_uncounted,
        (unsigned long long)cplay_overflow,(long long)cplay_rows,cplay_restored,cplay_stage,(long long)cplay_failures);
    return n>0 && (size_t)n<cap?n:0;
}
