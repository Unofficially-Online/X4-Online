/* Unit tests of the NAT traversal module (roadmap B7): primitives, message codec,
 * PCP/NAT-PMP/UPnP encoders and parsers, and core decisions that need no relay.
 * The full behaviour against simulated NATs: tests/test_nat_traversal.py. */
#ifndef _WIN32
#define _GNU_SOURCE
#endif
#include "lockstep_nat_net.h"
#include <stdlib.h>

static int failures;
#define CHECK(c) do {if(!(c)) {printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c);++failures;}} while(0)

static void test_hmac(void) {
    unsigned char out[32];char hex[65];
    /* RFC 4231 test cases 1 and 2. */
    unsigned char key1[20];memset(key1,0x0b,20);
    nat_hmac(key1,20,"Hi There",8,out);nat_hex(out,32,hex);
    CHECK(!strcmp(hex,"b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"));
    nat_hmac((const unsigned char*)"Jefe",4,"what do ya want for nothing?",28,out);nat_hex(out,32,hex);
    CHECK(!strcmp(hex,"5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"));
    /* Test case 6: a key longer than the block is hashed first. */
    unsigned char key6[131];memset(key6,0xaa,131);
    nat_hmac(key6,131,"Test Using Larger Than Block-Size Key - Hash Key First",54,out);nat_hex(out,32,hex);
    CHECK(!strcmp(hex,"60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"));
}
static void test_numbers_and_addresses(void) {
    uint64_t v;nat_addr a;char text[24];
    CHECK(nat_uint("0",10,&v)&&v==0);CHECK(nat_uint("65535",65535,&v)&&v==65535);
    CHECK(!nat_uint("",9,&v));CHECK(!nat_uint("01",9,&v));CHECK(!nat_uint("+1",9,&v));CHECK(!nat_uint("70000",65535,&v));
    CHECK(nat_addr_parse("203.0.113.7:5000",&a)&&a.ip==0xcb007107u&&a.port==5000);
    nat_addr_format(a,text,sizeof(text));CHECK(!strcmp(text,"203.0.113.7:5000"));
    CHECK(nat_addr_parse("-",&a)&&!a.ip&&!a.port);
    nat_addr_format(a,text,sizeof(text));CHECK(!strcmp(text,"-"));
    const char *bad[]={"203.0.113.7","203.0.113.7:0","203.0.113.07:1","256.0.0.1:1","1.2.3:4","1.2.3.4:65536","0.0.0.0:1","1.2.3.4:5x"," 1.2.3.4:5",""};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++) CHECK(!nat_addr_parse(bad[i],&a));
    CHECK(nat_ip_private(0x0a000001u)&&nat_ip_private(0xac100001u)&&nat_ip_private(0xc0a80101u)&&nat_ip_private(0x64400001u));
    CHECK(!nat_ip_private(0xcb007101u)&&!nat_ip_private(0xac200001u));
    CHECK(nat_ip_unusable(0)&&nat_ip_unusable(0xe0000001u)&&nat_ip_unusable(0xffffffffu)&&!nat_ip_unusable(0xcb007101u));
}
static void test_messages(void) {
    unsigned char key[32];memset(key,7,32);nat_msg m;nat_fields f;
    nat_msg_start(&m,"HELLO");nat_msg_addf(&m,"%u",2);nat_msg_addf(&m,"%s","0123456789abcdef");
    CHECK(nat_msg_seal(&m,key,32,NAT_REQUEST_MIN));
    CHECK(m.length==NAT_REQUEST_MIN);
    CHECK(nat_verify(m.data,m.length,key,32));
    CHECK(nat_split(m.data,m.length,&f)&&f.n==7&&!strcmp(f.f[2],"HELLO")&&!strcmp(f.f[3],"2"));
    for(unsigned i=0;i<m.length;i+=17) {char copy[NAT_DATAGRAM_MAX+1];memcpy(copy,m.data,m.length);copy[i]^=1;CHECK(!nat_verify(copy,m.length,key,32));}
    key[0]^=1;CHECK(!nat_verify(m.data,m.length,key,32));
    CHECK(!nat_split("NGNAT|2|X|1",11,&f));CHECK(!nat_split("NGNAX|1|X|1",11,&f));
    char nul[]="NGNAT|1|X|\0|1";CHECK(!nat_split(nul,sizeof(nul)-1,&f));
    char many[200]="NGNAT|1|X";for(unsigned i=0;i<30;i++) strcat(many,"|1");CHECK(!nat_split(many,strlen(many),&f));
}
static void test_pcp_natpmp(void) {
    unsigned char nonce[12],request[NAT_PCP_MAP_SIZE];for(unsigned i=0;i<12;i++) nonce[i]=(unsigned char)(i+1);
    nat_pcp_map_request(request,0x7f000001u,nonce,5000,5001,3600);
    CHECK(request[0]==2&&request[1]==1&&nat_get32(request+4)==3600);
    CHECK(request[18]==0xff&&request[19]==0xff&&nat_get32(request+20)==0x7f000001u);
    CHECK(!memcmp(request+24,nonce,12)&&request[36]==17&&nat_get16(request+40)==5000&&nat_get16(request+42)==5001);
    unsigned char reply[NAT_PCP_MAP_SIZE];memcpy(reply,request,sizeof(reply));
    reply[1]=0x81;reply[2]=0;reply[3]=0;nat_put32(reply+4,1800);nat_put32(reply+8,99);memset(reply+12,0,12);
    nat_put16(reply+42,40000);nat_put_mapped(reply+44,0xcb007107u);
    nat_pcp_reply r;
    CHECK(nat_pcp_map_parse(reply,sizeof(reply),nonce,5000,&r)&&!r.result&&r.lifetime==1800&&r.external_port==40000&&r.external_ip==0xcb007107u);
    reply[24]^=1;CHECK(!nat_pcp_map_parse(reply,sizeof(reply),nonce,5000,&r));reply[24]^=1;
    CHECK(!nat_pcp_map_parse(reply,sizeof(reply),nonce,5001,&r));
    reply[3]=2;CHECK(nat_pcp_map_parse(reply,sizeof(reply),nonce,5000,&r)&&r.result==2);
    CHECK(!strcmp(nat_pcp_result_name(2),"not_authorized")&&!strcmp(nat_pcp_result_name(200),"unknown_result"));
    unsigned char v0[8]={0,129,0,1,0,0,0,5};
    CHECK(nat_pcp_map_parse(v0,8,nonce,5000,&r)&&r.version0&&r.result==1);
    CHECK(!nat_pcp_map_parse(reply,10,nonce,5000,&r));
    unsigned char pmp[NAT_PMP_MAP_SIZE];nat_pmp_map_request(pmp,5000,5000,7200);
    CHECK(pmp[0]==0&&pmp[1]==1&&nat_get16(pmp+4)==5000&&nat_get16(pmp+6)==5000&&nat_get32(pmp+8)==7200);
    unsigned char pr[16]={0,129,0,0,0,0,0,9,0x13,0x88,0x9c,0x40,0,0,0x1c,0x20};nat_pmp_reply q;
    CHECK(nat_pmp_map_parse(pr,16,5000,&q)&&!q.result&&q.external_port==40000&&q.lifetime==7200);
    CHECK(!nat_pmp_map_parse(pr,16,5001,&q));CHECK(!nat_pmp_map_parse(pr,12,5000,&q));
    unsigned char pa[12]={0,128,0,0,0,0,0,9,203,0,113,7};
    CHECK(nat_pmp_address_parse(pa,12,&q)&&q.external_ip==0xcb007107u);
    pa[3]=2;CHECK(nat_pmp_address_parse(pa,12,&q)&&q.result==2);
}
static const char description[]=
    "<?xml version=\"1.0\"?><root><device><deviceList><device><serviceList>"
    "<service><serviceType>urn:schemas-upnp-org:service:WANCommonInterfaceConfig:1</serviceType><controlURL>/ctl/CmnIfCfg</controlURL></service>"
    "</serviceList><deviceList><device><serviceList>"
    "<service>\n <serviceType>urn:schemas-upnp-org:service:WANPPPConnection:1</serviceType>\n <controlURL>/ctl/PPP</controlURL></service>"
    "<service><serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType><controlURL>ctl/IPConn</controlURL></service>"
    "</serviceList></device></deviceList></device></deviceList></device></root>";
static void test_upnp(void) {
    char out[256];nat_url u,control;char service[96];
    const char ssdp[]="HTTP/1.1 200 OK\r\nCACHE-CONTROL: max-age=120\r\nlocation:  http://192.168.1.1:5431/rootDesc.xml \r\nST: x\r\n\r\n";
    CHECK(nat_http_header(ssdp,sizeof(ssdp)-1,"LOCATION",out,sizeof(out))&&!strcmp(out,"http://192.168.1.1:5431/rootDesc.xml"));
    CHECK(!nat_http_header(ssdp,sizeof(ssdp)-1,"SERVER",out,sizeof(out)));
    CHECK(nat_url_parse(out,&u)&&u.ip==0xc0a80101u&&u.port==5431&&!strcmp(u.path,"/rootDesc.xml"));
    CHECK(nat_url_parse("http://10.0.0.1/",&u)&&u.port==80);
    CHECK(!nat_url_parse("https://10.0.0.1/",&u)&&!nat_url_parse("http://router.local/",&u)&&!nat_url_parse("http://10.0.0.1/a b",&u));
    nat_url_parse("http://192.168.1.1:5431/rootDesc.xml",&u);
    CHECK(nat_igd_parse(description,sizeof(description)-1,&u,service,sizeof(service),&control));
    CHECK(!strcmp(service,"urn:schemas-upnp-org:service:WANIPConnection:1")&&!strcmp(control.path,"/ctl/IPConn")&&control.port==5431);
    char other[sizeof(description)+32];strcpy(other,description);
    char *ip=strstr(other,"<service><serviceType>urn:schemas-upnp-org:service:WANIPConnection:1");ip[30]='X';   /* only PPP left */
    CHECK(nat_igd_parse(other,strlen(other),&u,service,sizeof(service),&control)&&!strcmp(control.path,"/ctl/PPP"));
    CHECK(!nat_igd_parse("<root></root>",13,&u,service,sizeof(service),&control));
    const char absolute[]="<service><serviceType>urn:schemas-upnp-org:service:WANIPConnection:2</serviceType><controlURL>http://192.168.1.1:5000/c</controlURL></service>";
    CHECK(nat_igd_parse(absolute,sizeof(absolute)-1,&u,service,sizeof(service),&control)&&control.port==5000&&!strcmp(control.path,"/c"));
    const char foreign[]="<service><serviceType>urn:schemas-upnp-org:service:WANIPConnection:2</serviceType><controlURL>http://192.168.1.9:5000/c</controlURL></service>";
    CHECK(!nat_igd_parse(foreign,sizeof(foreign)-1,&u,service,sizeof(service),&control));   /* another host: refused */
    char request[2048];nat_url c;nat_url_parse("http://192.168.1.1:5431/ctl/IPConn",&c);
    int n=nat_upnp_add_request(request,sizeof(request),&c,"urn:schemas-upnp-org:service:WANIPConnection:1",5000,5000,0xc0a8010au,3600);
    CHECK(n>0&&strstr(request,"POST /ctl/IPConn HTTP/1.1\r\n")&&strstr(request,"SOAPAction: \"urn:schemas-upnp-org:service:WANIPConnection:1#AddPortMapping\"")&&
          strstr(request,"<NewInternalClient>192.168.1.10</NewInternalClient>")&&strstr(request,"<NewLeaseDuration>3600</NewLeaseDuration>")&&
          strstr(request,"<NewProtocol>UDP</NewProtocol>"));
    const char *body=strstr(request,"\r\n\r\n")+4;char length[16];
    CHECK(nat_http_header(request,(size_t)(body-request),"Content-Length",length,sizeof(length))&&(size_t)atoi(length)==strlen(body));
    const char error[]="HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/xml\r\n\r\n<s:Envelope><s:Body><s:Fault><detail><UPnPError><errorCode>718</errorCode></UPnPError></detail></s:Fault></s:Body></s:Envelope>";
    const char *b;size_t bl;
    CHECK(nat_http_status(error,sizeof(error)-1,&b,&bl)==500&&nat_upnp_error(b,bl)==718);
    const char ok[]="HTTP/1.0 200 OK\r\n\r\n<u:GetExternalIPAddressResponse><NewExternalIPAddress>203.0.113.9</NewExternalIPAddress></u:GetExternalIPAddressResponse>";
    uint32_t external=0;CHECK(nat_http_status(ok,sizeof(ok)-1,&b,&bl)==200&&nat_upnp_external_ip(b,bl,&external)&&external==0xcb007109u);
    CHECK(!nat_http_status("HTTP/1.1 2x0 OK\r\n\r\n",19,&b,&bl));
    char chunked[]="4\r\nWiki\r\n6;ext=1\r\npedia \r\nE\r\nin \r\n\r\nchunks.\r\n0\r\n\r\n";
    long cl=nat_http_unchunk(chunked,strlen(chunked));
    CHECK(cl==24&&!memcmp(chunked,"Wikipedia in \r\n\r\nchunks.",24));
    char truncated[]="9\r\nshort\r\n";CHECK(nat_http_unchunk(truncated,strlen(truncated))<0);
    char ssearch[256];CHECK(nat_ssdp_search(ssearch,sizeof(ssearch),1)>0&&strstr(ssearch,"ST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n"));
}
static void test_route(void) {
    const char table[]="Iface\tDestination\tGateway \tFlags\tRefCnt\tUse\tMetric\tMask\t\tMTU\tWindow\tIRTT\n"
        "eth0\t0001A8C0\t00000000\t0001\t0\t0\t0\t00FFFFFF\t0\t0\t0\n"
        "eth0\t00000000\t0101A8C0\t0003\t0\t0\t100\t00000000\t0\t0\t0\n";
    uint32_t gateway=0;CHECK(nat_route_parse_linux(table,&gateway)&&gateway==0xc0a80101u);
    CHECK(!nat_route_parse_linux("Iface\tDestination\n",&gateway));
}
static int sent;
static void count_send(void *context,nat_addr to,const char *data,size_t length) {(void)context;(void)to;(void)data;(void)length;++sent;}
static void test_core(void) {
    nat_config c;nat_core n;nat_config_defaults(&c);
    c.slot=1;c.clients=2;strcpy(c.token,"0123456789abcdef0123456789abcdef");
    nat_addr_parse("198.51.100.10:47000",&c.server[0]);nat_addr_parse("198.51.100.10:47001",&c.server[1]);
    CHECK(nat_core_init(&n,&c)&&!strcmp(nat_core_init(&n,&c),"instance"));
    memset(c.instance,9,8);
    CHECK(!nat_core_init(&n,&c));
    nat_config bad=c;bad.slot=3;CHECK(nat_core_init(&n,&bad)!=NULL);
    bad=c;strcpy(bad.token,"0123456789ABCDEF0123456789abcdef");CHECK(nat_core_init(&n,&bad)!=NULL);
    bad=c;bad.server[1]=bad.server[0];CHECK(nat_core_init(&n,&bad)!=NULL);
    CHECK(nat_core_path(&n,2,NULL,NULL)==NAT_PATH_RELAY);   /* a refused core answers RELAY */
    CHECK(!nat_core_init(&n,&c));
    CHECK(nat_core_path(&n,2,NULL,NULL)==NAT_PATH_PENDING);
    CHECK(nat_core_path(&n,1,NULL,NULL)==NAT_PATH_RELAY&&nat_core_path(&n,3,NULL,NULL)==NAT_PATH_RELAY);
    CHECK(!nat_core_input(&n,0,c.server[0],"hello",5,count_send,NULL));   /* not ours: the caller's data */
    CHECK(nat_core_input(&n,0,c.server[0],"NGNAT|1|SEEN|junk",17,count_send,NULL));
    /* No relay answers: RELAY "no_rendezvous" after ask_timeout_ms, with bounded traffic. */
    uint64_t now=0;sent=0;
    while(now<c.ask_timeout_ms+1000) {uint64_t due=nat_core_poll(&n,now,count_send,NULL);CHECK(due>now);now=due<now+1000?due:now+1000;}
    CHECK(nat_core_path(&n,2,NULL,NULL)==NAT_PATH_RELAY&&!strcmp(nat_core_reason(&n,2),"no_rendezvous"));
    CHECK(sent>0&&sent<=4*(int)(c.ask_timeout_ms/c.hello_ms)+4);
    CHECK(nat_core_peer_of(&n,c.server[0])==0);
    sent=0;nat_core_close(&n,count_send,NULL);CHECK(sent==0);   /* never registered: no BYE */
    CHECK(nat_core_path(&n,2,NULL,NULL)==NAT_PATH_RELAY&&nat_core_poll(&n,now,count_send,NULL)==UINT64_MAX);
    /* Replies from anywhere but the relay's ports, or with another key, change nothing. */
    CHECK(!nat_core_init(&n,&c));nat_core_poll(&n,0,count_send,NULL);
    nat_msg m;nat_msg_start(&m,"SEEN");nat_msg_addf(&m,"1");nat_msg_addf(&m,"%s",n.instance_hex);nat_msg_addf(&m,"1");
    nat_msg_addf(&m,"0");nat_msg_addf(&m,"203.0.113.1:5000");nat_msg_addf(&m,"eim");
    nat_msg forged=m;unsigned char wrong[32]={0};nat_msg_seal(&forged,wrong,32,0);
    nat_core_input(&n,10,c.server[0],forged.data,forged.length,count_send,NULL);CHECK(!n.seen_ok[0]);
    nat_msg_seal(&m,n.rkey,32,0);
    nat_addr elsewhere=c.server[0];elsewhere.port=9;
    nat_core_input(&n,10,elsewhere,m.data,m.length,count_send,NULL);CHECK(!n.seen_ok[0]);
    nat_core_input(&n,10,c.server[1],m.data,m.length,count_send,NULL);CHECK(!n.seen_ok[0]&&!n.seen_ok[1]);   /* index 0 from port 1 */
    nat_core_input(&n,10,c.server[0],m.data,m.length,count_send,NULL);CHECK(n.seen_ok[0]&&n.seen[0].port==5000);
    CHECK(nat_pair_unpunchable(NAT_MAP_EDM,NAT_FILTER_PORT,NAT_MAP_EDM,NAT_FILTER_PORT));
    CHECK(nat_pair_unpunchable(NAT_MAP_EIM,NAT_FILTER_PORT,NAT_MAP_EDM,NAT_FILTER_PORT));
    CHECK(!nat_pair_unpunchable(NAT_MAP_EIM,NAT_FILTER_OPEN,NAT_MAP_EDM,NAT_FILTER_PORT));
    CHECK(!nat_pair_unpunchable(NAT_MAP_EIM,NAT_FILTER_PORT,NAT_MAP_EIM,NAT_FILTER_PORT));
    CHECK(!nat_pair_unpunchable(NAT_MAP_UNKNOWN,NAT_FILTER_UNKNOWN,NAT_MAP_EDM,NAT_FILTER_PORT));
}
int main(void) {
    test_hmac();test_numbers_and_addresses();test_messages();test_pcp_natpmp();test_upnp();test_route();test_core();
    if(failures) {printf("test_lockstep_nat: %d failures\n",failures);return 1;}
    printf("test_lockstep_nat: ok\n");return 0;
}
