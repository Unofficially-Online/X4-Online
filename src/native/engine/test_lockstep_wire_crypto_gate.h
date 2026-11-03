/* Wire crypto on the gate's relay path (lockstep_wire_crypto_gate.h) against a
 * loopback UDP socket standing in for the relay, which uses the same library
 * functions as the Python relay. Covers configuration, the handshake (a forged
 * WELCOME is ignored), the signed report trailer, protocol 4 permits with every
 * signature checked, replays and outsiders dropped, authentic permits with a
 * wrong-slot record signature or another relay key stopping with 106, and the
 * handshake timeout (107). No engine hooks or native simulation are exercised. */
#define WCT_TOKEN "a0a1a2a3a4a5a6a7a8a9aaabacadaeaf"
static SOCKET wct_relay=INVALID_SOCKET;
static struct sockaddr_in wct_client;
static uint8_t wct_sid[16],wct_master[32],wct_relay_sk[64],wct_relay_pk[32],wct_evil_sk[64];
static uint8_t wct_sk[4][64],wct_pk[4][32],wct_group[32];
static wc_tx wct_to_client;static wc_rx wct_from_client;
static int wct_mode; /* 0 the relay answers; 1 a forged WELCOME first; 2 forged WELCOMEs only */
static void wct_send(const uint8_t *data,size_t n) {
    assert(sendto(wct_relay,(const char*)data,(int)n,0,(struct sockaddr*)&wct_client,sizeof(wct_client))==(int)n);
}
static size_t wct_seal_send(const void *plain,size_t n,uint8_t *keep) {
    uint8_t out[GATE_PERMIT_MAX+WC_OVERHEAD];size_t size=wc_seal(&wct_to_client,wct_sid,plain,n,out,sizeof(out));
    assert(size);wct_send(out,size);if(keep) memcpy(keep,out,size);
    return size;
}
static size_t wct_keys(uint8_t *out,const uint8_t pk3[32]) {
    int h=snprintf((char*)out,96,"NGKEYS|1|%s|2|1",WCT_TOKEN);
    memcpy(out+h+1,wct_group,32);memcpy(out+h+1+32,wct_pk[1],32);memcpy(out+h+1+64,wct_pk[2],32);memcpy(out+h+1+96,pk3,32);
    return (size_t)h+1+32*4;
}
/* The relay's side of the handshake: WELCOME, then NGKEYS. */
static DWORD WINAPI wct_relay_thread(LPVOID unused) {
    (void)unused;uint8_t data[GATE_PERMIT_MAX+WC_OVERHEAD],plain[GATE_PERMIT_MAX];ULONGLONG start=GetTickCount64();int welcomed=0;
    while(GetTickCount64()-start<3000) {
        int n=recv(wct_relay,(char*)data,sizeof(data),0);
        if(n<0) continue;
        if(!welcomed && n==WC_HELLO) {
            uint8_t seed[32],welcome[WC_WELCOME],player_pk[32];unsigned slot=0;wc_rx rx;wc_tx tx;
            if(wct_mode) {
                /* Signed by another key, as a man in the middle would: the gate must ignore it. */
                memset(seed,0x12,32);
                if(!wc_relay_welcome(wct_sid,wct_master,wct_evil_sk,3,2,data,(size_t)n,seed,welcome,&slot,player_pk,&rx,&tx)) return 2;
                wct_send(welcome,WC_WELCOME);
                if(wct_mode==2) return 0;
            }
            memset(seed,0x34,32);
            if(!wc_relay_welcome(wct_sid,wct_master,wct_relay_sk,3,2,data,(size_t)n,seed,welcome,&slot,player_pk,&wct_from_client,&wct_to_client) ||
               slot!=2) return 3;
            memcpy(wct_pk[2],player_pk,32);
            wct_send(welcome,WC_WELCOME);welcomed=1;continue;
        }
        size_t size=0;
        if(welcomed && wc_open(&wct_from_client,wct_sid,data,(size_t)n,plain,sizeof(plain),&size)==1) {
            char want[96];snprintf(want,sizeof(want),"NGKEYREQ|1|%s|2",WCT_TOKEN);
            if(size!=strlen(want) || memcmp(plain,want,size)) return 4;
            uint8_t keys[256];wct_seal_send(keys,wct_keys(keys,wct_pk[3]),NULL);
            return 0;
        }
    }
    return 1;
}
static void wct_blob(unsigned slot,uint64_t tick,char out[GATE_BLOB_HEX_MAX+1]) {
    if((tick+slot)%3==0) {out[0]=0;return;}
    snprintf(out,GATE_BLOB_HEX_MAX+1,"%02x%02x",slot,(unsigned)(tick&0xff));
}
/* The permit for tick T (window k=2, three clients) with its trailer;
 * signer[s] is whose key signs slot s's record, relay_sk signs the permit. */
static size_t wct_permit(uint8_t *out,size_t capacity,uint64_t tick,const unsigned signer[4],const uint8_t relay_sk[64]) {
    int n=snprintf((char*)out,capacity,"NGGO|4|%s|2|%llu|2",WCT_TOKEN,(unsigned long long)tick);
    uint64_t source=wc_permit_source(tick,2);char blobs[4][GATE_BLOB_HEX_MAX+1];
    for(unsigned s=1;s<=3;s++) {
        blobs[s][0]=0;if(source!=WC_NO_SOURCE) wct_blob(s,source,blobs[s]);
        n+=snprintf((char*)out+n,capacity-(size_t)n,"|%s",blobs[s]);
    }
    out[n++]=0;crypto_blake2b_ctx h;wc_records_begin(&h);
    for(unsigned s=1;s<=3;s++) {
        uint8_t blob[WC_BLOB_MAX],*sig=out+n+WC_SIG*(s-1);size_t bytes=strlen(blobs[s])/2;assert(wc_unhex(blob,blobs[s],bytes));
        if(source==WC_NO_SOURCE) memset(sig,0,WC_SIG);
        else assert(wc_record_sign(sig,wct_sk[signer[s]],wct_sid,s,3,2,source,blob,bytes));
        wc_records_slot(&h,blob,bytes,sig);
    }
    uint8_t records[32];wc_records_end(&h,records);
    assert(wc_permit_sign(out+n+WC_SIG*3,relay_sk,wct_sid,tick,3,2,records));
    return (size_t)n+WC_SIG*4;
}
/* This client's report of tick (blob wct_blob(2,tick)), prepared: signed trailer appended. */
static int wct_report(char *packet,size_t capacity,uint64_t tick) {
    wct_blob(2,tick,blob_proposal);
    int length=snprintf(packet,capacity,"NGSTATE|4|%s|2|%llu|%016llx|%016llx|%s|3|2|%s",WCT_TOKEN,(unsigned long long)tick,0ULL,0ULL,
                        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",blob_proposal);
    assert(length>0 && !wcg_prepare(packet,&length,capacity));
    return length;
}
static void wct_handshake(int mode,unsigned timeout,unsigned expect) {
    wct_mode=mode;timeout_ms=timeout;
    HANDLE thread=CreateThread(NULL,0,wct_relay_thread,NULL,0,NULL);assert(thread);
    assert(wcg_establish()==expect);
    assert(WaitForSingleObject(thread,4000)==WAIT_OBJECT_0);DWORD code=9;GetExitCodeThread(thread,&code);CloseHandle(thread);assert(!code);
}
static void test_wire_crypto_gate(void) {
    WSADATA w;assert(!WSAStartup(MAKEWORD(2,2),&w));
    gate_stats saved=stats;char saved_secret[sizeof(secret)];memcpy(saved_secret,secret,sizeof(secret));
    SOCKET saved_connection=connection;unsigned saved_timeout=timeout_ms;double saved_game=*(double*)(image+GAME_RVA);
    /* Keys: the relay's (and a wrong one), the psk master, players 1 and 3, the group key. */
    uint8_t seed[32];
    assert(wc_unhex(wct_sid,WCT_TOKEN,16));memset(wct_master,0x33,32);
    memset(seed,0x22,32);crypto_ed25519_key_pair(wct_relay_sk,wct_relay_pk,seed);
    uint8_t evil_pk[32];memset(seed,0x77,32);crypto_ed25519_key_pair(wct_evil_sk,evil_pk,seed);
    for(unsigned s=1;s<=3;s+=2) {memset(seed,0x40+s,32);crypto_ed25519_key_pair(wct_sk[s],wct_pk[s],seed);}
    memset(wct_group,0x5a,32);
    uint8_t psk[32];wc_slot_psk(psk,wct_master,wct_sid,2);
    char credential[WC_CREDENTIAL_LENGTH+1],other[WC_CREDENTIAL_LENGTH+1];
    strcpy(credential,"x4c1:");for(unsigned i=0;i<32;i++) sprintf(credential+5+2*i,"%02x",wct_relay_pk[i]);
    credential[69]=':';for(unsigned i=0;i<32;i++) sprintf(credential+70+2*i,"%02x",psk[i]);
    memcpy(other,credential,sizeof(other));other[100]=other[100]=='0'?'1':'0';
    /* Configuration: protocol 3 only, a well-formed credential, frozen once started. */
    memset(&wcg,0,sizeof(wcg));
    assert(x4gate_wire(2,2) && !wcg_configure(credential,strlen(credential)) && !wcg.armed);
    assert(x4gate_wire(3,3) && x4gate_lookahead(2));
    assert(!wcg_configure(credential,strlen(credential)-1) && !wcg_configure("x4c1:",5) && !wcg.armed);
    assert(wcg_configure(credential,strlen(credential)) && wcg.armed);
    used=1;assert(wcg_configure(credential,strlen(credential)) && !wcg_configure(other,strlen(other)));used=0;
    /* Loopback relay socket. */
    wct_relay=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);assert(wct_relay!=INVALID_SOCKET);
    struct sockaddr_in relay;memset(&relay,0,sizeof(relay));relay.sin_family=AF_INET;relay.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    int size=sizeof(relay);
    assert(!bind(wct_relay,(struct sockaddr*)&relay,sizeof(relay)) && !getsockname(wct_relay,(struct sockaddr*)&relay,&size));
    DWORD relay_ms=30,client_ms=25;
    assert(!setsockopt(wct_relay,SOL_SOCKET,SO_RCVTIMEO,(const char*)&relay_ms,sizeof(relay_ms)));
    connection=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);assert(connection!=INVALID_SOCKET);
    assert(!setsockopt(connection,SOL_SOCKET,SO_RCVTIMEO,(const char*)&client_ms,sizeof(client_ms)));
    assert(!connect(connection,(struct sockaddr*)&relay,sizeof(relay)));
    size=sizeof(wct_client);assert(!getsockname(connection,(struct sockaddr*)&wct_client,&size));
    strcpy(secret,WCT_TOKEN);stats.slot=2;lookahead_reset();
    stats.game=20.0;*(double*)(image+GAME_RVA)=20.0;stats.issued=stats.completed=0;stats.waits=0;
    /* A relay that only answers with another key's WELCOME (key mismatch, or a
     * man in the middle): never established, stop 107 at the timeout. */
    wct_handshake(2,400,WCG_STOP_HANDSHAKE);
    assert(!wcg.established && !wcg.keyed && wcg.hellos>=3 && wcg.dropped>=1);
    /* The handshake: a forged WELCOME is ignored, the relay's is taken, then NGKEYS. */
    {uint8_t junk[GATE_PERMIT_MAX];while(recv(wct_relay,(char*)junk,sizeof(junk),0)>=0) {}}
    memset(&wcg,0,sizeof(wcg));assert(wcg_configure(credential,strlen(credential)));
    wct_handshake(1,2000,0);
    assert(wcg.established && wcg.keyed && wcg.dropped>=1 && !wcg.stop);
    assert(!memcmp(wcg.roster[1],wcg.hs.sign_pk,32) && !memcmp(wcg.group,wct_group,32) && wc_zero(wcg.hs.x_secret,32) && wc_zero(wcg.psk,32));
    memcpy(wct_sk[2],wcg.hs.sign_sk,64); /* white box: the relay re-signs nothing, it forwards this client's signatures */
    /* The report carries a NUL and the signature of its input record. */
    char packet[GATE_STATE_MAX];int length=wct_report(packet,sizeof(packet),0),ascii=(int)strlen(packet);
    assert(length==ascii+1+(int)WC_SIG);
    uint8_t blob[WC_BLOB_MAX];size_t bytes=strlen(blob_proposal)/2;assert(wc_unhex(blob,blob_proposal,bytes));
    assert(wc_record_verify((uint8_t*)packet+ascii+1,wct_pk[2],wct_sid,2,3,2,0,blob,bytes));
    assert(!wc_record_verify((uint8_t*)packet+ascii+1,wct_pk[2],wct_sid,2,3,2,1,blob,bytes));
    /* Protocol 4 over the sealed wire: permits 1..3 queued, frames 0..2. The relay
     * opens each report; the token never crosses the wire in the clear. */
    unsigned honest[4]={0,1,2,3};uint8_t permit[GATE_PERMIT_MAX],keep[GATE_PERMIT_MAX+WC_OVERHEAD],data[GATE_PERMIT_MAX+WC_OVERHEAD],plain[GATE_PERMIT_MAX];
    size_t kept=0,got=0;
    for(uint64_t t=1;t<=3;t++) kept=wct_seal_send(permit,wct_permit(permit,sizeof(permit),t,honest,wct_relay_sk),keep);
    assert(!lookahead_frame(packet,(size_t)length,la_reply,sizeof(la_reply)) && !blob_inputs[0][0] && !blob_inputs[2][0]);
    int n=recv(wct_relay,(char*)data,sizeof(data),0);assert(n==length+(int)WC_OVERHEAD);
    for(int i=0;i+32<=n;i++) assert(memcmp(data+i,WCT_TOKEN,32));
    assert(wc_open(&wct_from_client,wct_sid,data,(size_t)n,plain,sizeof(plain),&got)==1 && got==(size_t)length && !memcmp(plain,packet,got));
    for(uint64_t t=1;t<=2;t++) {
        stats.completed=++stats.issued;length=wct_report(packet,sizeof(packet),t);
        assert(!lookahead_frame(packet,(size_t)length,la_reply,sizeof(la_reply)));
    }
    char expected[GATE_BLOB_HEX_MAX+1];
    for(unsigned s=1;s<=3;s++) {wct_blob(s,0,expected);assert(!strcmp(blob_inputs[s-1],expected));}
    assert(wcg.permits==3 && wcg.records==2 && wcg.reports>=3 && !wcg.stop);
    /* A replayed sealed permit, an unsealed one and garbage are dropped, nothing stops. */
    uint64_t replayed=wcg.rx.replayed,dropped=wcg.dropped;
    wct_send(keep,kept);assert(gate_wire_recv(la_reply,(int)sizeof(la_reply)-1)==0 && wcg.rx.replayed==replayed+1);
    const char *clear="NGGO|4|" WCT_TOKEN "|2|4|2|0101||0301";wct_send((const uint8_t*)clear,strlen(clear));
    assert(gate_wire_recv(la_reply,(int)sizeof(la_reply)-1)==0);
    memcpy(data,keep,kept);data[kept-1]^=1;wct_send(data,kept);assert(gate_wire_recv(la_reply,(int)sizeof(la_reply)-1)==0);
    assert(wcg.dropped==dropped+3 && !wcg.stop);
    /* A fresh copy of a verified permit is not verified again; a repeated NGKEYS is consumed. */
    size_t pn=wct_permit(permit,sizeof(permit),3,honest,wct_relay_sk);wct_seal_send(permit,pn,NULL);
    assert(gate_wire_recv(la_reply,(int)sizeof(la_reply)-1)>0 && wcg.cached==1 && wcg.permits==3);
    uint8_t keys[256];wct_seal_send(keys,wct_keys(keys,wct_pk[3]),NULL);
    assert(gate_wire_recv(la_reply,(int)sizeof(la_reply)-1)==0 && !wcg.stop);
    /* Fail closed on authentic but invalid content: slot 1's record signed by slot 3 (stop 106 in the wait). */
    unsigned wrong[4]={0,3,2,3};pn=wct_permit(permit,sizeof(permit),4,wrong,wct_relay_sk);wct_seal_send(permit,pn,NULL);
    stats.completed=++stats.issued;length=wct_report(packet,sizeof(packet),3);
    assert(lookahead_frame(packet,(size_t)length,la_reply,sizeof(la_reply))==WCG_STOP_INVALID);
    /* ... a permit signed by another relay key; a different roster after the first (here a duplicate key). */
    wcg.stop=0;pn=wct_permit(permit,sizeof(permit),4,honest,wct_evil_sk);wct_seal_send(permit,pn,NULL);
    assert(gate_wire_recv(la_reply,(int)sizeof(la_reply)-1)==0 && wcg.stop==WCG_STOP_INVALID);
    wcg.stop=0;wct_seal_send(keys,wct_keys(keys,wct_pk[1]),NULL);
    assert(gate_wire_recv(la_reply,(int)sizeof(la_reply)-1)==0 && wcg.stop==WCG_STOP_INVALID);
    /* ... and an authentic permit without its signatures. */
    wcg.stop=0;pn=wct_permit(permit,sizeof(permit),4,honest,wct_relay_sk);wct_seal_send(permit,pn-WC_SIG,NULL);
    assert(gate_wire_recv(la_reply,(int)sizeof(la_reply)-1)==0 && wcg.stop==WCG_STOP_INVALID);
    /* Protocol 3 (k=0) permits through the same check: source tick T-1, no window field. */
    {
        wcg.stop=0;wire_lookahead=0;char text[GATE_PERMIT_MAX];uint8_t sigs[4][WC_SIG],blob2[2]={2,9};
        int t=snprintf(text,sizeof(text),"NGGO|3|%s|2|10|0109|0209|",WCT_TOKEN);
        crypto_blake2b_ctx h;wc_records_begin(&h);
        for(unsigned s=1;s<=3;s++) {
            blob2[0]=(uint8_t)s;
            assert(wc_record_sign(sigs[s],wct_sk[s],wct_sid,s,3,0,9,blob2,s<3?2:0));wc_records_slot(&h,blob2,s<3?2:0,sigs[s]);
        }
        uint8_t records[32];wc_records_end(&h,records);
        memcpy(text+t+1,sigs[1],WC_SIG);memcpy(text+t+1+WC_SIG,sigs[2],WC_SIG);memcpy(text+t+1+2*WC_SIG,sigs[3],WC_SIG);
        assert(wc_permit_sign((uint8_t*)text+t+1+3*WC_SIG,wct_relay_sk,wct_sid,10,3,0,records));
        char copy[GATE_PERMIT_MAX];memcpy(copy,text,(size_t)t+1+4*WC_SIG);
        assert(wcg_accept(text,(size_t)t+1+4*WC_SIG)==t && !wcg.stop && !strcmp(text,"NGGO|3|" WCT_TOKEN "|2|10|0109|0209|"));
        assert(copy[48]=='9');copy[48]='8'; /* slot 1's blob 0109 -> 0108: its record signature fails */
        assert(wcg_accept(copy,(size_t)t+1+4*WC_SIG)==0 && wcg.stop==WCG_STOP_INVALID);
        wire_lookahead=2;
    }
    char status[512];assert(wcg_status(status,sizeof(status)) && strstr(status,"armed=1 established=1 keyed=1 "));
    /* The own key and the roster fingerprint (BLAKE2b-128 of the N keys), for comparison out of band. */
    {
        uint8_t all[96],fp[16];char want[160];memcpy(all,wct_pk[1],32);memcpy(all+32,wct_pk[2],32);memcpy(all+64,wct_pk[3],32);
        crypto_blake2b(fp,16,all,96);int w=snprintf(want,sizeof(want)," key=");
        for(unsigned i=0;i<32;i++) w+=snprintf(want+w,sizeof(want)-(size_t)w,"%02x",wct_pk[2][i]);
        w+=snprintf(want+w,sizeof(want)-(size_t)w," roster=");
        for(unsigned i=0;i<16;i++) w+=snprintf(want+w,sizeof(want)-(size_t)w,"%02x",fp[i]);
        assert(strstr(status,want) && strlen(strstr(status,want))==strlen(want));
    }
    printf("WIRE_CRYPTO_GATE %s\n",status);
    closesocket(connection);closesocket(wct_relay);wct_relay=INVALID_SOCKET;connection=saved_connection;timeout_ms=saved_timeout;
    WSACleanup();*(double*)(image+GAME_RVA)=saved_game;
    memset(&wcg,0,sizeof(wcg));stats=saved;memcpy(secret,saved_secret,sizeof(secret));lookahead_reset();
    assert(x4gate_wire(2,2) && !wire_lookahead);blob_proposal[0]=0;
    for(unsigned i=0;i<GATE_CLIENTS_MAX;i++) blob_inputs[i][0]=0;
    puts("wire crypto gate: configuration, handshake (forged WELCOME ignored, timeout 107), signed reports, sealed protocol 4 "
         "permits with every signature checked, replay/clear/forged dropped, invalid content 106 PASS");
}
