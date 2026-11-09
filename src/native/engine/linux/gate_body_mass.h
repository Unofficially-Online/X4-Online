/* native_gate_body1_mass = 'fallback' (Linux gate only, default off): a ship's second physics body gets, as it is created,
 * the mass properties of an empty compound, Windows' value. Included by gate_linux.c only.
 *
 * A ship's two physics records (gate_battle.h's gate.body) hold X4's XPhys::XDefinedMassShape (vtable 0x3c606b8; Jolt
 * type 2 subtype 0x11) over a Jolt MutableCompoundShape (type 1 subtype 8); the second body (dynamic, object layer 3)
 * collects the ship's parts (31 on the Argon fighter). Its mass properties are computed once, when the record
 * constructor (0xee8830) allocates the body (JPH::BodyManager::AllocateBody 0x1a969d0, CalculateMassAndInertia):
 * XDefinedMassShape::GetMassProperties (0xed4cf0) asks the compound, scales a positive mass to the shape's own mass
 * (+0x28) and multiplies the inertia (+0x2c); an empty compound has no mass and no inertia, so the shape's mass with a
 * zero inertia goes to MotionProperties::SetMassProperties, whose decomposition fails and which falls back to a sphere
 * of radius 1: inverse inertia 2.5 * inverse mass on all four lanes, the identity rotation. Later parts never update it
 * (the parts' NotifyShapeChanged passes updateMassProperties only when record+0x3a is 0, and it is 1 for this body, on
 * both executables). On Windows the body is created before its parts arrive: every fixture ship's second body has the
 * fallback (W35..W36i). On Linux the loader threads have often added parts already: the hull's own tensor, summed in a
 * thread-dependent order (it differs even between Linux processes).
 *
 * Here AllocateBody's first 14 bytes (push r14; push r13; mov r13,rdi; push r12; push rbp; mov rbp,rsi; push rbx: whole
 * instructions, nothing RIP-relative) become jmp [rip] -> lxbm_allocate in one aligned 16-byte compare-exchange (bytes
 * 14..15, the start of sub rsp,0x50, stay); the original runs through a trampoline (those 14 bytes, then jmp [rip] ->
 * 0x1a969de). After it returns, a body created dynamic (settings+0x60 = 2) whose shape is XDefinedMassShape over a
 * MutableCompoundShape and whose record (the settings' user data, +0x40, set by the constructor at 0xee8b5c) has +0x3a
 * = 1 (set at 0xee88b7, before the allocation) gets the fallback; the first body (+0x3a = 0: Jolt updates its mass as
 * the parts arrive) is only counted (updated): MotionProperties +0x20..+0x2c = 2.5f * mInvMass (+0x58), +0x30..+0x3c =
 * (0, 0, 0, 1). That covers the snapshot load, a hot joiner's load and ships made in the session alike. Counted: the
 * bodies seen, those that already had the fallback (created empty, as on Windows) and those changed. Pin body_allocate
 * (AllocateBody's head) and the shape's GetMassProperties (body_defined_mass); armed before the snapshot load on the
 * MoveWorker; the per-tick check (stop reason 204) and the restore are this file's. Windows needs nothing. */
#define LXBM_STOP_REASON 204u
#define LXBM_ALLOCATE_VA 0x1a969d0u
#define LXBM_DEFINED_MASS_VTABLE 0x3c606b8u
typedef void *(*lxbm_alloc_fn)(void *manager,const void *settings);
static const unsigned char lxbm_prologue[14]={0x41,0x56,0x41,0x55,0x49,0x89,0xfd,0x41,0x54,0x55,0x48,0x89,0xf5,0x53};
static unsigned char lxbm_original[16],*lxbm_page;
static int lxbm_installed,lxbm_restored;
static volatile int64_t lxbm_seen,lxbm_already,lxbm_changed,lxbm_updated,lxbm_failures;
static const char *lxbm_stage="idle";
static lxbm_alloc_fn lxbm_original_override; /* tests */
/* 1 when the body's motion properties hold the empty compound's fallback for its inverse mass, word for word
 * (lockstep_body_mass.h, shared with Windows' census). */
static int lxbm_is_fallback(const unsigned char *motion) {
    uint32_t w[8],im;memcpy(w,motion+0x20,32);memcpy(&im,motion+0x58,4);
    return body_mass_is_fallback(w,im);
}
static void lxbm_apply(unsigned char *body,const unsigned char *settings) {
    unsigned char *shape=NULL,*inner=NULL,*motion=NULL,*record=NULL,kind=0,fixed=0,ts[2]={0,0};void *vtable=NULL;
    if(!gate_read_local(settings+0x60,&kind,1) || kind!=2) return; /* EMotionType::Dynamic */
    if(!gate_read_local(body+0x40,&shape,8) || !shape || !gate_read_local(shape,&vtable,8) || vtable!=(void*)LX(LXBM_DEFINED_MASS_VTABLE) ||
       !gate_read_local(shape+0x20,&inner,8) || !inner || !gate_read_local(inner+0x18,ts,2) || ts[0]!=1 || ts[1]!=8) return;
    /* only where Jolt keeps the creation-time mass: the record's +0x3a (BodyCreationSettings::mUserData is the record) */
    if(!gate_read_local(settings+0x40,&record,8) || !record || !gate_read_local(record+0x3a,&fixed,1)) {__atomic_add_fetch(&lxbm_failures,1,__ATOMIC_SEQ_CST);return;}
    if(fixed!=1) {__atomic_add_fetch(&lxbm_updated,1,__ATOMIC_SEQ_CST);return;}
    if(!gate_read_local(body+0x48,&motion,8) || !motion) {__atomic_add_fetch(&lxbm_failures,1,__ATOMIC_SEQ_CST);return;}
    __atomic_add_fetch(&lxbm_seen,1,__ATOMIC_SEQ_CST);
    if(lxbm_is_fallback(motion)) {__atomic_add_fetch(&lxbm_already,1,__ATOMIC_SEQ_CST);return;}
    float im;memcpy(&im,motion+0x58,4);
    const float f=2.5f*im,diag[4]={f,f,f,f},rotation[4]={0.0f,0.0f,0.0f,1.0f};
    memcpy(motion+0x20,diag,16);memcpy(motion+0x30,rotation,16);
    __atomic_add_fetch(&lxbm_changed,1,__ATOMIC_SEQ_CST);
}
static void *lxbm_allocate(void *manager,const void *settings) {
    lxbm_alloc_fn original=lxbm_original_override?lxbm_original_override:(lxbm_alloc_fn)(void*)lxbm_page;
    void *body=original(manager,settings);
    if(body && settings) lxbm_apply(body,settings);
    return body;
}
static void lxbm_entry(unsigned char out[16]) {
    const void *to=(const void*)lxbm_allocate;
    out[0]=0xff;out[1]=0x25;memset(out+2,0,4);memcpy(out+6,&to,8);memcpy(out+14,lxbm_original+14,2);
}
static int lxbm_state(void) { /* 0 native, 1 ours, -1 anything else */
    unsigned char b[16],want[16];
    if(!gate_read_local(LX(LXBM_ALLOCATE_VA),b,16)) return -1;
    if(!memcmp(b,lxbm_prologue,14) && (!lxbm_installed || !memcmp(b+14,lxbm_original+14,2))) return 0;
    if(lxbm_page) {lxbm_entry(want);if(!memcmp(b,want,16)) return 1;}
    return -1;
}
static int lxbm_pins(void) {
    const lx_site *a=site_named("body_allocate"),*m=site_named("body_defined_mass");unsigned char copy[0x40];
    if(!a || !m || a->va!=LXBM_ALLOCATE_VA || a->length>sizeof(copy) || !site_ok(m) || lxbm_state()<0 ||
       !gate_read_local(LX(a->va),copy,a->length)) return 0;
    if(lxbm_installed) memcpy(copy,lxbm_original,16);
    return hash_ok(copy,a->length,a->sha256);
}
static int lxbm_swap(const unsigned char from[16],const unsigned char to[16]) {
    unsigned __int128 expected,desired;memcpy(&expected,from,16);memcpy(&desired,to,16);
    return gate_code_swap128(LX(LXBM_ALLOCATE_VA),expected,desired);
}
static int lxbm_install(void) {
    if(!gate_read_local(LX(LXBM_ALLOCATE_VA),lxbm_original,16) || memcmp(lxbm_original,lxbm_prologue,14)) return 0;
    if(!lxbm_page) lxbm_page=gate_near_page(LX(LXBM_ALLOCATE_VA));
    if(!lxbm_page || mprotect(lxbm_page,GATE_PAGE,PROT_READ|PROT_WRITE)) return 0;
    memset(lxbm_page,0xcc,GATE_PAGE);memcpy(lxbm_page,lxbm_original,14);
    {const void *back=LX(LXBM_ALLOCATE_VA+14u);lxbm_page[14]=0xff;lxbm_page[15]=0x25;memset(lxbm_page+16,0,4);memcpy(lxbm_page+20,&back,8);}
    if(mprotect(lxbm_page,GATE_PAGE,PROT_READ|PROT_EXEC)) return 0;
    unsigned char to[16];lxbm_entry(to);
    return lxbm_swap(lxbm_original,to);
}
/* Before the snapshot load and the first admitted frame, on the MoveWorker. */
static int lxbm_arm(void) {
    if(lxbm_installed) return stats.active;
    if(!stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) {lxbm_stage="guard";return 0;}
    if(lxbm_state()!=0) {lxbm_stage="entry";return 0;}
    if(!lxbm_pins()) {lxbm_stage="pins";return 0;}
    lxbm_seen=lxbm_already=lxbm_changed=lxbm_updated=lxbm_failures=0;
    if(!lxbm_install()) {lxbm_stage="install";return 0;}
    lxbm_installed=1;lxbm_restored=0;lxbm_stage="armed";return 1;
}
/* Main-thread tick check; any failure stops the gate with reason 204. */
static int lxbm_check(void) {
    const char *why=lxbm_state()!=1?"entry_changed":!lxbm_pins()?"block_changed":__atomic_load_n(&lxbm_failures,__ATOMIC_SEQ_CST)?"motion":NULL;
    if(why) {lxbm_stage=why;return 0;}
    return 1;
}
static void lxbm_restore(void) {
    if(!lxbm_installed) return;
    int state=lxbm_state();
    if(state==1) {unsigned char from[16];lxbm_entry(from);if(!lxbm_swap(from,lxbm_original)) return;}
    else if(state!=0) return; /* a foreign value stays */
    lxbm_installed=0;lxbm_restored=1; /* the trampoline stays: a thread may still be inside it */
}
static int lxbm_status(char *out,size_t size) {
    int n=snprintf(out,size,"tick=%llu installed=%d restored=%d seen=%lld already=%lld changed=%lld updated=%lld failures=%lld state=%d pin=%d stage=%s",
        (unsigned long long)stats.issued,lxbm_installed,lxbm_restored,(long long)lxbm_seen,(long long)lxbm_already,(long long)lxbm_changed,
        (long long)lxbm_updated,(long long)lxbm_failures,lxbm_state(),lxbm_pins(),lxbm_stage);
    return n>0 && (size_t)n<size?n:0;
}
static int lua_body1_mass(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);
    p_lua_pushnumber(L,mode && length==8 && !memcmp(mode,"fallback",8) && lxbm_arm());return 1;
}
static int lua_body1_mass_status(lua_State *L) {
    char out[256];int n=accept_ui()?lxbm_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
