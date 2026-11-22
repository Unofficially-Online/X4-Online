/* native_gate_contact_diag on the Linux gate (boot option 2^43, diagnostic, default off): Windows' lockstep_contact.h,
 * every contact Jolt reports for a fixture ship (added or persisted) with its manifold and its settings before and
 * after X4's listener, as CONTACT rows in Windows' text. Included by gate_linux.c after gate_body_step.h.
 *
 * Why: the battle's first state difference against Windows (lx70a against W36q, tick 133) is the velocity change of a
 * collision between actors 4 and 11 inside the Jolt step, a few ulps apart. These rows show whether the contact's
 * inputs (base, normal, depth, points, settings) already differ or only what the solver makes of them.
 *
 * Linux site: X4's XPhys::XContactListener (typeinfo N5XPhys16XContactListenerE, vtable 0x3b0e2c0, address point
 * 0x3b0e2d0). Slot 3 OnContactAdded (0x3b0e2e8 -> 0xee21a0: mov r9d,1; jmp RecordContact 0xee1060) and slot 4
 * OnContactPersisted (0x3b0e2f0 -> 0xee21b0: xor r9d,r9d; jmp 0xee1060), Windows' 0xb7230 / 0xb7250 -> 0xb75a0.
 * Windows hooks RecordContact's entry; here the two slots are pointer swaps, the originals called as they were.
 * The body and record offsets are Windows' (Jolt's Body: +0 pose, +0x40 shape, +0x48 motion, +0x50 user data = X4's
 * record, +0x70 BodyID, +0x74.. layer, types, flags; the record: +0 the owner, +0x20 its shape holder, due at +0x28).
 * Windows-only fields (the component's ID and class, the shape's class as image offsets) print 0. A contact on
 * another thread is recorded as well (an atomic slot claim) and counted as other_thread; Windows counts it as a
 * failure. Stop reasons as Windows: 69 the install at boot, 70 a failure or a slot no longer ours. */
#define LXCT_LISTENER_VTABLE 0x3b0e2d0u /* the address point */
#define LXCT_ADDED_SLOT_VA (LXCT_LISTENER_VTABLE+3u*8u)
#define LXCT_PERSISTED_SLOT_VA (LXCT_LISTENER_VTABLE+4u*8u)
#define LXCT_ADDED_VA 0xee21a0u
#define LXCT_PERSISTED_VA 0xee21b0u
#define LXCT_RECORD_VA 0xee1060u
#define LXCT_EVENTS 2048u
#define LXCT_POINTS 64u
typedef void (*lxct_fn)(void *,const void *,const void *,const void *,void *);
typedef struct {unsigned actor,body_id,record_kind;uint64_t shape_due;uint32_t pose[8],velocity[8];} lxct_body;
typedef struct {
    uint64_t tick,ordinal;unsigned added,count;
    lxct_body bodies[2];
    uint32_t base[4],normal[4],depth,subshape[2],points[2][LXCT_POINTS][4];
    uint32_t settings[2][16];
} lxct_event;
static lxct_event lxct_events[LXCT_EVENTS];
static volatile uint32_t lxct_claimed;   /* slots claimed (may pass LXCT_EVENTS: those are failures) */
static uint32_t lxct_count;              /* rows readable: set on the UI thread from the claims */
static volatile uint64_t lxct_calls,lxct_ordinal,lxct_other_thread;
static volatile int64_t lxct_failures;
static int lxct_installed,lxct_restored;
static lxct_fn lxct_added_override,lxct_persisted_override; /* tests */
/* The fixture actor (1..12) whose body this is, and which of its two records (0, 1; 2 another); 0 for none. */
static unsigned lxct_actor(const unsigned char *body,unsigned *kind) {
    void *record=NULL,*owner=NULL,*own=NULL;
    *kind=2;
    if(lxb_bound!=LXB_ACTORS || !gate_read_local(body+0x50,&record,8) || !record || !gate_read_local(record,&owner,8) || !owner) return 0;
    for(unsigned i=0;i<LXB_ACTORS;i++) {
        const lxb_actor *a=&lxb_actors[i];unsigned slot=lxb_body_slot[i];
        if(!a->object || a->object!=owner) continue;
        for(unsigned w=0;slot && w<2;w++) if(gate_read_local((unsigned char*)a->object+slot+w*8u,&own,8) && own==record) *kind=w;
        return i+1;
    }
    return 0;
}
static int lxct_body_read(const unsigned char *body,lxct_body *out) {
    void *record=NULL,*holder=NULL;unsigned char *motion=NULL;
    memset(out,0,sizeof(*out));out->actor=lxct_actor(body,&out->record_kind);
    if(!gate_read_local(body+0x50,&record,8) || !record || !gate_read_local(body+0x70,&out->body_id,4) ||
       !gate_read_local(body,out->pose,32) || !gate_read_local((unsigned char*)record+0x20,&holder,8) ||
       (holder && !gate_read_local((unsigned char*)holder+0x28,&out->shape_due,8)) ||
       !gate_read_local(body+0x48,&motion,8) || (motion && !gate_read_local(motion,out->velocity,32))) return 0;
    return 1;
}
/* Jolt 5.5 single precision ContactManifold, as Windows reads it: base +0, normal +0x10, depth +0x20, the sub shape
 * IDs +0x24, the two inline arrays of 64 Vec3 with their counts at +0x30 / +0x440 and points at +0x40 / +0x450. */
static int lxct_read(lxct_event *e,const unsigned char *a,const unsigned char *b,const unsigned char *m,const void *settings) {
    unsigned count2=0;
    if(!lxct_body_read(a,&e->bodies[0]) || !lxct_body_read(b,&e->bodies[1]) ||
       !gate_read_local(m,e->base,16) || !gate_read_local(m+0x10,e->normal,16) ||
       !gate_read_local(m+0x20,&e->depth,4) || !gate_read_local(m+0x24,e->subshape,8) ||
       !gate_read_local(m+0x30,&e->count,4) || !gate_read_local(m+0x440,&count2,4) ||
       e->count>LXCT_POINTS || count2!=e->count ||
       !gate_read_local(m+0x40,e->points[0],e->count*16u) || !gate_read_local(m+0x450,e->points[1],e->count*16u) ||
       !gate_read_local(settings,e->settings[0],sizeof(e->settings[0]))) return 0;
    return 1;
}
static void lxct_observe(lxct_fn original,unsigned added,void *self,const void *a,const void *b,const void *manifold,void *settings) {
    unsigned kind;
    if(!stats.active || boot_phase!=3 || (!lxct_actor(a,&kind) && !lxct_actor(b,&kind))) {original(self,a,b,manifold,settings);return;}
    if(GetCurrentThreadId()!=ui_thread) __atomic_add_fetch(&lxct_other_thread,1,__ATOMIC_SEQ_CST);
    const uint32_t index=__atomic_fetch_add(&lxct_claimed,1,__ATOMIC_SEQ_CST);
    if(index>=LXCT_EVENTS) {__atomic_add_fetch(&lxct_failures,1,__ATOMIC_SEQ_CST);original(self,a,b,manifold,settings);return;}
    lxct_event *e=&lxct_events[index];memset(e,0,sizeof(*e));
    e->tick=stats.issued;e->ordinal=__atomic_add_fetch(&lxct_ordinal,1,__ATOMIC_SEQ_CST);e->added=added;
    if(!lxct_read(e,a,b,manifold,settings)) {e->count=0;__atomic_add_fetch(&lxct_failures,1,__ATOMIC_SEQ_CST);}
    original(self,a,b,manifold,settings);
    if(!gate_read_local(settings,e->settings[1],sizeof(e->settings[1]))) __atomic_add_fetch(&lxct_failures,1,__ATOMIC_SEQ_CST);
    __atomic_add_fetch(&lxct_calls,1,__ATOMIC_SEQ_CST);
}
static void lxct_added(void *self,const void *a,const void *b,const void *manifold,void *settings) {
    lxct_observe(lxct_added_override?lxct_added_override:(lxct_fn)(void*)LX(LXCT_ADDED_VA),1,self,a,b,manifold,settings);
}
static void lxct_persisted(void *self,const void *a,const void *b,const void *manifold,void *settings) {
    lxct_observe(lxct_persisted_override?lxct_persisted_override:(lxct_fn)(void*)LX(LXCT_PERSISTED_VA),0,self,a,b,manifold,settings);
}
static int lxct_owned(void) {
    return !lxct_installed || (rd64(LX(LXCT_ADDED_SLOT_VA))==(uint64_t)(uintptr_t)lxct_added &&
                               rd64(LX(LXCT_PERSISTED_SLOT_VA))==(uint64_t)(uintptr_t)lxct_persisted);
}
/* At boot, before the snapshot load (Windows installs it in the boot call too). */
static int lxct_install(void) {
    const lx_site *s=site_named("contact_listener_slots");
    if(lxct_installed) return 1;
    if(!s || s->va!=LXCT_ADDED_VA || !site_ok(s)) return 0;
    if(rd64(LX(LXCT_ADDED_SLOT_VA))!=(uint64_t)(uintptr_t)LX(LXCT_ADDED_VA) ||
       rd64(LX(LXCT_PERSISTED_SLOT_VA))!=(uint64_t)(uintptr_t)LX(LXCT_PERSISTED_VA)) return 0;
    lxct_claimed=0;lxct_count=0;lxct_calls=lxct_ordinal=lxct_other_thread=0;lxct_failures=0;
    if(!gate_data_swap64(LX(LXCT_ADDED_SLOT_VA),(uint64_t)(uintptr_t)LX(LXCT_ADDED_VA),(uint64_t)(uintptr_t)lxct_added)) return 0;
    if(!gate_data_swap64(LX(LXCT_PERSISTED_SLOT_VA),(uint64_t)(uintptr_t)LX(LXCT_PERSISTED_VA),(uint64_t)(uintptr_t)lxct_persisted)) {
        gate_data_swap64(LX(LXCT_ADDED_SLOT_VA),(uint64_t)(uintptr_t)lxct_added,(uint64_t)(uintptr_t)LX(LXCT_ADDED_VA));return 0;
    }
    lxct_installed=1;lxct_restored=0;return 1;
}
static void lxct_restore(void) {
    if(!lxct_installed) return;
    const int a=gate_data_swap64(LX(LXCT_ADDED_SLOT_VA),(uint64_t)(uintptr_t)lxct_added,(uint64_t)(uintptr_t)LX(LXCT_ADDED_VA));
    const int b=gate_data_swap64(LX(LXCT_PERSISTED_SLOT_VA),(uint64_t)(uintptr_t)lxct_persisted,(uint64_t)(uintptr_t)LX(LXCT_PERSISTED_VA));
    if(a && b) {lxct_installed=0;lxct_restored=1;}
}
static int lxct_check(void) {return lxct_owned() && !lxct_failures;}
/* gate.contact(ordinal): Windows' text; ordinal 0 the rows held (the claims so far, at most LXCT_EVENTS). */
static int lxct_text(const lxct_event *e,char *out,size_t size) {
    int n=snprintf(out,size,"tick=%llu ordinal=%llu added=%u count=%u subshape=%08x,%08x base=",
        (unsigned long long)e->tick,(unsigned long long)e->ordinal,e->added,e->count,e->subshape[0],e->subshape[1]);
#define LXCT_PUT(...) do {if(n>0 && (size_t)n<size) n+=snprintf(out+n,size-(size_t)n,__VA_ARGS__);} while(0)
    for(unsigned i=0;i<3;i++) LXCT_PUT("%08x",e->base[i]);
    LXCT_PUT(" normal=");for(unsigned i=0;i<3;i++) LXCT_PUT("%08x",e->normal[i]);
    LXCT_PUT(" depth=%08x",e->depth);
    for(unsigned j=0;j<2;j++) {
        const lxct_body *b=&e->bodies[j];
        LXCT_PUT(" shape_due%u=%016llx",j,(unsigned long long)b->shape_due);
        LXCT_PUT(" record%u=%u",j,b->record_kind);
        LXCT_PUT(" body%u=%u:%08x:0:0:0:",j,b->actor,b->body_id);
        for(unsigned i=0;i<8;i++) if(i!=3) LXCT_PUT("%08x",b->pose[i]);
        LXCT_PUT(":");
        for(unsigned i=0;i<8;i++) if(i!=3 && i!=7) LXCT_PUT("%08x",b->velocity[i]);
        LXCT_PUT(" points%u=",j);
        for(unsigned point=0;point<e->count;point++) for(unsigned k=0;k<3;k++) LXCT_PUT("%08x",e->points[j][point][k]);
        LXCT_PUT(" settings%u=",j);
        for(unsigned i=0;i<6;i++) LXCT_PUT("%08x",e->settings[j][i]);
        LXCT_PUT(":%02x:",((const unsigned char*)e->settings[j])[0x18]);
        for(unsigned i=8;i<15;i++) if(i!=11) LXCT_PUT("%08x",e->settings[j][i]);
    }
#undef LXCT_PUT
    return n>0 && (size_t)n<size?n:0;
}
static int lua_contact(lua_State *L) {
    double ordinal=p_lua_tonumber(L,1);
    if(GetCurrentThreadId()!=ui_thread || !isfinite(ordinal) || ordinal<0 || floor(ordinal)!=ordinal) {p_lua_pushnil(L);return 1;}
    const uint32_t claimed=__atomic_load_n(&lxct_claimed,__ATOMIC_SEQ_CST);
    lxct_count=claimed<LXCT_EVENTS?claimed:LXCT_EVENTS;
    if(ordinal==0) {p_lua_pushnumber(L,lxct_count);return 1;}
    if(ordinal>lxct_count) {p_lua_pushnil(L);return 1;}
    static char out[8192];const int n=lxct_text(&lxct_events[(unsigned)ordinal-1],out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
/* Between ticks only (the probe drains after the frame): no step is running then. */
static int lua_contact_clear(lua_State *L) {
    int ok=GetCurrentThreadId()==ui_thread;if(ok) {__atomic_store_n(&lxct_claimed,0,__ATOMIC_SEQ_CST);lxct_count=0;}
    p_lua_pushnumber(L,ok);return 1;
}
static int lua_contact_status(lua_State *L) {
    char out[256];int n=snprintf(out,sizeof(out),"calls=%llu restored=%d failures=%lld installed=%d other_thread=%llu owned=%d",
        (unsigned long long)lxct_calls,lxct_restored,(long long)lxct_failures,lxct_installed,(unsigned long long)lxct_other_thread,lxct_owned());
    if(!accept_ui() || n<=0 || (size_t)n>=sizeof(out)) {p_lua_pushnil(L);return 1;}
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
