/* Opt-in protocol 5: adaptive window (docs/LOCKSTEP-NSLOT-WIRE.txt, PROTOCOL 5; relay
 * tools/dev/native_gate_adaptive.py). The protocol 4 input wire, but the relay chooses the input
 * lag: it pings every client while they wait at tick 0, then announces the plan (entry 1: from
 * proposal tick 0 the lag is L = k+1, the pilot delay P) in every permit, before the first gated
 * step. Later entries ('from proposal tick c the lag is L') change it during a session, each
 * announced before any tick it changes was sealed. A proposal sealed with tick c applies at c+L(c);
 * a larger lag leaves gap permits without inputs, a smaller one by d makes the d ticks before c
 * quiet (sealed empty, carried by no permit). Each permit names the proposal tick it carries, so a
 * client checks every permit against the agreed schedule: nothing is lost, applied twice or before
 * it was sealed, and a permit that disagrees stops the session.
 *   NGSTATE|5|<token>|<slot>|<tick>|<game>|<real>|<digest>|<clients>|<epoch>|<blob>
 *   NGGO|5|<token>|<slot>|<T>|<source or ->|<epoch>|<start>:<lag>:<pilot>|<mask>|<blob 1>|...|<blob N>
 *   NGPING|5|<token>|<slot>|<nonce>  ->  NGPONG|5|<token>|<slot>|<nonce>|<held_us>
 *   NGSTOP|5|<token>|<slot>|<tick>, NGDROP|5|<token>|<slot>|<tick>
 * epoch counts the schedule entries a report's sender held when it sealed it, or the entries in
 * force when the relay issued a permit (the permit carries the newest one). mask: two hex digits,
 * bit s-1 = slot s was dropped by the relay (stalled past its bound); its blobs are then empty.
 * held_us: how long the ping can have waited unread (0 when it arrived during a blocking receive).
 * Stop reasons: 190 a plan or entry this client cannot follow (above its window maximum, late,
 * out of order, a different copy, the pilot delay not adoptable), 191 a permit whose proposal tick
 * is not the one the schedule names, 192 a non-empty proposal sealed at a quiet tick, 193 this
 * client was dropped, 194 a schedule change its armed wire pilot records cannot follow (they keep
 * the plan's delay: no quiet ticks, no lag above pilot-1). 102/103/56 as in protocol 4. Default off.
 * Platform-neutral like lockstep_lookahead.h; ADAPT_WITH_PILOT (lockstep_gate.c) adds the pilot
 * delay adoption. */
#define LOCKSTEP_ADAPTIVE 1
#define ADAPT_ENTRIES 32u
#define ADAPT_STOP_PLAN 190u
#define ADAPT_STOP_SOURCE 191u
#define ADAPT_STOP_QUIET 192u
#define ADAPT_STOP_DROPPED 193u
#define ADAPT_STOP_PILOT 194u
#define ADAPT_PILOT_MAX 30u /* PILOT_DELAY_MAX */
typedef struct {uint64_t start;unsigned lag,pilot;} adapt_entry;
typedef struct {
    uint64_t tick,source;int used,gap;unsigned epoch,mask;adapt_entry entry;
    char blobs[GATE_CLIENTS_MAX][GATE_BLOB_HEX_MAX+1];
} adapt_permit;
static int wire_adaptive;                /* gate.lookahead_adaptive(kmax): protocol 5 */
static unsigned adapt_kmax;              /* largest window this client follows (rings hold kmax+1) */
static unsigned adapt_epoch,adapt_mask;  /* entries held; dropped slots so far */
static adapt_entry adapt_entries[ADAPT_ENTRIES];
static uint64_t adapt_cursor;            /* the next non-quiet proposal tick a permit must carry */
static uint64_t adapt_source;int adapt_source_set; /* the last taken permit's proposal tick (s.inputs_tick) */
static adapt_permit adapt_permits[LOOKAHEAD_RING];
static char adapt_scratch[GATE_CLIENTS_MAX][GATE_BLOB_HEX_MAX+1];
static LARGE_INTEGER adapt_empty;        /* when the socket was last found empty */
static uint64_t adapt_pings,adapt_gaps,adapt_ready,adapt_idle,adapt_resends,adapt_stale,adapt_duplicates,adapt_ahead_max;
static const char *adapt_why="none";
static void adapt_reset(void) {
    memset(adapt_entries,0,sizeof(adapt_entries));memset(adapt_permits,0,sizeof(adapt_permits));
    adapt_epoch=adapt_mask=0;adapt_cursor=adapt_source=0;adapt_source_set=0;
    adapt_pings=adapt_gaps=adapt_ready=adapt_idle=adapt_resends=adapt_stale=adapt_duplicates=adapt_ahead_max=0;
    adapt_why="none";QueryPerformanceCounter(&adapt_empty);
}
/* ---- the schedule (tools/dev/native_gate_adaptive.py Schedule) ---- */
static unsigned adapt_lag(uint64_t c) {
    unsigned lag=adapt_entries[0].lag;
    for(unsigned i=1;i<adapt_epoch;i++) if(adapt_entries[i].start<=c) lag=adapt_entries[i].lag;
    return lag;
}
static int adapt_quiet(uint64_t c) {
    for(unsigned i=1;i<adapt_epoch;i++) {
        unsigned before=adapt_entries[i-1].lag,lag=adapt_entries[i].lag;
        if(lag<before && c<adapt_entries[i].start && c+(before-lag)>=adapt_entries[i].start) return 1;
    }
    return 0;
}
static uint64_t adapt_next_source(uint64_t c) {
    do ++c; while(adapt_quiet(c));
    return c;
}
/* The earliest proposal tick entry i changes (its start, or its first quiet tick); 0 for the plan. */
static uint64_t adapt_first_affected(unsigned i) {
    if(!i) return 0;
    unsigned before=adapt_entries[i-1].lag,lag=adapt_entries[i].lag;
    return adapt_entries[i].start-(lag<before?before-lag:0);
}
/* Whether e may follow the entries held (Schedule.valid). */
static int adapt_entry_valid(const adapt_entry *e) {
    if(e->lag<1 || e->lag>adapt_kmax+1 || e->pilot<e->lag+1 || e->pilot>ADAPT_PILOT_MAX) return 0;
    if(!adapt_epoch) return e->start==0;
    const adapt_entry *p=&adapt_entries[adapt_epoch-1];
    if(adapt_epoch>=ADAPT_ENTRIES || e->pilot!=adapt_entries[0].pilot || e->lag==p->lag) return 0;
    uint64_t quiet=p->lag>e->lag?p->lag-e->lag:0;
    return e->start>p->start+quiet;
}
/* ---- pilot records: the plan's delay, before the first gated step ---- */
#ifdef ADAPT_WITH_PILOT
static int adapt_pilot_armed(void) {return pilot_armed && pilot_source==PILOT_SOURCE_WIRE;}
static int adapt_pilot_adopt(const adapt_entry *plan) {
    if(!adapt_pilot_armed()) return 1;            /* no wire pilot records: the delay is not used */
    if(predict_armed || stats.issued) return 0;   /* the prediction horizon is the armed delay */
    if(!pilot_config_valid(pilot_slot_count,pilot_source,plan->pilot,pilot_start,pilot_stop,pilot_suppress) ||
       !pilot_delay_covers(PILOT_SOURCE_WIRE,plan->pilot,plan->lag-1)) return 0;
    pilot_delay=plan->pilot;opred_delay_adopted();return 1;
}
#else
static int adapt_pilot_armed(void) {return 0;}
static int adapt_pilot_adopt(const adapt_entry *plan) {(void)plan;return 1;}
#endif
/* A new entry from a permit (receive time). 0, or the stop reason. */
static unsigned adapt_adopt(const adapt_entry *e,uint64_t completed) {
    if(!adapt_entry_valid(e)) {adapt_why="entry_refused";return ADAPT_STOP_PLAN;}
    adapt_entries[adapt_epoch]=*e;  /* counted below, once accepted */
    if(adapt_epoch) {
        /* Ahead of every tick it changes: this client has not sealed one of them. */
        uint64_t first=adapt_first_affected(adapt_epoch);
        if(first<=completed) {adapt_why="entry_late";return ADAPT_STOP_PLAN;}
        /* Wire pilot records keep the plan's delay: a quiet tick would leave one of their apply ticks without a record. */
        if(adapt_pilot_armed() && first<e->start) {adapt_why="pilot_quiet";return ADAPT_STOP_PILOT;}
    } else if(!adapt_pilot_adopt(e)) {adapt_why="pilot_delay";return ADAPT_STOP_PILOT;}
    ++adapt_epoch;
    if(adapt_epoch==1) wire_lookahead=e->lag-1;
    return 0;
}
/* ---- permits ---- */
static size_t adapt_hex2(const char *s,size_t n,unsigned *out) {
    unsigned v=0;
    if(n<2) return 0;
    for(unsigned i=0;i<2;i++) {
        char c=s[i];
        if(c>='0' && c<='9') v=v*16+(unsigned)(c-'0');
        else if(c>='a' && c<='f') v=v*16+(unsigned)(c-'a'+10);
        else return 0;
    }
    *out=v;return 2;
}
/* NGGO|5 for this client: 0 held (or an identical copy, or ignored: stale, not ours, malformed);
 * otherwise the stop reason. */
static unsigned adapt_permit_receive(const char *reply,size_t length,uint64_t completed) {
    char head[64];uint64_t tick=0,source=0,epoch=0,start=0,lag=0,pilot=0;unsigned mask=0;int gap=0;size_t at,n;
    int h=snprintf(head,sizeof(head),"NGGO|5|%s|%u|",secret,stats.slot);
    if(!reply || h<=0 || (size_t)h>=sizeof(head) || length>=GATE_PERMIT_MAX || length<=(size_t)h || memcmp(reply,head,(size_t)h)) return 0;
    at=(size_t)h;
    if(!(n=lookahead_decimal(reply+at,length-at,&tick))) return 0;
    at+=n;if(at>=length || reply[at++]!='|') return 0;
    if(at<length && reply[at]=='-') {gap=1;++at;}
    else {if(!(n=lookahead_decimal(reply+at,length-at,&source))) return 0;at+=n;}
    if(at>=length || reply[at++]!='|' || !(n=lookahead_decimal(reply+at,length-at,&epoch))) return 0;
    at+=n;if(at>=length || reply[at++]!='|' || !(n=lookahead_decimal(reply+at,length-at,&start))) return 0;
    at+=n;if(at>=length || reply[at++]!=':' || !(n=lookahead_decimal(reply+at,length-at,&lag))) return 0;
    at+=n;if(at>=length || reply[at++]!=':' || !(n=lookahead_decimal(reply+at,length-at,&pilot))) return 0;
    at+=n;if(at>=length || reply[at++]!='|' || !(n=adapt_hex2(reply+at,length-at,&mask))) return 0;
    at+=n;
    if(at>=length || reply[at]!='|' || lag>GATE_LOOKAHEAD_MAX+1 || pilot>ADAPT_PILOT_MAX || epoch>ADAPT_ENTRIES) return 0;
    if(tick<=completed) {++adapt_stale;return 0;}
    if(tick-completed>1+adapt_kmax) {adapt_why="ahead";return LOOKAHEAD_STOP_AHEAD;}
    const char *own="";
    if(!gap) {
        const lookahead_report *r=&lookahead_reports[source%LOOKAHEAD_RING];
        if(source>=tick || source>completed || tick-source>adapt_kmax+1 || !r->used || r->tick!=source) {adapt_why="source_window";return LOOKAHEAD_STOP_AHEAD;}
        own=r->blob;
    }
    char prefix[160];
    if(at>=sizeof(prefix)) return 0;
    memcpy(prefix,reply,at);prefix[at]=0;
    int ok=gate_permit_blobs(reply,length,prefix,stats.slot,wire_clients,own,adapt_scratch);
    if(ok<0) {adapt_why="own_proposal";return 56;}
    if(!ok) return 0;
    if(mask>>wire_clients) return 0;
    if(mask>>(stats.slot-1)&1u) {adapt_why="dropped";return ADAPT_STOP_DROPPED;}
    for(unsigned i=0;i<wire_clients;i++)
        if(adapt_scratch[i][0] && (gap || (mask>>i&1u))) {adapt_why=gap?"gap_inputs":"dropped_inputs";return LOOKAHEAD_STOP_AHEAD;}
    adapt_entry e={start,(unsigned)lag,(unsigned)pilot};
    if(!epoch || epoch>adapt_epoch+1) {adapt_why="epoch";return ADAPT_STOP_PLAN;}
    if(epoch==adapt_epoch+1) {unsigned reason=adapt_adopt(&e,completed);if(reason) return reason;}
    else {
        const adapt_entry *held=&adapt_entries[epoch-1];
        if(held->start!=e.start || held->lag!=e.lag || held->pilot!=e.pilot) {adapt_why="entry_copy";return ADAPT_STOP_PLAN;}
    }
    adapt_permit *p=&adapt_permits[tick%LOOKAHEAD_RING];
    if(p->used && p->tick==tick) {
        int same=p->gap==gap && p->source==(gap?0:source) && p->epoch==(unsigned)epoch && p->mask==mask &&
            p->entry.start==e.start && p->entry.lag==e.lag && p->entry.pilot==e.pilot;
        for(unsigned i=0;same && i<wire_clients;i++) same=!strcmp(p->blobs[i],adapt_scratch[i]);
        if(!same) {adapt_why="conflict";return LOOKAHEAD_STOP_CONFLICT;}
        ++adapt_duplicates;return 0;
    }
    p->tick=tick;p->used=1;p->gap=gap;p->source=gap?0:source;p->epoch=(unsigned)epoch;p->mask=mask;p->entry=e;
    for(unsigned i=0;i<wire_clients;i++) memcpy(p->blobs[i],adapt_scratch[i],strlen(adapt_scratch[i])+1);
    if(tick-completed>adapt_ahead_max) adapt_ahead_max=tick-completed;
    return 0;
}
/* The held permit for tick, checked against the schedule: 1 taken (s.inputs), 0 not held, else stop reason+1. */
static unsigned adapt_take(uint64_t tick) {
    adapt_permit *p=&adapt_permits[tick%LOOKAHEAD_RING];
    if(!p->used || p->tick!=tick) return 0;
    if(!adapt_epoch) {adapt_why="no_plan";return ADAPT_STOP_PLAN+1;}
    uint64_t apply=adapt_cursor+adapt_lag(adapt_cursor);
    if(apply<tick || (apply==tick)==p->gap || (!p->gap && p->source!=adapt_cursor)) {adapt_why="source";return ADAPT_STOP_SOURCE+1;}
    if((p->mask&adapt_mask)!=adapt_mask) {adapt_why="mask";return ADAPT_STOP_SOURCE+1;}
    adapt_mask=p->mask;
    for(unsigned i=0;i<wire_clients;i++) memcpy(blob_inputs[i],p->blobs[i],strlen(p->blobs[i])+1);
    if(p->gap) {adapt_source_set=0;++adapt_gaps;}
    else {
#ifdef ADAPT_WITH_PILOT
        if(adapt_mask) pilot_drop_note(adapt_mask,adapt_cursor); /* a dropped slot's rows stop with this proposal tick */
#endif
        adapt_source=adapt_cursor;adapt_source_set=1;
        wire_lookahead=adapt_lag(adapt_cursor)-1; /* hit claims reconstruct the source as T-1-k */
        adapt_cursor=adapt_next_source(adapt_cursor);
    }
    p->used=0;return 1;
}
/* ---- datagrams ---- */
static int adapt_matches(const char *reply,size_t length,const char *kind,uint64_t *value) {
    char head[64];size_t digits;
    int n=snprintf(head,sizeof(head),"%s|5|%s|%u|",kind,secret,stats.slot);
    if(n<=0 || (size_t)n>=sizeof(head) || length<=(size_t)n || memcmp(reply,head,(size_t)n)) return 0;
    digits=lookahead_decimal(reply+n,length-(size_t)n,value);
    return digits && (size_t)n+digits==length;
}
/* One datagram read held_us after it may have arrived: 0 to go on, otherwise the stop reason. */
static unsigned adapt_datagram(char *reply,int count,uint64_t held_us) {
    uint64_t value=0;
    reply[count]=0;
    if((size_t)count!=strlen(reply)) return 0;
    if(adapt_matches(reply,(size_t)count,"NGSTOP",&value)) return value<=stats.completed?1u:0u;
    if(adapt_matches(reply,(size_t)count,"NGDROP",&value)) {adapt_why="dropped";return ADAPT_STOP_DROPPED;}
    if(adapt_matches(reply,(size_t)count,"NGPING",&value)) {
        char pong[128];
        int n=snprintf(pong,sizeof(pong),"NGPONG|5|%s|%u|%llu|%llu",secret,stats.slot,(unsigned long long)value,
                       (unsigned long long)(held_us>1000000000ULL?1000000000ULL:held_us));
        if(n>0 && (size_t)n<sizeof(pong)) {send(connection,pong,n,0);++adapt_pings;}
        return 0;
    }
    return adapt_permit_receive(reply,(size_t)count,stats.completed);
}
/* Every datagram already queued, without blocking; a ping's hold is the time since the queue was last empty. */
static unsigned adapt_drain(char *reply,size_t capacity) {
    for(unsigned i=0;i<4*LOOKAHEAD_RING;i++) {
        u_long queued=0;LARGE_INTEGER now;
        if(ioctlsocket(connection,FIONREAD,&queued)) return 0;
        QueryPerformanceCounter(&now);
        if(!queued) {adapt_empty=now;return 0;}
        int count=recv(connection,reply,(int)capacity-1,0);
        if(count<0) {int error=WSAGetLastError();return error!=WSAETIMEDOUT&&error!=WSAEWOULDBLOCK?8u:0u;}
        unsigned reason=adapt_datagram(reply,count,ft_us(adapt_empty,now));
        if(reason) return reason;
    }
    return 0;
}
static void adapt_resend(uint64_t completed,ULONGLONG now) {
    for(uint64_t t=completed>adapt_kmax?completed-adapt_kmax:0;t<=completed;t++) {
        lookahead_report *r=&lookahead_reports[t%LOOKAHEAD_RING];
        if(r->used && r->tick==t && now-r->sent>=LOOKAHEAD_RESEND_MS) {lookahead_send(r,now);++adapt_resends;}
    }
}
/* 0: the permit for completed+1 is taken; otherwise the stop reason. */
static unsigned adapt_wait(char *reply,size_t capacity) {
    uint64_t want=stats.completed+1;ULONGLONG start=GetTickCount64();int blocked=0;
    for(;;) {
        ULONGLONG now=GetTickCount64();
        double drift=fabs(*(double*)(image+GAME_RVA)-stats.game);
        if(drift>stats.wait_game_delta) stats.wait_game_delta=drift;
        if(drift>0) return 5;
        if(closed_window()) return 6;
        unsigned reason=adapt_drain(reply,capacity);
        if(reason) return reason;
        reason=adapt_take(want);
        if(reason==1) {if(!blocked) ++adapt_ready;return 0;}
        if(reason) return reason-1;
        if(now-start>timeout_ms) return 7;
        adapt_resend(stats.completed,now);
        blocked=1;
        /* The queue was just found empty: what this receive returns arrived while it blocked (held 0). */
        int count=recv(connection,reply,(int)capacity-1,0);
        if(count<0) {
            int error=WSAGetLastError();
            if(error!=WSAETIMEDOUT&&error!=WSAEWOULDBLOCK) return 8;
            QueryPerformanceCounter(&adapt_empty);++adapt_idle;continue;
        }
        reason=adapt_datagram(reply,count,0);
        if(reason) return reason;
    }
}
/* Whether the permit for tick is held (the render-only frames' plan, lockstep_render_frames.h). */
static int adapt_held(uint64_t tick) {
    const adapt_permit *p=&adapt_permits[tick%LOOKAHEAD_RING];return p->used && p->tick==tick;
}
/* A proposal at a quiet tick stops the session before it is sealed (192); 0 otherwise. */
static unsigned adapt_quiet_check(void) {
    if(adapt_epoch && blob_proposal[0] && adapt_quiet(stats.completed)) {adapt_why="quiet_input";return ADAPT_STOP_QUIET;}
    return 0;
}
/* One gated frame: seal the report of stats.completed (quiet ticks carry nothing), send it, wait for the next permit. */
static unsigned adapt_frame(const char *packet,size_t length,char *reply,size_t capacity) {
    unsigned quiet=adapt_quiet_check();
    if(quiet) return quiet;
    if(!lookahead_seal(stats.completed,packet,length,blob_proposal)) return 56;
    lookahead_send(&lookahead_reports[stats.completed%LOOKAHEAD_RING],GetTickCount64());
    return adapt_wait(reply,capacity);
}
/* Protocol 5 (0 keeps protocols 2-4): after x4gate_wire(3,N), before start/boot; once started only the same maximum. */
static int adapt_configure(unsigned kmax) {
    int valid=wire_version==3 && kmax>=1 && kmax<=GATE_LOOKAHEAD_MAX && !peer_on(); /* peer links extend protocol 4 only */
    if(used) return valid && wire_adaptive && kmax==adapt_kmax;
    if(!valid) return 0;
    /* Until the plan, the window maximum bounds every check that reads the window (pilot_delay_covers). */
    wire_adaptive=1;adapt_kmax=kmax;wire_lookahead=kmax;return 1;
}
static int adapt_status(char *out,size_t capacity) {
    const adapt_entry *e=adapt_epoch?&adapt_entries[adapt_epoch-1]:NULL;
    int n=snprintf(out,capacity,"adaptive kmax=%u epoch=%u k=%d lag=%u pilot=%u start=%llu cursor=%llu mask=%02x gaps=%llu pings=%llu ready=%llu "
        "ahead_max=%llu idle=%llu resends=%llu stale=%llu duplicates=%llu why=%s",adapt_kmax,adapt_epoch,e?(int)e->lag-1:-1,e?e->lag:0u,
        e?e->pilot:0u,(unsigned long long)(e?e->start:0),(unsigned long long)adapt_cursor,adapt_mask,(unsigned long long)adapt_gaps,
        (unsigned long long)adapt_pings,(unsigned long long)adapt_ready,(unsigned long long)adapt_ahead_max,(unsigned long long)adapt_idle,
        (unsigned long long)adapt_resends,(unsigned long long)adapt_stale,(unsigned long long)adapt_duplicates,adapt_why);
    return n>0 && (size_t)n<capacity?n:0;
}
