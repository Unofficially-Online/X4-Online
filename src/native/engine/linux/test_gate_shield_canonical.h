/* native_gate_world_hash_shields on Linux (gate_world.h): at the Linux layout the canonical form prints the Windows test's
 * text for its group (scene2's 6f27e: value 1156.6 at 3.08, max 1196, delay 15, rate 1 x 50, one generator), only once
 * full or already full; nothing is written; the source must be the group's own; the stored rows stay stored. The arming
 * before the first capture and its refusals (mode, pins, after a scan), the status, the capture's time read. On the real
 * executable: the pins (the slots hold the getters' addresses), the read's slot tests, layout, constants and write, and
 * the group construct storing the holder and the source. Included by test_gate_linux.c. */
static const char *tsc_mode;
static const char *tsc_tolstring(lua_State *L,int index,size_t *length) {
    (void)L;(void)index;if(length) *length=tsc_mode?strlen(tsc_mode):0;return tsc_mode;
}
static const uint32_t tsc_slot_at[4]={LX_SHIELD_SOURCE_VTABLE+0x8u,LX_SHIELD_SOURCE_VTABLE+0x10u,LX_SHIELD_SOURCE_VTABLE+0x18u,LX_SHIELD_HOLDER_VTABLE+0x20u};
static const uint32_t tsc_slot_to[4]={LX_SHIELD_MAX_VA,LX_SHIELD_DELAY_VA,LX_SHIELD_RATE_VA,LX_SHIELD_WRITE_VA};
/* The slots as the loader leaves them: the getters' addresses in this image. */
static void tsc_relocate(void) {
    for(unsigned i=0;i<4;i++) {const uint64_t to=(uint64_t)(uintptr_t)LX(tsc_slot_to[i]);memcpy(LX(tsc_slot_at[i]),&to,8);}
}
static void tsc_writable(uint32_t va) {
    const uintptr_t page=(uintptr_t)LX(va)&~(uintptr_t)(GATE_PAGE-1);
    assert(!mprotect((void*)page,2*GATE_PAGE,PROT_READ|PROT_WRITE|PROT_EXEC));
}
/* Every pin refuses when touched: a byte of the read, each slot. */
static void tsc_tamper(void) {
    assert(lxw_shield_canonical_pins());
    const lx_site *read=site_named("shield_recharge");assert(read);
    const uint32_t bytes[]={read->va,read->va+0x1a0u,read->va+read->length-1u};
    for(unsigned i=0;i<sizeof(bytes)/sizeof(*bytes);i++) {
        tsc_writable(bytes[i]);*LX(bytes[i])^=1;assert(!lxw_shield_canonical_pins());*LX(bytes[i])^=1;
    }
    for(unsigned i=0;i<4;i++) {
        uint64_t slot;memcpy(&slot,LX(tsc_slot_at[i]),8);const uint64_t other=slot+0x10u;
        memcpy(LX(tsc_slot_at[i]),&other,8);assert(!lxw_shield_canonical_pins());memcpy(LX(tsc_slot_at[i]),&slot,8);
    }
    assert(lxw_shield_canonical_pins());
}
static void test_shield_canonical_linux(void) {
    wt_lua();p_lua_tolstring=tsc_tolstring;
    uint64_t saved_slots[4];for(unsigned i=0;i<4;i++) memcpy(&saved_slots[i],LX(tsc_slot_at[i]),8);
    static unsigned char object[0x300],group[LXW_SG_SPAN],group_copy[LXW_SG_SPAN];char raw[192],canon[192],ui[192];
    memset(object,0,sizeof(object));memset(group,0,sizeof(group));
    shield_canonical=0;shield_canonical_count=0;
    const double before=18.87,after=18.88,early=1.0;
    assert(lxw_shield_state(object,raw,NULL) && !strcmp(raw,"none") && lxw_shield_state(object,canon,&after) && !strcmp(canon,"none"));
    *(void**)(object+LXW_OBJ_SHIELD)=group;
    *(void**)(group+LXW_SG_SOURCE_VTABLE)=LX(LX_SHIELD_SOURCE_VTABLE);*(void**)(group+LXW_SG_SOURCE_GROUP)=group;
    *(void**)(group+LXW_SG_SOURCE)=group+LXW_SG_SOURCE_VTABLE;
    *(uintptr_t*)(group+LXW_SG_BEGIN)=8;*(uintptr_t*)(group+LXW_SG_END)=16;
    const uint64_t v=0x40921278d4fdf3b6ULL,c=0x4008aaa8eb463498ULL;const double max=1196.0,delay=15.0;const float r=1.0f,f=50.0f;
    memcpy(group+LXW_SG_VALUE,&v,8);memcpy(group+LXW_SG_CHANGED,&c,8);memcpy(group+LXW_SG_MAXIMUM,&max,8);memcpy(group+LXW_SG_DELAY,&delay,8);
    memcpy(group+LXW_SG_RATE,&r,4);memcpy(group+LXW_SG_FACTOR,&f,4);
    memcpy(group_copy,group,sizeof(group));
    /* Not full yet: the stored fields. Full: value = max, changed 0, the rest as stored (the Windows text). */
    assert(lxw_shield_state(object,raw,NULL) && lxw_shield_state(object,canon,&before) && !strcmp(raw,canon) && !shield_canonical_count);
    assert(!strncmp(raw,"40921278d4fdf3b64008aaa8eb463498",32));
    assert(lxw_shield_state(object,canon,&after) && strcmp(raw,canon) && shield_canonical_count==1);
    assert(!strncmp(canon,"4092b000000000000000000000000000",32) && !strcmp(canon+32,raw+32));
    assert(!memcmp(group,group_copy,sizeof(group))); /* nothing written */
    /* A UI read that wrote value = max, changed 0 hashes alike and is no rewrite; already full at any time, whatever its
     * changed time (a rewrite: the time goes). */
    const uint64_t full=0x4092b00000000000ULL,zero=0;memcpy(group+LXW_SG_VALUE,&full,8);memcpy(group+LXW_SG_CHANGED,&zero,8);
    assert(lxw_shield_state(object,ui,NULL) && !strcmp(ui,canon) && lxw_shield_state(object,ui,&after) && !strcmp(ui,canon) && shield_canonical_count==1);
    memcpy(group+LXW_SG_CHANGED,&c,8);
    assert(lxw_shield_state(object,raw,NULL) && strcmp(raw,canon) && lxw_shield_state(object,ui,&early) && !strcmp(ui,canon) && shield_canonical_count==2);
    const double near=1196.0+1e-9;memcpy(group+LXW_SG_VALUE,&near,8);memcpy(group+LXW_SG_CHANGED,&zero,8); /* full, other bits: a rewrite */
    assert(lxw_shield_state(object,ui,&early) && !strcmp(ui,canon) && shield_canonical_count==3);
    memcpy(group+LXW_SG_CHANGED,&c,8);
    /* A NaN value is not full (the read's test is false when unordered): the stored fields. */
    const uint64_t nan_bits=0x7ff8000000000000ULL,counted=shield_canonical_count;memcpy(group+LXW_SG_VALUE,&nan_bits,8);
    assert(lxw_shield_state(object,raw,NULL) && lxw_shield_state(object,canon,&after) && !strcmp(raw,canon) && shield_canonical_count==counted);
    memcpy(group+LXW_SG_VALUE,&v,8);
    /* The source must be the group's own ShieldRechargeSource; the stored form does not ask. */
    *(void**)(group+LXW_SG_SOURCE)=group;assert(!lxw_shield_state(object,canon,&after) && lxw_shield_state(object,raw,NULL));
    *(void**)(group+LXW_SG_SOURCE)=group+LXW_SG_SOURCE_VTABLE;
    *(void**)(group+LXW_SG_SOURCE_VTABLE)=LX(LX_SHIELD_HOLDER_VTABLE);assert(!lxw_shield_state(object,canon,&after) && !lxw_shield_state(object,raw,NULL));
    *(void**)(group+LXW_SG_SOURCE_VTABLE)=LX(LX_SHIELD_SOURCE_VTABLE);
    /* Arming: 'canonical' only, the pins, before the first capture; once on it stays on. */
    const uint64_t scans=world_hash_scans;world_hash_scans=0;shield_canonical_count=0;
    tsc_relocate();tsc_tamper();
    assert(lua_world_hash_shields_status(NULL)==1 && !strcmp(wt_exported,"mode=off canonical=0"));
    const char *refused[]={NULL,"","view","canonica","canonicalx","Canonical"};
    for(unsigned i=0;i<sizeof(refused)/sizeof(*refused);i++) {tsc_mode=refused[i];assert(lua_world_hash_shields(NULL)==1 && wt_result==0 && !shield_canonical);}
    tsc_mode="canonical";
    const int active=stats.active;stats.active=0;assert(lua_world_hash_shields(NULL)==1 && wt_result==0 && !shield_canonical);stats.active=1;
    uint64_t slot;memcpy(&slot,LX(tsc_slot_at[2]),8);const uint64_t other=slot+1u;memcpy(LX(tsc_slot_at[2]),&other,8);
    assert(lua_world_hash_shields(NULL)==1 && wt_result==0 && !shield_canonical);memcpy(LX(tsc_slot_at[2]),&slot,8);
    world_hash_scans=1;assert(lua_world_hash_shields(NULL)==1 && wt_result==0 && !shield_canonical);
    world_hash_scans=0;assert(lua_world_hash_shields(NULL)==1 && wt_result==1 && shield_canonical==1);
    world_hash_scans=1;assert(lua_world_hash_shields(NULL)==1 && wt_result==1 && shield_canonical==1);
    stats.active=active;
    assert(lxw_shield_state(object,canon,&after) && shield_canonical_count==1);
    assert(lua_world_hash_shields_status(NULL)==1 && !strcmp(wt_exported,"mode=canonical canonical=1"));
    /* The capture reads the thread's game time first: not finite, it stops there. */
    world_hash_scans=scans;
    double *time=gate_tls_time(gate_tls_self());assert(time);double kept;memcpy(&kept,time,8);
    const double nan=NAN;memcpy(time,&nan,8);
    const unsigned schema=world_hash_scans?world_hash_schema:1u;
    assert(!world_hash_capture(world_hash_tick+1u,schema) && !strcmp(world_hash_stage,"shield_time"));
    memcpy(time,&kept,8);
    shield_canonical=0;shield_canonical_count=0;tsc_mode=NULL;
    for(unsigned i=0;i<4;i++) memcpy(LX(tsc_slot_at[i]),&saved_slots[i],8);
    printf("world hash shields canonical (Linux layout, Windows text) PASS\n");
}
/* rel32 target of the instruction at va whose displacement starts after prefix bytes. */
static uint32_t tsc_rip(uint32_t va,unsigned prefix) {int32_t rel;memcpy(&rel,LX(va+prefix),4);return va+prefix+4u+(uint32_t)rel;}
static void test_shield_canonical_elf(void) {
    /* The file holds the getters' link-time addresses; the loader leaves them in place (ET_EXEC at 0x400000). */
    uint64_t saved_slots[4];
    for(unsigned i=0;i<4;i++) {memcpy(&saved_slots[i],LX(tsc_slot_at[i]),8);assert(saved_slots[i]==tsc_slot_to[i]);}
    uint64_t read_slot;memcpy(&read_slot,LX(LX_SHIELD_HOLDER_VTABLE+0x30u),8);assert(read_slot==0x1293330u);
    const lx_site *read=site_named("shield_recharge");assert(read && read->va==0x1293330u);
    tsc_relocate();tsc_tamper();
    /* The read: the holder in rbx, the source at holder+0x18 (group+0x70), each getter's slot against its address. */
    assert(!memcmp(LX(0x1293331u),"\x48\x89\xfb",3) && !memcmp(LX(0x1293343u),"\x48\x8b\x7f\x18",4));
    assert(!memcmp(LX(0x1293334u),"\x48\x8d\x15",3) && tsc_rip(0x1293334u,3)==LX_SHIELD_MAX_VA && !memcmp(LX(0x129334au),"\x48\x8b\x40\x08",4));
    assert(!memcmp(LX(0x12933c9u),"\x48\x8d\x15",3) && tsc_rip(0x12933c9u,3)==LX_SHIELD_DELAY_VA && !memcmp(LX(0x12933d8u),"\x48\x8b\x40\x10",4));
    assert(!memcmp(LX(0x1293449u),"\x48\x8d\x15",3) && tsc_rip(0x1293449u,3)==LX_SHIELD_RATE_VA && !memcmp(LX(0x1293453u),"\x48\x8b\x40\x18",4));
    assert(!memcmp(LX(0x12935ccu),"\x48\x8d\x15",3) && tsc_rip(0x12935ccu,3)==LX_SHIELD_WRITE_VA && !memcmp(LX(0x12935d3u),"\x48\x8b\x40\x20",4));
    /* The changed time at holder+0x10 (group+0x68), the inlined getters' fields, the write: changed 0, value = max at +8. */
    assert(!memcmp(LX(0x12933d0u),"\xf2\x0f\x10\x5b\x10",5) && !memcmp(LX(0x1293364u),"\xf2\x0f\x10\x40\x40",5));
    assert(!memcmp(LX(0x12935e2u),"\x48\xc7\x43\x10\x00\x00\x00\x00",8) && !memcmp(LX(0x12935eau),"\xf2\x0f\x11\x5b\x08",5));
    /* The full test's 1e-7 and the rate floor 1e-4f. */
    assert(!memcmp(LX(0x1293399u),"\xf2\x0f\x10\x25",4));double minimum;memcpy(&minimum,LX(tsc_rip(0x1293399u,4)),8);assert(minimum==1e-7);
    assert(!memcmp(LX(0x12934b3u),"\xf3\x0f\x10\x2d",4));float floor_;memcpy(&floor_,LX(tsc_rip(0x12934b3u,4)),4);assert(floor_==9.99999975e-05f);
    /* The construct: the holder's vtable at +0x58, the source pointer +0x70 = group+0x78, the source's vtable there. */
    assert(!memcmp(LX(0x17be400u),"\x48\x8d\x3d",3) && tsc_rip(0x17be400u,3)==LX_SHIELD_HOLDER_VTABLE && !memcmp(LX(0x17be40bu),"\x48\x89\x7b\x58",4));
    assert(!memcmp(LX(0x17be407u),"\x48\x8d\x53\x78",4) && !memcmp(LX(0x17be416u),"\x48\x89\x53\x70",4));
    assert(!memcmp(LX(0x17be40fu),"\x48\x8d\x3d",3) && tsc_rip(0x17be40fu,3)==LX_SHIELD_SOURCE_VTABLE && !memcmp(LX(0x17be41au),"\x48\x89\x7b\x78",4));
    for(unsigned i=0;i<4;i++) memcpy(LX(tsc_slot_at[i]),&saved_slots[i],8);
    printf("world hash shields ELF PASS\n");
}
