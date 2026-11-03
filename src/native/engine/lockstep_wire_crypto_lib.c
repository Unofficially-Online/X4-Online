/* x4mp_wire_crypto: the wire crypto of lockstep_wire_crypto.h behind a flat C
 * ABI, for the relay (tools/dev/native_gate_crypto.py through ctypes, standard
 * library only) and for tests. The same source is compiled into the gates, so
 * the relay and the clients share one implementation of every byte layout.
 * State structs are opaque to callers: allocate x4c_sizeof(kind) bytes. */
#include "lockstep_wire_crypto.h"
#ifdef _WIN32
#define X4C_EXPORT __declspec(dllexport)
#else
#define X4C_EXPORT __attribute__((visibility("default")))
#endif

X4C_EXPORT int x4c_abi(void) {return 1;}
/* 0 when the RFC known answers match; the relay refuses to start otherwise. */
X4C_EXPORT int x4c_selftest(void) {return wc_selftest();}
/* 1 wc_tx, 2 wc_rx, 3 wc_client, 4 wc_key. */
X4C_EXPORT size_t x4c_sizeof(int kind) {
    return kind==1?sizeof(wc_tx):kind==2?sizeof(wc_rx):kind==3?sizeof(wc_client):kind==4?sizeof(wc_key):0;
}
/* Ed25519 key pair from a 32-byte seed (the seed is wiped). */
X4C_EXPORT void x4c_ed25519_key_pair(uint8_t secret_key[64],uint8_t public_key[32],uint8_t seed[32]) {
    crypto_ed25519_key_pair(secret_key,public_key,seed);
}
X4C_EXPORT void x4c_slot_psk(uint8_t psk[32],const uint8_t master[32],const uint8_t sid[16],unsigned slot) {wc_slot_psk(psk,master,sid,slot);}
X4C_EXPORT int x4c_relay_welcome(const uint8_t sid[16],const uint8_t master[32],const uint8_t relay_secret[64],unsigned clients,unsigned lookahead,
                                 const uint8_t *hello,size_t n,uint8_t x_seed[32],uint8_t welcome[WC_WELCOME],unsigned *slot,
                                 uint8_t player_pk[32],wc_rx *from_player,wc_tx *to_player) {
    return wc_relay_welcome(sid,master,relay_secret,clients,lookahead,hello,n,x_seed,welcome,slot,player_pk,from_player,to_player);
}
X4C_EXPORT int x4c_client_hello(wc_client *c,const uint8_t sid[16],const uint8_t psk[32],const uint8_t relay_pk[32],unsigned slot,
                                unsigned clients,unsigned lookahead,uint8_t x_seed[32],uint8_t sign_seed[32],uint8_t hello[WC_HELLO]) {
    return wc_client_hello(c,sid,psk,relay_pk,slot,clients,lookahead,x_seed,sign_seed,hello);
}
X4C_EXPORT int x4c_client_welcome(wc_client *c,const uint8_t *welcome,size_t n,wc_tx *to_relay,wc_rx *from_relay) {
    return wc_client_welcome(c,welcome,n,to_relay,from_relay);
}
/* The player's Ed25519 key of an established or hello'd client state. */
X4C_EXPORT void x4c_client_keys(const wc_client *c,uint8_t secret_key[64],uint8_t public_key[32]) {
    if(secret_key) memcpy(secret_key,c->sign_sk,64);
    if(public_key) memcpy(public_key,c->sign_pk,32);
}
X4C_EXPORT void x4c_tx_init(wc_tx *t,const uint8_t base[32],unsigned channel,unsigned sender,unsigned receiver,unsigned generation,
                            unsigned epoch_shift,uint64_t next) {
    wc_tx_init(t,base,channel,sender,receiver,generation);t->k.epoch_shift=epoch_shift;t->next=next;
}
X4C_EXPORT void x4c_rx_init(wc_rx *r,const uint8_t base[32],unsigned channel,unsigned sender,unsigned receiver,unsigned generation,
                            unsigned epoch_shift) {
    wc_rx_init(r,base,channel,sender,receiver,generation);r->k.epoch_shift=epoch_shift;
}
/* opened, forged, replayed of an rx state. */
X4C_EXPORT void x4c_rx_counts(const wc_rx *r,uint64_t out[3]) {out[0]=r->opened;out[1]=r->forged;out[2]=r->replayed;}
X4C_EXPORT size_t x4c_seal(wc_tx *t,const uint8_t sid[16],const uint8_t *plain,size_t n,uint8_t *out,size_t capacity) {
    return wc_seal(t,sid,plain,n,out,capacity);
}
X4C_EXPORT int x4c_open(wc_rx *r,const uint8_t sid[16],const uint8_t *in,size_t n,uint8_t *plain,size_t capacity,size_t *plain_n) {
    return wc_open(r,sid,in,n,plain,capacity,plain_n);
}
X4C_EXPORT int x4c_record_sign(uint8_t sig[64],const uint8_t secret_key[64],const uint8_t sid[16],unsigned slot,unsigned clients,
                               unsigned lookahead,uint64_t tick,const uint8_t *blob,size_t n) {
    return wc_record_sign(sig,secret_key,sid,slot,clients,lookahead,tick,blob,n);
}
X4C_EXPORT int x4c_record_verify(const uint8_t sig[64],const uint8_t public_key[32],const uint8_t sid[16],unsigned slot,unsigned clients,
                                 unsigned lookahead,uint64_t tick,const uint8_t *blob,size_t n) {
    return wc_record_verify(sig,public_key,sid,slot,clients,lookahead,tick,blob,n);
}
/* packed: per slot in order, blob length (LE16) | blob | signature (64). */
X4C_EXPORT void x4c_records_hash(uint8_t out[32],const uint8_t *packed,size_t n) {
    crypto_blake2b_ctx h;wc_records_begin(&h);if(n) crypto_blake2b_update(&h,packed,n);wc_records_end(&h,out);
}
X4C_EXPORT int x4c_permit_sign(uint8_t sig[64],const uint8_t relay_secret[64],const uint8_t sid[16],uint64_t tick,unsigned clients,
                               unsigned lookahead,const uint8_t records[32]) {
    return wc_permit_sign(sig,relay_secret,sid,tick,clients,lookahead,records);
}
X4C_EXPORT int x4c_permit_verify(const uint8_t sig[64],const uint8_t relay_public[32],const uint8_t sid[16],uint64_t tick,unsigned clients,
                                 unsigned lookahead,const uint8_t records[32]) {
    return wc_permit_verify(sig,relay_public,sid,tick,clients,lookahead,records);
}
X4C_EXPORT int x4c_roster_check(const uint8_t *trailer,size_t n,unsigned clients,unsigned slot,const uint8_t own_pk[32]) {
    return wc_roster_check(trailer,n,clients,slot,own_pk);
}
/* Peer links: keys is an array of `clients` wc_key (x4c_sizeof(4) each). */
X4C_EXPORT void x4c_peer_key(wc_key *k,const uint8_t group[32],const uint8_t sid[16],unsigned generation,unsigned sender) {
    wc_peer_key(k,group,sid,generation,sender);
}
X4C_EXPORT void x4c_peer_tx(wc_tx *t,const uint8_t group[32],const uint8_t sid[16],unsigned generation,unsigned sender) {
    memset(t,0,sizeof(*t));wc_peer_key(&t->k,group,sid,generation,sender);
}
X4C_EXPORT size_t x4c_peer_seal(wc_tx *t,const uint8_t sid[16],const uint8_t secret_key[64],unsigned clients,unsigned lookahead,uint64_t tick,
                                const uint8_t *blob,size_t n,uint8_t *out,size_t capacity) {
    return wc_peer_seal(t,sid,secret_key,clients,lookahead,tick,blob,n,out,capacity);
}
X4C_EXPORT int x4c_peer_open(const wc_key *keys,const uint8_t (*roster)[32],const uint8_t sid[16],unsigned clients,unsigned lookahead,
                             const uint8_t *in,size_t n,unsigned *slot,uint64_t *tick,uint8_t blob[WC_BLOB_MAX],size_t *blob_n,uint8_t sig[64]) {
    return wc_peer_open(keys,roster,sid,clients,lookahead,in,n,slot,tick,blob,blob_n,sig);
}
