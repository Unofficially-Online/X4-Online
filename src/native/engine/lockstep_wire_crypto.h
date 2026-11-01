/* Opt-in wire crypto for the lockstep relay path and peer links
 * (docs/LOCKSTEP-NSLOT-WIRE.txt, WIRE CRYPTO; reports/LOCKSTEP-WIRE-CRYPTO-20261101.txt).
 *
 * Every primitive comes from Monocypher 4.0.2 (third_party/monocypher-4.0.2,
 * CC0 or BSD-2-Clause): ChaCha20-Poly1305 (RFC 8439) seals datagrams, Ed25519
 * (RFC 8032, SHA-512) signs input records and permits, X25519 (RFC 7748) runs
 * the handshake and BLAKE2b (RFC 7693) derives keys and MACs the HELLO. This
 * file only lays out bytes and composes those calls; it implements no primitive.
 *
 * Platform-neutral and stateless towards the host: no sockets, clock or random
 * source. Callers pass fresh random seeds (32 bytes each) and keep the state
 * structs. Used by the Windows and Linux gates (lockstep_wire_crypto_gate.h),
 * by the relay through lockstep_wire_crypto_lib.c (ctypes) and by the peer-link
 * work (B6) through the same seal/open and sign/verify functions.
 *
 * Layers (all default off):
 *  1. Handshake (HELLO/WELCOME, binary, unsealed): ephemeral X25519 between a
 *     player and the relay. The relay signs the transcript with its long-term
 *     Ed25519 key, whose public half is pinned in the player's configuration;
 *     the player proves its slot with a per-slot pre-shared key (psk) that MACs
 *     the HELLO and keys the KDF. Result: one key pair per player and direction.
 *  2. Sealed datagrams: 16-byte header | ChaCha20-Poly1305 ciphertext | 16-byte
 *     tag. Nonce = header bytes 4..15 (channel, sender, receiver, generation,
 *     64-bit counter); additional data = session id | header. The receiver
 *     keeps a 1,024-counter replay window per sender, updated only after the
 *     tag checked. A new subkey every 2^16 datagrams (epoch = counter >> 16).
 *  3. Input records: every player signs (session, slot, clients, lookahead,
 *     tick, blob) with its own Ed25519 key, fresh per session; the relay
 *     distributes the public keys (NGKEYS) and checks every report's signature,
 *     every client checks every other slot's signature in every permit.
 *  4. Permits: the relay signs (session, tick, clients, lookahead, source tick,
 *     hash of all records and their signatures) once per released tick.
 *  5. Peer links: a group key from NGKEYS; each sender seals under its own
 *     subkey of it and signs the record inside, so a player that holds the
 *     group key still cannot send inputs as another player. */
#ifndef LOCKSTEP_WIRE_CRYPTO_H
#define LOCKSTEP_WIRE_CRYPTO_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "monocypher.h"
#include "monocypher-ed25519.h"

#define WC_KEY 32u
#define WC_SID 16u            /* session id: the 16 bytes of the session token */
#define WC_SIG 64u
#define WC_HEADER 16u
#define WC_TAG 16u
#define WC_OVERHEAD (WC_HEADER+WC_TAG) /* bytes a sealed datagram adds */
#define WC_PLAIN_MAX 8192u
#define WC_SLOTS_MAX 8u
#define WC_LOOKAHEAD_MAX 8u
#define WC_BLOB_MAX 240u      /* GATE_BLOB_HEX_MAX/2 */
#define WC_WINDOW 1024u       /* replay window, in counters */
#define WC_EPOCH_SHIFT 16u    /* subkey epoch = counter >> 16 */
#define WC_HELLO 88u
#define WC_WELCOME 104u
#define WC_RECORD_HEAD 46u
#define WC_RECORD_MAX (WC_RECORD_HEAD+WC_BLOB_MAX)
#define WC_PERMIT_MESSAGE 84u
#define WC_PEER_HEAD 14u      /* slot clients lookahead 0 tick(8) length(2) */
#define WC_PEER_MAX (WC_OVERHEAD+WC_PEER_HEAD+WC_BLOB_MAX+WC_SIG)
#define WC_NO_SOURCE UINT64_MAX
#define WC_RELAY 0u
#define WC_EVERYONE 0xffu
enum {WC_C2R=1,WC_R2C=2,WC_PEER=3};

/* ---- bytes ---- */
static inline void wc_le64(uint8_t *p,uint64_t v) {for(unsigned i=0;i<8;i++) p[i]=(uint8_t)(v>>(8*i));}
static inline uint64_t wc_get64(const uint8_t *p) {uint64_t v=0;for(unsigned i=0;i<8;i++) v|=(uint64_t)p[i]<<(8*i);return v;}
/* Every derivation and signed message starts with a 16-byte, zero-padded name. */
static inline void wc_domain(uint8_t out[16],const char *name) {
    size_t n=strlen(name);memset(out,0,16);memcpy(out,name,n>16?16:n);
}
static inline int wc_zero(const uint8_t *p,size_t n) {uint8_t a=0;for(size_t i=0;i<n;i++) a|=p[i];return a==0;}
static inline int wc_hex_value(char c) {return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;}
/* Lowercase hex of exactly 2*n characters (the wire's spelling) to bytes. */
static inline int wc_unhex(uint8_t *out,const char *hex,size_t n) {
    for(size_t i=0;i<n;i++) {
        int a=wc_hex_value(hex[2*i]),b=a<0?-1:wc_hex_value(hex[2*i+1]);
        if(a<0||b<0) return 0;
        out[i]=(uint8_t)(a<<4|b);
    }
    return 1;
}

/* ---- keys and sealed datagrams ---- */
typedef struct {
    uint8_t base[WC_KEY];
    uint8_t channel,sender,receiver,generation;
    unsigned epoch_shift;
} wc_key;
typedef struct {wc_key k;uint64_t next;} wc_tx;
typedef struct {
    wc_key k;uint64_t top;int any;uint64_t bits[WC_WINDOW/64];
    uint64_t opened,forged,replayed;
} wc_rx;
static inline void wc_key_init(wc_key *k,const uint8_t base[WC_KEY],unsigned channel,unsigned sender,unsigned receiver,unsigned generation) {
    memset(k,0,sizeof(*k));memcpy(k->base,base,WC_KEY);
    k->channel=(uint8_t)channel;k->sender=(uint8_t)sender;k->receiver=(uint8_t)receiver;k->generation=(uint8_t)generation;
    k->epoch_shift=WC_EPOCH_SHIFT;
}
static inline void wc_tx_init(wc_tx *t,const uint8_t base[WC_KEY],unsigned channel,unsigned sender,unsigned receiver,unsigned generation) {
    memset(t,0,sizeof(*t));wc_key_init(&t->k,base,channel,sender,receiver,generation);
}
static inline void wc_rx_init(wc_rx *r,const uint8_t base[WC_KEY],unsigned channel,unsigned sender,unsigned receiver,unsigned generation) {
    memset(r,0,sizeof(*r));wc_key_init(&r->k,base,channel,sender,receiver,generation);
}
/* Subkey of one epoch: BLAKE2b-256 keyed with the channel base key over
 * "X4MP subkey v1" | channel | sender | receiver | generation | epoch (LE).
 * Directions and senders never share a subkey, even under one base key. */
static inline void wc_subkey(const wc_key *k,uint64_t epoch,uint8_t out[WC_KEY]) {
    uint8_t m[28];wc_domain(m,"X4MP subkey v1");
    m[16]=k->channel;m[17]=k->sender;m[18]=k->receiver;m[19]=k->generation;wc_le64(m+20,epoch);
    crypto_blake2b_keyed(out,WC_KEY,k->base,WC_KEY,m,sizeof(m));
}
/* Header: "X4C1" channel sender receiver generation counter(8, LE). */
static inline void wc_header(uint8_t h[WC_HEADER],const wc_key *k,uint64_t counter) {
    memcpy(h,"X4C1",4);h[4]=k->channel;h[5]=k->sender;h[6]=k->receiver;h[7]=k->generation;wc_le64(h+8,counter);
}
static inline int wc_header_matches(const uint8_t *in,size_t n,const wc_key *k) {
    return n>=WC_OVERHEAD && !memcmp(in,"X4C1",4) && in[4]==k->channel && in[5]==k->sender && in[6]==k->receiver && in[7]==k->generation;
}
/* Seal plain[0..n) with the next counter: n+WC_OVERHEAD bytes, or 0 (refused;
 * nothing consumed). out must not overlap plain. */
static inline size_t wc_seal(wc_tx *t,const uint8_t sid[WC_SID],const void *plain,size_t n,uint8_t *out,size_t capacity) {
    if(!t||!sid||!out||(!plain&&n)||n>WC_PLAIN_MAX||capacity<n+WC_OVERHEAD||t->next==UINT64_MAX) return 0;
    uint64_t counter=t->next++;
    uint8_t key[WC_KEY],ad[WC_SID+WC_HEADER];crypto_aead_ctx ctx;
    wc_header(out,&t->k,counter);
    wc_subkey(&t->k,counter>>t->k.epoch_shift,key);
    memcpy(ad,sid,WC_SID);memcpy(ad+WC_SID,out,WC_HEADER);
    crypto_aead_init_ietf(&ctx,key,out+4);
    crypto_aead_write(&ctx,out+WC_HEADER,out+WC_HEADER+n,ad,sizeof(ad),(const uint8_t*)plain,n);
    crypto_wipe(&ctx,sizeof(ctx));crypto_wipe(key,sizeof(key));
    return n+WC_OVERHEAD;
}
/* Tag check and decryption under k, without replay state. 1: authentic. */
static inline int wc_open_key(const wc_key *k,const uint8_t sid[WC_SID],const uint8_t *in,size_t n,uint8_t *plain,size_t capacity,
                              size_t *plain_n,uint64_t *counter) {
    if(!k||!sid||!in||!plain||!wc_header_matches(in,n,k)||n-WC_OVERHEAD>capacity||n-WC_OVERHEAD>WC_PLAIN_MAX) return 0;
    uint64_t c=wc_get64(in+8);size_t size=n-WC_OVERHEAD;
    uint8_t key[WC_KEY],ad[WC_SID+WC_HEADER];crypto_aead_ctx ctx;
    wc_subkey(k,c>>k->epoch_shift,key);
    memcpy(ad,sid,WC_SID);memcpy(ad+WC_SID,in,WC_HEADER);
    crypto_aead_init_ietf(&ctx,key,in+4);
    int bad=crypto_aead_read(&ctx,plain,in+n-WC_TAG,ad,sizeof(ad),in+WC_HEADER,size);
    crypto_wipe(&ctx,sizeof(ctx));crypto_wipe(key,sizeof(key));
    if(bad) {crypto_wipe(plain,size);return 0;}
    if(plain_n) *plain_n=size;
    if(counter) *counter=c;
    return 1;
}
/* Replay window (RFC 6479 style, 1,024 counters below the highest seen). */
static inline int wc_window_fresh(const wc_rx *r,uint64_t c) {
    if(!r->any||c>r->top) return 1;
    if(r->top-c>=WC_WINDOW) return 0;
    return !((r->bits[(c/64)%(WC_WINDOW/64)]>>(c%64))&1u);
}
static inline void wc_window_mark(wc_rx *r,uint64_t c) {
    if(!r->any) {memset(r->bits,0,sizeof(r->bits));r->top=c;r->any=1;}
    else if(c>r->top) {
        if(c-r->top>=WC_WINDOW) memset(r->bits,0,sizeof(r->bits));
        else for(uint64_t i=r->top+1;i<=c;i++) r->bits[(i/64)%(WC_WINDOW/64)]&=~((uint64_t)1<<(i%64));
        r->top=c;
    }
    r->bits[(c/64)%(WC_WINDOW/64)]|=(uint64_t)1<<(c%64);
}
/* 1: opened (plain[0..*plain_n)); 0: not authentic for this channel (dropped);
 * -1: a replay or older than the window (dropped before decryption). The
 * window moves only after the tag checked. */
static inline int wc_open(wc_rx *r,const uint8_t sid[WC_SID],const uint8_t *in,size_t n,uint8_t *plain,size_t capacity,size_t *plain_n) {
    uint64_t c;
    if(!r||!in||!wc_header_matches(in,n,&r->k)) {if(r) ++r->forged;return 0;}
    if(!wc_window_fresh(r,wc_get64(in+8))) {++r->replayed;return -1;}
    if(!wc_open_key(&r->k,sid,in,n,plain,capacity,plain_n,&c)) {++r->forged;return 0;}
    wc_window_mark(r,c);++r->opened;
    return 1;
}

/* ---- input records and permits ---- */
/* The record a player signs: "X4MP input v1" | session id | slot | clients |
 * lookahead | 0 | tick (LE) | blob length (LE16) | blob. Length or 0. */
static inline size_t wc_record(uint8_t out[WC_RECORD_MAX],const uint8_t sid[WC_SID],unsigned slot,unsigned clients,unsigned lookahead,
                               uint64_t tick,const uint8_t *blob,size_t n) {
    if(!out||!sid||clients<2||clients>WC_SLOTS_MAX||slot<1||slot>clients||lookahead>WC_LOOKAHEAD_MAX||n>WC_BLOB_MAX||(!blob&&n)) return 0;
    wc_domain(out,"X4MP input v1");memcpy(out+16,sid,WC_SID);
    out[32]=(uint8_t)slot;out[33]=(uint8_t)clients;out[34]=(uint8_t)lookahead;out[35]=0;
    wc_le64(out+36,tick);out[44]=(uint8_t)n;out[45]=(uint8_t)(n>>8);
    if(n) memcpy(out+WC_RECORD_HEAD,blob,n);
    return WC_RECORD_HEAD+n;
}
static inline int wc_record_sign(uint8_t sig[WC_SIG],const uint8_t secret_key[64],const uint8_t sid[WC_SID],unsigned slot,unsigned clients,
                                 unsigned lookahead,uint64_t tick,const uint8_t *blob,size_t n) {
    uint8_t m[WC_RECORD_MAX];size_t size=wc_record(m,sid,slot,clients,lookahead,tick,blob,n);
    if(!size||!sig||!secret_key) return 0;
    crypto_ed25519_sign(sig,secret_key,m,size);return 1;
}
static inline int wc_record_verify(const uint8_t sig[WC_SIG],const uint8_t public_key[WC_KEY],const uint8_t sid[WC_SID],unsigned slot,
                                   unsigned clients,unsigned lookahead,uint64_t tick,const uint8_t *blob,size_t n) {
    uint8_t m[WC_RECORD_MAX];size_t size=wc_record(m,sid,slot,clients,lookahead,tick,blob,n);
    return size && sig && public_key && !crypto_ed25519_check(sig,public_key,m,size);
}
/* The tick whose proposals the permit for tick T carries: T-1-k, none for T <= k. */
static inline uint64_t wc_permit_source(uint64_t tick,unsigned lookahead) {return tick>lookahead?tick-1-lookahead:WC_NO_SOURCE;}
/* Hash of a permit's records: BLAKE2b-256 over "X4MP records v1" then, per
 * slot in order, blob length (LE16) | blob | the slot's signature (64 zero
 * bytes when the permit has no source tick). */
static inline void wc_records_begin(crypto_blake2b_ctx *h) {
    uint8_t d[16];wc_domain(d,"X4MP records v1");crypto_blake2b_init(h,32);crypto_blake2b_update(h,d,16);
}
static inline void wc_records_slot(crypto_blake2b_ctx *h,const uint8_t *blob,size_t n,const uint8_t sig[WC_SIG]) {
    uint8_t length[2]={(uint8_t)n,(uint8_t)(n>>8)};
    crypto_blake2b_update(h,length,2);if(n) crypto_blake2b_update(h,blob,n);crypto_blake2b_update(h,sig,WC_SIG);
}
static inline void wc_records_end(crypto_blake2b_ctx *h,uint8_t out[32]) {crypto_blake2b_final(h,out);}
/* "X4MP permit v1" | session id | tick (LE) | clients | lookahead | 0 0 | source tick (LE, all ones for none) | records hash. */
static inline int wc_permit_message(uint8_t m[WC_PERMIT_MESSAGE],const uint8_t sid[WC_SID],uint64_t tick,unsigned clients,unsigned lookahead,
                                    const uint8_t records[32]) {
    if(!m||!sid||!records||!tick||clients<2||clients>WC_SLOTS_MAX||lookahead>WC_LOOKAHEAD_MAX) return 0;
    wc_domain(m,"X4MP permit v1");memcpy(m+16,sid,WC_SID);wc_le64(m+32,tick);
    m[40]=(uint8_t)clients;m[41]=(uint8_t)lookahead;m[42]=m[43]=0;
    wc_le64(m+44,wc_permit_source(tick,lookahead));memcpy(m+52,records,32);
    return 1;
}
static inline int wc_permit_sign(uint8_t sig[WC_SIG],const uint8_t relay_secret[64],const uint8_t sid[WC_SID],uint64_t tick,unsigned clients,
                                 unsigned lookahead,const uint8_t records[32]) {
    uint8_t m[WC_PERMIT_MESSAGE];
    if(!sig||!relay_secret||!wc_permit_message(m,sid,tick,clients,lookahead,records)) return 0;
    crypto_ed25519_sign(sig,relay_secret,m,sizeof(m));return 1;
}
static inline int wc_permit_verify(const uint8_t sig[WC_SIG],const uint8_t relay_public[WC_KEY],const uint8_t sid[WC_SID],uint64_t tick,
                                   unsigned clients,unsigned lookahead,const uint8_t records[32]) {
    uint8_t m[WC_PERMIT_MESSAGE];
    return sig && relay_public && wc_permit_message(m,sid,tick,clients,lookahead,records) && !crypto_ed25519_check(sig,relay_public,m,sizeof(m));
}

/* ---- handshake ---- */
/* Per-slot pre-shared key: BLAKE2b-256 keyed with the relay's master over
 * "X4MP slot psk v1" | session id | slot. Players get only their own. */
static inline void wc_slot_psk(uint8_t psk[WC_KEY],const uint8_t master[WC_KEY],const uint8_t sid[WC_SID],unsigned slot) {
    uint8_t m[33];wc_domain(m,"X4MP slot psk v1");memcpy(m+16,sid,WC_SID);m[32]=(uint8_t)slot;
    crypto_blake2b_keyed(psk,WC_KEY,master,WC_KEY,m,sizeof(m));
}
static inline void wc_hello_mac(uint8_t mac[16],const uint8_t psk[WC_KEY],const uint8_t sid[WC_SID],const uint8_t hello[WC_HELLO]) {
    uint8_t m[16+WC_SID+72];wc_domain(m,"X4MP hello v1");memcpy(m+16,sid,WC_SID);memcpy(m+32,hello,72);
    crypto_blake2b_keyed(mac,16,psk,WC_KEY,m,sizeof(m));
}
static inline void wc_welcome_message(uint8_t m[16+WC_SID+WC_HELLO+40],const uint8_t sid[WC_SID],const uint8_t hello[WC_HELLO],
                                      const uint8_t welcome[WC_WELCOME]) {
    wc_domain(m,"X4MP welcome v1");memcpy(m+16,sid,WC_SID);memcpy(m+32,hello,WC_HELLO);memcpy(m+32+WC_HELLO,welcome,40);
}
/* Both directions' base keys: BLAKE2b-512 keyed with the slot psk over
 * "X4MP keys v1" | session id | HELLO | WELCOME | X25519 shared secret. */
static inline void wc_session_keys(uint8_t c2r[WC_KEY],uint8_t r2c[WC_KEY],const uint8_t psk[WC_KEY],const uint8_t sid[WC_SID],
                                   const uint8_t hello[WC_HELLO],const uint8_t welcome[WC_WELCOME],const uint8_t shared[32]) {
    uint8_t m[16+WC_SID+WC_HELLO+WC_WELCOME+32],okm[64];
    wc_domain(m,"X4MP keys v1");memcpy(m+16,sid,WC_SID);memcpy(m+32,hello,WC_HELLO);
    memcpy(m+32+WC_HELLO,welcome,WC_WELCOME);memcpy(m+32+WC_HELLO+WC_WELCOME,shared,32);
    crypto_blake2b_keyed(okm,64,psk,WC_KEY,m,sizeof(m));
    memcpy(c2r,okm,WC_KEY);memcpy(r2c,okm+32,WC_KEY);
    crypto_wipe(m,sizeof(m));crypto_wipe(okm,sizeof(okm));
}
typedef struct {
    uint8_t sid[WC_SID],psk[WC_KEY],relay_pk[WC_KEY];
    uint8_t x_secret[32],sign_sk[64],sign_pk[WC_KEY];
    uint8_t hello[WC_HELLO];
    uint8_t slot,clients,lookahead,state; /* state 1: HELLO made, 2: established */
} wc_client;
/* HELLO = "X4H1" 1 slot clients lookahead | ephemeral X25519 public | the
 * player's Ed25519 public key | BLAKE2b-128 MAC under the slot psk. Both seeds
 * are consumed (wiped). 1 on success. */
static inline int wc_client_hello(wc_client *c,const uint8_t sid[WC_SID],const uint8_t psk[WC_KEY],const uint8_t relay_pk[WC_KEY],
                                  unsigned slot,unsigned clients,unsigned lookahead,uint8_t x_seed[32],uint8_t sign_seed[32],
                                  uint8_t hello[WC_HELLO]) {
    if(!c||!sid||!psk||!relay_pk||!x_seed||!sign_seed||clients<2||clients>WC_SLOTS_MAX||slot<1||slot>clients||lookahead>WC_LOOKAHEAD_MAX) return 0;
    memset(c,0,sizeof(*c));
    memcpy(c->sid,sid,WC_SID);memcpy(c->psk,psk,WC_KEY);memcpy(c->relay_pk,relay_pk,WC_KEY);
    memcpy(c->x_secret,x_seed,32);crypto_wipe(x_seed,32);
    crypto_ed25519_key_pair(c->sign_sk,c->sign_pk,sign_seed);
    c->slot=(uint8_t)slot;c->clients=(uint8_t)clients;c->lookahead=(uint8_t)lookahead;
    uint8_t *h=c->hello;
    memcpy(h,"X4H1",4);h[4]=1;h[5]=c->slot;h[6]=c->clients;h[7]=c->lookahead;
    crypto_x25519_public_key(h+8,c->x_secret);memcpy(h+40,c->sign_pk,WC_KEY);
    wc_hello_mac(h+72,c->psk,c->sid,h);
    if(hello) memcpy(hello,h,WC_HELLO);
    c->state=1;return 1;
}
/* Relay: check a HELLO and answer it. WELCOME = "X4H1" 2 slot clients
 * lookahead | ephemeral X25519 public | Ed25519 signature of the relay over
 * "X4MP welcome v1" | session id | HELLO | WELCOME[0..40). x_seed is the
 * relay's fresh ephemeral secret (wiped). Deterministic in its inputs: keep
 * the WELCOME to answer a repeated HELLO. 1 on success, 0 refused. */
static inline int wc_relay_welcome(const uint8_t sid[WC_SID],const uint8_t master[WC_KEY],const uint8_t relay_secret[64],unsigned clients,
                                   unsigned lookahead,const uint8_t *hello,size_t n,uint8_t x_seed[32],uint8_t welcome[WC_WELCOME],
                                   unsigned *slot,uint8_t player_pk[WC_KEY],wc_rx *from_player,wc_tx *to_player) {
    uint8_t psk[WC_KEY],mac[16],shared[32],c2r[WC_KEY],r2c[WC_KEY],m[16+WC_SID+WC_HELLO+40];int ok=0;
    if(!sid||!master||!relay_secret||!hello||!x_seed||!welcome||!slot||!player_pk||!from_player||!to_player) return 0;
    if(n!=WC_HELLO||memcmp(hello,"X4H1",4)||hello[4]!=1||hello[6]!=clients||hello[7]!=lookahead||clients<2||clients>WC_SLOTS_MAX||
       hello[5]<1||hello[5]>clients) {crypto_wipe(x_seed,32);return 0;}
    wc_slot_psk(psk,master,sid,hello[5]);wc_hello_mac(mac,psk,sid,hello);
    if(crypto_verify16(mac,hello+72)) goto done;
    memcpy(welcome,"X4H1",4);welcome[4]=2;welcome[5]=hello[5];welcome[6]=hello[6];welcome[7]=hello[7];
    crypto_x25519_public_key(welcome+8,x_seed);
    wc_welcome_message(m,sid,hello,welcome);
    crypto_ed25519_sign(welcome+40,relay_secret,m,sizeof(m));
    crypto_x25519(shared,x_seed,hello+8);
    if(wc_zero(shared,32)) {memset(welcome,0,WC_WELCOME);goto done;} /* low-order public key */
    wc_session_keys(c2r,r2c,psk,sid,hello,welcome,shared);
    wc_rx_init(from_player,c2r,WC_C2R,hello[5],WC_RELAY,0);wc_tx_init(to_player,r2c,WC_R2C,WC_RELAY,hello[5],0);
    *slot=hello[5];memcpy(player_pk,hello+40,WC_KEY);ok=1;
done:
    crypto_wipe(x_seed,32);crypto_wipe(psk,sizeof(psk));crypto_wipe(shared,sizeof(shared));
    crypto_wipe(c2r,sizeof(c2r));crypto_wipe(r2c,sizeof(r2c));
    return ok;
}
/* Player: check the relay's WELCOME (pinned relay key) and derive both
 * directions. 1: established; 0: not this handshake's WELCOME (ignored). */
static inline int wc_client_welcome(wc_client *c,const uint8_t *welcome,size_t n,wc_tx *to_relay,wc_rx *from_relay) {
    uint8_t m[16+WC_SID+WC_HELLO+40],shared[32],c2r[WC_KEY],r2c[WC_KEY];
    if(!c||c->state!=1||!welcome||!to_relay||!from_relay||n!=WC_WELCOME||memcmp(welcome,"X4H1",4)||welcome[4]!=2||
       welcome[5]!=c->slot||welcome[6]!=c->clients||welcome[7]!=c->lookahead) return 0;
    wc_welcome_message(m,c->sid,c->hello,welcome);
    if(crypto_ed25519_check(welcome+40,c->relay_pk,m,sizeof(m))) return 0;
    crypto_x25519(shared,c->x_secret,welcome+8);
    if(wc_zero(shared,32)) return 0;
    wc_session_keys(c2r,r2c,c->psk,c->sid,c->hello,welcome,shared);
    wc_tx_init(to_relay,c2r,WC_C2R,c->slot,WC_RELAY,0);wc_rx_init(from_relay,r2c,WC_R2C,WC_RELAY,c->slot,0);
    crypto_wipe(shared,sizeof(shared));crypto_wipe(c2r,sizeof(c2r));crypto_wipe(r2c,sizeof(r2c));
    crypto_wipe(c->x_secret,32);crypto_wipe(c->psk,WC_KEY);c->state=2;
    return 1;
}
/* NGKEYS trailer (after the ASCII line and its NUL): group key (32) and the
 * N players' Ed25519 public keys in slot order. 1: the roster holds this
 * player's own key at its slot, keys are pairwise distinct and none is zero. */
static inline int wc_roster_check(const uint8_t *trailer,size_t n,unsigned clients,unsigned slot,const uint8_t own_pk[WC_KEY]) {
    if(!trailer||!own_pk||clients<2||clients>WC_SLOTS_MAX||slot<1||slot>clients||n!=WC_KEY*(1+clients)||wc_zero(trailer,WC_KEY)) return 0;
    const uint8_t *keys=trailer+WC_KEY;
    if(crypto_verify32(keys+WC_KEY*(slot-1),own_pk)) return 0;
    for(unsigned i=0;i<clients;i++) {
        if(wc_zero(keys+WC_KEY*i,WC_KEY)) return 0;
        for(unsigned j=i+1;j<clients;j++) if(!memcmp(keys+WC_KEY*i,keys+WC_KEY*j,WC_KEY)) return 0;
    }
    return 1;
}

/* ---- peer links (B6): one input record per datagram ---- */
/* Sender slot s seals under base_s = BLAKE2b-256 keyed with the group key over
 * "X4MP peer v1" | session id | s, receiver WC_EVERYONE (one seal for every
 * peer). A player holding the group key can derive every base_s, so the tag
 * only keeps outsiders out; the record's Ed25519 signature names the sender. */
static inline void wc_peer_key(wc_key *k,const uint8_t group[WC_KEY],const uint8_t sid[WC_SID],unsigned generation,unsigned sender) {
    uint8_t m[33],base[WC_KEY];wc_domain(m,"X4MP peer v1");memcpy(m+16,sid,WC_SID);m[32]=(uint8_t)sender;
    crypto_blake2b_keyed(base,WC_KEY,group,WC_KEY,m,sizeof(m));
    wc_key_init(k,base,WC_PEER,sender,WC_EVERYONE,generation);crypto_wipe(base,sizeof(base));
}
/* Plaintext: slot clients lookahead 0 | tick (LE) | length (LE16) | blob | signature. */
static inline size_t wc_peer_seal(wc_tx *t,const uint8_t sid[WC_SID],const uint8_t secret_key[64],unsigned clients,unsigned lookahead,
                                  uint64_t tick,const uint8_t *blob,size_t n,uint8_t *out,size_t capacity) {
    uint8_t p[WC_PEER_HEAD+WC_BLOB_MAX+WC_SIG];
    if(!t||t->k.channel!=WC_PEER||!wc_record_sign(p+WC_PEER_HEAD+n,secret_key,sid,t->k.sender,clients,lookahead,tick,blob,n)) return 0;
    p[0]=t->k.sender;p[1]=(uint8_t)clients;p[2]=(uint8_t)lookahead;p[3]=0;wc_le64(p+4,tick);p[12]=(uint8_t)n;p[13]=(uint8_t)(n>>8);
    if(n) memcpy(p+WC_PEER_HEAD,blob,n);
    return wc_seal(t,sid,p,WC_PEER_HEAD+n+WC_SIG,out,capacity);
}
/* keys[s-1]: the peer key of sender s (wc_peer_key); roster[s-1]: its public
 * key. 1: an authentic record of *slot for *tick (blob, signature out);
 * 0: not authentic for any sender (outsider, garbage); -2: authentic under
 * the group key but the signature or fields are wrong: a player holding the
 * group key forged it (keep as evidence, never apply). Replays are the
 * caller's: a record is idempotent per (slot, tick); accept only ticks not yet
 * run and within the permit window, and treat two different records of one
 * (slot, tick) with valid signatures as proof that player equivocated. */
static inline int wc_peer_open(const wc_key *keys,const uint8_t (*roster)[WC_KEY],const uint8_t sid[WC_SID],unsigned clients,unsigned lookahead,
                               const uint8_t *in,size_t n,unsigned *slot,uint64_t *tick,uint8_t blob[WC_BLOB_MAX],size_t *blob_n,
                               uint8_t sig[WC_SIG]) {
    uint8_t p[WC_PEER_HEAD+WC_BLOB_MAX+WC_SIG];size_t size=0;
    if(!keys||!roster||!in||n<WC_OVERHEAD||clients<2||clients>WC_SLOTS_MAX||in[5]<1||in[5]>clients) return 0;
    unsigned s=in[5];
    if(!wc_open_key(&keys[s-1],sid,in,n,p,sizeof(p),&size,NULL)) return 0;
    if(size<WC_PEER_HEAD+WC_SIG) return -2;
    size_t length=(size_t)p[12]|(size_t)p[13]<<8;
    if(p[0]!=s||p[1]!=clients||p[2]!=lookahead||p[3]||length>WC_BLOB_MAX||size!=WC_PEER_HEAD+length+WC_SIG) return -2;
    uint64_t t=wc_get64(p+4);
    if(!wc_record_verify(p+WC_PEER_HEAD+length,roster[s-1],sid,s,clients,lookahead,t,p+WC_PEER_HEAD,length)) return -2;
    if(slot) *slot=s;
    if(tick) *tick=t;
    if(blob) memcpy(blob,p+WC_PEER_HEAD,length);
    if(blob_n) *blob_n=length;
    if(sig) memcpy(sig,p+WC_PEER_HEAD+length,WC_SIG);
    return 1;
}

/* ---- relay credentials in a player's configuration ---- */
/* "x4c1:<relay Ed25519 public key, 64 hex>:<slot psk, 64 hex>" (lowercase). */
#define WC_CREDENTIAL_LENGTH 134u
static inline int wc_credential_parse(const char *s,size_t n,uint8_t relay_pk[WC_KEY],uint8_t psk[WC_KEY]) {
    return s && n==WC_CREDENTIAL_LENGTH && !memcmp(s,"x4c1:",5) && s[69]==':' &&
           wc_unhex(relay_pk,s+5,WC_KEY) && wc_unhex(psk,s+70,WC_KEY) && !wc_zero(relay_pk,WC_KEY) && !wc_zero(psk,WC_KEY);
}

/* ---- known answers (RFC vectors; run before any key is used) ---- */
static inline int wc_kat_equal(const uint8_t *got,const char *hex,size_t n) {
    uint8_t want[128];return n<=sizeof(want) && wc_unhex(want,hex,n) && !memcmp(got,want,n);
}
/* 0 when every vector matches; otherwise the number of the first failure. */
static inline int wc_selftest(void) {
    uint8_t out[128],a[64],b[64],c[64];
    /* RFC 7693 appendix A: BLAKE2b-512("abc"). */
    crypto_blake2b(out,64,(const uint8_t*)"abc",3);
    if(!wc_kat_equal(out,"ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d1"
                         "7d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923",64)) return 1;
    /* RFC 7748 6.1: X25519 public keys and shared secret. */
    wc_unhex(a,"77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a",32);
    wc_unhex(b,"5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb",32);
    crypto_x25519_public_key(out,a);
    if(!wc_kat_equal(out,"8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a",32)) return 2;
    crypto_x25519_public_key(c,b);
    if(!wc_kat_equal(c,"de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f",32)) return 3;
    crypto_x25519(out,a,c);
    if(!wc_kat_equal(out,"4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742",32)) return 4;
    /* RFC 8032 7.1 tests 1 and 2: Ed25519 keys and signatures, then a rejected forgery. */
    static const char *const ed[2][4]={
        {"9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60","d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a","",
         "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"},
        {"4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb","3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c","72",
         "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"}};
    for(unsigned i=0;i<2;i++) {
        uint8_t seed[32],sk[64],pk[32],msg[1],sig[64];size_t len=strlen(ed[i][2])/2;
        wc_unhex(seed,ed[i][0],32);wc_unhex(msg,ed[i][2],len);
        crypto_ed25519_key_pair(sk,pk,seed);
        if(!wc_kat_equal(pk,ed[i][1],32)) return 5;
        crypto_ed25519_sign(sig,sk,msg,len);
        if(!wc_kat_equal(sig,ed[i][3],64) || crypto_ed25519_check(sig,pk,msg,len)) return 6;
        sig[0]^=1;
        if(!crypto_ed25519_check(sig,pk,msg,len)) return 7;
        crypto_wipe(sk,sizeof(sk));
    }
    /* RFC 8439 2.8.2: ChaCha20-Poly1305 AEAD (the IETF construction used for every datagram). */
    {
        static const char text[]="Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it.";
        uint8_t key[32],nonce[12],ad[12],cipher[114],mac[16],plain[114];crypto_aead_ctx ctx;
        for(unsigned i=0;i<32;i++) key[i]=(uint8_t)(0x80+i);
        wc_unhex(nonce,"070000004041424344454647",12);wc_unhex(ad,"50515253c0c1c2c3c4c5c6c7",12);
        crypto_aead_init_ietf(&ctx,key,nonce);crypto_aead_write(&ctx,cipher,mac,ad,12,(const uint8_t*)text,114);
        if(!wc_kat_equal(cipher,"d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d63dbea45e8ca9671282fafb69da92728b",48) ||
           !wc_kat_equal(cipher+48,"1a71de0a9e060b2905d6a5b67ecd3b3692ddbd7f2d778b8c9803aee328091b58fab324e4fad675945585808b4831d7bc",48) ||
           !wc_kat_equal(cipher+96,"3ff4def08e4b7a9de576d26586cec64b6116",18) || !wc_kat_equal(mac,"1ae10b594f09e26a7e902ecbd0600691",16)) return 8;
        crypto_aead_init_ietf(&ctx,key,nonce);
        if(crypto_aead_read(&ctx,plain,mac,ad,12,cipher,114) || memcmp(plain,text,114)) return 9;
        mac[15]^=0x80;crypto_aead_init_ietf(&ctx,key,nonce);
        if(!crypto_aead_read(&ctx,plain,mac,ad,12,cipher,114)) return 10;
        crypto_wipe(&ctx,sizeof(ctx));
    }
    return 0;
}
#endif
