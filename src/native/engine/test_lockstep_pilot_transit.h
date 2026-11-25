/* A player 2..N's gates on its own client (lockstep_pilot_transit.h): the gate pass's player test notes the passing ship
 * when it is this client's own slot's, X4's answer unchanged; the next UI frame in the scope runs X4's player branch
 * (the player's ship, the external view, the controlled ship, its cockpit, the cockpit effect) for slots 2, 3 and 8;
 * outside the scope nothing; the site by CAS, the tick check, a fault. */
static unsigned char *tptran_player_ship,*tptran_controlled,*tptran_cockpit;
static unsigned tptran_fx_calls,tptran_ship_calls;
static unsigned char tptran_external_answer;
static void *tptran_fx_on;
static void **tptran_player_ship_fn(void *unused,void **out) {(void)unused;tptran_ship_calls++;*out=tptran_player_ship;return out;}
static void **tptran_controlled_fn(void *entity,void **out) {(void)entity;*out=tptran_controlled;return out;}
static void **tptran_cockpit_fn(void *ship,void **out) {*out=ship==tptran_controlled?tptran_cockpit:NULL;return out;}
static unsigned char tptran_external_fn(void) {return tptran_external_answer;}
static void tptran_fx_fn(void *cockpit) {tptran_fx_calls++;tptran_fx_on=cockpit;}
static void tptran_fault_fn(void *cockpit) {(void)cockpit;*(volatile int*)(uintptr_t)8=1;}
static void test_pilot_transit(void) {
    static unsigned char ships[9][0x80],other[0x80],cockpits[9][0x40],player[0x300],universe[0x400];
    const unsigned saved_slot=pres_slot;const LONG saved_open=pres_open;
    pslot_slot saved[PILOT_SLOTS+1];memcpy(saved,pslots,sizeof(saved));
    for(unsigned k=1;k<=8;k++) {uint64_t id=0x3f100u+k;memset(ships[k],0,sizeof(ships[k]));memcpy(ships[k]+8,&id,8);}
    {uint64_t id=0x3f1ffu;memset(other,0,sizeof(other));memcpy(other+8,&id,8);}
    {void *p=player;memset(universe,0,sizeof(universe));memcpy(universe+PTRAN_PLAYER,&p,8);}
    unsigned char *universe_at=universe;
    ptran_fn.player_ship=tptran_player_ship_fn;ptran_fn.controlled=tptran_controlled_fn;ptran_fn.cockpit=tptran_cockpit_fn;
    ptran_fn.external=tptran_external_fn;ptran_fn.fx=tptran_fx_fn;ptran_fn.universe_at=(unsigned char*)&universe_at;
    ptran_reset();ptran_on=1;ptran_installed=1;
    const unsigned slots[3]={2,3,8};
    for(unsigned j=0;j<3;j++) {
        const unsigned k=slots[j];void *answer=NULL;
        pres_slot=k;memset(&pslots[k],0,sizeof(pslots[k]));pslots[k].seated=1;pslots[k].player=player;pslots[k].ship=ships[k];
        tptran_player_ship=ships[1];   /* X4's answer outside the scope: slot 1's ship */
        /* another ship through the gate: X4's answer, nothing noted */
        tptran_ship_calls=0;assert(ptran_gate_test(NULL,&answer,other)==&answer && answer==ships[1] && tptran_ship_calls==1 && !ptran_pending);
        /* slot 1's ship (X4's own test is its own): nothing noted */
        assert(ptran_gate_test(NULL,&answer,ships[1])==&answer && !ptran_pending);
        /* slot k's ship: X4's answer unchanged, its ID noted */
        answer=NULL;assert(ptran_gate_test(NULL,&answer,ships[k])==&answer && answer==ships[1] && (uint64_t)ptran_pending==0x3f100u+k);
        /* outside the scope: kept for the next frame in it */
        InterlockedExchange(&pres_open,0);tptran_fx_calls=0;ptran_scope_pass();assert(!tptran_fx_calls && ptran_pending);
        /* in the scope: X4's player is slot k's, the camera in the cockpit: the cockpit effect on slot k's cockpit, once */
        InterlockedExchange(&pres_open,1);tptran_player_ship=ships[k];tptran_controlled=ships[k];tptran_cockpit=cockpits[k];tptran_external_answer=0;
        const LONG64 effects=ptran_effects;
        ptran_scope_pass();assert(tptran_fx_calls==1 && tptran_fx_on==cockpits[k] && ptran_effects==effects+1 && !ptran_pending);
        ptran_scope_pass();assert(tptran_fx_calls==1);   /* nothing noted: nothing */
        /* the camera in an external view: X4 gives none */
        ptran_gate_test(NULL,&answer,ships[k]);tptran_external_answer=1;
        {const LONG64 external=ptran_external;ptran_scope_pass();assert(tptran_fx_calls==1 && ptran_external==external+1);}
        tptran_external_answer=0;
        /* the player's ship in the scope is another one than the noted (slot k left it before the frame): none */
        ptran_gate_test(NULL,&answer,ships[k]);tptran_player_ship=other;
        {const LONG64 others=ptran_other;ptran_scope_pass();assert(tptran_fx_calls==1 && ptran_other==others+1);}
        /* a ship without a cockpit: none, counted */
        ptran_gate_test(NULL,&answer,ships[k]);tptran_player_ship=ships[k];tptran_cockpit=NULL;
        {const LONG64 others=ptran_other;ptran_scope_pass();assert(tptran_fx_calls==1 && ptran_other==others+1);}
        tptran_cockpit=cockpits[k];
        /* not seated (dead, in transit): nothing noted */
        pslots[k].seated=0;ptran_gate_test(NULL,&answer,ships[k]);assert(!ptran_pending);pslots[k].seated=1;
        InterlockedExchange(&pres_open,0);
    }
    /* client 1 never notes; the module off notes nothing */
    tptran_player_ship=ships[1];pres_slot=1;{void *answer=NULL;ptran_gate_test(NULL,&answer,ships[1]);assert(!ptran_pending && answer==ships[1]);}
    pres_slot=2;ptran_installed=0;{void *answer=NULL;ptran_gate_test(NULL,&answer,ships[2]);assert(!ptran_pending && answer==ships[1]);}
    ptran_installed=1;
    {char row[160];assert(ptran_status(row,sizeof(row)) && !strcmp(row," transit=1/1/12/12/3/3/6/0"));}
    /* a fault in X4's effect: the module off on this client (the site back), why gate_fault; the session goes on */
    {   unsigned char *const block=VirtualAlloc(NULL,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(block);
        unsigned char *const region=block-0x5ce000u;unsigned char *const saved_base=pnat_base;unsigned char *const saved_page=ptran_page;
        pnat_base=region;ptran_page=NULL;ptran_installed=0;
        unsigned char original[5];ptran_original(original);memcpy(region+PTRAN_GATE_SITE,original,5);
        assert(((PTRAN_GATE_SITE+4u)&~15u)==(PTRAN_GATE_SITE&~15u));
        assert(ptran_install() && ptran_installed && ptran_page && ptran_check());
        {int32_t rel;memcpy(&rel,region+PTRAN_GATE_SITE+1,4);
         assert(region[PTRAN_GATE_SITE]==0xe8 && region+PTRAN_GATE_SITE+5+rel==ptran_page);
         assert(!memcmp(ptran_page,"\x4c\x8b\xc7\xff\x25\0\0\0\0",9));
         void *to=NULL;memcpy(&to,ptran_page+9,8);assert(to==(void*)ptran_gate_test);}
        region[PTRAN_GATE_SITE+2]^=1;assert(!ptran_check() && !strcmp(ptran_check_why,"gate_site_lost"));region[PTRAN_GATE_SITE+2]^=1;ptran_check_why="-";
        assert(ptran_check());
        pslots[2].seated=1;pslots[2].player=player;pslots[2].ship=ships[2];
        {void *answer=NULL;ptran_gate_test(NULL,&answer,ships[2]);}
        InterlockedExchange(&pres_open,1);tptran_player_ship=ships[2];tptran_controlled=ships[2];tptran_cockpit=cockpits[2];ptran_fn.fx=tptran_fault_fn;
        ptran_scope_pass();
        assert(ptran_faults==1 && !ptran_installed && !strcmp(ptran_why,"gate_fault") && !memcmp(region+PTRAN_GATE_SITE,original,5) && ptran_check());
        InterlockedExchange(&pres_open,0);
        /* the site not X4's: refused */
        region[PTRAN_GATE_SITE]=0xcc;ptran_why="-";assert(!ptran_install() && !ptran_installed && !strcmp(ptran_why,"site"));
        VirtualFree(ptran_page,0,MEM_RELEASE);ptran_page=saved_page;pnat_base=saved_base;VirtualFree(block,0,MEM_RELEASE);}
    ptran_on=0;ptran_installed=0;ptran_reset();memset(&ptran_fn,0,sizeof(ptran_fn));
    memcpy(pslots,saved,sizeof(saved));pres_slot=saved_slot;InterlockedExchange(&pres_open,saved_open);
    printf("pilot transit: the gate pass's player test noted for slot k's ship only (slots 2, 3, 8), X4's cockpit effect in the scope, the external view, another ship, no cockpit, not seated, client 1, off, the site, fault tests PASS\n");
}
static void test_pilot_transit_image(const char *path) {
    FILE *f=fopen(path,"rb");assert(f);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;IMAGE_SECTION_HEADER sections[32];
    assert(fread(&dos,1,sizeof(dos),f)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && !fseek(f,dos.e_lfanew,SEEK_SET));
    assert(fread(&nt,1,sizeof(nt),f)==sizeof(nt) && nt.FileHeader.TimeDateStamp==0x6a1eaded && nt.FileHeader.NumberOfSections<=32);
    unsigned count=nt.FileHeader.NumberOfSections;
    assert(fread(sections,sizeof(*sections),count,f)==count);
    static unsigned char bytes[0x420];
    for(unsigned i=0;i<PTRAN_PIN_COUNT;i++)
        assert(test_pnat_file_bytes(f,sections,count,ptran_pin_table[i].rva,ptran_pin_table[i].size,bytes) && hash_ok(bytes,ptran_pin_table[i].size,ptran_pin_table[i].sha));
    /* the player test: call 0x9f7b30 (the passing ship in rdi compared with its answer), IsExternalViewActive, X4's player
     * (Universe+0x230), its controlled ship 0x55b150, the cockpit 0x7cf7c0, the effect 0x3ca1b0 */
    #define TPTRAN_CALL(site,target) do {int32_t r_;assert(test_pnat_file_bytes(f,sections,count,(site),5,bytes) && bytes[0]==0xe8);memcpy(&r_,bytes+1,4); \
        assert((unsigned)((site)+5)+(unsigned)r_==(unsigned)(target));} while(0)
    TPTRAN_CALL(PTRAN_GATE_SITE,PTRAN_PLAYER_SHIP);TPTRAN_CALL(0x5cefdeu,PTRAN_EXTERNAL);TPTRAN_CALL(0x5cf010u,PTRAN_CONTROLLED);
    TPTRAN_CALL(0x5cf025u,PTRAN_COCKPIT);TPTRAN_CALL(0x5cf033u,PTRAN_GATE_FX);
    #undef TPTRAN_CALL
    assert(test_pnat_file_bytes(f,sections,count,0x5cefd9u,3,bytes) && !memcmp(bytes,"\x48\x3b\x38",3));   /* cmp rdi, [rax] */
    assert(test_pnat_file_bytes(f,sections,count,0x5ceff1u,7,bytes) && !memcmp(bytes,"\x48\x8d\x88\x30\x02\x00\x00",7));   /* lea rcx, [rax+0x230] */
    /* the callers of the pass: the trigger's handler (jmp), Gate slot 1068 and the highway's gates (rdx = the ship) */
    {int32_t r;assert(test_pnat_file_bytes(f,sections,count,0x5cdbbeu,5,bytes) && bytes[0]==0xe9);memcpy(&r,bytes+1,4);assert(0x5cdbc3u+(unsigned)r==0x5ce690u);}
    fclose(f);
    printf("pilot transit image: pins, the gate pass's player branch (calls, compare, X4's player) PASS\n");
}
