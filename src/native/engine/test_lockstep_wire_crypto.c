/* Offline tests of lockstep_wire_crypto.h: RFC known answers, pinned golden
 * vectors of our byte layouts (recomputed independently by
 * tests/test_native_gate_crypto.py with the Python cryptography package and
 * hashlib), tampering, replay, wrong-slot signatures, key mismatches that must
 * fail closed, rekey epochs, peer records, and measured costs.
 * Run by tools/dev/build_native_gate.py --test. */
#include "lockstep_wire_crypto.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
static double now_us(void) {LARGE_INTEGER f,t;QueryPerformanceFrequency(&f);QueryPerformanceCounter(&t);return (double)t.QuadPart*1e6/(double)f.QuadPart;}
#else
#include <time.h>
static double now_us(void) {struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (double)t.tv_sec*1e6+(double)t.tv_nsec/1e3;}
#endif

static void fill(uint8_t *p,size_t n,uint8_t first) {for(size_t i=0;i<n;i++) p[i]=(uint8_t)(first+i);}
static void hex(char *out,const uint8_t *p,size_t n) {for(size_t i=0;i<n;i++) sprintf(out+2*i,"%02x",p[i]);out[2*n]=0;}
static int same_hex(const uint8_t *p,size_t n,const char *want) {
    static char got[2*WC_PLAIN_MAX+1];hex(got,p,n);
    if(strcmp(got,want)) {printf("golden mismatch\n got  %s\n want %s\n",got,want);return 0;}
    return 1;
}
static const uint8_t TSID[16]={0xa0,0xa1,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xab,0xac,0xad,0xae,0xaf};
/* Golden values of our byte layouts on fixed inputs, recomputed independently
 * (Python cryptography + hashlib) by tests/test_native_gate_crypto.py. */
#define G_SEAL "58344331010200000500000000000000483ec75c587d76b2f4de4678099fa13a6c55cc07a621a492"
#define G_SUBKEY0 "a93111c7fd91f1275f5e269aefed5a1aa351aa3e58c90748ab727a5dafce9a61"
#define G_RECORD "58344d5020696e707574207631000000a0a1a2a3a4a5a6a7a8a9aaabacadaeaf02030100d2040000000000000300010203"
#define G_RECORD_PK "d04ab232742bb4ab3a1368bd4615e4e6d0224ab71a016baf8520a332c9778737"
#define G_RECORD_SIG "d4a91965b02bce3a190560c12cae13467a970e828578827da4bab8c9a6d089e7f9fec7d44858f3faa2cd50e367b149b64872190274fb1ac9211d34d6385db80d"
#define G_RECORDS "cbb951750f032a73878482cf5f486fcda2c59f13ced5410a71d43346d2b9f93a"
#define G_PERMIT_SIG "3864a753a250be48d8ab8a92529defaf0c3d9e9f4f3f6b37d847fab0ef17a05b7d565af93169f79f9214a8ff8421e3613428024edf4a56a40f0f3d2e646df90d"
#define G_PSK2 "7184b68463bd0b83a5b386f47bffcc183e170b47ddb7b6c3d794a5d1467fc4a9"
#define G_HELLO "5834483101020301ff2ee45601ec1b67310c7790404585ae697331eee1c1f8cf2419731c1fff3e6bc6822637c7d310ec57627be00ba259d253749f4aaf644470cffbe53a35f7324222d281a13ad6125b8ee756153faef3f6"
#define G_WELCOME "5834483102020301219e4d800da968d2a5fcb009c784f4746c7138edb9ee4844b739e830b05cf424970034402ad1179b6dbbb739989bd75a902ac2eb6dff3e11fb5ab1070e99e1d757fae9ff7093a32f933dfeff879de2c8a09cdfb2848b3adef298424b3f43de0a"
#define G_C2R "58344331010200000000000000000000376fccc3ef0251d2135d2f012834b8a6f754cbfd6d536fbc"
#define G_R2C "583443310200020000000000000000004006c742b67d0fe88329a654dc3690ee5861150dfb6d"

static void test_layouts(void) {
    uint8_t base[32],out[256],plain[64];size_t n=0;
    fill(base,32,0);
    wc_tx t;wc_rx r;wc_tx_init(&t,base,WC_C2R,2,WC_RELAY,0);wc_rx_init(&r,base,WC_C2R,2,WC_RELAY,0);t.next=5;
    size_t size=wc_seal(&t,TSID,"lockstep",8,out,sizeof(out));
    assert(size==8+WC_OVERHEAD && t.next==6);
    assert(same_hex(out,size,G_SEAL));
    assert(wc_open(&r,TSID,out,size,plain,sizeof(plain),&n)==1 && n==8 && !memcmp(plain,"lockstep",8));
    uint8_t sub[32];wc_subkey(&t.k,0,sub);assert(same_hex(sub,32,G_SUBKEY0));
    /* Record, signature and permit on fixed keys. */
    uint8_t seed[32],sk[64],pk[32],sig[64],blob[3]={1,2,3},rec[WC_RECORD_MAX];
    memset(seed,0x11,32);crypto_ed25519_key_pair(sk,pk,seed);
    size_t rn=wc_record(rec,TSID,2,3,1,1234,blob,3);assert(rn==WC_RECORD_HEAD+3);
    assert(same_hex(rec,rn,G_RECORD));
    assert(wc_record_sign(sig,sk,TSID,2,3,1,1234,blob,3));assert(same_hex(pk,32,G_RECORD_PK));
    assert(same_hex(sig,64,G_RECORD_SIG));
    crypto_blake2b_ctx h;uint8_t records[32],zero[64]={0};
    wc_records_begin(&h);wc_records_slot(&h,NULL,0,zero);wc_records_slot(&h,blob,3,sig);wc_records_slot(&h,blob,1,zero);wc_records_end(&h,records);
    assert(same_hex(records,32,G_RECORDS));
    uint8_t rseed[32],rsk[64],rpk[32],psig[64];memset(rseed,0x22,32);crypto_ed25519_key_pair(rsk,rpk,rseed);
    assert(wc_permit_sign(psig,rsk,TSID,1236,3,1,records));assert(same_hex(psig,64,G_PERMIT_SIG));
    uint8_t master[32],psk[32];memset(master,0x33,32);wc_slot_psk(psk,master,TSID,2);assert(same_hex(psk,32,G_PSK2));
    /* A handshake on fixed seeds, then one sealed datagram each way. */
    wc_client c;uint8_t xs[32],ss[32],rx_seed[32],hello[WC_HELLO],welcome[WC_WELCOME],ppk[32];unsigned slot=0;
    memset(xs,0x44,32);memset(ss,0x55,32);memset(rx_seed,0x66,32);memset(rseed,0x22,32);
    wc_tx ct,rt;wc_rx cr,rr;
    assert(wc_client_hello(&c,TSID,psk,rpk,2,3,1,xs,ss,hello) && wc_zero(xs,32) && wc_zero(ss,32));
    assert(wc_relay_welcome(TSID,master,rsk,3,1,hello,WC_HELLO,rx_seed,welcome,&slot,ppk,&rr,&rt) && slot==2 && wc_zero(rx_seed,32));
    assert(wc_client_welcome(&c,welcome,WC_WELCOME,&ct,&cr) && !memcmp(ppk,c.sign_pk,32));
    assert(same_hex(hello,WC_HELLO,G_HELLO));
    assert(same_hex(welcome,WC_WELCOME,G_WELCOME));
    size=wc_seal(&ct,TSID,"NGKEYREQ",8,out,sizeof(out));assert(same_hex(out,size,G_C2R));
    assert(wc_open(&rr,TSID,out,size,plain,sizeof(plain),&n)==1 && n==8);
    size=wc_seal(&rt,TSID,"NGKEYS",6,out,sizeof(out));assert(same_hex(out,size,G_R2C));
    assert(wc_open(&cr,TSID,out,size,plain,sizeof(plain),&n)==1 && n==6);
    crypto_wipe(sk,sizeof(sk));crypto_wipe(rsk,sizeof(rsk));
}

static void pair(wc_tx *ct,wc_rx *cr,wc_tx *rt,wc_rx *rr,uint8_t player_sk[64],uint8_t player_pk[32],uint8_t relay_pk[32],uint8_t master[32],unsigned slot) {
    uint8_t rseed[32],rsk[64],psk[32],xs[32],ss[32],rx_seed[32],hello[WC_HELLO],welcome[WC_WELCOME],ppk[32];unsigned got=0;wc_client c;
    memset(rseed,0x22,32);crypto_ed25519_key_pair(rsk,relay_pk,rseed);
    memset(master,0x33,32);wc_slot_psk(psk,master,TSID,slot);
    memset(xs,0x40+slot,32);memset(ss,0x50+slot,32);memset(rx_seed,0x60+slot,32);
    assert(wc_client_hello(&c,TSID,psk,relay_pk,slot,3,1,xs,ss,hello));
    assert(wc_relay_welcome(TSID,master,rsk,3,1,hello,WC_HELLO,rx_seed,welcome,&got,ppk,rr,rt) && got==slot);
    assert(wc_client_welcome(&c,welcome,WC_WELCOME,ct,cr));
    memcpy(player_sk,c.sign_sk,64);memcpy(player_pk,c.sign_pk,32);
}

static void test_tamper_and_replay(void) {
    wc_tx ct,rt;wc_rx cr,rr;uint8_t sk[64],pk[32],rpk[32],master[32],out[512],plain[512];size_t n;
    pair(&ct,&cr,&rt,&rr,sk,pk,rpk,master,1);
    const char *msg="NGSTATE|4|aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa|1|7|...";size_t len=strlen(msg);
    size_t size=wc_seal(&ct,TSID,msg,len,out,sizeof(out));assert(size==len+WC_OVERHEAD);
    /* Every single-bit flip anywhere in the datagram is refused, and refused without moving the window. */
    for(size_t i=0;i<size;i++) for(unsigned b=0;b<8;b++) {
        out[i]^=(uint8_t)(1u<<b);
        int r=wc_open(&rr,TSID,out,size,plain,sizeof(plain),&n);
        assert(r==0 || (i>=8 && i<16 && r==-1)); /* a flipped counter may land on a seen or too-old counter */
        out[i]^=(uint8_t)(1u<<b);
    }
    assert(rr.opened==0 && !rr.any);
    /* Truncation and extension. */
    assert(wc_open(&rr,TSID,out,size-1,plain,sizeof(plain),&n)==0 && wc_open(&rr,TSID,out,WC_OVERHEAD-1,plain,sizeof(plain),&n)==0);
    out[size]=0;assert(wc_open(&rr,TSID,out,size+1,plain,sizeof(plain),&n)==0);
    /* Another session id (the token) is another session. */
    uint8_t other[16];memcpy(other,TSID,16);other[15]^=1;
    assert(wc_open(&rr,other,out,size,plain,sizeof(plain),&n)==0);
    /* The wrong direction: the relay's own channel to that player. */
    wc_rx wrong;memcpy(&wrong,&cr,sizeof(wrong));assert(wc_open(&wrong,TSID,out,size,plain,sizeof(plain),&n)==0);
    /* Opens once; the same datagram again is a replay. */
    assert(wc_open(&rr,TSID,out,size,plain,sizeof(plain),&n)==1 && n==len && !memcmp(plain,msg,len));
    assert(wc_open(&rr,TSID,out,size,plain,sizeof(plain),&n)==-1 && rr.replayed==1);
    /* Reordering inside the window is fine, each counter once. */
    uint8_t d[40][128];size_t s[40];
    for(unsigned i=0;i<40;i++) s[i]=wc_seal(&ct,TSID,"x",1,d[i],sizeof(d[i]));
    for(int i=39;i>=0;i-=2) assert(wc_open(&rr,TSID,d[i],s[i],plain,sizeof(plain),&n)==1);
    for(int i=0;i<40;i+=2) assert(wc_open(&rr,TSID,d[i],s[i],plain,sizeof(plain),&n)==1);
    for(int i=0;i<40;i++) assert(wc_open(&rr,TSID,d[i],s[i],plain,sizeof(plain),&n)==-1);
    /* A datagram older than the window is refused even if never seen. */
    uint8_t old[128];size_t oldn=wc_seal(&ct,TSID,"o",1,old,sizeof(old));
    ct.next+=WC_WINDOW+5;
    uint8_t fresh[128];size_t freshn=wc_seal(&ct,TSID,"f",1,fresh,sizeof(fresh));
    assert(wc_open(&rr,TSID,fresh,freshn,plain,sizeof(plain),&n)==1);
    assert(wc_open(&rr,TSID,old,oldn,plain,sizeof(plain),&n)==-1);
    /* A forged datagram with a far-future counter is refused and does not move the window. */
    uint64_t top=rr.top;uint8_t forged[128];memcpy(forged,fresh,freshn);wc_le64(forged+8,top+100000);
    assert(wc_open(&rr,TSID,forged,freshn,plain,sizeof(plain),&n)==0 && rr.top==top);
    /* Counter exhaustion refuses to seal instead of reusing a nonce. */
    ct.next=UINT64_MAX;assert(!wc_seal(&ct,TSID,"x",1,out,sizeof(out)));
    /* Plain protocol datagrams are not sealed datagrams. */
    const char *plainwire="NGGO|4|aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa|1|2|1||";
    assert(wc_open(&cr,TSID,(const uint8_t*)plainwire,strlen(plainwire),plain,sizeof(plain),&n)==0);
}

static void test_epochs(void) {
    uint8_t base[32],out[64],plain[64],c1[64],c2[64];size_t n;fill(base,32,7);
    wc_tx t;wc_rx r;wc_tx_init(&t,base,WC_R2C,0,3,0);wc_rx_init(&r,base,WC_R2C,0,3,0);t.k.epoch_shift=r.k.epoch_shift=2;
    for(unsigned i=0;i<40;i++) {size_t s=wc_seal(&t,TSID,"epoch",5,out,sizeof(out));assert(wc_open(&r,TSID,out,s,plain,sizeof(plain),&n)==1);}
    /* The same plaintext at the same counter offset in two epochs uses two subkeys. */
    wc_tx a,b;memcpy(&a,&t,sizeof(a));memcpy(&b,&t,sizeof(b));a.next=4;b.next=8;
    wc_seal(&a,TSID,"same",4,c1,sizeof(c1));wc_seal(&b,TSID,"same",4,c2,sizeof(c2));
    assert(memcmp(c1+WC_HEADER,c2+WC_HEADER,4+WC_TAG));
    uint8_t k0[32],k1[32];wc_subkey(&t.k,0,k0);wc_subkey(&t.k,1,k1);assert(memcmp(k0,k1,32));
}

static void test_handshake_refusals(void) {
    uint8_t rseed[32],rsk[64],rpk[32],master[32],psk[32],xs[32],ss[32],rxs[32],hello[WC_HELLO],welcome[WC_WELCOME],ppk[32];
    unsigned slot=0;wc_client c;wc_tx ct,rt;wc_rx cr,rr;
    memset(rseed,0x22,32);crypto_ed25519_key_pair(rsk,rpk,rseed);memset(master,0x33,32);
    /* A player with another slot's psk (key mismatch) is refused by the relay. */
    wc_slot_psk(psk,master,TSID,1);memset(xs,1,32);memset(ss,2,32);
    assert(wc_client_hello(&c,TSID,psk,rpk,2,3,0,xs,ss,hello));
    memset(rxs,3,32);assert(!wc_relay_welcome(TSID,master,rsk,3,0,hello,WC_HELLO,rxs,welcome,&slot,ppk,&rr,&rt) && wc_zero(rxs,32));
    /* A HELLO for another session, window or player count is refused. */
    wc_slot_psk(psk,master,TSID,2);memset(xs,1,32);memset(ss,2,32);assert(wc_client_hello(&c,TSID,psk,rpk,2,3,0,xs,ss,hello));
    uint8_t other[16];memcpy(other,TSID,16);other[0]^=1;
    memset(rxs,3,32);assert(!wc_relay_welcome(other,master,rsk,3,0,hello,WC_HELLO,rxs,welcome,&slot,ppk,&rr,&rt));
    memset(rxs,3,32);assert(!wc_relay_welcome(TSID,master,rsk,3,1,hello,WC_HELLO,rxs,welcome,&slot,ppk,&rr,&rt));
    memset(rxs,3,32);assert(!wc_relay_welcome(TSID,master,rsk,4,0,hello,WC_HELLO,rxs,welcome,&slot,ppk,&rr,&rt));
    /* Any changed HELLO byte (slot claim, keys, MAC) is refused. */
    for(unsigned i=0;i<WC_HELLO;i++) {
        hello[i]^=0x10;memset(rxs,3,32);
        assert(!wc_relay_welcome(TSID,master,rsk,3,0,hello,WC_HELLO,rxs,welcome,&slot,ppk,&rr,&rt));
        hello[i]^=0x10;
    }
    /* The genuine HELLO is answered. */
    memset(rxs,3,32);assert(wc_relay_welcome(TSID,master,rsk,3,0,hello,WC_HELLO,rxs,welcome,&slot,ppk,&rr,&rt) && slot==2);
    /* A WELCOME signed by another relay key (relay key mismatch, or a man in
     * the middle) is ignored by the player; so is any changed WELCOME byte. */
    wc_client copy=c;uint8_t evil_seed[32],evil_sk[64],evil_pk[32],evil[WC_WELCOME];unsigned s2;
    memset(evil_seed,0x77,32);crypto_ed25519_key_pair(evil_sk,evil_pk,evil_seed);
    memset(rxs,3,32);assert(wc_relay_welcome(TSID,master,evil_sk,3,0,hello,WC_HELLO,rxs,evil,&s2,ppk,&rr,&rt));
    assert(!wc_client_welcome(&copy,evil,WC_WELCOME,&ct,&cr) && copy.state==1);
    for(unsigned i=0;i<WC_WELCOME;i++) {welcome[i]^=0x04;assert(!wc_client_welcome(&copy,welcome,WC_WELCOME,&ct,&cr));welcome[i]^=0x04;}
    assert(wc_client_welcome(&copy,welcome,WC_WELCOME,&ct,&cr) && copy.state==2 && wc_zero(copy.x_secret,32));
    /* An established client takes no second WELCOME. */
    assert(!wc_client_welcome(&copy,welcome,WC_WELCOME,&ct,&cr));
    /* A low-order ephemeral key (all-zero shared secret) is refused by the relay. */
    uint8_t low[WC_HELLO];memcpy(low,hello,WC_HELLO);memset(low+8,0,32);wc_hello_mac(low+72,psk,TSID,low);
    memset(rxs,3,32);assert(!wc_relay_welcome(TSID,master,rsk,3,0,low,WC_HELLO,rxs,welcome,&slot,ppk,&rr,&rt));
    /* Credentials. */
    char cred[WC_CREDENTIAL_LENGTH+1];uint8_t a[32],b[32];
    strcpy(cred,"x4c1:");hex(cred+5,rpk,32);cred[69]=':';hex(cred+70,psk,32);
    assert(wc_credential_parse(cred,strlen(cred),a,b) && !memcmp(a,rpk,32) && !memcmp(b,psk,32));
    cred[10]='A';assert(!wc_credential_parse(cred,strlen(cred),a,b));cred[10]='0';
    assert(!wc_credential_parse(cred,strlen(cred)-1,a,b));
    cred[3]='2';assert(!wc_credential_parse(cred,strlen(cred),a,b));
}

static void test_records(void) {
    uint8_t s1[32],s2[32],sk1[64],pk1[32],sk2[64],pk2[32],sig[64],blob[240];
    memset(s1,0x31,32);memset(s2,0x32,32);crypto_ed25519_key_pair(sk1,pk1,s1);crypto_ed25519_key_pair(sk2,pk2,s2);fill(blob,240,9);
    assert(wc_record_sign(sig,sk2,TSID,2,3,1,500,blob,240));
    assert(wc_record_verify(sig,pk2,TSID,2,3,1,500,blob,240));
    /* Wrong slot: slot 1 signs a record that names slot 2 (or verifies as slot 1). */
    uint8_t forged[64];assert(wc_record_sign(forged,sk1,TSID,2,3,1,500,blob,240));
    assert(!wc_record_verify(forged,pk2,TSID,2,3,1,500,blob,240));
    assert(!wc_record_verify(sig,pk2,TSID,1,3,1,500,blob,240) && !wc_record_verify(sig,pk1,TSID,2,3,1,500,blob,240));
    /* Any other tick, blob, session, size or configuration fails. */
    assert(!wc_record_verify(sig,pk2,TSID,2,3,1,501,blob,240) && !wc_record_verify(sig,pk2,TSID,2,3,1,499,blob,240));
    assert(!wc_record_verify(sig,pk2,TSID,2,4,1,500,blob,240) && !wc_record_verify(sig,pk2,TSID,2,3,2,500,blob,240));
    assert(!wc_record_verify(sig,pk2,TSID,2,3,1,500,blob,239));
    blob[100]^=1;assert(!wc_record_verify(sig,pk2,TSID,2,3,1,500,blob,240));blob[100]^=1;
    uint8_t other[16];memcpy(other,TSID,16);other[3]^=0x80;assert(!wc_record_verify(sig,pk2,other,2,3,1,500,blob,240));
    for(unsigned i=0;i<64;i++) {sig[i]^=0x20;assert(!wc_record_verify(sig,pk2,TSID,2,3,1,500,blob,240));sig[i]^=0x20;}
    /* Out of range arguments are refused, never signed. */
    assert(!wc_record_sign(sig,sk2,TSID,4,3,1,500,blob,1) && !wc_record_sign(sig,sk2,TSID,0,3,1,500,blob,1));
    assert(!wc_record_sign(sig,sk2,TSID,1,9,1,500,blob,1) && !wc_record_sign(sig,sk2,TSID,1,3,9,500,blob,1) && !wc_record_sign(sig,sk2,TSID,1,3,1,500,blob,241));
    /* An empty blob is a record too: the relay cannot silently drop a player's input. */
    assert(wc_record_sign(sig,sk1,TSID,1,3,0,7,NULL,0) && wc_record_verify(sig,pk1,TSID,1,3,0,7,NULL,0) && !wc_record_verify(sig,pk1,TSID,1,3,0,7,blob,1));
    /* Permits. */
    uint8_t rseed[32],rsk[64],rpk[32],records[32],psig[64];memset(rseed,0x22,32);crypto_ed25519_key_pair(rsk,rpk,rseed);
    crypto_blake2b_ctx h;wc_records_begin(&h);wc_records_slot(&h,blob,3,sig);wc_records_end(&h,records);
    assert(wc_permit_sign(psig,rsk,TSID,12,3,1,records) && wc_permit_verify(psig,rpk,TSID,12,3,1,records));
    assert(!wc_permit_verify(psig,rpk,TSID,13,3,1,records) && !wc_permit_verify(psig,rpk,TSID,12,2,1,records) && !wc_permit_verify(psig,rpk,TSID,12,3,0,records));
    assert(!wc_permit_verify(psig,pk1,TSID,12,3,1,records));
    records[0]^=1;assert(!wc_permit_verify(psig,rpk,TSID,12,3,1,records));
    assert(!wc_permit_sign(psig,rsk,TSID,0,3,1,records));
    assert(wc_permit_source(1,0)==0 && wc_permit_source(5,0)==4 && wc_permit_source(1,1)==WC_NO_SOURCE && wc_permit_source(2,1)==0 && wc_permit_source(3,1)==1);
    /* Roster checks. */
    uint8_t roster[32*4];memset(roster,0x99,32);memcpy(roster+32,pk1,32);memcpy(roster+64,pk2,32);memset(roster+96,0x5a,32);
    assert(wc_roster_check(roster,sizeof(roster),3,2,pk2) && wc_roster_check(roster,sizeof(roster),3,1,pk1));
    assert(!wc_roster_check(roster,sizeof(roster),3,1,pk2) && !wc_roster_check(roster,sizeof(roster)-1,3,2,pk2) && !wc_roster_check(roster,sizeof(roster),4,2,pk2));
    memcpy(roster+96,pk1,32);assert(!wc_roster_check(roster,sizeof(roster),3,2,pk2)); /* duplicate key */
    memset(roster+96,0,32);assert(!wc_roster_check(roster,sizeof(roster),3,2,pk2));
    memset(roster+96,0x5a,32);memset(roster,0,32);assert(!wc_roster_check(roster,sizeof(roster),3,2,pk2)); /* zero group key */
}

static void test_peer(void) {
    uint8_t group[32],s[3][32],sk[3][64],roster[3][32],out[WC_PEER_MAX],blob[240],got[240],sig[64];size_t n;unsigned slot;uint64_t tick;
    fill(group,32,0x70);
    for(unsigned i=0;i<3;i++) {memset(s[i],0x81+i,32);crypto_ed25519_key_pair(sk[i],roster[i],s[i]);}
    wc_key keys[3];for(unsigned i=0;i<3;i++) wc_peer_key(&keys[i],group,TSID,1,i+1);
    wc_tx t2;memset(&t2,0,sizeof(t2));wc_peer_key(&t2.k,group,TSID,1,2);
    fill(blob,240,1);
    size_t size=wc_peer_seal(&t2,TSID,sk[1],3,1,77,blob,17,out,sizeof(out));assert(size==WC_OVERHEAD+WC_PEER_HEAD+17+WC_SIG);
    assert(wc_peer_open(keys,(const uint8_t(*)[32])roster,TSID,3,1,out,size,&slot,&tick,got,&n,sig)==1 && slot==2 && tick==77 && n==17 && !memcmp(got,blob,17));
    /* A record is idempotent: the same datagram opens again (callers dedupe by slot and tick). */
    assert(wc_peer_open(keys,(const uint8_t(*)[32])roster,TSID,3,1,out,size,&slot,&tick,got,&n,sig)==1);
    /* An outsider without the group key: nothing opens, nothing is accepted. */
    for(size_t i=0;i<size;i++) {out[i]^=0x01;assert(wc_peer_open(keys,(const uint8_t(*)[32])roster,TSID,3,1,out,size,&slot,&tick,got,&n,sig)<=0);out[i]^=0x01;}
    uint8_t outsider[32];fill(outsider,32,0x71);wc_tx to;memset(&to,0,sizeof(to));wc_peer_key(&to.k,outsider,TSID,1,2);
    size=wc_peer_seal(&to,TSID,sk[1],3,1,78,blob,4,out,sizeof(out));
    assert(wc_peer_open(keys,(const uint8_t(*)[32])roster,TSID,3,1,out,size,&slot,&tick,got,&n,sig)==0);
    /* Player 1 holds the group key and seals a record as sender 2, signed with its own key: -2. */
    wc_tx forger;memset(&forger,0,sizeof(forger));wc_peer_key(&forger.k,group,TSID,1,2);
    uint8_t p[WC_PEER_HEAD+4+WC_SIG];p[0]=2;p[1]=3;p[2]=1;p[3]=0;wc_le64(p+4,79);p[12]=4;p[13]=0;memcpy(p+14,blob,4);
    assert(wc_record_sign(p+18,sk[0],TSID,2,3,1,79,blob,4));
    size=wc_seal(&forger,TSID,p,sizeof(p),out,sizeof(out));
    assert(wc_peer_open(keys,(const uint8_t(*)[32])roster,TSID,3,1,out,size,&slot,&tick,got,&n,sig)==-2);
    /* ...or replays sender 2's genuine record under another tick: -2. */
    assert(wc_record_sign(p+18,sk[1],TSID,2,3,1,79,blob,4));wc_le64(p+4,80);
    size=wc_seal(&forger,TSID,p,sizeof(p),out,sizeof(out));
    assert(wc_peer_open(keys,(const uint8_t(*)[32])roster,TSID,3,1,out,size,&slot,&tick,got,&n,sig)==-2);
    /* A sender's own subkey cannot be passed off as another sender's. */
    size=wc_peer_seal(&t2,TSID,sk[1],3,1,81,blob,4,out,sizeof(out));out[5]=3;
    assert(wc_peer_open(keys,(const uint8_t(*)[32])roster,TSID,3,1,out,size,&slot,&tick,got,&n,sig)==0);
}

typedef struct {const char *name;double us;} cost;
static double bench(void (*fn)(void*),void *arg,unsigned rounds) {
    fn(arg);double start=now_us();for(unsigned i=0;i<rounds;i++) fn(arg);return (now_us()-start)/rounds;
}
static uint8_t B_sk[64],B_pk[32],B_sig[64],B_blob[240],B_out[8192+64],B_plain[8192+64],B_base[32],B_x[32],B_y[32];
static size_t B_n,B_size;static wc_tx B_tx;static wc_rx B_rx;
static void b_seal(void *a) {(void)a;B_size=wc_seal(&B_tx,TSID,B_plain,B_n,B_out,sizeof(B_out));}
static void b_open(void *a) {(void)a;size_t n;B_rx.any=0;(void)wc_open(&B_rx,TSID,B_out,B_size,B_plain,sizeof(B_plain),&n);}
static void b_sign(void *a) {(void)a;wc_record_sign(B_sig,B_sk,TSID,1,8,1,1000,B_blob,(size_t)(uintptr_t)a);}
static void b_verify(void *a) {(void)a;int ok=wc_record_verify(B_sig,B_pk,TSID,1,8,1,1000,B_blob,(size_t)(uintptr_t)a);assert(ok);}
static void b_x25519(void *a) {(void)a;crypto_x25519(B_y,B_x,B_pk);}
static void b_hash(void *a) {(void)a;crypto_blake2b_ctx h;wc_records_begin(&h);for(unsigned s=0;s<8;s++) wc_records_slot(&h,B_blob,240,B_sig);wc_records_end(&h,B_y);}
static void test_costs(void) {
    uint8_t seed[32];memset(seed,0x5c,32);crypto_ed25519_key_pair(B_sk,B_pk,seed);fill(B_blob,240,3);fill(B_base,32,1);fill(B_x,32,9);
    wc_tx_init(&B_tx,B_base,WC_C2R,1,0,0);wc_rx_init(&B_rx,B_base,WC_C2R,1,0,0);
    /* Typical datagrams: a protocol 4 report with an empty blob plus the
     * signature trailer (about 245 bytes), the largest report (714), a
     * two-player permit with 28-byte pilot records (about 400) and the
     * largest eight-player permit (3,912 + 8*64 + 64 + 1 = 4,489). */
    static const size_t sizes[]={245,714,400,4489};double seal[4],open[4];
    for(unsigned i=0;i<4;i++) {B_n=sizes[i];fill(B_plain,B_n,0x20);seal[i]=bench(b_seal,NULL,4000);open[i]=bench(b_open,NULL,4000);}
    wc_record_sign(B_sig,B_sk,TSID,1,8,1,1000,B_blob,28);
    double sign28=bench(b_sign,(void*)(uintptr_t)28,2000),verify28=bench(b_verify,(void*)(uintptr_t)28,2000);
    wc_record_sign(B_sig,B_sk,TSID,1,8,1,1000,B_blob,240);
    double sign240=bench(b_sign,(void*)(uintptr_t)240,2000),verify240=bench(b_verify,(void*)(uintptr_t)240,2000);
    double dh=bench(b_x25519,NULL,1000),hash=bench(b_hash,NULL,4000);
    for(unsigned i=0;i<4;i++) printf("WIRE_CRYPTO_COST datagram bytes=%zu seal_us=%.2f open_us=%.2f\n",sizes[i],seal[i],open[i]);
    printf("WIRE_CRYPTO_COST record_sign_us blob28=%.1f blob240=%.1f record_verify_us blob28=%.1f blob240=%.1f x25519_us=%.1f records_hash8_us=%.2f\n",
           sign28,sign240,verify28,verify240,dh,hash);
    /* Per tick, protocol 4, pilot records: a client signs its report, seals it
     * and opens one permit with N-1 other signatures and the relay's; the relay
     * opens N reports, verifies N signatures, signs one permit and seals N. */
    for(unsigned clients=2;clients<=8;clients+=6) {
        double client=sign28+seal[0]+open[clients==2?2:3]+(clients-1)*verify28+verify28+hash;
        double relay=clients*(open[0]+verify28+seal[clients==2?2:3])+sign28+hash;
        printf("WIRE_CRYPTO_COST tick clients=%u client_us=%.0f relay_us=%.0f\n",clients,client,relay);
    }
}

int main(void) {
    assert(wc_selftest()==0);
    test_layouts();
    test_tamper_and_replay();
    test_epochs();
    test_handshake_refusals();
    test_records();
    test_peer();
    test_costs();
    printf("wire crypto tests passed\n");
    return 0;
}
