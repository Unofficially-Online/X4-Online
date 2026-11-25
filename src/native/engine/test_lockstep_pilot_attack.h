/* The player's attacks for every slot (lockstep_pilot_attack.h) on fake engine memory: X4's controlled-ship rule for
 * slot 1 and slots 2, 3 and 8, whose shot it is, the decision of the attacked-event poster (X4's own answer, a slot's player,
 * inside a slot's scope, nobody), the real stub executed from a hand-made caller with X4's registers and stack, the arm by
 * CAS, the tick check and the restore. Native image: the pins, the site's bytes and both of its jumps, the player's
 * "controls something" vfunc. */
static void *test_patk_ship_vt[0x1200/8+1],*test_patk_room_vt[0x1200/8+1],*test_patk_player_vt[0x1c50/8+1];
static unsigned char test_patk_ship_is(void *component,int cls) {(void)component;return cls==PATK_CLASS_CONTROLLED;}
static unsigned char test_patk_room_is(void *component,int cls) {(void)component;(void)cls;return 0;}
static unsigned char test_patk_controls(void *player,unsigned char any) {(void)player;return any==0;}
static unsigned char *test_patk_universe_player;
static unsigned char *test_patk_singleton(void) {return test_patk_universe_player;}
/* X4's post and the scope-and-forward step, recorded in order (1 forward, 2 post). */
typedef struct {unsigned what,slot;void *source,*event;} test_patk_call;
static test_patk_call test_patk_calls[16];
static unsigned test_patk_call_count;
static void test_patk_told(unsigned k,unsigned char *event,double time) {
    (void)time;assert(test_patk_call_count<16);test_patk_calls[test_patk_call_count++]=(test_patk_call){1,k,NULL,event};
}
static uintptr_t test_patk_native_post(void *source,void *event,double time,unsigned char immediate) {
    (void)time;assert(immediate==1 && test_patk_call_count<16);test_patk_calls[test_patk_call_count++]=(test_patk_call){2,0,source,event};
    return 0;
}
typedef struct {unsigned char ship[0x100],room[0x100],player[0x400];unsigned char *vector[3];} test_patk_seat;
/* A ship with its cockpit room and a player seated there: +0x70 the room, the vector {NULL, ship} as the slot module
 * seats P_k (the pilot post's index after an empty one). */
static void test_patk_seat_make(test_patk_seat *s,void *zone) {
    memset(s,0,sizeof(*s));void *svt=test_patk_ship_vt,*rvt=test_patk_room_vt,*pvt=test_patk_player_vt,*ship=s->ship,*room=s->room;
    memcpy(s->ship,&svt,8);memcpy(s->ship+0x70,&zone,8);memcpy(s->room,&rvt,8);memcpy(s->room+0x70,&ship,8);
    memcpy(s->player,&pvt,8);memcpy(s->player+0x70,&room,8);
    s->vector[0]=NULL;s->vector[1]=s->ship;
    {static uint64_t next=0x9001;uint64_t id=next++;memcpy(s->ship+8,&id,8);}
    unsigned char **begin=&s->vector[0],**end=&s->vector[2];memcpy(s->player+0x308,&begin,8);memcpy(s->player+0x310,&end,8);
}
static void test_patk_native_image(const char *path) {
    FILE *f=fopen(path,"rb");assert(f);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;IMAGE_SECTION_HEADER sections[32];
    assert(fread(&dos,1,sizeof(dos),f)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && !fseek(f,dos.e_lfanew,SEEK_SET));
    assert(fread(&nt,1,sizeof(nt),f)==sizeof(nt) && nt.FileHeader.TimeDateStamp==0x6a1eaded && nt.FileHeader.NumberOfSections<=32);
    assert(fread(sections,sizeof(*sections),nt.FileHeader.NumberOfSections,f)==nt.FileHeader.NumberOfSections);
    static unsigned char bytes[0x400];
    #define TEST_PATK_READ(rva,size) do { unsigned offset_=0; \
        for(unsigned j_=0;j_<nt.FileHeader.NumberOfSections;j_++) \
            if((rva)>=sections[j_].VirtualAddress && (rva)+(size)<=sections[j_].VirtualAddress+sections[j_].SizeOfRawData) \
                offset_=sections[j_].PointerToRawData+(rva)-sections[j_].VirtualAddress; \
        assert(offset_ && !fseek(f,(long)offset_,SEEK_SET) && fread(bytes,1,(size),f)==(size)); } while(0)
    for(unsigned i=0;i<sizeof(patk_pins_list)/sizeof(*patk_pins_list);i++) {
        assert(patk_pins_list[i].size<=sizeof(bytes));TEST_PATK_READ(patk_pins_list[i].rva,patk_pins_list[i].size);
        assert(hash_ok(bytes,patk_pins_list[i].size,patk_pins_list[i].sha));
        bytes[patk_pins_list[i].size-1]^=1;assert(!hash_ok(bytes,patk_pins_list[i].size,patk_pins_list[i].sha));
    }
    /* The pinned blocks meet the site: the function's head ends where it starts, the player path starts after it. */
    assert(patk_pins_list[0].rva+patk_pins_list[0].size==PATK_SITE && PATK_SITE+10==PATK_PLAYER_PATH);
    TEST_PATK_READ(PATK_SITE,10);assert(!memcmp(bytes,patk_site_original,10));
    assert(PATK_SITE+5+bytes[4]==PATK_GROUP_PATH && PATK_SITE+10+bytes[9]==PATK_GROUP_PATH);   /* je and jne: the attacked-by path */
    TEST_PATK_READ(0x5201b7,3);assert(bytes[0]==0x48 && bytes[1]==0x8b && bytes[2]==0xcd);     /* the player path posts on rbp */
    TEST_PATK_READ(PATK_POST_SITE,5);{int32_t rel;memcpy(&rel,bytes+1,4);assert(bytes[0]==0xe8 && PATK_POST_SITE+5+(unsigned)rel==PATK_POST);}
    assert(PATK_POST_SITE>PATK_PLAYER_PATH && PATK_POST_SITE+5<=PATK_GROUP_PATH && (PATK_POST_SITE&~15u)==((PATK_POST_SITE+4)&~15u));
    /* Object::vfunc126's post: inside the pinned body, one 16-byte block, the call of PostEvent with rcx the universe player
     * (mov rcx,rdi) and the event's attacker ID at +0x28 (mov rax,[rsi+8]; mov [rdx+0x28],rax). */
    TEST_PATK_READ(PATK_OWNER_SITE,5);{int32_t rel;memcpy(&rel,bytes+1,4);assert(bytes[0]==0xe8 && PATK_OWNER_SITE+5+(unsigned)rel==PATK_POST);}
    assert(PATK_OWNER_SITE>PATK_OWNER_FN && PATK_OWNER_SITE+5<=PATK_OWNER_FN+0x170u && (PATK_OWNER_SITE&~15u)==((PATK_OWNER_SITE+4)&~15u));
    TEST_PATK_READ(PATK_OWNER_SITE-3,3);assert(bytes[0]==0x48 && bytes[1]==0x8b && bytes[2]==0xcf);
    TEST_PATK_READ(0x6d0cbeu,8);assert(!memcmp(bytes,(const unsigned char[8]){0x48,0x8b,0x46,0x08,0x48,0x89,0x42,0x28},8));
    /* The player's "controls something": with 0 it answers yes (U::Player's vtable entry 70e580). */
    uint64_t entry;TEST_PATK_READ(0x2b329a8u+PATK_CONTROLLING,8);memcpy(&entry,bytes,8);assert(entry==nt.OptionalHeader.ImageBase+0x70e580u);
    TEST_PATK_READ(0x70e580u,0x15);
    assert(bytes[9]==0x84 && bytes[10]==0xd2 && bytes[11]==0x75 && bytes[13]==0xb0 && bytes[14]==0x01);   /* test dl,dl; jne; mov al,1 */
    #undef TEST_PATK_READ
    fclose(f);
    puts("pinned native image player attacks: attacked-event poster pins, the decision's bytes and jumps, the player path's post, the player's control predicate verified");
}
typedef unsigned char *(*test_patk_caller_fn)(void *shooter,void *controlled,void *player);
static void test_pilot_attack(void) {
    test_patk_ship_vt[PATK_IS_CLASS/8]=(void*)test_patk_ship_is;test_patk_room_vt[PATK_IS_CLASS/8]=(void*)test_patk_room_is;
    test_patk_player_vt[PATK_CONTROLLING/8]=(void*)test_patk_controls;
    static unsigned char zone[0x100],npc[0x100];static test_patk_seat seat[PILOT_SLOTS+1];
    void *svt=test_patk_ship_vt;memcpy(npc,&svt,8);{void *z=zone;memcpy(npc+0x70,&z,8);}
    unsigned char *saved_p1=pslot_p1,*saved_players[PILOT_SLOTS+1];unsigned saved_scope=pstk_scope_slot;
    for(unsigned k=0;k<=PILOT_SLOTS;k++) saved_players[k]=pslots[k].player;
    /* Slot 1 (the canonical player) and slots 2, 3 and 8 (the user's rule: never only player 2). */
    const unsigned slots[]={2,3,PILOT_SLOTS};
    test_patk_seat_make(&seat[1],zone);pslot_p1=seat[1].player;
    for(unsigned k=2;k<=PILOT_SLOTS;k++) pslots[k].player=NULL;
    for(unsigned i=0;i<3;i++) {test_patk_seat_make(&seat[slots[i]],zone);pslots[slots[i]].player=seat[slots[i]].player;}
    test_patk_universe_player=seat[1].player;patk_singleton_test=test_patk_singleton;pstk_scope_slot=0;
    /* X4's rule. */
    assert(patk_controlled(seat[1].player)==seat[1].ship && patk_controlled(NULL)==NULL);
    for(unsigned i=0;i<3;i++) assert(patk_controlled(seat[slots[i]].player)==seat[slots[i]].ship);
    {   /* not seated (the vector empty), seated in another ship's room, and the vector's first entry another ship: none */
        static test_patk_seat odd;test_patk_seat_make(&odd,zone);
        odd.vector[1]=NULL;assert(!patk_controlled(odd.player));
        odd.vector[1]=odd.ship;{void *room=seat[2].room;memcpy(odd.player+0x70,&room,8);}assert(!patk_controlled(odd.player));
        {void *room=odd.room;memcpy(odd.player+0x70,&room,8);}odd.vector[0]=seat[3].ship;assert(!patk_controlled(odd.player));
        odd.vector[0]=NULL;assert(patk_controlled(odd.player)==odd.ship);
        /* a roomless seat: the player's parent is the ship */
        {void *ship=odd.ship;memcpy(odd.player+0x70,&ship,8);}assert(patk_controlled(odd.player)==odd.ship);
    }
    /* Whose shot. */
    assert(patk_slot_of_ship(seat[1].ship)==1 && !patk_slot_of_ship(npc) && !patk_slot_of_ship(NULL));
    for(unsigned i=0;i<3;i++) assert(patk_slot_of_ship(seat[slots[i]].ship)==slots[i]);
    /* The decision: X4's own answer, a slot's player, nobody. */
    patk_own=patk_group=0;memset((void*)patk_slot,0,sizeof(patk_slot));
    assert(patk_attacker(seat[1].ship,seat[1].ship,seat[1].player)==seat[1].player && patk_own==1);
    for(unsigned i=0;i<3;i++) {
        const unsigned k=slots[i];
        assert(patk_attacker(seat[k].ship,seat[1].ship,seat[1].player)==seat[k].player && patk_slot[k]==1);
    }
    assert(!patk_attacker(npc,seat[1].ship,seat[1].player) && patk_group==1);   /* an NPC: the attacked-by path */
    assert(!patk_attacker(seat[2].ship,NULL,NULL) && patk_group==2);            /* no player (loading): X4's answer */
    /* Inside slot 3's scope the singleton is P_3: X4 answers slot 3 itself, slot 1's shot goes to P_1, slot 8's to P_8. */
    pstk_scope_slot=3;test_patk_universe_player=seat[3].player;
    assert(patk_attacker(seat[3].ship,seat[3].ship,seat[3].player)==seat[3].player && patk_own==2);
    assert(patk_attacker(seat[1].ship,seat[3].ship,seat[3].player)==seat[1].player && patk_slot[1]==1);
    assert(patk_attacker(seat[PILOT_SLOTS].ship,seat[3].ship,seat[3].player)==seat[PILOT_SLOTS].player && patk_slot[PILOT_SLOTS]==2);
    assert(patk_slot_of_ship(seat[1].ship)==1);   /* slot 1's player is the latched one, not the singleton */
    pstk_scope_slot=0;test_patk_universe_player=seat[1].player;
    /* Without the slot module (no P_k, nothing latched) slot 1 is the singleton and nobody else shoots as a player. */
    {   unsigned char *p1=pslot_p1;pslot_p1=NULL;unsigned char *p2=pslots[2].player;pslots[2].player=NULL;
        assert(patk_slot_of_ship(seat[1].ship)==1 && !patk_slot_of_ship(seat[2].ship));
        assert(!patk_attacker(seat[2].ship,seat[1].ship,seat[1].player));
        pslot_p1=p1;pslots[2].player=p2;}
    /* Arming on the fake image: refused without the pins and with foreign site bytes; then the CAS. */
    DWORD protection;assert(VirtualProtect(image+0x520000u,0x1000,PAGE_EXECUTE_READWRITE,&protection));
    assert(VirtualProtect(image+PATK_POST,16,PAGE_EXECUTE_READWRITE,&protection));thunk_absolute_jump(image+PATK_POST,(void*)test_patk_native_post);
    assert(VirtualProtect(image+0x6d0000u,0x1000,PAGE_EXECUTE_READWRITE,&protection));
    {int32_t rel=(int32_t)(PATK_POST-(PATK_OWNER_SITE+5));image[PATK_OWNER_SITE]=0xe8;memcpy(image+PATK_OWNER_SITE+1,&rel,4);}
    memcpy(image+PATK_SITE,patk_site_original,10);
    {int32_t rel=(int32_t)(PATK_POST-(PATK_POST_SITE+5));image[PATK_POST_SITE]=0xe8;memcpy(image+PATK_POST_SITE+1,&rel,4);}
    patk_installed=patk_done=patk_restored=0;patk_stage="off";
    patk_pins_checked=0;assert(!patk_arm() && patk_pins_checked==-1 && !strcmp(patk_stage,"pins") && !patk_installed);
    patk_pins_checked=1;image[PATK_SITE+3]^=1;assert(!patk_arm() && !strcmp(patk_stage,"pins"));image[PATK_SITE+3]^=1;
    /* X4's two continuations as stand-ins. The player path posts as X4's does (rcx the player rbp, rdx an event - here rdi,
     * the time in xmm2, r9b 1) through the post call kept at its own address, then returns rbp; the attacked-by path
     * returns 0; both put back what the caller pushed. */
    static const unsigned char player_path[]={0x48,0x83,0xec,0x20, 0x48,0x89,0xe9, 0x48,0x89,0xfa, 0x0f,0x57,0xd2, 0x41,0xb1,0x01,
        0xe9,0x29,0,0,0};                                                              /* ...; jmp the post call */
    static const unsigned char after_post[]={0xeb,0x3f};                               /* jmp 0x520200 */
    static const unsigned char tail[]={0x48,0x83,0xc4,0x20, 0x48,0x89,0xe8, 0x5f,0x5e,0x5d,0xc3};   /* add rsp,0x20; mov rax,rbp; pop..; ret */
    static const unsigned char group_path[]={0x31,0xc0, 0x5f,0x5e,0x5d,0xc3};         /* xor eax,eax; pop rdi; pop rsi; pop rbp; ret */
    assert(PATK_PLAYER_PATH+sizeof(player_path)==0x520191u && 0x520191u+0x29u==PATK_POST_SITE);
    memcpy(image+PATK_PLAYER_PATH,player_path,sizeof(player_path));memcpy(image+PATK_POST_SITE+5,after_post,sizeof(after_post));
    memcpy(image+0x520200u,tail,sizeof(tail));memcpy(image+PATK_GROUP_PATH,group_path,sizeof(group_path));
    {unsigned char other[5]={0xe8,1,2,3,4},keep[5];memcpy(keep,image+PATK_POST_SITE,5);memcpy(image+PATK_POST_SITE,other,5);
     assert(!patk_arm() && !strcmp(patk_stage,"pins"));memcpy(image+PATK_POST_SITE,keep,5);}   /* a foreign post call: refused */
    assert(patk_arm() && patk_installed && patk_done && patk_owned() && patk_check() && patk_state()==1 && patk_stub_intact());
    assert(patk_arm());   /* again: already ours */
    {int32_t rel;memcpy(&rel,image+PATK_SITE+1,4);assert(image[PATK_SITE]==0xe9 && image+PATK_SITE+5+rel==patk_page);}
    assert(!memcmp(image+PATK_SITE+5,patk_site_original+5,5));
    {int32_t rel;memcpy(&rel,image+PATK_POST_SITE+1,4);assert(image[PATK_POST_SITE]==0xe8 && image+PATK_POST_SITE+5+rel==patk_page+PATK_POST_STUB);}
    {int32_t rel;memcpy(&rel,image+PATK_OWNER_SITE+1,4);assert(image[PATK_OWNER_SITE]==0xe8 && image+PATK_OWNER_SITE+5+rel==patk_page+PATK_OWNER_STUB);}
    /* A caller with X4's registers at the site: push rbp, rsi, rdi (the stack as in 51ff10's body, 16-aligned); rsi the
     * shooter, rdi the singleton's controlled ship, rbp the singleton; jump to the site. */
    unsigned char *code=VirtualAlloc(NULL,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);assert(code);
    {   unsigned n=0;const unsigned char head[]={0x55,0x56,0x57, 0x48,0x89,0xce, 0x48,0x89,0xd7, 0x4c,0x89,0xc5, 0x48,0xb8};
        memcpy(code,head,sizeof(head));n=sizeof(head);unsigned char *site=image+PATK_SITE;memcpy(code+n,&site,8);n+=8;
        code[n++]=0xff;code[n++]=0xe0;   /* jmp rax */
        FlushInstructionCache(GetCurrentProcess(),code,n);}
    test_patk_caller_fn call=(test_patk_caller_fn)(void*)code;
    const LONG64 own=patk_own,group=patk_group;patk_told_test=test_patk_told;
    /* slot 1's shot: X4's post on P_1 alone */
    test_patk_call_count=0;
    assert(call(seat[1].ship,seat[1].ship,seat[1].player)==seat[1].player && patk_own==own+1);
    assert(test_patk_call_count==1 && test_patk_calls[0].what==2 && test_patk_calls[0].source==seat[1].player &&
           test_patk_calls[0].event==seat[1].ship && !patk_told);
    /* slot k's shot: the scripts nobody owns in slot k's scope first, then X4's post on P_k */
    for(unsigned i=0;i<3;i++) {
        const unsigned k=slots[i];const LONG64 before=patk_slot[k];test_patk_call_count=0;
        assert(call(seat[k].ship,seat[1].ship,seat[1].player)==seat[k].player && patk_slot[k]==before+1);
        assert(test_patk_call_count==2 && test_patk_calls[0].what==1 && test_patk_calls[0].slot==k && test_patk_calls[0].event==seat[1].ship &&
               test_patk_calls[1].what==2 && test_patk_calls[1].source==seat[k].player && patk_told==(LONG64)i+1);
    }
    test_patk_call_count=0;
    assert(!call(npc,seat[1].ship,seat[1].player) && patk_group==group+1 && !test_patk_call_count);   /* no post at all */
    assert(!call(seat[2].ship,NULL,NULL) && patk_group==group+2 && !test_patk_call_count);
    /* The player-owned attacker's post (Object::vfunc126): the attacker's ID at +0x28. Slots 2, 3 and 8: to P_k through its
     * scope first; slot 1 or an NPC fleet ship: X4's post on the universe player as it was. */
    {   unsigned char event[0x30];
        for(unsigned i=0;i<3;i++) {
            const unsigned k=slots[i];memset(event,0,sizeof(event));memcpy(event+0x28,seat[k].ship+8,8);test_patk_call_count=0;
            patk_owner_post(seat[1].player,event,1.0,1);
            assert(test_patk_call_count==2 && test_patk_calls[0].what==1 && test_patk_calls[0].slot==k && test_patk_calls[0].event==event &&
                   test_patk_calls[1].what==2 && test_patk_calls[1].source==seat[k].player && patk_owner_posts==(LONG64)i+1);
        }
        memset(event,0,sizeof(event));memcpy(event+0x28,seat[1].ship+8,8);test_patk_call_count=0;
        patk_owner_post(seat[1].player,event,1.0,1);
        assert(test_patk_call_count==1 && test_patk_calls[0].what==2 && test_patk_calls[0].source==seat[1].player && patk_owner_posts==3);
        memcpy(event+0x28,npc+8,8);test_patk_call_count=0;patk_owner_post(seat[1].player,event,1.0,1);
        assert(test_patk_call_count==1 && test_patk_calls[0].source==seat[1].player && patk_owner_posts==3);
        /* slot 2 off its seat (the vector empty): the universe player's post */
        seat[2].vector[1]=NULL;memcpy(event+0x28,seat[2].ship+8,8);test_patk_call_count=0;patk_owner_post(seat[1].player,event,1.0,1);
        assert(test_patk_call_count==1 && test_patk_calls[0].source==seat[1].player);seat[2].vector[1]=seat[2].ship;
    }
    patk_told_test=NULL;
    char status[256];
    assert(patk_status(status,sizeof(status)) && strstr(status,"attacks=armed own=1 group=2 told=6 unscoped=0 owner=3 slots=0/1/1/0/0/0/0/1"));
    /* A changed site or stub stops the gate (240); restore puts the bytes back. */
    image[PATK_SITE+2]^=1;assert(!patk_check() && !strcmp(patk_stage,"site_changed"));image[PATK_SITE+2]^=1;patk_failures=0;
    assert(patk_check());
    {int32_t rel;memcpy(&rel,image+PATK_POST_SITE+1,4);rel^=1;memcpy(image+PATK_POST_SITE+1,&rel,4);
     assert(!patk_check() && !strcmp(patk_stage,"site_changed"));rel^=1;memcpy(image+PATK_POST_SITE+1,&rel,4);patk_failures=0;}
    {int32_t rel;memcpy(&rel,image+PATK_OWNER_SITE+1,4);rel^=1;memcpy(image+PATK_OWNER_SITE+1,&rel,4);
     assert(!patk_check() && !strcmp(patk_stage,"site_changed"));rel^=1;memcpy(image+PATK_OWNER_SITE+1,&rel,4);patk_failures=0;}
    assert(patk_check());
    patk_restore();assert(!patk_installed && patk_restored && !memcmp(image+PATK_SITE,patk_site_original,10) && patk_check() && patk_state()==0);
    /* Reset to default off. */
    VirtualFree(code,0,MEM_RELEASE);
    memset(image+0x520000u,0,0x1000);memset(image+0x6d0000u,0,0x1000);memset(image+PATK_POST,0,16);patk_pins_checked=0;patk_stage="off";patk_restored=0;patk_singleton_test=NULL;
    patk_own=patk_group=patk_told=patk_unscoped=patk_owner_posts=patk_failures=0;memset((void*)patk_slot,0,sizeof(patk_slot));
    pslot_p1=saved_p1;pstk_scope_slot=saved_scope;for(unsigned k=0;k<=PILOT_SLOTS;k++) pslots[k].player=saved_players[k];
    printf("player attacks: X4's controlled-ship rule for slots 1, 2, 3 and %u, whose shot, the poster's decision (own, slot, "
           "inside a scope, nobody), the stub from X4's registers, slot k's post through its scope first, the player-owned attacker's post to its player, arm, tick check and restore tests PASS\n",PILOT_SLOTS);
}
