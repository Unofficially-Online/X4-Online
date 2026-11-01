/* Opt-in wire crypto on the gate's relay path (gate.wire_crypto, default off;
 * lockstep_wire_crypto.h, docs/LOCKSTEP-NSLOT-WIRE.txt WIRE CRYPTO). Shared by
 * the Windows and Linux gates and included before lockstep_lookahead.h. Needs
 * from the gate: connection, secret, stats, wire_version, wire_clients,
 * wire_lookahead, blob_proposal, used, timeout_ms, GetTickCount64,
 * closed_window, WSAGetLastError and ft_us (lockstep_gate_timing.h).
 *
 * Unarmed, gate_wire_send/gate_wire_recv are send/recv unchanged. Armed (only
 * with protocol 3 or 4), the first gated frame runs the handshake (HELLO until
 * WELCOME, then NGKEYREQ until NGKEYS, both within the gate timeout, else stop
 * 107), every report carries a NUL and this client's Ed25519 signature of its
 * input record, every datagram is sealed, and every permit must carry a valid
 * signature for every other slot's record and the relay's permit signature.
 * Fail closed: a datagram that is not authentic or is a replay is dropped
 * (counted); an authentic datagram with invalid content (a bad signature, a
 * roster that lacks this client's key, an unexpected format) stops with 106. */
#include "lockstep_wire_crypto.h"
#define WCG_STOP_INVALID 106u
#define WCG_STOP_HANDSHAKE 107u
#define WCG_RING (GATE_LOOKAHEAD_MAX+1u)
#ifdef _WIN32
static int gate_crypto_random(void *out,size_t n) {
    return n<=0xffffffffu && BCryptGenRandom(NULL,(PUCHAR)out,(ULONG)n,BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0;
}
#else
#include <errno.h>
#include <sys/syscall.h>
static int gate_crypto_random(void *out,size_t n) {
    unsigned char *p=(unsigned char*)out;
    while(n) {
        long got=syscall(SYS_getrandom,p,n,0);
        if(got<0) {if(errno==EINTR) continue;return 0;}
        p+=got;n-=(size_t)got;
    }
    return 1;
}
#endif
static struct {
    int armed,established,keyed;unsigned stop;
    uint8_t sid[WC_SID],relay_pk[WC_KEY],psk[WC_KEY],credential[32],group[WC_KEY],roster[WC_SLOTS_MAX][WC_KEY],fingerprint[16];
    wc_client hs;wc_tx tx;wc_rx rx;
    uint64_t verified_tick[WCG_RING];int verified_used[WCG_RING];uint8_t verified_hash[WCG_RING][32];
    uint64_t hellos,keyreqs,reports,permits,cached,records,dropped,sign_us,verify_us,establish_ms;
} wcg;
static uint8_t wcg_in[GATE_PERMIT_MAX+WC_OVERHEAD];
static uint8_t wcg_out[GATE_STATE_MAX+WC_OVERHEAD];
/* gate.wire_crypto(credential): before start/boot, after gate.wire(3,N) (and
 * gate.lookahead). Once started, only the identical credential returns 1. */
static int wcg_configure(const char *credential,size_t n) {
    uint8_t pk[WC_KEY],psk[WC_KEY],hash[32];
    if(!wc_credential_parse(credential,n,pk,psk)) return 0;
    crypto_blake2b(hash,32,(const uint8_t*)credential,n);
    int ok;
    if(used) ok=wcg.armed && !crypto_verify32(hash,wcg.credential);
    else if(wire_version!=3 || wc_selftest()) ok=0; /* the RFC known answers before any key is used */
    else {memcpy(wcg.relay_pk,pk,WC_KEY);memcpy(wcg.psk,psk,WC_KEY);memcpy(wcg.credential,hash,32);wcg.armed=1;ok=1;}
    crypto_wipe(psk,sizeof(psk));
    return ok;
}
/* Unarmed: send unchanged. Armed: sealed (a refused seal stops with 106). */
static int gate_wire_send(const char *data,size_t n) {
    if(!wcg.armed) return (int)send(connection,data,(int)n,0);
    size_t size=wcg.established?wc_seal(&wcg.tx,wcg.sid,data,n,wcg_out,sizeof(wcg_out)):0;
    if(!size) {if(!wcg.stop) wcg.stop=WCG_STOP_INVALID;return -1;}
    return (int)send(connection,(const char*)wcg_out,(int)size,0);
}
static size_t wcg_decimal(const char *s,size_t n,uint64_t *out) {
    size_t i=0;uint64_t v=0;
    for(;i<n && s[i]>='0' && s[i]<='9';i++) {
        unsigned d=(unsigned)(s[i]-'0');
        if((i && s[0]=='0') || v>(UINT64_MAX-d)/10) return 0;
        v=v*10+d;
    }
    if(i) *out=v;
    return i;
}
/* NGKEYS|1|<token>|<slot>|1 NUL group key | the N public keys. */
static int wcg_keys(const char *text,size_t ascii,const uint8_t *trailer,size_t tn) {
    char head[96];int n=snprintf(head,sizeof(head),"NGKEYS|1|%s|%u|1",secret,stats.slot);
    if(n<=0 || ascii!=(size_t)n || memcmp(text,head,ascii) || !wc_roster_check(trailer,tn,wire_clients,stats.slot,wcg.hs.sign_pk)) return 0;
    if(wcg.keyed) return !memcmp(wcg.group,trailer,WC_KEY) && !memcmp(wcg.roster,trailer+WC_KEY,WC_KEY*wire_clients);
    memcpy(wcg.group,trailer,WC_KEY);memcpy(wcg.roster,trailer+WC_KEY,WC_KEY*wire_clients);wcg.keyed=1;
    /* BLAKE2b-128 of the roster: players compare it (and their own key) out of band with each other and the relay's report. */
    crypto_blake2b(wcg.fingerprint,16,trailer+WC_KEY,WC_KEY*wire_clients);
    return 1;
}
/* An authentic NGGO|3 or NGGO|4: parse it exactly, then check every other
 * slot's record signature and the relay's permit signature. 1: valid. */
static int wcg_permit(const char *text,size_t ascii,const uint8_t *trailer,size_t tn) {
    char head[80];uint64_t tick=0;
    int n=snprintf(head,sizeof(head),"NGGO|%u|%s|%u|",wire_lookahead?4u:3u,secret,stats.slot);
    if(!wcg.keyed || n<=0 || ascii<=(size_t)n || memcmp(text,head,(size_t)n) || tn!=WC_SIG*(wire_clients+1u)) return 0;
    size_t at=(size_t)n,digits=wcg_decimal(text+at,ascii-at,&tick);
    if(!digits || !tick) return 0;
    at+=digits;
    if(wire_lookahead) {
        uint64_t k=0;
        if(at>=ascii || text[at]!='|') return 0;
        digits=wcg_decimal(text+at+1,ascii-at-1,&k);
        if(!digits || k!=wire_lookahead) return 0;
        at+=1+digits;
    }
    size_t start[GATE_CLIENTS_MAX],size[GATE_CLIENTS_MAX];
    for(unsigned i=0;i<wire_clients;i++) {
        if(at>=ascii || text[at]!='|') return 0;
        start[i]=++at;
        while(at<ascii && text[at]!='|') ++at;
        size[i]=at-start[i];
        if(!gate_blob_span(text+start[i],size[i])) return 0;
    }
    if(at!=ascii) return 0;
    /* A copy identical to a permit already verified is not verified again. */
    uint8_t digest[32];crypto_blake2b(digest,32,(const uint8_t*)text,ascii+1+tn);
    unsigned ring=(unsigned)(tick%WCG_RING);
    if(wcg.verified_used[ring] && wcg.verified_tick[ring]==tick && !memcmp(wcg.verified_hash[ring],digest,32)) {++wcg.cached;return 1;}
    uint64_t source=wc_permit_source(tick,wire_lookahead);
    LARGE_INTEGER begin,end;QueryPerformanceCounter(&begin);
    crypto_blake2b_ctx h;wc_records_begin(&h);
    for(unsigned i=0;i<wire_clients;i++) {
        uint8_t blob[WC_BLOB_MAX];const uint8_t *sig=trailer+WC_SIG*i;size_t bytes=size[i]/2;
        if(!wc_unhex(blob,text+start[i],bytes)) return 0;
        if(source==WC_NO_SOURCE) {if(bytes || !wc_zero(sig,WC_SIG)) return 0;}
        else if(i+1!=stats.slot) {
            if(!wc_record_verify(sig,wcg.roster[i],wcg.sid,i+1,wire_clients,wire_lookahead,source,blob,bytes)) return 0;
            ++wcg.records;
        }
        wc_records_slot(&h,blob,bytes,sig);
    }
    uint8_t records[32];wc_records_end(&h,records);
    if(!wc_permit_verify(trailer+WC_SIG*wire_clients,wcg.relay_pk,wcg.sid,tick,wire_clients,wire_lookahead,records)) return 0;
    QueryPerformanceCounter(&end);wcg.verify_us+=ft_us(begin,end);++wcg.permits;
    wcg.verified_used[ring]=1;wcg.verified_tick[ring]=tick;memcpy(wcg.verified_hash[ring],digest,32);
    return 1;
}
/* An opened plaintext in text[0..size): the length of its ASCII part to hand
 * to the protocol code (NUL-terminated), or 0 when consumed or refused. */
static int wcg_accept(char *text,size_t size) {
    const char *nul=memchr(text,0,size);size_t ascii=nul?(size_t)(nul-text):size;
    const uint8_t *trailer=nul?(const uint8_t*)nul+1:NULL;size_t tn=nul?size-ascii-1:0;
    int ok;
    if(ascii>=7 && !memcmp(text,"NGKEYS|",7)) {ok=wcg_keys(text,ascii,trailer,tn);ascii=0;}
    else if(ascii>=5 && !memcmp(text,"NGGO|",5)) ok=trailer && wcg_permit(text,ascii,trailer,tn);
    else ok=!nul;
    if(!ok) {if(!wcg.stop) wcg.stop=WCG_STOP_INVALID;ascii=0;}
    text[ascii]=0;
    return (int)ascii;
}
/* Unarmed: recv unchanged. Armed: like recv into reply[0..capacity), the
 * opened ASCII part; 0 for a datagram dropped (not authentic, a replay) or
 * consumed (NGKEYS); a socket error as recv. Check wcg.stop afterwards. */
static int gate_wire_recv(char *reply,int capacity) {
    if(!wcg.armed) return (int)recv(connection,reply,capacity,0);
    int n=(int)recv(connection,(char*)wcg_in,(int)sizeof(wcg_in),0);
    if(n<0) return n;
    size_t size=0;
    if(capacity<=0 || !wcg.established || wc_open(&wcg.rx,wcg.sid,wcg_in,(size_t)n,(uint8_t*)reply,(size_t)capacity,&size)!=1) {
        ++wcg.dropped;if(capacity>0) reply[0]=0;return 0;
    }
    return wcg_accept(reply,size);
}
/* The first gated frame: HELLO until the relay's WELCOME, then NGKEYREQ until
 * NGKEYS. 0 when keyed; otherwise the stop reason. */
static unsigned wcg_establish(void) {
    static char text[GATE_PERMIT_MAX];
    if(wire_version!=3 || stats.slot<1 || stats.slot>wire_clients) return WCG_STOP_INVALID;
    if(!wcg.hs.state) {
        uint8_t x[32],s[32];int ok=wc_unhex(wcg.sid,secret,WC_SID) && gate_crypto_random(x,32) && gate_crypto_random(s,32) &&
            wc_client_hello(&wcg.hs,wcg.sid,wcg.psk,wcg.relay_pk,stats.slot,wire_clients,wire_lookahead,x,s,NULL);
        crypto_wipe(x,sizeof(x));crypto_wipe(s,sizeof(s));crypto_wipe(wcg.psk,sizeof(wcg.psk));
        if(!ok) return WCG_STOP_INVALID;
    }
    ULONGLONG start=GetTickCount64(),last=0;
    while(!wcg.keyed) {
        ULONGLONG now=GetTickCount64();
        if(wcg.stop) return wcg.stop;
        if(closed_window()) return 6;
        if(now-start>timeout_ms) return WCG_STOP_HANDSHAKE;
        if(!last || now-last>=100) {
            if(!wcg.established) {send(connection,(const char*)wcg.hs.hello,(int)WC_HELLO,0);++wcg.hellos;}
            else {
                char request[96];int n=snprintf(request,sizeof(request),"NGKEYREQ|1|%s|%u",secret,stats.slot);
                if(n<=0 || (size_t)n>=sizeof(request)) return WCG_STOP_INVALID;
                gate_wire_send(request,(size_t)n);++wcg.keyreqs;
            }
            last=now;
        }
        int count;
        if(!wcg.established) {
            count=(int)recv(connection,(char*)wcg_in,(int)sizeof(wcg_in),0);
            if(count>=0) {
                if(wc_client_welcome(&wcg.hs,wcg_in,(size_t)count,&wcg.tx,&wcg.rx)) {wcg.established=1;last=0;}
                else ++wcg.dropped;
                continue;
            }
        }
        else count=gate_wire_recv(text,(int)sizeof(text)-1);
        if(count<0) {int error=WSAGetLastError();if(error!=WSAETIMEDOUT && error!=WSAEWOULDBLOCK) return 8;}
    }
    wcg.establish_ms=GetTickCount64()-start;
    return 0;
}
/* Before the report of stats.completed leaves: establish once, then append a
 * NUL and the signature of (slot, completed, blob_proposal). 0 or the stop reason. */
static unsigned wcg_prepare(char *packet,int *length,size_t capacity) {
    if(!wcg.armed) return 0;
    if(!wcg.keyed) {unsigned reason=wcg_establish();if(reason) return reason;}
    size_t hex=strlen(blob_proposal);uint8_t blob[WC_BLOB_MAX],sig[WC_SIG];
    if(*length<0 || (size_t)*length+1+WC_SIG>=capacity || hex%2 || hex/2>WC_BLOB_MAX || !wc_unhex(blob,blob_proposal,hex/2)) return WCG_STOP_INVALID;
    LARGE_INTEGER begin,end;QueryPerformanceCounter(&begin);
    if(!wc_record_sign(sig,wcg.hs.sign_sk,wcg.sid,stats.slot,wire_clients,wire_lookahead,stats.completed,blob,hex/2)) return WCG_STOP_INVALID;
    QueryPerformanceCounter(&end);wcg.sign_us+=ft_us(begin,end);++wcg.reports;
    packet[*length]=0;memcpy(packet+*length+1,sig,WC_SIG);*length+=1+(int)WC_SIG;
    return 0;
}
static int wcg_status(char *out,size_t capacity) {
    char key[2*WC_KEY+1],roster[33];
    for(unsigned i=0;i<WC_KEY;i++) snprintf(key+2*i,3,"%02x",wcg.hs.sign_pk[i]);
    for(unsigned i=0;i<16;i++) snprintf(roster+2*i,3,"%02x",wcg.fingerprint[i]);
    if(!wcg.hs.state) key[0]=0;
    if(!wcg.keyed) roster[0]=0;
    int n=snprintf(out,capacity,"armed=%d established=%d keyed=%d establish_ms=%llu hellos=%llu keyreqs=%llu reports=%llu sign_us=%llu "
        "permits=%llu cached=%llu records=%llu verify_us=%llu opened=%llu forged=%llu replayed=%llu dropped=%llu sealed=%llu stop=%u "
        "key=%s roster=%s",
        wcg.armed,wcg.established,wcg.keyed,(unsigned long long)wcg.establish_ms,(unsigned long long)wcg.hellos,(unsigned long long)wcg.keyreqs,
        (unsigned long long)wcg.reports,(unsigned long long)wcg.sign_us,(unsigned long long)wcg.permits,(unsigned long long)wcg.cached,
        (unsigned long long)wcg.records,(unsigned long long)wcg.verify_us,(unsigned long long)wcg.rx.opened,(unsigned long long)wcg.rx.forged,
        (unsigned long long)wcg.rx.replayed,(unsigned long long)wcg.dropped,(unsigned long long)wcg.tx.next,wcg.stop,key,roster);
    return n>0 && (size_t)n<capacity?n:0;
}
