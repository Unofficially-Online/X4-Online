/* A ship's death draws keyed for every ship, both gates (native_gate_death_rng = 'all', the default).
 *
 * The KillEvent listener of every ship class (Windows U::Ship 0x7cd610 in the S, M, L and XL vtables' slot 8;
 * Linux 0x12adf40, slot 9) gives the wreck its drift: 0x7d3bf0 (Linux inlined in the listener) draws six
 * Random::FloatState values from the delivering thread's word (TLS slot0+0x318 / fs:-0x2720), three for the angular
 * and three for the linear velocity, in the same order on both executables. That word is the UI thread's, which the
 * frame's other users (UI queries, effects, sounds) move by different counts on the two executables: in the 22 Nov
 * mixed census (W67 against lx94a) the word at the end of the frame differed at ticks 1, 25-35, 51-61 and 75, and at
 * 75 the Kha'ak ship 0x9fa51 died. Twelve fighters whose move target was that ship (FlightControlModel target
 * reference 0x9fa51) took their next step at 77 toward a wreck that drifted differently, and their orientations
 * parted by about 1e-4. With this mode the listener runs on a word seeded from the gate tick, the dying ship's
 * portable key (saved ID, or created ID with native_gate_portable_created) and the kill reason, and the caller's word
 * is put back after it: a wreck drifts the same on every client whatever else drew that frame. A ship without a
 * portable key, a kill delivered on another thread or before the admitted frames runs natively (unkeyed=). */
#define DEATH_ALL_DOMAIN 0x44454154485f414cULL /* "DEATH_AL" */
static uint64_t death_all_seed(uint64_t tick,uint64_t key,unsigned reason) {
    return lockstep_mix(lockstep_mix(lockstep_mix(DEATH_ALL_DOMAIN^tick)^key)^(uint64_t)reason);
}
/* Rows (diagnostic, native_gate_death_all_rows): each keyed death's tick, key, reason, the listener's time argument and
 * the delivering thread's game time (TLS) as it enters, the same text on both gates:
 *   DEATH_ALL_ROW index= tick= key= reason= time= clock=
 * Written by the UI thread only (the keyed path's guard). */
#define DEATH_ALL_ROWS 256u
typedef struct {uint64_t tick,key,time,clock;unsigned reason;} death_all_row_t;
static death_all_row_t death_all_ring[DEATH_ALL_ROWS];
static uint64_t death_all_row_count;
static void death_all_note(uint64_t tick,uint64_t key,unsigned reason,double time,uint64_t clock) {
    death_all_row_t *r=&death_all_ring[death_all_row_count%DEATH_ALL_ROWS];
    r->tick=tick;r->key=key;r->reason=reason;memcpy(&r->time,&time,8);r->clock=clock;++death_all_row_count;
}
__attribute__((unused)) static int death_all_row(uint64_t index,char *out,size_t capacity) {
    int n;
    if(index>=death_all_row_count) return 0;
    if(death_all_row_count>DEATH_ALL_ROWS && index<death_all_row_count-DEATH_ALL_ROWS)
        return (n=snprintf(out,capacity,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<capacity?n:0;
    const death_all_row_t *r=&death_all_ring[index%DEATH_ALL_ROWS];
    n=snprintf(out,capacity,"index=%llu tick=%llu key=%016llx reason=%u time=%016llx clock=%016llx",(unsigned long long)index,
        (unsigned long long)r->tick,(unsigned long long)r->key,r->reason,(unsigned long long)r->time,(unsigned long long)r->clock);
    return n>0 && (size_t)n<capacity?n:0;
}
/* Wreck clock rows (diagnostic, read-only, native_gate_wreck_clock_rows = {first, last}): every call of a wreck's pose
 * function, U::LinearDecelerationController (Windows slot 6 0xe27580(this, out, time); Linux slot 39 0x14ddcd0(this, out,
 * flag, time)), in those ticks: the time it is asked for, the calling thread's game time (current and previous) and
 * thread, the first four image return addresses, the same text on both gates:
 *   WRECK_CLOCK index= tick= self= time= cur= prev= thread=ui|main|other chain=
 * 22 Nov: the wreck controllers are byte-equal on both executables and the fighters that aim at it step at the same
 * time, yet the target point they take from it is one tick of drift apart. */
#define WRECK_CLOCK_ROWS 4096u
typedef struct {uint64_t tick,self,time,cur,prev;uint32_t chain[4];unsigned char thread;} wreck_clock_row_t;
static wreck_clock_row_t wreck_clock_ring[WRECK_CLOCK_ROWS];
static volatile LONG64 wreck_clock_count;
static uint64_t wreck_clock_first,wreck_clock_last;
static void wreck_clock_note(const void *self,double time,uint64_t cur,uint64_t prev,unsigned thread,const uint32_t chain[4]) {
    if(!wreck_clock_last || stats.issued<wreck_clock_first || stats.issued>wreck_clock_last) return;
    const LONG64 index=InterlockedIncrement64(&wreck_clock_count)-1;
    wreck_clock_row_t *r=&wreck_clock_ring[(uint64_t)index%WRECK_CLOCK_ROWS];
    r->tick=stats.issued;r->self=(uint64_t)(uintptr_t)self;memcpy(&r->time,&time,8);r->cur=cur;r->prev=prev;r->thread=(unsigned char)thread;
    memcpy(r->chain,chain,sizeof(r->chain));
}
__attribute__((unused)) static int wreck_clock_row(uint64_t index,char *out,size_t capacity) {
    const LONG64 count=InterlockedCompareExchange64(&wreck_clock_count,0,0);int n;
    if((LONG64)index>=count) return 0;
    if(count>(LONG64)WRECK_CLOCK_ROWS && index<(uint64_t)(count-(LONG64)WRECK_CLOCK_ROWS))
        return (n=snprintf(out,capacity,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<capacity?n:0;
    const wreck_clock_row_t *r=&wreck_clock_ring[index%WRECK_CLOCK_ROWS];
    static const char *const threads[3]={"ui","main","other"};
    n=snprintf(out,capacity,"index=%llu tick=%llu self=%llx time=%016llx cur=%016llx prev=%016llx thread=%s chain=%x,%x,%x,%x",
        (unsigned long long)index,(unsigned long long)r->tick,(unsigned long long)(r->self&0xffffffu),(unsigned long long)r->time,
        (unsigned long long)r->cur,(unsigned long long)r->prev,threads[r->thread<3?r->thread:2],r->chain[0],r->chain[1],r->chain[2],r->chain[3]);
    return n>0 && (size_t)n<capacity?n:0;
}
