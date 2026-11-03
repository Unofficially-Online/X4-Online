/* Opt-in peer links (roadmap B6): extension 1 over protocol 4. Specification in
 * docs/LOCKSTEP-NSLOT-WIRE.txt (PEER LINKS), Python reference and simulator in
 * tools/dev/native_gate_peer.py / native_gate_peer_sim.py, measurements in
 * reports/LOCKSTEP-PEER-LINKS-20261101.txt. Shared by the Windows and Linux gates
 * (included by lockstep_lookahead.h). Off unless gate.peer_links(m) was called
 * before the gate started; then, with the protocol 4 window k >= 1, k + m <= 8:
 * - a second UDP socket (the peer socket) says NGPHELLO to the relay until it
 *   answers with every client's peer address (NGPADDR, sent only once every
 *   client opted in) or refuses (NGPREFUSE: the session stays relay-only);
 * - every sealed proposal also goes once to each peer the NAT module reaches
 *   (NGPREC); a record sealed with t applies at T = t+1+k, as with the permit;
 * - F is the highest tick whose relay permit, and every earlier one, arrived.
 *   Any tick T runs only when T <= F + m: on its relay permit, or without it on
 *   all N records of T-1-k (and F >= 1: the relay admitted tick 0 for everyone;
 *   never past the relay's last tick, which NGPADDR names: the session ends there);
 * - every relay permit is compared with the peer copies of its records, before
 *   or after its tick ran: a difference stops with 104, two different peer
 *   copies of one record with 108, another copy of a final permit with 103.
 *   The relay hears of it first (NGPFAIL) and stops everyone. Malformed,
 *   unauthenticated or unexpected peer datagrams are dropped, never fatal.
 * m = 0 exchanges and checks records but runs only on permits (measurement).
 * The relay stays the referee: digests, clocks, timeouts and stops; a client
 * never runs more than m ticks past it. */
#ifndef _WIN32
#include <sys/select.h>
typedef socklen_t peer_socklen;
#else
typedef int peer_socklen;
#endif
#include "lockstep_nat.h"   /* the NAT traversal core (sans-IO); the wire crypto comes with lockstep_wire_crypto_gate.h */
#define PEER_VERSION 1u
#define PEER_RING 32u                /* apply ticks; a live record is never more than 2k+m+3 <= 19 past F */
#define PEER_STOP_MISMATCH 104u      /* a peer copy differs from the relay's permit (102, 103: lookahead) */
#define PEER_STOP_EQUIVOCATION 108u  /* two different peer copies of one record (105 is the Linux gate's) */
#define PEER_DATAGRAM_MAX 2048u
#define PEER_SIGNATURE_HEX_MAX 256u
#define PEER_HELLO_MS 100u
enum {PEER_OFF,PEER_HELLO,PEER_ACTIVE,PEER_REFUSED,PEER_FAILED};

/* ---- Interfaces of the parallel modules. The stubs below stay for sessions without the
 * real modules (and for tests); the real ones are joined further down: peer_nat_traversal
 * (gate.peer_links(m, P)) and the wire crypto records (gate.wire_crypto). ---- */
typedef struct {
    const char *name;
    /* The port for the peer socket (0: any), asked once before it is bound; UPnP maps it first. */
    unsigned (*local_port)(void);
    /* After NGPADDR, every frame until decided: 1 = send slot's records to *route from sock;
     * 0 = unreachable, that slot's records come only with the relay's permits; -1 = still
     * punching. advertised = the address the relay saw for that player's peer socket. */
    int (*route)(SOCKET sock,unsigned slot,const struct sockaddr_in *advertised,struct sockaddr_in *route);
    /* A datagram on the peer socket that is neither a relay answer nor a routed peer's: 1 consumed. */
    int (*datagram)(SOCKET sock,const struct sockaddr_in *from,const char *data,size_t length);
} peer_nat_api;
typedef struct {
    const char *name;
    /* AEAD under the session key (the module owns nonces and framing): the sealed length in
     * out, 0 = do not send. */
    size_t (*seal)(const char *plain,size_t length,char *out,size_t capacity);
    /* The plain length in out (capacity leaves room for a terminating zero), 0 = not sealed
     * with the session key: the datagram is dropped, never fatal. */
    size_t (*open)(const char *sealed,size_t length,char *out,size_t capacity);
    /* This player's signature over record[0..length), lowercase hex, zero-terminated, at most
     * PEER_SIGNATURE_HEX_MAX characters: 1 ok, 0 = do not send. */
    int (*sign)(const char *record,size_t length,char *signature,size_t capacity);
    /* 1: signature[0..signature_length) is slot's (public key from the relay) over record. */
    int (*verify)(unsigned slot,const char *record,size_t length,const char *signature,size_t signature_length);
} peer_crypto_api;
static unsigned peer_stub_port(void) {return 0;}
static int peer_stub_route(SOCKET sock,unsigned slot,const struct sockaddr_in *advertised,struct sockaddr_in *route) {
    (void)sock;(void)slot;*route=*advertised;return 1;
}
static int peer_stub_datagram(SOCKET sock,const struct sockaddr_in *from,const char *data,size_t length) {
    (void)sock;(void)from;(void)data;(void)length;return 0;
}
static size_t peer_stub_copy(const char *in,size_t length,char *out,size_t capacity) {
    if(!in || length>=capacity) return 0;
    memcpy(out,in,length);out[length]=0;return length;
}
static int peer_stub_sign(const char *record,size_t length,char *signature,size_t capacity) {
    (void)record;(void)length;if(!capacity) return 0;signature[0]=0;return 1;
}
static int peer_stub_verify(unsigned slot,const char *record,size_t length,const char *signature,size_t signature_length) {
    (void)slot;(void)record;(void)length;(void)signature;return signature_length==0;
}
/* The stubs reach every advertised address and neither encrypt nor sign: a forged record
 * can then stop a session (the permit check), but never be applied unnoticed. */
static const peer_nat_api peer_nat_stub={"stub",peer_stub_port,peer_stub_route,peer_stub_datagram};
static const peer_crypto_api peer_crypto_stub={"stub",peer_stub_copy,peer_stub_copy,peer_stub_sign,peer_stub_verify};
static const peer_nat_api *peer_nat=&peer_nat_stub;
static const peer_crypto_api *peer_crypto=&peer_crypto_stub;

typedef struct {
    uint64_t tick;unsigned used,have,final,ran;  /* have: bit slot-1 per record held */
    LONGLONG complete_us,permit_us;               /* all N records held / the permit arrived */
    char blobs[GATE_CLIENTS_MAX][GATE_BLOB_HEX_MAX+1];
} peer_entry;
static peer_entry peer_entries[PEER_RING];
static int peer_window=-1;                        /* gate.peer_links(m): -1 off (default) */
static unsigned peer_state,peer_routed,peer_pending;
static SOCKET peer_socket=INVALID_SOCKET;
static struct sockaddr_in peer_relay,peer_routes[GATE_CLIENTS_MAX],peer_advertised[GATE_CLIENTS_MAX];
static uint64_t peer_final,peer_last;            /* F; the relay's last tick (--ticks) */
static ULONGLONG peer_hello_ms;static int peer_hello_sent;
static uint64_t peer_sent,peer_received,peer_dropped,peer_duplicates,peer_stale,peer_runs,peer_first,peer_relay_first,
    peer_lead_us,peer_behind_max,peer_unreachable;
static char peer_scratch[PEER_DATAGRAM_MAX],peer_plain[PEER_DATAGRAM_MAX];
/* The real modules' state (below): the NAT core on the peer socket, the wire crypto's peer keys. */
static nat_core peer_traversal;
static unsigned peer_rendezvous;                  /* P; 0: the stub's routes (NGPADDR) */
static int peer_traversal_started;
static wc_tx peer_wc_tx;static wc_key peer_wc_keys[WC_SLOTS_MAX];static int peer_wc_ready;
static uint64_t peer_forged;                      /* authentic under the group key, but a wrong signature or field */

static int peer_on(void) {return peer_window>=0;}
/* Before the gate starts: m = -1 (off) or 0..8-k with protocol 4 (k >= 1) configured;
 * once started only the identical m (UI reloads). start_gate checks it again. */
static int peer_links_configure(int m,int started) {
    int valid=m==-1 || (m>=0 && wire_version==3 && wire_lookahead>=1 && wire_lookahead+(unsigned)m<=GATE_LOOKAHEAD_MAX);
    if(started) return valid && m==peer_window;
    if(!valid) return 0;
    peer_window=m;return 1;
}
static int peer_config_ok(void) {
    return peer_window<0 || (wire_version==3 && wire_lookahead>=1 && wire_lookahead+(unsigned)peer_window<=GATE_LOOKAHEAD_MAX);
}
static LONGLONG peer_now_us(void) {
    LARGE_INTEGER count,frequency;QueryPerformanceCounter(&count);QueryPerformanceFrequency(&frequency);
    if(frequency.QuadPart<=0) return 0;
    return count.QuadPart/frequency.QuadPart*1000000+count.QuadPart%frequency.QuadPart*1000000/frequency.QuadPart;
}
/* From any thread (finish): the socket is taken exactly once. */
static void peer_close(void) {
#ifdef _WIN32
    SOCKET owned=(SOCKET)InterlockedExchange64((volatile LONG64*)&peer_socket,(LONG64)INVALID_SOCKET);
#else
    SOCKET owned=__atomic_exchange_n(&peer_socket,INVALID_SOCKET,__ATOMIC_SEQ_CST);
#endif
    if(owned!=INVALID_SOCKET) closesocket(owned);
}
/* At gate start (lookahead_reset): no socket, nothing held; the configured window stays. */
static void peer_reset(void) {
    peer_close();memset(peer_entries,0,sizeof(peer_entries));
    peer_state=peer_on()?PEER_HELLO:PEER_OFF;peer_routed=peer_pending=0;peer_final=peer_last=0;peer_hello_ms=0;peer_hello_sent=0;
    memset(peer_routes,0,sizeof(peer_routes));memset(peer_advertised,0,sizeof(peer_advertised));memset(&peer_relay,0,sizeof(peer_relay));
    peer_sent=peer_received=peer_dropped=peer_duplicates=peer_stale=peer_runs=peer_first=peer_relay_first=0;
    peer_lead_us=peer_behind_max=peer_unreachable=0;
    memset(&peer_traversal,0,sizeof(peer_traversal));peer_traversal.closed=1;peer_traversal_started=0;
    crypto_wipe(&peer_wc_tx,sizeof(peer_wc_tx));crypto_wipe(peer_wc_keys,sizeof(peer_wc_keys));peer_wc_ready=0;peer_forged=0;
}
static unsigned peer_all(void) {return (1u<<wire_clients)-1u;}
static peer_entry *peer_entry_at(uint64_t tick,int create) {
    peer_entry *e=&peer_entries[tick%PEER_RING];
    if(e->used && e->tick==tick) return e;
    if(!create) return NULL;
    e->tick=tick;e->used=1;e->have=e->final=e->ran=0;e->complete_us=e->permit_us=0;
    return e;
}
static void peer_complete(peer_entry *e) {
    if(!e->complete_us && !e->final && e->have==peer_all()) e->complete_us=peer_now_us();
}
/* NGPFAIL|1|<token>|<slot>|<tick>|<reason> over the relay connection, once, before the gate stops. */
static void peer_notify(unsigned reason) {
    char text[128];
    int n=snprintf(text,sizeof(text),"NGPFAIL|%u|%s|%u|%llu|%u",PEER_VERSION,secret,stats.slot,(unsigned long long)stats.completed,reason);
    if(n<=0 || (size_t)n>=sizeof(text) || connection==INVALID_SOCKET) return;
    if(!wcg.armed) send(connection,text,n,0);
    else if(wcg.established) gate_wire_send(text,(size_t)n);   /* sealed like every datagram on the relay path */
}
/* The peer socket: bound to loopback for a loopback relay, else to any address, on the
 * NAT module's port; its datagrams to the relay go where the relay connection goes. */
static int peer_open_socket(void) {
    if(peer_socket!=INVALID_SOCKET) return 1;
    struct sockaddr_in relay,local;peer_socklen size=sizeof(relay);memset(&relay,0,sizeof(relay));memset(&local,0,sizeof(local));
    if(connection==INVALID_SOCKET || getpeername(connection,(struct sockaddr*)&relay,&size) || relay.sin_family!=AF_INET) return 0;
#ifdef SOCK_CLOEXEC
    SOCKET s=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,IPPROTO_UDP);
#else
    SOCKET s=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
#endif
    if(s==INVALID_SOCKET) return 0;
    unsigned port=peer_nat->local_port();
    local.sin_family=AF_INET;local.sin_port=htons((unsigned short)(port<=65535u?port:0u));
    local.sin_addr.s_addr=relay.sin_addr.s_addr==htonl(INADDR_LOOPBACK)?htonl(INADDR_LOOPBACK):htonl(INADDR_ANY);
    if(bind(s,(struct sockaddr*)&local,sizeof(local))) {closesocket(s);return 0;}
    peer_relay=relay;peer_socket=s;return 1;
}
static void peer_send(const struct sockaddr_in *to,const char *data,size_t length) {
    if(peer_socket!=INVALID_SOCKET) sendto(peer_socket,data,(int)length,0,(const struct sockaddr*)to,sizeof(*to));
}
/* ---- The real modules (joined at the integration, 1 November 2026) ----
 * NAT traversal (lockstep_nat.h, reports/LOCKSTEP-NAT-TRAVERSAL-20261101.txt): gate.peer_links(m, P) with the relay's
 * NGNAT rendezvous on UDP P and P+1 (native_gate_relay.py --rendezvous-port P). The sans-IO core runs on the peer socket
 * itself (hole punching needs the socket whose mapping the rendezvous saw): datagrams starting NGNAT| are the core's,
 * everything else the peer links'. A slot's route is the core's DIRECT path (a nonce round trip over that address
 * authenticated it); RELAY (no rendezvous, timeout, a symmetric pair, a closed core) means unreachable: that slot's
 * records then come only with the permits. NGPADDR's addresses are not used for routes then. Without P the stub routes
 * to the addresses the relay saw (NGPADDR), as on contrib/ferrite-p2p.
 * Wire crypto (lockstep_wire_crypto.h, layer 5): with gate.wire_crypto armed every record goes out as wc_peer_seal (the
 * group key from NGKEYS, this slot's subkey, the record signed with this player's session Ed25519 key, the same signature
 * the relay checks on its report) and only wc_peer_open's authentic records of the routed sender are taken; nothing is
 * sent before the keys arrived and nothing in the clear. Without wire crypto the stub (no secrecy, no authenticity). */
static nat_addr peer_nat_addr(const struct sockaddr_in *a) {nat_addr n;n.ip=ntohl(a->sin_addr.s_addr);n.port=ntohs(a->sin_port);return n;}
static void peer_traversal_send(void *context,nat_addr to,const char *data,size_t length) {
    (void)context;struct sockaddr_in a;memset(&a,0,sizeof(a));
    a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(to.ip);a.sin_port=htons(to.port);peer_send(&a,data,length);
}
/* Once the peer socket is open: this slot, the session token, the rendezvous at the relay's address, this host's
 * address toward the relay with the peer socket's port as the LAN candidate; loopback candidates only with a loopback
 * relay (one machine). A refused start leaves the core closed: every pair RELAY. */
static void peer_traversal_start(void) {
    if(peer_traversal_started || peer_socket==INVALID_SOCKET) return;
    peer_traversal_started=1;
    nat_config c;nat_config_defaults(&c);
    c.slot=stats.slot;c.clients=wire_clients;memcpy(c.token,secret,32);c.token[32]=0;
    nat_addr relay=peer_nat_addr(&peer_relay);
    c.server[0].ip=c.server[1].ip=relay.ip;c.server[0].port=(uint16_t)peer_rendezvous;c.server[1].port=(uint16_t)(peer_rendezvous+1u);
    c.allow_loopback=nat_ip_loopback(relay.ip);
    struct sockaddr_in local,bound;peer_socklen size=sizeof(bound);
    memset(&local,0,sizeof(local));memset(&bound,0,sizeof(bound));
    if(!getsockname(peer_socket,(struct sockaddr*)&bound,&size)) {
        size=sizeof(local);
        if(!getsockname(connection,(struct sockaddr*)&local,&size) && local.sin_addr.s_addr) {c.lan.ip=ntohl(local.sin_addr.s_addr);c.lan.port=ntohs(bound.sin_port);}
    }
    if(!gate_crypto_random(c.instance,8) || nat_core_init(&peer_traversal,&c)) {memset(&peer_traversal,0,sizeof(peer_traversal));peer_traversal.closed=1;}
}
static unsigned peer_traversal_port(void) {return 0;}
static int peer_traversal_route(SOCKET sock,unsigned slot,const struct sockaddr_in *advertised,struct sockaddr_in *route) {
    (void)sock;(void)advertised;
    nat_addr path;memset(&path,0,sizeof(path));
    int decided=nat_core_path(&peer_traversal,slot,&path,NULL);
    if(decided==NAT_PATH_PENDING) return -1;
    if(decided!=NAT_PATH_DIRECT) return 0;
    memset(route,0,sizeof(*route));route->sin_family=AF_INET;route->sin_addr.s_addr=htonl(path.ip);route->sin_port=htons(path.port);
    return 1;
}
static int peer_traversal_datagram(SOCKET sock,const struct sockaddr_in *from,const char *data,size_t length) {
    (void)sock;return nat_core_input(&peer_traversal,GetTickCount64(),peer_nat_addr(from),data,length,peer_traversal_send,NULL);
}
static const peer_nat_api peer_nat_traversal={"traversal",peer_traversal_port,peer_traversal_route,peer_traversal_datagram};
/* Every poll: the core's timers (rendezvous, probes, keep-alives), from the moment the peer socket exists. */
static void peer_traversal_service(ULONGLONG now) {
    if(peer_nat!=&peer_nat_traversal || peer_socket==INVALID_SOCKET) return;
    peer_traversal_start();
    nat_core_poll(&peer_traversal,now,peer_traversal_send,NULL);
}
/* gate.peer_links(m, P): P = 0 (the stub) or 1024..65534; once started only the identical P. */
static int peer_nat_configure(unsigned port,int started) {
    int valid=!port || (port>=1024u && port<=65534u);
    if(started) return valid && port==peer_rendezvous;
    if(!valid) return 0;
    peer_rendezvous=port;peer_nat=port?&peer_nat_traversal:&peer_nat_stub;return 1;
}
static int peer_wc(void) {return wcg.armed;}
static int peer_wc_keys_ready(void) {
    if(peer_wc_ready) return 1;
    if(!wcg.keyed || stats.slot<1 || stats.slot>wire_clients) return 0;
    memset(&peer_wc_tx,0,sizeof(peer_wc_tx));wc_peer_key(&peer_wc_tx.k,wcg.group,wcg.sid,0,stats.slot);
    for(unsigned s=1;s<=wire_clients;s++) wc_peer_key(&peer_wc_keys[s-1],wcg.group,wcg.sid,0,s);
    peer_wc_ready=1;return 1;
}
/* This slot's record of tick, sealed and signed into peer_scratch: its length, 0 = do not send. */
static size_t peer_wc_seal(uint64_t tick,const char *blob) {
    size_t hex=strlen(blob);uint8_t bytes[WC_BLOB_MAX];
    if(!peer_wc_keys_ready() || hex%2 || hex/2>WC_BLOB_MAX || !wc_unhex(bytes,blob,hex/2)) return 0;
    return wc_peer_seal(&peer_wc_tx,wcg.sid,wcg.hs.sign_sk,wire_clients,wire_lookahead,tick,bytes,hex/2,(uint8_t*)peer_scratch,sizeof(peer_scratch));
}
/* NGPHELLO|1|<token>|<slot>|<clients>|<k>|<m>, every PEER_HELLO_MS until the relay answers. */
static void peer_hello(ULONGLONG now) {
    if(peer_state!=PEER_HELLO || peer_socket==INVALID_SOCKET || (peer_hello_sent && now-peer_hello_ms<PEER_HELLO_MS)) return;
    char text[128];
    int n=snprintf(text,sizeof(text),"NGPHELLO|%u|%s|%u|%u|%u|%d",PEER_VERSION,secret,stats.slot,wire_clients,wire_lookahead,peer_window);
    if(n>0 && (size_t)n<sizeof(text)) peer_send(&peer_relay,text,(size_t)n);
    peer_hello_ms=now;peer_hello_sent=1;
}
/* "a.b.c.d:port", canonical decimals. */
static int peer_parse_address(const char *s,size_t n,struct sockaddr_in *out) {
    size_t at=0;uint32_t ip=0;uint64_t value=0;
    for(unsigned i=0;i<4;i++) {
        size_t digits=lookahead_decimal(s+at,n-at,&value);
        if(!digits || value>255) return 0;
        at+=digits;ip=(ip<<8)|(uint32_t)value;
        if(at>=n || s[at]!=(i<3?'.':':')) return 0;
        ++at;
    }
    size_t digits=lookahead_decimal(s+at,n-at,&value);
    if(!digits || at+digits!=n || !value || value>65535) return 0;
    memset(out,0,sizeof(*out));out->sin_family=AF_INET;out->sin_addr.s_addr=htonl(ip);out->sin_port=htons((unsigned short)value);
    return 1;
}
static void peer_poll_routes(void) {
    for(unsigned i=0;i<wire_clients;i++) if(peer_pending&(1u<<i)) {
        struct sockaddr_in route;memset(&route,0,sizeof(route));
        int decided=peer_nat->route(peer_socket,i+1,&peer_advertised[i],&route);
        if(decided<0) continue;
        peer_pending&=~(1u<<i);
        if(decided) {peer_routes[i]=route;peer_routed|=1u<<i;}
        else ++peer_unreachable;
    }
}
/* NGPADDR|1|<token>|<slot>|<m>|<last>|<ip:port 1>|...|<N>  or  NGPREFUSE|1|<token>|<slot>|off|mismatch */
static void peer_relay_answer(const char *text,size_t n) {
    if(peer_state!=PEER_HELLO) return;
    char head[96];int h=snprintf(head,sizeof(head),"NGPREFUSE|%u|%s|%u|",PEER_VERSION,secret,stats.slot);
    if(h>0 && n>(size_t)h && !memcmp(text,head,(size_t)h)) {
        size_t r=n-(size_t)h;
        if((r==3 && !memcmp(text+h,"off",3)) || (r==8 && !memcmp(text+h,"mismatch",8))) peer_state=PEER_REFUSED;
        else ++peer_dropped;
        return;
    }
    h=snprintf(head,sizeof(head),"NGPADDR|%u|%s|%u|%d|",PEER_VERSION,secret,stats.slot,peer_window);
    if(h<=0 || n<=(size_t)h || memcmp(text,head,(size_t)h)) {++peer_dropped;return;}
    struct sockaddr_in book[GATE_CLIENTS_MAX];size_t at=(size_t)h;uint64_t last=0;
    size_t digits=lookahead_decimal(text+at,n-at,&last);
    if(!digits || at+digits>=n || text[at+digits]!='|') {++peer_dropped;return;}
    at+=digits+1;
    for(unsigned i=0;i<wire_clients;i++) {
        size_t end=at;
        while(end<n && text[end]!='|') ++end;
        if(!peer_parse_address(text+at,end-at,&book[i]) || (i+1<wire_clients?end>=n:end!=n)) {++peer_dropped;return;}
        at=end+1;
    }
    for(unsigned i=0;i<wire_clients;i++) if(i+1!=stats.slot) {peer_advertised[i]=book[i];peer_pending|=1u<<i;}
    peer_last=last;peer_state=PEER_ACTIVE;peer_poll_routes();
}
static unsigned peer_source_slot(const struct sockaddr_in *from) {
    for(unsigned i=0;i<wire_clients;i++)
        if((peer_routed&(1u<<i)) && peer_routes[i].sin_addr.s_addr==from->sin_addr.s_addr && peer_routes[i].sin_port==from->sin_port) return i+1;
    return 0;
}
static unsigned peer_drop(void) {++peer_dropped;return 0;}
static unsigned peer_accept(unsigned slot,uint64_t tick,const char *blob,size_t blob_length);
/* With wire crypto: wc_peer_open's authentic record, of the routed sender only. */
static unsigned peer_wc_record(unsigned slot,const char *data,size_t n) {
    unsigned sender=0;uint64_t tick=0;uint8_t bytes[WC_BLOB_MAX],sig[WC_SIG];size_t length=0;
    int opened=peer_wc_keys_ready()?wc_peer_open(peer_wc_keys,(const uint8_t (*)[WC_KEY])wcg.roster,wcg.sid,wire_clients,wire_lookahead,
        (const uint8_t*)data,n,&sender,&tick,bytes,&length,sig):0;
    if(opened==-2) ++peer_forged;   /* a group key holder forged it: never applied (evidence: the status) */
    if(opened!=1 || sender!=slot) return peer_drop();
    static const char digits[]="0123456789abcdef";char hex[GATE_BLOB_HEX_MAX+1];
    for(size_t i=0;i<length;i++) {hex[2*i]=digits[bytes[i]>>4];hex[2*i+1]=digits[bytes[i]&15u];}
    hex[2*length]=0;
    return peer_accept(slot,tick,hex,2*length);
}
/* A datagram from a peer: the NAT module's own (NGNAT|, also over a routed path), a wire crypto
 * record, or NGPREC|1|<token>|<slot>|<tick>|<clients>|<k>|<blob>|<signature> through the stub
 * crypto. 0 or the stop reason. */
static unsigned peer_record(const char *data,size_t n,const struct sockaddr_in *from) {
    if(n>=6 && !memcmp(data,"NGNAT|",6)) return peer_nat->datagram(peer_socket,from,data,n)?0:peer_drop();
    unsigned slot=peer_state==PEER_ACTIVE?peer_source_slot(from):0;
    if(!slot) return peer_nat->datagram(peer_socket,from,data,n)?0:peer_drop();
    if(peer_wc()) return peer_wc_record(slot,data,n);   /* with wire crypto nothing in the clear is taken */
    size_t plain=peer_crypto->open(data,n,peer_plain,sizeof(peer_plain));
    if(!plain || plain>=sizeof(peer_plain)) return peer_drop();
    peer_plain[plain]=0;
    if(strlen(peer_plain)!=plain) return peer_drop();
    char head[96];int h=snprintf(head,sizeof(head),"NGPREC|%u|%s|%u|",PEER_VERSION,secret,slot);
    if(h<=0 || plain<=(size_t)h || memcmp(peer_plain,head,(size_t)h)) return peer_drop();
    uint64_t tick=0,value=0,expected[2]={wire_clients,wire_lookahead};size_t at=(size_t)h,digits=lookahead_decimal(peer_plain+at,plain-at,&tick);
    if(!digits || at+digits>=plain || peer_plain[at+digits]!='|') return peer_drop();
    at+=digits+1;
    for(unsigned i=0;i<2;i++) {
        digits=lookahead_decimal(peer_plain+at,plain-at,&value);
        if(!digits || value!=expected[i] || at+digits>=plain || peer_plain[at+digits]!='|') return peer_drop();
        at+=digits+1;
    }
    size_t blob=at;
    while(at<plain && peer_plain[at]!='|') ++at;
    if(at>=plain) return peer_drop();
    size_t blob_length=at-blob,signature_length=plain-at-1;const char *signature=peer_plain+at+1;
    if(!gate_blob_span(peer_plain+blob,blob_length) || signature_length>PEER_SIGNATURE_HEX_MAX || !gate_blob_span(signature,signature_length) ||
       !peer_crypto->verify(slot,peer_plain,at,signature,signature_length)) return peer_drop();
    return peer_accept(slot,tick,peer_plain+blob,blob_length);
}
/* An authentic record of slot sealed with tick: held for T = tick+1+k; 0 or the stop reason. */
static unsigned peer_accept(unsigned slot,uint64_t tick,const char *blob,size_t blob_length) {
    if(tick>UINT64_MAX-1u-wire_lookahead) return peer_drop();
    uint64_t T=tick+1+wire_lookahead;peer_entry *e;
    if(T<=peer_final) {
        e=peer_entry_at(T,0);
        if(!e) {++peer_stale;return 0;}
    }
    else if(T-peer_final>=PEER_RING) return peer_drop();
    else e=peer_entry_at(T,1);
    unsigned bit=1u<<(slot-1);char *held=e->blobs[slot-1];
    if(e->have&bit) {
        if(strlen(held)==blob_length && !memcmp(held,blob,blob_length)) {++peer_duplicates;return 0;}
        unsigned reason=e->final?PEER_STOP_MISMATCH:PEER_STOP_EQUIVOCATION;
        peer_notify(reason);return reason;
    }
    memcpy(held,blob,blob_length);held[blob_length]=0;e->have|=bit;++peer_received;peer_complete(e);
    if(peer_nat==&peer_nat_traversal) nat_core_alive(&peer_traversal,slot,GetTickCount64());   /* the direct path carries */
    return 0;
}
static unsigned peer_datagram(const char *data,size_t n,const struct sockaddr_in *from) {
    if(from->sin_addr.s_addr==peer_relay.sin_addr.s_addr && from->sin_port==peer_relay.sin_port) {
        if(n>=sizeof(peer_plain)) return peer_drop();
        memcpy(peer_plain,data,n);peer_plain[n]=0;
        if(strlen(peer_plain)==n) peer_relay_answer(peer_plain,n);
        return 0;
    }
    return peer_record(data,n,from);
}
/* Every datagram queued on the peer socket (bounded per call), never blocking. A receive
 * error (an ICMP port-unreachable from a departed peer) never stops: the relay carries on. */
static unsigned peer_drain(void) {
    for(unsigned i=0;i<64 && peer_socket!=INVALID_SOCKET;i++) {
        u_long queued=0;
        if(ioctlsocket(peer_socket,FIONREAD,&queued) || !queued) return 0;
        struct sockaddr_in from;peer_socklen size=sizeof(from);memset(&from,0,sizeof(from));
        int count=recvfrom(peer_socket,peer_scratch,(int)sizeof(peer_scratch),0,(struct sockaddr*)&from,&size);
        if(count<0) continue;
        if((size_t)count>=sizeof(peer_scratch) || from.sin_family!=AF_INET) {++peer_dropped;continue;}
        unsigned reason=peer_datagram(peer_scratch,(size_t)count,&from);
        if(reason) return reason;
    }
    return 0;
}
/* Once per wait round and frame: queued peer datagrams, the hello, routes still pending. */
static unsigned peer_poll(ULONGLONG now) {
    if(!peer_on()) return 0;
    unsigned reason=peer_drain();
    if(reason) return reason;
    peer_hello(now);
    peer_traversal_service(now);
    if(peer_pending && peer_state==PEER_ACTIVE) peer_poll_routes();
    return 0;
}
/* The stub path's record: NGPREC text, signed and sealed by peer_crypto into peer_scratch; its
 * length, 0 = do not send. */
static size_t peer_text_seal(uint64_t tick,const char *blob) {
    char text[PEER_DATAGRAM_MAX],signature[PEER_SIGNATURE_HEX_MAX+1];
    int n=snprintf(text,sizeof(text),"NGPREC|%u|%s|%u|%llu|%u|%u|%s",PEER_VERSION,secret,stats.slot,(unsigned long long)tick,
                   wire_clients,wire_lookahead,blob);
    signature[0]=0;
    if(n<=0 || (size_t)n+2+PEER_SIGNATURE_HEX_MAX>=sizeof(text) || !peer_crypto->sign(text,(size_t)n,signature,sizeof(signature))) return 0;
    size_t s=0;
    while(s<sizeof(signature) && signature[s]) ++s;
    if(s>PEER_SIGNATURE_HEX_MAX) return 0;
    text[n]='|';memcpy(text+n+1,signature,s);
    return peer_crypto->seal(text,(size_t)n+1+s,peer_scratch,sizeof(peer_scratch));
}
/* Own proposal sealed with tick, after its report left: kept for T = tick+1+k, sent once to
 * every routed peer. The first call opens the peer socket (failure: relay-only, logged). */
static unsigned peer_sealed(uint64_t tick,const char *blob) {
    if(!peer_on()) return 0;
    if(peer_state==PEER_HELLO && peer_socket==INVALID_SOCKET && !peer_open_socket()) peer_state=PEER_FAILED;
    peer_entry *e=peer_entry_at(tick+1+wire_lookahead,1);
    size_t length=strlen(blob);
    memcpy(e->blobs[stats.slot-1],blob,length+1);e->have|=1u<<(stats.slot-1);peer_complete(e);
    if(peer_state==PEER_ACTIVE) {
        if(peer_pending) peer_poll_routes();
        size_t sealed=peer_wc()?peer_wc_seal(tick,blob):peer_text_seal(tick,blob);
        for(unsigned i=0;sealed && i<wire_clients;i++)
            if(peer_routed&(1u<<i)) {peer_send(&peer_routes[i],peer_scratch,sealed);++peer_sent;}
    }
    return peer_poll(GetTickCount64());
}
/* While waiting (instead of the 25 ms receive on the relay connection alone): up to ms for
 * either socket, then the peer socket is drained. 0 timeout, 1 readable, -1 error. */
static int peer_block(unsigned ms,unsigned *reason) {
    fd_set set;FD_ZERO(&set);FD_SET(connection,&set);FD_SET(peer_socket,&set);
    struct timeval wait;wait.tv_sec=(long)(ms/1000u);wait.tv_usec=(long)(ms%1000u)*1000;
    SOCKET top=connection>peer_socket?connection:peer_socket;
    int ready=select((int)(top+1),&set,NULL,NULL,&wait);
    *reason=0;
    if(ready<0) {int error=WSAGetLastError();return error==WSAEWOULDBLOCK||error==WSAETIMEDOUT?0:-1;}
    if(ready) *reason=peer_drain();
    return ready>0;
}
/* Any tick, on its permit or on peer records, at most m ticks past F. */
static int peer_holds(uint64_t tick) {return peer_on() && tick>peer_final+(uint64_t)peer_window;}
/* All N records of tick-1-k without the relay's permit become s.inputs. */
static int peer_take(uint64_t tick) {
    if(peer_state!=PEER_ACTIVE || peer_final<1 || peer_holds(tick) || tick>peer_last) return 0;
    peer_entry *e=peer_entry_at(tick,0);
    if(!e || e->final || e->have!=peer_all()) return 0;
    for(unsigned i=0;i<wire_clients;i++) memcpy(blob_inputs[i],e->blobs[i],strlen(e->blobs[i])+1);
    e->ran=1;++peer_runs;
    if(tick-peer_final>peer_behind_max) peer_behind_max=tick-peer_final;
    return 1;
}
/* The relay's permit for tick (every blob, own proposal already checked), before or after
 * the tick ran: 1 ok, -3 another copy of a final permit (103), -4 a peer copy differs (104). */
static int peer_permit(uint64_t tick,char blobs[][GATE_BLOB_HEX_MAX+1]) {
    if(!peer_on()) return 1;
    peer_entry *e=peer_entry_at(tick,tick>peer_final);
    if(!e) return 1;
    if(e->final) {
        for(unsigned i=0;i<wire_clients;i++) if(strcmp(e->blobs[i],blobs[i])) {peer_notify(LOOKAHEAD_STOP_CONFLICT);return -3;}
        return 1;
    }
    for(unsigned i=0;i<wire_clients;i++) if((e->have&(1u<<i)) && strcmp(e->blobs[i],blobs[i])) {peer_notify(PEER_STOP_MISMATCH);return -4;}
    for(unsigned i=0;i<wire_clients;i++) memcpy(e->blobs[i],blobs[i],strlen(blobs[i])+1);
    e->have=peer_all();e->final=1;e->permit_us=peer_now_us();
    if(e->complete_us) {++peer_first;if(e->permit_us>e->complete_us) peer_lead_us+=(uint64_t)(e->permit_us-e->complete_us);}
    else ++peer_relay_first;
    for(;;) {
        peer_entry *following=peer_entry_at(peer_final+1,0);
        if(!following || !following->final) break;
        ++peer_final;
    }
    return 1;
}
/* Reports to resend while waiting: from F-k (the report that releases the missing permit
 * F+1) when that is older than the protocol 4 window; k + m <= 8 keeps it in the ring. */
static uint64_t peer_resend_from(uint64_t from) {
    if(!peer_on()) return from;
    uint64_t first=peer_final>wire_lookahead?peer_final-wire_lookahead:0;
    return first<from?first:from;
}
static int peer_status(char *out,size_t capacity) {
    static const char *const names[]={"off","hello","active","refused","failed"};
    int n=snprintf(out,capacity,"peer=%s window=%d sent=%llu received=%llu peer_runs=%llu peer_first=%llu relay_first=%llu lead_us=%llu "
        "behind_max=%llu final=%llu last=%llu dropped=%llu duplicates=%llu stale=%llu unreachable=%llu crypto=%s nat=%s forged=%llu",
        names[peer_state<=PEER_FAILED?peer_state:0],peer_window,(unsigned long long)peer_sent,(unsigned long long)peer_received,
        (unsigned long long)peer_runs,(unsigned long long)peer_first,(unsigned long long)peer_relay_first,(unsigned long long)peer_lead_us,
        (unsigned long long)peer_behind_max,(unsigned long long)peer_final,(unsigned long long)peer_last,(unsigned long long)peer_dropped,
        (unsigned long long)peer_duplicates,
        (unsigned long long)peer_stale,(unsigned long long)peer_unreachable,peer_wc()?"wire":peer_crypto->name,peer_nat->name,
        (unsigned long long)peer_forged);
    return n>0 && (size_t)n<capacity?n:0;
}
