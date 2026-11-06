/* The local lists on Linux (gate_runtime_ids.h, native_gate_runtime_ids_local): on the synthetic image the route
 * table's call rows against their pin (each a call to its target), the route by stack (each kind of row, exact ends,
 * ui over view, the probe's frame in slot 6, off and before the seal), the arming refusals, the lists in the map's
 * blocks 30 and 31 at the Linux field offsets, local and shared assignment through a stand-in AssignNewID, Remove of a
 * local slot back onto its own list, and the hook end to end from planted code at a row (glibc's backtrace gives the
 * X4 return address); on the real executable every row's pin and the two slot functions behind their vtables.
 * Included by test_gate_linux.c. */
static unsigned char *trl_map;
static unsigned trl_locks,trl_unlocks;
static void trl_lock(void *mutex) {assert(mutex==trl_map+0x10);++trl_locks;}
static void trl_unlock(void *mutex) {assert(mutex==trl_map+0x10);++trl_unlocks;}
static uint32_t trl_field(unsigned at) {uint32_t v;memcpy(&v,trl_map+at,4);return v;}
static void trl_field_set(unsigned at,uint32_t v) {memcpy(trl_map+at,&v,4);}
/* AssignNewID as X4's: the head comes off the map's list, the slot holds the object and is used. */
static uint64_t trl_assign(void *object) {
    const uint32_t slot=trl_field(RID_F_HEAD);volatile uint64_t *p=rid_slot(trl_map,slot);assert(p && trl_locks>trl_unlocks);
    const uint32_t next=(uint32_t)(*p>>32);
    trl_field_set(RID_F_HEAD,next);if(!next) trl_field_set(RID_F_TAIL,0);else *rid_slot(trl_map,next)&=0xffffffff00000000ULL;
    *p=(uint64_t)(uintptr_t)object;
    unsigned entry;unsigned char *b=rid_bucket(trl_map,slot,&entry);uint32_t used;memcpy(&used,b+0x1c,4);used|=1u<<(entry*4u+16u);memcpy(b+0x1c,&used,4);
    trl_field_set(RID_F_FREE,trl_field(RID_F_FREE)-1u);trl_field_set(RID_F_LIVE,trl_field(RID_F_LIVE)+1u);
    uint64_t inserts;memcpy(&inserts,trl_map+RID_F_INSERTS,8);++inserts;memcpy(trl_map+RID_F_INSERTS,&inserts,8);
    return slot;
}
/* The hook's original: the lock around it as AssignNewID takes it (recursive in X4). */
static uint64_t trl_assign_locked(void *object) {
    trl_lock(trl_map+0x10);const uint64_t id=trl_assign(object);trl_unlock(trl_map+0x10);return id;
}
/* Remove: the slot unused, onto the tail. */
static uint64_t trl_remove(void *map,uint64_t id) {
    assert(map==trl_map);const uint32_t slot=(uint32_t)id&0x1ffffffu;
    unsigned entry;unsigned char *b=rid_bucket(trl_map,slot,&entry);uint32_t used;memcpy(&used,b+0x1c,4);used&=~(0xfu<<(entry*4u+16u));memcpy(b+0x1c,&used,4);
    const uint32_t tail=trl_field(RID_F_TAIL);*rid_slot(trl_map,slot)=(uint64_t)tail;
    if(tail) *rid_slot(trl_map,tail)=(*rid_slot(trl_map,tail)&0xffffffffULL)|((uint64_t)slot<<32);else trl_field_set(RID_F_HEAD,slot);
    trl_field_set(RID_F_TAIL,slot);trl_field_set(RID_F_FREE,trl_field(RID_F_FREE)+1u);trl_field_set(RID_F_LIVE,trl_field(RID_F_LIVE)-1u);
    return 1;
}
static void *trl_allocate(size_t size) {return calloc(1,size);}
static void *trl_allocate_null(size_t size) {(void)size;return NULL;}
#define TRL_ROUTE(expected,...) do {const uint32_t f_[]={__VA_ARGS__};assert(lxr_route_of(f_,sizeof(f_)/sizeof(*f_))==(expected));} while(0)
/* At entry-8 .. entry+0x12: sub rsp,8; nop4; call target (its return at entry+5); add rsp,8; ret. Entered at entry-8. */
static void trl_plant_call(uint32_t ret_va,uint32_t target_va) {
    unsigned char code[18]={0x48,0x83,0xec,0x08,0x0f,0x1f,0x40,0x00,0xe8,0,0,0,0,0x48,0x83,0xc4,0x08,0xc3};
    const int32_t rel=(int32_t)(target_va-ret_va);memcpy(code+9,&rel,4);tl_write(LX(ret_va-13u),code,sizeof(code));
}
static uint64_t trl_run(uint32_t ret_va,void *object) {return ((uint64_t (*)(void *))(void*)LX(ret_va-13u))(object);}
static void test_runtime_ids_local_linux(void) {
    /* The table: every call row distinct and in the text, every body's pin among the start pins. */
    for(unsigned i=0;i<LX_RID_SITES;i++) {
        const rid_site_t *s=&lx_rid_sites[i];assert(s->lo<=s->hi && s->hi<LX_TEXT_END_VA-LX_IMAGE_BASE && (s->route==RID_ROUTE_UI || s->route==RID_ROUTE_VIEW));
        for(unsigned j=0;j<i;j++) assert(lx_rid_sites[j].lo!=s->lo);
        if(s->target) {assert(s->lo==s->hi && !s->ui_move);continue;}
        unsigned k=0;while(k<gate_site_count && !(gate_sites[k].va==s->lo+LX_IMAGE_BASE && gate_sites[k].length==s->hi-s->lo+1u)) ++k;
        assert(k<gate_site_count && !strncmp(gate_sites[k].name,"rid_",4));
    }
    /* The pins: the synthetic image has no calls there; planted, each row's; one wrong target refuses. */
    assert(!rid_local_pins());
    for(unsigned i=0;i<LX_RID_SITES;i++) if(lx_rid_sites[i].target) {
        unsigned char call[5]={0xe8};const int32_t rel=(int32_t)(lx_rid_sites[i].target-lx_rid_sites[i].lo);memcpy(call+1,&rel,4);
        tl_write(image+lx_rid_sites[i].lo-5u,call,5);
    }
    assert(rid_local_pins());
    {unsigned char wrong[5];memcpy(wrong,LX(0x13927d2u),5);wrong[1]^=1;tl_write(LX(0x13927d2u),wrong,5);assert(!rid_local_pins());wrong[1]^=1;tl_write(LX(0x13927d2u),wrong,5);}
    assert(rid_local_pins());
    /* The route. */
    const uint32_t saved_active=stats.active;const DWORD saved_ui=ui_thread;const int saved_created=created_installed;
    unsigned char saved_map[8];memcpy(saved_map,LX(LX_ID_MAP_VA),8);
    rid_sealed=1;rid_mode=RID_SET;InterlockedExchange(&rid_local_on,1L);
    TRL_ROUTE(RID_ROUTE_UI,LXR(0x189e28cu),LXR(0x1316e60u),LXR(0x119c625u),LXR(0xb08900u));  /* a UIElement through CreateInstance */
    TRL_ROUTE(RID_ROUTE_UI,LXR(0x189e28cu),LXR(0x11a58bdu));TRL_ROUTE(RID_ROUTE_UI,LXR(0xa71953u));TRL_ROUTE(RID_ROUTE_UI,LXR(0x1c9a7f3u));
    TRL_ROUTE(RID_ROUTE_UI,LXR(0x700000u),LXR(0xb08820u));TRL_ROUTE(RID_ROUTE_UI,LXR(0x700000u),LXR(0xb08a9fu));
    TRL_ROUTE(RID_ROUTE_VIEW,LXR(0x189e28cu),LXR(0x119e50fu),LXR(0x700000u));                  /* an EffectObject */
    TRL_ROUTE(RID_ROUTE_VIEW,LXR(0x1796d53u));TRL_ROUTE(RID_ROUTE_VIEW,LXR(0x13927d7u));TRL_ROUTE(RID_ROUTE_VIEW,LXR(0x1392be5u));
    TRL_ROUTE(RID_ROUTE_VIEW,LXR(0x700000u),LXR(0xa70cd0u));TRL_ROUTE(RID_ROUTE_VIEW,LXR(0x700000u),LXR(0xc78d00u));
    TRL_ROUTE(RID_ROUTE_UI,LXR(0x1796d53u),LXR(0x700000u),LXR(0xfb01a0u));                     /* the render walk's effects: the camera's */
    TRL_ROUTE(RID_ROUTE_UI,LXR(0xc8c5a8u),LXR(0xc78d00u),LXR(0xa76000u));                      /* a message from the UI's own slot */
    TRL_ROUTE(RID_ROUTE_SHARED,LXR(0x189e28cu),LXR(0x1316e60u),LXR(0x700000u));
    /* Exact ends. */
    TRL_ROUTE(RID_ROUTE_SHARED,LXR(0xfb017fu));TRL_ROUTE(RID_ROUTE_UI,LXR(0xfb0180u));TRL_ROUTE(RID_ROUTE_UI,LXR(0xfb01efu));TRL_ROUTE(RID_ROUTE_SHARED,LXR(0xfb01f0u));
    TRL_ROUTE(RID_ROUTE_SHARED,LXR(0x13927d6u));TRL_ROUTE(RID_ROUTE_SHARED,LXR(0x13927d8u));
    TRL_ROUTE(RID_ROUTE_UI,LXR(0xa758a0u));TRL_ROUTE(RID_ROUTE_UI,LXR(0xa77e8fu));TRL_ROUTE(RID_ROUTE_SHARED,LXR(0xa77e90u));
    /* Slot 6 and the probe's frame in it: shared, except what the table knows. */
    TRL_ROUTE(RID_ROUTE_UI,LXR(0x700000u),LXR(0xa76000u));
    InterlockedExchange(&rid_probe_frame,1L);
    TRL_ROUTE(RID_ROUTE_SHARED,LXR(0x700000u),LXR(0xa76000u));TRL_ROUTE(RID_ROUTE_UI,LXR(0x119c625u),LXR(0xa76000u));TRL_ROUTE(RID_ROUTE_UI,LXR(0x700000u),LXR(0xfb01a0u));
    InterlockedExchange(&rid_probe_frame,0L);
    /* Off, and before the seal: shared. */
    InterlockedExchange(&rid_local_on,0L);TRL_ROUTE(RID_ROUTE_SHARED,LXR(0x119c625u));InterlockedExchange(&rid_local_on,1L);
    rid_sealed=0;TRL_ROUTE(RID_ROUTE_SHARED,LXR(0x119c625u));
    InterlockedExchange(&rid_local_on,0L);
    /* A map: block 0 linked as EnsureFreeSlots makes it, X4's mutex at +0x10 behind its lock and unlock stand-ins. */
    static unsigned char map[0x200] __attribute__((aligned(16)));memset(map,0,sizeof(map));trl_map=map;
    unsigned char lock_bytes[14],unlock_bytes[14];memcpy(lock_bytes,LX(LX_MUTEX_LOCK_VA),14);memcpy(unlock_bytes,LX(LX_MUTEX_UNLOCK_VA),14);
    tlf_jump_code(LX_MUTEX_LOCK_VA,(void*)trl_lock);tlf_jump_code(LX_MUTEX_UNLOCK_VA,(void*)trl_unlock);
    void *block0=calloc(1,RID_BLOCK_BYTES);assert(block0);
    {rid_local_t shared;rid_local_link(map,&shared,0,block0);trl_field_set(RID_F_HEAD,shared.head);trl_field_set(RID_F_TAIL,shared.tail);trl_field_set(RID_F_FREE,shared.free);}
    assert(trl_field(RID_F_HEAD)==1u && trl_field(RID_F_TAIL)==0x100000u);
    /* The refusals: not set mode, after the seal, not the UI thread, no hook, the wrong build, no map, a taken block, no memory. */
    stats.active=1;ui_thread=GetCurrentThreadId();rid_local_built=0;
    rid_mode=RID_OBSERVE;assert(!rid_local_arm() && !strcmp(rid_stage,"local_phase"));
    rid_mode=RID_SET;rid_sealed=1;assert(!rid_local_arm() && !strcmp(rid_stage,"local_phase"));rid_sealed=0;
    ui_thread=GetCurrentThreadId()+1u;assert(!rid_local_arm() && !strcmp(rid_stage,"local_phase"));ui_thread=GetCurrentThreadId();
    created_installed=0;assert(!rid_local_arm() && !strcmp(rid_stage,"local_hook"));created_installed=1;
    {unsigned char wrong[5];memcpy(wrong,LX(0xa7194eu),5);wrong[0]=0x90;tl_write(LX(0xa7194eu),wrong,5);
     assert(!rid_local_arm() && !strcmp(rid_stage,"local_pins"));wrong[0]=0xe8;tl_write(LX(0xa7194eu),wrong,5);}
    memset(LX(LX_ID_MAP_VA),0,8);assert(!rid_local_arm() && !strcmp(rid_stage,"map"));
    void *m=map;memcpy(LX(LX_ID_MAP_VA),&m,8);
    void *taken=(void*)1;memcpy(map+RID_F_BLOCKS+RID_LOCAL_VIEW_BLOCK*8u,&taken,8);assert(!rid_local_arm() && !strcmp(rid_stage,"local_blocks"));
    memset(map+RID_F_BLOCKS+RID_LOCAL_VIEW_BLOCK*8u,0,8);
    rid_allocate_native=trl_allocate_null;assert(!rid_local_arm() && !strcmp(rid_stage,"local_allocate") && !rid_local_built && !rid_local_on);
    /* Armed: the map's two last blocks, every slot free, in order; the shared list untouched. */
    rid_allocate_native=trl_allocate;trl_locks=trl_unlocks=0;
    assert(rid_local_arm() && rid_local_on && rid_local_built && rid_local_arm() && trl_locks==1 && trl_unlocks==1);
    {
        const rid_local_t *u=&rid_local[RID_ROUTE_UI],*v=&rid_local[RID_ROUTE_VIEW];
        assert(u->head==0x1f00001u && u->tail==0x1ffffffu && u->free==0xfffffu && v->head==0x1e00001u && v->tail==0x1f00000u && v->free==0x100000u);
        void *b30,*b31;memcpy(&b30,map+RID_F_BLOCKS+30u*8u,8);memcpy(&b31,map+RID_F_BLOCKS+31u*8u,8);assert(b30 && b31 && b30!=b31);
        assert(rid_local_of(0x1f00001u)==u && rid_local_of(0x1e00001u)==v && !rid_local_of(0x1e00000u) && !rid_local_of(1u));
        assert(trl_field(RID_F_HEAD)==1u && trl_field(RID_F_TAIL)==0x100000u && trl_field(RID_F_FREE)==0x100000u);
    }
    /* Assignment: the local lists and the shared one apart; afterwards the shared fields are the shared list's. */
    unsigned char object[16];lxr_assign_native=trl_assign;rid_sealed=1;InterlockedExchange(&rid_active,1L);
    trl_locks=trl_unlocks=0;
    assert(rid_assign_ui(object)==0x1f00001u && rid_assign_ui(object)==0x1f00002u && rid_assign_view(object)==0x1e00001u);
    assert(trl_locks==3 && trl_unlocks==3 && rid_local[RID_ROUTE_UI].allocs==2 && rid_local[RID_ROUTE_VIEW].allocs==1 && rid_local[RID_ROUTE_UI].head==0x1f00003u);
    assert(trl_field(RID_F_HEAD)==1u && trl_field(RID_F_TAIL)==0x100000u && trl_field(RID_F_FREE)==0x100000u);
    /* Remove: a local slot back onto its list, a shared one off the shared tail as set mode does. */
    rid_remove_native=trl_remove;
    assert(rid_remove(map,0x1f00001u)==1 && rid_local[RID_ROUTE_UI].tail==0x1f00001u && rid_local[RID_ROUTE_UI].frees==1 && trl_field(RID_F_TAIL)==0x100000u);
    /* The hook end to end, entered at planted code ending in a row's call: the stack picks the list. */
    lxr_assign_native=trl_assign_locked;
    {
        unsigned char sphere[14];memcpy(sphere,LX(LX_RID_SPHERE),14);tlf_jump_code(LX_RID_SPHERE,(void*)rid_assign);
        trl_plant_call(0x13927d7u,LX_RID_SPHERE);trl_plant_call(0xa76009u,LX_RID_SPHERE);trl_plant_call(0x700009u,LX_RID_SPHERE);
        trl_locks=trl_unlocks=0;
        assert(trl_run(0x13927d7u,object)==0x1e00002u);                    /* the player's query sphere: the view list */
        assert(trl_run(0xa76009u,object)==0x1f00003u);                     /* slot 6: the UI list */
        assert(trl_run(0x700009u,object)==1u && trl_field(RID_F_HEAD)==2u); /* elsewhere: shared */
        InterlockedExchange(&rid_probe_frame,1L);assert(trl_run(0xa76009u,object)==2u);InterlockedExchange(&rid_probe_frame,0L);
        rid_sealed=0;assert(trl_run(0x13927d7u,object)==3u);rid_sealed=1; /* before the seal: shared */
        assert(trl_locks==trl_unlocks && trl_field(RID_F_HEAD)==4u);
        unsigned char zero[18];memset(zero,0,sizeof(zero));
        tl_write(LX(0x13927d7u-13u),zero,18);tl_write(LX(0xa76009u-13u),zero,18);tl_write(LX(0x700009u-13u),zero,18);tl_write(LX(LX_RID_SPHERE),sphere,14);
        unsigned char call[5]={0xe8};const int32_t rel=(int32_t)(LX_RID_SPHERE-0x13927d7u);memcpy(call+1,&rel,4);tl_write(LX(0x13927d2u),call,5);
    }
    char text[900];assert(rid_status(text,sizeof(text))>0 && strstr(text,"local=1,ui:"));
    /* Clean up: off, the lists dropped, the stand-ins and the map pointer back. */
    InterlockedExchange(&rid_local_on,0L);InterlockedExchange(&rid_active,0L);rid_local_built=0;memset(rid_local,0,sizeof(rid_local));memset(rid_local_now,0,sizeof(rid_local_now));
    {void *b;memcpy(&b,map+RID_F_BLOCKS+30u*8u,8);free(b);memcpy(&b,map+RID_F_BLOCKS+31u*8u,8);free(b);}
    free(block0);trl_map=NULL;
    tl_write(LX(LX_MUTEX_LOCK_VA),lock_bytes,14);tl_write(LX(LX_MUTEX_UNLOCK_VA),unlock_bytes,14);
    memcpy(LX(LX_ID_MAP_VA),saved_map,8);
    rid_allocate_native=NULL;lxr_assign_native=NULL;rid_remove_native=NULL;rid_mode=RID_OFF;rid_sealed=0;rid_stage="idle";
    stats.active=saved_active;ui_thread=saved_ui;created_installed=saved_created;
    printf("runtime ids local PASS (%u rows)\n",(unsigned)LX_RID_SITES);
}
/* The real executable: every call row's call, the body pins (verify), the slot functions behind their vtables. */
static void test_runtime_ids_local_elf(void) {
    assert(rid_local_pins());
    /* UI::CUIController's secondary vtable (top -8), slot 2: sub rdi,8; jmp 0xa758a0. X4::GameRender's slot 2: 0xfb0180. */
    uint64_t slot;memcpy(&slot,LX(0x3ab4978u+16u),8);assert(slot==0xa77e90u);
    assert(!memcmp(LX(0xa77e90u),"\x48\x83\xef\x08\xe9",5));{int32_t rel;memcpy(&rel,LX(0xa77e95u),4);assert(0xa77e99u+(uint32_t)rel==0xa758a0u);}
    memcpy(&slot,LX(0x3ab1270u+16u),8);assert(slot==0xfb0180u);
    printf("runtime ids local ELF PASS\n");
}
