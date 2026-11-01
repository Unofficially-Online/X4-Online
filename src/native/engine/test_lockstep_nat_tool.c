/* Test tool for the NAT traversal module (roadmap B7); never part of a package.
 *   driver   line protocol on stdin/stdout: the sans-IO core in the virtual-time NAT
 *            simulation of tools/dev/nat_sim.py (no sockets at all)
 *   link     a real nat_link on loopback against the relay's rendezvous extension
 *   portmap  PCP / NAT-PMP / UPnP against the loopback fakes of tools/dev/nat_fake_gateway.py
 * Every address comes from the command line; there is no discovery mode here, so this
 * tool cannot reach a real router. */
#ifndef _WIN32
#define _GNU_SOURCE
#endif
#include "lockstep_nat_net.h"
#include <stdlib.h>

static int parse_u32(const char *text,uint32_t *out) {uint64_t v;if(!nat_uint(text,0xffffffffu,&v)) return 0;*out=(uint32_t)v;return 1;}
/* "ip:port" where port 0 asks for an ephemeral port (nat_addr_parse refuses port 0). */
static int parse_bind(const char *text,nat_addr *out) {
    char copy[32];size_t n=strlen(text);
    if(n<3||n>=sizeof(copy)) return 0;
    strcpy(copy,text);
    int ephemeral=!strcmp(copy+n-2,":0");
    if(ephemeral) copy[n-1]='1';
    if(!nat_addr_parse(copy,out)) return 0;
    if(ephemeral) out->port=0;
    return 1;
}

/* ---- driver ---- */
static void driver_send(void *context,nat_addr to,const char *data,size_t length) {
    (void)context;char a[24],hex[2*NAT_DATAGRAM_MAX+1];
    nat_addr_format(to,a,sizeof(a));nat_hex((const unsigned char*)data,length,hex);
    printf("SEND %s %s\n",a,hex);
}
static void driver_next(uint64_t due) {if(due==UINT64_MAX) printf("NEXT -1\n"); else printf("NEXT %llu\n",(unsigned long long)due);}
static int driver_option(nat_config *c,const char *key,const char *value) {
    uint32_t v=0;
    struct {const char *name;uint32_t *field;} timings[]={{"hello",&c->hello_ms},{"classify",&c->classify_ms},{"ask",&c->ask_ms},
        {"asktimeout",&c->ask_timeout_ms},{"probe",&c->probe_ms},{"punch",&c->punch_ms},{"upgrade",&c->upgrade_ms},
        {"keepalive",&c->keepalive_ms},{"kafast",&c->keepalive_fast_ms},{"dead",&c->dead_ms},{"retry",&c->retry_ms},{"refresh",&c->refresh_ms}};
    for(unsigned i=0;i<sizeof(timings)/sizeof(*timings);i++) if(!strcmp(key,timings[i].name)) return parse_u32(value,timings[i].field);
    if(!strcmp(key,"slot")) {if(!parse_u32(value,&v)) return 0;c->slot=v;return 1;}
    if(!strcmp(key,"clients")) {if(!parse_u32(value,&v)) return 0;c->clients=v;return 1;}
    if(!strcmp(key,"attempts")) {if(!parse_u32(value,&v)) return 0;c->attempts_max=v;return 1;}
    if(!strcmp(key,"want")) {if(!parse_u32(value,&v)) return 0;c->want=v;return 1;}
    if(!strcmp(key,"loopback")) {if(!parse_u32(value,&v)) return 0;c->allow_loopback=(int)v;return 1;}
    if(!strcmp(key,"sharelan")) {if(!parse_u32(value,&v)) return 0;c->share_lan=(int)v;return 1;}
    if(!strcmp(key,"token")) {if(strlen(value)!=32) return 0;strcpy(c->token,value);return 1;}
    if(!strcmp(key,"inst")) return nat_unhex(value,c->instance,8);
    if(!strcmp(key,"s0")) return nat_addr_parse(value,&c->server[0]);
    if(!strcmp(key,"s1")) return nat_addr_parse(value,&c->server[1]);
    if(!strcmp(key,"lan")) return nat_addr_parse(value,&c->lan);
    if(!strcmp(key,"map")) return nat_addr_parse(value,&c->mapped);
    return 0;
}
static int run_driver(void) {
    static nat_core core;char line[4096];int ready=0;
    static const char *const states[]={"pending","direct","relay"};
    while(fgets(line,sizeof(line),stdin)) {
        line[strcspn(line,"\r\n")]=0;
        char *command=strtok(line," ");
        if(!command) continue;
        if(!strcmp(command,"QUIT")) break;
        if(!strcmp(command,"INIT")) {
            nat_config c;nat_config_defaults(&c);int ok=1;
            for(char *pair;(pair=strtok(NULL," "));) {
                char *eq=strchr(pair,'=');
                if(!eq) {ok=0;break;}
                *eq=0;if(!driver_option(&c,pair,eq+1)) {ok=0;printf("ERR option %s\n",pair);break;}
            }
            const char *refused=ok?nat_core_init(&core,&c):"options";
            if(refused) printf("ERR %s\n",refused); else {printf("OK\n");ready=1;}
        }
        else if(!ready) printf("ERR not_initialised\n");
        else if(!strcmp(command,"RECV")) {
            char *now_text=strtok(NULL," "),*from_text=strtok(NULL," "),*hex=strtok(NULL," ");
            uint64_t now;nat_addr from;static unsigned char data[NAT_DATAGRAM_MAX*2+1];
            size_t length=hex?strlen(hex)/2:0;
            if(!now_text||!from_text||!hex||!nat_uint(now_text,UINT64_C(999999999999999),&now)||!nat_addr_parse(from_text,&from)||
               strlen(hex)%2||length>sizeof(data)||!nat_unhex(hex,data,length)) printf("ERR recv\n");
            else {
                int consumed=nat_core_input(&core,now,from,(const char*)data,length,driver_send,NULL);
                printf("CONSUMED %d\n",consumed);
                driver_next(nat_core_poll(&core,now,driver_send,NULL));
            }
        }
        else if(!strcmp(command,"POLL")) {
            char *now_text=strtok(NULL," ");uint64_t now;
            if(!now_text||!nat_uint(now_text,UINT64_C(999999999999999),&now)) printf("ERR poll\n");
            else driver_next(nat_core_poll(&core,now,driver_send,NULL));
        }
        else if(!strcmp(command,"PATH")) {
            char *k_text=strtok(NULL," ");uint32_t k=0,rtt=0;nat_addr path={0,0};char a[24];
            if(!k_text||!parse_u32(k_text,&k)) printf("ERR path\n");
            else {
                int state=nat_core_path(&core,k,&path,&rtt);nat_addr_format(state==NAT_PATH_DIRECT?path:(nat_addr){0,0},a,sizeof(a));
                printf("PATH %u %s %s %u %s\n",(unsigned)k,states[state],a,(unsigned)rtt,nat_core_reason(&core,k));
            }
        }
        else if(!strcmp(command,"ALIVE")) {
            char *k_text=strtok(NULL," "),*now_text=strtok(NULL," ");uint32_t k;uint64_t now;
            if(k_text&&now_text&&parse_u32(k_text,&k)&&nat_uint(now_text,UINT64_C(999999999999999),&now)) nat_core_alive(&core,k,now);
        }
        else if(!strcmp(command,"MAPPED")) {
            char *a_text=strtok(NULL," "),*now_text=strtok(NULL," ");nat_addr a;uint64_t now;
            if(a_text&&now_text&&nat_addr_parse(a_text,&a)&&nat_uint(now_text,UINT64_C(999999999999999),&now)) nat_core_set_mapped(&core,a,now);
        }
        else if(!strcmp(command,"CLOSE")) nat_core_close(&core,driver_send,NULL);
        else if(!strcmp(command,"DESCRIBE")) {char text[1024];nat_core_describe(&core,text,sizeof(text));printf("DESC %s\n",text);}
        else printf("ERR command\n");
        printf("END\n");fflush(stdout);
    }
    return 0;
}

/* ---- link (real sockets, loopback) ---- */
static const char *argument(int argc,char **argv,const char *name) {
    for(int i=2;i+1<argc;i++) if(!strcmp(argv[i],name)) return argv[i+1];
    return NULL;
}
static int flag(int argc,char **argv,const char *name) {for(int i=2;i<argc;i++) if(!strcmp(argv[i],name)) return 1;return 0;}
static int run_link(int argc,char **argv) {
    nat_config c;nat_config_defaults(&c);
    const char *server=argument(argc,argv,"--server"),*token=argument(argc,argv,"--token"),*slot=argument(argc,argv,"--slot"),
               *clients=argument(argc,argv,"--clients"),*seconds_text=argument(argc,argv,"--seconds"),*bind_text=argument(argc,argv,"--bind");
    uint32_t s=0,n=0,seconds=10;nat_addr bind_addr={0,0};
    if(!server||!token||!slot||!clients||!nat_addr_parse(server,&c.server[0])||strlen(token)!=32||!parse_u32(slot,&s)||!parse_u32(clients,&n)||
       (seconds_text&&!parse_u32(seconds_text,&seconds))||(bind_text&&!parse_bind(bind_text,&bind_addr))) {fprintf(stderr,"link: bad arguments\n");return 2;}
    c.server[1]=c.server[0];c.server[1].port=(uint16_t)(c.server[0].port+1);
    strcpy(c.token,token);c.slot=s;c.clients=n;c.allow_loopback=flag(argc,argv,"--loopback");
    if(flag(argc,argv,"--fast")) {c.classify_ms=600;c.punch_ms=3000;c.ask_timeout_ms=4000;}
    int data=flag(argc,argv,"--data"),hold=flag(argc,argv,"--hold");   /* hold: run --seconds, print every path change */
    int last_state[NAT_PEERS_MAX+1];for(unsigned k=0;k<=NAT_PEERS_MAX;k++) last_state[k]=-1;
    nat_link link;const char *refused=nat_link_open(&link,&c,bind_addr.ip,bind_addr.port);
    if(refused) {printf("LINK REFUSED %s\n",refused);return 1;}
    char a[24];nat_addr_format(link.bound,a,sizeof(a));printf("LINK OPEN %s\n",a);fflush(stdout);
    uint64_t start=nat_clock_ms(),last_data=0;unsigned got_data=0,wanted=link.core.cfg.want;
    for(;;) {
        uint64_t now=nat_clock_ms();
        char buffer[600];unsigned peer=0;int length;
        while((length=nat_link_recv(&link,now,buffer,sizeof(buffer)-1,&peer))>=0) {
            buffer[length]=0;
            if(!(got_data>>peer&1u)) {printf("DATA %u %s\n",peer,buffer);fflush(stdout);}
            got_data|=1u<<peer;nat_core_alive(&link.core,peer,now);
        }
        uint64_t due=nat_link_service(&link,now);
        int decided=1;
        for(unsigned k=1;k<=n;k++) {
            if(!(wanted>>k&1u)) continue;
            nat_addr seen_path={0,0};uint32_t seen_rtt=0;
            int state=nat_link_path(&link,k,&seen_path,&seen_rtt);
            if(state==NAT_PATH_PENDING) decided=0;
            if(hold&&state!=last_state[k]) {
                static const char *const names[]={"pending","direct","relay"};char pa[24];
                nat_addr_format(state==NAT_PATH_DIRECT?seen_path:(nat_addr){0,0},pa,sizeof(pa));
                printf("CHANGE %llu PATH %u %s %s %u %s\n",(unsigned long long)(now-start),k,names[state],pa,(unsigned)seen_rtt,nat_core_reason(&link.core,k));
                fflush(stdout);last_state[k]=state;
            }
            if(state==NAT_PATH_DIRECT&&data&&now-last_data>=200) {
                char text[64];int tl=snprintf(text,sizeof(text),"hello from slot %u",(unsigned)s);nat_link_send(&link,k,text,(size_t)tl);
            }
            if(data&&state==NAT_PATH_DIRECT&&!(got_data>>k&1u)) decided=0;
        }
        if(data&&now-last_data>=200) last_data=now;
        if((decided&&!hold&&now-start>=1000)||now-start>=(uint64_t)seconds*1000u) break;
        uint64_t wait=due>now?due-now:0;if(wait>20) wait=20;
        nat_wait(link.s,0,(uint32_t)wait);
    }
    /* Keep answering the peer's last probes for a moment so both sides settle. */
    for(uint64_t until=nat_clock_ms()+500;nat_clock_ms()<until;) {
        char buffer[600];unsigned peer;uint64_t now=nat_clock_ms();
        while(nat_link_recv(&link,now,buffer,sizeof(buffer),&peer)>=0) {}
        nat_link_service(&link,now);nat_wait(link.s,0,20);
    }
    for(unsigned k=1;k<=n;k++) {
        if(!(wanted>>k&1u)) continue;
        nat_addr path={0,0};uint32_t rtt=0;int state=nat_link_path(&link,k,&path,&rtt);
        static const char *const states[]={"pending","direct","relay"};
        nat_addr_format(state==NAT_PATH_DIRECT?path:(nat_addr){0,0},a,sizeof(a));
        printf("PATH %u %s %s %u %s\n",k,states[state],a,(unsigned)rtt,nat_core_reason(&link.core,k));
    }
    char text[1024];nat_core_describe(&link.core,text,sizeof(text));printf("DESC %s\n",text);
    nat_link_close(&link);printf("LINK CLOSED\n");fflush(stdout);
    return 0;
}

/* ---- portmap (loopback fakes only) ---- */
static int run_portmap(int argc,char **argv) {
    nat_portmap_options o;memset(&o,0,sizeof(o));
    const char *gateway=argument(argc,argv,"--gateway"),*ssdp=argument(argc,argv,"--ssdp"),*methods=argument(argc,argv,"--methods"),
               *internal=argument(argc,argv,"--internal-port"),*external=argument(argc,argv,"--external-port"),
               *lease=argument(argc,argv,"--lease"),*timeout=argument(argc,argv,"--timeout");
    uint32_t v;
    if(gateway&&!nat_addr_parse(gateway,&o.gateway)) return 2;
    if(ssdp&&!nat_addr_parse(ssdp,&o.ssdp)) return 2;
    if(!methods||!internal||!parse_u32(internal,&v)||!v||v>65535) {fprintf(stderr,"portmap: bad arguments\n");return 2;}
    o.internal_port=(uint16_t)v;
    if(external) {if(!parse_u32(external,&v)||v>65535) return 2;o.external_port=(uint16_t)v;}
    o.lease_s=3600;if(lease&&!parse_u32(lease,&o.lease_s)) return 2;
    o.timeout_ms=1000;if(timeout&&!parse_u32(timeout,&o.timeout_ms)) return 2;
    if(strstr(methods,"pcp")) o.methods|=NAT_PM_PCP;
    if(strstr(methods,"natpmp")) o.methods|=NAT_PM_NATPMP;
    if(strstr(methods,"upnp")) o.methods|=NAT_PM_UPNP;
    /* Only explicit loopback targets, unless --real-gateway names the live test with a real router (NAT plan L8, done only
     * with the router owner's OK): never a real gateway by accident. */
    int real=flag(argc,argv,"--real-gateway");
    if(!o.gateway.ip&&!o.ssdp.ip) {fprintf(stderr,"portmap: --gateway or --ssdp required\n");return 2;}
    if(!real&&((o.gateway.ip&&!nat_ip_loopback(o.gateway.ip))||(o.ssdp.ip&&!nat_ip_loopback(o.ssdp.ip)))) {
        fprintf(stderr,"portmap: loopback fakes only (--real-gateway for the live router test)\n");return 2;
    }
    nat_portmap m;char a[24];static const char *const names[]={"none","pcp","natpmp","","upnp"};
    if(!nat_portmap_acquire(&m,&o)) {printf("REFUSED %s\n",m.refusal);return 0;}
    nat_addr_format(m.external,a,sizeof(a));printf("MAPPED %s %s %u\n",names[m.method],a,(unsigned)m.lease_s);
    if(flag(argc,argv,"--renew")) {
        if(nat_portmap_renew(&m)) {nat_addr_format(m.external,a,sizeof(a));printf("RENEWED %s %u\n",a,(unsigned)m.lease_s);}
        else printf("RENEW_FAILED\n");
    }
    if(flag(argc,argv,"--release")) {nat_portmap_release(&m);printf("RELEASED\n");}
    return 0;
}

int main(int argc,char **argv) {
    if(argc>=2&&!strcmp(argv[1],"driver")) return run_driver();
    if(argc>=2&&!strcmp(argv[1],"link")) return run_link(argc,argv);
    if(argc>=2&&!strcmp(argv[1],"portmap")) return run_portmap(argc,argv);
    fprintf(stderr,"usage: %s driver|link|portmap ...\n",argv[0]);
    return 2;
}
