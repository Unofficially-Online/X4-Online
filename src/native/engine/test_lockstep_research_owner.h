/* lockstep_research_owner.h: which library stands where in each scope, whose player a production module's calls get,
 * the receipt (synthetic: the players are byte blocks with the two vectors at the pinned offsets, the factions, the
 * scope, the add, the post and the question are stand-ins; nothing here shows what X4's research code does with a list). */
static unsigned char test_rsch_players[4][0x7400];
static unsigned char test_rsch_wares[3][0x100];
static void *test_rsch_factions[4];
static void *test_rsch_now;
static unsigned test_rsch_adds,test_rsch_scope_ok,test_rsch_scope_in,test_rsch_scopes,test_rsch_added_in;
static void *test_rsch_posted,*test_rsch_questioned;
static unsigned char *test_rsch_player(unsigned k) {return k>=1 && k<=3?test_rsch_players[k]:NULL;}
static void *test_rsch_faction(unsigned k) {return k>=1 && k<=3?test_rsch_factions[k]:NULL;}
static void *test_rsch_canonical(void) {return test_rsch_now;}
static int test_rsch_scope_enter(unsigned k) {if(!test_rsch_scope_ok) return 0;test_rsch_scope_in=k;++test_rsch_scopes;return 1;}
static void test_rsch_scope_leave(unsigned k) {assert(test_rsch_scope_in==k);test_rsch_scope_in=0;}
static void test_rsch_ware(unsigned i,const char *id) {
    memset(test_rsch_wares[i],0,sizeof(test_rsch_wares[i]));
    const uint64_t size=strlen(id),capacity=15;
    assert(size<=capacity);
    memcpy(test_rsch_wares[i],id,size);memcpy(test_rsch_wares[i]+0x10,&size,8);memcpy(test_rsch_wares[i]+0x18,&capacity,8);
}
/* What X4's add does to the list of the player it is given: one more entry. */
static unsigned char test_rsch_entries[4][8*RSCH_ENTRY];
static void test_rsch_add(void *player,void *ware,double a,double b,double c) {
    (void)a;(void)b;(void)c;
    unsigned char *p=player,*vector[3];
    memcpy(vector,p+RSCH_LIST,sizeof(vector));
    if(!vector[0]) {   /* the first entry of this list: storage of its own */
        static unsigned next;vector[0]=vector[1]=test_rsch_entries[next++%4];vector[2]=vector[0]+8*RSCH_ENTRY;
    }
    assert(vector[1]+RSCH_ENTRY<=vector[2]);
    memset(vector[1],0,RSCH_ENTRY);memcpy(vector[1],&ware,8);vector[1]+=RSCH_ENTRY;
    memcpy(p+RSCH_LIST,vector,sizeof(vector));++test_rsch_adds;test_rsch_added_in=test_rsch_scope_in;
}
static uint64_t test_rsch_post(void *source,void *event,double time,unsigned char immediate) {
    (void)event;(void)time;test_rsch_posted=source;return immediate;
}
static uint64_t test_rsch_knows(void *player,void *key) {(void)key;test_rsch_questioned=player;return 1;}
static unsigned test_rsch_count(unsigned k) {
    unsigned n=0;uint64_t h=0;
    assert(rsch_read(k,&n,&h,NULL,0));
    return n;
}
static unsigned test_rsch_blueprints(unsigned k) {
    unsigned n=0;uint64_t h=0;
    assert(rsch_read_blueprints(k,&n,&h,NULL,0));
    return n;
}
static void test_research_owner(void) {
    unsigned char *(*const player)(unsigned)=rsch_player;void *(*const faction)(unsigned)=rsch_faction;void *(*const canonical)(void)=rsch_canonical;
    int (*const scope_enter)(unsigned)=rsch_scope_enter;void (*const scope_leave)(unsigned)=rsch_scope_leave;
    const DWORD thread=ui_thread;const int armed=rsch_armed;
    static int f_player,f_two;char out[256],names[128];unsigned n=0;uint64_t h=0;
    static unsigned char blueprints[2*RSCH_BLUEPRINT_ENTRY];
    memset(test_rsch_players,0,sizeof(test_rsch_players));memset(test_rsch_entries,0,sizeof(test_rsch_entries));
    rsch_player=test_rsch_player;rsch_faction=test_rsch_faction;rsch_canonical=test_rsch_canonical;ui_thread=GetCurrentThreadId();
    rsch_scope_enter=test_rsch_scope_enter;rsch_scope_leave=test_rsch_scope_leave;
    test_rsch_now=&f_player;test_rsch_factions[1]=&f_player;test_rsch_factions[2]=&f_two;test_rsch_factions[3]=&f_player;
    test_rsch_ware(0,"research_a");test_rsch_ware(1,"research_b");test_rsch_ware(2,"research_c");
    rsch_open=0;rsch_pending=0;memset(rsch_kind,0,sizeof(rsch_kind));
    rsch_exchanges=rsch_nested=rsch_finishes=rsch_credited=rsch_told=rsch_asked=rsch_unscoped=rsch_lost=rsch_offthread=0;
    test_rsch_adds=0;test_rsch_scope_ok=1;test_rsch_scope_in=0;test_rsch_scopes=0;

    /* not armed: no scope exchanges anything, an owner question passes through, no receipt */
    rsch_armed=0;
    rsch_view_enter(2);rsch_slot_enter(3);
    assert(!rsch_open && !rsch_exchanges);
    assert(rsch_finish_owner(&f_two,&f_player)==&f_two && !rsch_pending);
    assert(!rsch_receipt(out,sizeof(out)));

    rsch_armed=1;
    /* X4's own player researched a (no scope: its own list, the player X4 gave) */
    rsch_finish_add(test_rsch_add,test_rsch_players[1],test_rsch_wares[0],1,1,1);
    assert(test_rsch_count(1)==1 && test_rsch_count(2)==0 && test_rsch_count(3)==0 && !rsch_credited && !test_rsch_scopes);
    assert(!rsch_receipt(out,sizeof(out)));   /* no slot from 2 on has anything: no part */

    /* slot 2 has its own faction: its menus (X4's player stands for it) show its own library, blueprints and research;
     * its own scope needs nothing */
    {   unsigned char *vector[3]={blueprints,blueprints+RSCH_BLUEPRINT_ENTRY,blueprints+sizeof(blueprints)};void *ware=test_rsch_wares[1];
        memcpy(blueprints,&ware,8);memcpy(test_rsch_players[2]+RSCH_BLUEPRINTS,vector,sizeof(vector));}
    assert(test_rsch_blueprints(2)==1 && test_rsch_blueprints(1)==0);
    rsch_view_enter(2);
    assert(rsch_open==2 && rsch_kind[2]==RSCH_OWN);
    assert(!memcmp(test_rsch_players[1]+RSCH_LIST,"\0\0\0\0\0\0\0\0",8));       /* P_1 shows the empty research list */
    {   unsigned char *begin=NULL;memcpy(&begin,test_rsch_players[1]+RSCH_BLUEPRINTS,8);assert(begin==blueprints);}   /* and its blueprints */
    rsch_view_enter(3);assert(rsch_open==2);                                    /* slot 3 shares: nothing to open, nothing nested */
    rsch_view_leave(3);assert(rsch_open==2);                                    /* another slot's close leaves it */
    rsch_view_leave(2);
    assert(!rsch_open && test_rsch_count(1)==1 && test_rsch_count(2)==0 && test_rsch_blueprints(2)==1 && test_rsch_blueprints(1)==0);
    rsch_slot_enter(2);assert(!rsch_open);rsch_slot_leave(2);

    /* slot 3 is of X4's player faction: in its own scope P_3 shows the shared library, and what is added there is the group's */
    rsch_slot_enter(3);
    assert(rsch_open==3 && rsch_kind[3]==RSCH_SHARED);
    rsch_finish_add(test_rsch_add,test_rsch_players[3],test_rsch_wares[1],1,1,1);   /* MD's add_research in slot 3's scope */
    rsch_view_enter(2);assert(rsch_open==3 && rsch_nested==1);                      /* a second exchange is refused and counted */
    rsch_slot_leave(3);
    assert(!rsch_open && test_rsch_count(1)==2 && test_rsch_count(3)==0);

    /* a module of slot 2's faction: X4 is told "the player's", and each call that follows gets slot 2's player */
    assert(rsch_finish_owner(&f_two,&f_player)==&f_player && rsch_pending==2);
    assert(rsch_use_post(test_rsch_post,test_rsch_players[1],NULL,1.0,0)==0 && test_rsch_posted==test_rsch_players[2] && !rsch_pending && rsch_told==1);
    assert(rsch_finish_owner(&f_two,&f_player)==&f_player);
    assert(rsch_use_knows(test_rsch_knows,test_rsch_players[1],NULL)==1 && test_rsch_questioned==test_rsch_players[2] && rsch_asked==1);
    assert(rsch_finish_owner(&f_two,&f_player)==&f_player && rsch_pending==2);
    rsch_finish_add(test_rsch_add,test_rsch_players[1],test_rsch_wares[2],1,1,1);   /* the add: on P_2, inside slot 2's scope */
    assert(!rsch_pending && !rsch_open && rsch_credited==1 && rsch_last_slot==2 && !strcmp(rsch_last_ware,"research_c"));
    assert(test_rsch_count(1)==2 && test_rsch_count(2)==1 && test_rsch_scopes==1 && test_rsch_added_in==2 && !test_rsch_scope_in && !rsch_unscoped);
    assert(rsch_read(2,&n,&h,names,sizeof(names)) && !strcmp(names,"research_c"));
    assert(rsch_read(1,&n,&h,names,sizeof(names)) && !strcmp(names,"research_a,research_b"));
    /* whatever player the universe has at that moment (another slot's scope): still slot 2's; no scope to open: added all the same */
    test_rsch_scope_ok=0;
    assert(rsch_finish_owner(&f_two,&f_player)==&f_player);
    rsch_finish_add(test_rsch_add,test_rsch_players[3],test_rsch_wares[0],1,1,1);
    assert(test_rsch_count(2)==2 && test_rsch_count(3)==0 && test_rsch_count(1)==2 && rsch_unscoped==1 && rsch_credited==2);
    test_rsch_scope_ok=1;
    /* inside slot 2's own replayed call (X4's player stands for it, g_PlayerFaction is its faction, P_1 shows its
     * library): the question reads P_1, the event is P_2's, and the add puts the library back for its length */
    rsch_view_enter(2);test_rsch_now=&f_player;
    assert(rsch_open==2);
    assert(rsch_finish_owner(&f_two,&f_two)==&f_two && rsch_pending==2);
    assert(rsch_use_knows(test_rsch_knows,test_rsch_players[1],NULL)==1 && test_rsch_questioned==test_rsch_players[1]);
    assert(rsch_finish_owner(&f_two,&f_two)==&f_two);
    (void)rsch_use_post(test_rsch_post,test_rsch_players[1],NULL,1.0,1);assert(test_rsch_posted==test_rsch_players[2]);
    assert(rsch_finish_owner(&f_two,&f_two)==&f_two);
    {   const unsigned scopes=test_rsch_scopes;
        rsch_finish_add(test_rsch_add,test_rsch_players[1],test_rsch_wares[1],1,1,1);
        assert(rsch_open==2 && test_rsch_scopes==scopes && rsch_credited==3 && !rsch_lost);}   /* no scope inside a call's scope */
    rsch_view_leave(2);
    assert(!rsch_open && test_rsch_count(2)==3 && test_rsch_count(1)==2);
    /* X4's player's own module (coop: slot 3's too), and a faction that is no slot's: as X4 has it */
    assert(rsch_finish_owner(&f_player,&f_player)==&f_player && !rsch_pending);
    (void)rsch_use_post(test_rsch_post,test_rsch_players[1],NULL,1.0,0);assert(test_rsch_posted==test_rsch_players[1]);
    {   static int f_other;assert(rsch_finish_owner(&f_other,&f_player)==&f_other && !rsch_pending);}
    /* the notices about a library of a slot's own faction stay with that slot; a shared library's and other events do not */
    {   const int ok=rsch_notices_ok;const LONG64 before=rsch_private;
        rsch_notices_ok=1;
        assert(rsch_event_private(2,0x164u) && rsch_event_private(2,0x175u) && rsch_event_private(2,0x172u) &&
               rsch_event_private(2,0x171u) && rsch_event_private(2,0x151u) && rsch_private==before+5);
        assert(!rsch_event_private(3,0x164u) && !rsch_event_private(2,0x163u) && !rsch_event_private(1,0x164u) && rsch_private==before+5);
        rsch_notices_ok=0;assert(!rsch_event_private(2,0x164u));
        {   /* the getters as X4 has them, then one that reads otherwise */
            static unsigned char *page;if(!page) page=calloc(1,0x9745d8u);assert(page);
            for(unsigned i=0;i<RSCH_NOTICES;i++) {
                const unsigned t=rsch_notices[i].type;unsigned char *at=page+rsch_notices[i].getter;
                at[0]=0xb8;at[1]=(unsigned char)t;at[2]=(unsigned char)(t>>8);at[3]=at[4]=0;at[5]=0xc3;
            }
            assert(rsch_notices_check(page) && !rsch_notices_check(NULL));
            page[0x974030u+1]^=1;assert(!rsch_notices_check(page));page[0x974030u+1]^=1;
        }
        rsch_notices_ok=ok;rsch_private=before;}
    /* off the frame thread the owner is answered as X4 has it */
    ui_thread=GetCurrentThreadId()+1;
    assert(rsch_finish_owner(&f_two,&f_player)==&f_two && !rsch_pending && rsch_offthread==1);
    ui_thread=GetCurrentThreadId();

    /* the receipt: every player's counts and hashes once a slot from 2 on has anything; the same lists give the same text */
    {   char again[256];const size_t a=rsch_receipt(out,sizeof(out)),b=rsch_receipt(again,sizeof(again));
        assert(a && a==b && !strcmp(out,again) && !strncmp(out,"RSCH_2:1=2.",11) && strstr(out,";2=3.") && strstr(out,";3=0."));
        const char *two=strstr(out,";2=3.");assert(two && strstr(two,"/1.") && strstr(out,"/0."));}
    rsch_view_enter(2);assert(!rsch_receipt(out,sizeof(out)) || strstr(out,"unreadable"));rsch_view_leave(2);   /* never read while exchanged */
    assert(rsch_status(out,sizeof(out)) && strstr(out,"research=armed") && strstr(out,"kinds=os") && strstr(out,"credited=3") &&
           strstr(out," private=") &&
           strstr(out,"told=2") && strstr(out,"asked=2") && strstr(out,"last=2:research_b"));

    rsch_armed=armed;rsch_open=0;rsch_pending=0;memset(rsch_kind,0,sizeof(rsch_kind));
    rsch_player=player;rsch_faction=faction;rsch_canonical=canonical;ui_thread=thread;
    rsch_scope_enter=scope_enter;rsch_scope_leave=scope_leave;
}
