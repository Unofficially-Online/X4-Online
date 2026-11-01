/* NAT traversal core for the optional direct player links (roadmap B7): rendezvous
 * through the relay's NGNAT protocol 1 extension, UDP hole punching with a nonce
 * exchange, keep-alives and the per-peer decision "direct path" or "use the relay".
 *
 * Sans-IO and portable C11: no sockets, clocks, threads or random numbers in here.
 * The caller passes the time (milliseconds, any monotonic origin), every received
 * datagram and a send callback; lockstep_nat_net.h is the winsock/BSD socket wrapper.
 * Default off: nothing runs unless a caller creates a core. Fail closed: a peer is
 * DIRECT only after an authenticated nonce round trip over that exact address; every
 * other outcome (no rendezvous, unknown peer, timeout, symmetric pair, lost path,
 * closed peer) answers RELAY. Wire, keys and the reflector rules:
 * tools/dev/native_gate_rendezvous.py and reports/LOCKSTEP-NAT-TRAVERSAL-20261101.txt.
 * Original code. */
#ifndef LOCKSTEP_NAT_H
#define LOCKSTEP_NAT_H
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "lockstep_sha256.h"

#define NAT_PEERS_MAX 8u           /* slots 1..8, GATE_CLIENTS_MAX */
#define NAT_CANDIDATES_MAX 6u
#define NAT_REQUEST_MIN 256u       /* rendezvous requests are padded to this; replies are never larger */
#define NAT_DATAGRAM_MAX 512u
#define NAT_FIELDS_MAX 20u
#define NAT_NONCES 32u             /* outstanding probe nonces per peer (round trip measurement) */
#define NAT_SEEN 32u               /* received probe nonces per peer (replay refusal) */
#define NAT_VALID 4u               /* addresses that delivered authenticated messages per peer */

typedef struct {uint32_t ip; uint16_t port;} nat_addr;   /* host byte order; ip 0 = none */
typedef void (*nat_send_fn)(void *context,nat_addr to,const char *data,size_t length);

enum {NAT_PATH_PENDING=0,NAT_PATH_DIRECT=1,NAT_PATH_RELAY=2};
enum {NAT_MAP_UNKNOWN=0,NAT_MAP_OPEN=1,NAT_MAP_EIM=2,NAT_MAP_EDM=3};
enum {NAT_FILTER_UNKNOWN=0,NAT_FILTER_OPEN=1,NAT_FILTER_PORT=2};
/* Candidate kinds in rising preference. */
enum {NAT_CAND_PRFLX=1,NAT_CAND_PUBLIC=2,NAT_CAND_MAPPED=3,NAT_CAND_LAN=4};
enum {NAT_PEER_OFF=0,NAT_PEER_ASKING,NAT_PEER_PROBING,NAT_PEER_DIRECT,NAT_PEER_RELAY};

static const char *const nat_mapping_names[]={"unknown","open","eim","edm"};
static const char *const nat_filter_names[]={"unknown","open","port"};

typedef struct {
    unsigned slot,clients;          /* own slot 1..clients, clients 2..8 */
    char token[33];                 /* the session token (32 lowercase hex); keys the rendezvous MAC */
    unsigned char instance[8];      /* random per process start (the caller's OS random source) */
    nat_addr server[2];             /* the relay's rendezvous ports P and P+1 */
    nat_addr lan;                   /* own local address:port of the link socket, or none */
    nat_addr mapped;                /* external address:port from PCP/NAT-PMP/UPnP, or none */
    unsigned want;                  /* bit k: find a path to slot k (0: every other slot) */
    int share_lan;                  /* publish lan; the relay passes it only to peers with the same public IP */
    int allow_loopback;             /* tests only: 127/8 candidates */
    uint32_t hello_ms,classify_ms,ask_ms,ask_timeout_ms,probe_ms,punch_ms,upgrade_ms,
             keepalive_ms,keepalive_fast_ms,dead_ms,retry_ms,refresh_ms;
    unsigned attempts_max;
} nat_config;

typedef struct {nat_addr addr;unsigned kind;int confirmed;} nat_candidate;

typedef struct {
    unsigned state,attempts,mapping,filter,ncand,nvalid;
    int decided,keyed,selected;
    uint64_t ask_since,next_ask,punch_until,next_probe,upgrade_until,last_heard,next_keepalive,retry_at,direct_since;
    unsigned char instance[8],key[16];
    nat_candidate cand[NAT_CANDIDATES_MAX];
    nat_addr path,valid[NAT_VALID];
    struct {uint64_t nonce,sent;} out[NAT_NONCES];
    uint64_t seen[NAT_SEEN];
    unsigned out_next,seen_next,nvalid_next;
    uint32_t rtt_ms;
    uint64_t probes_sent,acks_received,paths_lost;
    const char *reason;
} nat_peer;

typedef struct {
    nat_config cfg;
    char instance_hex[17];
    unsigned char rkey[32];
    uint64_t seq,nonce_counter,start_ms,next_hello,classify_until,final_hello_seq;
    nat_addr seen[2];
    int seen_ok[2],ftest_ok,alt_contacted,classified,registered,closed,started;
    unsigned mapping,filter;
    nat_peer peer[NAT_PEERS_MAX+1];
} nat_core;

/* ---- primitives ---- */
static inline void nat_hmac(const unsigned char *key,size_t key_length,const void *data,size_t length,unsigned char out[32]) {
    unsigned char k[64],pad[64];lockstep_sha256_ctx c;
    memset(k,0,sizeof(k));
    if(key_length>64) lockstep_sha256(key,key_length,k); else memcpy(k,key,key_length);
    for(unsigned i=0;i<64;i++) pad[i]=(unsigned char)(k[i]^0x36u);
    lockstep_sha256_init(&c);lockstep_sha256_update(&c,pad,64);lockstep_sha256_update(&c,data,length);lockstep_sha256_final(&c,out);
    for(unsigned i=0;i<64;i++) pad[i]=(unsigned char)(k[i]^0x5cu);
    lockstep_sha256_init(&c);lockstep_sha256_update(&c,pad,64);lockstep_sha256_update(&c,out,32);lockstep_sha256_final(&c,out);
}
static inline void nat_hex(const unsigned char *bytes,size_t length,char *out) {
    static const char digits[]="0123456789abcdef";
    for(size_t i=0;i<length;i++) {out[2*i]=digits[bytes[i]>>4];out[2*i+1]=digits[bytes[i]&15];}
    out[2*length]=0;
}
static inline int nat_unhex(const char *text,unsigned char *out,size_t length) {
    if(strlen(text)!=2*length) return 0;
    for(size_t i=0;i<2*length;i++) {
        char c=text[i];unsigned v=c>='0'&&c<='9'?(unsigned)(c-'0'):c>='a'&&c<='f'?(unsigned)(c-'a'+10):16u;
        if(v>15) return 0;
        if(i&1) out[i/2]=(unsigned char)(out[i/2]|v); else out[i/2]=(unsigned char)(v<<4);
    }
    return 1;
}
/* Canonical decimal: digits only, no sign, no leading zero, at most max. */
static inline int nat_uint(const char *text,uint64_t max,uint64_t *out) {
    uint64_t v=0;size_t n=strlen(text);
    if(!n||n>15||(n>1&&text[0]=='0')) return 0;
    for(size_t i=0;i<n;i++) {if(text[i]<'0'||text[i]>'9') return 0;v=v*10+(uint64_t)(text[i]-'0');}
    if(v>max) return 0;
    *out=v;return 1;
}
static inline int nat_addr_eq(nat_addr a,nat_addr b) {return a.ip==b.ip&&a.port==b.port;}
static inline int nat_addr_format(nat_addr a,char *out,size_t capacity) {
    if(!a.ip) return snprintf(out,capacity,"-");
    return snprintf(out,capacity,"%u.%u.%u.%u:%u",(unsigned)(a.ip>>24),(unsigned)(a.ip>>16)&255u,
                    (unsigned)(a.ip>>8)&255u,(unsigned)a.ip&255u,(unsigned)a.port);
}
/* "a.b.c.d:port" with canonical numbers, or "-" for none. */
static inline int nat_addr_parse(const char *text,nat_addr *out) {
    if(!strcmp(text,"-")) {out->ip=0;out->port=0;return 1;}
    char part[8];uint32_t ip=0;uint64_t v;const char *p=text;
    for(unsigned i=0;i<5;i++) {
        const char *end=p;while(*end>='0'&&*end<='9') end++;
        size_t n=(size_t)(end-p);if(!n||n>=sizeof(part)) return 0;
        memcpy(part,p,n);part[n]=0;
        if(i<4) {if(!nat_uint(part,255,&v)||*end!=(i<3?'.':':')) return 0;ip=ip<<8|(uint32_t)v;}
        else if(!nat_uint(part,65535,&v)||!v||*end) return 0;
        p=end+1;
    }
    if(!ip) return 0;
    out->ip=ip;out->port=(uint16_t)v;return 1;
}
static inline int nat_ip_private(uint32_t ip) {
    return (ip>>24)==10||(ip>>20)==(172u<<4|1u)||(ip>>16)==(192u<<8|168u)||(ip>>22)==(100u<<2|1u)||(ip>>16)==(169u<<8|254u);
}
static inline int nat_ip_loopback(uint32_t ip) {return (ip>>24)==127;}
/* Never a probe target: unspecified, this-network, multicast, reserved and broadcast. */
static inline int nat_ip_unusable(uint32_t ip) {return !ip||(ip>>24)==0||(ip>>28)>=14;}

/* ---- messages: NGNAT|1|TYPE|...|mac, mac = first 16 bytes of HMAC-SHA256(key, everything before "|mac") ---- */
typedef struct {char data[NAT_DATAGRAM_MAX+1];size_t length;int bad;} nat_msg;
static inline void nat_msg_start(nat_msg *m,const char *type) {
    m->bad=0;m->length=(size_t)snprintf(m->data,sizeof(m->data),"NGNAT|1|%s",type);
}
static inline void nat_msg_addf(nat_msg *m,const char *format,...) {
    va_list list;va_start(list,format);
    if(m->length+1>=NAT_DATAGRAM_MAX) m->bad=1;
    else {
        m->data[m->length++]='|';
        int n=vsnprintf(m->data+m->length,NAT_DATAGRAM_MAX-m->length,format,list);
        if(n<0||(size_t)n>=NAT_DATAGRAM_MAX-m->length) m->bad=1; else m->length+=(size_t)n;
    }
    va_end(list);
}
/* pad_to: rendezvous requests carry a '.' pad field so that the whole datagram is pad_to bytes. */
static inline int nat_msg_seal(nat_msg *m,const unsigned char *key,size_t key_length,size_t pad_to) {
    if(pad_to) {
        size_t tail=1+1+32;   /* "|" pad "|" mac */
        if(m->length+tail>pad_to) {m->bad=1;return 0;}
        size_t pad=pad_to-m->length-tail;
        m->data[m->length++]='|';memset(m->data+m->length,'.',pad);m->length+=pad;
    }
    if(m->bad||m->length+33>NAT_DATAGRAM_MAX) {m->bad=1;return 0;}
    unsigned char mac[32];nat_hmac(key,key_length,m->data,m->length,mac);
    m->data[m->length++]='|';nat_hex(mac,16,m->data+m->length);m->length+=32;
    return 1;
}
typedef struct {char buffer[NAT_DATAGRAM_MAX+1];char *f[NAT_FIELDS_MAX];unsigned n;} nat_fields;
static inline int nat_split(const char *data,size_t length,nat_fields *out) {
    if(length<6||length>NAT_DATAGRAM_MAX||memcmp(data,"NGNAT|",6)) return 0;
    memcpy(out->buffer,data,length);out->buffer[length]=0;
    if(strlen(out->buffer)!=length) return 0;
    out->n=0;char *p=out->buffer;
    for(;;) {
        if(out->n==NAT_FIELDS_MAX) return 0;
        out->f[out->n++]=p;
        char *bar=strchr(p,'|');if(!bar) break;
        *bar=0;p=bar+1;
    }
    return out->n>=4&&!strcmp(out->f[1],"1");
}
static inline int nat_verify(const char *data,size_t length,const unsigned char *key,size_t key_length) {
    unsigned char mac[32],got[16];
    if(length<34||data[length-33]!='|') return 0;
    char hex[33];memcpy(hex,data+length-32,32);hex[32]=0;
    if(!nat_unhex(hex,got,16)) return 0;
    nat_hmac(key,key_length,data,length-33,mac);
    unsigned diff=0;for(unsigned i=0;i<16;i++) diff|=(unsigned)(mac[i]^got[i]);
    return !diff;
}

/* ---- configuration ---- */
static inline void nat_config_defaults(nat_config *c) {
    memset(c,0,sizeof(*c));
    c->share_lan=1;
    c->hello_ms=250;c->classify_ms=2000;c->ask_ms=250;c->ask_timeout_ms=15000;
    c->probe_ms=100;c->punch_ms=6000;c->upgrade_ms=600;
    c->keepalive_ms=15000;c->keepalive_fast_ms=1000;c->dead_ms=30000;
    c->retry_ms=30000;c->refresh_ms=30000;c->attempts_max=3;
}
/* 0 or a refusal reason; a refused core stays closed and every path answers RELAY. */
static inline const char *nat_core_init(nat_core *n,const nat_config *c) {
    memset(n,0,sizeof(*n));n->closed=1;
    if(c->clients<2||c->clients>NAT_PEERS_MAX||c->slot<1||c->slot>c->clients) return "slot";
    if(strlen(c->token)!=32) return "token";
    unsigned char token[16];if(!nat_unhex(c->token,token,16)) return "token";
    for(unsigned i=0;i<2;i++) if(!c->server[i].ip||!c->server[i].port) return "server";
    if(nat_addr_eq(c->server[0],c->server[1])) return "server";
    static const unsigned char zero[8];if(!memcmp(c->instance,zero,8)) return "instance";
    if(!c->hello_ms||!c->ask_ms||!c->probe_ms||!c->punch_ms||!c->keepalive_ms||!c->keepalive_fast_ms||
       !c->dead_ms||!c->retry_ms||!c->refresh_ms||c->dead_ms<=c->keepalive_fast_ms) return "timing";
    n->cfg=*c;
    if(!n->cfg.want) n->cfg.want=((1u<<(c->clients+1))-2u)&~(1u<<c->slot);
    n->cfg.want&=((1u<<(c->clients+1))-2u)&~(1u<<c->slot);
    nat_hex(c->instance,8,n->instance_hex);
    static const char label[]="NGNAT|1|rendezvous";
    nat_hmac((const unsigned char*)c->token,32,label,sizeof(label)-1,n->rkey);
    n->closed=0;return 0;
}
static inline int nat_wanted(const nat_core *n,unsigned k) {return k>=1&&k<=n->cfg.clients&&(n->cfg.want>>k&1u);}

/* ---- sending ---- */
static inline void nat_request(nat_core *n,unsigned index,const char *type,const char *extra,nat_send_fn send,void *context) {
    nat_msg m;nat_msg_start(&m,type);
    nat_msg_addf(&m,"%u",n->cfg.slot);nat_msg_addf(&m,"%s",n->instance_hex);nat_msg_addf(&m,"%llu",(unsigned long long)++n->seq);
    if(extra) nat_msg_addf(&m,"%s",extra);
    if(nat_msg_seal(&m,n->rkey,32,NAT_REQUEST_MIN)) send(context,n->cfg.server[index],m.data,m.length);
}
static inline void nat_send_hello(nat_core *n,unsigned index,nat_send_fn send,void *context) {
    char lan[24],mapped[24],extra[96];
    nat_addr_format(n->cfg.share_lan?n->cfg.lan:(nat_addr){0,0},lan,sizeof(lan));nat_addr_format(n->cfg.mapped,mapped,sizeof(mapped));
    snprintf(extra,sizeof(extra),"%s|%s|%s|%s",lan,mapped,nat_mapping_names[n->mapping],nat_filter_names[n->filter]);
    nat_request(n,index,"HELLO",extra,send,context);
}
static inline uint64_t nat_next_nonce(nat_core *n) {
    unsigned char input[24],mac[32];uint64_t v=0;
    memcpy(input,"nonce|",6);memcpy(input+6,n->cfg.instance,8);
    uint64_t c=++n->nonce_counter;for(unsigned i=0;i<8;i++) input[14+i]=(unsigned char)(c>>(8*i));
    nat_hmac(n->rkey,32,input,22,mac);
    for(unsigned i=0;i<8;i++) v=v<<8|mac[i];
    return v?v:1;
}
static inline void nat_peer_message(nat_core *n,unsigned k,const char *type,uint64_t value,nat_addr to,nat_send_fn send,void *context) {
    nat_peer *p=&n->peer[k];char peer_inst[17];nat_hex(p->instance,8,peer_inst);
    nat_msg m;nat_msg_start(&m,type);
    nat_msg_addf(&m,"%u",n->cfg.slot);nat_msg_addf(&m,"%u",k);
    nat_msg_addf(&m,"%s",n->instance_hex);nat_msg_addf(&m,"%s",peer_inst);
    nat_msg_addf(&m,"%016llx",(unsigned long long)value);
    if(nat_msg_seal(&m,p->key,16,0)) send(context,to,m.data,m.length);
}
static inline void nat_probe(nat_core *n,unsigned k,nat_addr to,uint64_t now,nat_send_fn send,void *context) {
    nat_peer *p=&n->peer[k];uint64_t nonce=nat_next_nonce(n);
    p->out[p->out_next].nonce=nonce;p->out[p->out_next].sent=now;p->out_next=(p->out_next+1)%NAT_NONCES;
    ++p->probes_sent;
    nat_peer_message(n,k,"PROBE",nonce,to,send,context);
}

/* ---- peer state ---- */
static inline int nat_candidate_ok(const nat_core *n,nat_addr a,unsigned kind) {
    if(!a.ip||!a.port||nat_ip_unusable(a.ip)) return 0;
    if(nat_ip_loopback(a.ip)) return n->cfg.allow_loopback;
    if(kind==NAT_CAND_LAN) return nat_ip_private(a.ip);
    if(kind==NAT_CAND_MAPPED) return !nat_ip_private(a.ip)||n->cfg.allow_loopback;
    return 1;
}
static inline int nat_candidate_add(nat_core *n,nat_peer *p,nat_addr a,unsigned kind) {
    for(unsigned i=0;i<p->ncand;i++) if(nat_addr_eq(p->cand[i].addr,a)) {if(kind>p->cand[i].kind) p->cand[i].kind=kind;return (int)i;}
    if(!nat_candidate_ok(n,a,kind)||p->ncand>=NAT_CANDIDATES_MAX) return -1;
    if(kind==NAT_CAND_PRFLX) {unsigned prflx=0;for(unsigned i=0;i<p->ncand;i++) prflx+=p->cand[i].kind==NAT_CAND_PRFLX;if(prflx>=2) return -1;}
    p->cand[p->ncand].addr=a;p->cand[p->ncand].kind=kind;p->cand[p->ncand].confirmed=0;
    return (int)p->ncand++;
}
static inline void nat_valid_add(nat_peer *p,nat_addr a) {
    for(unsigned i=0;i<p->nvalid;i++) if(nat_addr_eq(p->valid[i],a)) return;
    if(p->nvalid<NAT_VALID) p->valid[p->nvalid++]=a;
    else {p->valid[p->nvalid_next]=a;p->nvalid_next=(p->nvalid_next+1)%NAT_VALID;}
}
static inline void nat_relay(nat_peer *p,uint64_t now,const char *reason,int final,uint32_t retry_ms) {
    p->state=NAT_PEER_RELAY;p->decided=1;p->reason=reason;p->selected=-1;
    p->retry_at=final?UINT64_MAX:now+retry_ms;
}
/* Pairs that hole punching cannot connect without port prediction (see the report's table):
 * both mappings endpoint-dependent, or one endpoint-dependent against port-dependent filtering. */
static inline int nat_pair_unpunchable(unsigned my_map,unsigned my_filter,unsigned peer_map,unsigned peer_filter) {
    if(my_map==NAT_MAP_EDM&&peer_map==NAT_MAP_EDM) return 1;
    if(my_map==NAT_MAP_EDM&&peer_filter==NAT_FILTER_PORT) return 1;
    if(peer_map==NAT_MAP_EDM&&my_filter==NAT_FILTER_PORT) return 1;
    return 0;
}
static inline void nat_classify(nat_core *n,uint64_t now) {
    n->classified=1;n->next_hello=now;
    if(n->seen_ok[0]&&n->cfg.lan.ip&&nat_addr_eq(n->seen[0],n->cfg.lan)) n->mapping=NAT_MAP_OPEN;
    else if(n->seen_ok[0]&&n->seen_ok[1]) n->mapping=nat_addr_eq(n->seen[0],n->seen[1])?NAT_MAP_EIM:NAT_MAP_EDM;
    else n->mapping=NAT_MAP_UNKNOWN;
    n->filter=n->ftest_ok?NAT_FILTER_OPEN:n->seen_ok[0]?NAT_FILTER_PORT:NAT_FILTER_UNKNOWN;
}

/* ---- input ---- */
static inline void nat_on_peer_info(nat_core *n,nat_fields *m,uint64_t now) {
    uint64_t k;unsigned char inst[8],key[16];nat_addr pub,lan,mapped;unsigned map=NAT_MAP_UNKNOWN,filter=NAT_FILTER_UNKNOWN;
    if(m->n!=15||!nat_uint(m->f[6],NAT_PEERS_MAX,&k)||!nat_wanted(n,(unsigned)k)||!nat_unhex(m->f[7],inst,8)||
       !nat_addr_parse(m->f[10],&pub)||!nat_addr_parse(m->f[11],&lan)||!nat_addr_parse(m->f[12],&mapped)||!nat_unhex(m->f[13],key,16)) return;
    for(unsigned i=0;i<4;i++) if(!strcmp(m->f[8],nat_mapping_names[i])) map=i;
    for(unsigned i=0;i<3;i++) if(!strcmp(m->f[9],nat_filter_names[i])) filter=i;
    nat_peer *p=&n->peer[k];
    if(p->state!=NAT_PEER_ASKING) return;
    /* The peer is still classifying: ask again (both sides then take the same decision). */
    if((map==NAT_MAP_UNKNOWN||filter==NAT_FILTER_UNKNOWN)&&now-p->ask_since<n->cfg.classify_ms) return;
    if(p->keyed&&memcmp(p->instance,inst,8)) {p->nvalid=0;p->rtt_ms=0;}   /* the peer restarted */
    memcpy(p->instance,inst,8);memcpy(p->key,key,16);p->keyed=1;p->mapping=map;p->filter=filter;
    p->ncand=0;p->selected=-1;
    if(lan.ip) nat_candidate_add(n,p,lan,NAT_CAND_LAN);
    if(mapped.ip) nat_candidate_add(n,p,mapped,NAT_CAND_MAPPED);
    if(pub.ip) nat_candidate_add(n,p,pub,NAT_CAND_PUBLIC);
    int lan_known=0;for(unsigned i=0;i<p->ncand;i++) lan_known|=p->cand[i].kind==NAT_CAND_LAN;
    ++p->attempts;
    if(!p->ncand) {nat_relay(p,now,"no_candidates",p->attempts>=n->cfg.attempts_max,n->cfg.retry_ms);return;}
    if(!lan_known&&!n->cfg.mapped.ip&&!mapped.ip&&nat_pair_unpunchable(n->mapping,n->filter,map,filter)) {
        nat_relay(p,now,"unpunchable_nat_pair",1,0);return;
    }
    p->state=NAT_PEER_PROBING;p->punch_until=now+n->cfg.punch_ms;p->next_probe=now;
}
static inline void nat_on_rendezvous(nat_core *n,nat_fields *m,nat_addr from,uint64_t now) {
    uint64_t slot,seq,index;const char *type=m->f[2];
    if(m->n<7||!nat_uint(m->f[3],NAT_PEERS_MAX,&slot)||slot!=n->cfg.slot||strcmp(m->f[4],n->instance_hex)||
       !nat_uint(m->f[5],UINT64_C(999999999999999),&seq)||!seq||seq>n->seq) return;
    if(!strcmp(type,"SEEN")) {
        nat_addr observed;
        if(m->n!=10||!nat_uint(m->f[6],1,&index)||!nat_addr_eq(from,n->cfg.server[index])||!nat_addr_parse(m->f[7],&observed)||!observed.ip) return;
        n->seen[index]=observed;n->seen_ok[index]=1;
        if(index==0&&n->classified&&seq>=n->final_hello_seq&&!n->registered) {n->registered=1;n->next_hello=now+n->cfg.refresh_ms;}
    }
    else if(!strcmp(type,"FSEEN")) {
        /* The point of the filter test: the reply leaves the relay's second port, to which this
         * socket never sent. Arriving at all means filtering does not depend on the port. */
        if(m->n==7&&nat_addr_eq(from,n->cfg.server[1])&&!n->alt_contacted) n->ftest_ok=1;
    }
    else if(!strcmp(type,"PEER")) {if(nat_addr_eq(from,n->cfg.server[0])) nat_on_peer_info(n,m,now);}
}
static inline int nat_nonce_parse(const char *text,uint64_t *out) {
    unsigned char b[8];if(!nat_unhex(text,b,8)) return 0;
    uint64_t v=0;for(unsigned i=0;i<8;i++) v=v<<8|b[i];
    *out=v;return 1;
}
static inline void nat_on_peer(nat_core *n,nat_fields *m,const char *data,size_t length,nat_addr from,uint64_t now,nat_send_fn send,void *context) {
    uint64_t k,to,value;unsigned char inst[8],mine[8];const char *type=m->f[2];
    if(m->n!=9||!nat_uint(m->f[3],NAT_PEERS_MAX,&k)||!nat_wanted(n,(unsigned)k)||!nat_uint(m->f[4],NAT_PEERS_MAX,&to)||to!=n->cfg.slot) return;
    nat_peer *p=&n->peer[k];
    if(!p->keyed||!nat_unhex(m->f[5],inst,8)||memcmp(inst,p->instance,8)||!nat_unhex(m->f[6],mine,8)||memcmp(mine,n->cfg.instance,8)) return;
    if(!nat_nonce_parse(m->f[7],&value)||!nat_candidate_ok(n,from,NAT_CAND_PRFLX)||!nat_verify(data,length,p->key,16)) return;
    if(!strcmp(type,"PROBE")) {
        for(unsigned i=0;i<NAT_SEEN;i++) if(p->seen[i]==value) return;   /* a replay gets no answer */
        p->seen[p->seen_next]=value;p->seen_next=(p->seen_next+1)%NAT_SEEN;
        nat_valid_add(p,from);
        if(p->state==NAT_PEER_DIRECT&&nat_addr_eq(from,p->path)) p->last_heard=now;
        if(p->state==NAT_PEER_RELAY&&p->retry_at!=UINT64_MAX) {
            /* The peer still punches (its rendezvous came later): punch again at once. */
            p->state=NAT_PEER_PROBING;p->punch_until=now+n->cfg.punch_ms;p->next_probe=now;p->selected=-1;
        }
        if(p->state==NAT_PEER_PROBING) nat_candidate_add(n,p,from,NAT_CAND_PRFLX);   /* peer-reflexive */
        if(p->state==NAT_PEER_PROBING||p->state==NAT_PEER_DIRECT) nat_peer_message(n,(unsigned)k,"ACK",value,from,send,context);
        return;
    }
    if(!strcmp(type,"ACK")) {
        unsigned i;for(i=0;i<NAT_NONCES;i++) if(p->out[i].nonce==value) break;
        if(i==NAT_NONCES) return;
        uint64_t sent=p->out[i].sent;p->out[i].nonce=0;
        if(p->state!=NAT_PEER_PROBING&&p->state!=NAT_PEER_DIRECT) return;
        ++p->acks_received;p->rtt_ms=(uint32_t)(now-sent);p->last_heard=now;nat_valid_add(p,from);
        int c=nat_candidate_add(n,p,from,NAT_CAND_PRFLX);
        if(c<0) {for(unsigned j=0;j<p->ncand;j++) if(nat_addr_eq(p->cand[j].addr,from)) c=(int)j;}
        unsigned kind=c>=0?p->cand[c].kind:NAT_CAND_PRFLX;
        if(c>=0) p->cand[c].confirmed=1;
        unsigned current=p->selected>=0?p->cand[p->selected].kind:0;
        if(p->state==NAT_PEER_PROBING||(c>=0&&kind>current&&now<p->upgrade_until)) {
            if(p->state==NAT_PEER_PROBING) {
                p->state=NAT_PEER_DIRECT;p->decided=1;p->reason="direct";p->attempts=0;p->direct_since=now;
                p->upgrade_until=now+n->cfg.upgrade_ms;p->next_keepalive=now+n->cfg.keepalive_ms;
            }
            if(c>=0&&kind>=current) {p->selected=c;p->path=from;}
            else if(p->selected<0) p->path=from;
        }
        return;
    }
    if(!strcmp(type,"CLOSE")&&p->state!=NAT_PEER_OFF) nat_relay(p,now,"peer_closed",1,0);
}
/* 1: an NGNAT datagram (handled or refused here); 0: not ours, the caller's own traffic. */
static inline int nat_core_input(nat_core *n,uint64_t now,nat_addr from,const char *data,size_t length,nat_send_fn send,void *context) {
    if(length<6||memcmp(data,"NGNAT|",6)) return 0;
    nat_fields m;
    if(n->closed||!nat_split(data,length,&m)) return 1;
    const char *type=m.f[2];
    if(!strcmp(type,"SEEN")||!strcmp(type,"FSEEN")||!strcmp(type,"PEER")||!strcmp(type,"WAIT")||!strcmp(type,"GONE")) {
        if((nat_addr_eq(from,n->cfg.server[0])||nat_addr_eq(from,n->cfg.server[1]))&&nat_verify(data,length,n->rkey,32))
            nat_on_rendezvous(n,&m,from,now);
    }
    else if(!strcmp(type,"PROBE")||!strcmp(type,"ACK")||!strcmp(type,"CLOSE")) nat_on_peer(n,&m,data,length,from,now,send,context);
    return 1;
}

/* ---- timers ---- */
static inline uint64_t nat_min(uint64_t a,uint64_t b) {return a<b?a:b;}
/* Sends what is due at now; returns the time of the next due action (UINT64_MAX: none). */
static inline uint64_t nat_core_poll(nat_core *n,uint64_t now,nat_send_fn send,void *context) {
    if(n->closed) return UINT64_MAX;
    const nat_config *c=&n->cfg;
    if(!n->started) {n->started=1;n->start_ms=now;n->classify_until=now+c->classify_ms;n->next_hello=now;}
    /* Classification: first the filtering test (FTEST to P, FSEEN comes back from P+1), then the
     * mapping test (HELLO to P+1). In this order a HELLO to P+1 cannot have opened the filter
     * that FSEEN has to pass; an FSEEN after the first datagram to P+1 is ignored. */
    uint64_t filter_until=n->start_ms+c->classify_ms/2;
    int filter_phase=!n->ftest_ok&&now<filter_until;
    if(!n->classified&&(now>=n->classify_until||(n->seen_ok[0]&&n->seen_ok[1]&&!filter_phase))) nat_classify(n,now);
    if(now>=n->next_hello||(!n->classified&&!filter_phase&&!n->alt_contacted)) {
        if(!n->classified) {
            if(!n->seen_ok[0]) nat_send_hello(n,0,send,context);
            if(filter_phase) nat_request(n,0,"FTEST",NULL,send,context);
            else if(!n->seen_ok[1]) {n->alt_contacted=1;nat_send_hello(n,1,send,context);}
            n->next_hello=now+c->hello_ms;
            if(filter_phase&&filter_until<n->next_hello) n->next_hello=filter_until;
        } else {
            nat_send_hello(n,0,send,context);
            if(!n->registered&&!n->final_hello_seq) n->final_hello_seq=n->seq;
            /* An unreachable relay is asked less often once the peers have been told RELAY. */
            n->next_hello=now+(n->registered||now-n->start_ms>=c->ask_timeout_ms?c->refresh_ms:c->hello_ms);
        }
    }
    uint64_t due=n->next_hello;
    if(!n->classified) due=nat_min(due,n->classify_until);
    for(unsigned k=1;k<=c->clients;k++) {
        if(!nat_wanted(n,k)) continue;
        nat_peer *p=&n->peer[k];
        if(p->state==NAT_PEER_OFF&&n->classified&&n->seen_ok[0]) {p->state=NAT_PEER_ASKING;p->ask_since=now;p->next_ask=now;}
        if(p->state==NAT_PEER_OFF) {
            if(now-n->start_ms>=c->ask_timeout_ms) {++p->attempts;nat_relay(p,now,"no_rendezvous",p->attempts>=c->attempts_max,c->retry_ms);}
            else due=nat_min(due,n->start_ms+c->ask_timeout_ms);
        }
        if(p->state==NAT_PEER_RELAY&&now>=p->retry_at) {
            p->state=NAT_PEER_ASKING;p->ask_since=now;p->next_ask=now;n->next_hello=now;due=now;
        }
        if(p->state==NAT_PEER_ASKING) {
            if(now-p->ask_since>=c->ask_timeout_ms) {
                ++p->attempts;nat_relay(p,now,"peer_not_registered",p->attempts>=c->attempts_max,c->retry_ms);
            } else if(now>=p->next_ask) {
                char extra[12];snprintf(extra,sizeof(extra),"%u",k);   /* room for any unsigned (gcc -O2 format-truncation) */
                nat_request(n,0,"ASK",extra,send,context);p->next_ask=now+c->ask_ms;
            }
            if(p->state==NAT_PEER_ASKING) due=nat_min(due,nat_min(p->next_ask,p->ask_since+c->ask_timeout_ms));
        }
        if(p->state==NAT_PEER_PROBING) {
            if(now>=p->punch_until) nat_relay(p,now,"punch_timeout",p->attempts>=c->attempts_max,c->retry_ms);
            else {
                if(now>=p->next_probe) {
                    for(unsigned i=0;i<p->ncand;i++) nat_probe(n,k,p->cand[i].addr,now,send,context);
                    p->next_probe=now+c->probe_ms;
                }
                due=nat_min(due,nat_min(p->next_probe,p->punch_until));
            }
        }
        if(p->state==NAT_PEER_DIRECT) {
            if(now-p->last_heard>=c->dead_ms) {
                ++p->paths_lost;p->attempts=0;nat_relay(p,now,"path_lost",0,c->keepalive_fast_ms);
                due=nat_min(due,p->retry_at);continue;
            }
            /* Better candidates (LAN, mapped) confirmed shortly after the first path take over. */
            if(now<p->upgrade_until&&now>=p->next_probe) {
                unsigned current=p->selected>=0?p->cand[p->selected].kind:0;
                for(unsigned i=0;i<p->ncand;i++) if(p->cand[i].kind>current&&!p->cand[i].confirmed) nat_probe(n,k,p->cand[i].addr,now,send,context);
                p->next_probe=now+c->probe_ms;
            }
            if(now>=p->next_keepalive) {
                nat_probe(n,k,p->path,now,send,context);
                /* Quiet path: check consent every keepalive_fast_ms until dead_ms. */
                p->next_keepalive=now+(now-p->last_heard>=c->keepalive_ms?c->keepalive_fast_ms:c->keepalive_ms);
            }
            due=nat_min(due,nat_min(p->next_keepalive,p->last_heard+c->dead_ms));
            if(now<p->upgrade_until) due=nat_min(due,nat_min(p->next_probe,p->upgrade_until));
        }
        if(p->state==NAT_PEER_RELAY&&p->retry_at!=UINT64_MAX) due=nat_min(due,p->retry_at);
    }
    return due;
}

/* ---- queries for the peer link layer ---- */
/* The answer the direct input links ask for: DIRECT with the address to send to (from
 * this core's socket), RELAY ("unreachable, use the relay") or PENDING (still trying;
 * use the relay meanwhile). */
static inline int nat_core_path(const nat_core *n,unsigned k,nat_addr *path,uint32_t *rtt_ms) {
    if(n->closed||!nat_wanted(n,k)) return NAT_PATH_RELAY;
    const nat_peer *p=&n->peer[k];
    if(p->state==NAT_PEER_DIRECT) {if(path) *path=p->path;if(rtt_ms) *rtt_ms=p->rtt_ms;return NAT_PATH_DIRECT;}
    return p->decided?NAT_PATH_RELAY:NAT_PATH_PENDING;
}
static inline const char *nat_core_reason(const nat_core *n,unsigned k) {
    if(n->closed) return "closed";
    if(!nat_wanted(n,k)) return "not_wanted";
    return n->peer[k].reason?n->peer[k].reason:"pending";
}
/* The slot whose authenticated NGNAT messages came from this address (0: none). The
 * link layer still authenticates its own records (roadmap B8). */
static inline unsigned nat_core_peer_of(const nat_core *n,nat_addr from) {
    for(unsigned k=1;k<=n->cfg.clients;k++) {
        const nat_peer *p=&n->peer[k];
        if(!nat_wanted(n,k)||p->state!=NAT_PEER_DIRECT) continue;
        if(nat_addr_eq(p->path,from)) return k;
        for(unsigned i=0;i<p->nvalid;i++) if(nat_addr_eq(p->valid[i],from)) return k;
    }
    return 0;
}
/* The link layer received an authenticated record from slot k over the direct path. */
static inline void nat_core_alive(nat_core *n,unsigned k,uint64_t now) {
    if(nat_wanted(n,k)&&n->peer[k].state==NAT_PEER_DIRECT&&now>n->peer[k].last_heard) n->peer[k].last_heard=now;
}
/* A port mapping appeared, changed on renewal or was lost (ip 0): advertise it at once. */
static inline void nat_core_set_mapped(nat_core *n,nat_addr mapped,uint64_t now) {
    if(n->closed||nat_addr_eq(n->cfg.mapped,mapped)) return;
    n->cfg.mapped=mapped;
    if(n->started) n->next_hello=now;
}
/* Session end: tell direct peers and the relay, then answer RELAY for everyone. */
static inline void nat_core_close(nat_core *n,nat_send_fn send,void *context) {
    if(n->closed) return;
    for(unsigned k=1;k<=n->cfg.clients;k++) {
        nat_peer *p=&n->peer[k];
        if(nat_wanted(n,k)&&p->keyed&&(p->state==NAT_PEER_DIRECT||p->state==NAT_PEER_PROBING)) {
            if(p->state==NAT_PEER_DIRECT) nat_peer_message(n,k,"CLOSE",nat_next_nonce(n),p->path,send,context);
            else for(unsigned i=0;i<p->ncand;i++) nat_peer_message(n,k,"CLOSE",nat_next_nonce(n),p->cand[i].addr,send,context);
        }
    }
    if(n->seen_ok[0]) nat_request(n,0,"BYE",NULL,send,context);
    n->closed=1;
}
/* One line for logs: own classification and every wanted peer's state. */
static inline int nat_core_describe(const nat_core *n,char *out,size_t capacity) {
    static const char *const states[]={"off","asking","probing","direct","relay"};
    char a[24],b[24];nat_addr_format(n->seen[0],a,sizeof(a));nat_addr_format(n->seen[1],b,sizeof(b));
    size_t used=(size_t)snprintf(out,capacity,"slot=%u mapping=%s filter=%s public=%s alt=%s registered=%d",n->cfg.slot,
        nat_mapping_names[n->mapping],nat_filter_names[n->filter],a,b,n->registered);
    for(unsigned k=1;k<=n->cfg.clients&&used<capacity;k++) {
        if(!nat_wanted(n,k)) continue;
        const nat_peer *p=&n->peer[k];nat_addr_format(p->state==NAT_PEER_DIRECT?p->path:(nat_addr){0,0},a,sizeof(a));
        used+=(size_t)snprintf(out+used,capacity-used," peer%u=%s/%s/%s/rtt%u",k,states[p->state],nat_core_reason(n,k),a,(unsigned)p->rtt_ms);
    }
    return (int)used;
}
#endif
