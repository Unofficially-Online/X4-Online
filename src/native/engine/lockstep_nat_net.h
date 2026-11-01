/* Sockets for the NAT traversal core (roadmap B7): Windows winsock and Linux/BSD.
 *
 * nat_link: one unconnected UDP socket shared by the NGNAT rendezvous/punching
 * messages and the caller's own direct-link datagrams (hole punching only works when
 * the data leaves through the socket whose NAT mapping the relay observed). The
 * gate's relay socket (lockstep_gate.c `connection`) stays separate and untouched.
 *
 *   nat_link_open      socket + core; nothing is sent before the first service call
 *   nat_link_recv      reads datagrams: NGNAT ones are handled inside, others are
 *                      returned with the sending slot (only from validated peer
 *                      addresses; anything else is dropped)
 *   nat_link_service   timers (rendezvous, probes, keep-alives); returns next due ms
 *   nat_link_path      DIRECT + address, RELAY ("unreachable, use the relay") or PENDING
 *   nat_link_send      sends to a DIRECT peer, else -1 (send through the relay)
 *   nat_link_close     CLOSE to direct peers, BYE to the relay, socket closed
 *
 * nat_portmap_*: blocking, bounded (opt.timeout_ms per method) PCP -> NAT-PMP -> UPnP
 * IGD port mapping with lease, renewal and release; call from a worker thread before
 * nat_link_open (the result goes into nat_config.mapped). The gateway and SSDP target
 * come from the caller; the routing table and SSDP multicast are only consulted with
 * allow_discovery, which no test sets. Single-threaded per link. Original code. */
#ifndef LOCKSTEP_NAT_NET_H
#define LOCKSTEP_NAT_NET_H
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include <iphlpapi.h>
typedef SOCKET nat_socket;
#define NAT_NO_SOCKET INVALID_SOCKET
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR,12)
#endif
#else
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
typedef int nat_socket;
#define NAT_NO_SOCKET (-1)
#endif
#include <stdlib.h>
#include "lockstep_nat.h"
#include "lockstep_nat_portmap.h"

/* ---- platform ---- */
static inline int nat_net_start(void) {
#ifdef _WIN32
    WSADATA w;return !WSAStartup(MAKEWORD(2,2),&w);
#else
    return 1;
#endif
}
static inline void nat_net_stop(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}
static inline void nat_socket_close(nat_socket s) {
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}
static inline int nat_random_bytes(void *out,size_t length) {
#ifdef _WIN32
    return BCryptGenRandom(NULL,(PUCHAR)out,(ULONG)length,BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0;
#else
    int fd=open("/dev/urandom",O_RDONLY|O_CLOEXEC);if(fd<0) return 0;
    size_t got=0;
    while(got<length) {ssize_t n=read(fd,(char*)out+got,length-got);if(n<=0) {if(n<0&&errno==EINTR) continue;break;}got+=(size_t)n;}
    close(fd);return got==length;
#endif
}
static inline uint64_t nat_clock_ms(void) {
#ifdef _WIN32
    LARGE_INTEGER f,c;QueryPerformanceFrequency(&f);QueryPerformanceCounter(&c);
    return (uint64_t)(c.QuadPart/f.QuadPart*1000+(c.QuadPart%f.QuadPart)*1000/f.QuadPart);
#else
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000u+(uint64_t)t.tv_nsec/1000000u;
#endif
}
static inline void nat_sleep_ms(unsigned ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec t={(time_t)(ms/1000),(long)(ms%1000)*1000000L};while(nanosleep(&t,&t)&&errno==EINTR) {}
#endif
}
static inline struct sockaddr_in nat_sockaddr(nat_addr a) {
    struct sockaddr_in s;memset(&s,0,sizeof(s));s.sin_family=AF_INET;s.sin_port=htons(a.port);s.sin_addr.s_addr=htonl(a.ip);return s;
}
static inline nat_addr nat_from_sockaddr(const struct sockaddr_in *s) {nat_addr a={ntohl(s->sin_addr.s_addr),ntohs(s->sin_port)};return a;}
static inline int nat_set_nonblocking(nat_socket s,int on) {
#ifdef _WIN32
    u_long v=(u_long)on;return !ioctlsocket(s,FIONBIO,&v);
#else
    int flags=fcntl(s,F_GETFL,0);if(flags<0) return 0;
    return !fcntl(s,F_SETFL,on?flags|O_NONBLOCK:flags&~O_NONBLOCK);
#endif
}
/* 1 readable/writable, 0 timeout, -1 error. */
static inline int nat_wait(nat_socket s,int for_write,uint32_t timeout_ms) {
    fd_set set;FD_ZERO(&set);FD_SET(s,&set);
    struct timeval t={(long)(timeout_ms/1000),(long)(timeout_ms%1000)*1000L};
    int r=select((int)s+1,for_write?NULL:&set,for_write?&set:NULL,NULL,&t);
    return r>0?1:r==0?0:-1;
}
static inline nat_socket nat_udp_open(uint32_t ip,uint16_t port,nat_addr *bound) {
    nat_socket s=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if(s==NAT_NO_SOCKET) return s;
#ifdef _WIN32
    /* No WSAECONNRESET on later receives after an ICMP port unreachable. */
    BOOL off=FALSE;DWORD unused=0;WSAIoctl(s,SIO_UDP_CONNRESET,&off,sizeof(off),NULL,0,&unused,NULL,NULL);
#endif
    struct sockaddr_in local=nat_sockaddr((nat_addr){ip,port});struct sockaddr_in got;socklen_t size=sizeof(got);
    if(bind(s,(struct sockaddr*)&local,sizeof(local))||!nat_set_nonblocking(s,1)||getsockname(s,(struct sockaddr*)&got,&size)) {
        nat_socket_close(s);return NAT_NO_SOCKET;
    }
    if(bound) *bound=nat_from_sockaddr(&got);
    return s;
}
static inline int nat_udp_sendto(nat_socket s,nat_addr to,const void *data,size_t length) {
    struct sockaddr_in d=nat_sockaddr(to);
    return sendto(s,(const char*)data,(int)length,0,(struct sockaddr*)&d,sizeof(d))==(int)length;
}
/* Length of the next datagram, or -1 when none is waiting. */
static inline int nat_udp_recvfrom(nat_socket s,void *buffer,size_t capacity,nat_addr *from) {
    for(;;) {
        struct sockaddr_in a;socklen_t size=sizeof(a);
        int n=(int)recvfrom(s,(char*)buffer,(int)capacity,0,(struct sockaddr*)&a,&size);
        if(n>=0) {if(size<(socklen_t)sizeof(a)||a.sin_family!=AF_INET) continue;*from=nat_from_sockaddr(&a);return n;}
#ifdef _WIN32
        int e=WSAGetLastError();if(e==WSAECONNRESET||e==WSAEMSGSIZE) continue;   /* oversize: dropped whole */
#else
        if(errno==EINTR) continue;
#endif
        return -1;
    }
}
/* The local address the system would use toward dest (connect on UDP sends nothing). */
static inline int nat_local_ip_toward(nat_addr dest,uint32_t *ip) {
    nat_socket s=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(s==NAT_NO_SOCKET) return 0;
    struct sockaddr_in d=nat_sockaddr(dest),got;socklen_t size=sizeof(got);
    int ok=!connect(s,(struct sockaddr*)&d,sizeof(d))&&!getsockname(s,(struct sockaddr*)&got,&size);
    if(ok) *ip=ntohl(got.sin_addr.s_addr);
    nat_socket_close(s);return ok&&*ip;
}
/* Linux /proc/net/route text: the default route's gateway (little-endian hex fields). */
static inline int nat_route_parse_linux(const char *text,uint32_t *gateway) {
    const char *line=strchr(text,'\n');
    while(line&&*++line) {
        char name[32];unsigned long destination,via,flags;
        if(sscanf(line,"%31s %lx %lx %lx",name,&destination,&via,&flags)==4&&!destination&&(flags&3u)==3u&&via) {
            *gateway=(uint32_t)((via&255u)<<24|(via>>8&255u)<<16|(via>>16&255u)<<8|(via>>24&255u));return 1;
        }
        line=strchr(line,'\n');
    }
    return 0;
}
/* Production only (allow_discovery): the IPv4 default gateway, read from the routing table. */
static inline int nat_default_gateway(uint32_t *gateway) {
#ifdef _WIN32
    typedef DWORD (WINAPI *best_route_fn)(DWORD,DWORD,PMIB_IPFORWARDROW);
    HMODULE module=LoadLibraryW(L"iphlpapi.dll");if(!module) return 0;
    best_route_fn best=(best_route_fn)(void*)GetProcAddress(module,"GetBestRoute");
    MIB_IPFORWARDROW row;memset(&row,0,sizeof(row));
    int ok=best&&!best(htonl(0x01010101u),0,&row)&&row.dwForwardNextHop;   /* any public destination */
    if(ok) *gateway=ntohl(row.dwForwardNextHop);
    FreeLibrary(module);return ok;
#else
    char text[8192];FILE *f=fopen("/proc/net/route","r");if(!f) return 0;
    size_t n=fread(text,1,sizeof(text)-1,f);fclose(f);text[n]=0;
    return nat_route_parse_linux(text,gateway);
#endif
}

/* ---- the direct link socket ---- */
typedef struct {nat_socket s;nat_core core;nat_addr bound;int net;uint64_t dropped;} nat_link;
static inline void nat_link_sender(void *context,nat_addr to,const char *data,size_t length) {
    nat_link *l=(nat_link*)context;nat_udp_sendto(l->s,to,data,length);
}
/* 0 or a refusal reason (the link then stays closed and every path is RELAY). cfg->lan
 * ip 0: the local address toward the relay; cfg->instance all zero: random. */
static inline const char *nat_link_open(nat_link *l,nat_config *cfg,uint32_t bind_ip,uint16_t bind_port) {
    memset(l,0,sizeof(*l));l->s=NAT_NO_SOCKET;l->core.closed=1;
    if(!nat_net_start()) return "winsock";
    l->net=1;
    l->s=nat_udp_open(bind_ip,bind_port,&l->bound);
    if(l->s==NAT_NO_SOCKET) {nat_net_stop();l->net=0;return "socket";}
    static const unsigned char zero[8];
    if(!memcmp(cfg->instance,zero,8)&&!nat_random_bytes(cfg->instance,8)) {nat_socket_close(l->s);l->s=NAT_NO_SOCKET;nat_net_stop();l->net=0;return "random";}
    if(!cfg->lan.ip) {
        uint32_t ip=l->bound.ip;
        if(ip||nat_local_ip_toward(cfg->server[0],&ip)) {cfg->lan.ip=ip;cfg->lan.port=l->bound.port;}
    }
    const char *refused=nat_core_init(&l->core,cfg);
    if(refused) {nat_socket_close(l->s);l->s=NAT_NO_SOCKET;nat_net_stop();l->net=0;}
    return refused;
}
/* A non-NGNAT datagram from a validated direct peer: its length (peer = slot), else -1. */
static inline int nat_link_recv(nat_link *l,uint64_t now,void *buffer,size_t capacity,unsigned *peer) {
    if(l->s==NAT_NO_SOCKET) return -1;
    for(;;) {
        nat_addr from;int n=nat_udp_recvfrom(l->s,buffer,capacity,&from);
        if(n<0) return -1;
        if(nat_core_input(&l->core,now,from,(const char*)buffer,(size_t)n,nat_link_sender,l)) continue;
        unsigned k=nat_core_peer_of(&l->core,from);
        if(!k) {++l->dropped;continue;}
        *peer=k;return n;
    }
}
static inline uint64_t nat_link_service(nat_link *l,uint64_t now) {
    return l->s==NAT_NO_SOCKET?UINT64_MAX:nat_core_poll(&l->core,now,nat_link_sender,l);
}
static inline int nat_link_path(const nat_link *l,unsigned peer,nat_addr *path,uint32_t *rtt_ms) {
    return l->s==NAT_NO_SOCKET?NAT_PATH_RELAY:nat_core_path(&l->core,peer,path,rtt_ms);
}
/* 0 sent directly; -1: no direct path (send through the relay). NGNAT-prefixed data is refused. */
static inline int nat_link_send(nat_link *l,unsigned peer,const void *data,size_t length) {
    nat_addr path;
    if(length>=6&&!memcmp(data,"NGNAT|",6)) return -1;
    if(nat_link_path(l,peer,&path,NULL)!=NAT_PATH_DIRECT) return -1;
    return nat_udp_sendto(l->s,path,data,length)?0:-1;
}
static inline void nat_link_close(nat_link *l) {
    if(l->s!=NAT_NO_SOCKET) {nat_core_close(&l->core,nat_link_sender,l);nat_socket_close(l->s);l->s=NAT_NO_SOCKET;}
    l->core.closed=1;
    if(l->net) {nat_net_stop();l->net=0;}
}

/* ---- port mapping (blocking, bounded) ---- */
typedef struct {
    nat_addr gateway;          /* PCP/NAT-PMP server; ip 0: the default gateway (allow_discovery only) */
    nat_addr ssdp;             /* SSDP search target; ip 0: 239.255.255.250:1900 (allow_discovery only) */
    int allow_discovery;
    unsigned methods;          /* NAT_PM_PCP|NAT_PM_NATPMP|NAT_PM_UPNP */
    uint16_t internal_port,external_port;   /* external 0: ask for the internal port */
    uint32_t lease_s,timeout_ms;
} nat_portmap_options;
typedef struct {
    nat_portmap_options opt;
    unsigned method;           /* 0: no mapping */
    nat_addr external,gateway;
    uint32_t lease_s,internal_ip;
    uint64_t acquired_ms;
    unsigned char nonce[12];
    nat_url control;char service[96];
    char refusal[96];          /* why the last method failed (the first refusal is kept) */
} nat_portmap;
static inline void nat_portmap_refuse(nat_portmap *m,const char *method,const char *why) {
    if(!m->refusal[0]) snprintf(m->refusal,sizeof(m->refusal),"%s:%s",method,why);
}
/* One request/response exchange over a connected UDP socket with RFC-style doubling
 * retransmissions inside timeout_ms. Returns the reply length or -1. */
typedef int (*nat_accept_fn)(const unsigned char *reply,size_t length,void *context);
static inline int nat_udp_exchange(nat_addr server,const unsigned char *request,size_t length,unsigned char *reply,size_t capacity,
                            uint32_t timeout_ms,nat_accept_fn accept,void *context) {
    nat_socket s=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(s==NAT_NO_SOCKET) return -1;
    struct sockaddr_in d=nat_sockaddr(server);
    if(connect(s,(struct sockaddr*)&d,sizeof(d))) {nat_socket_close(s);return -1;}
    uint64_t start=nat_clock_ms(),next=start;uint32_t step=timeout_ms/7?timeout_ms/7:1;int result=-1;
    for(;;) {
        uint64_t now=nat_clock_ms();
        if(now-start>=timeout_ms) break;
        if(now>=next) {send(s,(const char*)request,(int)length,0);next=now+step;step*=2;}
        uint64_t until=next<start+timeout_ms?next:start+timeout_ms;
        int w=nat_wait(s,0,(uint32_t)(until>now?until-now:1));
        if(w<0) break;
        if(!w) continue;
        int n=(int)recv(s,(char*)reply,(int)capacity,0);
        if(n<0) {
#ifdef _WIN32
            if(WSAGetLastError()==WSAECONNRESET) break;   /* port unreachable: no server there */
#else
            if(errno==ECONNREFUSED) break;
#endif
            continue;
        }
        if(accept((const unsigned char*)reply,(size_t)n,context)) {result=n;break;}
    }
    nat_socket_close(s);return result;
}
typedef struct {const unsigned char *nonce;uint16_t internal_port;nat_pcp_reply pcp;nat_pmp_reply pmp;int address;} nat_pm_context;
static inline int nat_accept_pcp(const unsigned char *p,size_t n,void *c) {nat_pm_context *x=(nat_pm_context*)c;return nat_pcp_map_parse(p,n,x->nonce,x->internal_port,&x->pcp);}
static inline int nat_accept_pmp(const unsigned char *p,size_t n,void *c) {
    nat_pm_context *x=(nat_pm_context*)c;
    return x->address?nat_pmp_address_parse(p,n,&x->pmp):nat_pmp_map_parse(p,n,x->internal_port,&x->pmp);
}
/* 1 granted, 0 refused or no reply, -1 the gateway speaks only NAT-PMP. */
static inline int nat_pcp_map(nat_portmap *m,uint16_t suggested,uint32_t lifetime) {
    unsigned char request[NAT_PCP_MAP_SIZE],reply[1100];
    nat_pcp_map_request(request,m->internal_ip,m->nonce,m->opt.internal_port,suggested,lifetime);
    nat_pm_context x={m->nonce,m->opt.internal_port,{0},{0},0};
    if(nat_udp_exchange(m->gateway,request,sizeof(request),reply,sizeof(reply),m->opt.timeout_ms,nat_accept_pcp,&x)<0) {nat_portmap_refuse(m,"pcp","no_reply");return 0;}
    if(x.pcp.version0) return -1;
    if(x.pcp.result) {nat_portmap_refuse(m,"pcp",nat_pcp_result_name(x.pcp.result));return 0;}
    if(!lifetime) return 1;
    if(!x.pcp.external_ip||!x.pcp.external_port||!x.pcp.lifetime) {nat_portmap_refuse(m,"pcp","empty_grant");return 0;}
    m->external.ip=x.pcp.external_ip;m->external.port=x.pcp.external_port;m->lease_s=x.pcp.lifetime;return 1;
}
static inline int nat_pmp_map(nat_portmap *m,uint16_t suggested,uint32_t lifetime) {
    unsigned char request[NAT_PMP_MAP_SIZE],reply[64];
    nat_pm_context x={NULL,m->opt.internal_port,{0},{0},0};
    if(lifetime&&!m->external.ip) {
        unsigned char ask[2];nat_pmp_address_request(ask);x.address=1;
        if(nat_udp_exchange(m->gateway,ask,2,reply,sizeof(reply),m->opt.timeout_ms,nat_accept_pmp,&x)<0) {nat_portmap_refuse(m,"natpmp","no_reply");return 0;}
        if(x.pmp.result||!x.pmp.external_ip) {nat_portmap_refuse(m,"natpmp",x.pmp.result==2?"not_authorized":"no_external_address");return 0;}
        m->external.ip=x.pmp.external_ip;x.address=0;
    }
    nat_pmp_map_request(request,m->opt.internal_port,lifetime?suggested:0,lifetime);
    if(nat_udp_exchange(m->gateway,request,sizeof(request),reply,sizeof(reply),m->opt.timeout_ms,nat_accept_pmp,&x)<0) {nat_portmap_refuse(m,"natpmp","no_reply");return 0;}
    if(x.pmp.result) {
        static const char *const why[]={"success","unsupported_version","not_authorized","network_failure","no_resources","unsupported_opcode"};
        nat_portmap_refuse(m,"natpmp",x.pmp.result<6?why[x.pmp.result]:"unknown_result");return 0;
    }
    if(!lifetime) return 1;
    if(!x.pmp.external_port||!x.pmp.lifetime) {nat_portmap_refuse(m,"natpmp","empty_grant");return 0;}
    m->external.port=x.pmp.external_port;m->lease_s=x.pmp.lifetime;return 1;
}
/* HTTP/1.1 exchange with Connection: close; the response (chunked bodies decoded in place). */
static inline int nat_http_exchange(const nat_url *u,const char *request,size_t length,char *response,size_t capacity,uint32_t timeout_ms) {
    nat_socket s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(s==NAT_NO_SOCKET) return -1;
    struct sockaddr_in d=nat_sockaddr((nat_addr){u->ip,(uint16_t)u->port});
    uint64_t start=nat_clock_ms();int total=-1;
    if(!nat_set_nonblocking(s,1)) goto done;
    if(connect(s,(struct sockaddr*)&d,sizeof(d))) {
#ifdef _WIN32
        if(WSAGetLastError()!=WSAEWOULDBLOCK) goto done;
#else
        if(errno!=EINPROGRESS) goto done;
#endif
        if(nat_wait(s,1,timeout_ms)!=1) goto done;
        int error=0;socklen_t size=sizeof(error);
        if(getsockopt(s,SOL_SOCKET,SO_ERROR,(char*)&error,&size)||error) goto done;
    }
    for(size_t sent=0;sent<length;) {
        uint64_t now=nat_clock_ms();if(now-start>=timeout_ms) goto done;
        if(nat_wait(s,1,(uint32_t)(timeout_ms-(now-start)))!=1) goto done;
        int n=(int)send(s,request+sent,(int)(length-sent),0);if(n<=0) goto done;
        sent+=(size_t)n;
    }
    size_t got=0;
    for(;;) {
        uint64_t now=nat_clock_ms();if(now-start>=timeout_ms) goto done;
        if(got+1>=capacity) goto done;
        int w=nat_wait(s,0,(uint32_t)(timeout_ms-(now-start)));if(w!=1) goto done;
        int n=(int)recv(s,response+got,(int)(capacity-1-got),0);
        if(n<0) goto done;
        if(!n) break;
        got+=(size_t)n;
    }
    response[got]=0;total=(int)got;
    const char *body;size_t body_length;char encoding[32];
    if(nat_http_status(response,got,&body,&body_length)&&nat_http_header(response,(size_t)(body-response),"Transfer-Encoding",encoding,sizeof(encoding))&&
       (!strcmp(encoding,"chunked")||!strcmp(encoding,"Chunked"))) {
        long n=nat_http_unchunk((char*)body,body_length);
        if(n<0) total=-1; else {total=(int)((size_t)(body-response)+(size_t)n);response[total]=0;}
    }
done:
    nat_socket_close(s);return total;
}
/* SSDP search: the first IGD answer whose LOCATION names the answering address. */
static inline int nat_ssdp_discover(const nat_portmap_options *o,nat_url *location) {
    nat_addr target=o->ssdp;
    if(!target.ip) {if(!o->allow_discovery) return 0;target.ip=0xeffffffau;target.port=1900;}
    nat_socket s=nat_udp_open(0,0,NULL);if(s==NAT_NO_SOCKET) return 0;
    if((target.ip>>28)==14) {unsigned char ttl=2;setsockopt(s,IPPROTO_IP,IP_MULTICAST_TTL,(const char*)&ttl,sizeof(ttl));}
    char search[256];int found=0;
    for(unsigned v=1;v<=2;v++) {int n=nat_ssdp_search(search,sizeof(search),v);nat_udp_sendto(s,target,search,(size_t)n);}
    uint64_t start=nat_clock_ms();
    while(!found) {
        uint64_t now=nat_clock_ms();if(now-start>=o->timeout_ms) break;
        if(nat_wait(s,0,(uint32_t)(o->timeout_ms-(now-start)))!=1) break;
        char reply[1500];nat_addr from;int n=nat_udp_recvfrom(s,reply,sizeof(reply)-1,&from);
        if(n<=0) continue;
        reply[n]=0;char text[256];
        if(strncmp(reply,"HTTP/1.1 200",12)||!nat_http_header(reply,(size_t)n,"LOCATION",text,sizeof(text))) continue;
        if(nat_url_parse(text,location)&&location->ip==from.ip) found=1;
    }
    nat_socket_close(s);return found;
}
static inline int nat_upnp_add(nat_portmap *m,uint16_t external,uint32_t lease,unsigned *error) {
    char request[2048],response[4096];const char *body;size_t body_length;
    int n=nat_upnp_add_request(request,sizeof(request),&m->control,m->service,external,m->opt.internal_port,m->internal_ip,lease);
    if(n<0) return 0;
    int got=nat_http_exchange(&m->control,request,(size_t)n,response,sizeof(response),m->opt.timeout_ms);
    int status=got>0?nat_http_status(response,(size_t)got,&body,&body_length):0;
    *error=status==200?0:status&&body?nat_upnp_error(body,body_length):0;
    if(status!=200&&!*error) *error=status?status:1;
    return status==200;
}
static inline int nat_upnp_map(nat_portmap *m) {
    nat_url location;char request[2048],response[16384];const char *body;size_t body_length;
    if(!nat_ssdp_discover(&m->opt,&location)) {nat_portmap_refuse(m,"upnp","no_gateway");return 0;}
    char get[512];nat_addr host={location.ip,(uint16_t)location.port};char hosttext[24];nat_addr_format(host,hosttext,sizeof(hosttext));
    int n=snprintf(get,sizeof(get),"GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",location.path,hosttext);
    int got=nat_http_exchange(&location,get,(size_t)n,response,sizeof(response),m->opt.timeout_ms);
    if(got<=0||nat_http_status(response,(size_t)got,&body,&body_length)!=200||
       !nat_igd_parse(body,body_length,&location,m->service,sizeof(m->service),&m->control)) {nat_portmap_refuse(m,"upnp","no_wan_service");return 0;}
    if(!nat_local_ip_toward((nat_addr){m->control.ip,(uint16_t)m->control.port},&m->internal_ip)) {nat_portmap_refuse(m,"upnp","no_route");return 0;}
    n=nat_upnp_address_request(request,sizeof(request),&m->control,m->service);
    got=n<0?-1:nat_http_exchange(&m->control,request,(size_t)n,response,sizeof(response),m->opt.timeout_ms);
    uint32_t external_ip=0;
    if(got<=0||nat_http_status(response,(size_t)got,&body,&body_length)!=200||!nat_upnp_external_ip(body,body_length,&external_ip)||!external_ip) {
        nat_portmap_refuse(m,"upnp","no_external_address");return 0;
    }
    uint16_t external=m->opt.external_port?m->opt.external_port:m->opt.internal_port;uint32_t lease=m->opt.lease_s;unsigned error=0;
    for(unsigned attempt=0;attempt<6;attempt++) {
        if(nat_upnp_add(m,external,lease,&error)) {m->external.ip=external_ip;m->external.port=external;m->lease_s=lease;return 1;}
        if(error==718&&attempt<4) {external=(uint16_t)(external>=65535?1024:external+1);continue;}   /* ConflictInMappingEntry */
        if(error==725&&lease) {lease=0;continue;}                     /* OnlyPermanentLeasesSupported: removed at release */
        if(error==724&&external!=m->opt.internal_port) {external=m->opt.internal_port;continue;}   /* SamePortValuesRequired */
        break;
    }
    char why[32];snprintf(why,sizeof(why),"error_%u",error);nat_portmap_refuse(m,"upnp",why);return 0;
}
/* 1: a mapping (method, external, lease_s); 0: none, m->refusal says why. Blocking. */
static inline int nat_portmap_acquire(nat_portmap *m,const nat_portmap_options *o) {
    memset(m,0,sizeof(*m));m->opt=*o;
    if(!o->internal_port||!o->timeout_ms||o->timeout_ms>10000) {nat_portmap_refuse(m,"options","invalid");return 0;}
    if(!nat_net_start()) {nat_portmap_refuse(m,"options","winsock");return 0;}
    m->gateway=o->gateway;
    if(!m->gateway.ip&&o->allow_discovery&&nat_default_gateway(&m->gateway.ip)) m->gateway.port=NAT_PCP_PORT;
    uint16_t suggested=o->external_port?o->external_port:o->internal_port;int ok=0;
    if(m->gateway.ip&&(o->methods&(NAT_PM_PCP|NAT_PM_NATPMP))&&nat_local_ip_toward(m->gateway,&m->internal_ip)) {
        int pcp=0;
        if(o->methods&NAT_PM_PCP) {
            if(!nat_random_bytes(m->nonce,sizeof(m->nonce))) nat_portmap_refuse(m,"pcp","random");
            else if((pcp=nat_pcp_map(m,suggested,o->lease_s))>0) {m->method=NAT_PM_PCP;ok=1;}
        }
        if(!ok&&(o->methods&NAT_PM_NATPMP)&&(pcp<0||!(o->methods&NAT_PM_PCP))&&nat_pmp_map(m,suggested,o->lease_s)) {m->method=NAT_PM_NATPMP;ok=1;}
    }
    if(!ok&&(o->methods&NAT_PM_UPNP)&&nat_upnp_map(m)) {m->method=NAT_PM_UPNP;ok=1;}
    if(ok&&nat_ip_private(m->external.ip)) {
        /* Another NAT sits above this gateway: the mapping cannot be reached from the internet. */
        unsigned method=m->method;nat_portmap_refuse(m,method==NAT_PM_UPNP?"upnp":method==NAT_PM_PCP?"pcp":"natpmp","double_nat");
        if(method==NAT_PM_PCP) nat_pcp_map(m,0,0); else if(method==NAT_PM_NATPMP) nat_pmp_map(m,0,0);
        else {char request[1024],response[2048];int n=nat_upnp_delete_request(request,sizeof(request),&m->control,m->service,m->external.port);
              if(n>0) nat_http_exchange(&m->control,request,(size_t)n,response,sizeof(response),m->opt.timeout_ms);}
        m->method=0;m->external.ip=0;m->external.port=0;ok=0;
    }
    if(ok) {m->acquired_ms=nat_clock_ms();m->refusal[0]=0;}
    nat_net_stop();
    return ok;
}
/* Renew at half the lease (lease 0: permanent, never due). */
static inline int nat_portmap_due(const nat_portmap *m,uint64_t now) {return m->method&&m->lease_s&&now-m->acquired_ms>=(uint64_t)m->lease_s*500u;}
/* 1 renewed (external may change: re-advertise it); 0 the mapping is gone. */
static inline int nat_portmap_renew(nat_portmap *m) {
    if(!m->method) return 0;
    if(!nat_net_start()) return 0;
    int ok=0;unsigned error=0;nat_addr before=m->external;
    if(m->method==NAT_PM_PCP) ok=nat_pcp_map(m,m->external.port,m->opt.lease_s)>0;
    else if(m->method==NAT_PM_NATPMP) ok=nat_pmp_map(m,m->external.port,m->opt.lease_s);
    else ok=!m->lease_s||nat_upnp_add(m,m->external.port,m->lease_s,&error);
    if(ok&&m->external.ip!=before.ip&&nat_ip_private(m->external.ip)) ok=0;
    if(ok) m->acquired_ms=nat_clock_ms(); else {m->method=0;m->external.ip=0;m->external.port=0;}
    nat_net_stop();return ok;
}
/* Best effort, bounded: removes the mapping (lifetime 0 / DeletePortMapping). */
static inline void nat_portmap_release(nat_portmap *m) {
    if(!m->method||!nat_net_start()) return;
    if(m->method==NAT_PM_PCP) nat_pcp_map(m,0,0);
    else if(m->method==NAT_PM_NATPMP) nat_pmp_map(m,0,0);
    else {
        char request[1024],response[2048];
        int n=nat_upnp_delete_request(request,sizeof(request),&m->control,m->service,m->external.port);
        if(n>0) nat_http_exchange(&m->control,request,(size_t)n,response,sizeof(response),m->opt.timeout_ms);
    }
    m->method=0;nat_net_stop();
}
#endif
