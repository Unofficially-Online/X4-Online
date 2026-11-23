/* Spoken lines per client, DEFAULT ON in play (native_gate_speech_local = true; needs native_gate_speak_timer, whose stop
 * reason 139 and block pin it shares; Windows gate).
 *
 * X4 speaks for one player: scripts start a line because of where the player is (slot 1's ship on every client), and
 * U::EntitySpeakData::SpeakNextLine 0x561330 starts its voice sound on whatever PC runs it. So every client plays slot 1's
 * comm chatter, wherever its own player is, and a hot joiner plays the session's past chatter again while it replays the
 * ticks since its save (user report, 10 Nov: the first instance's combat chatter on the second). The simulation does not
 * need the sound: with the speak timer every line ends on its text timer.
 *
 * This client starts no voice sound (lockstep_speak_timer.h's second site, the je at 0x56163b as two nops) while
 *   - it is a hot joiner before its handover (the replay of the session's past), or
 *   - its own slot is not slot 1 and its ship is in another sector (class 0x58) than slot 1's player: the lines are about
 *     slot 1's surroundings.
 * A line that belongs to a player is that player's: while an MD frame of a cue slot k owns runs (lockstep_mission_owners.h:
 * a mission slot k accepted, the story of the start slot k took), the site holds the owner's answer - client k starts the
 * voice, every other client does not, a hot joiner's catch-up never - and the frame's end puts the step's answer back
 * (spl_owner_enter / spl_owner_leave, called by the frame rule on the simulation's thread). So slot 2's wingmate speaks on
 * client 2, wherever slot 1 is, and not on client 1. Slot 1's own missions the same way: client 1 alone hears them.
 * A line the speaker's own queue starts later, outside a frame (the second line of a dialogue, a line that waited for
 * another), is its owner's too: the speaker is noted when the line is requested (spl_queue_note, below).
 * In slot 1's sector a client hears what slot 1 hears. Decided once per step in the probe's frame (gate.speech_local_frame:
 * the simulation's own thread, canonical state, so no line is being started while the site changes), by a 16-bit CAS. A
 * site that holds neither value is a failure of the speak timer's (its tick check stops, 139). An unseated slot (dead, in
 * a suit, walking) keeps the last answer. Not covered: lines X4 would speak for another player's own surroundings (never
 * started: the scripts ask about slot 1), other sounds of the replay (positional, at slot 1's camera until the handover). */
static int spl_armed,spl_muted;
static uint64_t spl_frames,spl_toggles,spl_catchup,spl_far,spl_near,spl_unknown;
static unsigned spl_owner_depth;   /* owned MD frames on the stack now */
static int spl_owner_before;       /* the step's answer the outermost of them found */
static uint64_t spl_owner_frames,spl_owner_played,spl_owner_muted;
static const char *spl_stage="idle";
static int join_catching_up(void);
static int (*spl_catching_up)(void)=join_catching_up;   /* tests replace it */
/* The class-0x58 (sector) ancestor of a component, itself included; NULL when none. */
static void *spl_sector_of(void *object) {
    unsigned char *p=object;
    for(unsigned depth=0;p && depth<24;depth++) {
        void **vtable=NULL;if(!read_local(p,&vtable,8) || !vtable) return NULL;
        int (*is_a)(void *,int)=NULL;if(!read_local((unsigned char*)vtable+0x11e8,&is_a,8) || !is_a) return NULL;
        if(is_a(p,0x58)) return p;
        if(!read_local(p+0x70,&p,8)) return NULL;
    }
    return NULL;
}
/* 1 mute, 0 play, -1 unknown (keep). */
static int spl_want(void) {
    if(spl_catching_up()) {++spl_catchup;return 1;}
    const unsigned slot=stats.slot;
    if(slot<2 || slot>PILOT_SLOTS) return 0;
    const pslot_slot *s=&pslots[slot];
    if(!s->seated || s->walking || !s->ship || !pslot_p1) {++spl_unknown;return -1;}
    void *mine=spl_sector_of(s->ship),*theirs=spl_sector_of(pslot_p1);
    if(!mine || !theirs) {++spl_unknown;return -1;}
    if(mine!=theirs) {++spl_far;return 1;}
    ++spl_near;return 0;
}
static int spl_set(int mute) {
    if(mute==spl_muted) return 1;
    if(!speak_site_swap(SPEAK_MUTE_SITE,mute?SPEAK_MUTE_ORIGINAL:SPEAK_MUTE_PATCHED,mute?SPEAK_MUTE_PATCHED:SPEAK_MUTE_ORIGINAL)) {
        spl_stage="cas";speak_timer_stage="mute_site";InterlockedIncrement64(&speak_timer_failures);return 0;
    }
    spl_muted=mute;++spl_toggles;return 1;
}
/* An MD frame of a cue slot k owns starts (the frame rule, on the simulation's thread): the lines it starts are slot k's. */
static void spl_owner_enter(unsigned k) {
    if(!spl_armed || !stats.active || k<1 || k>PILOT_SLOTS) return;
    if(spl_owner_depth++) return;   /* a frame inside a frame of the same scope: the outermost decides */
    spl_owner_before=spl_muted;++spl_owner_frames;
    const int mute=spl_catching_up() || stats.slot!=k;
    if(mute) ++spl_owner_muted;else ++spl_owner_played;
    (void)spl_set(mute);
}
/* ...and ends: the step's answer is back. */
static void spl_owner_leave(void) {
    if(!spl_owner_depth || --spl_owner_depth) return;
    if(spl_armed) (void)spl_set(spl_owner_before);
}
/* A player's own computer voice: the target readout. X4 reads the name of a newly selected target aloud (the game option
 * speaktargetname; U::Player's 0x705b80 queues the line on the speaker 0x703ac0 finds among the player's children, call
 * 0x561a10 at 0x705e7c). A slot's selection is replayed on every client, so every client read it aloud (a playtest
 * report of 14 Nov). The line itself stays on every client (its queue and timers are simulation state); only the voice
 * sound is the selecting player's:
 *   - the routine's own check of the option (call 0x1265960 at 0x705bc0) is where the speaker is noted: when the
 *     option is on, the speaker of the player the routine runs for (0x703ac0 on the singleton: the scope's player, slot
 *     1 outside every scope) is noted by its runtime ID with that slot. (The queueing call at 0x705e7c lies across two
 *     16-byte blocks, which the patcher does not write; build60 had it there and installed nothing.)
 *   - U::EntitySpeakData::SpeakNextLine 0x561330 starts the sound, later, from its own queue: its four call sites run
 *     it for a noted speaker (speak data +8 is the entity's ID) with the mute site holding that owner's answer (this
 *     client plays its own slot's, a hot joiner's catch-up nothing), and put the step's answer back after it.
 * Every other speaker and every other line of the session: as before. Sites and functions pinned; without them, or
 * without the slots' near page (a session of one), nothing is installed. Status lines=installed/noted/played/muted.
 *
 * A mission's speaker the same way. U::EntitySpeakData's request 0x561a10 (the speak action, an entity's own lines)
 * starts a line at once only when the speaker is silent; every further line starts later from the speaker's queue, in
 * a frame of nobody's, and took the step's answer: player 1 heard the rest of player 2's dialogue, and player 2, in
 * another sector, did not (the stock dialogue library speaks all lines of a call with one action). The request's entry
 * jumps to spl_queue here (its first instruction runs from a trampoline on the slots' near page; with the speech
 * diagnostic's own hook on that entry, lockstep_npc_enqueue.h, that hook calls spl_queue_note instead): a request made
 * in a frame of a player's own cue (mpp_show_slot) notes the speaker for that player, as the readout's is noted; a
 * request made in nobody's frame while the speaker is silent (speak data +0x10 zero) takes a cue's note away again.
 * Status cues=installed/noted/cleared. */
#define SPL_OPTION 0x1265960u             /* (config, key, default) -> the option's value */
#define SPL_OPTION_HEAD 0x40u
#define SPL_OPTION_SHA "0f7ff3176475ae81a7d6a3cfe14ab108e22af757c411b56fb112d1de4667bb13"
#define SPL_OPTION_SITE 0x705bc0u         /* U::Player 0x705b80: is speaktargetname on */
#define SPL_SPEAKER 0x703ac0u             /* (player, &entity): the player's speaker among its children */
#define SPL_SPEAKER_SIZE 0x163u
#define SPL_SPEAKER_SHA "94108c4b1e32f39110a72c79323252d10ffe0ea9efe9d6de7c240d15d6648868"
#define SPL_NEXT 0x561330u                /* U::EntitySpeakData::SpeakNextLine(this) */
#define SPL_NEXT_HEAD 0x40u
#define SPL_NEXT_SHA "248067cc4ba6d799712da8f0a67780d1fd4444e59be09d46362050cb3c79f61e"
#define SPL_OPTION_STUB 0x180u            /* on the slots' near page, after the fade's (0x120, 0x140) */
#define SPL_NEXT_STUB 0x1a0u
#define SPL_QUEUE 0x561a10u               /* U::EntitySpeakData's request to speak (lines, priority, interrupt, recipient, source, flags, text) */
#define SPL_QUEUE_SIZE 0x8au
#define SPL_QUEUE_SHA "398d2e47b9907849f3d93468cf465aaa120e5bba4b57c7a55f5ac5c5df514900"
#define SPL_QUEUE_STUB 0x420u             /* on the slots' near page, after the call monitor's (0x300..0x400) */
#define SPL_QUEUE_TRAMPOLINE 0x440u       /* the request's first instruction, then on at +5 */
#define SPL_SPEAKERS 24u
#define SPL_KIND_READOUT 0u               /* noted at the readout's option check */
#define SPL_KIND_CUE 1u                   /* noted at a request made in a player's own cue */
static const unsigned spl_next_sites[4]={0x562575u,0x5628cbu,0x564108u,0x9f07b4u};
typedef unsigned char (*spl_option_fn)(void *,const char *,unsigned char);
typedef void *(*spl_speaker_fn)(void *,void **);
typedef void (*spl_next_fn)(void *);
static spl_option_fn spl_option_original;static spl_speaker_fn spl_speaker_original;static spl_next_fn spl_next_original;   /* tests set their own */
static void *(*spl_player)(void);        /* tests: the player the routine runs for */
static struct {uint64_t id;unsigned owner,kind;} spl_speakers[SPL_SPEAKERS];
static unsigned spl_speaker_next;
static int spl_lines_installed,spl_lines_tried;
typedef unsigned char (*spl_queue_fn)(void *,void *,int,unsigned char,void *,void *,unsigned,void *);
static const unsigned char spl_queue_head[5]={0x44,0x88,0x4c,0x24,0x20};   /* mov [rsp+0x20],r9b */
static unsigned char spl_queue_patched[5];
static int spl_queue_installed;
static spl_queue_fn spl_queue_original;   /* tests: in place of the trampoline */
static unsigned char spl_option_patched[5],spl_next_patched[4][5];
static volatile LONG64 spl_lines_noted,spl_lines_played,spl_lines_muted,spl_cues_noted,spl_cues_cleared;
static unsigned mpp_show_slot(void);      /* lockstep_mission_owners.h: the player whose own cue's frame runs now */
/* Subtitles for the lines this client hears. A line whose voice X4 does not start (a text line, or a line this module
 * muted) goes into X4's set of text lines (SpeakNextLine 0x561330: its voice handle +0x14 stays 0, 0x5b65f0 inserts the
 * speak data into the set at 0x2f57660), and every frame the universe's update (0x9fdf29 -> 0x563a10) shows the line
 * 0x563bb0 picks from that set as the HUD's subtitle (U::VoiceOutputEvent). So a line muted here was still read out on
 * this screen: player 1's "Successfully docked." on player 2's (dockpair, 22 Nov: SPEECH_START owner=1 muted=1 on client
 * 2 and the subtitle there), and a mission call's subtitles on every screen (docs/PLAYTEST.md, 11 Nov).
 * The picker 0x563bb0 is ours (its entry jumps to spl_pick on the slots' near page; X4's own code is never called): the
 * same walk over the set in the same order with the same choices (states 0, 2, 6, 7 left out; +0x61 left out unless
 * state 4; the first with +0x5e clear at once; else the highest +0x58 among those with +0x60 clear), and a speak data
 * whose last line started here under this module's mute (spl_next notes each start) is left out. The set itself, the
 * lines and their timers are X4's, untouched. Pinned (the whole picker); restored on stop. Status subtitles=installed/hidden. */
#define SPL_PICK 0x563bb0u
#define SPL_PICK_SIZE 0xb0u
#define SPL_PICK_SHA "3062f192868831cb6f66ff4da524710bf51c66cb8e5c1321b0c700c9771e0add"
#define SPL_PICK_BEGIN 0x2f57680u         /* the set's first node */
#define SPL_PICK_END 0x2f57660u           /* the set's end (its header) */
#define SPL_PICK_STUB 0xa80u              /* on the slots' near page, after the undock events' (0xa40, 0xa60) */
#define SPL_STARTED 256u
static const unsigned char spl_pick_head[5]={0x48,0x8b,0x05,0xc9,0x3a};   /* mov rax,[rip+0x29f3ac9] (its first five bytes) */
static unsigned char spl_pick_patched[5];
static int spl_pick_installed;
static volatile LONG64 spl_pick_hidden;
static struct {const void *data;int muted;} spl_started[SPL_STARTED];   /* a speak data's last start here: muted by us? */
static unsigned spl_started_at(const void *data) {const uintptr_t p=(uintptr_t)data;return (unsigned)(((p>>4)^(p>>12))%SPL_STARTED);}
static void spl_started_note(const void *data,int muted) {
    if(!data) return;
    const unsigned at=spl_started_at(data);spl_started[at].data=data;spl_started[at].muted=muted!=0;
}
static int spl_started_muted(const void *data) {const unsigned at=spl_started_at(data);return spl_started[at].data==data && spl_started[at].muted;}
/* X4's picker with the one rule more (the set's nodes: +0 parent, +8 left, +0x10 right, +0x20 the speak data). */
static void *spl_pick_in(unsigned char *begin,unsigned char *end,int filter) {
    unsigned char *node=begin,*best=NULL;
    for(unsigned guard=0;node && node!=end && guard<4096u;guard++) {
        unsigned char *const sd=*(unsigned char **)(node+0x20);
        const uint32_t state=*(const uint32_t *)(sd+0x10);
        if(!(state<=7u && ((0xc5u>>state)&1u)) && (state==4u || !sd[0x61])) {
            if(filter && spl_started_muted(sd)) InterlockedIncrement64(&spl_pick_hidden);
            else {
                if(!sd[0x5e]) return sd;
                if(!sd[0x60] && (!best || *(const int32_t *)(sd+0x58)>*(const int32_t *)(best+0x58))) best=sd;
            }
        }
        unsigned char *next=*(unsigned char **)(node+0x10);
        if(next) {while(*(unsigned char **)(next+8)) next=*(unsigned char **)(next+8);}
        else {
            unsigned char *child=node;next=*(unsigned char **)node;
            for(unsigned up=0;next && *(unsigned char **)(next+8)!=child && up<64u;up++) {child=next;next=*(unsigned char **)next;}
        }
        node=next;
    }
    return best;
}
static __attribute__((noinline)) void *spl_pick(void) {
    return spl_pick_in(*(unsigned char **)(image+SPL_PICK_BEGIN),image+SPL_PICK_END,spl_armed && stats.active && GetCurrentThreadId()==ui_thread);
}
static int spl_pick_pins(const unsigned char *base) {return hash_ok(base+SPL_PICK,SPL_PICK_SIZE,SPL_PICK_SHA) && !memcmp(base+SPL_PICK,spl_pick_head,5);}
static void spl_pick_install(void) {
    DWORD old,ignored;
    if(spl_pick_installed || !pslot_page || !spl_pick_pins(image)) return;
    if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_READWRITE,&old)) return;
    pview_abs_jump(pslot_page+SPL_PICK_STUB,(void*)spl_pick);
    if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_EXECUTE_READ,&ignored)) return;
    FlushInstructionCache(GetCurrentProcess(),pslot_page,PVIEW_PAGE);
    const int32_t rel=(int32_t)((intptr_t)(pslot_page+SPL_PICK_STUB)-(intptr_t)(pnat_base+SPL_PICK+5));
    spl_pick_patched[0]=0xe9;memcpy(spl_pick_patched+1,&rel,4);
    memset(spl_started,0,sizeof(spl_started));
    if(pview_cas(SPL_PICK,spl_pick_head,spl_pick_patched,5)) spl_pick_installed=1;
}
static void spl_pick_restore(void) {
    if(spl_pick_installed && pview_cas(SPL_PICK,spl_pick_patched,spl_pick_head,5)) spl_pick_installed=0;
}
/* Rows (cheap, requests and starts are a few a minute): each request to speak (the request's entry: the tick, the speaker's
 * ID, the first line as page:id, how many lines, whose the frame is, the open scope, the MD cue of the frame that asked)
 * and each start of a line (SpeakNextLine: the speaker, its noted owner, whether this client's voice is muted for it).
 * gate.speech_local_rows() hands the new ones to the probe's log: SPEECH_REQUEST / SPEECH_START. */
#define SPL_ROWS 64u
static struct {uint64_t tick,speaker;uint32_t line[2];unsigned kind,count,owner,scope,muted;int state[2],result;char cue[56];} spl_rows[SPL_ROWS];
static unsigned spl_rows_written,spl_rows_read;
static int spl_data_state(void *data) {int state=-1;if(!data || !read_local((unsigned char*)data+0x10,&state,4)) state=-1;return state;}
/* A request's row after X4's own request: the speak data's state then and the request's answer. */
static void spl_row_answer(unsigned at,void *data,unsigned char result) {
    at%=SPL_ROWS;spl_rows[at].state[1]=spl_data_state(data);spl_rows[at].result=result;
}
static unsigned spl_row(unsigned kind,void *data,void *lines,unsigned owner,int muted) {
    uint64_t id=0;uintptr_t span[2]={0,0};
    if(!data || !read_local((unsigned char*)data+8,&id,8)) id=0;
    unsigned at=spl_rows_written++%SPL_ROWS;
    memset(&spl_rows[at],0,sizeof(spl_rows[at]));
    spl_rows[at].tick=stats.issued;spl_rows[at].speaker=id;spl_rows[at].kind=kind;spl_rows[at].owner=owner;
    spl_rows[at].scope=pstk_scope_slot;spl_rows[at].muted=muted<0?2u:(unsigned)muted;
    spl_rows[at].state[0]=spl_rows[at].state[1]=spl_data_state(data);spl_rows[at].result=-1;
    if(lines && read_local(lines,span,sizeof(span)) && span[1]>span[0]) {
        spl_rows[at].count=(unsigned)((span[1]-span[0])/0x30);
        if(!read_local((void*)span[0],spl_rows[at].line,8)) spl_rows[at].line[0]=spl_rows[at].line[1]=0;
    }
    if(kind==0 && script_rng_current) snprintf(spl_rows[at].cue,sizeof(spl_rows[at].cue),"%s",script_rng_current->id.name);
    return at;
}
/* The rows not handed out yet, one per line (a ring: the oldest are lost when more than SPL_ROWS came between two reads). */
static int spl_rows_text(char *out,size_t capacity) {
    size_t n=0;out[0]=0;
    if(spl_rows_written-spl_rows_read>SPL_ROWS) spl_rows_read=spl_rows_written-SPL_ROWS;
    while(spl_rows_read!=spl_rows_written && n+420<capacity) {
        const unsigned at=spl_rows_read++%SPL_ROWS;
        const int w=spl_rows[at].kind==0?
            snprintf(out+n,capacity-n,"SPEECH_REQUEST tick=%llu speaker=%llx line=%u:%u count=%u owner=%u scope=%u state=%d/%d result=%d cue=%s\n",
                (unsigned long long)spl_rows[at].tick,(unsigned long long)spl_rows[at].speaker,spl_rows[at].line[0],spl_rows[at].line[1],
                spl_rows[at].count,spl_rows[at].owner,spl_rows[at].scope,spl_rows[at].state[0],spl_rows[at].state[1],spl_rows[at].result,
                spl_rows[at].cue[0]?spl_rows[at].cue:"-"):
            snprintf(out+n,capacity-n,"SPEECH_START tick=%llu speaker=%llx owner=%u scope=%u muted=%u state=%d\n",
                (unsigned long long)spl_rows[at].tick,(unsigned long long)spl_rows[at].speaker,spl_rows[at].owner,spl_rows[at].scope,spl_rows[at].muted,
                spl_rows[at].state[0]);
        if(w<=0 || (size_t)w>=capacity-n) break;
        n+=(size_t)w;
    }
    return (int)n;
}
static void spl_speaker_note(uint64_t id,unsigned owner,unsigned kind) {
    unsigned at=SPL_SPEAKERS;
    if(!id || !owner) return;
    for(unsigned i=0;i<SPL_SPEAKERS;i++) if(spl_speakers[i].id==id) {at=i;break;}
    for(unsigned i=0;at==SPL_SPEAKERS && i<SPL_SPEAKERS;i++) if(!spl_speakers[i].id) at=i;
    if(at==SPL_SPEAKERS) {at=spl_speaker_next;spl_speaker_next=(spl_speaker_next+1u)%SPL_SPEAKERS;}
    spl_speakers[at].id=id;spl_speakers[at].owner=owner;spl_speakers[at].kind=kind;
    InterlockedIncrement64(kind==SPL_KIND_CUE?&spl_cues_noted:&spl_lines_noted);
}
/* A request to speak (0x561a10, before X4's own): whose the speaker's coming lines are. */
static void spl_queue_note(void *data) {
    uint64_t id=0;int state=0;
    if(!spl_armed || !stats.active || GetCurrentThreadId()!=ui_thread || !data || !read_local((unsigned char*)data+8,&id,8) || !id) return;
    const unsigned owner=mpp_show_slot();
    if(owner) {spl_speaker_note(id,owner,SPL_KIND_CUE);return;}
    for(unsigned i=0;i<SPL_SPEAKERS;i++) if(spl_speakers[i].id==id && spl_speakers[i].kind==SPL_KIND_CUE) {
        if(read_local((unsigned char*)data+0x10,&state,4) && !state) {memset(&spl_speakers[i],0,sizeof(spl_speakers[i]));InterlockedIncrement64(&spl_cues_cleared);}
        return;
    }
}
/* The owner of a speak data's entity, noted before; 0 for every other one. */
static unsigned spl_speaker_owner(void *data) {
    uint64_t id=0;
    if(!data || !read_local((unsigned char*)data+8,&id,8) || !id) return 0;
    for(unsigned i=0;i<SPL_SPEAKERS;i++) if(spl_speakers[i].id==id) return spl_speakers[i].owner;
    return 0;
}
/* The request's entry: the note, then X4's own request with every argument as it came. */
__attribute__((noinline)) static unsigned char spl_queue(void *data,void *lines,int priority,unsigned char interrupt,void *recipient,void *source,
        unsigned flags,void *text) {
    const spl_queue_fn original=spl_queue_original?spl_queue_original:(spl_queue_fn)(void*)(pslot_page+SPL_QUEUE_TRAMPOLINE);
    const int here=spl_armed && stats.active && GetCurrentThreadId()==ui_thread;
    const unsigned row=here?spl_row(0,data,lines,mpp_show_slot(),-1):0u;
    spl_queue_note(data);
    const unsigned char result=original(data,lines,priority,interrupt,recipient,source,flags,text);
    if(here) spl_row_answer(row,data,result);
    return result;
}
static void spl_queue_install(void) {
    DWORD old,ignored;
    if(spl_queue_installed || !pslot_page || !hash_ok(image+SPL_QUEUE,SPL_QUEUE_SIZE,SPL_QUEUE_SHA) || memcmp(pnat_base+SPL_QUEUE,spl_queue_head,5)) return;
    if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_READWRITE,&old)) return;
    pview_abs_jump(pslot_page+SPL_QUEUE_STUB,(void*)spl_queue);
    memcpy(pslot_page+SPL_QUEUE_TRAMPOLINE,spl_queue_head,5);
    pview_abs_jump(pslot_page+SPL_QUEUE_TRAMPOLINE+5,pnat_base+SPL_QUEUE+5);
    if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_EXECUTE_READ,&ignored)) return;
    FlushInstructionCache(GetCurrentProcess(),pslot_page,PVIEW_PAGE);
    const int32_t rel=(int32_t)((intptr_t)(pslot_page+SPL_QUEUE_STUB)-(intptr_t)(pnat_base+SPL_QUEUE+5));
    spl_queue_patched[0]=0xe9;memcpy(spl_queue_patched+1,&rel,4);
    if(pview_cas(SPL_QUEUE,spl_queue_head,spl_queue_patched,5)) spl_queue_installed=1;
}
static void spl_queue_restore(void) {
    if(spl_queue_installed && pview_cas(SPL_QUEUE,spl_queue_patched,spl_queue_head,5)) spl_queue_installed=0;
}
static unsigned char spl_option(void *config,const char *key,unsigned char fallback) {
    const unsigned char on=(spl_option_original?spl_option_original:(spl_option_fn)(image+SPL_OPTION))(config,key,fallback);
    if(on && spl_armed && stats.active && GetCurrentThreadId()==ui_thread) {
        void *player=spl_player?spl_player():(void*)pnat_player(),*entity=NULL;uint64_t id=0;
        __try {if(player) (void)(spl_speaker_original?spl_speaker_original:(spl_speaker_fn)(image+SPL_SPEAKER))(player,&entity);}
        __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {entity=NULL;}
        if(entity && read_local((unsigned char*)entity+8,&id,8)) spl_speaker_note(id,pstk_scope_slot?pstk_scope_slot:1u,SPL_KIND_READOUT);
    }
    return on;
}
static void spl_next(void *data) {
    const spl_next_fn next=spl_next_original?spl_next_original:(spl_next_fn)(image+SPL_NEXT);
    const int here=spl_armed && stats.active && GetCurrentThreadId()==ui_thread;
    const unsigned owner=here?spl_speaker_owner(data):0;
    if(!owner) {if(here) {spl_row(1,data,NULL,0,spl_muted);spl_started_note(data,spl_muted);}next(data);return;}
    const int before=spl_muted,mute=spl_catching_up() || stats.slot!=owner;
    spl_row(1,data,NULL,owner,mute);spl_started_note(data,mute);
    InterlockedIncrement64(mute?&spl_lines_muted:&spl_lines_played);
    (void)spl_set(mute);
    next(data);
    if(spl_armed) (void)spl_set(before);
}
static void spl_lines_install(void) {
    if(spl_lines_installed || spl_lines_tried || !pslot_page) return;
    spl_lines_tried=1;
    spl_queue_install();   /* on its own: the notes it takes are read by the four calls below when they are there */
    spl_pick_install();    /* on its own too: without the four calls below nothing is noted and it picks as X4 does */
    if(!hash_ok(image+SPL_OPTION,SPL_OPTION_HEAD,SPL_OPTION_SHA) || !hash_ok(image+SPL_SPEAKER,SPL_SPEAKER_SIZE,SPL_SPEAKER_SHA) ||
       !hash_ok(image+SPL_NEXT,SPL_NEXT_HEAD,SPL_NEXT_SHA)) return;
    unsigned done=0;
    for(;done<4;done++) {
        if(!pres_site_install(spl_next_sites[done],SPL_NEXT,SPL_NEXT_STUB,(void*)spl_next,spl_next_patched[done])) break;
        /* the speak timer's block check reads this site as the original call (lockstep_speak_timer.h) */
        const int32_t rel=(int32_t)(SPL_NEXT-(spl_next_sites[done]+5u));
        speak_timer_sites[done].rva=spl_next_sites[done];speak_timer_sites[done].original[0]=0xe8;memcpy(speak_timer_sites[done].original+1,&rel,4);
        memcpy(speak_timer_sites[done].patched,spl_next_patched[done],5);speak_timer_sites[done].on=1;
    }
    if(done==4 && pres_site_install(SPL_OPTION_SITE,SPL_OPTION,SPL_OPTION_STUB,(void*)spl_option,spl_option_patched)) {spl_lines_installed=1;return;}
    while(done) {--done;if(pres_site_uninstall(spl_next_sites[done],SPL_NEXT,spl_next_patched[done])) speak_timer_sites[done].on=0;}
}
static void spl_lines_restore(void) {
    spl_queue_restore();
    spl_pick_restore();
    if(!spl_lines_installed) return;
    int ok=pres_site_uninstall(SPL_OPTION_SITE,SPL_OPTION,spl_option_patched);
    for(unsigned i=0;i<4;i++) {
        if(pres_site_uninstall(spl_next_sites[i],SPL_NEXT,spl_next_patched[i])) speak_timer_sites[i].on=0;
        else ok=0;
    }
    if(ok) spl_lines_installed=0;
    memset(spl_speakers,0,sizeof(spl_speakers));
}
/* gate.speech_local(): before the first tick, after the speak timer (an identical rerun after a UI reload answers 1). */
static int spl_arm(void) {
    if(spl_armed) return stats.active!=0;
    if(!stats.active || GetCurrentThreadId()!=ui_thread || !speak_timer_installed || !speak_timer_patched) {spl_stage="refused";return 0;}
    if(speak_mute_site()!=SPEAK_MUTE_ORIGINAL) {spl_stage="site";return 0;}
    spl_frames=spl_toggles=spl_catchup=spl_far=spl_near=spl_unknown=0;spl_muted=0;spl_armed=1;spl_stage="armed";
    spl_owner_depth=0;spl_owner_before=0;spl_owner_frames=spl_owner_played=spl_owner_muted=0;
    spl_lines_tried=0;spl_speaker_next=0;memset(spl_speakers,0,sizeof(spl_speakers));spl_cues_noted=spl_cues_cleared=0;return 1;
}
/* gate.speech_local_frame(): once per step, in the probe's frame. Answers the state (1 muted). */
static int spl_frame(void) {
    if(!spl_armed || !stats.active) return 0;
    ++spl_frames;
    spl_lines_install();   /* once, when the slots' near page is there */
    const int want=spl_want();
    if(want>=0) {
        if(spl_owner_depth) spl_owner_before=want;   /* never inside an owned frame in the probe's frame; kept right if it were */
        else (void)spl_set(want);
    }
    return spl_muted;
}
/* Stop: the site back before the speak timer's own restore. A foreign value stays. */
static void spl_restore(void) {
    spl_lines_restore();
    if(!spl_armed) return;
    if(spl_muted && speak_site_swap(SPEAK_MUTE_SITE,SPEAK_MUTE_PATCHED,SPEAK_MUTE_ORIGINAL)) spl_muted=0;
    spl_armed=0;spl_owner_depth=0;spl_stage="restored";
}
static int spl_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"armed=%d muted=%d site=%04x frames=%llu toggles=%llu catchup=%llu far=%llu near=%llu unknown=%llu stage=%s "
        "owned=%llu/%llu/%llu lines=%d/%lld/%lld/%lld cues=%d/%lld/%lld subtitles=%d/%lld",
        spl_armed,spl_muted,speak_mute_site(),(unsigned long long)spl_frames,(unsigned long long)spl_toggles,(unsigned long long)spl_catchup,
        (unsigned long long)spl_far,(unsigned long long)spl_near,(unsigned long long)spl_unknown,spl_stage,
        (unsigned long long)spl_owner_frames,(unsigned long long)spl_owner_played,(unsigned long long)spl_owner_muted,
        spl_lines_installed,(long long)spl_lines_noted,(long long)spl_lines_played,(long long)spl_lines_muted,
        spl_queue_installed,(long long)spl_cues_noted,(long long)spl_cues_cleared,spl_pick_installed,(long long)spl_pick_hidden);
    return n>0 && (size_t)n<capacity?n:0;
}
