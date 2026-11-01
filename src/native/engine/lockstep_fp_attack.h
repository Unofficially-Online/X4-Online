/* FR29b, native_gate_fp_attack_capture = {first,last} (DEFAULT OFF, read-only; armed after native_gate_fp_redirect, whose
 * page holds the stubs; native_gate_fp_attack_watch = up to 32 saved IDs, needs native_gate_world_identities).
 *
 * Why: W14/L7 and FR28 split the Xenon corvette 07336a's shield at tick 546 by one binary32 ulp (W 0x439128e1, L
 * 0x439128e2). The step comes from out-of-sight combat (aiscripts fight.attack.object.fighter): get_attackstrength
 * (GetAttackStrength, Windows 0x4f43d0 / Linux 0x1d1b9d0) with its weapon loop (0x4f2c10 / 0x1deb960) gives binary64
 * damage values; apply_attackstrength rounds $result_hullshield to float and applies it (0x5209c0 / 0x17f8d90 ->
 * ApplyDamage). Every operation of that chain read statically was equal; what differs is an input at the call (the
 * distance at the current time, the thread RNG word, a weapon's damage) or an operation not read yet. This capture
 * shows which, call by call, on both executables.
 *
 * How: the calls of the chain go through hooks. Each hooked CALL SITE's rel32 leads to a stub on the fp redirect page
 * (Windows fr_page, Linux the thunk) whose jmp [rip] reaches a C hook with the callee's own arguments (the stub keeps
 * the caller's stack: stack arguments and the return address are where the callee expects them); the hook calls the
 * original callee once with the same arguments and returns its result. Only these call sites change, so no other
 * caller of these functions sees a hook (the damage add is a leaf some callers keep registers across: Windows
 * 0x81fc87 reads r8 after it). The sites (platform files list them; a Linux rel32 must lie in one 16-byte line):
 *   gas    the script action's calls of GetAttackStrength (Windows 0xbcda62, 0xbcdac7; Linux 0xe07346, 0xe077cb)
 *   wl     GetAttackStrength's call of the weapon loop (0x4f55f8 / 0x1d1c48b)
 *   dps    per-weapon damage (0xf1140 / 0xf05ff0) from the weapon loop and the drone path
 *   add    the damage struct's += (0xf0260 / 0xf05be0) in GetAttackStrength, the weapon loop, drones, missiles
 *   rng    Random::FloatState (0x14ddc90 / 0x1a491c0) in the weapon loop, drones (and Linux's inlined module path)
 *   dist   the object distance at a time (0x3e1160 / 0x1dea820) in GetAttackStrength
 *   apply  ApplyAttackStrengthAction's damage call (0x5209c0 / 0x17f8d90)
 * A gas call is captured when its tick lies in [first,last] and its attacker or target is watched (every call without
 * a watch list); the others only inside a captured gas call on the same thread (seq = order of entry within it), apply
 * by the same window and watch rule on its own (call=0). Recorded per call, all as bits: tick, thread, the thread's
 * game time and RNG word before and after (the draw's own state for rng), the attacker, target and object keys
 * (saved IDs; r<runtime> otherwise), the arguments that can differ (below, per kind) and the damage structures
 * (+0x8..+0x47: hull/shield/shieldonly/hullonly doubles, the maximum, the three factors, the flags) before and after.
 * Nothing is written to the game; nothing native is called but the original once. FP_ATTACK_ROW lines (fa_row), the
 * reader tools/dev/census_fp_attack.py aligns two clients' calls and prints the first differing input or operation.
 *
 * args (normalized so that both platforms print the same positions; a value only one platform passes is 0 on the other):
 *   gas    list4!=0, list5!=0, b7, b8, b9, i10, [Windows' float 11]
 *   wl     a6, a7 (double), a8 (float: distance - radius), a10, a11, a12, a14 (float), a15 (float), a17, a18, list4!=0,
 *          list5!=0, [Windows' a16 (double), Windows' a19]           obj = a13
 *   dps    the flag byte                                               obj = the weapon
 *   add    -                                                            label: the accumulator (gas.out1/2, wl.out1/2, local)
 *   rng    bound (float), result (float)
 *   dist   time (double), the third argument, the mode, result (float) obj, obj2 = the two objects
 *   apply  the flag byte                                               ret = the result byte
 * Platform part: fa_tls_rng() / fa_tls_time() (the calling thread's words, NULL when none), the site table and hooks, the
 * install (fa_arm_platform, fa_restore, fa_owned); this header the state, the capture, rows, status and Lua glue. */
#define FA_RECORDS 16384u
#define FA_IDS 32u
#define FA_THREADS 128u
#define FA_ARGS 16u
#define FA_BLOB 0x40u /* +0x8..+0x47 of a damage struct */
enum {FA_GAS=1,FA_WL,FA_DPS,FA_ADD,FA_RNG,FA_DIST,FA_APPLY,FA_KINDS};
enum {FA_IN_EXEC=0,FA_IN_GAS,FA_IN_WL,FA_IN_DRONES,FA_IN_MISSILES,FA_IN_APPLY,FA_SCOPES};
enum {FA_LABEL_NONE=0,FA_LABEL_GAS1,FA_LABEL_GAS2,FA_LABEL_WL1,FA_LABEL_WL2,FA_LABEL_LOCAL};
static const char *const fa_kind_names[FA_KINDS]={"?","gas","wl","dps","add","rng","dist","apply"};
static const char *const fa_scope_names[FA_SCOPES]={"exec","gas","wl","drones","missiles","apply"};
static const char *const fa_label_names[6]={"-","gas.out1","gas.out2","wl.out1","wl.out2","local"};
typedef struct {
    volatile LONG64 ready;
    uint64_t tick,call,seq;
    uint32_t kind,scope,thread,label,nargs,blobs; /* blobs: bit i = blob i present */
    uint64_t rt[4],key[4]; /* attacker, target, obj, obj2: runtime ID and saved key (0: none) */
    uint64_t time,rng_in,rng_out,ret;
    uint64_t args[FA_ARGS];
    unsigned char blob[4][FA_BLOB];
} fa_record;
typedef struct {
    unsigned depth;                       /* 1 while a captured gas call runs on this thread */
    uint64_t call,seq;
    uint64_t rt[2],key[2];                /* its attacker and target */
    const unsigned char *gas_out[2],*wl_out[2];
} fa_ctx;
static volatile LONG64 fa_thread_keys[FA_THREADS];
static fa_ctx fa_ctxs[FA_THREADS];
static fa_record fa_ring[FA_RECORDS];
static uint64_t fa_ids[FA_IDS],fa_first,fa_last;
static unsigned fa_id_count,fa_installed,fa_restored;
static const char *fa_stage="idle";
static volatile LONG64 fa_open; /* captured gas calls in progress, all threads: the sub-hooks' fast path */
static volatile LONG64 fa_records,fa_dropped,fa_calls_captured,fa_nested,fa_no_context,fa_outside,fa_unread;
static volatile LONG64 fa_unwound; /* captured gas calls an exception unwound (closed without a row; Windows) */
static volatile LONG64 fa_hits[FA_KINDS],fa_kept[FA_KINDS];
static uint64_t fa_scan,fa_emitted;
static unsigned fa_sites_hooked,fa_sites_skipped;
static uint64_t *fa_tls_rng(void);
static double *fa_tls_time(void);
static uint64_t *fa_test_rng;static double *fa_test_time; /* tests only: the thread's words (NULL: the real ones) */
static fa_ctx *fa_context(int claim) {
    const LONG64 key=(LONG64)GetCurrentThreadId()+1;
    for(unsigned i=0;i<FA_THREADS;i++) if(fa_thread_keys[i]==key) return &fa_ctxs[i];
    if(!claim) return NULL;
    for(unsigned i=0;i<FA_THREADS;i++)
        if(!fa_thread_keys[i] && InterlockedCompareExchange64(&fa_thread_keys[i],key,0)==0) return &fa_ctxs[i];
    return NULL;
}
/* An object's runtime ID (+8) and saved key (0 when it has none). */
static uint64_t fa_key(const void *object,uint64_t *runtime) {
    uint64_t r=0;unsigned klass=WORLD_KEY_NONE;*runtime=0;
    if(!object || !read_local((const unsigned char*)object+8,&r,8) || !r) return 0;
    *runtime=r;
    const uint64_t k=world_identity_installed?world_identity_key(r,&klass):0;
    return k && klass==WORLD_KEY_SAVED?k:0;
}
static int fa_watched(uint64_t key) {
    if(!key) return 0;
    for(unsigned i=0;i<fa_id_count;i++) if(fa_ids[i]==key) return 1;
    return 0;
}
static int fa_window(void) {
    const uint64_t t=stats.issued;
    return fa_installed && stats.active && t>=fa_first && t<=fa_last;
}
static void fa_blob(fa_record *r,unsigned i,const void *structure) {
    if(!structure || !read_local((const unsigned char*)structure+8,r->blob[i],FA_BLOB)) {InterlockedIncrement64(&fa_unread);return;}
    r->blobs|=1u<<i;
}
static void fa_words_in(fa_record *r) {
    const uint64_t *rng=fa_tls_rng();const double *time=fa_tls_time();
    if(rng) r->rng_in=*rng;
    if(time) memcpy(&r->time,time,8);
}
static void fa_words_out(fa_record *r) {const uint64_t *rng=fa_tls_rng();if(rng) r->rng_out=*rng;}
static void fa_put(fa_record *r) {
    InterlockedIncrement64(&fa_kept[r->kind<FA_KINDS?r->kind:0]);
    const LONG64 index=InterlockedIncrement64(&fa_records)-1;
    if(index>=(LONG64)FA_RECORDS) {InterlockedIncrement64(&fa_dropped);return;}
    fa_record *slot=&fa_ring[index];
    memcpy((unsigned char*)slot+sizeof(slot->ready),(const unsigned char*)r+sizeof(r->ready),sizeof(*r)-sizeof(r->ready));
    InterlockedExchange64(&slot->ready,index+1);
}
static void fa_begin_record(fa_record *r,unsigned kind,unsigned scope) {
    memset(r,0,sizeof(*r));r->kind=kind;r->scope=scope;r->tick=stats.issued;r->thread=GetCurrentThreadId()==ui_thread?0:1;
}
/* ---- The capture (the platform hooks call these around the original) ---- */
/* gas: the attacker, its two damage structs and the target; NULL when this call is not captured. */
static fa_ctx *fa_gas_enter(fa_record *r,const void *attacker,const void *target,const void *out1,const void *out2) {
    InterlockedIncrement64(&fa_hits[FA_GAS]);
    if(!fa_window()) return NULL;
    uint64_t art,trt;const uint64_t ak=fa_key(attacker,&art),tk=fa_key(target,&trt);
    if(fa_id_count && !fa_watched(ak) && !fa_watched(tk)) {InterlockedIncrement64(&fa_outside);return NULL;}
    fa_ctx *c=fa_context(1);
    if(!c) {InterlockedIncrement64(&fa_no_context);return NULL;}
    if(c->depth) {InterlockedIncrement64(&fa_nested);return NULL;}
    fa_begin_record(r,FA_GAS,FA_IN_EXEC);
    c->depth=1;c->call=(uint64_t)InterlockedIncrement64(&fa_calls_captured);c->seq=0;
    c->rt[0]=art;c->rt[1]=trt;c->key[0]=ak;c->key[1]=tk;
    c->gas_out[0]=(const unsigned char*)out1;c->gas_out[1]=(const unsigned char*)out2;c->wl_out[0]=c->wl_out[1]=NULL;
    r->call=c->call;r->seq=c->seq++;r->rt[0]=art;r->rt[1]=trt;r->key[0]=ak;r->key[1]=tk;
    fa_blob(r,0,out1);fa_blob(r,2,out2);fa_words_in(r);
    InterlockedIncrement64(&fa_open);
    return c;
}
static void fa_gas_exit(fa_ctx *c,fa_record *r,const void *out1,const void *out2) {
    fa_blob(r,1,out1);fa_blob(r,3,out2);fa_words_out(r);
    c->depth=0;c->gas_out[0]=c->gas_out[1]=c->wl_out[0]=c->wl_out[1]=NULL;
    InterlockedAdd64(&fa_open,-1);
    fa_put(r);
}
/* A call inside a captured gas call on this thread; NULL otherwise. */
static fa_ctx *fa_sub_enter(fa_record *r,unsigned kind,unsigned scope) {
    InterlockedIncrement64(&fa_hits[kind]);
    if(!InterlockedCompareExchange64(&fa_open,0,0)) return NULL;
    fa_ctx *c=fa_context(0);
    if(!c || !c->depth) return NULL;
    fa_begin_record(r,kind,scope);
    r->call=c->call;r->seq=c->seq++;r->rt[0]=c->rt[0];r->rt[1]=c->rt[1];r->key[0]=c->key[0];r->key[1]=c->key[1];
    fa_words_in(r);
    return c;
}
static void fa_sub_exit(fa_record *r) {fa_words_out(r);fa_put(r);}
static void fa_obj(fa_record *r,unsigned i,const void *object) {r->key[i]=fa_key(object,&r->rt[i]);}
static unsigned fa_label(const fa_ctx *c,const void *acc) {
    const unsigned char *a=(const unsigned char*)acc;
    if(a && a==c->gas_out[0]) return FA_LABEL_GAS1;
    if(a && a==c->gas_out[1]) return FA_LABEL_GAS2;
    if(a && a==c->wl_out[0]) return FA_LABEL_WL1;
    if(a && a==c->wl_out[1]) return FA_LABEL_WL2;
    return FA_LABEL_LOCAL;
}
/* The weapon loop: the platform passes its normalized arguments (FA_WL_ARGS of them) and its two structs. */
#define FA_WL_ARGS 14u
static fa_ctx *fa_wl_enter(fa_record *r,const void *out1,const void *out2,const void *a13,const uint64_t args[FA_WL_ARGS]) {
    fa_ctx *c=fa_sub_enter(r,FA_WL,FA_IN_GAS);
    if(!c) return NULL;
    c->wl_out[0]=(const unsigned char*)out1;c->wl_out[1]=(const unsigned char*)out2;
    fa_obj(r,2,a13);r->nargs=FA_WL_ARGS;memcpy(r->args,args,FA_WL_ARGS*8);
    fa_blob(r,0,out1);fa_blob(r,2,out2);
    return c;
}
static void fa_wl_exit(fa_ctx *c,fa_record *r,const void *out1,const void *out2) {
    fa_blob(r,1,out1);fa_blob(r,3,out2);c->wl_out[0]=c->wl_out[1]=NULL;fa_sub_exit(r);
}
/* A draw: rng_in/rng_out are the state the draw steps (the hook sets rng_in), not the thread's word. */
static void fa_rng_exit(fa_record *r,const uint64_t *state) {
    if(state) r->rng_out=*state;else fa_words_out(r);
    fa_put(r);
}
/* apply: captured on its own (call 0) by the window and the watch list. */
static int fa_apply_enter(fa_record *r,const void *attacker,const void *target,const void *damage) {
    InterlockedIncrement64(&fa_hits[FA_APPLY]);
    if(!fa_window()) return 0;
    uint64_t art,trt;const uint64_t ak=fa_key(attacker,&art),tk=fa_key(target,&trt);
    if(fa_id_count && !fa_watched(ak) && !fa_watched(tk)) return 0;
    fa_begin_record(r,FA_APPLY,FA_IN_APPLY);
    r->rt[0]=art;r->rt[1]=trt;r->key[0]=ak;r->key[1]=tk;
    fa_blob(r,0,damage);fa_words_in(r);
    return 1;
}
/* ---- Configuration, rows, status ---- */
static int fa_watch(uint64_t id) {
    if(!id) return 0;
    for(unsigned i=0;i<fa_id_count;i++) if(fa_ids[i]==id) return 1;
    if(fa_installed || fa_id_count>=FA_IDS) return 0;
    fa_ids[fa_id_count++]=id;return 1;
}
/* Shared arm checks: after fp_redirect, before the first admitted frame on the UI thread, with world identities. */
static int fa_arm_ready(uint64_t first,uint64_t last) {
    if(!first || first>last) {fa_stage="window";return 0;}
    if(!fr_installed || !stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) {fa_stage="refused";return 0;}
    if(!world_identity_installed) {fa_stage="needs";return 0;}
    fa_first=first;fa_last=last;fa_records=fa_dropped=fa_calls_captured=fa_nested=fa_no_context=fa_outside=fa_unread=fa_unwound=0;fa_open=0;
    for(unsigned k=0;k<FA_KINDS;k++) fa_hits[k]=fa_kept[k]=0;
    fa_scan=fa_emitted=0;
    return 1;
}
static int fa_status(char *out,size_t cap) {
    const int n=snprintf(out,cap,"installed=%u restored=%u stage=%s window=%llu..%llu ids=%u sites=%u skipped=%u captured=%lld nested=%lld no_context=%lld "
        "outside=%lld unread=%lld open=%lld records=%lld dropped=%lld rows=%llu"
        " hits=gas:%lld,wl:%lld,dps:%lld,add:%lld,rng:%lld,dist:%lld,apply:%lld kept=gas:%lld,wl:%lld,dps:%lld,add:%lld,rng:%lld,dist:%lld,apply:%lld"
        " unwound=%lld",
        fa_installed,fa_restored,fa_stage,(unsigned long long)fa_first,(unsigned long long)fa_last,fa_id_count,fa_sites_hooked,fa_sites_skipped,
        (long long)fa_calls_captured,(long long)fa_nested,(long long)fa_no_context,(long long)fa_outside,(long long)fa_unread,(long long)fa_open,
        (long long)(fa_records<(LONG64)FA_RECORDS?fa_records:(LONG64)FA_RECORDS),(long long)fa_dropped,(unsigned long long)fa_emitted,
        (long long)fa_hits[FA_GAS],(long long)fa_hits[FA_WL],(long long)fa_hits[FA_DPS],(long long)fa_hits[FA_ADD],(long long)fa_hits[FA_RNG],
        (long long)fa_hits[FA_DIST],(long long)fa_hits[FA_APPLY],(long long)fa_kept[FA_GAS],(long long)fa_kept[FA_WL],(long long)fa_kept[FA_DPS],
        (long long)fa_kept[FA_ADD],(long long)fa_kept[FA_RNG],(long long)fa_kept[FA_DIST],(long long)fa_kept[FA_APPLY],(long long)fa_unwound);
    return n>0 && (size_t)n<cap?n:0;
}
static char *fa_id_text(char *out,uint64_t key,uint64_t runtime) {
    if(key) snprintf(out,24,"%016llx",(unsigned long long)key);
    else if(runtime) snprintf(out,24,"r%llx",(unsigned long long)runtime);
    else memcpy(out,"-",2);
    return out;
}
static char *fa_blob_text(char *out,const fa_record *r,unsigned i) {
    static const char digits[]="0123456789abcdef";
    if(!((r->blobs>>i)&1u)) {memcpy(out,"-",2);return out;}
    for(unsigned k=0;k<FA_BLOB;k++) {out[2*k]=digits[r->blob[i][k]>>4];out[2*k+1]=digits[r->blob[i][k]&15];}
    out[2*FA_BLOB]=0;return out;
}
/* The index-th record in ring order (asked from 0, the probe's cursor; a record not yet complete ends the scan). */
static int fa_row(uint64_t index,char *out,size_t cap) {
    if(index!=fa_emitted || fa_scan>=FA_RECORDS) return 0;
    const LONG64 count=InterlockedCompareExchange64(&fa_records,0,0);
    if((LONG64)fa_scan>=count) return 0;
    const fa_record *r=&fa_ring[fa_scan];
    if(InterlockedCompareExchange64((volatile LONG64*)&r->ready,0,0)!=(LONG64)fa_scan+1) return 0;
    char ids[4][24],args[FA_ARGS*17+2],blobs[4][2*FA_BLOB+1];size_t at=0;
    args[0]='-';args[1]=0;
    for(unsigned k=0;k<r->nargs && k<FA_ARGS;k++) at+=(size_t)snprintf(args+at,sizeof(args)-at,"%s%llx",k?",":"",(unsigned long long)r->args[k]);
    const int n=snprintf(out,cap,"index=%llu tick=%llu call=%llu seq=%llu kind=%s in=%s thread=%s attacker=%s target=%s obj=%s obj2=%s label=%s "
        "time=%016llx rng=%016llx:%016llx ret=%llx args=%s b0=%s b1=%s b2=%s b3=%s",
        (unsigned long long)index,(unsigned long long)r->tick,(unsigned long long)r->call,(unsigned long long)r->seq,
        fa_kind_names[r->kind<FA_KINDS?r->kind:0],fa_scope_names[r->scope<FA_SCOPES?r->scope:0],r->thread?"other":"ui",
        fa_id_text(ids[0],r->key[0],r->rt[0]),fa_id_text(ids[1],r->key[1],r->rt[1]),fa_id_text(ids[2],r->key[2],r->rt[2]),
        fa_id_text(ids[3],r->key[3],r->rt[3]),fa_label_names[r->label<6?r->label:0],(unsigned long long)r->time,
        (unsigned long long)r->rng_in,(unsigned long long)r->rng_out,(unsigned long long)r->ret,args,
        fa_blob_text(blobs[0],r,0),fa_blob_text(blobs[1],r,1),fa_blob_text(blobs[2],r,2),fa_blob_text(blobs[3],r,3));
    if(n<=0 || (size_t)n>=cap) return 0;
    ++fa_scan;++fa_emitted;return n;
}
