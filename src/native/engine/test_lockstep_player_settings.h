/* Player settings per slot (lockstep_player_settings.h) on a private executable region, with the faction family's stand-ins
 * (test_lockstep_ui_faction.h: the guard's real hook, the claimed entries) and stand-ins of X4's setters that do what X4's do
 * to its player (the fields, the cover's faction's logo, the repaint and re-dress of g_PlayerFaction's objects through the
 * hooked readers, the ware vector through push_back). Three slots in factions mode (player, x4lan_player2, x4lan_player3):
 * the arming and the install, the tables filled from X4's player, capture of slot k's settings, the apply on two "clients"
 * (the same rows, tables and receipt, X4's player untouched, slot k's ships repainted with slot k's theme, slot k's
 * faction's logo), the readers for every owner (slot table, X4's player, X4's own path; while a scope holds slot k's values),
 * the view and the presentation (the fields, the cover, the getters' "player"), refusals, coop, a scope left open, a patch
 * lost, the stop. The image test: the pins, the entries' first bytes, the offsets as X4's code uses them, tampering. */
static unsigned char tps_theme[4][0x40],tps_ware[3][0x180],tps_fac[3][0x400],tps_fdata[3][0x600],tps_props[0x800],tps_npc[0x200];
static unsigned char *tps_region;
static void *tps_seen_rbx,*tps_seen_rdi;
static int tps_licence;
/* An MSVC std::string of up to 15 bytes, inline. */
static void tps_string(unsigned char *s,const char *text) {
    size_t n=strlen(text);uint64_t size=n,cap=15;assert(n<=15);memset(s,0,16);memcpy(s,text,n);memcpy(s+0x10,&size,8);memcpy(s+0x18,&cap,8);
}
static void tps_put(unsigned char *at,const void *v) {memcpy(at,&v,8);}
static void *tps_get(const unsigned char *at) {void *v=NULL;memcpy(&v,at,8);return v;}
static unsigned char *tps_find_theme(const char *id) {
    for(unsigned i=0;i<4;i++) if(!strcmp((const char*)tps_theme[i]+PSS_THEME_ID,id)) return tps_theme[i];
    return NULL;
}
static unsigned char *tps_find_ware(const char *id) {
    static const char *ids[3]={"spacefuel","spaceweed","energycells"};
    for(unsigned i=0;i<3;i++) if(!strcmp(ids[i],id)) return tps_ware[i];
    return NULL;
}
/* X4's push_back of the ware vector: 4 inline, then the heap (here malloc; the tables keep what it hands out). */
static void tps_push_back(void *vector,void *const *value) {
    unsigned char *v=vector;uint64_t cap=0,size=0;memcpy(&cap,v+0x20,8);memcpy(&size,v+0x28,8);
    unsigned char *data=cap==4?v:tps_get(v);
    if(size==cap) {
        unsigned char *grown=malloc((size_t)cap*16);assert(grown);memcpy(grown,data,(size_t)size*8);
        if(cap!=4) free(data);
        cap*=2;memcpy(v+0x20,&cap,8);tps_put(v,grown);data=grown;
    }
    memcpy(data+8*size,value,8);++size;memcpy(v+0x28,&size,8);
}
static void tps_erase(unsigned char *v,void *ware) {
    uint64_t cap=0,size=0;memcpy(&cap,v+0x20,8);memcpy(&size,v+0x28,8);
    unsigned char *data=cap==4?v:tps_get(v);
    for(uint64_t i=0;i<size;i++) if(tps_get(data+8*i)==ware) {memmove(data+8*i,data+8*(i+1),(size_t)(size-i-1)*8);--size;memcpy(v+0x28,&size,8);return;}
}
static unsigned char *tps_props_of(void *macro) {(void)macro;return tps_props;}
static unsigned char tps_has_licence(void *holder,uint32_t type,void *faction) {(void)holder;(void)type;(void)faction;return (unsigned char)tps_licence;}
/* The hooked entries, called as X4's code calls them. */
static void *tps_paint(void *object,void *faction) {return ((void*(*)(void*,void*))(void*)(tps_region+PSS_PAINT_FN))(object,faction);}
static uint64_t tps_clothing(void *npc,void *faction) {return ((uint64_t(*)(void*,void*))(void*)(tps_region+PSS_CLOTHING_FN))(npc,faction);}
static unsigned char tps_illegal(void *ware,void *faction,void *holder) {return ((unsigned char(*)(void*,void*,void*))(void*)(tps_region+PSS_ILLEGAL_FN))(ware,faction,holder);}
static const char *tps_arg(unsigned i,const char *id) {
    if(pss_args[i].arg==0) return ((const char*(*)(const char*))(void*)(tps_region+pss_args[i].rva))(id);
    return ((const char*(*)(const char*,const char*,const char*))(void*)(tps_region+pss_args[i].rva))("signal",id,"");
}
/* X4's setters as they treat X4's player (the singleton) and g_PlayerFaction's objects. */
static void *tps_now(void) {return tps_get(tps_region+UITRADE_PLAYER_FACTION);}
static void tps_set_paint(const char *id) {
    unsigned char *p=pnat_player(),*theme=tps_find_theme(id);tps_put(p+0x6bb8,theme);tps_put(p+0x6bc0,theme);
    for(uint64_t id2=0x7001;id2<=0x7003;id2++) {unsigned char *ship=tuf_object(id2);if(tuf_owner(ship)==tps_now()) tps_put(ship+0x3a0,tps_paint(ship,tps_now()));}
}
static void tps_set_clothing(const char *id) {
    unsigned char *p=pnat_player(),*theme=tps_find_theme(id);tps_put(p+0x6bc8,theme);tps_put(p+0x6bd0,theme);
    tps_put(tps_npc+0x100,(void*)(uintptr_t)tps_clothing(tps_npc,tps_now()));
}
static void tps_set_illegal(const char *id,uint64_t on) {
    unsigned char *v=pnat_player()+0x8a6d0,*ware=tps_find_ware(id);
    if(on&0xff) {void *w=ware;tps_push_back(v,&w);} else tps_erase(v,ware);
}
static void tps_set_logo(const uifac_logo *logo) {
    unsigned char *p=pnat_player(),*owner=tps_get(p+PSS_COVER);if(!owner) owner=tps_get(p+0x2c0);
    unsigned char *data=tps_get(owner+PSS_FACTION_DATA);tps_string(data+PSS_LOGO_FILE,logo->file);data[PSS_LOGO_PERSONAL]=logo->ispersonal;
}
static void tps_set_wait(uint64_t v) {pnat_player()[0x8a6b4]=(unsigned char)(v&0xff);}
static void tps_set_level(float v) {memcpy(pnat_player()+0x8a6a8,&v,4);}
static void tps_retarget(const char *claim,void *fn) {memcpy(tps_region+tuf_rva(tuf_claim(claim))+7,&fn,8);}
/* The world as a save gives it to every client. */
static void tps_world(void) {
    unsigned char *p=tuf_player;memset(p,0,sizeof(tuf_player));
    tps_put(p+0x6bb8,tps_theme[0]);tps_put(p+0x6bc0,tps_theme[0]);tps_put(p+0x6bc8,tps_theme[2]);tps_put(p+0x6bd0,tps_theme[2]);
    float level[3]={0.5f,-1.0f,-1.0f};memcpy(p+0x8a6a8,level,12);p[0x8a6b4]=1;p[0x8a6b5]=1;p[0x8a720]=1;
    uint64_t four=4;memcpy(p+0x8a6d0+0x20,&four,8);
    for(unsigned i=0;i<5;i++) {void *w=tps_ware[1];tps_push_back(p+0x8a6d0,&w);}   /* five: X4's player's vector is on the heap */
    tps_put(p+0x2c0,tps_fac[0]);
    for(unsigned f=0;f<3;f++) {memset(tps_fdata[f],0,sizeof(tps_fdata[f]));tps_put(tps_fac[f]+PSS_FACTION_DATA,tps_fdata[f]);tps_string(tps_fdata[f]+PSS_LOGO_FILE,f?"faction_logo":"player_logo");}
    for(uint64_t id=0x7001;id<=0x7003;id++) tps_put(tuf_object(id)+0x3a0,NULL);
    tps_put(tps_npc+0x100,NULL);tps_put(tps_region+UITRADE_PLAYER_FACTION,tps_fac[0]);
    memset(pss_slots,0,sizeof(pss_slots));if(pss_armed) pss_refresh();
}
static void tps_owner(uint64_t id,unsigned f) {tps_put(tuf_object(id)+0x348,tps_fac[f]);}
static void tps_code(unsigned rva,const unsigned char *b,unsigned n) {memcpy(tps_region+rva,b,n);}
static void test_player_settings(void) {
    unsigned char *saved_base=pnat_base,*saved_image=image;DWORD saved_ui=ui_thread;uint64_t saved_issued=stats.issued,saved_stop=pilot_stop,saved_start=pstk_capture_start;
    int saved_capturing=pstk_capturing,saved_armed=pnat_armed,saved_mode=pnat_mode,saved_slots=pslot_armed,saved_present=pres_armed;
    unsigned saved_capture_slot=pstk_capture_slot,saved_count=pilot_slot_count,saved_mask=pstk_slot_mask,saved_pres_slot=pres_slot;uirep_fns saved_fn=uirep_fn;
    const uiguard_entry *saved_tab=uiguard_tab;unsigned saved_tab_count=uiguard_count;pilot_slot saved_pilot[3];memcpy(saved_pilot,pilot_slots,sizeof(saved_pilot));
    unsigned char *region=VirtualAlloc(NULL,TEST_PNAT_REGION,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);assert(region);
    tps_region=tuf_region=region;pnat_base=image=region;ui_thread=GetCurrentThreadId();memset(region,0xcc,0x4000);test_uirep_reset();
    {void *map=tuf_map_marker;memcpy(region+UIREP_MAP,&map,8);unsigned char *game=tuf_game,*player=tuf_player;memcpy(region+PNAT_UNIVERSE,&game,8);memcpy(tuf_game+0x230,&player,8);}
    uirep_fn=(uirep_fns){.find=tuf_find,.portable=tuf_portable};uitrade_owner_fn=tuf_owner;uitrade_scope_open=0;
    /* The library: paint themes a/b/c, a clothing theme; wares; the macro's default theme. */
    tps_string(tps_theme[0]+PSS_THEME_ID,"paint_a");tps_string(tps_theme[1]+PSS_THEME_ID,"paint_b");tps_string(tps_theme[2]+PSS_THEME_ID,"cloth_a");
    tps_string(tps_theme[3]+PSS_THEME_ID,"paint_c");
    {static unsigned char fallback[0x40];tps_string(fallback+PSS_THEME_ID,"macro_default");tps_put(tps_props+0x7c0,fallback);}
    memset(tps_ware,0,sizeof(tps_ware));for(unsigned i=0;i<3;i++) {uint64_t two=2;memcpy(tps_ware[i]+PSS_WARE_LIST+0x10,&two,8);}
    {void *argon=(void*)0xa460;tps_put(tps_ware[2]+PSS_WARE_LIST,argon);uint64_t one=1;memcpy(tps_ware[2]+PSS_WARE_LIST+0x18,&one,8);}   /* energycells: illegal to "argon" */
    uint32_t licence=7;memcpy(tps_ware[0]+PSS_WARE_LICENCE,&licence,4);
    /* Code at the hooked entries: X4's first bytes; the clothing function's three continuations and the four getters. */
    tps_code(PSS_PAINT_FN,pss_paint_orig,5);tps_code(PSS_ILLEGAL_FN,pss_mov_rbx,5);
    {static const unsigned char head[]={0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0xc7,0xc0,0x0c,0x57,0,0,0x48,0x8b,0x5c,0x24,0x30,0x48,0x83,0xc4,0x20,0x5f,0xc3};
     static const unsigned char def[]={0x48,0xc7,0xc0,0xfa,0xd3,0,0,0x49,0x89,0xc1,0xeb,0x04};
     unsigned char go[64];unsigned n=0;void *a=&tps_seen_rbx,*b=&tps_seen_rdi;
     go[n++]=0x48;go[n++]=0xb8;memcpy(go+n,&a,8);n+=8;go[n++]=0x48;go[n++]=0x89;go[n++]=0x18;
     go[n++]=0x48;go[n++]=0xb8;memcpy(go+n,&b,8);n+=8;go[n++]=0x48;go[n++]=0x89;go[n++]=0x38;
     static const unsigned char out[]={0x4c,0x89,0xc8,0x48,0x8b,0x5c,0x24,0x30,0x48,0x83,0xc4,0x20,0x5f,0xc3};memcpy(go+n,out,sizeof(out));n+=sizeof(out);
     tps_code(PSS_CLOTHING_FN,head,sizeof(head));tps_code(PSS_CLOTHING_DEFAULT,def,sizeof(def));tps_code(PSS_CLOTHING_GO,go,n);}
    {static const unsigned char t0[]={0x48,0x89,0xd0,0x48,0x8b,0x6c,0x24,0x20,0xc3},t1[]={0x48,0x89,0xd0,0x48,0x83,0xc4,0x28,0x5d,0x5b,0xc3},
         t2[]={0x48,0x89,0xd0,0x48,0x83,0xc4,0x20,0x5f,0xc3},t3[]={0x4c,0x89,0xc8,0x48,0x83,0xc4,0x28,0xc3};
     const unsigned char *tails[PSS_ARGS]={t0,t1,t2,t3};const unsigned sizes[PSS_ARGS]={sizeof(t0),sizeof(t1),sizeof(t2),sizeof(t3)};
     for(unsigned i=0;i<PSS_ARGS;i++) {tps_code(pss_args[i].rva,pss_args[i].bytes,pss_args[i].prologue);tps_code(pss_args[i].rva+pss_args[i].prologue,tails[i],sizes[i]);}}
    /* Three slots, factions mode: 0x7001 the player's, 0x7002 x4lan_player2's, 0x7003 x4lan_player3's. */
    tps_owner(0x7001,0);tps_owner(0x7002,1);tps_owner(0x7003,2);
    memset(pilot_slots,0,sizeof(saved_pilot));pilot_slots[0].runtime=0x7001;pilot_slots[1].runtime=0x7002;pilot_slots[2].runtime=0x7003;
    tuf_reset_managers(40);tuf_table_build();
    tps_retarget("SetPlayerPaintTheme",(void*)tps_set_paint);tps_retarget("SetPlayerClothingTheme",(void*)tps_set_clothing);
    tps_retarget("SetPlayerIllegalWare",(void*)tps_set_illegal);tps_retarget("SetPlayerLogo",(void*)tps_set_logo);
    tps_retarget("SetPlayerShipsWaitForPlayer",(void*)tps_set_wait);
    {unsigned c=tuf_claim("SetPlayerGlobalLoadoutLevel");void *fn=(void*)tps_set_level;memcpy(region+tuf_rva(c)+7,&fn,8);}
    uiguard_tab=tuf_table;uiguard_count=TUF_ENTRIES;uiguard_region=NULL;uiguard_installed=0;uiguard_armed=0;memset(uiguard_on,0,sizeof(uiguard_on));
    uiguard_unclaim(NULL);uifac_armed=0;pss_armed=0;pss_page=NULL;pss_installed=0;pss_done=0;pres_settings_hook=NULL;
    pnat_armed=1;pnat_mode=PNAT_MODE_STOCK;pstk_capturing=1;pilot_slot_count=3;pstk_slot_mask=7;pslot_armed=1;pres_armed=1;
    pstk_capture_start=1;stats.issued=10;pilot_stop=100000;pstk_capture_slot=2;rid_probe_frame=0;pres_open=0;render_rng_in_slot=0;uirep_dispatch_depth=0;
    tps_world();
    /* ---- arming: the faction family first; the entries as X4's; the hooks at the first pass ---- */
    pss_pin_tab=NULL;pss_pin_count=0;pss_pins_checked=0;pss_fn=(pss_fns){0};
    assert(!pss_arm_using() && !strcmp(pss_arm_why,"needs_ui_faction"));
    assert(uirep_arm_using(UIREP_FAMILY_FACTION|UIREP_FAMILY_TRAVEL));uiguard_groups=0;assert(uiguard_arm_using(1u<<UIGUARD_G_FLIGHT));
    assert(uifac_arm_using());assert(!pss_arm_using() && !strcmp(pss_arm_why,"functions"));
    pss_fn=(pss_fns){.macro_props=tps_props_of,.has_licence=tps_has_licence,.push_back=tps_push_back};
    region[PSS_ILLEGAL_FN]^=1;assert(!pss_arm_using() && !strcmp(pss_arm_why,"entry_changed"));region[PSS_ILLEGAL_FN]^=1;
    assert(pss_arm_using() && pss_armed && pres_settings_hook==pss_present_hook && pss_arm_using());
    /* Filled at the arming from X4's player, the vector rebuilt through push_back (its own heap block, not the player's). */
    assert(pss_slots[2].used && pss_slots[3].used && pss_slots[2].filled && !pss_slots[1].used && pss_fills==2 && pss_fill_wares==10);
    assert(pss_vec_size(pss_slots[2].values+PSS_AT_WARES)==5 && tps_get(pss_slots[2].values+PSS_AT_WARES)!=tps_get(tuf_player+0x8a6d0));
    assert(tps_get(pss_slots[3].values+PSS_AT_PAINT)==tps_theme[0] && !memcmp(pss_slots[3].values+PSS_AT_LEVEL,tuf_player+0x8a6a8,4));
    assert(!memcmp(pss_slots[3].values+PSS_AT_WAITS,tuf_player+0x8a6b4,2));
    {static const unsigned char none[8];assert(!memcmp(pss_slots[3].values+PSS_AT_LEVEL+4,none,8));}   /* X4's quantity and quality: not a slot's */
    assert(uiguard_install() && uiguard_intact());uiguard_live=1;
    assert(pss_pass(11) && pss_installed && pss_intact() && !strcmp(pss_stage,"live"));
    assert(region[PSS_PAINT_FN]==0xe9 && region[PSS_CLOTHING_FN]==0xe9 && region[PSS_ILLEGAL_FN]==0xe9);
    for(unsigned i=0;i<PSS_ARGS;i++) assert(region[pss_args[i].rva]==0xe9);
    /* ---- the readers before any change: every owner answers as X4 would ---- */
    unsigned char *ship1=tuf_object(0x7001),*ship2=tuf_object(0x7002),*ship3=tuf_object(0x7003);
    assert(tps_paint(ship1,tps_fac[0])==tps_theme[0] && tps_paint(ship2,tps_fac[1])==tps_theme[0]);   /* slot 2's table: the save's theme */
    assert(tps_paint(ship1,(void*)0xa460)==tps_get(tps_props+0x7c0));                                /* another faction: the macro's default */
    assert(tps_clothing(tps_npc,tps_fac[1])==(uint64_t)(uintptr_t)tps_theme[2] && tps_seen_rbx==tps_fac[1] && tps_seen_rdi==tps_npc);
    assert(tps_clothing(tps_npc,tps_fac[0])==0x570c && tps_clothing(tps_npc,(void*)0xa460)==0x570c);   /* X4's own path */
    assert(tps_illegal(tps_ware[1],tps_fac[0],NULL)==1 && tps_illegal(tps_ware[1],tps_fac[2],NULL)==1 && tps_illegal(tps_ware[0],tps_fac[1],NULL)==0);
    assert(tps_illegal(tps_ware[2],(void*)0xa460,NULL)==1 && tps_illegal(tps_ware[2],tps_fac[1],NULL)==0 && tps_illegal(tps_ware[2],NULL,NULL)==1);
    {const char *player="player";assert(tps_arg(3,player)==player && tps_arg(0,player)==player);}   /* outside a UI scope: as given */
    /* ---- two clients presenting different slots start identical (census f3set): X4's player's loadout quantity and quality
     * (+0x8a6ac/+0x8a6b0) hold heap bytes of each process (X4 never constructs, loads or saves them; client 2 read "a3b25632"
     * live, client 1 zero). Client 1 presents slot 1, client 2 slot 2: its presentation scope (slot 2's values in X4's
     * player's fields) is open on the UI thread while it fills a slot's table and takes the receipt and the rows. The
     * same tables, receipt and rows on both, nothing of the quantity or quality in them. ---- */
    {
        static const unsigned char heap[2][8]={{0,0,0,0,0,0,0,0},{0x61,0x33,0x62,0x32,0x35,0x36,0x33,0x32}};
        char frame[2][512],state[2][1536];static unsigned char table[2][4][PSS_BLOCK];
        for(int client=0;client<2;client++) {
            tps_world();memcpy(tuf_player+0x8a6ac,heap[client],8);
            memset(pss_slots,0,sizeof(pss_slots));pss_refresh();                                    /* the arming's fill */
            assert(pss_slots[2].used && pss_slots[3].used && !memcmp(pss_slots[2].values+PSS_AT_LEVEL+4,heap[0],8));
            /* the same replayed change on both clients: slot 2's level and ships wait now differ from X4's player's */
            float quarter=0.25f;memcpy(pss_slots[2].values+PSS_AT_LEVEL,&quarter,4);pss_slots[2].values[PSS_AT_WAITS]=0;
            if(client) {pres_slot=2;pss_present_hook(1);assert(pss_present_on && pss_swap_slot==2 && tuf_player[0x8a6b4]==0);}
            /* slot 3's faction first known now (inside client 2's presentation): X4's own values, not slot 2's */
            pss_slots[3].used=pss_slots[3].filled=0;assert(pss_fill(3));pss_slots[3].used=1;
            tuf_client_reset();
            assert(uifac_frame_text(frame[client],sizeof(frame[client]),1) && pss_state_text(state[client],sizeof(state[client])));
            if(client) {pss_present_hook(0);assert(!pss_present_on && !pss_swap_depth && tuf_player[0x8a6b4]==1);pres_slot=0;}
            for(unsigned k=2;k<=3;k++) memcpy(table[client][k],pss_slots[k].values,PSS_BLOCK);
        }
        assert(!strcmp(frame[0],frame[1]) && !strcmp(state[0],state[1]));
        assert(strstr(frame[0],";w111.3f000000;P2:011.3e800000.5.") && strstr(frame[0],",3:111.3f000000.5."));
        assert(strstr(state[0],"slot=1 wait=1/1 reserve=1 loadout=0.500 ") && strstr(state[0],"slot=2 wait=0/1 reserve=1 loadout=0.250 ") &&
               strstr(state[0],"slot=3 wait=1/1 reserve=1 loadout=0.500 "));
        for(unsigned k=2;k<=3;k++) {
            /* every byte but the ware vector's (its heap block is each client's own) the same; the wares the same */
            assert(!memcmp(table[0][k],table[1][k],PSS_AT_WARES) && !memcmp(table[0][k]+PSS_AT_RESERVE,table[1][k]+PSS_AT_RESERVE,PSS_BLOCK-PSS_AT_RESERVE));
            assert(!memcmp(table[1][k]+PSS_AT_LEVEL+4,heap[0],8));
            assert(pss_vec_size(table[0][k]+PSS_AT_WARES)==5 && pss_vec_size(table[1][k]+PSS_AT_WARES)==5);
            assert(pss_vec_has(table[1][k]+PSS_AT_WARES,4,0x20,0x28,tps_ware[1]) && !pss_vec_has(table[1][k]+PSS_AT_WARES,4,0x20,0x28,tps_ware[0]));
        }
        tps_world();
    }
    /* ---- client 2 (slot 2) captures its own settings: nothing runs here, records queued ---- */
    tuf_reset_counts();uirep_ui_depth=1;pstk_capture_slot=2;
    ((tuf_str2_fn)tuf_entry("SetPlayerPaintTheme"))("paint_b",NULL);
    ((void(*)(const char*))tuf_entry("SetPlayerClothingTheme"))("paint_c");
    ((tuf_bool_fn)tuf_entry("SetPlayerShipsWaitForPlayer"))(0);
    ((tuf_float_fn)tuf_entry("SetPlayerGlobalLoadoutLevel"))(0.25f);
    ((void(*)(const char*,unsigned char))tuf_entry("SetPlayerIllegalWare"))("spacefuel",1);
    ((void(*)(const char*,unsigned char))tuf_entry("SetPlayerIllegalWare"))("spaceweed",0);
    {uifac_logo logo={"logo_two","icon_two",1};((tuf_ptr_fn)tuf_entry("SetPlayerLogo"))(&logo);}
    assert(uirep_queue_count==7 && tuf_player[0x8a6b4]==1 && tps_get(tuf_player+0x6bb8)==tps_theme[0]);
    {char rows[256];assert(!uifac_refusals_text(rows,sizeof(rows)));}
    uirep_ui_depth=0;tuf_take_queue();
    /* ---- two clients apply the same wire: the same rows, tables and receipt; X4's player untouched ---- */
    char rows_a[4096],frames[2][512];unsigned char player_before[PSS_BLOCK],table_two[PSS_BLOCK];
    for(int client=0;client<2;client++) {
        tps_world();tuf_client_reset();
        for(unsigned r=0;r<PSS_REGIONS;r++) memcpy(player_before+pss_regions[r].at,tuf_player+pss_regions[r].offset,pss_regions[r].size);
        assert(tuf_apply(2,"x4lan_player2",NULL)==1);
        assert(strstr(tuf_rows,"SetPlayerPaintTheme scope settings|SetPlayerClothingTheme scope settings|SetPlayerShipsWaitForPlayer scope settings|"
            "SetPlayerGlobalLoadoutLevel scope settings|SetPlayerIllegalWare scope settings|SetPlayerIllegalWare scope settings|SetPlayerLogo scope settings|"));
        for(unsigned r=0;r<PSS_REGIONS;r++) assert(!memcmp(player_before+pss_regions[r].at,tuf_player+pss_regions[r].offset,pss_regions[r].size));
        assert(!tps_get(tuf_player+PSS_COVER) && tps_now()==tps_fac[0] && !pss_swap_depth && !uitrade_scope_open);
        const unsigned char *v=pss_slots[2].values;float level;memcpy(&level,v+PSS_AT_LEVEL,4);
        assert(tps_get(v+PSS_AT_PAINT)==tps_theme[1] && tps_get(v+PSS_AT_CLOTHING)==tps_theme[3] && v[PSS_AT_WAITS]==0 && v[PSS_AT_WAITS+1]==1 && level==0.25f);
        assert(pss_vec_size(v+PSS_AT_WARES)==5 && pss_vec_has(v+PSS_AT_WARES,4,0x20,0x28,tps_ware[0]));       /* + spacefuel, - one spaceweed */
        assert(tps_get(ship2+0x3a0)==tps_theme[1] && !tps_get(ship1+0x3a0) && !tps_get(ship3+0x3a0));         /* slot 2's ships repainted, theirs only */
        assert(tps_get(tps_npc+0x100)==tps_theme[3]);                                                            /* re-dressed with slot 2's */
        assert(!strcmp((const char*)tps_fdata[1]+PSS_LOGO_FILE,"logo_two") && tps_fdata[1][PSS_LOGO_PERSONAL]==1);
        assert(!strcmp((const char*)tps_fdata[0]+PSS_LOGO_FILE,"player_logo") && !strcmp((const char*)tps_fdata[2]+PSS_LOGO_FILE,"faction_logo"));
        assert(!memcmp(pss_slots[3].values+PSS_AT_LEVEL,player_before+PSS_AT_LEVEL,4) && !memcmp(pss_slots[3].values+PSS_AT_WAITS,player_before+PSS_AT_WAITS,2));   /* slot 3 keeps its own */
        assert(uifac_frame_text(frames[client],sizeof(frames[client]),1) && strstr(frames[client],";P2:011.3e800000.5."));
        assert(strstr(frames[client],",3:111.3f000000.5.") && strstr(frames[client],";h"));
        memcpy(table_two,pss_slots[2].values,PSS_BLOCK);   /* the last client's (its vector's heap block is its own) */
        if(!client) memcpy(rows_a,tuf_rows,sizeof(rows_a));else assert(!strcmp(rows_a,tuf_rows));
    }
    assert(!strcmp(frames[0],frames[1]));
    assert(pss_swaps[PSS_K_APPLY]==14 && !pss_refused);
    /* ---- the readers now: slot 2's for its faction, X4's player's and slot 3's unchanged ---- */
    assert(tps_paint(ship2,tps_fac[1])==tps_theme[1] && tps_paint(ship1,tps_fac[0])==tps_theme[0] && tps_paint(ship3,tps_fac[2])==tps_theme[0]);
    assert(tps_clothing(tps_npc,tps_fac[1])==(uint64_t)(uintptr_t)tps_theme[3] && tps_clothing(tps_npc,tps_fac[2])==(uint64_t)(uintptr_t)tps_theme[2]);
    assert(tps_illegal(tps_ware[0],tps_fac[1],NULL)==1 && tps_illegal(tps_ware[0],tps_fac[0],NULL)==0 && tps_illegal(tps_ware[0],tps_fac[2],NULL)==0);
    tps_licence=1;assert(tps_illegal(tps_ware[0],tps_fac[1],tps_fac[0])==0);tps_licence=0;assert(tps_illegal(tps_ware[0],tps_fac[1],tps_fac[0])==1);
    {unsigned char none[8]={0};memcpy(pss_slots[2].values+PSS_AT_PAINT,none,8);   /* no theme of its own: the macro's default */
     assert(tps_paint(ship2,tps_fac[1])==tps_get(tps_props+0x7c0));memcpy(pss_slots[2].values+PSS_AT_PAINT,table_two+PSS_AT_PAINT,8);
     memcpy(pss_slots[3].values+PSS_AT_CLOTHING,none,8);assert(tps_clothing(tps_npc,tps_fac[2])==0xd3fa);}                  /* the NPC macro's default */
    /* ---- slot 3: its own records change its own table only ---- */
    tuf_reset_counts();uirep_ui_depth=1;pstk_capture_slot=3;
    ((tuf_str2_fn)tuf_entry("SetPlayerPaintTheme"))("paint_c",NULL);((tuf_float_fn)tuf_entry("SetPlayerGlobalLoadoutLevel"))(0.75f);
    uirep_ui_depth=0;tuf_take_queue();tuf_client_reset();
    assert(tuf_apply(3,"x4lan_player3",NULL)==1 && strstr(tuf_rows,"SetPlayerPaintTheme scope settings|SetPlayerGlobalLoadoutLevel scope settings|"));
    assert(tps_paint(ship3,tps_fac[2])==tps_theme[3] && tps_get(ship3+0x3a0)==tps_theme[3] && !memcmp(pss_slots[2].values,table_two,PSS_BLOCK));
    {float level;memcpy(&level,pss_slots[3].values+PSS_AT_LEVEL,4);assert(level==0.75f);}
    /* ---- the view on client 2: X4's player holds slot 2's values, its cover is slot 2's faction, "player" is slot 2's ---- */
    pstk_capture_slot=2;uirep_ui_depth=1;strcpy(pss_own_id,"x4lan_player2");
    assert(uifac_view_enter()==1 && uifac_view_settings && pss_swap_slot==2 && tps_get(tuf_player+PSS_COVER)==tps_fac[1]);
    assert(tps_get(tuf_player+0x6bb8)==tps_theme[1] && tuf_player[0x8a6b4]==0 && tps_now()==tps_fac[1]);
    for(unsigned i=0;i<PSS_ARGS;i++) {assert(!strcmp(tps_arg(i,"player"),"x4lan_player2"));assert(!strcmp(tps_arg(i,"argon"),"argon"));}
    assert(pss_rewrites==PSS_ARGS);
    /* The readers while the view holds slot 2's values: slot 2's live, the player faction X4's player's saved values. */
    assert(tps_paint(ship2,tps_fac[1])==tps_theme[1] && tps_paint(ship1,tps_fac[0])==tps_theme[0] && tps_illegal(tps_ware[0],tps_fac[0],NULL)==0);
    {char state[1536];assert(pss_state_text(state,sizeof(state)) && strstr(state,"slot=1 wait=1/1 reserve=1 loadout=0.500 illegal=5 paint=paint_a clothing=cloth_a logo=player_logo"));
     assert(strstr(state,"slot=2 wait=0/1 reserve=1 loadout=0.250 illegal=5 paint=paint_b clothing=paint_c logo=logo_two/personal"));}
    assert(uifac_view_leave() && !uifac_view_open && !pss_swap_depth && !tps_get(tuf_player+PSS_COVER) && tps_get(tuf_player+0x6bb8)==tps_theme[0]);
    {const char *player="player";assert(tps_arg(0,player)==player);}
    /* A change inside a UI scope is refused and discarded; a cover changed inside the view is reported. */
    assert(uifac_view_enter()==1);tuf_player[0x8a720]=0;tps_put(tuf_player+PSS_COVER,tps_fac[2]);
    assert(!uifac_view_leave() && pss_refused==1 && !strcmp(pss_refused_region,"loop_reserve") && pss_slots[2].values[PSS_AT_RESERVE]==1 && tuf_player[0x8a720]==1);
    assert(!tps_get(tuf_player+PSS_COVER));
    /* ---- the presentation scope on client 2 (the whole UI): the values, no cover; the view nests inside it ---- */
    pres_slot=2;pss_present_hook(1);
    assert(pss_present_on && pss_swap_slot==2 && tps_get(tuf_player+0x6bb8)==tps_theme[1] && !tps_get(tuf_player+PSS_COVER));
    assert(!strcmp(tps_arg(1,"player"),"x4lan_player2"));
    assert(uifac_view_enter()==1 && pss_swap_depth==2 && tps_get(tuf_player+PSS_COVER)==tps_fac[1]);
    assert(uifac_view_leave() && pss_swap_depth==1 && !tps_get(tuf_player+PSS_COVER) && tps_get(tuf_player+0x6bb8)==tps_theme[1]);
    assert(!pss_swap_enter(3,PSS_K_VIEW,1) && !pss_swap_enter(2,PSS_K_APPLY,1));   /* another slot, an apply: never inside */
    pss_present_hook(0);assert(!pss_present_on && !pss_swap_depth && tps_get(tuf_player+0x6bb8)==tps_theme[0]);
    uirep_ui_depth=0;pres_slot=0;
    /* ---- a pass that finds a scope open stops, everything back; a patch lost stops ---- */
    assert(pss_swap_enter(2,PSS_K_VIEW,1));
    assert(!uifac_pass(12) && !strcmp(uifac_why,"player_settings_scope_open") && !pss_swap_depth && !tps_get(tuf_player+PSS_COVER));
    uifac_why="-";pss_why="-";pss_stage="live";
    region[pss_args[2].rva+1]^=1;assert(!pss_pass(13) && !strcmp(pss_why,"player_settings_hook_lost"));region[pss_args[2].rva+1]^=1;
    pss_why="-";pss_stage="live";assert(pss_pass(14));
    /* ---- coop: slot 2's ship is the player's; its table is not used, the entity's settings are X4's player's ---- */
    tps_owner(0x7002,0);assert(pss_pass(15) && !pss_slots[2].used && pss_slots[2].filled);
    assert(tps_paint(ship2,tps_fac[1])==tps_get(tps_props+0x7c0));   /* nobody answers for x4lan_player2 any more: X4's own path */
    tuf_reset_counts();tuf_client_reset();uirep_ui_depth=1;pstk_capture_slot=2;((tuf_bool_fn)tuf_entry("SetPlayerShipsWaitForPlayer"))(0);uirep_ui_depth=0;tuf_take_queue();
    assert(tuf_apply(2,"player",NULL)==1 && strstr(tuf_rows,"SetPlayerShipsWaitForPlayer|") && tuf_player[0x8a6b4]==0);
    tps_owner(0x7002,1);tuf_player[0x8a6b4]=1;assert(pss_pass(16) && pss_slots[2].used);
    /* An apply whose slot table is not in use is skipped everywhere (player_entity), as without the key. */
    tuf_reset_counts();tuf_client_reset();uirep_ui_depth=1;((tuf_bool_fn)tuf_entry("SetPlayerShipsWaitForPlayer"))(1);uirep_ui_depth=0;tuf_take_queue();
    pss_slots[2].used=0;assert(tuf_apply(2,"x4lan_player2",NULL)==1 && strstr(tuf_rows,"SetPlayerShipsWaitForPlayer skipped=player_entity|"));pss_slots[2].used=1;
    {char text[768];assert(pss_status(text,sizeof(text)) && strstr(text,"player_settings=live") && strstr(text,"slots=2/2") && strstr(text,"own=x4lan_player2"));}
    /* ---- the stop: every entry back as X4's ---- */
    assert(pss_pass(pilot_stop+1) && !pss_installed && pss_restored && !strcmp(pss_stage,"stopped") && pres_settings_hook==NULL);
    assert(!memcmp(region+PSS_PAINT_FN,pss_paint_orig,5) && !memcmp(region+PSS_CLOTHING_FN,pss_mov_rbx,5) && !memcmp(region+PSS_ILLEGAL_FN,pss_mov_rbx,5));
    for(unsigned i=0;i<PSS_ARGS;i++) assert(!memcmp(region+pss_args[i].rva,pss_args[i].bytes,5));
    pss_restore();assert(!pss_installed);
    uiguard_uninstall();uiguard_unclaim(NULL);uiguard_armed=0;uiguard_live=0;uifac_armed=0;pss_armed=0;pres_settings_hook=NULL;
    if(uiguard_region) {VirtualFree(uiguard_region,0,MEM_RELEASE);uiguard_region=NULL;}
    if(pss_page) {VirtualFree(pss_page,0,MEM_RELEASE);pss_page=NULL;}
    pss_pin_tab=pss_pin_table;pss_pin_count=PSS_PIN_COUNT;pss_pins_checked=0;
    uitrade_owner_fn=NULL;uiguard_tab=saved_tab;uiguard_count=saved_tab_count;memcpy(pilot_slots,saved_pilot,sizeof(saved_pilot));
    test_uirep_reset();VirtualFree(region,0,MEM_RELEASE);
    pnat_base=saved_base;image=saved_image;uirep_fn=saved_fn;ui_thread=saved_ui;stats.issued=saved_issued;pilot_stop=saved_stop;pstk_capture_start=saved_start;
    pstk_capturing=saved_capturing;pnat_armed=saved_armed;pnat_mode=saved_mode;pslot_armed=saved_slots;pres_armed=saved_present;pstk_capture_slot=saved_capture_slot;
    pilot_slot_count=saved_count;pstk_slot_mask=saved_mask;pres_slot=saved_pres_slot;
    printf("Player settings per slot: tables, apply on two clients, readers per owner, view, presentation, getters, coop, stop on stand-ins\n");
}
static void test_player_settings_native_image(const char *path) {
    unsigned char *prior=image,*prior_base=pnat_base;
    image=test_pmath_map(path);pnat_base=image;
    pss_pin_tab=pss_pin_table;pss_pin_count=PSS_PIN_COUNT;pss_pins_checked=0;assert(pss_pins());
    /* Every patched entry starts as pinned; the getters' copied first instructions are what X4 has. */
    for(unsigned h=0;h<PSS_HOOKS;h++) assert(!memcmp(image+pss_hook_rva(h),pss_hook_orig(h),5));
    for(unsigned i=0;i<PSS_ARGS;i++) assert(!memcmp(image+pss_args[i].rva,pss_args[i].bytes,pss_args[i].prologue));
    /* The offsets as X4's own code uses them (each inside a pinned body). */
    unsigned d=0;
    memcpy(&d,image+0x70a737u+3,4);assert(d==0x6bb8u);memcpy(&d,image+0x70a760u+3,4);assert(d==0x6bc0u);      /* mov [rcx+paint],rdx; [rdi+last],rbx */
    memcpy(&d,image+0x70a91au+3,4);assert(d==0x6bc8u);memcpy(&d,image+0x70a943u+3,4);assert(d==0x6bd0u);      /* clothing */
    memcpy(&d,image+0x1b9680u+3,4);assert(d==0x8a6d0u);                                                        /* SetPlayerIllegalWare: add rcx,wares */
    memcpy(&d,image+0x1dcf93u+4,4);assert(d==0x8a6acu);                                                        /* quantity, behind the level */
    assert(image[0x154907u]==0x4d && image[0x154909u]==0x48 && image[0x15490au]==0x20 && image[0x15491cu]==0x28);   /* capacity +0x20, size +0x28 */
    memcpy(&d,image+0x6d83f7u+3,4);assert(d==0x6bb8u);memcpy(&d,image+0x6d8420u+3,4);assert(d==0x7c0u);       /* the reader: the player's, the macro's */
    memcpy(&d,image+0x6bf580u+3,4);assert(d==0x6bc8u);
    assert(image[PSS_CLOTHING_GO]==0x4d && image[PSS_CLOTHING_GO+1]==0x85 && image[PSS_CLOTHING_GO+2]==0xc9);       /* test r9,r9 */
    assert(!memcmp(image+PSS_CLOTHING_DEFAULT,"\x48\x8b\x4f\x60",4) && image[PSS_CLOTHING_STOCK]==0x57);           /* mov rcx,[rdi+0x60]; push rdi */
    assert(!memcmp(image+0x6bf541u,"\x48\x8b\xda\x48\x8b\xf9",6));                                                 /* mov rbx,rdx; mov rdi,rcx */
    memcpy(&d,image+0x8fa766u+3,4);assert(d==PSS_WARE_LIST);memcpy(&d,image+0x8fa7c8u+2,4);assert(d==PSS_WARE_FLAGS);
    memcpy(&d,image+0x8fa7d5u+2,4);assert(d==PSS_WARE_LICENCE);
    memcpy(&d,image+0x1b9750u+2,4);assert(d==8u*PSS_OWNER_SLOT);                                                 /* SetPlayerLogo: call [rax+0x1610] */
    memcpy(&d,image+0x1b9771u+3,4);assert(d==PSS_FACTION_DATA);memcpy(&d,image+0x1b9778u+3,4);assert(d==PSS_LOGO_FILE);
    memcpy(&d,image+PSS_OWNER_FN+3,4);assert(d==PSS_COVER);
    {   /* the calls the replaced bodies make: 0x7a93f0 and HasLicence at their pinned places */
        int32_t rel=0;memcpy(&rel,image+0x6d841bu+1,4);assert(image[0x6d841bu]==0xe8 && 0x6d841bu+5u+(unsigned)rel==PSS_MACRO_PROPS);
        memcpy(&rel,image+0x8fa7eau+1,4);assert(image[0x8fa7eau]==0xe8 && 0x8fa7eau+5u+(unsigned)rel==PSS_HAS_LICENCE);
        memcpy(&rel,image+0x1b9696u+1,4);assert(image[0x1b9696u]==0xe8 && 0x1b9696u+5u+(unsigned)rel==PSS_PUSH_BACK);
    }
    /* Tampered code refuses: a reader's body, a setter, the Player vtable's slot 706. */
    image[PSS_PAINT_FN+0x40]^=1;pss_pins_checked=0;assert(!pss_pins() && pin_failed_rva==PSS_PAINT_FN);image[PSS_PAINT_FN+0x40]^=1;
    image[0x1b96d0u+0x80]^=1;pss_pins_checked=0;assert(!pss_pins() && pin_failed_rva==0x1b96d0u);image[0x1b96d0u+0x80]^=1;
    image[PSS_PLAYER_VTABLE+8u*PSS_OWNER_SLOT]^=8;pss_pins_checked=0;assert(!pss_pins() && pin_failed_rva==PSS_PLAYER_VTABLE);image[PSS_PLAYER_VTABLE+8u*PSS_OWNER_SLOT]^=8;
    pss_pins_checked=0;assert(pss_pins());pss_pins_checked=0;
    VirtualFree(image,0,MEM_RELEASE);image=prior;pnat_base=prior_base;
    printf("Player settings verified on the pinned image: %u pinned functions, %u patched entries, the offsets and calls\n",(unsigned)PSS_PIN_COUNT,(unsigned)PSS_HOOKS);
}
