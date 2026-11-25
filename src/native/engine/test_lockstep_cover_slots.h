/* Every player's cover and ship stance (lockstep_cover_slots.h): the two keys ride the stock record while the module is
 * installed and run X4's two cases for the replayed slot's player (slot 1 canonical, slots 2, 3 and 8 in their scopes),
 * the notice on that slot's own client only; the stance list's order; the four readers answer for slot k's player on
 * client k's UI and are X4's everywhere else; the entries' heads and the pins on the pinned image. Stand-ins for X4. */
typedef struct {void *vt;unsigned char body[0x8a800];} tpcov_player;
typedef struct {void *vt;unsigned char body[0xc00];} tpcov_ship;
static void *tpcov_player_vt[800],*tpcov_ship_vt[800];
static tpcov_player tpcov_players[PILOT_SLOTS+1];
static tpcov_ship tpcov_ships[PILOT_SLOTS+1];
static unsigned char tpcov_universe[0x240],tpcov_factions[3][0x40],tpcov_defaults[0x1480],tpcov_ctrl_defaults[0x800];
static unsigned char *tpcov_stance_list[3],tpcov_stances[3][8];
static unsigned char *tpcov_ability_ship,*tpcov_ability_player,*tpcov_ability_faction;
static unsigned tpcov_notices,tpcov_notice_action,tpcov_notice_state,tpcov_sets,tpcov_stance_sets,tpcov_stance_offs,tpcov_original_calls;
static unsigned char *tpcov_set_ship,*tpcov_set_faction,*tpcov_stance_ship,*tpcov_stance_value,*tpcov_view;
static unsigned char tpcov_coverable_answer,tpcov_stance_can_answer=1;
static unsigned char *tpcov_occupied_ship(void) {   /* the singleton's ship: its slot's stand-in */
    unsigned char *player=pcov_singleton();
    for(unsigned k=1;k<=PILOT_SLOTS;k++) if(player==(unsigned char*)&tpcov_players[k]) return (unsigned char*)&tpcov_ships[k];
    return NULL;
}
static void **tpcov_occupied(void *unused,void **out) {(void)unused;*out=tpcov_occupied_ship();return out;}
static unsigned char *tpcov_ability(void *player,void *ship) {
    return player==tpcov_ability_player && ship==tpcov_ability_ship?tpcov_ability_faction:NULL;
}
static unsigned char *tpcov_defaults_of(void *macro) {(void)macro;return tpcov_defaults;}
static unsigned char *tpcov_ctrl_defaults_of(void *macro) {(void)macro;return tpcov_ctrl_defaults;}
static unsigned char tpcov_stance_can(void *ship) {(void)ship;return tpcov_stance_can_answer;}
static void tpcov_stance_set(void *ship,void *stance) {tpcov_stance_ship=ship;tpcov_stance_value=stance;++tpcov_stance_sets;memcpy((unsigned char*)ship+PCOV_SHIP_STANCE,&stance,8);}
static void tpcov_stance_off(void *ship) {tpcov_stance_ship=ship;++tpcov_stance_offs;memset((unsigned char*)ship+PCOV_SHIP_STANCE,0,8);}
static void tpcov_notice(void *owner,int action,int state,int kind,void *data) {
    assert(owner==(void*)tpcov_universe && kind==0x4d && data==(void*)(image+PCOV_NOTICE_DATA));
    ++tpcov_notices;tpcov_notice_action=(unsigned)action;tpcov_notice_state=(unsigned)state;
}
static unsigned char tpcov_has_cover(void *player) {return pcov_load((unsigned char*)player+PCOV_PLAYER_COVER)!=NULL;}
static unsigned char *tpcov_cover_of(void *player) {return pcov_load((unsigned char*)player+PCOV_PLAYER_COVER);}
static void tpcov_player_set(void *player,void *faction,unsigned char notify) {(void)notify;memcpy((unsigned char*)player+PCOV_PLAYER_COVER,&faction,8);}
/* The ship's setter as X4's pilot rule makes it: the ship and its pilot (the stand-in of the same slot) wear the faction. */
static void tpcov_ship_set(void *ship,void *faction,unsigned char notify) {
    assert(notify==1);tpcov_set_ship=ship;tpcov_set_faction=faction;++tpcov_sets;
    memcpy((unsigned char*)ship+PCOV_OBJECT_COVER,&faction,8);
    for(unsigned k=1;k<=PILOT_SLOTS;k++) if(ship==(void*)&tpcov_ships[k]) memcpy(tpcov_players[k].body-8+PCOV_PLAYER_COVER,&faction,8);
}
static unsigned char tpcov_ship_coverable(void *ship) {(void)ship;return tpcov_coverable_answer;}
static unsigned char tpcov_is_class(void *component,int cls) {
    for(unsigned k=1;k<=PILOT_SLOTS;k++) if(component==(void*)&tpcov_ships[k]) return cls==PCOV_CLASS_SHIP || cls==PCOV_CLASS_CONTROLLABLE;
    return 0;
}
static const char *tpcov_orig_ability(uint64_t id) {(void)id;++tpcov_original_calls;return "x4_ability";}
static unsigned char tpcov_orig_coverable(uint64_t id) {(void)id;++tpcov_original_calls;return 7;}
static unsigned char tpcov_orig_can(void) {++tpcov_original_calls;return 9;}
static const char *tpcov_orig_cover(void) {++tpcov_original_calls;return "x4_cover";}
static unsigned char *tpcov_view_of(void) {return tpcov_view;}
static void tpcov_singleton(unsigned k) {unsigned char *p=k?(unsigned char*)&tpcov_players[k]:NULL;memcpy(tpcov_universe+0x230,&p,8);}
static void tpcov_faction(unsigned i,const char *id) {   /* a faction stand-in: its ID inline at +0x10, capacity 15 */
    memset(tpcov_factions[i],0,sizeof(tpcov_factions[i]));snprintf((char*)tpcov_factions[i]+0x10,16,"%s",id);
    const uint64_t capacity=15;memcpy(tpcov_factions[i]+0x28,&capacity,8);
}
/* A blackboard with one entry: key -> a time value. */
static unsigned char tpcov_node[0x50],tpcov_value[0x10];
static void tpcov_blackboard(unsigned char *player,uint64_t key,int type,double time) {
    unsigned char *header=player+PCOV_BLACKBOARD,*node=tpcov_node;
    memset(header,0,0x30);memset(tpcov_node,0,sizeof(tpcov_node));memset(tpcov_value,0,sizeof(tpcov_value));
    memcpy(header+8,&node,8);memcpy(tpcov_node+0x20,&key,8);
    memcpy(tpcov_value,&type,4);memcpy(tpcov_value+8,&time,8);
    unsigned char *value=tpcov_value;memcpy(tpcov_node+0x48,&value,8);
}
static void test_cover_slots(void) {
    const unsigned scope_was=pstk_scope_slot;unsigned char *const base_was=pcov_base;const pcov_fns fn_was=pcov_fn;
    unsigned char universe_was[8],key_was[8],owner_was[8];
    memcpy(universe_was,image+PCOV_UNIVERSE,8);memcpy(key_was,image+PCOV_COOLDOWN_KEY,8);memcpy(owner_was,image+PCOV_NOTICE_OWNER,8);
    pcov_base=image;
    {unsigned char *u=tpcov_universe;memcpy(image+PCOV_UNIVERSE,&u,8);memcpy(image+PCOV_NOTICE_OWNER,&u,8);}
    {const uint64_t key=0x5151u;memcpy(image+PCOV_COOLDOWN_KEY,&key,8);}
    tpcov_player_vt[PCOV_SLOT_HAS_COVER]=(void*)tpcov_has_cover;tpcov_player_vt[PCOV_SLOT_COVER]=(void*)tpcov_cover_of;
    tpcov_player_vt[PCOV_SLOT_SET_COVER]=(void*)tpcov_player_set;
    tpcov_ship_vt[PCOV_SLOT_SET_COVER]=(void*)tpcov_ship_set;tpcov_ship_vt[PCOV_SLOT_COVERABLE]=(void*)tpcov_ship_coverable;
    tpcov_ship_vt[PCOV_SLOT_IS_CLASS]=(void*)tpcov_is_class;
    for(unsigned k=0;k<=PILOT_SLOTS;k++) {
        memset(&tpcov_players[k],0,sizeof(tpcov_players[k]));memset(&tpcov_ships[k],0,sizeof(tpcov_ships[k]));
        tpcov_players[k].vt=tpcov_player_vt;tpcov_ships[k].vt=tpcov_ship_vt;
    }
    tpcov_faction(0,"yaki");tpcov_faction(1,"argon");tpcov_faction(2,"teladi");
    pcov_fn.occupied=tpcov_occupied;pcov_fn.ability=tpcov_ability;pcov_fn.defaults=tpcov_defaults_of;pcov_fn.stance_can=tpcov_stance_can;
    pcov_fn.stance_set=tpcov_stance_set;pcov_fn.stance_off=tpcov_stance_off;pcov_fn.notice=tpcov_notice;pcov_fn.ctrl_defaults=tpcov_ctrl_defaults_of;
    /* Not installed: the keys are refused as before; installed: they ride the record, for every slot. */
    pcov_installed=0;
    assert(!pcov_key_replayed(PSTK_ACTION,PCOV_KEY_COVER) && pstk_verdict(PSTK_ACTION,PCOV_KEY_COVER)==PSTK_REFUSE && pstk_verdict(PSTK_ACTION,PCOV_KEY_STANCE)==PSTK_REFUSE);
    pcov_installed=1;
    assert(pcov_key_replayed(PSTK_ACTION,PCOV_KEY_COVER) && pcov_key_replayed(PSTK_ACTION,PCOV_KEY_STANCE) && !pcov_key_replayed(PSTK_STATE,PCOV_KEY_COVER));
    for(unsigned k=1;k<=PILOT_SLOTS;k++)
        assert(pstk_verdict_slot(PSTK_ACTION,PCOV_KEY_COVER,k)==PSTK_REPLAY && pstk_verdict_slot(PSTK_ACTION,PCOV_KEY_STANCE,k)==PSTK_REPLAY);
    /* The cover key for slots 1, 2, 3 and 8, seen from three clients: the slot's own, slot 1's, another one. */
    const unsigned slots[4]={1,2,3,8};
    for(unsigned j=0;j<4;j++) {
        const unsigned k=slots[j],clients[3]={k,1,k==8?2:8};
        for(unsigned c=0;c<3;c++) {
            const unsigned own=clients[c];pcov_own_slot_test=own;pstk_scope_slot=k==1?0:k;tpcov_singleton(k);
            unsigned char *const player=(unsigned char*)&tpcov_players[k],*const ship=(unsigned char*)&tpcov_ships[k];
            memset(player+PCOV_PLAYER_COVER,0,8);memset(ship+PCOV_OBJECT_COVER,0,8);memset(player+PCOV_PLAYER_LAST,0,8);
            /* the ship's cover ability (a story's): on, then off; the notice on the slot's own client only */
            tpcov_ability_player=player;tpcov_ability_ship=ship;tpcov_ability_faction=tpcov_factions[0];
            tpcov_notices=tpcov_sets=0;pcov_cover_key(k,player);
            assert(tpcov_sets==1 && tpcov_set_ship==ship && tpcov_set_faction==tpcov_factions[0] && pcov_load(player+PCOV_PLAYER_COVER)==tpcov_factions[0]);
            assert(tpcov_notices==(own==k) && pcov_last_state[k]==2 && (own!=k || (tpcov_notice_action==PCOV_KEY_COVER && tpcov_notice_state==2)));
            tpcov_notices=0;pcov_cover_key(k,player);
            assert(tpcov_sets==2 && !tpcov_set_faction && !pcov_load(player+PCOV_PLAYER_COVER) && pcov_last_state[k]==0x14 && tpcov_notices==(own==k));
            /* another player's ability is not this one's: no ability, the ship not coverable: nothing, no notice */
            tpcov_ability_player=(unsigned char*)&tpcov_players[k==2?3:2];tpcov_coverable_answer=0;tpcov_notices=0;
            {const LONG64 none=pcov_none;pcov_cover_key(k,player);assert(tpcov_sets==2 && !tpcov_notices && pcov_none==none+1);}
            /* a ship that may take any cover: the last chosen one (none yet: "unavailable", the cover stays off) */
            tpcov_coverable_answer=1;pcov_cover_key(k,player);
            assert(tpcov_sets==3 && !tpcov_set_faction && pcov_last_state[k]==0x22);
            {unsigned char *f=tpcov_factions[1];memcpy(player+PCOV_PLAYER_LAST,&f,8);}
            pcov_cover_key(k,player);assert(tpcov_sets==4 && tpcov_set_faction==tpcov_factions[1] && pcov_last_state[k]==2);
            pcov_cover_key(k,player);assert(tpcov_sets==5 && !tpcov_set_faction && pcov_last_state[k]==0x14);
            assert(tpcov_notices==(own==k?3u:0u));
        }
    }
    /* The stance key: the list in order, then none when the defaults' word +0x1468 is 0, else the first again. */
    {unsigned char *const ship=(unsigned char*)&tpcov_ships[2];pcov_own_slot_test=2;pstk_scope_slot=2;tpcov_singleton(2);
     for(unsigned i=0;i<3;i++) tpcov_stance_list[i]=tpcov_stances[i];
     unsigned char **begin=tpcov_stance_list,**end=tpcov_stance_list+3;
     memcpy(tpcov_defaults+PCOV_DEF_STANCES,&begin,8);memcpy(tpcov_defaults+PCOV_DEF_STANCES+8,&end,8);
     memset(tpcov_defaults+PCOV_DEF_STANCE_NONE,0,8);
     memset(ship+PCOV_SHIP_STANCE,0,8);tpcov_stance_sets=tpcov_stance_offs=tpcov_notices=0;
     for(unsigned i=0;i<3;i++) {pcov_stance_key(2);assert(tpcov_stance_sets==i+1 && tpcov_stance_value==tpcov_stances[i] && tpcov_stance_ship==ship);}
     pcov_stance_key(2);assert(tpcov_stance_offs==1 && !pcov_load(ship+PCOV_SHIP_STANCE) && tpcov_notices==4 && tpcov_notice_action==PCOV_KEY_STANCE && tpcov_notice_state==2);
     {const uint64_t wrap=1;memcpy(tpcov_defaults+PCOV_DEF_STANCE_NONE,&wrap,8);}
     {unsigned char *last=tpcov_stances[2];memcpy(ship+PCOV_SHIP_STANCE,&last,8);}
     pcov_stance_key(2);assert(tpcov_stance_sets==4 && tpcov_stance_value==tpcov_stances[0]);
     /* the stance's cooldown, or an empty list: nothing; slot 3's client sees no notice of slot 2's key */
     tpcov_stance_can_answer=0;pcov_stance_key(2);assert(tpcov_stance_sets==4 && tpcov_notices==5);tpcov_stance_can_answer=1;
     pcov_own_slot_test=3;pcov_stance_key(2);assert(tpcov_stance_sets==5 && tpcov_notices==5);
     end=begin;memcpy(tpcov_defaults+PCOV_DEF_STANCES+8,&end,8);pcov_stance_key(2);assert(tpcov_stance_sets==5);}
    /* The replay walk: X4's player handler restated, any other handler of the id called as it is. */
    {static pnat_node nodes[2];static unsigned char header_bytes[sizeof(pnat_node)];pnat_node *header=(pnat_node*)header_bytes;
     memset(header,0,sizeof(*header));memset(nodes,0,sizeof(nodes));
     nodes[0].key=PCOV_KEY_COVER;nodes[0].handler=(void*)(image+PCOV_HANDLER);nodes[0].parent=header;header->left=&nodes[0];
     unsigned char *const player=(unsigned char*)&tpcov_players[3],*const ship=(unsigned char*)&tpcov_ships[3];
     pstk_scope_slot=3;pcov_own_slot_test=3;tpcov_singleton(3);memset(player+PCOV_PLAYER_COVER,0,8);
     tpcov_ability_player=player;tpcov_ability_ship=ship;tpcov_ability_faction=tpcov_factions[2];
     const LONG64 keys=pcov_keys[0],slot3=pcov_key_slots[3];tpcov_sets=0;
     pcov_key_walk(header_bytes,PSTK_ACTION,PCOV_KEY_COVER,0);
     assert(tpcov_sets==1 && tpcov_set_faction==tpcov_factions[2] && pcov_keys[0]==keys+1 && pcov_key_slots[3]==slot3+1);
     tpcov_singleton(0);{const LONG64 none=pcov_none;pcov_key_walk(header_bytes,PSTK_ACTION,PCOV_KEY_COVER,0);assert(pcov_none==none+1 && tpcov_sets==1);}}
    /* The readers: slot k's player on its own client's UI, X4's function otherwise. */
    {for(unsigned i=0;i<PCOV_READS;i++) pcov_original[i]=NULL;
     pcov_original[PCOV_R_ABILITY]=(void*)tpcov_orig_ability;pcov_original[PCOV_R_COVERABLE]=(void*)tpcov_orig_coverable;
     pcov_original[PCOV_R_CAN]=(void*)tpcov_orig_can;pcov_original[PCOV_R_COVER]=(void*)tpcov_orig_cover;
     pcov_view_test=tpcov_view_of;tpcov_original_calls=0;
     for(unsigned j=1;j<4;j++) {
         const unsigned k=slots[j];unsigned char *const player=(unsigned char*)&tpcov_players[k];
         tpcov_view=NULL;
         assert(!strcmp(pcov_ui_cover(),"x4_cover") && pcov_ui_can()==9 && !strcmp(pcov_ui_ability(1),"x4_ability") && pcov_ui_coverable(1)==7);
         assert(tpcov_original_calls==4u*j);
         tpcov_view=player;
         {unsigned char *f=tpcov_factions[1];memcpy(player+PCOV_PLAYER_COVER,&f,8);}
         assert(!strcmp(pcov_ui_cover(),"argon"));
         memset(player+PCOV_PLAYER_COVER,0,8);assert(!strcmp(pcov_ui_cover(),""));
         /* the cooldown on the slot's blackboard: not over until its time; another key or type: no cooldown */
         tpcov_blackboard(player,0x5151u,8,30.0);assert(pcov_can_for(player,10.0)==0 && pcov_can_for(player,30.0)==1 && pcov_can_for(player,31.0)==1);
         tpcov_blackboard(player,0x5150u,8,30.0);assert(pcov_can_for(player,10.0)==1);
         tpcov_blackboard(player,0x5152u,8,30.0);assert(pcov_can_for(player,10.0)==1);
         tpcov_blackboard(player,0x5151u,3,30.0);assert(pcov_can_for(player,10.0)==1);
         tpcov_blackboard(player,0x5151u,8,-1.0);assert(pcov_can_for(player,-5.0)==1);
         memset(player+PCOV_BLACKBOARD,0,0x30);assert(pcov_can_for(player,1.0)==1);
         /* the ability and coverable answers for P_k: its own table, the defaults' flag */
         tpcov_ability_player=player;tpcov_ability_ship=(unsigned char*)&tpcov_ships[k];tpcov_ability_faction=tpcov_factions[0];
         memset(tpcov_ctrl_defaults,0,sizeof(tpcov_ctrl_defaults));
         assert(pcov_coverable_for(player,(unsigned char*)&tpcov_ships[k])==1 && pcov_coverable_for(player,(unsigned char*)&tpcov_ships[1])==0);
         tpcov_ctrl_defaults[PCOV_CTRL_ALLOWED]=1;assert(pcov_coverable_for(player,(unsigned char*)&tpcov_ships[1])==1);
         assert(!strcmp(pcov_faction_id(pcov_fn.ability(player,(unsigned char*)&tpcov_ships[k])),"yaki") && !strcmp(pcov_faction_id(NULL),""));
     }
     pcov_view_test=NULL;tpcov_view=NULL;}
    /* A long faction ID lives behind its pointer. */
    {static char longid[40]="a_faction_with_a_long_identifier";unsigned char f[0x40];memset(f,0,sizeof(f));
     char *p=longid;memcpy(f+0x10,&p,8);const uint64_t capacity=40;memcpy(f+0x28,&capacity,8);assert(pcov_faction_id(f)==longid);}
    char status[512];assert(pcov_status(status,sizeof(status))>0 && strstr(status,"installed=1"));
    pcov_installed=0;pcov_own_slot_test=0;pstk_scope_slot=scope_was;pcov_base=base_was;pcov_fn=fn_was;
    memcpy(image+PCOV_UNIVERSE,universe_was,8);memcpy(image+PCOV_COOLDOWN_KEY,key_was,8);memcpy(image+PCOV_NOTICE_OWNER,owner_was,8);
    memset(pcov_last_state,0,sizeof(pcov_last_state));
    puts("cover slots tests (keys for slots 1, 2, 3 and 8, the notice per client, stances, the readers) PASS");
}
/* The pinned image: the pins, the readers' heads (whole instructions, the one displacement), the two cases' calls. */
static void test_cover_slots_image(const char *path) {
    FILE *f=fopen(path,"rb");assert(f);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;IMAGE_SECTION_HEADER sections[32];
    assert(fread(&dos,1,sizeof(dos),f)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && !fseek(f,dos.e_lfanew,SEEK_SET));
    assert(fread(&nt,1,sizeof(nt),f)==sizeof(nt) && nt.FileHeader.TimeDateStamp==0x6a1eaded && nt.FileHeader.NumberOfSections<=32);
    unsigned count=nt.FileHeader.NumberOfSections;
    assert(fread(sections,sizeof(*sections),count,f)==count);
    static unsigned char bytes[0xc00];
    for(unsigned i=0;i<PCOV_PIN_COUNT;i++)
        assert(pcov_pins[i].size<=sizeof(bytes) && test_pnat_file_bytes(f,sections,count,pcov_pins[i].rva,pcov_pins[i].size,bytes) &&
               hash_ok(bytes,pcov_pins[i].size,pcov_pins[i].sha));
    for(unsigned i=0;i<PCOV_READS;i++) {
        assert(test_pnat_file_bytes(f,sections,count,pcov_reads[i].rva,pcov_reads[i].size,bytes) && hash_ok(bytes,pcov_reads[i].size,pcov_reads[i].sha));
        assert(!memcmp(bytes,pcov_reads[i].head,pcov_reads[i].length) && pcov_reads[i].length>=5 && pcov_reads[i].length+14u<=0x20u);
        assert(!(pcov_reads[i].rva&15u));   /* the five-byte jump inside one 16-byte block */
    }
    /* CanPlayerActivateCoverAbility's moved displacement is maybe_g_Universe's (0x3d198f8) */
    {int32_t d=0;memcpy(&d,pcov_reads[PCOV_R_CAN].head+7,4);assert(pcov_reads[PCOV_R_CAN].rva+11u+(uint32_t)d==PCOV_UNIVERSE);}
    /* the cases' calls: the occupied ship, the ability, the notice (twice for the cover, once for the stance), the stances */
    static const struct {unsigned rva,target;} calls[]={{0x71c2a5u,PCOV_OCCUPIED},{0x71c2bdu,PCOV_ABILITY},{0x71c30au,PCOV_NOTICE},
        {0x71c3a1u,PCOV_NOTICE},{0x71c3ecu,PCOV_OCCUPIED},{0x71c402u,PCOV_DEFAULTS},{0x71c41eu,PCOV_STANCE_CAN},{0x71c4aeu,PCOV_NOTICE},
        {0x71c4beu,PCOV_STANCE_SET},{0x71c4c5u,PCOV_STANCE_OFF}};
    for(unsigned i=0;i<sizeof(calls)/sizeof(*calls);i++) {
        assert(test_pnat_file_bytes(f,sections,count,calls[i].rva,5,bytes) && bytes[0]==0xe8);
        int32_t r;memcpy(&r,bytes+1,4);assert(calls[i].rva+5u+(uint32_t)r==calls[i].target);
    }
    /* the player's last cover faction (+0x8a780) and the ship's stance (+0xbe8), as the cases read them */
    assert(test_pnat_file_bytes(f,sections,count,0x71c37bu,8,bytes) && !memcmp(bytes,"\x48\x83\xbe\x80\xa7\x08\x00\x00",8));
    assert(test_pnat_file_bytes(f,sections,count,0x71c42du,7,bytes) && !memcmp(bytes,"\x48\x8b\x8b\xe8\x0b\x00\x00",7));
    fclose(f);
    printf("cover slots image: %u pins, the four readers' heads, the cases' %u calls PASS\n",(unsigned)PCOV_PIN_COUNT,(unsigned)(sizeof(calls)/sizeof(*calls)));
}
