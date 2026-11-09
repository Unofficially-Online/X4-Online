/* native_gate_impulse_diag (Linux gate only, diagnostic, default off): the impulses X4 gives a fixture ship's physics
 * bodies, with the body as Jolt sees it before and its velocities after, as IMPULSE rows. Included by gate_linux.c after
 * gate_body_step.h.
 *
 * Why: at the battle's tick 28 a fixture ship's body0 gets two velocity changes before the Jolt step, each followed by
 * the record copy-back into the navigation controller (Linux 0xee8150, Windows 0xbf27d); the first is bit-identical to
 * Windows, the second is not (W36j against lx50a). Both change linear and angular velocity, which of X4's three
 * body-0 actions (0x17e2e60: AddTorque, 0x17e2f10: AddImpulse at a point, 0x17e3000: AddAngularImpulse) only the
 * impulse at a point does. These rows show whether the split is in the impulse and point X4 computes or in Jolt's
 * application (v += impulse * inverse mass; w += world inverse inertia * ((point - position) x impulse)).
 *
 * JPH::BodyInterface::AddImpulse(BodyID const&, Vec3 impulse, Vec3 point) is 0x1a7fc90 (Windows inlines
 * Body::AddImpulse, 0x141405910). Its first 14 bytes (push r13; movq r13,xmm1; push r12; movq r12,xmm0: whole
 * instructions, nothing RIP-relative) become jmp [rip] -> lxai_add in one aligned 16-byte compare-exchange (bytes 14..15,
 * push rbp and the start of mov rbp,rdi, stay); the original runs through a trampoline (the 14 bytes, then jmp [rip] ->
 * 0x1a7fc9e). A call on a fixture ship's body (matched by BodyID) records, in the shared order (gate_body_step.h), the
 * caller, the impulse, the point, the body's pose, its MotionProperties' first 0x60 bytes (velocities, inverse inertia
 * and its rotation, force, torque, inverse mass) and its velocities after. Pinned (body_add_impulse); the per-tick
 * check (stop reason 206) and the restore are this file's. */
#define LXAI_STOP_REASON 206u
#define LXAI_VA 0x1a7fc90u
#define LXAI_MAX 8u
#define LXAI_HELPER_VA 0x17e2f10u
#define LXAI_HELPER_SIZE 0xf0u
#define LXAI_HELPER_RETURN 0x17e2f9cu
#define LXAI_OUTER_RETURN_OFFSET 0x38u
typedef struct {float v[4];} lxai_vec;
typedef void (*lxai_fn)(void *iface,const void *id,lxai_vec impulse,lxai_vec point);
static const unsigned char lxai_prologue[14]={0x41,0x55,0x66,0x49,0x0f,0x7e,0xcd,0x41,0x54,0x66,0x49,0x0f,0x7e,0xc4};
static unsigned char lxai_original[16],*lxai_page;
static int lxai_installed,lxai_restored;
static volatile int64_t lxai_calls,lxai_recorded,lxai_failures;
static volatile int64_t lxai_caller_known;
static volatile int64_t lxai_caller_unknown;
static volatile int64_t lxai_caller_read_failed;
static volatile int64_t lxai_caller_rejected;
static const char *lxai_stage="idle";
static lxai_fn lxai_original_override; /* tests */
typedef struct {
    uint64_t seq, source, caller;
    unsigned which, caller_state;
    uint32_t impulse[4], point[4], pose[8], motion[24], after[8];
} lxai_record;
typedef struct {uint64_t tick;unsigned count;lxai_record records[LXAI_MAX];} lxai_trace;
static lxai_trace lxai_traces[LXB_ACTORS];
enum {
    LXAI_CALLER_OTHER_SOURCE,
    LXAI_CALLER_KNOWN,
    LXAI_CALLER_HELPER_PIN,
    LXAI_CALLER_FRAME,
    LXAI_CALLER_READ_FAILED,
    LXAI_CALLER_OUTSIDE_TEXT
};
static int lxai_helper_pin(void) {
    const lx_site *helper = site_named("body_impulse_helper");
    return helper && helper->va == LXAI_HELPER_VA && helper->length == LXAI_HELPER_SIZE && site_ok(helper);
}
static const char *lxai_caller_state_name(unsigned state) {
    switch (state) {
    case LXAI_CALLER_KNOWN: return "known";
    case LXAI_CALLER_HELPER_PIN: return "helper_pin";
    case LXAI_CALLER_FRAME: return "frame";
    case LXAI_CALLER_READ_FAILED: return "read_failed";
    case LXAI_CALLER_OUTSIDE_TEXT: return "outside_text";
    default: return "source";
    }
}
/* This is one fixed, pinned stack layout, not a frame-chain walk.
 * At helper entry E, three pushes and sub rsp,16 put RSP at E-0x28.
 * CALL 0x17e2f97 adds its return at E-0x30. The C callback's own frame
 * is E-0x38, so its +8 must hold 0x17e2f9c and +0x38 holds E's return.
 * The helper uses RBP as a vector pointer: following saved RBP is invalid.
 * Obtain frame_address(0) directly inside the noninline callback below. */
static unsigned lxai_outer_return(uint64_t source, const void *callback_frame, uint64_t *caller) {
    *caller = 0;
    if (source != LXAI_HELPER_RETURN) return LXAI_CALLER_OTHER_SOURCE;
    if (!lxai_helper_pin()) return LXAI_CALLER_HELPER_PIN;
    const uintptr_t frame = (uintptr_t)callback_frame;
    if (!frame || frame > UINTPTR_MAX - LXAI_OUTER_RETURN_OFFSET) return LXAI_CALLER_FRAME;
    uintptr_t inner = 0;
    if (!gate_read_local((const void *)(frame + 8), &inner, sizeof(inner))) return LXAI_CALLER_READ_FAILED;
    if (inner != (uintptr_t)LX(LXAI_HELPER_RETURN)) return LXAI_CALLER_FRAME;
    uintptr_t outer = 0;
    if (!gate_read_local((const void *)(frame + LXAI_OUTER_RETURN_OFFSET), &outer, sizeof(outer))) {
        return LXAI_CALLER_READ_FAILED;
    }
    if (outer < (uintptr_t)LX(LX_TEXT_VA) || outer >= (uintptr_t)LX(LX_TEXT_END_VA)) {
        return LXAI_CALLER_OUTSIDE_TEXT;
    }
    *caller = outer - (uintptr_t)image + LX_IMAGE_BASE;
    return LXAI_CALLER_KNOWN;
}
static void lxai_capture_caller(lxai_record *record, const void *callback_frame) {
    record->caller_state = lxai_outer_return(record->source, callback_frame, &record->caller);
    if (record->caller_state == LXAI_CALLER_KNOWN) {
        __atomic_add_fetch(&lxai_caller_known, 1, __ATOMIC_SEQ_CST);
    } else {
        __atomic_add_fetch(&lxai_caller_unknown, 1, __ATOMIC_SEQ_CST);
        if (record->caller_state == LXAI_CALLER_READ_FAILED) {
            __atomic_add_fetch(&lxai_caller_read_failed, 1, __ATOMIC_SEQ_CST);
        } else if (record->caller_state != LXAI_CALLER_OTHER_SOURCE) {
            __atomic_add_fetch(&lxai_caller_rejected, 1, __ATOMIC_SEQ_CST);
        }
    }
}
static void lxai_caller_counters_reset(void) {
    lxai_caller_known = 0;
    lxai_caller_unknown = 0;
    lxai_caller_read_failed = 0;
    lxai_caller_rejected = 0;
}
static __attribute__((noinline)) void lxai_add(void *iface,const void *id,lxai_vec impulse,lxai_vec point) {
    const void *callback_frame = __builtin_frame_address(0);
    lxai_fn original=lxai_original_override?lxai_original_override:(lxai_fn)(void*)lxai_page;
    unsigned actor=0,which=0;unsigned char *body=NULL,*motion=NULL;uint32_t want=0xffffffffu;
    __atomic_add_fetch(&lxai_calls,1,__ATOMIC_SEQ_CST);
    if(stats.active && GetCurrentThreadId()==ui_thread && id && gate_read_local(id,&want,4) && want!=0xffffffffu)
        for(unsigned a=1;a<=LXB_ACTORS && !actor;a++) for(unsigned w=0;w<2 && !actor;w++) {
            unsigned char *m=NULL,*b=lxbs_find(a,w,&m);uint32_t have=0xffffffffu;
            if(b && gate_read_local(b+0x70,&have,4) && have==want) {actor=a;which=w;body=b;motion=m;}
        }
    lxai_record *r=NULL;
    if(actor) {
        lxai_trace *trace=&lxai_traces[actor-1];
        if(trace->tick!=stats.issued) {trace->tick=stats.issued;trace->count=0;}
        if(trace->count<LXAI_MAX) {
            r=&trace->records[trace->count++];memset(r,0,sizeof(*r));
            r->seq=__atomic_add_fetch(&lx_phys_seq,1,__ATOMIC_SEQ_CST);r->which=which;
            r->source=(uint64_t)(uintptr_t)((const unsigned char*)__builtin_return_address(0)-image)+LX_IMAGE_BASE;
            lxai_capture_caller(r, callback_frame);
            memcpy(r->impulse,impulse.v,16);memcpy(r->point,point.v,16);
            if(!gate_read_local(body,r->pose,32) || !gate_read_local(motion,r->motion,0x60)) __atomic_add_fetch(&lxai_failures,1,__ATOMIC_SEQ_CST);
        } else __atomic_add_fetch(&lxai_failures,1,__ATOMIC_SEQ_CST);
    }
    original(iface,id,impulse,point);
    if(r) {if(!gate_read_local(motion,r->after,32)) __atomic_add_fetch(&lxai_failures,1,__ATOMIC_SEQ_CST);else __atomic_add_fetch(&lxai_recorded,1,__ATOMIC_SEQ_CST);}
}
static void lxai_entry(unsigned char out[16]) {
    const void *to=(const void*)lxai_add;
    out[0]=0xff;out[1]=0x25;memset(out+2,0,4);memcpy(out+6,&to,8);memcpy(out+14,lxai_original+14,2);
}
static int lxai_state(void) { /* 0 native, 1 ours, -1 anything else */
    unsigned char b[16],want[16];
    if(!gate_read_local(LX(LXAI_VA),b,16)) return -1;
    if(!memcmp(b,lxai_prologue,14) && (!lxai_installed || !memcmp(b+14,lxai_original+14,2))) return 0;
    if(lxai_page) {lxai_entry(want);if(!memcmp(b,want,16)) return 1;}
    return -1;
}
static int lxai_pins(void) {
    const lx_site *s=site_named("body_add_impulse");unsigned char copy[0x40];
    if(!s || s->va!=LXAI_VA || s->length>sizeof(copy) || lxai_state()<0 || !gate_read_local(LX(s->va),copy,s->length)) return 0;
    if(lxai_installed) memcpy(copy,lxai_original,16);
    return hash_ok(copy,s->length,s->sha256) && lxai_helper_pin();
}
static int lxai_swap(const unsigned char from[16],const unsigned char to[16]) {
    unsigned __int128 expected,desired;memcpy(&expected,from,16);memcpy(&desired,to,16);
    return gate_code_swap128(LX(LXAI_VA),expected,desired);
}
static int lxai_install(void) {
    if(!gate_read_local(LX(LXAI_VA),lxai_original,16) || memcmp(lxai_original,lxai_prologue,14)) return 0;
    if(!lxai_page) lxai_page=gate_near_page(LX(LXAI_VA));
    if(!lxai_page || mprotect(lxai_page,GATE_PAGE,PROT_READ|PROT_WRITE)) return 0;
    memset(lxai_page,0xcc,GATE_PAGE);memcpy(lxai_page,lxai_original,14);
    {const void *back=LX(LXAI_VA+14u);lxai_page[14]=0xff;lxai_page[15]=0x25;memset(lxai_page+16,0,4);memcpy(lxai_page+20,&back,8);}
    if(mprotect(lxai_page,GATE_PAGE,PROT_READ|PROT_EXEC)) return 0;
    unsigned char to[16];lxai_entry(to);
    return lxai_swap(lxai_original,to);
}
/* Before the snapshot load, on the MoveWorker. */
static int lxai_arm(void) {
    if(lxai_installed) return stats.active;
    if(!stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) {lxai_stage="guard";return 0;}
    if(lxai_state()!=0) {lxai_stage="entry";return 0;}
    if(!lxai_pins()) {lxai_stage="pins";return 0;}
    lxai_calls=lxai_recorded=lxai_failures=0;memset(lxai_traces,0,sizeof(lxai_traces));
    lxai_caller_counters_reset();
    if(!lxai_install()) {lxai_stage="install";return 0;}
    lxai_installed=1;lxai_restored=0;lxai_stage="armed";return 1;
}
static int lxai_check(void) {
    const char *why=lxai_state()!=1?"entry_changed":!lxai_pins()?"block_changed":NULL;
    if(why) {lxai_stage=why;return 0;}
    return 1;
}
static void lxai_restore(void) {
    if(!lxai_installed) return;
    int state=lxai_state();
    if(state==1) {unsigned char from[16];lxai_entry(from);if(!lxai_swap(from,lxai_original)) return;}
    else if(state!=0) return; /* a foreign value stays */
    lxai_installed=0;lxai_restored=1; /* the trampoline stays: a thread may still be inside it */
}
static int lxai_status(char *out,size_t size) {
    int n=snprintf(out,size,"tick=%llu installed=%d restored=%d calls=%lld recorded=%lld failures=%lld state=%d pin=%d stage=%s "
        "caller_known=%lld caller_unknown=%lld caller_read_failed=%lld caller_rejected=%lld",
        (unsigned long long)stats.issued,lxai_installed,lxai_restored,(long long)lxai_calls,(long long)lxai_recorded,
        (long long)lxai_failures,lxai_state(),lxai_pins(),lxai_stage,
        (long long)lxai_caller_known,(long long)lxai_caller_unknown,
        (long long)lxai_caller_read_failed,(long long)lxai_caller_rejected);
    return n>0 && (size_t)n<size?n:0;
}
/* "which=W tick=T seq=S source=X impulse=<4> point=<4> pose=<8> motion=<24> after=<8>"
 * (words %08x), followed by optional caller=<hex> caller_state=<reason> metadata.
 * caller=0 explicitly means the guarded outer return was not established. */
static int lxai_text(unsigned actor,unsigned ordinal,char *out,size_t cap) {
    if(actor<1 || actor>LXB_ACTORS || cap<800) return 0;
    lxai_trace *trace=&lxai_traces[actor-1];unsigned count=trace->tick==stats.issued?trace->count:0;
    if(ordinal<1 || ordinal>count) return 0;
    const lxai_record *r=&trace->records[ordinal-1];
    int n=snprintf(out,cap,"which=%u tick=%llu seq=%llu source=%llx impulse=",r->which,(unsigned long long)trace->tick,
        (unsigned long long)r->seq,(unsigned long long)r->source);
    n+=lxb_hex_words(out+n,cap-(size_t)n,r->impulse,4);
    n+=snprintf(out+n,cap-(size_t)n," point=");n+=lxb_hex_words(out+n,cap-(size_t)n,r->point,4);
    n+=snprintf(out+n,cap-(size_t)n," pose=");n+=lxb_hex_words(out+n,cap-(size_t)n,r->pose,8);
    n+=snprintf(out+n,cap-(size_t)n," motion=");n+=lxb_hex_words(out+n,cap-(size_t)n,r->motion,24);
    n+=snprintf(out+n,cap-(size_t)n," after=");n+=lxb_hex_words(out+n,cap-(size_t)n,r->after,8);
    n+=snprintf(out+n,cap-(size_t)n," caller=%llx caller_state=%s",
        (unsigned long long)r->caller,lxai_caller_state_name(r->caller_state));
    return n>0 && (size_t)n<cap?n:0;
}
static int lua_impulse_diag(lua_State *L) {p_lua_pushnumber(L,lxai_arm());return 1;}
static int lua_impulse_status(lua_State *L) {
    char out[384];int n=accept_ui()?lxai_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* gate.impulse(actor, ordinal): ordinal 0 the count of this tick's recorded calls. */
static int lua_impulse(lua_State *L) {
    double actor=p_lua_tonumber(L,1),ordinal=p_lua_tonumber(L,2);char out[1024];
    if(!accept_ui() || !lxai_installed || !isfinite(actor) || floor(actor)!=actor || actor<1 || actor>LXB_ACTORS ||
       !isfinite(ordinal) || floor(ordinal)!=ordinal || ordinal<0 || ordinal>LXAI_MAX) {p_lua_pushnil(L);return 1;}
    lxai_trace *trace=&lxai_traces[(unsigned)actor-1];
    if(ordinal==0) {p_lua_pushnumber(L,trace->tick==stats.issued?trace->count:0);return 1;}
    int n=lxai_text((unsigned)actor,(unsigned)ordinal,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
