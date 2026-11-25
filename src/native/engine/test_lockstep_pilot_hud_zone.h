/* The HUD's zone scanners on a client of slot 2..N (lockstep_pilot_hud_zone.h): in the presentation scope a scanner that
 * holds another zone than the one X4's walk finds from X4's player (its parent the slot's room there) is rescanned by its
 * own slot 570 with the manager's player; a started scanner only, no other source, not outside the scope, not with
 * another player in the manager; X4's own rescan to slot 1's zone outside the scope is put right at the next pass; slots
 * 2, 3 and 8 alike. The image test reads what the module relies on. */
#define TPHZ_CLASS 0x200u   /* a test component: its class, one byte */
static unsigned char tphz_is_class(void *component,int klass) {return ((unsigned char*)component)[TPHZ_CLASS]==(unsigned char)klass;}
static unsigned char *tphz_manager;
static unsigned tphz_calls;
static void *tphz_old;
/* X4's slot 570 with the manager's player as the old one: no listener moves, the rescan (the zone from the manager's player). */
static void tphz_scan(void *source,void *old_player) {
    unsigned char *player=NULL,*zone=NULL;
    memcpy(&player,tphz_manager+PHZ_MANAGER_PLAYER,8);
    tphz_calls++;tphz_old=old_player;
    zone=phz_zone_of(player);
    memcpy((unsigned char*)source+PHZ_ZONE,&zone,8);
}
static unsigned char *tphz_held(const unsigned char *scanner) {unsigned char *h=NULL;memcpy(&h,scanner+PHZ_ZONE,8);return h;}
static void tphz_other_scan(void *source,void *old_player) {(void)source;(void)old_player;tphz_calls+=100;}
static void tphz_fault_scan(void *source,void *old_player) {(void)source;(void)old_player;*(volatile int*)(uintptr_t)8=1;}
static void test_pilot_hud_zone(void) {
    static unsigned char manager[0x100],p1[0x300],other[0x300],rooms[4][0x300],ships[4][0x300],zones[5][0x300],sector[0x300];
    static unsigned char nodes[3][0x60],scanners[4][0x60],room_scanner[0x60];
    static void *component_vt[0x11f0/8+1],*scan_vt[PHZ_SCAN_SLOT+1],*other_vt[PHZ_SCAN_SLOT+1],*fault_vt[PHZ_SCAN_SLOT+1];
    static unsigned char *vectors[3][4];
    unsigned char *const saved_p1=pslot_p1,**const saved_at=phz_manager_at;const void *const saved_scan=phz_scan;
    const int saved_on=phz_on;const LONG saved_open=pres_open;
    memset(component_vt,0,sizeof(component_vt));component_vt[PHZ_IS_CLASS/8]=(void*)tphz_is_class;
    scan_vt[PHZ_SCAN_SLOT]=(void*)tphz_scan;other_vt[PHZ_SCAN_SLOT]=(void*)tphz_other_scan;fault_vt[PHZ_SCAN_SLOT]=(void*)tphz_fault_scan;
    void *cvt=component_vt,*svt=scan_vt,*ovt=other_vt,*fvt=fault_vt,*none=NULL;
    #define TPHZ_PUT(buffer,offset,value) do {void *v_=(void*)(value);memcpy((unsigned char*)(buffer)+(offset),&v_,8);} while(0)
    /* the components: zones 0 (slot 1's), 1..3 (the slots'), 4 (an outer zone without a sector); their parents */
    for(unsigned i=0;i<5;i++) {memcpy(zones[i],&cvt,8);zones[i][TPHZ_CLASS]=PHZ_ZONE_CLASS;TPHZ_PUT(zones[i],PHZ_PARENT,sector);uint64_t id=0x3d400u+i;memcpy(zones[i]+PHZ_ID,&id,8);}
    memcpy(sector,&cvt,8);sector[TPHZ_CLASS]=0x58;TPHZ_PUT(sector,PHZ_PARENT,none);
    for(unsigned i=0;i<4;i++) {
        memcpy(rooms[i],&cvt,8);rooms[i][TPHZ_CLASS]=0x54;TPHZ_PUT(rooms[i],PHZ_PARENT,ships[i]);
        memcpy(ships[i],&cvt,8);ships[i][TPHZ_CLASS]=0x49;TPHZ_PUT(ships[i],PHZ_PARENT,zones[i]);
    }
    memcpy(p1,&cvt,8);p1[TPHZ_CLASS]=0x2e;TPHZ_PUT(p1,PHZ_PARENT,rooms[0]);   /* X4's player in slot 1's cockpit */
    memcpy(other,&cvt,8);other[TPHZ_CLASS]=0x2e;TPHZ_PUT(other,PHZ_PARENT,rooms[1]);
    /* the manager: X4's player; the map: header at +0x20 (its left the root), root nodes[1] with nodes[0] left and nodes[2] right */
    memset(manager,0,sizeof(manager));tphz_manager=manager;TPHZ_PUT(manager,PHZ_MANAGER_PLAYER,p1);
    TPHZ_PUT(manager,PHZ_MAP_HEADER+PHZ_NODE_LEFT,nodes[1]);TPHZ_PUT(manager,PHZ_MAP_FIRST,nodes[0]);
    memset(nodes,0,sizeof(nodes));
    TPHZ_PUT(nodes[1],PHZ_NODE_PARENT,manager+PHZ_MAP_HEADER);TPHZ_PUT(nodes[1],PHZ_NODE_LEFT,nodes[0]);TPHZ_PUT(nodes[1],PHZ_NODE_RIGHT,nodes[2]);
    TPHZ_PUT(nodes[0],PHZ_NODE_PARENT,nodes[1]);TPHZ_PUT(nodes[2],PHZ_NODE_PARENT,nodes[1]);
    /* the sources: node 0 a scanner and another source (a room scanner), node 1 a scanner X4 has not started, node 2 two scanners */
    vectors[0][0]=scanners[0];vectors[0][1]=room_scanner;vectors[1][0]=scanners[1];vectors[2][0]=scanners[2];vectors[2][1]=scanners[3];
    const unsigned sizes[3]={2,1,2};
    for(unsigned i=0;i<3;i++) {TPHZ_PUT(nodes[i],PHZ_NODE_BEGIN,&vectors[i][0]);TPHZ_PUT(nodes[i],PHZ_NODE_END,&vectors[i][sizes[i]]);}
    for(unsigned i=0;i<4;i++) {memset(scanners[i],0,sizeof(scanners[i]));memcpy(scanners[i],&svt,8);scanners[i][PHZ_STARTED]=i!=1;TPHZ_PUT(scanners[i],PHZ_ZONE,zones[0]);}
    memset(room_scanner,0,sizeof(room_scanner));memcpy(room_scanner,&ovt,8);room_scanner[PHZ_STARTED]=1;TPHZ_PUT(room_scanner,PHZ_ZONE,zones[0]);
    unsigned char *manager_at=manager;
    pslot_p1=p1;phz_manager_at=&manager_at;phz_scan=(const void*)tphz_scan;phz_on=1;phz_reset();
    #define TPHZ_HELD(i) tphz_held(scanners[i])
    /* outside the scope: nothing */
    InterlockedExchange(&pres_open,0);tphz_calls=0;phz_scope_pass();assert(!tphz_calls && !phz_passes);
    const unsigned slots[3]={2,3,8};
    for(unsigned j=0;j<3;j++) {
        const unsigned room=1+j%3;   /* slot k's room in its own ship and zone */
        (void)slots[j];
        InterlockedExchange(&pres_open,1);TPHZ_PUT(p1,PHZ_PARENT,rooms[room]);   /* the scope: X4's player in slot k's room */
        const LONG64 rescans=phz_rescans;tphz_calls=0;
        phz_scope_pass();
        assert(tphz_calls==3 && tphz_old==p1 && phz_rescans==rescans+3 && phz_last_scanners==4);
        assert(TPHZ_HELD(0)==zones[room] && TPHZ_HELD(2)==zones[room] && TPHZ_HELD(3)==zones[room]);
        assert(TPHZ_HELD(1)==zones[0]);   /* not started: X4 starts it */
        {unsigned char *h=NULL;memcpy(&h,room_scanner+PHZ_ZONE,8);assert(h==zones[0]);}   /* another source's slot 570: not asked */
        assert(phz_last_to==0x3d400u+room);
        /* the same zone at the next frame: nothing */
        tphz_calls=0;phz_scope_pass();assert(!tphz_calls && phz_rescans==rescans+3);
        /* X4's own rescan outside the scope (slot 1's zone change): put right at the next pass in the scope */
        TPHZ_PUT(scanners[2],PHZ_ZONE,zones[0]);tphz_calls=0;phz_scope_pass();
        assert(tphz_calls==1 && TPHZ_HELD(2)==zones[room] && phz_last_from==0x3d400u && phz_last_to==0x3d400u+room);
        TPHZ_PUT(p1,PHZ_PARENT,rooms[0]);InterlockedExchange(&pres_open,0);
        for(unsigned i=0;i<4;i++) if(i!=1) TPHZ_PUT(scanners[i],PHZ_ZONE,zones[0]);
    }
    InterlockedExchange(&pres_open,1);TPHZ_PUT(p1,PHZ_PARENT,rooms[1]);
    /* another player in the manager: left as it is */
    {   const LONG64 other_count=phz_other;TPHZ_PUT(manager,PHZ_MANAGER_PLAYER,other);tphz_calls=0;phz_scope_pass();
        assert(!tphz_calls && phz_other==other_count+1);TPHZ_PUT(manager,PHZ_MANAGER_PLAYER,p1);}
    /* no zone above X4's player (a parent chain without one): nothing */
    {   const LONG64 nozone=phz_nozone;TPHZ_PUT(p1,PHZ_PARENT,sector);tphz_calls=0;phz_scope_pass();
        assert(!tphz_calls && phz_nozone==nozone+1);TPHZ_PUT(p1,PHZ_PARENT,rooms[1]);}
    /* off: nothing */
    phz_on=0;tphz_calls=0;{const LONG64 passes=phz_passes;phz_scope_pass();assert(!tphz_calls && phz_passes==passes);}phz_on=1;
    {char row[160];assert(phz_status(row,sizeof(row)) && !strncmp(row," hud_zone=1/",12) && strstr(row,"/3d400>3d403"));}
    /* a fault in X4's rescan: this module off on this client, counted; the next pass does nothing */
    {   const LONG64 faults=phz_faults;phz_scan=(const void*)tphz_fault_scan;memcpy(scanners[0],&fvt,8);
        phz_scope_pass();assert(!phz_on && phz_faults==faults+1);
        tphz_calls=0;phz_scope_pass();assert(!tphz_calls);
        memcpy(scanners[0],&svt,8);}
    #undef TPHZ_HELD
    #undef TPHZ_PUT
    InterlockedExchange(&pres_open,saved_open);pslot_p1=saved_p1;phz_manager_at=saved_at;phz_scan=saved_scan;phz_on=saved_on;phz_reset();
    printf("pilot hud zone: the zone scanners on slot k's zone in its scope (slots 2, 3, 8), X4's outside rescan put right, other sources, not started, another player, no zone, off, fault tests PASS\n");
}
static void test_pilot_hud_zone_image(const char *path) {
    FILE *f=fopen(path,"rb");assert(f);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;IMAGE_SECTION_HEADER sections[32];
    assert(fread(&dos,1,sizeof(dos),f)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && !fseek(f,dos.e_lfanew,SEEK_SET));
    assert(fread(&nt,1,sizeof(nt),f)==sizeof(nt) && nt.FileHeader.TimeDateStamp==0x6a1eaded && nt.FileHeader.NumberOfSections<=32);
    unsigned count=nt.FileHeader.NumberOfSections;
    assert(fread(sections,sizeof(*sections),count,f)==count);
    static unsigned char bytes[0x400];
    for(unsigned i=0;i<PHZ_PIN_COUNT;i++)
        assert(test_pnat_file_bytes(f,sections,count,phz_pin_table[i].rva,phz_pin_table[i].size,bytes) && hash_ok(bytes,phz_pin_table[i].size,phz_pin_table[i].sha));
    /* the four classes (their type descriptors' names), slot 570 the scanner's rescan and slot 569 its start */
    static const char *const names[PHZ_VTABLES]={".?AVAdjacentZoneScanner@ICom@UI@@",".?AVBasicMessageSource@ICom@UI@@",
        ".?AVFastObjectSource@ICom@UI@@",".?AVPlayerCapShipSource@ICom@UI@@"};
    for(unsigned i=0;i<PHZ_VTABLES;i++) {
        uint64_t col_va=0,v=0;unsigned td=0;char text[64];
        assert(test_pnat_file_bytes(f,sections,count,phz_vtables[i]-8,8,bytes));memcpy(&col_va,bytes,8);
        assert(test_pnat_file_bytes(f,sections,count,(unsigned)(col_va-0x140000000ULL)+12,4,bytes));memcpy(&td,bytes,4);
        assert(test_pnat_file_bytes(f,sections,count,td+16,sizeof(text),(unsigned char*)text));text[sizeof(text)-1]=0;
        assert(!strcmp(text,names[i]));
        assert(test_pnat_file_bytes(f,sections,count,phz_vtables[i]+8u*569u,16,bytes));
        memcpy(&v,bytes,8);assert(v==0x140000000ULL+0xad8f00u);memcpy(&v,bytes+8,8);assert(v==0x140000000ULL+PHZ_SCAN);
    }
    /* rip-relative loads of the manager: the rescan, slot 570, and the UI controller's copy of [0x6cf2788] */
    #define TPHZ_RIP(site,len,target) do {int32_t r_;assert(test_pnat_file_bytes(f,sections,count,(site),(len),bytes));memcpy(&r_,bytes+(len)-4,4); \
        assert((unsigned)((site)+(len))+(unsigned)r_==(unsigned)(target));} while(0)
    TPHZ_RIP(0xad8f59u,7,PHZ_MANAGER);TPHZ_RIP(0xad8dcfu,7,PHZ_MANAGER);TPHZ_RIP(0xb3b7eeu,7,PHZ_MANAGER);TPHZ_RIP(0xb3b7e7u,7,0x6cf2788u);
    #undef TPHZ_RIP
    /* the rescan: the manager's player (+0x60), IsOfClass 0x6d (vtable +0x11e8) up the parents (+0x70), the scanner's zone +0x28 */
    static const struct {unsigned rva;unsigned char size,code[8];} sites[]={
        {0xad8f60u,4,{0x48,0x8b,0x70,0x60}},{0xad8f73u,5,{0xba,0x6d,0,0,0}},{0xad8f7bu,6,{0xff,0x90,0xe8,0x11,0,0}},{0xad8f85u,4,{0x48,0x8b,0x76,0x70}},
        {0xad8f90u,4,{0x49,0x8b,0x7e,0x28}},
        /* slot 569: +0x18, then slot 570 (+0x11d0) */
        {0xad8f06u,4,{0x80,0x79,0x18,0}},{0xad8f14u,6,{0xff,0x90,0xd0,0x11,0,0}},
        /* slot 570: the manager's player, compared with the old one; the thread's time; the rescan */
        {0xad8ddcu,4,{0x48,0x8b,0x5b,0x60}},{0xad8ed2u,5,{0xb8,0,3,0,0}},{0xad8eeeu,5,{0xe9,0x3d,0,0,0}},
        /* the setter's walk: the header +0x20, the first node at header +0x20, a node's sources +0x28..+0x30, slot 570 */
        {0xaea58cu,4,{0x4c,0x8d,0x73,0x20}},{0xaea590u,4,{0x49,0x8b,0x5e,0x20}},{0xaea5a0u,4,{0x48,0x8b,0x6b,0x30}},
        {0xaea5a4u,4,{0x48,0x8b,0x73,0x28}},{0xaea5b9u,6,{0xff,0x90,0xd0,0x11,0,0}}};
    for(unsigned i=0;i<sizeof(sites)/sizeof(*sites);i++)
        assert(test_pnat_file_bytes(f,sections,count,sites[i].rva,sites[i].size,bytes) && !memcmp(bytes,sites[i].code,sites[i].size));
    fclose(f);
    printf("pilot hud zone image: pins, the four scanner classes' slots 569/570, the manager's loads and offsets PASS\n");
}
