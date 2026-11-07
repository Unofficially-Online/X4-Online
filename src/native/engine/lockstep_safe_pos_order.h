/* Determinism fix, DEFAULT OFF: native_gate_safe_pos_order = true.
 *
 * The GetSafePos solver 0x14e4320 (rcx the boxes vector, rdx the point, xmm2 the radius, r9 the direction, then a5, a
 * word it never reads, the allow-y byte, the move direction and a9; FR27, lockstep_safe_pos.h) absorbs the boxes it is
 * given one at a time in vector order: each absorbed box is united into a world bound (its rounding depends on the
 * order), the point is pushed out of that bound from the start point again, and the box is swapped with the last one in
 * place before the scan starts over. The same boxes in another order give a point 1-3 ulps apart. Its callers pass a
 * cache kept on the space object: Sector::GetSafePos 0x786d10 builds [sector+0x3d0] once, while it is null (from the
 * sector's children, 5f7ef0 each), and a copy at [sector+0x3d8] that 78e910 extends when [sector+0x440] is set; the
 * constructor 0x781e60 is the only other writer. Zone::GetSafePos 0x835b80 passes the zone's. So a cache's order is the
 * order it was built in, permuted by every get_safe_pos since: a hot joiner builds its caches after its load and never
 * ran the session's earlier calls [w1z143 .. 147, j1-333: move.generic's get_safe_pos point for 742be in ZRL-560 is 2
 * ulps apart on the joiner at 1049, the long-soak divergence; every published pose 0x3dde10 composes with is equal].
 *
 * This module sorts the boxes by the bytes the solver reads (+0x00..+0x23 and +0x30..+0x6f; +0x24..+0x2f are padding)
 * at the solver's entry and then runs the native solver, so its result depends only on the set of boxes, not on any
 * history. Boxes equal in those bytes are interchangeable to the solver. Every peer must arm it from boot.
 *
 * Entry: the solver begins mov rax,rsp; mov [rax+0x18],rbx (7 bytes, no rip operand). Its first five bytes become jmp
 * rel32 to a stub on our page (jmp [rip] -> spo_hook); the trampoline replays the 7 bytes and jumps to 0x14e4327
 * (FR27's scheme). The page lies within +-2 GB of the entry. Pins: the solver's 0x567 bytes through the view (FR27's
 * SHA), the entry raw. While installed, reads through hash_ok, read_local and sample_read see the original entry
 * (spo_unpatch_copy), so pins over the solver pass. FR27's solver redirect and the step chain's FR25 entry hook check the
 * raw entry and refuse while this holds it; this refuses an entry that is not original. Armed on the UI thread before
 * the first admitted frame; installed and restored with every other thread suspended (portable math's freeze) and none
 * inside the 5-byte window. Each gate tick checks the entry and the page: any change stops the gate with reason 197.
 * Windows only (Linux's solver is 0x1a4b9f0, not hooked here). */
#define SPO_STOP_REASON 197u
#define SPO_SOLVER 0x14e4320u
#define SPO_RESUME 0x14e4327u
#define SPO_SIZE 0x567u
#define SPO_SHA "20b7ad8293e574399b0f9478be79216349b1981a9c1a4faf7a9e4808153dde07"
#define SPO_BOX 0x70u
#define SPO_MAX_BOXES 65536u
#define SPO_PAGE_ANCHOR 0x14e0000u
#define SPO_STUB 0x00u  /* jmp [rip] -> spo_hook */
#define SPO_TRAMP 0x20u /* mov rax,rsp; mov [rax+0x18],rbx; jmp [rip] -> 0x14e4327 */
#define SPO_CODE 0x40u
static const unsigned char spo_original[7]={0x48,0x8b,0xc4,0x48,0x89,0x58,0x18};
typedef uint64_t (*spo_fn)(void *,float *,float,float *,float,uint64_t,uint64_t,float *,float);
static int spo_installed,spo_restored;
static unsigned char *spo_page;
static volatile LONG spo_any;   /* the entry may hold our bytes: the view is on */
static volatile LONG64 spo_calls,spo_sorted,spo_moved,spo_invalid,spo_max_n,spo_failures;
static const char *spo_stage="idle";

/* The order: the bytes the solver reads, as unsigned bytes. */
static int spo_compare(const unsigned char *a,const unsigned char *b) {
    const int head=memcmp(a,b,0x24);
    return head?head:memcmp(a+0x30,b+0x30,SPO_BOX-0x30);
}
/* Sort n boxes in place (insertion sort: stable and allocation free; the vectors are a few hundred boxes); returns
 * whether any box moved. */
static int spo_sort_boxes(unsigned char *begin,size_t n) {
    int moved=0;unsigned char held[SPO_BOX];
    for(size_t i=1;i<n;i++) {
        unsigned char *box=begin+i*SPO_BOX;
        if(spo_compare(box-SPO_BOX,box)<=0) continue;
        memcpy(held,box,SPO_BOX);size_t j=i;
        while(j && spo_compare(begin+(j-1)*SPO_BOX,held)>0) {memcpy(begin+j*SPO_BOX,begin+(j-1)*SPO_BOX,SPO_BOX);--j;}
        memcpy(begin+j*SPO_BOX,held,SPO_BOX);moved=1;
    }
    return moved;
}
/* The vector the solver gets: begin and end (16 bytes), whole boxes; refused vectors go to the solver unsorted. */
static void spo_sort_vector(void *vec) {
    uintptr_t be[2];
    if(!vec || !read_local(vec,be,16) || be[1]<be[0] || (be[1]-be[0])%SPO_BOX || (be[1]-be[0])/SPO_BOX>SPO_MAX_BOXES) {
        InterlockedIncrement64(&spo_invalid);return;
    }
    const size_t n=(be[1]-be[0])/SPO_BOX;
    if(n>1 && be[0]) {
        InterlockedIncrement64(&spo_sorted);
        if(spo_sort_boxes((unsigned char*)be[0],n)) InterlockedIncrement64(&spo_moved);
    }
    LONG64 now=spo_max_n;
    while((LONG64)n>now) {const LONG64 seen=InterlockedCompareExchange64(&spo_max_n,(LONG64)n,now);if(seen==now) break;now=seen;}
}
static uint64_t spo_hook(void *vec,float *p,float r,float *dir,float a5,uint64_t a6,uint64_t a7,float *move,float a9) {
    InterlockedIncrement64(&spo_calls);
    if(stats.active) spo_sort_vector(vec);
    return ((spo_fn)(void*)(spo_page+SPO_TRAMP))(vec,p,r,dir,a5,a6,a7,move,a9);
}

/* ---- the page and the entry ---- */
static void spo_jump_to(unsigned char *at,const void *target) {
    static const unsigned char jump[6]={0xff,0x25,0,0,0,0};memcpy(at,jump,6);memcpy(at+6,&target,8);
}
static void spo_code(unsigned char out[SPO_CODE]) {
    memset(out,0xcc,SPO_CODE);
    spo_jump_to(out+SPO_STUB,(const void*)spo_hook);
    memcpy(out+SPO_TRAMP,spo_original,7);spo_jump_to(out+SPO_TRAMP+7,(const void*)(image+SPO_RESUME));
}
static int spo_reach(const unsigned char *page) {
    const intptr_t d=(intptr_t)(page+SPO_STUB)-(intptr_t)(image+SPO_SOLVER+5);
    return d>INT32_MIN+0x2000 && d<INT32_MAX-0x2000;
}
static unsigned char *spo_alloc_page(void) {
    const uintptr_t at=(uintptr_t)(image+SPO_PAGE_ANCHOR)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            const uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            if(spo_reach(p)) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static int spo_write_page(void) {
    DWORD old,ignored;
    if(!VirtualProtect(spo_page,0x1000,PAGE_READWRITE,&old)) return 0;
    memset(spo_page,0xcc,0x1000);spo_code(spo_page);
    if(!VirtualProtect(spo_page,0x1000,PAGE_EXECUTE_READ,&ignored)) return 0;
    FlushInstructionCache(GetCurrentProcess(),spo_page,0x1000);return 1;
}
static int spo_page_ok(void) {
    unsigned char want[SPO_CODE];if(!spo_page) return 0;
    spo_code(want);return !memcmp(spo_page,want,SPO_CODE);
}
static void spo_patch_bytes(unsigned char out[5]) {
    const int32_t rel=(int32_t)((intptr_t)(spo_page+SPO_STUB)-(intptr_t)(image+SPO_SOLVER+5));
    out[0]=0xe9;memcpy(out+1,&rel,4);
}
/* The entry, raw: 0 original, 1 our jump, -1 anything else. */
static int spo_state(void) {
    unsigned char now[5],ours[5];memcpy(now,(const void*)(image+SPO_SOLVER),5);
    if(!memcmp(now,spo_original,5)) return 0;
    if(spo_page && spo_reach(spo_page)) {spo_patch_bytes(ours);if(!memcmp(now,ours,5)) return 1;}
    return -1;
}
static int spo_pins(void) {
    static unsigned char body[SPO_SIZE];
    return read_local(image+SPO_SOLVER,body,SPO_SIZE) && hash_ok(body,SPO_SIZE,SPO_SHA) && !memcmp(body,spo_original,7);
}
/* ---- original-byte view (lockstep_gate.c fp_patch_view) ---- */
static int spo_overlaps(const void *source,size_t bytes) {
    if(!*(volatile LONG*)&spo_any || !image || !source || !bytes) return 0;
    const uintptr_t a=(uintptr_t)source,lo=(uintptr_t)(image+SPO_SOLVER),hi=lo+5;
    return a<hi && a+bytes>lo;
}
static void spo_unpatch_copy(const void *source,void *copy,size_t bytes) {
    if(!spo_overlaps(source,bytes)) return;
    const uintptr_t a=(uintptr_t)source,lo=(uintptr_t)(image+SPO_SOLVER);
    for(unsigned i=0;i<5;i++) if(lo+i>=a && lo+i<a+bytes) ((unsigned char*)copy)[lo+i-a]=spo_original[i];
}
/* ---- install and restore: every other thread suspended, none inside [entry+1, entry+5) ---- */
static int spo_window_clear(void) {
    const uintptr_t entry=(uintptr_t)(image+SPO_SOLVER);
    for(unsigned i=0;i<pmath_frozen_count;i++) {
        CONTEXT c;memset(&c,0,sizeof(c));c.ContextFlags=CONTEXT_CONTROL;
        if(!GetThreadContext(pmath_frozen[i],&c)) return 0;
        if(c.Rip>entry && c.Rip<entry+5) return 0;
    }
    return 1;
}
/* 1 done, 0 refused or busy (nothing changed). */
static int spo_switch(int install) {
    unsigned char ours[5];spo_patch_bytes(ours);
    if(install) InterlockedExchange(&spo_any,1);
    for(unsigned attempt=0;attempt<200;attempt++) {
        if(!pmath_freeze()) break;
        if(!spo_window_clear()) {pmath_thaw();Sleep(1);continue;}
        const int ok=view_cas_bytes(SPO_SOLVER,install?spo_original:ours,install?ours:spo_original,5);
        pmath_thaw();
        if(ok && !install) InterlockedExchange(&spo_any,0);
        if(!ok && install) InterlockedExchange(&spo_any,0);
        return ok;
    }
    if(install) InterlockedExchange(&spo_any,0);
    return 0;
}
/* gate.safe_pos_order(): UI thread, before the first admitted frame. */
static int spo_arm(void) {
    if(spo_installed) return stats.active!=0;
    if(!stats.active || stats.issued || (boot_mode && boot_phase) || GetCurrentThreadId()!=ui_thread) {spo_stage="state";return 0;}
    if(!spo_pins()) {spo_stage="pins";return 0;}
    if(spo_state()!=0) {spo_stage="entry";return 0;} /* another module's patch (FR27, the step chain) */
    if(!spo_page) spo_page=spo_alloc_page();
    if(!spo_page || !spo_write_page() || !spo_page_ok()) {spo_stage="page";return 0;}
    spo_calls=spo_sorted=spo_moved=spo_invalid=spo_max_n=spo_failures=0;spo_restored=0;
    if(!spo_switch(1)) {spo_stage="busy";return 0;}
    spo_installed=1;spo_stage="armed";return 1;
}
static int spo_owned(void) {return !spo_installed || (spo_state()==1 && spo_page_ok());}
/* Gate tick: any change stops the gate with reason 197. */
static int spo_check(void) {
    if(InterlockedCompareExchange64(&spo_failures,0,0)) return 0;
    if(!spo_owned()) {spo_stage="site_changed";InterlockedIncrement64(&spo_failures);return 0;}
    return 1;
}
static void spo_restore(void) {
    if(!spo_installed) return;
    if(spo_state()!=1) {spo_stage="restore_foreign";return;}
    if(spo_switch(0)) {spo_installed=0;spo_restored=1;spo_stage="restored";}
    else spo_stage="restore_busy";
}
static int spo_status(char *out,size_t capacity) {
    const int n=snprintf(out,capacity,"installed=%d restored=%d stage=%s calls=%lld sorted=%lld moved=%lld invalid=%lld max_n=%lld failures=%lld",
        spo_installed,spo_restored,spo_stage,(long long)spo_calls,(long long)spo_sorted,(long long)spo_moved,(long long)spo_invalid,
        (long long)spo_max_n,(long long)spo_failures);
    return n>0 && (size_t)n<capacity?n:0;
}
