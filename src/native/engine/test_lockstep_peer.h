/* Peer links (lockstep_peer.h): configuration and window rules without sockets,
 * the NAT and crypto interfaces, then hello/address exchange, records, peer runs
 * and the fail notice through lockstep_lookahead.h against loopback UDP sockets
 * standing in for the relay and a peer. No engine hooks or native simulation.
 * Needs test_lockstep_lookahead.h (la_* helpers, LA_TOKEN). */
static struct sockaddr_in pl_address(uint32_t ip,unsigned port) {
    struct sockaddr_in a;memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(ip);a.sin_port=htons((unsigned short)port);
    return a;
}
static unsigned pl_from(const char *text,uint32_t ip,unsigned port) {
    struct sockaddr_in from=pl_address(ip,port);return peer_datagram(text,strlen(text),&from);
}
/* A record from 10.0.0.<sender>:2000<sender> (the routed address of that slot) claiming slot. */
static unsigned pl_record(unsigned sender,unsigned slot,uint64_t tick,const char *blob,const char *signature) {
    char text[PEER_DATAGRAM_MAX];
    int n=snprintf(text,sizeof(text),"NGPREC|1|%s|%u|%llu|%u|%u|%s|%s",LA_TOKEN,slot,(unsigned long long)tick,wire_clients,wire_lookahead,blob,signature);
    assert(n>0 && (size_t)n<sizeof(text));
    return pl_from(text,0x0a000000u+sender,20000+sender);
}
static void pl_bundle(uint64_t T,char out[][GATE_BLOB_HEX_MAX+1]) {
    for(unsigned s=1;s<=wire_clients;s++) {out[s-1][0]=0;if(T>wire_lookahead) la_blob(s,T-1-wire_lookahead,out[s-1]);}
}
static int pl_inputs(uint64_t T) {
    char expected[GATE_CLIENTS_MAX][GATE_BLOB_HEX_MAX+1];pl_bundle(T,expected);
    for(unsigned s=0;s<wire_clients;s++) if(strcmp(blob_inputs[s],expected[s])) return 0;
    return 1;
}
static int pl_route_calls;
static int pl_route(SOCKET sock,unsigned slot,const struct sockaddr_in *advertised,struct sockaddr_in *route) {
    (void)sock;++pl_route_calls;
    if(slot==3) return 0;                        /* "use the relay" */
    if(slot==1 && pl_route_calls<3) return -1;   /* still punching */
    *route=*advertised;return 1;
}
static int pl_punches;
static int pl_datagram(SOCKET sock,const struct sockaddr_in *from,const char *data,size_t length) {
    (void)sock;(void)from;if(length==5 && !memcmp(data,"PUNCH",5)) {++pl_punches;return 1;}return 0;
}
static size_t pl_seal(const char *plain,size_t length,char *out,size_t capacity) {
    if(length+1>=capacity) return 0;out[0]='S';memcpy(out+1,plain,length);return length+1;
}
static size_t pl_open(const char *sealed,size_t length,char *out,size_t capacity) {
    if(!length || sealed[0]!='S' || length>=capacity) return 0;memcpy(out,sealed+1,length-1);out[length-1]=0;return length-1;
}
static unsigned pl_sum(const char *record,size_t length) {unsigned sum=0;for(size_t i=0;i<length;i++) sum+=(unsigned char)record[i];return sum&0xffffu;}
static int pl_sign(const char *record,size_t length,char *signature,size_t capacity) {
    return snprintf(signature,capacity,"%04x",pl_sum(record,length))==4;
}
static int pl_verify(unsigned slot,const char *record,size_t length,const char *signature,size_t signature_length) {
    char expected[8];snprintf(expected,sizeof(expected),"%04x",pl_sum(record,length));
    return slot>=1 && signature_length==4 && !memcmp(signature,expected,4);
}
static void test_peer_rules(void) {
    strcpy(secret,LA_TOKEN);stats.slot=2;stats.completed=0;wire_version=3;wire_clients=3;wire_lookahead=2;peer_window=-1;
    /* Configuration: k >= 1 and k + m <= 8, once started only the identical window. */
    assert(peer_links_configure(2,0) && peer_window==2 && peer_config_ok());
    assert(!peer_links_configure(7,0) && peer_window==2 && peer_links_configure(6,0) && !peer_links_configure(-2,0));
    assert(peer_links_configure(-1,0) && peer_window==-1 && peer_config_ok() && !peer_on());
    assert(peer_links_configure(2,0) && peer_links_configure(2,1) && !peer_links_configure(1,1) && !peer_links_configure(-1,1) && peer_window==2);
    wire_lookahead=0;assert(!peer_config_ok() && !peer_links_configure(1,0));
    wire_lookahead=7;assert(!peer_config_ok());wire_lookahead=2;wire_version=2;assert(!peer_config_ok());wire_version=3;
    struct sockaddr_in a;
    assert(peer_parse_address("10.0.0.1:20001",14,&a) && a.sin_addr.s_addr==htonl(0x0a000001u) && a.sin_port==htons(20001));
    assert(peer_parse_address("255.255.255.255:65535",21,&a) && a.sin_port==htons(65535));
    const char *bad[]={"10.0.0.1:0","10.0.0.256:1","10.0.0.01:1","10.0.0:1","10.0.0.1","10.0.0.1:65536","10.0.0.1:1x","a.0.0.1:1",
                       "10.0.0.1:","10.0.0.1:+1","",":1","10.0.0.1.1:1"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++) assert(!peer_parse_address(bad[i],strlen(bad[i]),&a));
    /* Hello until the relay answers; only its own, well-formed answer counts. */
    peer_reset();assert(peer_state==PEER_HELLO && peer_socket==INVALID_SOCKET);
    peer_relay=pl_address(0x0a000064u,9000);
    const char *ignored[]={"NGPADDR|1|" LA_TOKEN "|1|2|120|10.0.0.1:20001|10.0.0.2:20002|10.0.0.3:20003",
        "NGPADDR|1|" LA_TOKEN "|2|3|120|10.0.0.1:20001|10.0.0.2:20002|10.0.0.3:20003",
        "NGPADDR|1|" LA_TOKEN "|2|2|120|10.0.0.1:20001|10.0.0.2:20002",
        "NGPADDR|1|" LA_TOKEN "|2|2|120|10.0.0.1:20001|10.0.0.2:20002|10.0.0.3:20003|",
        "NGPADDR|1|" LA_TOKEN "|2|2|120|10.0.0.1:20001|10.0.0.2:0|10.0.0.3:20003",
        "NGPADDR|2|" LA_TOKEN "|2|2|120|10.0.0.1:20001|10.0.0.2:20002|10.0.0.3:20003",
        "NGPADDR|1|bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb|2|2|120|10.0.0.1:20001|10.0.0.2:20002|10.0.0.3:20003",
        "NGPADDR|1|" LA_TOKEN "|2|2|0120|10.0.0.1:20001|10.0.0.2:20002|10.0.0.3:20003",
        "NGPADDR|1|" LA_TOKEN "|2|2|10.0.0.1:20001|10.0.0.2:20002|10.0.0.3:20003",
        "NGPREFUSE|1|" LA_TOKEN "|2|maybe","NGPREFUSE|1|" LA_TOKEN "|1|off"};
    for(unsigned i=0;i<sizeof(ignored)/sizeof(*ignored);i++) {assert(!pl_from(ignored[i],0x0a000064u,9000) && peer_state==PEER_HELLO);}
    assert(peer_dropped==sizeof(ignored)/sizeof(*ignored));
    const char *book="NGPADDR|1|" LA_TOKEN "|2|2|120|10.0.0.1:20001|10.0.0.2:20002|10.0.0.3:20003";
    assert(!pl_from(book,0x0a000064u,9001) && peer_state==PEER_HELLO);              /* not the relay: not a routed peer either */
    assert(!pl_from(book,0x0a000064u,9000) && peer_state==PEER_ACTIVE && peer_routed==5u && !peer_pending && peer_last==120);
    peer_reset();peer_relay=pl_address(0x0a000064u,9000);
    assert(!pl_from("NGPREFUSE|1|" LA_TOKEN "|2|mismatch",0x0a000064u,9000) && peer_state==PEER_REFUSED);
    assert(!peer_sealed(0,"0200") && !peer_take(3) && peer_sent==0);                 /* relay only */
    /* The rules (k=2, m=2, three clients, this is slot 2). */
    peer_reset();peer_relay=pl_address(0x0a000064u,9000);pl_from(book,0x0a000064u,9000);
    char bundle[GATE_CLIENTS_MAX][GATE_BLOB_HEX_MAX+1],own[GATE_BLOB_HEX_MAX+1];
    for(uint64_t t=0;t<=2;t++) {la_blob(2,t,own);assert(!peer_sealed(t,own));}
    assert(peer_sent==6);                                                           /* two routed peers, three ticks */
    la_blob(1,0,own);assert(!pl_record(1,1,0,own,""));la_blob(3,0,own);assert(!pl_record(3,3,0,own,""));
    assert(!peer_take(3));                                                          /* F=0: tick 0 not admitted yet */
    char empty[GATE_CLIENTS_MAX][GATE_BLOB_HEX_MAX+1];memset(empty,0,sizeof(empty));
    assert(peer_permit(1,empty)==1 && peer_permit(2,empty)==1 && peer_final==2);
    assert(peer_take(3) && pl_inputs(3) && peer_runs==1);
    for(uint64_t t=1;t<=2;t++) {la_blob(1,t,own);assert(!pl_record(1,1,t,own,""));}
    assert(!peer_take(4));                                                          /* slot 3's record of tick 1 missing */
    la_blob(3,1,own);pl_record(3,3,1,own,"");assert(peer_take(4) && pl_inputs(4));
    la_blob(3,2,own);pl_record(3,3,2,own,"");assert(peer_holds(5) && !peer_take(5)); /* 5 > F+m */
    pl_bundle(3,bundle);assert(peer_permit(3,bundle)==1 && peer_final==3 && !peer_holds(5));
    assert(peer_take(5) && pl_inputs(5) && peer_behind_max==2 && peer_first==1 && peer_relay_first==2);
    assert(peer_resend_from(5-2)==1 && peer_resend_from(0)==0);
    peer_last=5;assert(!peer_take(6) && peer_runs==3);peer_last=120;                 /* never past the relay's last tick */
    assert(peer_status(empty[0],sizeof(empty[0])) && strstr(empty[0],"peer=active window=2 ") && strstr(empty[0],"peer_runs=3 ") &&
           strstr(empty[0],"crypto=stub nat=stub"));
    memset(empty,0,sizeof(empty));
    /* Duplicates are fine; the permit for 4 (run on peers) with another blob stops (104). */
    la_blob(1,1,own);assert(!pl_record(1,1,1,own,"") && peer_duplicates==1);
    pl_bundle(4,bundle);strcpy(bundle[0],"ffff");assert(peer_permit(4,bundle)==-4 && peer_final==3);
    /* Two different peer copies of one record stop (108); another copy of a final permit 103;
     * a later copy that differs from a final permit 104. */
    assert(!pl_record(1,1,5,"aa","") && pl_record(1,1,5,"bb","")==PEER_STOP_EQUIVOCATION);
    pl_bundle(3,bundle);strcpy(bundle[2],"00");assert(peer_permit(3,bundle)==-3);
    assert(pl_record(1,1,0,"ffff","")==PEER_STOP_MISMATCH);
    /* Dropped, never fatal: beyond the ring, not routed, claiming another slot, malformed, signed by the stub. */
    uint64_t dropped=peer_dropped;
    assert(!pl_record(1,1,3+PEER_RING-3,"01",""));                                  /* T = F+32 */
    assert(!pl_from("NGPREC|1|" LA_TOKEN "|1|9|3|2|01|",0x0a000009u,20009));        /* unknown sender */
    assert(!pl_record(1,3,9,"01","") && !pl_record(1,1,9,"0A","") && !pl_record(1,1,9,"012","") && !pl_record(1,1,9,"01","00") &&
           !pl_record(1,1,9,"01","0") && !pl_from("NGPREC|1|" LA_TOKEN "|1|9|3|2|01",0x0a000001u,20001) &&
           !pl_from("NGPREC|1|" LA_TOKEN "|1|09|3|2|01|",0x0a000001u,20001) && !pl_from("NGPREC|1|" LA_TOKEN "|1|9|2|2|01|",0x0a000001u,20001) &&
           !pl_from("NGPREC|1|" LA_TOKEN "|1|9|3|1|01|",0x0a000001u,20001) && !pl_from("NGPREC|2|" LA_TOKEN "|1|9|3|2|01|",0x0a000001u,20001) &&
           !pl_from("NGPREC|1|bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb|1|9|3|2|01|",0x0a000001u,20001));
    assert(peer_dropped==dropped+13 && peer_entry_at(12,0)==NULL);
    for(uint64_t T=4;T<=40;T++) {pl_bundle(T,bundle);if(T==8) strcpy(bundle[0],"aa");assert(peer_permit(T,bundle)==1);}
    assert(peer_final==40);
    uint64_t stale=peer_stale;assert(!pl_record(1,1,0,"01","") && peer_stale==stale+1);  /* T=3, overwritten long ago */
    /* Interfaces: a NAT module that punches and refuses a pair, a crypto module that seals and signs. */
    static const peer_nat_api nat={"test",peer_stub_port,pl_route,pl_datagram};
    static const peer_crypto_api crypto={"test",pl_seal,pl_open,pl_sign,pl_verify};
    peer_nat=&nat;peer_crypto=&crypto;pl_route_calls=0;pl_punches=0;
    peer_reset();peer_relay=pl_address(0x0a000064u,9000);pl_from(book,0x0a000064u,9000);
    assert(peer_state==PEER_ACTIVE && !peer_routed && peer_pending==1u && peer_unreachable==1);
    assert(!peer_poll(0) && peer_routed==1u && !peer_pending);
    assert(!pl_from("PUNCH",0x0a000005u,1) && pl_punches==1);
    char text[PEER_DATAGRAM_MAX],sealed[PEER_DATAGRAM_MAX];
    int n=snprintf(text,sizeof(text),"NGPREC|1|%s|1|0|3|2|0100",LA_TOKEN);char signature[8];pl_sign(text,(size_t)n,signature,sizeof(signature));
    snprintf(text+n,sizeof(text)-(size_t)n,"|%s",signature);
    struct sockaddr_in from=pl_address(0x0a000001u,20001);
    assert(!peer_datagram(text,strlen(text),&from) && peer_received==0);              /* not sealed */
    size_t length=pl_seal(text,strlen(text),sealed,sizeof(sealed));
    sealed[length-1]=sealed[length-1]=='0'?'1':'0';assert(!peer_datagram(sealed,length,&from) && peer_received==0); /* forged */
    length=pl_seal(text,strlen(text),sealed,sizeof(sealed));
    assert(!peer_datagram(sealed,length,&from) && peer_received==1 && peer_entry_at(3,0) && !strcmp(peer_entry_at(3,0)->blobs[0],"0100"));
    from=pl_address(0x0a000003u,20003);assert(!peer_datagram(sealed,length,&from) && peer_received==1);   /* pair 3: relay only */
    assert(peer_status(text,sizeof(text)) && strstr(text,"unreachable=1 crypto=test nat=test"));
    peer_nat=&peer_nat_stub;peer_crypto=&peer_crypto_stub;
    peer_window=-1;peer_reset();assert(peer_state==PEER_OFF);
    wire_version=2;wire_clients=2;wire_lookahead=0;
}
/* Count NGSTATE reports and pick the peer socket's hello and fail notice off the relay socket. */
static unsigned pl_relay_read(struct sockaddr_in *hello_from,char *hello,char *fail) {
    char data[GATE_STATE_MAX+16];unsigned reports=0;
    for(;;) {
        struct sockaddr_in from;int size=sizeof(from);
        int n=recvfrom(la_relay,data,sizeof(data)-1,0,(struct sockaddr*)&from,&size);
        if(n<0) break;
        data[n]=0;
        if(!strncmp(data,"NGSTATE|4|",10)) ++reports;
        else if(!strncmp(data,"NGPHELLO|",9)) {if(hello) strcpy(hello,data);if(hello_from) *hello_from=from;}
        else if(!strncmp(data,"NGPFAIL|",8)) {if(fail) strcpy(fail,data);}
        else assert(0);
    }
    return reports;
}
static void test_peer_wait(void) {
    WSADATA w;assert(!WSAStartup(MAKEWORD(2,2),&w));
    SOCKET saved_connection=connection;unsigned saved_timeout=timeout_ms;double saved_game=*(double*)(image+GAME_RVA);
    la_relay=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);assert(la_relay!=INVALID_SOCKET);
    SOCKET other=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);assert(other!=INVALID_SOCKET);   /* the peer in slot 1 */
    struct sockaddr_in relay=pl_address(INADDR_LOOPBACK,0),peer1=pl_address(INADDR_LOOPBACK,0);int size=sizeof(relay);
    assert(!bind(la_relay,(struct sockaddr*)&relay,sizeof(relay)) && !getsockname(la_relay,(struct sockaddr*)&relay,&size));
    size=sizeof(peer1);assert(!bind(other,(struct sockaddr*)&peer1,sizeof(peer1)) && !getsockname(other,(struct sockaddr*)&peer1,&size));
    DWORD drain_ms=60,client_ms=25;
    assert(!setsockopt(la_relay,SOL_SOCKET,SO_RCVTIMEO,(const char*)&drain_ms,sizeof(drain_ms)));
    assert(!setsockopt(other,SOL_SOCKET,SO_RCVTIMEO,(const char*)&drain_ms,sizeof(drain_ms)));
    connection=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);assert(connection!=INVALID_SOCKET);
    assert(!setsockopt(connection,SOL_SOCKET,SO_RCVTIMEO,(const char*)&client_ms,sizeof(client_ms)));
    assert(!connect(connection,(struct sockaddr*)&relay,sizeof(relay)));
    size=sizeof(la_client);assert(!getsockname(connection,(struct sockaddr*)&la_client,&size));
    strcpy(secret,LA_TOKEN);stats.slot=2;wire_version=3;wire_clients=2;wire_lookahead=1;
    stats.game=20.0;*(double*)(image+GAME_RVA)=20.0;stats.issued=stats.completed=0;timeout_ms=2000;
    char packet[GATE_STATE_MAX],hello[256],fail[256],text[512];struct sockaddr_in hello_from;
    /* Off: one report, no peer socket, nothing else on the wire. */
    peer_window=-1;lookahead_reset();la_send_permit(1);la_send_permit(2);
    la_blob(2,0,blob_proposal);la_state(packet,sizeof(packet),0,blob_proposal);
    hello[0]=0;assert(!lookahead_frame(packet,strlen(packet),la_reply,sizeof(la_reply)) && peer_socket==INVALID_SOCKET);
    assert(pl_relay_read(NULL,hello,NULL)==1 && !hello[0]);
    /* On (m=2): tick 0 opens the peer socket, which says hello from its own port. */
    assert(peer_links_configure(2,0));lookahead_reset();la_send_permit(1);la_send_permit(2);
    assert(!lookahead_frame(packet,strlen(packet),la_reply,sizeof(la_reply)) && peer_socket!=INVALID_SOCKET && peer_state==PEER_HELLO);
    assert(pl_relay_read(&hello_from,hello,NULL)==1 && !strcmp(hello,"NGPHELLO|1|" LA_TOKEN "|2|2|1|2"));
    assert(hello_from.sin_addr.s_addr==htonl(INADDR_LOOPBACK) && hello_from.sin_port!=la_client.sin_port);
    snprintf(text,sizeof(text),"NGPADDR|1|%s|2|2|100|127.0.0.1:%u|127.0.0.1:%u",LA_TOKEN,ntohs(peer1.sin_port),ntohs(hello_from.sin_port));
    assert(sendto(la_relay,text,(int)strlen(text),0,(struct sockaddr*)&hello_from,sizeof(hello_from))==(int)strlen(text));
    /* Tick 1: the answer is read after this tick's record would have gone out; permit 2 is held. */
    stats.completed=++stats.issued;la_blob(2,1,blob_proposal);la_state(packet,sizeof(packet),1,blob_proposal);
    assert(!lookahead_frame(packet,strlen(packet),la_reply,sizeof(la_reply)) && peer_state==PEER_ACTIVE && peer_routed==1u && peer_final==2);
    /* Tick 2: slot 1's record of tick 1 is there before the permit for 3: tick 3 runs on the peer records. */
    snprintf(text,sizeof(text),"NGPREC|1|%s|1|1|2|1|",LA_TOKEN);la_blob(1,1,text+strlen(text));strcat(text,"|");
    assert(sendto(other,text,(int)strlen(text),0,(struct sockaddr*)&hello_from,sizeof(hello_from))==(int)strlen(text));
    Sleep(20);
    stats.completed=++stats.issued;la_blob(2,2,blob_proposal);la_state(packet,sizeof(packet),2,blob_proposal);
    assert(!lookahead_frame(packet,strlen(packet),la_reply,sizeof(la_reply)) && peer_runs==1 && pl_inputs(3) && peer_behind_max==1);
    int n=recv(other,text,sizeof(text)-1,0);assert(n>0);text[n]=0;
    char expected[512];snprintf(expected,sizeof(expected),"NGPREC|1|%s|2|2|2|1|",LA_TOKEN);la_blob(2,2,expected+strlen(expected));strcat(expected,"|");
    assert(!strcmp(text,expected) && recv(other,text,sizeof(text)-1,0)<0);          /* sent once, tick 1's never */
    /* Tick 3: a peer record arriving while the client waits wakes it (both sockets are watched). */
    stats.completed=++stats.issued;la_blob(2,3,blob_proposal);la_state(packet,sizeof(packet),3,blob_proposal);
    snprintf(text,sizeof(text),"NGPREC|1|%s|1|2|2|1|",LA_TOKEN);la_blob(1,2,text+strlen(text));strcat(text,"|");
    assert(sendto(other,text,(int)strlen(text),0,(struct sockaddr*)&hello_from,sizeof(hello_from))==(int)strlen(text));
    assert(!lookahead_frame(packet,strlen(packet),la_reply,sizeof(la_reply)) && peer_runs==2 && pl_inputs(4) && peer_behind_max==2);
    /* Tick 4 would be 5 > F+m=4: the client waits; the relay's permit for 3 differs from the
     * peer copy it ran on: 104, and the relay hears of it (NGPFAIL). */
    stats.completed=++stats.issued;la_blob(2,4,blob_proposal);la_state(packet,sizeof(packet),4,blob_proposal);
    snprintf(text,sizeof(text),"NGPREC|1|%s|1|3|2|1|",LA_TOKEN);la_blob(1,3,text+strlen(text));strcat(text,"|");
    assert(sendto(other,text,(int)strlen(text),0,(struct sockaddr*)&hello_from,sizeof(hello_from))==(int)strlen(text));
    la_permit_text(text,sizeof(text),3);assert(!strcmp(text+strlen(text)-6,"|0101|"));text[strlen(text)-2]='f';
    la_send(text,strlen(text));Sleep(20);
    fail[0]=0;pl_relay_read(NULL,NULL,NULL);
    assert(lookahead_frame(packet,strlen(packet),la_reply,sizeof(la_reply))==PEER_STOP_MISMATCH && peer_runs==2);
    pl_relay_read(NULL,NULL,fail);assert(!strcmp(fail,"NGPFAIL|1|" LA_TOKEN "|2|4|104"));
    assert(peer_status(text,sizeof(text)) && strstr(text,"peer=active window=2 sent=3 received=3 peer_runs=2 "));
    peer_close();assert(peer_socket==INVALID_SOCKET);peer_close();
    closesocket(connection);closesocket(la_relay);closesocket(other);la_relay=INVALID_SOCKET;connection=saved_connection;timeout_ms=saved_timeout;
    WSACleanup();*(double*)(image+GAME_RVA)=saved_game;
}
/* The real modules (integration, 1 November 2026): with gate.wire_crypto the records are wc_peer_seal / wc_peer_open (the
 * group key, the senders' session Ed25519 keys), nothing in the clear; with gate.peer_links(m, P) the NAT traversal core
 * decides the routes on the peer socket. Keys and the core's decisions are set here as NGKEYS and a rendezvous would. */
static void test_peer_real(void) {
    strcpy(secret,LA_TOKEN);stats.slot=2;stats.completed=0;wire_version=3;wire_clients=3;wire_lookahead=2;
    static unsigned char saved_wcg[sizeof(wcg)];memcpy(saved_wcg,&wcg,sizeof(wcg));
    uint8_t sk[3][64],pk[3][WC_KEY],seed[32];
    memset(&wcg,0,sizeof(wcg));wcg.armed=1;
    for(unsigned s=0;s<3;s++) {memset(seed,0x40+(int)s,sizeof(seed));crypto_ed25519_key_pair(sk[s],pk[s],seed);memcpy(wcg.roster[s],pk[s],WC_KEY);}
    memset(wcg.group,0x5a,WC_KEY);assert(wc_unhex(wcg.sid,LA_TOKEN,WC_SID));memcpy(wcg.hs.sign_sk,sk[1],64);
    peer_window=2;peer_reset();peer_relay=pl_address(0x0a000064u,9000);
    const char *book="NGPADDR|1|" LA_TOKEN "|2|2|120|10.0.0.1:20001|10.0.0.2:20002|10.0.0.3:20003";
    pl_from(book,0x0a000064u,9000);assert(peer_state==PEER_ACTIVE && peer_routed==5u);
    char own[GATE_BLOB_HEX_MAX+1],text[PEER_DATAGRAM_MAX];
    /* Before the keys (NGKEYS) nothing goes out: never a record in the clear. */
    la_blob(2,0,own);assert(!peer_sealed(0,own) && peer_sent==0);
    wcg.keyed=1;
    la_blob(2,1,own);assert(!peer_sealed(1,own) && peer_sent==2);
    {   /* What went out: slot 2's sealed, signed record of tick 1, which every player opens with the group key. */
        unsigned sender=0;uint64_t tick=0;uint8_t blob[WC_BLOB_MAX],sig[WC_SIG],expected[WC_BLOB_MAX];size_t length=0;
        size_t sealed=WC_OVERHEAD+WC_PEER_HEAD+strlen(own)/2+WC_SIG;
        assert(!memcmp(peer_scratch,"X4C1",4) && !strstr(peer_scratch,"NGPREC"));
        assert(wc_peer_open(peer_wc_keys,(const uint8_t (*)[WC_KEY])wcg.roster,wcg.sid,3,2,(const uint8_t*)peer_scratch,sealed,&sender,&tick,blob,&length,sig)==1);
        assert(sender==2 && tick==1 && length==strlen(own)/2 && wc_unhex(expected,own,length) && !memcmp(blob,expected,length));
        assert(wc_record_verify(sig,pk[1],wcg.sid,2,3,2,1,blob,length));   /* the relay path's record signature */
    }
    /* Slot 1's record from slot 1's route: taken; from slot 3's route: dropped; a group key holder's forgery: dropped and
     * counted; a text record: dropped. */
    wc_tx tx1;memset(&tx1,0,sizeof(tx1));wc_peer_key(&tx1.k,wcg.group,wcg.sid,0,1);
    uint8_t blob[WC_BLOB_MAX],out[WC_PEER_MAX];la_blob(1,1,own);size_t bytes=strlen(own)/2;assert(wc_unhex(blob,own,bytes));
    size_t n=wc_peer_seal(&tx1,wcg.sid,sk[0],3,2,1,blob,bytes,out,sizeof(out));assert(n);
    struct sockaddr_in from1=pl_address(0x0a000001u,20001),from3=pl_address(0x0a000003u,20003);
    uint64_t received=peer_received,dropped=peer_dropped;
    assert(!peer_datagram((const char*)out,n,&from1) && peer_received==received+1 && peer_entry_at(4,0) && !strcmp(peer_entry_at(4,0)->blobs[0],own));
    assert(!peer_datagram((const char*)out,n,&from1) && peer_duplicates==1);
    assert(!peer_datagram((const char*)out,n,&from3) && peer_dropped==dropped+1);
    n=wc_peer_seal(&tx1,wcg.sid,sk[2],3,2,2,blob,bytes,out,sizeof(out));
    assert(!peer_datagram((const char*)out,n,&from1) && peer_forged==1 && peer_dropped==dropped+2);
    assert(!pl_record(1,1,2,own,"") && peer_received==received+1 && peer_dropped==dropped+3);
    {uint8_t other[2]={0xff,0xff};n=wc_peer_seal(&tx1,wcg.sid,sk[0],3,2,1,other,2,out,sizeof(out));}   /* signed, but another record */
    assert(peer_datagram((const char*)out,n,&from1)==PEER_STOP_EQUIVOCATION);
    assert(peer_status(text,sizeof(text)) && strstr(text,"crypto=wire nat=stub forged=1"));
    memcpy(&wcg,saved_wcg,sizeof(wcg));peer_reset();
    /* NAT traversal: P selects the core; it answers pending, direct (its authenticated path) or relay, and gets its own
     * datagrams from anyone, a routed peer too. */
    assert(!peer_nat_configure(80,0) && !peer_nat_configure(65535,0) && peer_nat_configure(40000,0) && peer_nat==&peer_nat_traversal);
    assert(peer_nat_configure(40000,1) && !peer_nat_configure(40002,1) && !peer_nat_configure(0,1));
    peer_reset();peer_relay=pl_address(0x0a000064u,9000);
    {nat_config c;nat_config_defaults(&c);c.slot=2;c.clients=3;memcpy(c.token,LA_TOKEN,33);memset(c.instance,7,8);
     c.server[0]=(nat_addr){0x0a000064u,40000};c.server[1]=(nat_addr){0x0a000064u,40001};
     assert(!nat_core_init(&peer_traversal,&c));peer_traversal_started=1;}   /* as peer_traversal_start does with a socket */
    pl_from(book,0x0a000064u,9000);
    assert(peer_state==PEER_ACTIVE && !peer_routed && peer_pending==5u);         /* both pairs still punching */
    peer_traversal.peer[1].state=NAT_PEER_DIRECT;peer_traversal.peer[1].path=(nat_addr){0xc0a80005u,31000};
    peer_traversal.peer[3].decided=1;peer_traversal.peer[3].state=NAT_PEER_RELAY;
    assert(!peer_poll(0) && peer_routed==1u && !peer_pending && peer_unreachable==1);
    assert(peer_routes[0].sin_addr.s_addr==htonl(0xc0a80005u) && peer_routes[0].sin_port==htons(31000));
    struct sockaddr_in direct=pl_address(0xc0a80005u,31000),stranger=pl_address(0x0a000009u,1);dropped=peer_dropped;
    assert(!peer_datagram("NGNAT|1|PROBE|x",15,&direct) && !peer_datagram("NGNAT|1|SEEN|x",14,&stranger) && peer_dropped==dropped);
    assert(!peer_datagram("hello",5,&stranger) && peer_dropped==dropped+1);
    snprintf(text,sizeof(text),"NGPREC|1|%s|1|5|3|2|0100|",LA_TOKEN);assert(!peer_datagram(text,strlen(text),&direct) && peer_received==1);
    assert(peer_status(text,sizeof(text)) && strstr(text,"crypto=stub nat=traversal"));
    assert(peer_nat_configure(0,0) && peer_nat==&peer_nat_stub && !peer_rendezvous);
    peer_window=-1;peer_reset();wire_version=2;wire_clients=2;wire_lookahead=0;
}
static void test_peer(void) {
    gate_stats saved=stats;char saved_secret[sizeof(secret)];memcpy(saved_secret,secret,sizeof(secret));
    test_peer_rules();test_peer_real();test_peer_wait();
    stats=saved;memcpy(secret,saved_secret,sizeof(secret));peer_window=-1;lookahead_reset();
    wire_version=2;wire_clients=2;wire_lookahead=0;blob_proposal[0]=0;
    for(unsigned i=0;i<GATE_CLIENTS_MAX;i++) blob_inputs[i][0]=0;
    puts("peer links: configuration, address exchange, peer runs inside F+m, permit/peer/equivocation checks, NAT and crypto "
         "interfaces, the NAT traversal core's routes and wire crypto records, wake on either socket, fail notice PASS");
}
