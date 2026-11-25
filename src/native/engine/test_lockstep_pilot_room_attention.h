/* A slot's player's room at inroom (lockstep_pilot_room_attention.h): planted players of slots 2, 3 and 8, their rooms and
 * ships on the test image. The rule (a seated slot's room, a room further up, an unseated player, another room, a room
 * X4 already has at inroom), the pass (recompute below inroom only, a room X4 does not recompute, a parent that is no
 * room), the status, and the hook refused on bytes that are not X4's. */
static int32_t test_prat_level=9;
static int32_t test_prat_compute(void *room) {
    (void)room;
    return test_prat_level;
}
static unsigned test_prat_set_calls;
static void *test_prat_set_last;
static void test_prat_set(void *component,int level) {
    assert(level==1);
    ++test_prat_set_calls;
    test_prat_set_last=component;
}
static void test_prat_pointer(unsigned char *at,const void *value) {
    memcpy(at,&value,8);
}
static void test_pilot_room_attention(void) {
    _Alignas(16) static unsigned char players[3][0x200];   /* slots 2, 3, 8 */
    _Alignas(16) static unsigned char rooms[4][0x200];     /* slot 2's cockpit, slot 3's control room, slot 8's room, another */
    _Alignas(16) static unsigned char inner[0x200];        /* a room inside slot 8's room (slot 8 sits there) */
    _Alignas(16) static unsigned char ships[3][0x200];
    static const unsigned slots[3]={2,3,PILOT_SLOTS};
    unsigned char *const saved_base=pnat_base;
    pslot_slot saved[3];
    for(unsigned i=0;i<3;i++) {
        saved[i]=pslots[slots[i]];
    }
    memset(players,0,sizeof(players));
    memset(rooms,0,sizeof(rooms));
    memset(inner,0,sizeof(inner));
    memset(ships,0,sizeof(ships));
    pnat_base=image;
    /* The world: P_2 in its cockpit, P_3 in a control room, P_8 in a room inside a room; each room in its ship. */
    test_prat_pointer(rooms[0],image+prat_vtables[1]);   /* U::Cockpit */
    test_prat_pointer(rooms[1],image+prat_vtables[2]);   /* U::ControlRoom */
    test_prat_pointer(rooms[2],image+prat_vtables[0]);   /* U::Room */
    test_prat_pointer(rooms[3],image+prat_vtables[0]);
    test_prat_pointer(inner,image+prat_vtables[0]);
    test_prat_pointer(rooms[0]+PRAT_PARENT,ships[0]);
    test_prat_pointer(rooms[1]+PRAT_PARENT,ships[1]);
    test_prat_pointer(rooms[2]+PRAT_PARENT,ships[2]);
    test_prat_pointer(rooms[3]+PRAT_PARENT,ships[2]);
    test_prat_pointer(inner+PRAT_PARENT,rooms[2]);
    test_prat_pointer(players[0]+PRAT_PARENT,rooms[0]);
    test_prat_pointer(players[1]+PRAT_PARENT,rooms[1]);
    test_prat_pointer(players[2]+PRAT_PARENT,inner);
    for(unsigned i=0;i<3;i++) {
        memset(&pslots[slots[i]],0,sizeof(pslot_slot));
        pslots[slots[i]].player=players[i];
        pslots[slots[i]].seated=1;
    }
    prat_compute_override=test_prat_compute;
    prat_set_override=test_prat_set;
    prat_reset();
    prat_why="-";          /* an earlier test may have run the seat's install with the option off */
    prat_failed_rva=0;
    /* The rule: a seated slot's own room and a room further up are inroom; another room and the ship keep X4's answer. */
    test_prat_level=9;
    assert(prat_room_attention(rooms[0])==PRAT_INROOM && prat_holder(rooms[0])==2);
    assert(prat_room_attention(rooms[1])==PRAT_INROOM && prat_holder(rooms[1])==3);
    assert(prat_room_attention(inner)==PRAT_INROOM && prat_holder(inner)==PILOT_SLOTS);
    assert(prat_room_attention(rooms[2])==PRAT_INROOM && prat_holder(rooms[2])==PILOT_SLOTS);   /* an ancestor of P_8 */
    assert(prat_room_attention(rooms[3])==9 && !prat_holder(rooms[3]));                         /* the same ship, no player */
    assert(prat_raised==4 && prat_asked==5);
    test_prat_level=5;                                                                          /* the dip at a zone change */
    assert(prat_room_attention(rooms[0])==PRAT_INROOM && prat_room_attention(rooms[3])==5);
    test_prat_level=PRAT_INROOM;                                                                /* X4 has it there already */
    assert(prat_room_attention(rooms[3])==PRAT_INROOM && prat_raised==5);
    test_prat_level=9;
    pslots[3].seated=0;                                                                         /* not seated: X4's answer */
    assert(prat_room_attention(rooms[1])==9);
    pslots[3].seated=1;
    /* The pass: only with the hooks in; a seated slot's room below inroom is recomputed by X4, at inroom left alone, a room
     * X4 does not recompute (+0xd1 clear) left alone, a parent that is no room (P_8's is a room) left alone. */
    {   const int32_t low=9,high=PRAT_INROOM;
        const unsigned char live=1;
        memcpy(rooms[0]+PRAT_ATTENTION,&low,4);
        memcpy(rooms[1]+PRAT_ATTENTION,&high,4);
        memcpy(inner+PRAT_ATTENTION,&low,4);
        memcpy(rooms[0]+PRAT_LIVE,&live,1);
        memcpy(rooms[1]+PRAT_LIVE,&live,1);}
    prat_installed=0;
    test_prat_set_calls=0;
    prat_pass();
    assert(!test_prat_set_calls);
    prat_installed=1;
    prat_pass();
    assert(test_prat_set_calls==1 && test_prat_set_last==rooms[0] && prat_recomputed==1);   /* inner: +0xd1 clear */
    inner[PRAT_LIVE]=1;
    prat_pass();
    assert(test_prat_set_calls==3 && test_prat_set_last==inner);
    test_prat_pointer(players[2]+PRAT_PARENT,ships[2]);                                       /* a ship, no room */
    {   const int32_t low=9;
        memcpy(ships[2]+PRAT_ATTENTION,&low,4);
        ships[2][PRAT_LIVE]=1;}
    test_prat_set_calls=0;
    prat_pass();
    assert(test_prat_set_calls==1 && test_prat_set_last==rooms[0]);
    /* The status, then the hook on bytes that are not X4's: refused by its pins, the entries untouched. */
    {   char row[256];
        int n=snprintf(row,sizeof(row),"x");
        prat_on=1;
        n=prat_status(row,sizeof(row),n);
        if(n<=0 || !strstr(row," rat=1:9/5/4 rat_why=-")) {
            printf("pilot slots room attention status: %s\n",row);
        }
        assert(n>0 && strstr(row," rat=1:9/5/4 rat_why=-"));}
    prat_installed=0;
    {   void *entry=NULL;
        memcpy(&entry,image+prat_vtables[0]+8u*PRAT_SLOT,8);
        prat_on=0;
        prat_install();
        assert(!prat_installed && !strcmp(prat_why,"off"));
        prat_on=1;
        prat_install();
        assert(!prat_installed && !strcmp(prat_why,"pins") && prat_failed_rva==PRAT_ROOM_COMPUTE && prat_intact());
        void *now=NULL;
        memcpy(&now,image+prat_vtables[0]+8u*PRAT_SLOT,8);
        assert(now==entry);
        char row[256];
        int n=snprintf(row,sizeof(row),"x");
        assert(prat_status(row,sizeof(row),n)>0 && strstr(row," rat_why=pins:76e520"));
        prat_on=0;
        n=snprintf(row,sizeof(row),"x");
        assert(prat_status(row,sizeof(row),n)==n && !strcmp(row,"x"));}
    /* Back as the test found it. */
    prat_why="-";
    prat_failed_rva=0;
    prat_compute_override=NULL;
    prat_set_override=NULL;
    prat_reset();
    for(unsigned i=0;i<3;i++) {
        pslots[slots[i]]=saved[i];
    }
    pnat_base=saved_base;
    printf("pilot slots room attention: a seated slot's room and the rooms above it inroom for slots 2, 3 and 8, another room "
           "and an unseated player X4's answer, the pass (hooks out, below inroom, at inroom, not recomputed, no room), the "
           "status, the hook refused on bytes not X4's PASS\n");
}
/* The pinned native image: the room compute (its test of X4's player and its inroom answer), SetAttention (its recompute
 * of a component with +0xd1 through slot 703), the three entries; then the hooks in and out on a copy of those bytes. */
static void test_pilot_room_attention_image(const char *path) {
    FILE *f=fopen(path,"rb");
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS64 nt;
    IMAGE_SECTION_HEADER sections[32];
    static unsigned char bytes[0x400];
    assert(f);
    assert(fread(&dos,1,sizeof(dos),f)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && !fseek(f,dos.e_lfanew,SEEK_SET));
    assert(fread(&nt,1,sizeof(nt),f)==sizeof(nt) && nt.FileHeader.TimeDateStamp==0x6a1eaded && nt.FileHeader.NumberOfSections<=32);
    const unsigned count=nt.FileHeader.NumberOfSections;
    assert(fread(sections,sizeof(*sections),count,f)==count);
    assert(test_pnat_file_bytes(f,sections,count,PRAT_ROOM_COMPUTE,PRAT_ROOM_COMPUTE_SIZE,bytes) &&
           hash_ok(bytes,PRAT_ROOM_COMPUTE_SIZE,PRAT_ROOM_COMPUTE_SHA));
    {   static const unsigned char player[7]={0x48,0x8d,0xb0,0x30,0x02,0x00,0x00};   /* lea rsi,[rax+0x230]: the Universe's player */
        static const unsigned char inroom[5]={0xb8,0x0b,0x00,0x00,0x00};            /* mov eax,11 */
        assert(!memcmp(bytes+(0x76e579u-PRAT_ROOM_COMPUTE),player,sizeof(player)));
        assert(!memcmp(bytes+(0x76e5efu-PRAT_ROOM_COMPUTE),inroom,sizeof(inroom)));}
    assert(test_pnat_file_bytes(f,sections,count,PRAT_SET_ATTENTION,PRAT_SET_ATTENTION_SIZE,bytes) &&
           hash_ok(bytes,PRAT_SET_ATTENTION_SIZE,PRAT_SET_ATTENTION_SHA));
    {   static const unsigned char recompute[18]={0x80,0xb9,0xd1,0x00,0x00,0x00,0x00,0x74,0x0b,0x48,0x8b,0x01,0xff,0x90,0xf8,0x15,0x00,0x00};
        assert(!memcmp(bytes+(0x3d5671u-PRAT_SET_ATTENTION),recompute,sizeof(recompute)));}   /* cmp [rcx+0xd1],0; je; call [rax+0x15f8] */
    for(unsigned i=0;i<PRAT_VTABLES;i++) {
        uint64_t entry=0;
        assert(test_pnat_file_bytes(f,sections,count,prat_vtables[i]+8u*PRAT_SLOT,8,bytes));
        memcpy(&entry,bytes,8);
        assert(entry==0x140000000ULL+PRAT_ROOM_COMPUTE);
    }
    /* The hooks on a copy of the bytes and the entries: in, intact, out; a foreign entry and another build refused. */
    {   const size_t span=(size_t)SCRIPT_RNG_AI_VT+0x1000u;
        unsigned char *region=VirtualAlloc(NULL,span,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        unsigned char *const saved_base=pnat_base;
        const int saved_on=prat_on;
        void *entry=NULL;
        unsigned failed=1;
        assert(region);
        assert(test_pnat_file_bytes(f,sections,count,PRAT_ROOM_COMPUTE,PRAT_ROOM_COMPUTE_SIZE,region+PRAT_ROOM_COMPUTE));
        assert(test_pnat_file_bytes(f,sections,count,PRAT_SET_ATTENTION,PRAT_SET_ATTENTION_SIZE,region+PRAT_SET_ATTENTION));
        entry=region+PRAT_ROOM_COMPUTE;
        for(unsigned i=0;i<PRAT_VTABLES;i++) {
            memcpy(region+prat_vtables[i]+8u*PRAT_SLOT,&entry,8);
        }
        pnat_base=region;
        prat_on=1;
        prat_installed=0;
        assert(prat_arm_check(region,&failed) && !failed);
        prat_install();
        assert(prat_installed && !strcmp(prat_why,"-") && prat_intact());
        for(unsigned i=0;i<PRAT_VTABLES;i++) {
            memcpy(&entry,region+prat_vtables[i]+8u*PRAT_SLOT,8);
            assert(entry==(void*)prat_room_attention);
        }
        assert(prat_arm_check(region,&failed));                                                  /* a reloaded UI asks again */
        assert(prat_restore() && !prat_installed);
        for(unsigned i=0;i<PRAT_VTABLES;i++) {
            memcpy(&entry,region+prat_vtables[i]+8u*PRAT_SLOT,8);
            assert(entry==region+PRAT_ROOM_COMPUTE);
        }
        /* view points' own patches in both functions (its g_Universe load, its call of slot 703) read as X4's bytes;
         * anything else there refused. */
        {   unsigned char *const saved_page=view_page;
            unsigned char load[4],call[6],keep_load[4],keep_call[6];
            unsigned site=VIEW_SITES;
            for(unsigned i=0;i<VIEW_SITES;i++) {
                if(view_sites[i].rva==0x76e572u) {
                    site=i;
                }
            }
            assert(site<VIEW_SITES);
            view_page=image+0x10000u;
            view_site_bytes(site,1,load);
            view_call_bytes(1,call);
            memcpy(keep_load,region+0x76e572u+3,4);
            memcpy(keep_call,region+VIEW_CALL,6);
            memcpy(region+0x76e572u+3,load,4);
            memcpy(region+VIEW_CALL,call,6);
            assert(prat_arm_check(region,&failed) && !failed);
            region[0x76e572u+3]^=0x40;
            assert(!prat_arm_check(region,&failed) && failed==PRAT_ROOM_COMPUTE);
            memcpy(region+0x76e572u+3,keep_load,4);
            region[VIEW_CALL+2]^=0x40;
            assert(!prat_arm_check(region,&failed) && failed==PRAT_SET_ATTENTION);
            memcpy(region+VIEW_CALL,keep_call,6);
            view_page=saved_page;
            assert(prat_arm_check(region,&failed) && !failed);}
        entry=region+PRAT_SET_ATTENTION;                                                          /* a foreign entry */
        memcpy(region+prat_vtables[2]+8u*PRAT_SLOT,&entry,8);
        assert(!prat_arm_check(region,&failed) && failed==prat_vtables[2]+8u*PRAT_SLOT);
        entry=region+PRAT_ROOM_COMPUTE;
        memcpy(region+prat_vtables[2]+8u*PRAT_SLOT,&entry,8);
        region[PRAT_SET_ATTENTION+0x30]^=1;                                                       /* another build */
        assert(!prat_arm_check(region,&failed) && failed==PRAT_SET_ATTENTION);
        region[PRAT_SET_ATTENTION+0x30]^=1;
        region[PRAT_ROOM_COMPUTE+0x30]^=1;
        assert(!prat_arm_check(region,&failed) && failed==PRAT_ROOM_COMPUTE);
        prat_why="-";
        prat_on=saved_on;
        pnat_base=saved_base;
        VirtualFree(region,0,MEM_RELEASE);}
    fclose(f);
    printf("pinned native image pilot slots room attention verified (the room compute's test of X4's player and its inroom "
           "answer, SetAttention's recompute through slot 703, the three entries; the hooks in and out, a foreign entry and "
           "another build refused)\n");
}
