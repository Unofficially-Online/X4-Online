/* Determinism fix, DEFAULT OFF in the bare gate (a lockstep session's default is on): native_gate_move_rng = true.
 *
 * Seen 16/17 Nov with two clients at a station (lockstep_draw_watch.h, frame mode, run sw42): from the tick the first
 * player arrives, the frame's RNG word parts by one to four steps on many ticks (146 of 500), and comes together
 * again at the next frame's seed. The draws one client makes and the other does not, by drawer:
 *   - BodyClass's weighted pick of a body's next animation (0x374f50, inline PCG, store 0x374fcc) from
 *     BodyClass::Move 0x382015 <- NPC::Move 0x6bc420 (call returning to 0x6bc609): 58 on one client, 53 on the other,
 *     on different ticks;
 *   - a draw of NPC::Move itself (Random at 0x14ddcf0 from 0x6bc6dc): 22 / 23;
 *   - SignalLeak::Move 0x651310 (0x14ddcf0 from 0x651372): 24 on one client only.
 * When a body's animation ends is each client's own (the starts, CHARACTER_PLAY, fall on different ticks from the
 * arrival on), and a signal leak draws by what this client shows. A draw more or less moves every later draw of the
 * frame along the word: in the runs a mass-traffic ship made in such a frame got a pilot with another character
 * macro on one client (the world check then differed at the next census), in others shield values.
 * Here: the Move of U::NPC (vtable 0x2b38a48) and of U::SignalLeak (0x2af7fa8), vfunc 674, runs with the thread's
 * word TLS+0x318 keyed from (tick, the object's portable ID, the class; an object without one: its ordinal among such
 * objects of its class in the tick), and the caller's word is put back afterwards, also when an exception unwinds.
 * Their draws never reach the frame word; two clients whose bodies do the same thing at the same tick still draw the
 * same values. Data only (two vtable entries, CAS), admitted frames on the simulation thread; other threads, boot
 * frames and loading pass through.
 * What it does not do: make the bodies' animations end at the same tick on every client. What follows from an
 * animation's end (the people's scripts hear it: cue instances, their runtime IDs) can still differ.
 * Pins: the first 0x40 bytes of both functions and both entries; else nothing is installed and arming refuses.
 * No stop reason: an entry someone else changed is left as it is at the restore.
 * Status MOVE_RNG: installed, per class calls/drew/draws, objects without a portable ID, calls off the simulation
 * thread, step counts beyond the bound, exceptions.
 * Diagnostic rows (native_gate_move_rng_rows): MOVE_RNG_ROW per Move that drew: tick, class, the object's portable ID
 * (0: none) and its parent's, the number of draws; a ring of MRNG_ROWS, read by index. */
#define MRNG_ROWS 65536u
typedef struct {uint64_t tick,id,parent;uint32_t draws,site;} mrng_row_t;
static mrng_row_t *mrng_ring;
static volatile LONG64 mrng_row_cursor;
static volatile LONG mrng_rows_on;
#define MRNG_SLOT 0x1510u                 /* vfunc 674: Move */
#define MRNG_DOMAIN 0x58344c4d4f564552ULL /* "X4LMOVER" */
#define MRNG_HEAD 0x40u
#define MRNG_SITES 2u
typedef void (*mrng_fn)(void *);
typedef struct {unsigned vtable,target;const char *sha,*name;} mrng_site_t;
static const mrng_site_t mrng_sites[MRNG_SITES]={
    {0x2b38a48u,0x6bc420u,"89c1807783be566ad3b158bf48a3e73b4a630a4609b54cb5b4ad0aaf2333637a","npc"},
    {0x2af7fa8u,0x651310u,"001145f8c3df92dcd20d3a14111ba79cb6fec339d8657aaaaf85d22f404d3361","leak"},
};
static int mrng_installed,mrng_restored;
static unsigned char mrng_swapped[MRNG_SITES];
static volatile LONG mrng_active;
/* A third kind without an entry of its own: a Player's Move (lockstep_player_timer.h holds that entry and calls
 * mrng_player). A player's body picks its animations as an NPC's does (BodyClass::Move from Player::Move 0x71342d),
 * and what the animation system answers it depends on this client's camera most of all: its own player's body is
 * the one the camera sits in (run sw52: a docked player's body started motion 0x21 every 25 ticks, two ticks later
 * on the other client). */
#define MRNG_KINDS 3u
#define MRNG_PLAYER 2u
static volatile LONG64 mrng_calls[MRNG_KINDS],mrng_drew[MRNG_KINDS],mrng_draws[MRNG_KINDS],mrng_unkeyed,mrng_foreign,mrng_unknown,mrng_failures;
static uint64_t mrng_tick=UINT64_MAX;
static uint32_t mrng_ordinal[MRNG_KINDS];
static __thread uint64_t mrng_fallback;   /* the ID of an object without a portable one, when its caller has one for it */
static mrng_fn mrng_original_override; /* tests only */
static __thread uint64_t bclk_npc;       /* body clock rows (below): the NPC whose Move runs on this thread */
static __thread int bclk_inside;
static const char *mrng_stage="idle";
static void mrng_npc(void *self);
static void mrng_leak(void *self);
static void *mrng_wrapper(unsigned site) {return site?(void*)mrng_leak:(void*)mrng_npc;}
static int mrng_pins(void) {
    for(unsigned i=0;i<MRNG_SITES;i++) {
        void *slot=NULL;
        if(!hash_ok(image+mrng_sites[i].target,MRNG_HEAD,mrng_sites[i].sha) || !read_local(image+mrng_sites[i].vtable+MRNG_SLOT,&slot,8)) return 0;
        if(slot!=(void*)(image+mrng_sites[i].target) && !(mrng_installed && mrng_swapped[i])) return 0;
    }
    return 1;
}
/* One Move: the original with the object's own word for this tick. */
static void mrng_run(unsigned site,mrng_fn original,uint64_t *rng,void *self) {
    InterlockedIncrement64(&mrng_calls[site]);
    if(!rng || !InterlockedCompareExchange(&mrng_active,0,0) || !stats.active || boot_phase!=3 || !stats.issued) {original(self);return;}
    if(GetCurrentThreadId()!=ui_thread) {InterlockedIncrement64(&mrng_foreign);original(self);return;}
    if(mrng_tick!=stats.issued) {mrng_tick=stats.issued;memset(mrng_ordinal,0,sizeof(mrng_ordinal));}
    uint64_t runtime=0,id=0,ordinal=0;unsigned klass=0;
    if(read_local((unsigned char*)self+8,&runtime,8) && runtime) id=world_identity_key(runtime,&klass);
    if(!id && mrng_fallback) id=mrng_fallback;
    if(!id) {ordinal=++mrng_ordinal[site];InterlockedIncrement64(&mrng_unkeyed);}
    const uint64_t saved=*rng,seed=lockstep_seed(MRNG_DOMAIN,stats.issued,id,site,ordinal,klass);
    *rng=seed;
    const int body=site!=1;   /* an NPC or a player: its body's clock is the simulation's inside this Move */
    if(body) {bclk_npc=id;bclk_inside=1;}
    __try {original(self);}
    __finally {
        if(body) bclk_inside=0;
        const uint64_t exit=*rng;*rng=saved;
        const uint32_t draws=actor_all_steps(seed,exit);
        if(draws==UINT32_MAX) InterlockedIncrement64(&mrng_unknown);
        else if(draws) {InterlockedIncrement64(&mrng_drew[site]);InterlockedAdd64(&mrng_draws[site],draws);}
        if(draws && mrng_ring && InterlockedCompareExchange(&mrng_rows_on,0,0)) {
            unsigned char *parent=NULL;uint64_t parent_runtime=0,parent_id=0;unsigned parent_class=0;
            if(read_local((unsigned char*)self+0x70,&parent,8) && parent && read_local(parent+8,&parent_runtime,8) && parent_runtime)
                parent_id=world_identity_key(parent_runtime,&parent_class);
            const LONG64 index=InterlockedIncrement64(&mrng_row_cursor)-1;
            mrng_ring[(uint64_t)index%MRNG_ROWS]=(mrng_row_t){stats.issued,id,parent_id,draws,site};
        }
        if(AbnormalTermination()) InterlockedIncrement64(&mrng_failures);
    }
}
static __attribute__((noinline)) void mrng_npc(void *self) {
    mrng_run(0,mrng_original_override?mrng_original_override:(mrng_fn)(image+mrng_sites[0].target),actor_all_tls_rng(),self);
}
/* lockstep_player_timer.h: a Player's Move in an admitted frame. fallback: an ID for a player without a portable one
 * (a slot's player made by the gate): the same on every client. */
static void mrng_player(mrng_fn original,void *self,uint64_t fallback) {
    mrng_fallback=fallback;
    __try {mrng_run(MRNG_PLAYER,original,actor_all_tls_rng(),self);}
    __finally {mrng_fallback=0;}
}
static __attribute__((noinline)) void mrng_leak(void *self) {
    mrng_run(1,mrng_original_override?mrng_original_override:(mrng_fn)(image+mrng_sites[1].target),actor_all_tls_rng(),self);
}
/* Give every entry we hold back; 1 when none is left. */
static int mrng_unswap(void) {
    int all=1;
    for(unsigned i=0;i<MRNG_SITES;i++) if(mrng_swapped[i]) {
        if(script_rng_slot_swap(mrng_sites[i].vtable+MRNG_SLOT,mrng_wrapper(i),image+mrng_sites[i].target)) mrng_swapped[i]=0;else all=0;
    }
    return all;
}
/* Before the first admitted frame, on the simulation thread; an identical rerun answers 1. */
static int mrng_arm(void) {
    if(mrng_installed) return stats.active;
    if(!stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(!mrng_pins()) {mrng_stage="pins";return 0;}
    memset((void*)mrng_calls,0,sizeof(mrng_calls));memset((void*)mrng_drew,0,sizeof(mrng_drew));memset((void*)mrng_draws,0,sizeof(mrng_draws));
    mrng_unkeyed=mrng_foreign=mrng_unknown=mrng_failures=0;mrng_tick=UINT64_MAX;
    InterlockedExchange(&mrng_active,1);mrng_installed=1;
    for(unsigned i=0;i<MRNG_SITES;i++) {
        if(!script_rng_slot_swap(mrng_sites[i].vtable+MRNG_SLOT,image+mrng_sites[i].target,mrng_wrapper(i))) {
            InterlockedExchange(&mrng_active,0);mrng_unswap();mrng_installed=0;mrng_stage="slot";return 0;
        }
        mrng_swapped[i]=1;
    }
    mrng_restored=0;mrng_stage="armed";return 1;
}
static int mrng_rows_arm(void) {
    if(!mrng_installed) return 0;
    if(!mrng_ring) mrng_ring=VirtualAlloc(NULL,sizeof(mrng_row_t)*MRNG_ROWS,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!mrng_ring) return 0;
    InterlockedExchange(&mrng_rows_on,1);return 1;
}
static int mrng_row_text(uint64_t index,char *out,size_t capacity) {
    const LONG64 cursor=InterlockedCompareExchange64(&mrng_row_cursor,0,0);
    if(!mrng_ring || (LONG64)index>=cursor) return 0;
    int n;
    if(cursor>(LONG64)MRNG_ROWS && index<(uint64_t)(cursor-(LONG64)MRNG_ROWS)) n=snprintf(out,capacity,"index=%llu evicted=1",(unsigned long long)index);
    else {
        const mrng_row_t *r=&mrng_ring[index%MRNG_ROWS];
        n=snprintf(out,capacity,"index=%llu tick=%llu class=%s id=%016llx parent=%016llx draws=%u",(unsigned long long)index,
            (unsigned long long)r->tick,r->site<MRNG_SITES?mrng_sites[r->site].name:"player",(unsigned long long)r->id,(unsigned long long)r->parent,r->draws);
    }
    return n>0 && (size_t)n<capacity?n:0;
}
/* Diagnostic (native_gate_body_clock_rows): what BodyClass::Move is told about a body's running animation. It asks
 * the character animation system (the static object 0x31bf8d0, vtable XGfx::SCharacterBaseClass 0x2c90ac8) whether the
 * animation is still active (vfunc 26, 0x129ff00), for its remaining time (vfunc 27, 0x129fff0: the control's float
 * +0x54) and its elapsed time (vfunc 28, 0x12a0060: +0x44), and ends the animation by those (380dd0: not active, or
 * remaining below the sequence's lead, or elapsed above its limit), then posts CharacterAnimationFinishedEvent
 * (37b3f0) and picks the next. The three entries point at wrappers that pass through and row the answer while an
 * NPC's Move runs on the simulation thread: BODY_CLOCK_ROW tick, the NPC's portable ID, which question, the low word
 * of the motion key, the answer. Two clients' rows of the same person and tick tell whether the animation system's
 * answers part (a clock of its own) or only the times they are asked. Observation only. */
#define BCLK_VTABLE 0x2c90ac8u
#define BCLK_ROWS 262144u
typedef struct {uint64_t tick,id,value;uint32_t motion,which;} bclk_row_t;
static bclk_row_t *bclk_ring;
static volatile LONG64 bclk_cursor;
static volatile LONG bclk_on;
static int bclk_installed;
static const unsigned bclk_targets[3]={0x129ff00u,0x129fff0u,0x12a0060u};
static const char *const bclk_shas[3]={   /* the first 0x20 bytes of each */
    "797e08026fa3c0168f909f5fb9d9b39c927f6c401a3f48974278decc9577490f",
    "75989b5e2a01efea0bffc4c3597feb3b5dff4d56795488b27fedabe135e21e9e",
    "75989b5e2a01efea0bffc4c3597feb3b5dff4d56795488b27fedabe135e21e9e"};
/* The clock itself (native_gate_body_clock, a session default; needs native_gate_move_rng and the character play
 * module, which tells the plays). Seen with the rows (run sw49, two clients, both keyed alike): a person's elapsed
 * and remaining time stood still for three or four ticks on one client and then caught up (1.5000 at ticks 2022..2025,
 * 1.6333 at 2026) while the other client's moved every tick: X4 steps a body's animation less often when this
 * client's camera has little of it. BodyClass::Move ends an animation when its remaining time falls below the
 * sequence's lead, so the end fell on different ticks on the two clients, the finished event with it, and the
 * people's scripts ran ticks to seconds apart.
 * Here: when an NPC's Move starts an animation (the play, told by lockstep_character_play.h right after X4's play
 * returned), the gate notes for (that NPC, the motion) the thread's game time and the animation's whole remaining
 * time as X4 answers it at that moment (the control is new: nothing has stepped it yet, so the answer is the same on
 * every client). From then on, inside that NPC's Move, the three answers come from the simulation's time alone:
 * elapsed = now - start, remaining = whole - elapsed (not below 0), active = elapsed < whole, whatever X4's own
 * control says: it lags on a client that steps it rarely, and it can also be gone early (run sw55: a 0.8333 s
 * animation's control was gone at 0.7667 s on the client whose player had just docked). From the tick after the one
 * in which the animation was over by this clock the note is spent, and X4 answers again (an animation started some
 * other way must not meet an old note). Without a noted play (a queued play, another entry than vfunc 17, a body
 * outside an NPC's or a player's Move) X4's answers pass through.
 * Status clock=installed/plays/noted/answers. */
#define BCLK_ENTRIES 4096u
typedef struct {uint64_t id,over_tick;uint32_t motion,used;double start,whole;} bclk_entry_t;   /* over_tick: 0 running */
static bclk_entry_t bclk_entries[BCLK_ENTRIES];
static volatile LONG bclk_clock_on;
static volatile LONG64 bclk_plays,bclk_noted,bclk_answers;
static double (*bclk_now_override)(void);   /* tests only */
static uint32_t bclk_motion_key(uint64_t motion) {return (uint32_t)(motion&0xffffffu)|((uint32_t)(motion>>32&0xffu)<<24);}
static int bclk_now(double *now) {
    if(bclk_now_override) {*now=bclk_now_override();return 1;}
    uint64_t *word=actor_all_tls_rng();
    if(!word) return 0;
    memcpy(now,(unsigned char*)word-0x18,8);   /* TLS+0x300: the thread's game time; the RNG word is TLS+0x318 */
    return isfinite(*now);
}
static bclk_entry_t *bclk_entry(uint64_t id,uint32_t motion,int make) {
    const unsigned first=(unsigned)lockstep_mix(id^((uint64_t)motion*0x9e3779b97f4a7c15ULL))&(BCLK_ENTRIES-1u);
    bclk_entry_t *oldest=NULL;
    for(unsigned n=0;n<16;n++) {
        bclk_entry_t *e=&bclk_entries[(first+n)&(BCLK_ENTRIES-1u)];
        if(e->used && e->id==id && e->motion==motion) return e;
        if(make && (!oldest || !e->used || (oldest->used && e->start<oldest->start))) oldest=e;
    }
    if(!make) return NULL;
    oldest->id=id;oldest->motion=motion;oldest->used=1;oldest->start=0;oldest->whole=-1;oldest->over_tick=0;return oldest;
}
static void bclk_note(unsigned which,uint64_t motion,uint64_t value);
/* lockstep_character_play.h, after X4's play (vfunc 17) answered: character and motion as the play got them. */
static void bclk_played(void *self,uint64_t character,uint64_t motion,int started) {
    if(!InterlockedCompareExchange(&bclk_clock_on,0,0) || !bclk_inside || !bclk_npc || GetCurrentThreadId()!=ui_thread) return;
    InterlockedIncrement64(&bclk_plays);
    bclk_entry_t *e=bclk_entry(bclk_npc,bclk_motion_key(motion),1);
    double now=0;e->whole=-1;e->over_tick=0;
    if(!started || !bclk_now(&now)) return;
    const double remaining=((double(*)(void*,uint64_t,uint64_t))(image+bclk_targets[1]))(self,character,motion);
    const double elapsed=((double(*)(void*,uint64_t,uint64_t))(image+bclk_targets[2]))(self,character,motion);
    {uint64_t bits;memcpy(&bits,&remaining,8);bclk_note(3,motion,bits);memcpy(&bits,&elapsed,8);bclk_note(4,motion,bits);}   /* rows: X4's answers at the play */
    if(!(remaining>0.0) || !(elapsed>=0.0) || !isfinite(remaining) || !isfinite(elapsed)) return;   /* no control yet: X4's answers */
    e->start=now;e->whole=remaining+elapsed;InterlockedIncrement64(&bclk_noted);
}
/* The simulation's answer for the NPC whose Move runs: 1 with *elapsed and *whole, 0 = X4's own answers. */
static int bclk_clock(uint64_t motion,double *elapsed,double *whole) {
    double now=0;
    if(!InterlockedCompareExchange(&bclk_clock_on,0,0) || !bclk_inside || !bclk_npc || GetCurrentThreadId()!=ui_thread) return 0;
    bclk_entry_t *e=bclk_entry(bclk_npc,bclk_motion_key(motion),0);
    if(!e || !(e->whole>0.0) || (e->over_tick && stats.issued>e->over_tick) || !bclk_now(&now)) return 0;
    *elapsed=now>e->start?now-e->start:0.0;*whole=e->whole;
    if(*elapsed>=*whole && !e->over_tick) e->over_tick=stats.issued?stats.issued:1;
    InterlockedIncrement64(&bclk_answers);return 1;
}
static void bclk_note(unsigned which,uint64_t motion,uint64_t value) {
    if(!bclk_ring || !bclk_inside || !InterlockedCompareExchange(&bclk_on,0,0)) return;
    const LONG64 index=InterlockedIncrement64(&bclk_cursor)-1;
    bclk_ring[(uint64_t)index%BCLK_ROWS]=(bclk_row_t){stats.issued,bclk_npc,value,(uint32_t)motion,which};
}
static unsigned char bclk_active(void *self,uint64_t character,uint64_t motion,unsigned char queued) {
    unsigned char r=((unsigned char(*)(void*,uint64_t,uint64_t,unsigned char))(image+bclk_targets[0]))(self,character,motion,queued);
    double elapsed=0,whole=0;
    if(bclk_clock(motion,&elapsed,&whole)) r=elapsed<whole;
    bclk_note(0,motion,r);return r;
}
static double bclk_remaining(void *self,uint64_t character,uint64_t motion) {
    double r=((double(*)(void*,uint64_t,uint64_t))(image+bclk_targets[1]))(self,character,motion);
    double elapsed=0,whole=0;
    if(bclk_clock(motion,&elapsed,&whole)) r=whole>elapsed?whole-elapsed:0.0;
    uint64_t bits;memcpy(&bits,&r,8);bclk_note(1,motion,bits);return r;
}
static double bclk_elapsed(void *self,uint64_t character,uint64_t motion) {
    double r=((double(*)(void*,uint64_t,uint64_t))(image+bclk_targets[2]))(self,character,motion);
    double elapsed=0,whole=0;
    if(bclk_clock(motion,&elapsed,&whole)) r=elapsed;
    uint64_t bits;memcpy(&bits,&r,8);bclk_note(2,motion,bits);return r;
}
static void *bclk_wrapper(unsigned i) {return i==0?(void*)bclk_active:i==1?(void*)bclk_remaining:(void*)bclk_elapsed;}
/* The three entries (before the first tick; the move RNG module armed). rows: the diagnostic ring; clock: the
 * simulation's answers. Either may be asked first; the entries are swapped once. */
static int bclk_arm(int rows,int clock) {
    if(!mrng_installed) return 0;
    if(!bclk_installed) {
        if(stats.issued) return 0;
        for(unsigned i=0;i<3;i++) if(*(void*volatile*)(image+BCLK_VTABLE+(26u+i)*8u)!=(void*)(image+bclk_targets[i])) return 0;
        for(unsigned i=0;i<3;i++) if(!hash_ok(image+bclk_targets[i],0x20,bclk_shas[i])) return 0;
        for(unsigned i=0;i<3;i++) if(!script_rng_slot_swap(BCLK_VTABLE+(26u+i)*8u,image+bclk_targets[i],bclk_wrapper(i))) return 0;
        bclk_installed=1;memset(bclk_entries,0,sizeof(bclk_entries));bclk_plays=bclk_noted=bclk_answers=0;
    }
    if(rows) {
        if(!bclk_ring) bclk_ring=VirtualAlloc(NULL,sizeof(bclk_row_t)*BCLK_ROWS,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
        if(!bclk_ring) return 0;
        InterlockedExchange(&bclk_on,1);
    }
    if(clock) InterlockedExchange(&bclk_clock_on,1);
    return 1;
}
static void bclk_restore(void) {
    InterlockedExchange(&bclk_on,0);InterlockedExchange(&bclk_clock_on,0);
    if(!bclk_installed) return;
    for(unsigned i=0;i<3;i++) (void)script_rng_slot_swap(BCLK_VTABLE+(26u+i)*8u,bclk_wrapper(i),image+bclk_targets[i]);
    bclk_installed=0;
}
static int bclk_row_text(uint64_t index,char *out,size_t capacity) {
    static const char *const names[5]={"active","remaining","elapsed","play_remaining","play_elapsed"};
    const LONG64 cursor=InterlockedCompareExchange64(&bclk_cursor,0,0);
    if(!bclk_ring || (LONG64)index>=cursor) return 0;
    int n;
    if(cursor>(LONG64)BCLK_ROWS && index<(uint64_t)(cursor-(LONG64)BCLK_ROWS)) n=snprintf(out,capacity,"index=%llu evicted=1",(unsigned long long)index);
    else {
        const bclk_row_t *r=&bclk_ring[index%BCLK_ROWS];double value;memcpy(&value,&r->value,8);
        if(r->which==0) n=snprintf(out,capacity,"index=%llu tick=%llu id=%016llx ask=active motion=%08x value=%llu",(unsigned long long)index,
            (unsigned long long)r->tick,(unsigned long long)r->id,r->motion,(unsigned long long)r->value);
        else n=snprintf(out,capacity,"index=%llu tick=%llu id=%016llx ask=%s motion=%08x value=%.4f",(unsigned long long)index,
            (unsigned long long)r->tick,(unsigned long long)r->id,names[r->which<5?r->which:0],r->motion,value);
    }
    return n>0 && (size_t)n<capacity?n:0;
}
static void mrng_restore(void) {
    bclk_restore();
    InterlockedExchange(&mrng_rows_on,0);
    InterlockedExchange(&mrng_active,0);
    if(!mrng_installed) return;
    if(!mrng_unswap()) return; /* a foreign value stays */
    mrng_installed=0;mrng_restored=1;
}
static int mrng_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"installed=%d npc=%lld/%lld/%lld leak=%lld/%lld/%lld player=%lld/%lld/%lld unkeyed=%lld foreign=%lld unknown=%lld restored=%d stage=%s failures=%lld "
        "clock=%d/%lld/%lld/%lld",
        mrng_installed,(long long)mrng_calls[0],(long long)mrng_drew[0],(long long)mrng_draws[0],(long long)mrng_calls[1],(long long)mrng_drew[1],
        (long long)mrng_draws[1],(long long)mrng_calls[2],(long long)mrng_drew[2],(long long)mrng_draws[2],(long long)mrng_unkeyed,(long long)mrng_foreign,(long long)mrng_unknown,mrng_restored,mrng_stage,(long long)mrng_failures,
        bclk_installed && bclk_clock_on,(long long)bclk_plays,(long long)bclk_noted,(long long)bclk_answers);
    return n>0 && (size_t)n<capacity?n:0;
}
