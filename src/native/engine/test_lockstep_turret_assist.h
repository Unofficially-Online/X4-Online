/* A player 2..N ship's turrets in "attack my current target" (lockstep_turret_assist.h): the rule answers slot k's soft
 * target for a turret on slot k's ship (slots 2, 3 and 8; the turret on the ship or deeper under it, also while slot k is
 * on foot), none when slot k has no target or its object is gone, and X4's answer (slot 1's) for slot 1's ship, a ship
 * nobody sits in, a seat left, a turret too deep, a parent loop, no turret and the module off; the site by CAS, the
 * trampoline, the tick check, a site that is not X4's. */
static void *ttast_slot1_target;
static unsigned ttast_getter_calls;
static unsigned char ttast_objects[PILOT_SLOTS+1][0x40];
static void **ttast_target(void *unused,void **out) {(void)unused;++ttast_getter_calls;*out=ttast_slot1_target;return out;}
static void *ttast_find(uint64_t id) {return id>=0x1000u+2 && id<=0x1000u+PILOT_SLOTS?(void*)ttast_objects[id-0x1000u]:NULL;}
static void test_turret_assist(void) {
    static unsigned char players[PILOT_SLOTS+1][0x6e00],ships[PILOT_SLOTS+1][0x100],mounts[PILOT_SLOTS+1][0x100],turrets[PILOT_SLOTS+1][0x400],
        deep[PILOT_SLOTS+1][0x400],chain[6][0x100],npc[0x100],npc_turret[0x400],slot1_target[0x40],loop[0x100],loop_turret[0x400];
    pslot_slot kept[PILOT_SLOTS+1];memcpy(kept,pslots,sizeof(kept));
    const tast_out_fn saved_target=tast_fn.target;const tast_find_fn saved_find=tast_fn.find;
    #define TTAST_PUT(object,parent) do {unsigned char *const p_=(parent);memcpy((object)+TAST_PARENT,&p_,8);} while(0)
    for(unsigned k=0;k<=PILOT_SLOTS;k++) {
        memset(&pslots[k],0,sizeof(pslots[k]));memset(players[k],0,sizeof(players[k]));memset(ships[k],0,sizeof(ships[k]));
        memset(mounts[k],0,sizeof(mounts[k]));memset(turrets[k],0,sizeof(turrets[k]));memset(deep[k],0,sizeof(deep[k]));
        pslots[k].player=players[k];pslots[k].ship=ships[k];pslots[k].seated=1;
        TTAST_PUT(turrets[k],ships[k]);TTAST_PUT(mounts[k],ships[k]);TTAST_PUT(deep[k],mounts[k]);
        const uint64_t id=0x1000u+k;memcpy(players[k]+TAST_SOFT_TARGET,&id,8);
    }
    memset(npc,0,sizeof(npc));memset(npc_turret,0,sizeof(npc_turret));TTAST_PUT(npc_turret,npc);
    tast_fn.target=ttast_target;tast_find_test=ttast_find;ttast_slot1_target=slot1_target;
    tast_installed=1;tast_calls=tast_changed=tast_none=0;memset((void*)tast_answered,0,sizeof(tast_answered));
    const unsigned slots[3]={2,3,8};
    for(unsigned j=0;j<3;j++) {
        const unsigned k=slots[j];
        /* slot k's turret, on the ship and one part deeper: slot k's target, X4's getter still called */
        void *out=NULL;ttast_getter_calls=0;
        assert(tast_rule(NULL,&out,turrets[k])==&out && out==ttast_objects[k] && ttast_getter_calls==1);
        out=NULL;assert(tast_rule(NULL,&out,deep[k])==&out && out==ttast_objects[k] && ttast_getter_calls==2);
        assert(tast_answered[k]==2);
        /* on foot from its seat (seated for the scope): still its ship's turrets */
        pslots[k].walking=1;out=NULL;assert(tast_rule(NULL,&out,turrets[k])==&out && out==ttast_objects[k]);pslots[k].walking=0;
        /* no target: none (as X4's player without one); its object gone: none */
        {const LONG64 none=tast_none;uint64_t id=0;memcpy(players[k]+TAST_SOFT_TARGET,&id,8);
         out=NULL;assert(tast_rule(NULL,&out,turrets[k])==&out && !out && tast_none==none+1);
         id=0x77u;memcpy(players[k]+TAST_SOFT_TARGET,&id,8);out=NULL;assert(tast_rule(NULL,&out,turrets[k])==&out && !out && tast_none==none+2);
         id=0x1000u+k;memcpy(players[k]+TAST_SOFT_TARGET,&id,8);}
        /* slot k left its seat: X4's answer */
        pslots[k].seated=0;out=NULL;assert(tast_rule(NULL,&out,turrets[k])==&out && out==slot1_target);pslots[k].seated=1;
        /* slot k without a player: X4's answer */
        pslots[k].player=NULL;out=NULL;assert(tast_rule(NULL,&out,turrets[k])==&out && out==slot1_target);pslots[k].player=players[k];
    }
    void *out=NULL;
    /* slot 1's ship and a ship nobody sits in: X4's answer (slot 1's target) */
    assert(tast_rule(NULL,&out,turrets[1])==&out && out==slot1_target);
    out=NULL;assert(tast_rule(NULL,&out,npc_turret)==&out && out==slot1_target);
    /* five parents up: too deep, X4's answer; four: slot 2's */
    {for(unsigned i=0;i<6;i++) memset(chain[i],0,sizeof(chain[i]));
     TTAST_PUT(chain[0],chain[1]);TTAST_PUT(chain[1],chain[2]);TTAST_PUT(chain[2],chain[3]);TTAST_PUT(chain[3],chain[4]);TTAST_PUT(chain[4],ships[2]);
     out=NULL;assert(tast_rule(NULL,&out,chain[0])==&out && out==slot1_target);
     out=NULL;assert(tast_rule(NULL,&out,chain[1])==&out && out==ttast_objects[2]);}
    /* a parent loop ends the walk; no turret; the module off: X4's answer */
    memset(loop,0,sizeof(loop));memset(loop_turret,0,sizeof(loop_turret));TTAST_PUT(loop,loop);TTAST_PUT(loop_turret,loop);
    out=NULL;assert(tast_rule(NULL,&out,loop_turret)==&out && out==slot1_target);
    out=NULL;assert(tast_rule(NULL,&out,NULL)==&out && out==slot1_target);
    tast_installed=0;out=NULL;assert(tast_rule(NULL,&out,turrets[2])==&out && out==slot1_target);
    /* the status: the answers per slot 2..8 */
    {char status[320];assert(tast_status(status,sizeof(status))>0);
     assert(strstr(status,"installed=0 ") && strstr(status," slots=6/5/0/0/0/0/5"));}
    /* the site on a region of the image's shape: ours by CAS, the trampoline (mov r8, rdi; the rule), the tick check, X4's
     * call back; a site that is not X4's call: refused, nothing written */
    {   unsigned char *const block=VirtualAlloc(NULL,0x1000000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(block);
        unsigned char *const region=block-0x400000u,*const saved_image=image,*const saved_page=tast_page;
        image=region;tast_page=NULL;tast_installed=0;tast_why="-";tast_check_why="-";
        unsigned char o[5];tast_original(o);memcpy(region+TAST_SITE,o,5);
        assert(tast_install() && tast_installed && tast_check());
        int32_t rel;memcpy(&rel,region+TAST_SITE+1,4);unsigned char *const t=region+TAST_SITE+5+rel;
        assert(region[TAST_SITE]==0xe8 && t==tast_page);
        static const unsigned char head[5]={0x4c,0x8b,0xc7,0xff,0x25};void *to=NULL;
        assert(!memcmp(t,head,5) && !t[5] && !t[6] && !t[7] && !t[8]);memcpy(&to,t+9,8);assert(to==(void*)tast_rule);
        region[TAST_SITE+2]^=1;assert(!tast_check() && !strcmp(tast_check_why,"site_lost"));region[TAST_SITE+2]^=1;tast_check_why="-";
        assert(tast_check());
        tast_uninstall();assert(!tast_installed && !memcmp(region+TAST_SITE,o,5) && !strcmp(tast_why,"-"));
        region[TAST_SITE]=0xcc;assert(!tast_install() && !tast_installed && !strcmp(tast_why,"site") && region[TAST_SITE]==0xcc);
        VirtualFree(tast_page,0,MEM_RELEASE);tast_page=saved_page;image=saved_image;VirtualFree(block,0,MEM_RELEASE);}
    #undef TTAST_PUT
    tast_installed=0;tast_why="-";tast_check_why="-";tast_find_test=NULL;tast_fn.target=saved_target;tast_fn.find=saved_find;
    tast_calls=tast_changed=tast_none=0;memset((void*)tast_answered,0,sizeof(tast_answered));
    memcpy(pslots,kept,sizeof(kept));
    puts("turret assist tests PASS");
}
/* X4.exe 9.00: the pins, X4's call at the site, the turret's register and the getter's universe, offset, registry and map. */
static void test_turret_assist_image(const char *path) {
    FILE *f=fopen(path,"rb");assert(f);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;IMAGE_SECTION_HEADER sections[32];
    assert(fread(&dos,1,sizeof(dos),f)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && !fseek(f,dos.e_lfanew,SEEK_SET));
    assert(fread(&nt,1,sizeof(nt),f)==sizeof(nt) && nt.FileHeader.TimeDateStamp==0x6a1eaded && nt.FileHeader.NumberOfSections<=32);
    unsigned count=nt.FileHeader.NumberOfSections;
    assert(fread(sections,sizeof(*sections),count,f)==count);
    static unsigned char bytes[0x100];
    for(unsigned i=0;i<sizeof(tast_pins_list)/sizeof(*tast_pins_list);i++)
        assert(test_pnat_file_bytes(f,sections,count,tast_pins_list[i].rva,tast_pins_list[i].size,bytes) && hash_ok(bytes,tast_pins_list[i].size,tast_pins_list[i].sha));
    /* the block: mov rdi, rdx (the turret) first, mode 13's test, then X4's call of the getter */
    assert(test_pnat_file_bytes(f,sections,count,0x4ef3afu,0xa2u,bytes));
    {static const unsigned char rdi[3]={0x48,0x8b,0xfa},mode[4]={0x41,0x83,0xff,0x0d};
     assert(!memcmp(bytes,rdi,3) && !memcmp(bytes+(0x4ef431u-0x4ef3afu),mode,4));
     const unsigned char *call=bytes+(TAST_SITE-0x4ef3afu);int32_t r;memcpy(&r,call+1,4);
     assert(call[0]==0xe8 && (unsigned)(TAST_SITE+5)+(unsigned)r==TAST_PLAYER_TARGET);}
    /* the getter: the universe's player (+0x230), its soft target (+0x6dd0), the map and the registry's find */
    assert(test_pnat_file_bytes(f,sections,count,TAST_PLAYER_TARGET,0x99u,bytes));
    {int32_t r;memcpy(&r,bytes+0x0d,4);assert(bytes[0x0a]==0x48 && bytes[0x0b]==0x8b && bytes[0x0c]==0x35 && 0x9fcce1u+(unsigned)r==0x3d198f8u);
     uint32_t off;memcpy(&off,bytes+0x1c,4);assert(bytes[0x19]==0x48 && off==0x230u);
     memcpy(&off,bytes+0x34,4);assert(bytes[0x31]==0x48 && bytes[0x32]==0x8b && bytes[0x33]==0x96 && off==TAST_SOFT_TARGET);
     memcpy(&r,bytes+0x58,4);assert(bytes[0x55]==0x48 && bytes[0x56]==0x8b && bytes[0x57]==0x0d && 0x9fcd2cu+(unsigned)r==TAST_MAP);
     memcpy(&r,bytes+0x63,4);assert(bytes[0x62]==0xe8 && 0x9fcd37u+(unsigned)r==TAST_FIND);}
    fclose(f);
    puts("turret assist image tests PASS");
}
