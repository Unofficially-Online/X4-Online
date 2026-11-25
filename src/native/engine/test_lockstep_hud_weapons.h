/* The HUD weapon panel by X4's group rule (lockstep_hud_weapons.h). Synthetic memory, slots 2 and 3 presented (and slot 8
 * for the counts): a leased gun (MindlessShootController) of the presented ship's active primary group is active, the
 * same gun after a group change is not; a launcher without a controller in the active secondary group is active as a
 * secondary weapon and not as a primary one; X4's yes stays (a bomb launcher's class), X4's PlayerShootController keeps
 * X4's no; another ship's weapon, no presented ship, a group index out of range or negative, another thread and the module
 * off keep X4's answer; the counters per slot. The eight sites by CAS on a region of the image's shape, the two
 * trampolines called through, the tick check, a site that is not X4's call (refused, nothing left written). */
static unsigned char *thudw_presented;
static unsigned char thudw_x4_answer;
static unsigned thudw_x4_calls;
static unsigned char *thudw_ship(void) {return thudw_presented;}
static unsigned char thudw_x4(void *weapon) {(void)weapon;++thudw_x4_calls;return thudw_x4_answer;}
typedef struct {unsigned char ship[0x700];uint64_t ids[3][4];uint64_t ids2[2][4];} thudw_ship_t;
/* A ship with three primary groups (group g: the IDs base+g*4+1..3) and two secondary ones; group 0 active in each. */
static void thudw_ship_make(thudw_ship_t *s,uint64_t base) {
    memset(s,0,sizeof(*s));
    for(unsigned g=0;g<3;g++) for(unsigned i=0;i<3;i++) s->ids[g][i]=base+g*4u+i+1u;
    for(unsigned g=0;g<2;g++) for(unsigned i=0;i<2;i++) s->ids2[g][i]=base+0x100u+g*4u+i+1u;
}
static void test_hud_weapons(void) {
    static thudw_ship_t ships[PILOT_SLOTS+1];
    static unsigned char groups[PILOT_SLOTS+1][2][0x18*3];
    static unsigned char weapons[PILOT_SLOTS+1][4][0x400],controllers[3][0x100];
    const unsigned saved_slot=pstk_capture_slot;const DWORD saved_ui=ui_thread;
    const hudw_test_fn saved_primary=hudw_fn.primary,saved_secondary=hudw_fn.secondary;
    hudw_fn.primary=thudw_x4;hudw_fn.secondary=thudw_x4;hudw_ship_test=thudw_ship;hudw_thread_any=1;hudw_installed=1;
    hudw_asks=hudw_yes=hudw_own=hudw_unlisted=hudw_no_ship=hudw_elsewhere=0;
    memset((void*)hudw_answered,0,sizeof(hudw_answered));memset((void*)hudw_slots,0,sizeof(hudw_slots));
    /* the controllers: the gate's lease, X4's player class, the missiles' basic one */
    memset(controllers,0,sizeof(controllers));
    {unsigned char *v[3]={image+PFIRE_MINDLESS_VTABLE,image+HUDW_PLAYER_VTABLE,image+0x2b7fcc0u};
     for(unsigned i=0;i<3;i++) memcpy(controllers[i],&v[i],8);}
    #define THUDW_PUT(at,value) do {const void *const v_=(value);memcpy((at),&v_,8);} while(0)
    #define THUDW_ID(w,id) do {const uint64_t i_=(id);memcpy((w)+HUDW_ID,&i_,8);} while(0)
    #define THUDW_CTL(w,c) THUDW_PUT((w)+HUDW_CONTROLLER,(c))
    /* slot k's ship: primary groups at +0x638 (three vectors of 0x18), the active one +0x668; secondary +0x650, +0x66c */
    for(unsigned k=1;k<=PILOT_SLOTS;k++) {
        thudw_ship_t *s=&ships[k];thudw_ship_make(s,0x10000u*k);
        for(unsigned g=0;g<3;g++) {
            unsigned char *v=groups[k][0]+g*0x18u;THUDW_PUT(v,&s->ids[g][0]);THUDW_PUT(v+8,&s->ids[g][3]);THUDW_PUT(v+16,&s->ids[g][3]);
        }
        for(unsigned g=0;g<2;g++) {
            unsigned char *v=groups[k][1]+g*0x18u;THUDW_PUT(v,&s->ids2[g][0]);THUDW_PUT(v+8,&s->ids2[g][2]);THUDW_PUT(v+16,&s->ids2[g][2]);
        }
        THUDW_PUT(s->ship+HUDW_GROUPS,groups[k][0]);THUDW_PUT(s->ship+HUDW_GROUPS+8,groups[k][0]+3*0x18u);
        THUDW_PUT(s->ship+HUDW_GROUPS2,groups[k][1]);THUDW_PUT(s->ship+HUDW_GROUPS2+8,groups[k][1]+2*0x18u);
        /* weapon 0: gun 1 of group 0, leased; 1: gun 1 of group 1, no controller; 2: launcher 1 of secondary group 0, none;
         * 3: gun 2 of group 0 under X4's own PlayerShootController */
        memset(weapons[k],0,sizeof(weapons[k]));
        THUDW_ID(weapons[k][0],s->ids[0][0]);THUDW_CTL(weapons[k][0],controllers[0]);
        THUDW_ID(weapons[k][1],s->ids[1][0]);
        THUDW_ID(weapons[k][2],s->ids2[0][0]);
        THUDW_ID(weapons[k][3],s->ids[0][1]);THUDW_CTL(weapons[k][3],controllers[1]);
    }
    const unsigned slots[2]={2,3};
    for(unsigned j=0;j<2;j++) {
        const unsigned k=slots[j];thudw_ship_t *s=&ships[k];int32_t a;
        pstk_capture_slot=k;thudw_presented=s->ship;thudw_x4_answer=0;
        /* the presented ship's active primary group (0): the leased gun is active, X4's test asked first */
        thudw_x4_calls=0;
        assert(hudw_primary(weapons[k][0])==1 && thudw_x4_calls==1);
        /* a gun of group 1 is not; after the group change to 1 it is and gun 0's is not */
        assert(hudw_primary(weapons[k][1])==0);
        a=1;memcpy(s->ship+HUDW_ACTIVE,&a,4);
        assert(hudw_primary(weapons[k][1])==1 && hudw_primary(weapons[k][0])==0);
        a=0;memcpy(s->ship+HUDW_ACTIVE,&a,4);
        /* the launcher of the active secondary group: a secondary yes, a primary no; secondary group 1 active: no */
        assert(hudw_secondary(weapons[k][2])==1 && hudw_primary(weapons[k][2])==0);
        a=1;memcpy(s->ship+HUDW_ACTIVE2,&a,4);assert(hudw_secondary(weapons[k][2])==0);
        /* the basic controller of a firing launcher: still X4's group rule */
        a=0;memcpy(s->ship+HUDW_ACTIVE2,&a,4);THUDW_CTL(weapons[k][2],controllers[2]);assert(hudw_secondary(weapons[k][2])==1);
        THUDW_CTL(weapons[k][2],NULL);
        /* X4's own PlayerShootController: its no stays (its flag is X4's answer) */
        {const LONG64 own=hudw_own;assert(hudw_primary(weapons[k][3])==0 && hudw_own==own+1);}
        /* X4's yes stays (a bomb launcher's class 5, a player flag): counted, no rule */
        {const LONG64 yes=hudw_yes;thudw_x4_answer=1;assert(hudw_primary(weapons[k][1])==1 && hudw_yes==yes+1);thudw_x4_answer=0;}
        /* another slot's weapon is not in this ship's groups: X4's no */
        {const LONG64 unlisted=hudw_unlisted;const unsigned other=k==2?3:2;
         assert(hudw_primary(weapons[other][0])==0 && hudw_unlisted==unlisted+1);}
        /* the group index out of range, negative: X4's no */
        a=3;memcpy(s->ship+HUDW_ACTIVE,&a,4);assert(hudw_primary(weapons[k][0])==0);
        a=-1;memcpy(s->ship+HUDW_ACTIVE,&a,4);assert(hudw_primary(weapons[k][0])==0);
        a=0;memcpy(s->ship+HUDW_ACTIVE,&a,4);
        /* no presented ship (on foot, no player): X4's no */
        {const LONG64 none=hudw_no_ship;thudw_presented=NULL;assert(hudw_primary(weapons[k][0])==0 && hudw_no_ship==none+1);}
        thudw_presented=s->ship;
        /* another thread: X4's answer */
        {const LONG64 elsewhere=hudw_elsewhere;hudw_thread_any=0;ui_thread=GetCurrentThreadId()+1u;
         assert(hudw_primary(weapons[k][0])==0 && hudw_elsewhere==elsewhere+1);hudw_thread_any=1;ui_thread=saved_ui;}
        /* the module off: X4's answer */
        hudw_installed=0;assert(hudw_primary(weapons[k][0])==0 && hudw_secondary(weapons[k][2])==0);hudw_installed=1;
        assert(hudw_primary(weapons[k][0])==1);
    }
    /* slot 8 presented: its own gun */
    pstk_capture_slot=8;thudw_presented=ships[8].ship;assert(hudw_primary(weapons[8][0])==1);
    /* the status: slots 2 and 3 three primary and two secondary answers each, slot 8 one */
    {char status[384];assert(hudw_status(status,sizeof(status))>0);
     assert(strstr(status,"installed=1 ") && strstr(status," answered=7/4 ") && strstr(status," slots=0/5/5/0/0/0/0/1"));}
    /* the sites on a region of the image's shape: ours by CAS, the trampolines called through, the tick check, X4's calls
     * back; a site that is not X4's call: refused, nothing left written */
    {   unsigned char *const block=VirtualAlloc(NULL,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(block);
        unsigned char *const region=block-0xb06000u,*const saved_image=image,*const saved_page=hudw_page;
        image=region;hudw_page=NULL;hudw_installed=0;hudw_why="-";hudw_check_why="-";
        unsigned char o[HUDW_SITES][5];
        for(unsigned i=0;i<HUDW_SITES;i++) {hudw_call(hudw_sites[i].rva,region+hudw_sites[i].target,o[i]);memcpy(region+hudw_sites[i].rva,o[i],5);}
        assert(hudw_install() && hudw_installed && hudw_check());
        for(unsigned i=0;i<HUDW_SITES;i++) {
            int32_t rel;memcpy(&rel,region+hudw_sites[i].rva+1,4);unsigned char *const t=region+hudw_sites[i].rva+5+rel;
            assert(region[hudw_sites[i].rva]==0xe8 && t==hudw_page+(hudw_sites[i].target==HUDW_SECONDARY?0x20u:0u));
            static const unsigned char head[6]={0xff,0x25,0,0,0,0};void *to=NULL;
            assert(!memcmp(t,head,6));memcpy(&to,t+6,8);
            assert(to==(hudw_sites[i].target==HUDW_SECONDARY?(void*)hudw_secondary:(void*)hudw_primary));
        }
        /* through the trampolines: slot 3's leased gun as a primary weapon, its launcher as a secondary one */
        pstk_capture_slot=3;thudw_presented=ships[3].ship;
        assert(((hudw_test_fn)(void*)hudw_page)(weapons[3][0])==1 && ((hudw_test_fn)(void*)(hudw_page+0x20))(weapons[3][2])==1);
        assert(((hudw_test_fn)(void*)hudw_page)(weapons[3][2])==0);
        region[hudw_sites[5].rva+2]^=1;assert(!hudw_check() && !strcmp(hudw_check_why,"site_lost"));region[hudw_sites[5].rva+2]^=1;
        hudw_check_why="-";assert(hudw_check());
        hudw_uninstall();assert(!hudw_installed && !strcmp(hudw_why,"-"));
        for(unsigned i=0;i<HUDW_SITES;i++) assert(!memcmp(region+hudw_sites[i].rva,o[i],5));
        region[hudw_sites[6].rva]=0xcc;
        assert(!hudw_install() && !hudw_installed && !strcmp(hudw_why,"site") && region[hudw_sites[6].rva]==0xcc);
        for(unsigned i=0;i<HUDW_SITES;i++) if(i!=6) assert(!memcmp(region+hudw_sites[i].rva,o[i],5));
        VirtualFree(hudw_page,0,MEM_RELEASE);hudw_page=saved_page;image=saved_image;VirtualFree(block,0,MEM_RELEASE);}
    #undef THUDW_PUT
    #undef THUDW_ID
    #undef THUDW_CTL
    hudw_installed=0;hudw_why="-";hudw_check_why="-";hudw_ship_test=NULL;hudw_thread_any=0;
    hudw_fn.primary=saved_primary;hudw_fn.secondary=saved_secondary;pstk_capture_slot=saved_slot;ui_thread=saved_ui;
    hudw_asks=hudw_yes=hudw_own=hudw_unlisted=hudw_no_ship=hudw_elsewhere=0;
    memset((void*)hudw_answered,0,sizeof(hudw_answered));memset((void*)hudw_slots,0,sizeof(hudw_slots));
    puts("hud weapons tests PASS");
}
/* X4.exe 9.00: the pins; each site's call of its test with the weapon in rcx; the two tests' controller, classes and
 * flags; the controller classes' answers (PlayerShootController 6, the bomb launcher's 5, Mindless neither: the cause);
 * GetWeaponDetails3's ship walk; the group getters' offsets. */
static void test_hud_weapons_image(const char *path) {
    FILE *f=fopen(path,"rb");assert(f);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;IMAGE_SECTION_HEADER sections[32];
    assert(fread(&dos,1,sizeof(dos),f)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && !fseek(f,dos.e_lfanew,SEEK_SET));
    assert(fread(&nt,1,sizeof(nt),f)==sizeof(nt) && nt.FileHeader.TimeDateStamp==0x6a1eaded && nt.FileHeader.NumberOfSections<=32);
    unsigned count=nt.FileHeader.NumberOfSections;
    assert(fread(sections,sizeof(*sections),count,f)==count);
    static unsigned char bytes[0x100];
    for(unsigned i=0;i<HUDW_PIN_COUNT;i++)
        assert(test_pnat_file_bytes(f,sections,count,hudw_pins_list[i].rva,hudw_pins_list[i].size,bytes) && hash_ok(bytes,hudw_pins_list[i].size,hudw_pins_list[i].sha));
    /* each site: E8 to its test, rcx = rbx (the weapon) set before it (mov rcx, rbx within the 16 bytes before) */
    for(unsigned i=0;i<HUDW_SITES;i++) {
        assert(test_pnat_file_bytes(f,sections,count,hudw_sites[i].rva-16u,21u,bytes));
        int32_t r;memcpy(&r,bytes+17,4);assert(bytes[16]==0xe8 && hudw_sites[i].rva+5u+(unsigned)r==hudw_sites[i].target);
        int found=0;static const unsigned char mov[3]={0x48,0x8b,0xcb};
        for(unsigned at=0;at+3<=16;at++) if(!memcmp(bytes+at,mov,3)) found=1;
        assert(found);
    }
    /* the primary test: +0x318, class 5 then class 6 with +0xa1; the secondary one: +0x318, class 6 with +0xa2 */
    assert(test_pnat_file_bytes(f,sections,count,HUDW_PRIMARY,0x5fu,bytes));
    {uint32_t v;memcpy(&v,bytes+0x0c,4);assert(bytes[0x09]==0x48 && bytes[0x0a]==0x8b && v==HUDW_CONTROLLER);
     memcpy(&v,bytes+0x19,4);assert(bytes[0x18]==0xba && v==5u);memcpy(&v,bytes+0x34,4);assert(bytes[0x33]==0xba && v==6u);
     memcpy(&v,bytes+0x48,4);assert(bytes[0x46]==0x80 && v==0xa1u);}
    assert(test_pnat_file_bytes(f,sections,count,HUDW_SECONDARY,0x44u,bytes));
    {uint32_t v;memcpy(&v,bytes+0x0c,4);assert(v==HUDW_CONTROLLER);memcpy(&v,bytes+0x19,4);assert(bytes[0x18]==0xba && v==6u);
     memcpy(&v,bytes+0x2d,4);assert(bytes[0x2b]==0x80 && v==0xa2u);}
    /* the classes (vtable slot 3, of class?): PlayerShootController 6 or 1; the bomb launcher's 5 or 1; Mindless 3 or 1 */
    {uint64_t entry;
     assert(test_pnat_file_bytes(f,sections,count,HUDW_PLAYER_VTABLE+0x18u,8u,bytes));memcpy(&entry,bytes,8);
     assert(entry-nt.OptionalHeader.ImageBase==0x7e5a30u);
     assert(test_pnat_file_bytes(f,sections,count,0x7e5a30u,0x10u,bytes));
     assert(bytes[0]==0x83 && bytes[1]==0xfa && bytes[2]==6 && bytes[5]==0x83 && bytes[6]==0xfa && bytes[7]==1);
     assert(test_pnat_file_bytes(f,sections,count,PFIRE_MINDLESS_VTABLE+0x18u,8u,bytes));memcpy(&entry,bytes,8);
     assert(entry-nt.OptionalHeader.ImageBase==0x6a1570u);
     assert(test_pnat_file_bytes(f,sections,count,0x6a1570u,0x0cu,bytes));
     uint32_t mask;memcpy(&mask,bytes+4,4);assert(bytes[0]==0x8d && bytes[2]==0xff && bytes[3]==0xa9 && mask==0xfffffffdu);   /* (c-1)&~2: 1 or 3 */
     assert(test_pnat_file_bytes(f,sections,count,0x2b7d2c8u+0x18u,8u,bytes));memcpy(&entry,bytes,8);
     assert(entry-nt.OptionalHeader.ImageBase==0x3882d0u);
     assert(test_pnat_file_bytes(f,sections,count,0x3882d0u,0x0cu,bytes));memcpy(&mask,bytes+4,4);assert(mask==0xfffffffbu);}  /* 1 or 5 */
    /* GetWeaponDetails3's ship: [g_Universe] +0x230, its +0x70 parents, class 0x75 through +0x11e8 */
    assert(test_pnat_file_bytes(f,sections,count,0xb087c4u,0x5du,bytes));
    {int32_t r;memcpy(&r,bytes+3,4);assert(bytes[0]==0x48 && bytes[1]==0x8b && bytes[2]==0x05 && 0xb087cbu+(unsigned)r==HUDW_UNIVERSE);
     uint32_t v;memcpy(&v,bytes+0x16,4);assert(bytes[0x14]==0x48 && bytes[0x15]==0x05 && v==HUDW_PLAYER);
     assert(bytes[0x2f]==0x48 && bytes[0x30]==0x8b && bytes[0x31]==0x5b && bytes[0x32]==HUDW_PARENT);
     memcpy(&v,bytes+0x40,4);assert(bytes[0x3f]==0xba && v==HUDW_SHIP_CLASS);
     memcpy(&v,bytes+0x49,4);assert(bytes[0x47]==0xff && bytes[0x48]==0x90 && v==HUDW_IS_CLASS);}
    /* the group getters: +0x668 / +0x638 (+0x640), +0x66c / +0x650 (+0x658), 0x18 per group */
    assert(test_pnat_file_bytes(f,sections,count,0x4e4800u,0x49u,bytes));
    {uint32_t v;memcpy(&v,bytes+3,4);assert(v==HUDW_ACTIVE);memcpy(&v,bytes+0x0f,4);assert(v==HUDW_GROUPS);memcpy(&v,bytes+0x20,4);assert(v==HUDW_GROUPS+8u);}
    assert(test_pnat_file_bytes(f,sections,count,0x4e4850u,0x49u,bytes));
    {uint32_t v;memcpy(&v,bytes+3,4);assert(v==HUDW_ACTIVE2);memcpy(&v,bytes+0x0f,4);assert(v==HUDW_GROUPS2);memcpy(&v,bytes+0x20,4);assert(v==HUDW_GROUPS2+8u);}
    fclose(f);
    puts("hud weapons image tests PASS");
}
