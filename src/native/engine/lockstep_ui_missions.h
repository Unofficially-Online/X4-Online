/* Missions and conversations of slot 1 on every client, DEFAULT OFF (native_gate_ui_replay families 'missions' and
 * 'conversation'; they need native_gate_ui_guard with the groups of the same names; stop reason 164 like the replay).
 * Report: reports/LOCKSTEP-UI-MISSIONS-20261101.txt (model (a) of its decision section: slot 1's missions and
 * conversations replayed, slots 2+ refused until MD has a player per slot, model (c)).
 *
 *   missions      AbortMission, SetActiveMission, MissionBriefingInteractButtonClicked, StartBriefing, StopBriefing: FFI
 *                 exports the UI guard hooks (group missions). Each reaches MD at once on the mission's or offer's cue or
 *                 sets the mission manager's active mission [F, pinned 9.00: 0x175860 -> Cue_RaiseCueEvent 0x63b
 *                 event_mission_aborted; 0x1bbcb0 -> 0x63e event_briefing_started (0x11228d0) or 0x641 submission
 *                 selected; 0x1bc9e0 -> 0x63f (0x1122990) or 0x642; 0x1b49c0 -> 0x640 interact button; 0x1b7010 ->
 *                 0x113b760 / 0x113b920 on the manager]. On slot 1's client the guard's hook captures the call (op 0x48:
 *                 which, the cue as a reference, the briefing's texture and flag) instead of refusing it, and answers
 *                 what the menu expects (SetActiveMission: true). Every client calls the export at capture + delay from
 *                 the probe's frame, in call order with the other Lua-run events (the replay's bypass: the guard passes).
 *                 Slots 2+: refused as before, by the guard (MD acts on player.entity, slot 1's).
 *   conversation  StartConversationFromMenu, StartSubConversationFromMenu, ProceedFromMenu, ReturnFromMenu,
 *                 CancelConversation, UnsuspendConversation, SelectDialogOption, SelectBackOption: Lua C globals the Lua
 *                 core wraps by name (op 0x49: the global's number, then the core's value codec). The four without
 *                 component arguments are captured here too when a script reaches the C function past the wrapper (the
 *                 core UI's dialog menu, a function value held before the arming): the same event. Applied in the probe's
 *                 frame through the stock function on every client. The conversation manager is one per process, updated
 *                 in U::UniverseFrameUpdate (0x9fdef0 -> 0x1177d60) at game time [F]; StartConversationFromMenu queues a
 *                 request (+0x1c0) only while it is idle (+0x1a0 == 1, set by X4::GameClient::Init) [F].
 *   cue reference a mission or offer ID is an MD cue's ID in the universe ID map [F: AbortMission and StartBriefing look it up
 *                 with 0xced70(map, id, 0) and cast to MD::Cue, vtable 0x2c75170 at offset 0]. It travels as the ID and a
 *                 32-bit check of the ID and the cue's script module name (cue +0x160 definition, +0x150 module, +0x18
 *                 name); an ID naming no cue on a client is gone there (the same everywhere when the worlds agree), a check
 *                 that differs stops (ui_ref_mismatch). The Lua core uses it for SignalObject's offer argument too.
 *   receipt       '|UIMIS_1:' with missions the manager's offer count, with conversation the conversation manager's state
 *                 and whether it holds a conversation: a capture or an apply that took on one client only differs there.
 *                 Mission count and active mission are rows only (UI_MISSIONS_STATE): native_gate_guidance_local's
 *                 own guidance is a mission on one client by design (reports/LOCKSTEP-UI-REPLAY-20261030.txt 14).
 * Nothing here runs without the families: the arming claims the entries in the guard (uiguard_claim, dispatch by entry,
 * docs/LOCKSTEP-REGISTRY.txt section 3); the guard's hook then asks uimis_guard first, which answers 0 (refuse as before)
 * for every call it does not capture. */
#define UIMIS_OP_MISSION UIREP_OP_MISSION
#define UIMIS_OP_CONVERSATION UIREP_OP_CONVERSATION
#define UIMIS_FAMILIES (UIREP_FAMILY_MISSIONS|UIREP_FAMILY_CONVERSATION)
#define UIMIS_REF 12u
#define UIMIS_TEXTURE_MAX 48u
#define UIMIS_MISSION_MANAGER 0x6cf1658u      /* pointer: X4::GameClient's mission manager (0x3d0 bytes, ctor 0x1137710) */
#define UIMIS_OFFERS 0x48u                    /* +0x48..+0x50 offers of 0x350 bytes, +0x60..+0x68 missions of 0x5d0, ID at +0 */
#define UIMIS_OFFER_SIZE 0x350u
#define UIMIS_MISSIONS 0x60u
#define UIMIS_MISSION_SIZE 0x5d0u
#define UIMIS_CONVERSATION_MANAGER 0x38ee230u /* pointer: X4::GameClient's conversation manager */
#define UIMIS_CONV_STATE 0x1a0u
#define UIMIS_CONV_CURRENT 0x80u
#define UIMIS_CUE_DEFINITION 0x160u
#define UIMIS_DEFINITION_MODULE 0x150u
#define UIMIS_MODULE_NAME 0x18u
#define UIMIS_RING 32u
enum {UIMIS_ABORT=1,UIMIS_ACTIVE,UIMIS_INTERACT,UIMIS_BRIEFING_START,UIMIS_BRIEFING_STOP,UIMIS_KINDS};
static const struct {const char *name;unsigned rva;} uimis_exports[UIMIS_KINDS]={{"",0},{"AbortMission",0x175860u},
    {"SetActiveMission",0x1b7010u},{"MissionBriefingInteractButtonClicked",0x1b49c0u},{"StartBriefing",0x1bbcb0u},{"StopBriefing",0x1bc9e0u}};
/* The conversation globals in the Lua core's order (native_gate_ui_replay_core.lua K.CONVERSATION): the wire's numbers. */
enum {UIMIS_C_START=1,UIMIS_C_SUB,UIMIS_C_PROCEED,UIMIS_C_RETURN,UIMIS_C_CANCEL,UIMIS_C_UNSUSPEND,UIMIS_C_SELECT,UIMIS_C_BACK,UIMIS_CONV_KINDS};
static const char *const uimis_conv_names[UIMIS_CONV_KINDS]={"","StartConversationFromMenu","StartSubConversationFromMenu","ProceedFromMenu",
    "ReturnFromMenu","CancelConversation","UnsuspendConversation","SelectDialogOption","SelectBackOption"};
#define UIMIS_CONV_GLOBALS (UIMIS_CONV_KINDS-1u)
/* Hiring (the Lua core's hire scope around MapMenu.buttonHire, native_gate_ui_replay_core.lua K.HIRE_MONEY): the NPC's fee,
 * TransferPlayerMoneyTo to the NPC's station, as an event of this family in call order with ProceedFromMenu('g_hire', ...).
 * A wire number of the op only: no guard entry is claimed for it (TransferPlayerMoneyTo is the trade family's entry). */
#define UIMIS_C_HIRE_MONEY 16u
typedef uint64_t (*uimis_call_fn)(uint64_t,uint64_t,uint64_t);
typedef struct {uint64_t tick,id;unsigned char op,kind;} uimis_row;
static uimis_call_fn uimis_fn[UIMIS_KINDS];          /* the exports: the image's, or a test's stand-ins */
static double (*uimis_lua_number)(void *L,int index); /* lua_tonumber, set by gate.ui_missions */
static int uimis_armed;
static const char *uimis_why="-";
static uimis_row uimis_rows[UIMIS_RING];
static unsigned uimis_row_head,uimis_row_tail;
static volatile LONG64 uimis_captured[UIMIS_KINDS],uimis_conv_captured[UIMIS_CONV_KINDS],uimis_applied,uimis_gone,uimis_unmapped,uimis_rows_lost;

/* ---- cue references ---- */
static int uimis_read(const void *from,void *to,size_t n) {return read_local(from,to,n);}
static unsigned char *uimis_cue(uint64_t id) {
    void *map=NULL;uintptr_t vt=0;unsigned char *cue;
    if(!id || !uirep_fn.find || !read_local(pnat_base+UIREP_MAP,&map,8)) return NULL;
    cue=(unsigned char*)uirep_fn.find(map,id,0);
    return cue && read_local(cue,&vt,8) && vt==(uintptr_t)(image+SCRIPT_RNG_CUE_VT)?cue:NULL;
}
/* The ID with its script module's name: 0 when the module cannot be read (never a guessed check). */
static uint32_t uimis_cue_check(const unsigned char *cue,uint64_t id) {
    uintptr_t definition=0,module=0;char name[96];
    if(!cue || !read_local(cue+UIMIS_CUE_DEFINITION,&definition,8) || !definition || !read_local((void*)(definition+UIMIS_DEFINITION_MODULE),&module,8) ||
       !module || !npc_string_with(uimis_read,module+UIMIS_MODULE_NAME,name,sizeof(name))) return 0;
    uint64_t h=14695981039346656037ULL;
    for(size_t i=0;name[i];i++) h=(h^(unsigned char)name[i])*1099511628211ULL;
    return (uint32_t)lockstep_mix(h^id)|1u;
}
static int uimis_put_cue(unsigned char out[UIMIS_REF],uint64_t id) {
    uint32_t check=uimis_cue_check(uimis_cue(id),id);
    if(!check) return 0;
    for(unsigned i=0;i<8;i++) out[i]=(unsigned char)(id>>(8*i));
    for(unsigned i=0;i<4;i++) out[8+i]=(unsigned char)(check>>(8*i));
    return 1;
}
/* 1 = this client names the same cue, 0 = gone here, -1 = another cue under that ID: stop. */
static int uimis_resolve(const unsigned char in[UIMIS_REF],uint64_t *id) {
    uint32_t check=0;*id=0;
    for(unsigned i=0;i<8;i++) *id|=(uint64_t)in[i]<<(8*i);
    for(unsigned i=0;i<4;i++) check|=(uint32_t)in[8+i]<<(8*i);
    unsigned char *cue=uimis_cue(*id);
    if(!cue) return 0;
    return uimis_cue_check(cue,*id)==check?1:-1;
}

/* ---- the managers, read only ---- */
static unsigned char *uimis_manager(unsigned rva) {unsigned char *m=NULL;return read_local(pnat_base+rva,&m,8)?m:NULL;}
static long uimis_count(unsigned char *m,unsigned at,unsigned size) {
    uintptr_t b[2]={0,0};
    if(!m || !read_local(m+at,b,16) || b[1]<b[0] || (b[1]-b[0])%size) return -1;
    return (long)((b[1]-b[0])/size);
}
static int uimis_conv_state(int *state,int *current) {
    unsigned char *m=uimis_manager(UIMIS_CONVERSATION_MANAGER);uintptr_t c=0;*state=-1;*current=0;
    if(!m || !read_local(m+UIMIS_CONV_STATE,state,4) || !read_local(m+UIMIS_CONV_CURRENT,&c,8)) return 0;
    *current=c!=0;return 1;
}

/* ---- capture: asked by the UI guard's hook (lockstep_ui_guard.h uiguard_decide) for every call it would refuse ---- */
static void uimis_note(unsigned op,unsigned kind,uint64_t id) {
    if(uimis_row_head-uimis_row_tail>=UIMIS_RING) {InterlockedIncrement64(&uimis_rows_lost);return;}
    uimis_row *r=&uimis_rows[uimis_row_head%UIMIS_RING];r->tick=stats.issued;r->id=id;r->op=(unsigned char)op;r->kind=(unsigned char)kind;++uimis_row_head;
}
static unsigned uimis_kind_of(const char *name) {
    for(unsigned k=1;k<UIMIS_KINDS;k++) if(!strcmp(name,uimis_exports[k].name)) return k;
    return 0;
}
static unsigned uimis_conv_kind_of(const char *name) {
    for(unsigned k=UIMIS_C_CANCEL;k<UIMIS_CONV_KINDS;k++) if(!strcmp(name,uimis_conv_names[k])) return k;   /* the four without components */
    return 0;
}
/* The guard's argument reader, declared by lockstep_ui_guard.h. */
static uint64_t uiguard_arg(const uint64_t *frame,unsigned i);
/* 1 = captured (X4's function does not run here, *ret is its answer), 0 = not ours: the guard refuses as before. */
static int uimis_guard(const char *name,int lua,const uint64_t *frame,uint64_t *ret) {
    unsigned char data[UIREP_EVENT_MAX];unsigned n=0;
    *ret=0;
    if(!uirep_armed || !uimis_armed || !(uirep_families&UIMIS_FAMILIES) || pstk_capture_slot!=1) return 0;   /* slots 2+: refused */
    if(lua) {
        unsigned k=(uirep_families&UIREP_FAMILY_CONVERSATION)?uimis_conv_kind_of(name):0;
        if(!k) return 0;
        data[n++]=(unsigned char)k;
        if(k==UIMIS_C_SELECT) {   /* one number, the button: the core codec's count 1, tag number, the double's bytes */
            void *L=(void*)(uintptr_t)uiguard_arg(frame,0);
            double button=L && uimis_lua_number?uimis_lua_number(L,1):-1.0;
            if(!(button>=0.0 && button<=255.0) || floor(button)!=button) {InterlockedIncrement64(&uimis_unmapped);uirep_note_refusal(name,"argument");return 0;}
            data[n++]=1;data[n++]=3;memcpy(data+n,&button,8);n+=8;
        } else data[n++]=0;
        if(!uirep_push(UIMIS_OP_CONVERSATION,data,n)) return 0;
        InterlockedIncrement64(&uimis_conv_captured[k]);uimis_note(UIMIS_OP_CONVERSATION,k,0);
        return 1;
    }
    unsigned k=(uirep_families&UIREP_FAMILY_MISSIONS)?uimis_kind_of(name):0;
    if(!k) return 0;
    uint64_t id=uiguard_arg(frame,0);
    data[n++]=(unsigned char)k;
    if(k==UIMIS_ACTIVE && !id) {memset(data+n,0,UIMIS_REF);n+=UIMIS_REF;}   /* SetActiveMission(0): none */
    else if(!uimis_put_cue(data+n,id)) {InterlockedIncrement64(&uimis_unmapped);uirep_note_refusal(name,"unmapped");return 0;}
    else n+=UIMIS_REF;
    if(k==UIMIS_BRIEFING_START) {
        const char *texture=(const char*)(uintptr_t)uiguard_arg(frame,1);size_t length=texture?strnlen(texture,UIMIS_TEXTURE_MAX+1):0;
        if(!texture || length>UIMIS_TEXTURE_MAX) {uirep_note_refusal(name,"texture");return 0;}
        data[n++]=(unsigned char)((uiguard_arg(frame,2)&0xffu)!=0);data[n++]=(unsigned char)length;memcpy(data+n,texture,length);n+=(unsigned)length;
    } else if(k==UIMIS_BRIEFING_STOP) data[n++]=(unsigned char)((uiguard_arg(frame,1)&0xffu)!=0);
    if(!uirep_push(UIMIS_OP_MISSION,data,n)) return 0;
    InterlockedIncrement64(&uimis_captured[k]);uimis_note(UIMIS_OP_MISSION,k,id);
    if(k==UIMIS_ACTIVE) *ret=1;   /* the menus do not read it; the call took */
    return 1;
}

/* The guard's dispatch (uiguard_claim_by) for the entries the families claim. */
static int uimis_claim(unsigned index,const uint64_t *frame,uint64_t *ret) {
    const uiguard_entry *e=&uiguard_tab[index];
    return uimis_guard(e->name,e->kind==UIGUARD_LUA,frame,ret);
}
/* The families' entries in the guard table: the five exports with missions, the four conversation C functions without
 * component arguments with conversation. 0 and uimis_why when another family holds one (then nothing is claimed). */
static int uimis_claim_entries(void) {
    uiguard_unclaim(uimis_claim);
    for(unsigned i=0;i<uiguard_count && i<UIGUARD_MAX;i++) {
        const uiguard_entry *e=&uiguard_tab[i];
        int ours=e->kind==UIGUARD_LUA?((uirep_families&UIREP_FAMILY_CONVERSATION) && uimis_conv_kind_of(e->name)):
                                      ((uirep_families&UIREP_FAMILY_MISSIONS) && uimis_kind_of(e->name));
        if(ours && !uiguard_claim(i,uimis_claim)) {
            static char why[96];snprintf(why,sizeof(why),"ui_missions_%s:%s",uiguard_claim_why,e->name);uimis_why=why;
            uiguard_unclaim(uimis_claim);return 0;
        }
    }
    return 1;
}

/* ---- the section (lockstep_ui_replay.h uirep_event_ok asks for these ops): slot 1, its family, exact lengths ---- */
static unsigned uimis_mission_length(const unsigned char *data,unsigned n) {
    if(!n || !data[0] || data[0]>=UIMIS_KINDS) return 0;
    unsigned at=1u+UIMIS_REF;
    if(data[0]==UIMIS_BRIEFING_START) {if(n<at+2u || data[at]>1 || data[at+1]>UIMIS_TEXTURE_MAX) return 0;at+=2u+data[at+1];}
    else if(data[0]==UIMIS_BRIEFING_STOP) {if(n<at+1u || data[at]>1) return 0;at+=1u;}
    return at;
}
static int uimis_event_ok(unsigned slot,unsigned op,const unsigned char *data,unsigned n,const char **why) {
    if(op==UIMIS_OP_MISSION) {
        if(!(uirep_families&UIREP_FAMILY_MISSIONS) || slot!=1 || uimis_mission_length(data,n)!=n) {*why="ui_missions";return 0;}
        return 1;
    }
    if(op==UIMIS_OP_CONVERSATION) {
        if(!(uirep_families&UIREP_FAMILY_CONVERSATION) || slot!=1 || n<2 || !data[0] || (data[0]>UIMIS_CONV_GLOBALS && data[0]!=UIMIS_C_HIRE_MONEY)) {*why="ui_conversation";return 0;}
        return 1;
    }
    *why="ui_op";return 0;
}

/* ---- apply: one mission event, every client, the probe's frame (Lua: gate.ui_missions_run) ----
 * 1 and X4's answer, 0 and why nothing ran (gone: the same everywhere), -1 = stop (a reference that differs, a decode). */
static int uimis_run(unsigned slot,const unsigned char *data,unsigned n,char *what,size_t capacity) {
    uint64_t id=0;int resolved=1;
    snprintf(what,capacity,"refused");
    if(slot!=1 || !(uirep_families&UIREP_FAMILY_MISSIONS) || uimis_mission_length(data,n)!=n) {snprintf(what,capacity,"decode");return -1;}
    unsigned k=data[0];
    int none=k==UIMIS_ACTIVE && !memcmp(data+1,"\0\0\0\0\0\0\0\0\0\0\0\0",UIMIS_REF);
    if(!none) resolved=uimis_resolve(data+1,&id);
    if(resolved<0) {snprintf(what,capacity,"ref_mismatch");return -1;}
    if(!resolved) {InterlockedIncrement64(&uimis_gone);InterlockedIncrement64(&uirep_stale);snprintf(what,capacity,"%s:gone",uimis_exports[k].name);return 0;}
    if(!uimis_fn[k]) {snprintf(what,capacity,"no_function");return -1;}
    static char texture[UIMIS_TEXTURE_MAX+1];uint64_t a1=0,a2=0,result;
    if(k==UIMIS_BRIEFING_START) {
        unsigned at=1u+UIMIS_REF;a2=data[at];memcpy(texture,data+at+2,data[at+1]);texture[data[at+1]]=0;a1=(uint64_t)(uintptr_t)texture;
    } else if(k==UIMIS_BRIEFING_STOP) a1=data[1u+UIMIS_REF];
    result=uimis_fn[k](id,a1,a2);
    InterlockedIncrement64(&uimis_applied);
    if(k==UIMIS_ACTIVE) snprintf(what,capacity,"%s(%llu)=%u",uimis_exports[k].name,(unsigned long long)id,(unsigned)(result&0xffu));
    else if(k==UIMIS_BRIEFING_START) snprintf(what,capacity,"%s(%llu,%s,%u)",uimis_exports[k].name,(unsigned long long)id,texture,(unsigned)a2);
    else if(k==UIMIS_BRIEFING_STOP) snprintf(what,capacity,"%s(%llu,%u)",uimis_exports[k].name,(unsigned long long)id,(unsigned)a1);
    else snprintf(what,capacity,"%s(%llu)",uimis_exports[k].name,(unsigned long long)id);
    return 1;
}

/* ---- pins (build 9.00): the code that gives the offsets and events above their meaning, checked at the arming ---- */
static const struct {unsigned rva,size;const char *sha;} uimis_pin_table[]={
    {0x175860u,0xc2,"9f3fef9a62c837658b1bee8a032e570ca52b301b68b30c4221a4d818f1708568"},   /* AbortMission: +0x60/0x5d0, Find(map,id,0), 0x63b */
    {0x1b7010u,0x79,"ad75f7152bbf139e70006c2ed3f845a5598ae2d66a2235bab569711fa219a133"},   /* SetActiveMission */
    {0x1b49c0u,0x128,"e0df3565ef3fc190011ff98fd568eaf4844b106679e186e0ab83d05c3bcc03f9"},  /* MissionBriefingInteractButtonClicked: 0x640 */
    {0x1bbcb0u,0x285,"eb9bba50b5abfd6297e71b4bf5127179d3cd8d6daf2f143d1a20803608b6a928"},  /* StartBriefing: +0x48/0x350 offers, 0x641 */
    {0x1bc9e0u,0xe0,"81d0f07af49dc8630f863298abfd8f81d1eaa587eb8ef378dc86d8898eb2071c"},   /* StopBriefing: 0x642 */
    {0x276b01u,0x26,"7a1c6259cfaed643ea66f7f63e277d2f902438e1fbd7f2cd9639bb8ce5a98e23"},   /* StartConversationFromMenu: idle +0x1a0 == 1 */
    {0x11784a2u,0xc0,"92bf0ee94059bde52f68ca2b61798bdfc747341e1b5645f912ba72c66dda9374"},  /* the conversation state writer: +0x80, +0x1a0 */
    {0x12334b0u,0x41,"3aadde51babd0ec1fd7d39aea11d67a94b38fa09f158e5e6537b121faf67a906"},  /* the mission manager's creation (0x6cf1658) */
    {0x1224bd1u,0x7,"1b9cdf4b576d06ffe271156bf1a7db4831a7db6030ae6f9c01d2a1f7ae2518f1"},   /* GameClient::Init stores 0x38ee230 */
};
#define UIMIS_PIN_COUNT (sizeof(uimis_pin_table)/sizeof(*uimis_pin_table))
static int uimis_pins_checked;
static int uimis_pins(void) {
    if(!uimis_pins_checked) {
        int ok=1;
        for(unsigned i=0;ok && i<UIMIS_PIN_COUNT;i++)
            if(!(ok=hash_ok(image+uimis_pin_table[i].rva,uimis_pin_table[i].size,uimis_pin_table[i].sha))) pin_failed_rva=uimis_pin_table[i].rva;
        uimis_pins_checked=ok?1:-1;
    }
    return uimis_pins_checked==1;
}

/* ---- arming, pass, rows ---- */
/* The families' prerequisite, at the replay's first pass (fail closed): the guard armed with the families' groups, so
 * a call of slots 2+ and a call the Lua core does not see are refused and a call of slot 1 reaches the capture. */
static int uimis_ready(void) {
    if(!(uirep_families&UIMIS_FAMILIES)) return 1;
    if(!uimis_armed) {uimis_why="ui_missions_not_armed";return 0;}
    if(!uiguard_armed) {uimis_why="ui_missions_need_guard";return 0;}
    if(((uirep_families&UIREP_FAMILY_MISSIONS) && !((uiguard_groups>>UIGUARD_G_MISSIONS)&1u)) ||
       ((uirep_families&UIREP_FAMILY_CONVERSATION) && !((uiguard_groups>>UIGUARD_G_CONVERSATION)&1u))) {uimis_why="ui_missions_need_guard_group";return 0;}
    return 1;
}
static void uimis_use_image(void) {
    for(unsigned k=1;k<UIMIS_KINDS;k++) uimis_fn[k]=(uimis_call_fn)(void*)(image+uimis_exports[k].rva);
}
/* gate.ui_missions(): after gate.ui_replay (with a family of this module) and gate.ui_guard; 0 and why otherwise. */
static int uimis_arm(double (*number)(void*,int)) {
    if(!uirep_armed || !(uirep_families&UIMIS_FAMILIES)) {uimis_why="needs_family";return 0;}
    if(!uimis_pins()) {static char pin_why[32];snprintf(pin_why,sizeof(pin_why),"pins_ui_missions:%x",pin_failed_rva);uimis_why=pin_why;return 0;}
    uimis_lua_number=number;uimis_armed=1;uimis_why="-";
    if(!uimis_ready() || !uimis_claim_entries()) {uimis_armed=0;return 0;}
    if(!uimis_fn[UIMIS_ABORT]) uimis_use_image();
    uimis_row_head=uimis_row_tail=0;
    return 1;
}
/* "tick:op:name:id,..." since the last call (what this client captured natively); 0 when none. */
static int uimis_rows_text(char *out,size_t capacity) {
    int n=0;out[0]=0;
    while(uimis_row_tail!=uimis_row_head) {
        const uimis_row *r=&uimis_rows[uimis_row_tail%UIMIS_RING];
        const char *name=r->op==UIMIS_OP_MISSION?uimis_exports[r->kind].name:uimis_conv_names[r->kind];
        int more=snprintf(out+n,capacity-(size_t)n,"%s%llu:%u:%s:%llu",n?",":"",(unsigned long long)r->tick,r->op,name,(unsigned long long)r->id);
        if(more<0 || (size_t)more>=capacity-(size_t)n) break;
        n+=more;++uimis_row_tail;
    }
    return n;
}
/* The receipt part: with missions the offers, with conversation the manager's state and current conversation. */
static int uimis_frame_text(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"UIMIS_1:");
    if(n<0 || (size_t)n>=capacity) return 0;
    if(uirep_families&UIREP_FAMILY_MISSIONS) {
        long offers=uimis_count(uimis_manager(UIMIS_MISSION_MANAGER),UIMIS_OFFERS,UIMIS_OFFER_SIZE);
        int more=snprintf(out+n,capacity-(size_t)n,"o%ld",offers);
        if(more<0 || (size_t)more>=capacity-(size_t)n) return 0;
        n+=more;
    }
    if(uirep_families&UIREP_FAMILY_CONVERSATION) {
        int state=-1,current=0;uimis_conv_state(&state,&current);
        int more=snprintf(out+n,capacity-(size_t)n,"/c%d.%d",state,current);
        if(more<0 || (size_t)more>=capacity-(size_t)n) return 0;
        n+=more;
    }
    return n;
}
/* What a person and the live test compare (not hashed): missions, offers, the conversation manager. */
static int uimis_state_text(char *out,size_t capacity) {
    unsigned char *m=uimis_manager(UIMIS_MISSION_MANAGER);int state=-1,current=0;uimis_conv_state(&state,&current);
    int n=snprintf(out,capacity,"missions=%ld offers=%ld conversation=%d.%d",uimis_count(m,UIMIS_MISSIONS,UIMIS_MISSION_SIZE),
        uimis_count(m,UIMIS_OFFERS,UIMIS_OFFER_SIZE),state,current);
    return n>0 && (size_t)n<capacity?n:0;
}
static int uimis_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"ui_missions=%s why=%s families=%x captured=%lld/%lld/%lld/%lld/%lld conversation=%lld/%lld/%lld/%lld applied=%lld gone=%lld unmapped=%lld lost=%lld",
        uimis_armed?"armed":"off",uimis_why,uirep_families&UIMIS_FAMILIES,(long long)uimis_captured[UIMIS_ABORT],(long long)uimis_captured[UIMIS_ACTIVE],
        (long long)uimis_captured[UIMIS_INTERACT],(long long)uimis_captured[UIMIS_BRIEFING_START],(long long)uimis_captured[UIMIS_BRIEFING_STOP],
        (long long)uimis_conv_captured[UIMIS_C_CANCEL],(long long)uimis_conv_captured[UIMIS_C_UNSUSPEND],(long long)uimis_conv_captured[UIMIS_C_SELECT],
        (long long)uimis_conv_captured[UIMIS_C_BACK],(long long)uimis_applied,(long long)uimis_gone,(long long)uimis_unmapped,(long long)uimis_rows_lost);
    return n>0 && (size_t)n<capacity?n:0;
}
