/* Physics shape refresh on the Linux X4 9.00 executable: the Linux twin of lockstep_shape.h's fixture path
 * (native_gate_shape_diag, _shape_rng, _shape_initial_refresh; boot options 1048576, 2097152, 4194304 as on Windows)
 * and of lockstep_region_camera.h's shape_refresh group (native_gate_shape_refresh = 'view'). Included by gate_linux.c
 * only, after gate_battle.h and gate_mining_camera.h.
 *
 * Each physics step calls XPhys::Body_UpdateShape per body record (Windows 0xbff80; record +0 the owner object, +0x20
 * the shape holder, whose +0x28 is the next refresh's due time). Once the due time has passed it moves the compound's
 * children to their parts' poses (NotifyShapeChanged) and sets the next due time to now + an interval from the
 * owner's distance to the camera: clamp((distance - size) / size, 0.5, 5) - Random::FloatState(word, 0.1), size the
 * compound's bounds diagonal. On Linux 0xee9760 is that function with Windows' 0xc2c00 and 0xc2810 inlined: the due
 * test at 0xee97fa, the interval's macro test at 0xee9850 (xmm0 = 0 when the macro's +0x278 is set), then the je at
 * 0xee9865 into the distance path 0xee9ac8, which comes back to 0xee986b with the interval (double) in xmm0.
 *
 * lx60a against W36m: every fixture body's due time apart from the first row on (Windows re-bases them at its first
 * call and draws the interval from keyed words; Linux kept the loading-time values, all within 12.02..12.15 s), and the
 * first refreshes at ticks 145..150 parted the positions of actors 4, 5 and 11 at tick 150 by a few ulps.
 *
 * Two patches, one wrapper:
 *  - the entry (16-aligned, 0xee9760: mov rax,[rdi+0x20]; test rax,rax; je ret) becomes a jmp rel32 to a page near the
 *    image whose jmp [rip] enters lxsh_update(record, flag); the original runs through a trampoline (the same three
 *    instructions, a ret for no holder, else jmp [rip] -> 0xee976d with rax the holder). No branch lands in
 *    0xee9761..0xee976c. In an admitted frame on the UI thread a fixture actor's record (gate_battle.h: its ship, the
 *    record at the ship's first or second body slot) takes Windows' shape_move: a call ordinal per actor, record and
 *    tick; _initial_refresh sets the due time to time - dt at the first call per holder (its pointer and native ID at
 *    +8); _rng runs the call on actor_seed("SHAPE_UP", tick, actor, which, ordinal, 0) and gives the word back. Then,
 *    for every call with shape_refresh armed and latched, Windows' rc_shape_update: the first call per owner key (saved
 *    or created) sets the due time to the thread's game time.
 *  - the je's rel32 (one aligned 16-byte compare-exchange) points at a stub that calls lxsh_interval(holder rbp, owner
 *    r12): Windows' rc_shape_interval on the latched point (the player object's ship, as rc_shape_latch without view
 *    points: W36 latched one view point, that ship): no point in the owner's sector 5 s, else the clamp above from
 *    GetTransformRelativeTo at the thread's game time in the sector (exact square roots) and the bounds (shape vtable
 *    +0x20, Windows +0x18), minus FloatState (0x1a491c0) on a word keyed by (tick, owner key) instead of the thread's;
 *    then 0xee986b with the interval. Before the first latch it takes the native path.
 * Rows: gate.shape(actor, which) in Windows' text. Stops as on Windows: 45 (the install refused), 46 (a failure of the
 * fixture path), 165 (the shape key table full); the per-tick check (the patches as ours, the pin) stops with 211. */
#define LXSH_STOP_INSTALL 45u
#define LXSH_STOP_FAILURE 46u
#define LXSH_STOP_KEYS 165u
#define LXSH_STOP_CHECK 211u
#define LXSH_UPDATE_VA 0xee9760u  /* Body_UpdateShape (Windows 0xbff80) */
#define LXSH_RESUME_VA 0xee976du  /* after the entry's mov, test, je */
#define LXSH_JE_VA 0xee9865u      /* je -> the distance path */
#define LXSH_DIST_VA 0xee9ac8u    /* the native interval from the camera */
#define LXSH_DUE_VA 0xee986bu     /* addsd xmm0,[now]: the due time from the interval */
#define LXSH_FLOAT_VA 0x1a491c0u  /* Random::FloatState(range) on the thread's word (Windows 0x14ddc90) */
#define LXSH_ACTORS 12u
#define LXSH_DOMAIN 0x53484150455f5550ULL    /* "SHAPE_UP", Windows' shape_execute */
#define LXSH_RC_DOMAIN 0x5348415045445545ULL /* "SHAPEDUE", Windows' rc_shape_interval */
#define LXSH_KEYS 262144u
#define LXSH_HOOK 0x00u   /* jmp [rip] -> lxsh_update */
#define LXSH_TRAMP 0x20u  /* mov rax,[rdi+0x20]; test rax,rax; jne +1; ret; jmp [rip] -> 0xee976d */
#define LXSH_STUB 0x40u   /* the interval stub */
#define LXSH_STUB_BYTES 80u /* 66 used */
static const unsigned char lxsh_entry_original[5]={0x48,0x8b,0x47,0x20,0x48};
typedef void (*lxsh_fn)(void *,unsigned char);
typedef struct {uint64_t tick,calls,total_calls,time,due_before,due_after,native_due_after,rng_before,rng_after,init_total,last_init;unsigned flag,initialized;} lxsh_sample;
typedef struct {void *holder;uint64_t id;} lxsh_holder_key;
static lxsh_sample lxsh_samples[LXSH_ACTORS][2];
static lxsh_holder_key lxsh_initialized[LXSH_ACTORS][2];
static unsigned char *lxsh_page;
static int lxsh_installed,lxsh_restored,lxsh_entry_on,lxsh_je_on,lxsh_seeded,lxsh_initial,lxsh_fixture,lxsh_rc_armed;
static uint64_t lxsh_calls,lxsh_inits;
static volatile int64_t lxsh_failures,lxsh_rc_failures;
static const char *lxsh_stage="idle";
/* shape_refresh: the latched point and its sector (-1 before the first latch), the aligned keys, the counters. */
static void *volatile lxsh_point,*volatile lxsh_sector;
static volatile long lxsh_count=-1;
static volatile int64_t lxsh_keys[LXSH_KEYS];
static volatile int64_t lxsh_update_calls,lxsh_interval_calls,lxsh_aligned,lxsh_unmapped,lxsh_near,lxsh_far,lxsh_native,lxsh_unkeyed;
static lxsh_fn lxsh_original_override;        /* tests: the trampoline */
static float (*lxsh_float_override)(float);   /* tests: FloatState */
typedef void (*lxsh_bounds_fn)(float *,void *); /* GetLocalBounds: the AABox through the hidden pointer */
static unsigned lxb_fixture_record(void *record,unsigned *which); /* gate_battle.h: the actor 1..12, else 0 */
static double lxsh_now(void) {
    unsigned char *base=gate_tls_self();double now=NAN;
    if(base) memcpy(&now,gate_tls_time(base),8);
    return now;
}
/* 1 when the key is new (now recorded), 0 when seen, -1 when the table is full. */
static int lxsh_key_first(uint64_t key) {
    unsigned h=(unsigned)(lockstep_mix(key)&(LXSH_KEYS-1u));
    for(unsigned n=0;n<LXSH_KEYS/2u;n++,h=(h+1u)&(LXSH_KEYS-1u)) {
        int64_t expected=0;
        if(__atomic_compare_exchange_n(&lxsh_keys[h],&expected,(int64_t)key,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST)) return 1;
        if((uint64_t)expected==key) return 0;
    }
    return -1;
}
static uint64_t lxsh_owner_key(const unsigned char *owner) {
    uint64_t runtime=0;unsigned klass;
    return owner && gate_read_local(owner+8,&runtime,8) && runtime?world_identity_key(runtime,&klass):0;
}
/* Windows' rc_shape_update before the refresh: the first call per owner key aligns the due time. */
static void lxsh_align(unsigned char *holder,unsigned char *owner) {
    __atomic_add_fetch(&lxsh_update_calls,1,__ATOMIC_SEQ_CST);
    if(!lxsh_rc_armed || !holder || !owner || __atomic_load_n(&lxsh_count,__ATOMIC_SEQ_CST)<0) return;
    const uint64_t key=lxsh_owner_key(owner);
    if(!key) {__atomic_add_fetch(&lxsh_unmapped,1,__ATOMIC_SEQ_CST);return;}
    const int first=lxsh_key_first(key);
    if(first<0) {lxsh_stage="shape_keys";__atomic_add_fetch(&lxsh_rc_failures,1,__ATOMIC_SEQ_CST);return;}
    if(first) {const double now=lxsh_now();memcpy(holder+0x28,&now,8);__atomic_add_fetch(&lxsh_aligned,1,__ATOMIC_SEQ_CST);}
}
static void lxsh_run(void *record,unsigned char flag) {
    unsigned char *holder=NULL,*owner=NULL;
    if(gate_read_local((unsigned char*)record+0x20,&holder,8) && holder && gate_read_local(record,&owner,8)) lxsh_align(holder,owner);
    (lxsh_original_override?lxsh_original_override:(lxsh_fn)(void*)(lxsh_page+LXSH_TRAMP))(record,flag);
}
static int lxsh_holder_seen(unsigned actor,const void *holder,uint64_t id) {
    for(unsigned i=0;i<2;i++) if(lxsh_initialized[actor-1][i].holder==holder && lxsh_initialized[actor-1][i].id==id) return 1;
    return 0;
}
/* Windows' shape_move and shape_execute for a fixture actor's record (no dock records here). */
static void lxsh_fixture_run(void *record,unsigned char flag,unsigned actor,unsigned which) {
    lxsh_sample *s=&lxsh_samples[actor-1][which];unsigned char *holder=NULL,*base=gate_tls_self();
    if(s->tick!=stats.issued) {s->tick=stats.issued;s->calls=0;}
    ++s->calls;++s->total_calls;++lxsh_calls;s->flag=flag;
    uint64_t *rng=base?gate_tls_rng(base):NULL;double time=lxsh_now();
    if(!rng || !isfinite(time) || !gate_read_local((unsigned char*)record+0x20,&holder,8)) {
        __atomic_add_fetch(&lxsh_failures,1,__ATOMIC_SEQ_CST);lxsh_run(record,flag);return;
    }
    memcpy(&s->time,&time,8);
    const uint64_t saved=*rng;s->initialized=0;s->due_before=s->due_after=s->native_due_after=0;
    if(holder) {
        uint64_t holder_id=0;
        if(!gate_read_local(holder+8,&holder_id,8) || !holder_id) {__atomic_add_fetch(&lxsh_failures,1,__ATOMIC_SEQ_CST);lxsh_run(record,flag);return;}
        memcpy(&s->due_before,holder+0x28,8);
        if(lxsh_initial && !lxsh_holder_seen(actor,holder,holder_id)) {
            const double due=time-gate_tick_dt();memcpy(holder+0x28,&due,8);
            s->initialized=1;++lxsh_inits;++s->init_total;s->last_init=stats.issued;
        }
        if(lxsh_initial) {lxsh_initialized[actor-1][which].holder=holder;lxsh_initialized[actor-1][which].id=holder_id;}
    }
    if(lxsh_seeded) *rng=lockstep_seed(LXSH_DOMAIN,stats.issued,actor,which,s->calls,0); /* actor_seed: the fixture's number */
    s->rng_before=*rng;
    lxsh_run(record,flag); /* X4's exceptions end the process: nothing to unwind */
    s->rng_after=*rng;
    if(lxsh_seeded) *rng=saved;
    if(holder) {memcpy(&s->native_due_after,holder+0x28,8);memcpy(&s->due_after,holder+0x28,8);}
}
static void lxsh_update(void *record,unsigned char flag) {
    unsigned which=0;
    const unsigned actor=lxsh_fixture && stats.active && boot_phase==3 && GetCurrentThreadId()==ui_thread?lxb_fixture_record(record,&which):0;
    if(actor) {lxsh_fixture_run(record,flag,actor,which);return;}
    lxsh_run(record,flag);
}
/* Windows' rc_shape_interval: 1 with *out the interval, 0 for the native path (before the first latch). */
static float lxsh_float(float range) {
    return lxsh_float_override?lxsh_float_override(range):((float (*)(float))(void*)LX(LXSH_FLOAT_VA))(range);
}
static int lxsh_interval(void *holder,void *owner,double *out) {
    __atomic_add_fetch(&lxsh_interval_calls,1,__ATOMIC_SEQ_CST);
    const long count=__atomic_load_n(&lxsh_count,__ATOMIC_SEQ_CST);
    if(!lxsh_rc_armed || count<0) {__atomic_add_fetch(&lxsh_native,1,__ATOMIC_SEQ_CST);return 0;}
    if(!owner || !holder) {*out=0.5;return 1;}
    unsigned char *o=owner,*parent=NULL;
    void *sector=gate_read_local(o+0x70,&parent,8) && parent?lxmc_ancestor(parent,0x58):NULL;
    const double now=lxsh_now();float best=-1.0f;
    void *point=lxsh_point;
    if(sector && count>0 && point && lxsh_sector==sector) {
        float a[16] __attribute__((aligned(16))),b[16] __attribute__((aligned(16)));
        lxmc_gtr(point,a,now,sector);lxmc_gtr(owner,b,now,sector);
        const float dx=a[0]-b[0],dy=a[1]-b[1],dz=a[2]-b[2],d=sqrtf(dx*dx+dy*dy+dz*dz);
        best=d;
    }
    if(best<0) {__atomic_add_fetch(&lxsh_far,1,__ATOMIC_SEQ_CST);*out=5.0;return 1;}
    __atomic_add_fetch(&lxsh_near,1,__ATOMIC_SEQ_CST);
    unsigned char *body=NULL;void *shape=NULL;void **vtable=NULL;lxsh_bounds_fn bounds=NULL;
    if(!gate_read_local((unsigned char*)holder+0x10,&body,8) || !body || !gate_read_local(body+8,&shape,8) || !shape ||
       !gate_read_local(shape,&vtable,8) || !vtable || !gate_read_local((unsigned char*)vtable+0x20,&bounds,8) || !bounds) {
        lxsh_stage="bounds";__atomic_add_fetch(&lxsh_rc_failures,1,__ATOMIC_SEQ_CST);return 0;
    }
    float box[8] __attribute__((aligned(16)));bounds(box,shape);
    const float ex=box[4]-box[0],ey=box[5]-box[1],ez=box[6]-box[2],size=sqrtf(ex*ex+ey*ey+ez*ez);
    if(!(size>=0.0001f)) {*out=0.5;return 1;}
    float v=(best-size)/size;
    if(v>5.0f) v=5.0f;
    if(!(v>0.5f)) v=0.5f; /* maxss: 0.5 also for NaN */
    const uint64_t key=lxsh_owner_key(o);
    if(!key) __atomic_add_fetch(&lxsh_unkeyed,1,__ATOMIC_SEQ_CST);
    unsigned char *base=gate_tls_self();uint64_t *word=base?gate_tls_rng(base):NULL;
    if(!word) {lxsh_stage="tls";__atomic_add_fetch(&lxsh_rc_failures,1,__ATOMIC_SEQ_CST);return 0;}
    const uint64_t saved=*word;*word=lockstep_seed(LXSH_RC_DOMAIN,stats.issued,key,0,0,0);
    const float r=lxsh_float(0.1f);
    *word=saved;
    *out=(double)(v-r);return 1;
}
/* Every admitted tick (region_camera_frame): the player object's ship, else the player object, and its sector. */
static void lxsh_latch(void) {
    unsigned char *universe=NULL;void *player=NULL,*parent=NULL,*point=NULL;long n=0;
    if(gate_read_local(LX(LX_UNIVERSE_VA),&universe,8) && universe && gate_read_local(universe+0x238,&player,8) && player &&
       gate_read_local((unsigned char*)player+0x70,&parent,8)) {
        point=parent?lxmc_ancestor(parent,0x49):NULL;if(!point) point=player;n=1;
    }
    __atomic_store_n(&lxsh_count,0,__ATOMIC_SEQ_CST);
    lxsh_point=point;lxsh_sector=point?lxmc_ancestor(point,0x58):NULL;
    __atomic_store_n(&lxsh_count,n,__ATOMIC_SEQ_CST);
}
/* sub rsp,16; mov rdi,rbp; mov rsi,r12; mov rdx,rsp; movabs rax,lxsh_interval; call rax; movsd xmm0,[rsp]; add rsp,16;
 * test eax,eax; je native; jmp [rip] -> the due store; native: jmp [rip] -> the distance path. */
static void lxsh_stub_bytes(unsigned char out[LXSH_STUB_BYTES]) {
    static const unsigned char head[]={0x48,0x83,0xec,0x10, 0x48,0x89,0xef, 0x4c,0x89,0xe6, 0x48,0x89,0xe2, 0x48,0xb8};
    static const unsigned char mid[]={0xff,0xd0, 0xf2,0x0f,0x10,0x04,0x24, 0x48,0x83,0xc4,0x10, 0x85,0xc0, 0x74,0x0e};
    const void *fn=(void*)lxsh_interval,*due=LX(LXSH_DUE_VA),*dist=LX(LXSH_DIST_VA);unsigned n=0;
    memset(out,0xcc,LXSH_STUB_BYTES);
    memcpy(out+n,head,sizeof(head));n+=sizeof(head);memcpy(out+n,&fn,8);n+=8;
    memcpy(out+n,mid,sizeof(mid));n+=sizeof(mid);
    lxw_jump_write(out+n,due);n+=14;lxw_jump_write(out+n,dist);
}
static void lxsh_page_bytes(unsigned char *page) {
    memset(page,0xcc,0x100);
    lxw_jump_write(page+LXSH_HOOK,(void*)lxsh_update);
    static const unsigned char tramp[]={0x48,0x8b,0x47,0x20, 0x48,0x85,0xc0, 0x75,0x01, 0xc3};
    memcpy(page+LXSH_TRAMP,tramp,sizeof(tramp));lxw_jump_write(page+LXSH_TRAMP+sizeof(tramp),LX(LXSH_RESUME_VA));
    lxsh_stub_bytes(page+LXSH_STUB);
}
static void lxsh_entry_bytes(unsigned char out[5]) {
    const int32_t d=(int32_t)((intptr_t)(lxsh_page+LXSH_HOOK)-(intptr_t)(LX(LXSH_UPDATE_VA)+5));out[0]=0xe9;memcpy(out+1,&d,4);
}
static int32_t lxsh_je_rel(int ours) {
    const unsigned char *to=ours?lxsh_page+LXSH_STUB:LX(LXSH_DIST_VA);
    return (int32_t)((intptr_t)to-(intptr_t)(LX(LXSH_JE_VA)+6));
}
/* 0 native, 1 ours, -1 anything else. */
static int lxsh_entry_state(void) {
    unsigned char b[5],want[5];
    if(!gate_read_local(LX(LXSH_UPDATE_VA),b,5)) return -1;
    if(!memcmp(b,lxsh_entry_original,5)) return 0;
    if(lxsh_page) {lxsh_entry_bytes(want);if(!memcmp(b,want,5)) return 1;}
    return -1;
}
static int lxsh_je_state(void) {
    unsigned char b[6];int32_t rel;
    if(!gate_read_local(LX(LXSH_JE_VA),b,6) || b[0]!=0x0f || b[1]!=0x84) return -1;
    memcpy(&rel,b+2,4);
    if(rel==lxsh_je_rel(0)) return 0;
    if(lxsh_page && rel==lxsh_je_rel(1)) return 1;
    return -1;
}
/* The whole function as found, with both sites read as the original. */
static int lxsh_pins(void) {
    const lx_site *s=site_named("shape_update");static unsigned char copy[0x700];
    if(!s || s->va!=LXSH_UPDATE_VA || s->length>sizeof(copy) || lxsh_entry_state()<0 || lxsh_je_state()<0 ||
       !gate_read_local(LX(s->va),copy,s->length) || !site_ok(site_named("build_rng_float_range"))) return 0;
    memcpy(copy,lxsh_entry_original,5);
    {const int32_t rel=lxsh_je_rel(0);memcpy(copy+(LXSH_JE_VA-LXSH_UPDATE_VA)+2,&rel,4);}
    return hash_ok(copy,s->length,s->sha256);
}
static int lxsh_line_swap(uint32_t va,unsigned at,const unsigned char *from,const unsigned char *to,unsigned n) {
    unsigned char *line=(unsigned char*)((uintptr_t)LX(va)&~(uintptr_t)15);const unsigned off=(unsigned)(LX(va)-line)+at;
    unsigned __int128 expected,desired;memcpy(&expected,line,16);desired=expected;
    if(off+n>16 || memcmp((unsigned char*)&expected+off,from,n)) return 0;
    memcpy((unsigned char*)&desired+off,to,n);
    return gate_code_swap128(line,expected,desired);
}
static int lxsh_page_ready(void) {
    if(!lxsh_page) lxsh_page=gate_near_page(LX(LXSH_UPDATE_VA));
    if(!lxsh_page) return 0;
    const intptr_t d=(intptr_t)lxsh_page-(intptr_t)LX(LXSH_UPDATE_VA);
    if(d<-0x70000000ll || d>0x70000000ll || mprotect(lxsh_page,GATE_PAGE,PROT_READ|PROT_WRITE)) return 0;
    lxsh_page_bytes(lxsh_page);
    return !mprotect(lxsh_page,GATE_PAGE,PROT_READ|PROT_EXEC);
}
static int lxsh_entry_install(void) {
    if(lxsh_entry_on) return 1;
    if(lxsh_entry_state()!=0 || !lxsh_page_ready()) return 0;
    unsigned char to[5];lxsh_entry_bytes(to);
    if(!lxsh_line_swap(LXSH_UPDATE_VA,0,lxsh_entry_original,to,5)) return 0;
    lxsh_entry_on=1;return 1;
}
/* The fixture path, in the boot (Windows shape_install): the entry hook. */
static int lxsh_install(int seed,int initial) {
    if(lxsh_fixture) return seed==lxsh_seeded && initial==lxsh_initial;
    if(!boot_mode || !lxsh_pins() || !lxsh_entry_install()) {lxsh_stage="install";return 0;}
    memset(lxsh_samples,0,sizeof(lxsh_samples));memset(lxsh_initialized,0,sizeof(lxsh_initialized));
    lxsh_calls=lxsh_inits=0;lxsh_failures=0;lxsh_seeded=seed;lxsh_initial=initial;lxsh_fixture=1;lxsh_installed=1;lxsh_restored=0;
    lxsh_stage="installed";return 1;
}
/* region_camera('shape_refresh'): before the first admitted frame, on the MoveWorker; the entry hook (shared) and the je. */
static int lxsh_rc_arm(void) {
    if(lxsh_rc_armed) return stats.active;
    if(!stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) {lxsh_stage="guard";return 0;}
    if(!lxsh_pins() || !site_ok(site_named("warp_gtrt")) || lxsh_je_state()!=0) {lxsh_stage="pins";return 0;}
    if(!lxsh_entry_install()) {lxsh_stage="entry";return 0;}
    unsigned char from[4],to[4];{int32_t r=lxsh_je_rel(0);memcpy(from,&r,4);r=lxsh_je_rel(1);memcpy(to,&r,4);}
    if(!lxsh_line_swap(LXSH_JE_VA,2,from,to,4)) {lxsh_stage="je";return 0;}
    memset((void*)lxsh_keys,0,sizeof(lxsh_keys));__atomic_store_n(&lxsh_count,-1,__ATOMIC_SEQ_CST);lxsh_point=lxsh_sector=NULL;
    lxsh_update_calls=lxsh_interval_calls=lxsh_aligned=lxsh_unmapped=lxsh_near=lxsh_far=lxsh_native=lxsh_unkeyed=0;lxsh_rc_failures=0;
    lxsh_je_on=1;lxsh_rc_armed=1;lxsh_installed=1;lxsh_restored=0;lxsh_stage="armed";return 1;
}
static int lxsh_owned(void) {
    if(!lxsh_installed) return 1;
    unsigned char want[LXSH_STUB_BYTES];lxsh_stub_bytes(want);
    return (!lxsh_entry_on || lxsh_entry_state()==1) && (!lxsh_je_on || lxsh_je_state()==1) && lxsh_page &&
           !memcmp(lxsh_page+LXSH_STUB,want,LXSH_STUB_BYTES) && lxsh_pins();
}
/* Main-thread tick check: 0 with the stop reason. */
static unsigned lxsh_check(void) {
    if(!lxsh_owned()) {lxsh_stage="changed";return LXSH_STOP_CHECK;}
    if(lxsh_rc_failures) return LXSH_STOP_KEYS;
    if(lxsh_fixture && lxsh_failures) return LXSH_STOP_FAILURE;
    return 0;
}
static void lxsh_restore(void) {
    if(!lxsh_installed) return;
    int all=1;
    if(lxsh_je_on) {
        unsigned char from[4],to[4];{int32_t r=lxsh_je_rel(1);memcpy(from,&r,4);r=lxsh_je_rel(0);memcpy(to,&r,4);}
        if(lxsh_je_state()==1 && lxsh_line_swap(LXSH_JE_VA,2,from,to,4)) lxsh_je_on=0;else all=0;
    }
    if(lxsh_entry_on) {
        unsigned char from[5];lxsh_entry_bytes(from);
        if(lxsh_entry_state()==1 && lxsh_line_swap(LXSH_UPDATE_VA,0,from,lxsh_entry_original,5)) lxsh_entry_on=0;else all=0;
    }
    lxsh_rc_armed=0;
    if(all) {lxsh_installed=0;lxsh_fixture=0;lxsh_restored=1;lxsh_stage="restored";} /* the page stays: a thread may be inside it */
}
static int lxsh_text(unsigned actor,unsigned which,char *out,size_t size) {
    if(!lxsh_fixture || actor<1 || actor>LXSH_ACTORS || which>1) return 0;
    const lxsh_sample *s=&lxsh_samples[actor-1][which];
    const int n=snprintf(out,size,"tick=%llu calls=%llu time=%016llx flag=%u due_before=%016llx due_after=%016llx initialized=%u rng_before=%016llx rng_after=%016llx init_total=%llu last_init=%llu",
        (unsigned long long)s->tick,(unsigned long long)s->calls,(unsigned long long)s->time,s->flag,(unsigned long long)s->due_before,
        (unsigned long long)s->due_after,s->initialized,(unsigned long long)s->rng_before,(unsigned long long)s->rng_after,
        (unsigned long long)s->init_total,(unsigned long long)s->last_init);
    return n>0 && (size_t)n<size?n:0;
}
/* Windows' shape_init_totals: actor/which/init_total for every fixture record. */
static int lxsh_init_totals(char *out,size_t size) {
    int n=0;out[0]=0;
    for(unsigned i=0;i<LXSH_ACTORS;i++) for(unsigned j=0;j<2;j++) {
        const int k=snprintf(out+n,size-(size_t)n,"%s%u/%u/%llu",n?",":"",i+1,j,(unsigned long long)lxsh_samples[i][j].init_total);
        if(k<0 || (size_t)(n+k)>=size) return n;
        n+=k;
    }
    return n;
}
static int lxsh_rc_status(char *out,size_t size) {
    const int n=snprintf(out,size,"shape_refresh=%lld/%lld aligned=%lld unmapped=%lld near=%lld far=%lld shape_native=%lld unkeyed=%lld latched=%ld failures=%lld stage=%s",
        (long long)lxsh_update_calls,(long long)lxsh_interval_calls,(long long)lxsh_aligned,(long long)lxsh_unmapped,(long long)lxsh_near,
        (long long)lxsh_far,(long long)lxsh_native,(long long)lxsh_unkeyed,__atomic_load_n(&lxsh_count,__ATOMIC_SEQ_CST),(long long)lxsh_rc_failures,lxsh_stage);
    return n>0 && (size_t)n<size?n:0;
}
static int lua_shape(lua_State *L) {
    const double actor=p_lua_tonumber(L,1),which=p_lua_tonumber(L,2);char out[512];
    const int n=accept_ui() && isfinite(actor) && actor>=1 && actor<=LXSH_ACTORS && floor(actor)==actor && (which==0 || which==1)?
        lxsh_text((unsigned)actor,(unsigned)which,out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
