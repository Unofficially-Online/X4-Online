/* Every player's own superhighway (lockstep_highway_players.h): a highway's player is X4's when its ship is in it, else
 * the slot 2..N ship it recorded (first come), else the first seated slot ship in it; the camera calls of its scene,
 * booster and controllers answer that ship, the add / remove tests answer the recorded slot ship as X4's player's, the
 * flight model's ID test the player's ID; slots 2, 3 and 8; the sites by CAS, the tick check, another module's view. */
static unsigned char tdhwp_vt[8],tdhwp_other_vt[8];
static unsigned char *tdhwp_ships[PILOT_SLOTS+1],*tdhwp_native;
static unsigned char *tdhwp_slot(unsigned k) {return k<=PILOT_SLOTS?tdhwp_ships[k]:NULL;}
static void **tdhwp_native_fn(void *unused,void **out) {(void)unused;*out=tdhwp_native;return out;}
static uint64_t tdhwp_native_id(void *unused) {(void)unused;uint64_t id=0;if(tdhwp_native) memcpy(&id,tdhwp_native+8,8);return id;}
static void test_highway_players(void) {
    static unsigned char highway[0x700],other_highway[0x700],scene[0x100],booster[0x100],controller[0x100],zone[0x100];
    static unsigned char ships[PILOT_SLOTS+1][0x100],npc[0x100];
    #define TDHWP_PUT(buffer,offset,value) do {void *v_=(void*)(value);memcpy((unsigned char*)(buffer)+(offset),&v_,8);} while(0)
    void *const vt=tdhwp_vt,*const ovt=tdhwp_other_vt;
    memset(highway,0,sizeof(highway));memcpy(highway,&vt,8);TDHWP_PUT(highway,HWP_SCENE,scene);
    memset(other_highway,0,sizeof(other_highway));memcpy(other_highway,&vt,8);
    memset(scene,0,sizeof(scene));memcpy(scene,&ovt,8);TDHWP_PUT(scene,0x70,highway);
    memset(booster,0,sizeof(booster));memcpy(booster,&ovt,8);TDHWP_PUT(booster,0x70,scene);
    memset(controller,0,sizeof(controller));TDHWP_PUT(controller,0x70,scene);
    memset(zone,0,sizeof(zone));memcpy(zone,&ovt,8);
    for(unsigned k=1;k<=PILOT_SLOTS;k++) {memset(ships[k],0,sizeof(ships[k]));memcpy(ships[k],&ovt,8);uint64_t id=0x3f200u+k;memcpy(ships[k]+8,&id,8);TDHWP_PUT(ships[k],0x70,zone);}
    memset(npc,0,sizeof(npc));memcpy(npc,&ovt,8);{uint64_t id=0x3f2ffu;memcpy(npc+8,&id,8);}TDHWP_PUT(npc,0x70,highway);
    memset(tdhwp_ships,0,sizeof(tdhwp_ships));
    const hwp_out_fn saved_camera=hwp_fn.camera,saved_ship=hwp_fn.ship;const hwp_id_fn saved_id=hwp_fn.id;const unsigned char *const saved_vt=hwp_fn.highway_vt;
    unsigned char *const saved_slot1=pilot_slots[0].object;
    hwp_fn.camera=tdhwp_native_fn;hwp_fn.ship=tdhwp_native_fn;hwp_fn.id=tdhwp_native_id;hwp_fn.highway_vt=tdhwp_vt;hwp_slot_ship=tdhwp_slot;
    pilot_slots[0].object=ships[1];tdhwp_native=ships[1];
    memset(hwp_records,0,sizeof(hwp_records));hwp_installed=1;
    hwp_calls[0]=hwp_calls[1]=hwp_calls[2]=0;hwp_follow=hwp_players=hwp_ids=hwp_recorded_count=hwp_released=hwp_full=hwp_no_highway=0;
    const unsigned slots[3]={2,3,8};
    for(unsigned j=0;j<3;j++) {
        const unsigned k=slots[j],l=j==2?2:k+1;   /* slot k rides the highway; slot l (another slot) comes later */
        void *out=NULL;
        memset(hwp_records,0,sizeof(hwp_records));memset(tdhwp_ships,0,sizeof(tdhwp_ships));
        tdhwp_ships[k]=ships[k];tdhwp_ships[l]=ships[l];
        /* no slot ship in the highway: X4's answers */
        assert(hwp_out_rule(0,NULL,&out,scene,NULL)==&out && out==ships[1]);
        /* slot k's ship in the highway (not yet recorded): every camera call of the scene, booster, controllers is slot k's */
        TDHWP_PUT(ships[k],0x70,highway);
        for(unsigned i=0;i<9;i++) {
            unsigned char *ctx=hwp_sites[i].ctx==HWP_CTX_CONTROLLER?controller:i==2?booster:scene;
            out=NULL;assert(hwp_out_rule(i,NULL,&out,ctx,NULL)==&out && out==ships[k]);
        }
        out=NULL;assert(hwp_out_rule(9,NULL,&out,scene,NULL)==&out && out==ships[k]);   /* the traffic density */
        /* a ship of another highway's scene: X4's */
        {   static unsigned char far_scene[0x100];memset(far_scene,0,sizeof(far_scene));memcpy(far_scene,&ovt,8);TDHWP_PUT(far_scene,0x70,other_highway);
            out=NULL;hwp_out_rule(0,NULL,&out,far_scene,NULL);assert(out==ships[1]);}
        /* no highway above the context: X4's, counted */
        {const LONG64 none=hwp_no_highway;out=NULL;hwp_out_rule(0,NULL,&out,zone,NULL);assert(out==ships[1] && hwp_no_highway==none+1);}
        /* slot k enters: recorded, answered as X4's player's ship (the scene's player); the NPC stays traffic */
        out=NULL;hwp_out_rule(10,NULL,&out,ships[k],highway);assert(out==ships[k] && hwp_record_of(highway)==ships[k]);
        out=NULL;hwp_out_rule(10,NULL,&out,npc,highway);assert(out==ships[1]);
        /* the scene's start (slot 701) asks again: the same answer */
        out=NULL;hwp_out_rule(12,NULL,&out,ships[k],highway);assert(out==ships[k]);
        /* slot l enters the same highway, parented to the scene: its traffic; the camera still slot k's */
        TDHWP_PUT(ships[l],0x70,scene);
        out=NULL;hwp_out_rule(10,NULL,&out,ships[l],highway);assert(out==ships[1]);
        out=NULL;hwp_out_rule(0,NULL,&out,scene,NULL);assert(out==ships[k]);
        /* the flight model: slot k's ID is the player's, slot l's not, the NPC's not */
        {uint64_t id=0;memcpy(&id,ships[k]+8,8);assert(hwp_id_rule(13,NULL,id)==id);
         memcpy(&id,ships[l]+8,8);assert(hwp_id_rule(14,NULL,id)==0x3f201u);
         memcpy(&id,npc+8,8);assert(hwp_id_rule(13,NULL,id)==0x3f201u);}
        /* slot l leaves: it was not the player (X4's: traffic removed); slot k leaves: the record goes */
        out=NULL;hwp_out_rule(11,NULL,&out,ships[l],highway);assert(out==ships[1] && hwp_record_of(highway)==ships[k]);
        TDHWP_PUT(ships[k],0x70,zone);
        out=NULL;hwp_out_rule(11,NULL,&out,ships[k],highway);assert(out==ships[k] && !hwp_record_of(highway));
        /* now slot l, still in the scene, is the first slot ship in it: the camera follows it, its next entry records it */
        out=NULL;hwp_out_rule(5,NULL,&out,controller,NULL);assert(out==ships[l]);
        /* X4's own player in the highway: X4's answers everywhere, and no slot ship is recorded */
        TDHWP_PUT(ships[1],0x70,highway);
        out=NULL;hwp_out_rule(0,NULL,&out,scene,NULL);assert(out==ships[1]);
        out=NULL;hwp_out_rule(10,NULL,&out,ships[l],highway);assert(out==ships[1] && !hwp_record_of(highway));
        {uint64_t id=0;memcpy(&id,ships[l]+8,8);assert(hwp_id_rule(13,NULL,id)==0x3f201u);}
        TDHWP_PUT(ships[1],0x70,zone);TDHWP_PUT(ships[l],0x70,zone);
    }
    /* off: X4's answers */
    hwp_installed=0;{void *out=NULL;TDHWP_PUT(ships[2],0x70,highway);tdhwp_ships[2]=ships[2];hwp_out_rule(0,NULL,&out,scene,NULL);assert(out==ships[1]);
        TDHWP_PUT(ships[2],0x70,zone);}
    {char row[320];assert(hwp_status(row,sizeof(row)) && !strncmp(row,"installed=0 calls=",18) && strstr(row," players=") && strstr(row," why=- check=-"));}
    /* the record table full: counted, X4's answer */
    {   memset(hwp_records,0,sizeof(hwp_records));hwp_installed=1;
        static unsigned char highways[HWP_RECORDS][0x700];
        for(unsigned i=0;i<HWP_RECORDS;i++) {memset(highways[i],0,sizeof(highways[i]));memcpy(highways[i],&vt,8);assert(hwp_record_add(highways[i],ships[2]));}
        const LONG64 full=hwp_full;assert(!hwp_record_add(highway,ships[3]) && hwp_full==full+1);
        memset(hwp_records,0,sizeof(hwp_records));}
    /* the sites on a region of the image's shape: ours by CAS, the trampolines (context moves, the rule), the tick check, another
     * module's view of a pinned range reads X4's calls, X4's bytes back */
    {   unsigned char *const block=VirtualAlloc(NULL,0x1000000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(block);
        unsigned char *const region=block-0x5e0000u,*const saved_image=image,*const saved_page=hwp_page;
        image=region;hwp_page=NULL;hwp_installed=0;hwp_check_why="-";hwp_why="-";
        for(unsigned i=0;i<HWP_SITES;i++) {
            if(hwp_sites[i].site<0x5e0000u || hwp_sites[i].site+5>0x5e0000u+0x1000000u) continue;
            unsigned char o[5];hwp_original(i,o);memcpy(region+hwp_sites[i].site,o,5);
        }
        /* the region holds every site (0x5e0000..0x15e0000) */
        unsigned held=0;for(unsigned i=0;i<HWP_SITES;i++) held+=hwp_sites[i].site>=0x5e0000u && hwp_sites[i].site+5<=0x5e0000u+0x1000000u;
        assert(held==HWP_SITES);
        assert(hwp_install() && hwp_installed && hwp_check());
        for(unsigned i=0;i<HWP_SITES;i++) {
            int32_t rel;memcpy(&rel,region+hwp_sites[i].site+1,4);unsigned char *t=region+hwp_sites[i].site+5+rel;
            assert(region[hwp_sites[i].site]==0xe8 && t==hwp_page+i*HWP_TRAMP && !memcmp(t,hwp_sites[i].mov,3));
            const unsigned j=hwp_sites[i].mov2[0]?6:3;assert(!hwp_sites[i].mov2[0] || !memcmp(t+3,hwp_sites[i].mov2,3));
            void *to=NULL;assert(t[j]==0xff && t[j+1]==0x25);memcpy(&to,t+j+6,8);assert(to==hwp_rules[i]);
        }
        {   unsigned char view[0x40];const unsigned at=0xeb0995u;memcpy(view,region+at,sizeof(view));
            hwp_original_view(at,view,sizeof(view));unsigned char o[5];hwp_original(13,o);assert(!memcmp(view+(0xeb09a4u-at),o,5));}
        region[hwp_sites[3].site+1]^=1;assert(!hwp_check() && !strcmp(hwp_check_why,"site_lost"));region[hwp_sites[3].site+1]^=1;hwp_check_why="-";
        assert(hwp_check());
        hwp_uninstall();assert(!hwp_installed);
        for(unsigned i=0;i<HWP_SITES;i++) {unsigned char o[5];hwp_original(i,o);assert(!memcmp(region+hwp_sites[i].site,o,5));}
        /* a site that is not X4's call: refused, nothing written */
        region[hwp_sites[7].site]=0xcc;hwp_why="-";assert(!hwp_install() && !hwp_installed && !strcmp(hwp_why,"site"));
        for(unsigned i=0;i<HWP_SITES;i++) if(i!=7) {unsigned char o[5];hwp_original(i,o);assert(!memcmp(region+hwp_sites[i].site,o,5));}
        VirtualFree(hwp_page,0,MEM_RELEASE);hwp_page=saved_page;image=saved_image;VirtualFree(block,0,MEM_RELEASE);}
    #undef TDHWP_PUT
    hwp_installed=0;hwp_why="-";hwp_check_why="-";memset(hwp_records,0,sizeof(hwp_records));hwp_slot_ship=NULL;
    hwp_fn.camera=saved_camera;hwp_fn.ship=saved_ship;hwp_fn.id=saved_id;hwp_fn.highway_vt=saved_vt;pilot_slots[0].object=saved_slot1;
    printf("highway players: a highway's player (X4's, the recorded slot, the first seated slot), the camera calls of its scene, booster and controllers, the add / remove tests, the flight model's ID (slots 2, 3, 8), another highway, no highway, off, the table full, the sites, the check, another module's view tests PASS\n");
}
static void test_highway_players_image(const char *path) {
    FILE *f=fopen(path,"rb");assert(f);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;IMAGE_SECTION_HEADER sections[32];
    assert(fread(&dos,1,sizeof(dos),f)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && !fseek(f,dos.e_lfanew,SEEK_SET));
    assert(fread(&nt,1,sizeof(nt),f)==sizeof(nt) && nt.FileHeader.TimeDateStamp==0x6a1eaded && nt.FileHeader.NumberOfSections<=32);
    unsigned count=nt.FileHeader.NumberOfSections;
    assert(fread(sections,sizeof(*sections),count,f)==count);
    static unsigned char bytes[0x200];
    for(unsigned i=0;i<sizeof(hwp_fn_pins)/sizeof(*hwp_fn_pins);i++)
        assert(test_pnat_file_bytes(f,sections,count,hwp_fn_pins[i].rva,hwp_fn_pins[i].size,bytes) && hash_ok(bytes,hwp_fn_pins[i].size,hwp_fn_pins[i].sha));
    for(unsigned i=0;i<HWP_SITES;i++) {
        const hwp_site *s=&hwp_sites[i];
        assert(s->block<=s->site && s->site+5<=s->block+s->block_size && s->block_size<=sizeof(bytes));
        assert(test_pnat_file_bytes(f,sections,count,s->block,s->block_size,bytes) && hash_ok(bytes,s->block_size,s->block_sha));
        int32_t r;const unsigned char *call=bytes+(s->site-s->block);memcpy(&r,call+1,4);
        assert(call[0]==0xe8 && (unsigned)(s->site+5)+(unsigned)r==s->target);
    }
    /* the context registers: each is set from the function's this (or the ship / ID) before the call, and the compare after */
    static const struct {unsigned rva;unsigned char size,code[8];} sites[]={
        {0x5edf24u,3,{0x48,0x8b,0xf1}},{0x5ee8cbu,3,{0x48,0x8b,0xf9}},{0x5ec4b6u,3,{0x48,0x8b,0xe9}},{0x5ef2f9u,3,{0x48,0x8b,0xe9}},
        {0x5f116bu,3,{0x4c,0x8b,0xe9}},{0xda0a38u,3,{0x48,0x8b,0xd9}},{0xda0cdcu,3,{0x48,0x8b,0xd9}},{0xda0fe5u,3,{0x48,0x8b,0xf1}},
        {0xda1618u,3,{0x48,0x8b,0xf9}},{0x5ef85au,3,{0x4c,0x8b,0xf9}},{0x5ff989u,3,{0x48,0x3b,0x18}},{0x5ffa4eu,3,{0x48,0x3b,0x18}},
        {0x5ffc7fu,3,{0x48,0x3b,0x38}},{0xeb09a9u,3,{0x48,0x3b,0xf8}},{0xeb104du,3,{0x48,0x3b,0xd8}},
        {0x5ffb92u,3,{0x48,0x8b,0xf1}},{0x5ff934u,3,{0x48,0x8b,0xf1}},{0x5ff9d4u,3,{0x48,0x8b,0xf1}}};
    for(unsigned i=0;i<sizeof(sites)/sizeof(*sites);i++)
        assert(test_pnat_file_bytes(f,sections,count,sites[i].rva,sites[i].size,bytes) && !memcmp(bytes,sites[i].code,sites[i].size));
    fclose(f);
    printf("highway players image: pins, the 15 calls and their context registers PASS\n");
}
