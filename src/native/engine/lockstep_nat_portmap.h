/* Port-mapping messages for the direct player links (roadmap B7), sans-IO: PCP MAP
 * (RFC 6887), NAT-PMP (RFC 6886) and the UPnP Internet Gateway Device texts (SSDP
 * search, device description, SOAP AddPortMapping/DeletePortMapping/
 * GetExternalIPAddress). Encoders and strict parsers only; lockstep_nat_net.h sends
 * them. Parsers treat every byte from the network as untrusted and bounded.
 * Original code from the RFCs and the UPnP IGD service specification. */
#ifndef LOCKSTEP_NAT_PORTMAP_H
#define LOCKSTEP_NAT_PORTMAP_H
#include "lockstep_nat.h"

#define NAT_PCP_PORT 5351u
#define NAT_PCP_MAP_SIZE 60u
#define NAT_PMP_MAP_SIZE 12u
enum {NAT_PM_PCP=1u,NAT_PM_NATPMP=2u,NAT_PM_UPNP=4u};

static inline void nat_put16(unsigned char *p,unsigned v) {p[0]=(unsigned char)(v>>8);p[1]=(unsigned char)v;}
static inline void nat_put32(unsigned char *p,uint32_t v) {p[0]=(unsigned char)(v>>24);p[1]=(unsigned char)(v>>16);p[2]=(unsigned char)(v>>8);p[3]=(unsigned char)v;}
static inline unsigned nat_get16(const unsigned char *p) {return (unsigned)p[0]<<8|p[1];}
static inline uint32_t nat_get32(const unsigned char *p) {return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
/* IPv4-mapped IPv6 (::ffff:a.b.c.d); ip 0 gives ::ffff:0.0.0.0, "no preference". */
static inline void nat_put_mapped(unsigned char *p,uint32_t ip) {memset(p,0,10);p[10]=p[11]=0xff;nat_put32(p+12,ip);}
static inline int nat_get_mapped(const unsigned char *p,uint32_t *ip) {
    static const unsigned char prefix[12]={0,0,0,0,0,0,0,0,0,0,0xff,0xff};
    if(memcmp(p,prefix,12)) return 0;
    *ip=nat_get32(p+12);return 1;
}

/* ---- PCP MAP (version 2, opcode 1) for UDP ---- */
static inline void nat_pcp_map_request(unsigned char out[NAT_PCP_MAP_SIZE],uint32_t client_ip,const unsigned char nonce[12],
                                uint16_t internal_port,uint16_t suggested_port,uint32_t lifetime) {
    memset(out,0,NAT_PCP_MAP_SIZE);
    out[0]=2;out[1]=1;nat_put32(out+4,lifetime);nat_put_mapped(out+8,client_ip);
    memcpy(out+24,nonce,12);out[36]=17;nat_put16(out+40,internal_port);nat_put16(out+42,suggested_port);nat_put_mapped(out+44,0);
}
typedef struct {unsigned result;uint32_t lifetime,epoch,external_ip;uint16_t external_port;int version0;} nat_pcp_reply;
/* 1: a MAP response for this nonce and internal port (result may still refuse); 0: ignore it.
 * A NAT-PMP-only gateway answers version 2 with a version 0 "unsupported version". */
static inline int nat_pcp_map_parse(const unsigned char *p,size_t length,const unsigned char nonce[12],uint16_t internal_port,nat_pcp_reply *r) {
    memset(r,0,sizeof(*r));
    if(length>=4&&p[0]==0&&p[1]>=128) {r->version0=1;r->result=nat_get16(p+2);return 1;}
    if(length<NAT_PCP_MAP_SIZE||length>1100||p[0]!=2||p[1]!=0x81) return 0;
    r->result=p[3];r->lifetime=nat_get32(p+4);r->epoch=nat_get32(p+8);
    if(r->result) return 1;   /* errors may echo a shortened payload; the code is what counts */
    if(memcmp(p+24,nonce,12)||p[36]!=17||nat_get16(p+40)!=internal_port) return 0;
    r->external_port=(uint16_t)nat_get16(p+42);
    if(!nat_get_mapped(p+44,&r->external_ip)) return 0;
    return 1;
}
static inline const char *nat_pcp_result_name(unsigned result) {
    static const char *const names[]={"success","unsupported_version","not_authorized","malformed_request","unsupported_opcode",
        "unsupported_option","malformed_option","network_failure","no_resources","unsupported_protocol","user_exceeded_quota",
        "cannot_provide_external","address_mismatch","excessive_remote_peers"};
    return result<sizeof(names)/sizeof(*names)?names[result]:"unknown_result";
}

/* ---- NAT-PMP (version 0) ---- */
static inline void nat_pmp_address_request(unsigned char out[2]) {out[0]=0;out[1]=0;}
static inline void nat_pmp_map_request(unsigned char out[NAT_PMP_MAP_SIZE],uint16_t internal_port,uint16_t suggested_port,uint32_t lifetime) {
    memset(out,0,NAT_PMP_MAP_SIZE);out[1]=1;   /* opcode 1: UDP */
    nat_put16(out+4,internal_port);nat_put16(out+6,suggested_port);nat_put32(out+8,lifetime);
}
typedef struct {unsigned result;uint32_t epoch,lifetime,external_ip;uint16_t internal_port,external_port;} nat_pmp_reply;
static inline int nat_pmp_address_parse(const unsigned char *p,size_t length,nat_pmp_reply *r) {
    memset(r,0,sizeof(*r));
    if(length<4||p[0]!=0||p[1]!=128) return 0;
    r->result=nat_get16(p+2);
    if(r->result) return 1;
    if(length<12) return 0;
    r->epoch=nat_get32(p+4);r->external_ip=nat_get32(p+8);return 1;
}
static inline int nat_pmp_map_parse(const unsigned char *p,size_t length,uint16_t internal_port,nat_pmp_reply *r) {
    memset(r,0,sizeof(*r));
    if(length<4||p[0]!=0||p[1]!=129) return 0;
    r->result=nat_get16(p+2);
    if(r->result) return 1;
    if(length<16||nat_get16(p+8)!=internal_port) return 0;
    r->epoch=nat_get32(p+4);r->internal_port=(uint16_t)nat_get16(p+8);r->external_port=(uint16_t)nat_get16(p+10);r->lifetime=nat_get32(p+12);
    return 1;
}

/* ---- UPnP IGD ---- */
static inline int nat_ssdp_search(char *out,size_t capacity,unsigned version) {
    return snprintf(out,capacity,"M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\n"
                    "ST: urn:schemas-upnp-org:device:InternetGatewayDevice:%u\r\n\r\n",version);
}
/* Case-insensitive header lookup in an HTTP-like text block; value trimmed into out. */
static inline int nat_http_header(const char *text,size_t length,const char *name,char *out,size_t capacity) {
    size_t n=strlen(name);
    for(size_t i=0;i+n+1<length;) {
        size_t end=i;while(end<length&&text[end]!='\n') end++;
        int match=end-i>n&&text[i+n]==':';
        for(size_t j=0;match&&j<n;j++) {char a=text[i+j],b=name[j];if(a>='a'&&a<='z') a=(char)(a-32);if(b>='a'&&b<='z') b=(char)(b-32);match=a==b;}
        if(match) {
            size_t v=i+n+1;while(v<end&&(text[v]==' '||text[v]=='\t')) v++;
            size_t e=end;while(e>v&&(text[e-1]=='\r'||text[e-1]==' '||text[e-1]=='\t')) e--;
            if(e-v>=capacity) return 0;
            memcpy(out,text+v,e-v);out[e-v]=0;return 1;
        }
        i=end+1;
    }
    return 0;
}
typedef struct {uint32_t ip;unsigned port;char path[200];} nat_url;
/* http://a.b.c.d[:port]/path only: a gateway names itself by its LAN address. */
static inline int nat_url_parse(const char *text,nat_url *u) {
    memset(u,0,sizeof(*u));
    if(strncmp(text,"http://",7)) return 0;
    const char *host=text+7,*slash=strchr(host,'/');
    size_t hn=slash?(size_t)(slash-host):strlen(host);char hostport[32];
    if(!hn||hn>=sizeof(hostport)) return 0;
    memcpy(hostport,host,hn);hostport[hn]=0;
    if(!strchr(hostport,':')) {if(hn+3>=sizeof(hostport)) return 0;strcat(hostport,":80");}
    nat_addr a;if(!nat_addr_parse(hostport,&a)) return 0;
    const char *path=slash?slash:"/";
    if(strlen(path)>=sizeof(u->path)) return 0;
    for(const char *c=path;*c;c++) if((unsigned char)*c<=32||*c=='"'||(unsigned char)*c>=127) return 0;
    u->ip=a.ip;u->port=a.port;strcpy(u->path,path);return 1;
}
/* Text between <tag> and </tag> after start, within [start,limit). */
static inline const char *nat_xml_text(const char *start,const char *limit,const char *tag,char *out,size_t capacity) {
    char open[48],close[48];snprintf(open,sizeof(open),"<%s>",tag);snprintf(close,sizeof(close),"</%s>",tag);
    size_t on=strlen(open);
    for(const char *p=start;p+on<=limit;p++) {
        if(memcmp(p,open,on)) continue;
        const char *v=p+on,*e=v;
        while(e+strlen(close)<=limit&&memcmp(e,close,strlen(close))) e++;
        if(e+strlen(close)>limit) return NULL;
        while(v<e&&(*v==' '||*v=='\r'||*v=='\n'||*v=='\t')) v++;
        const char *t=e;while(t>v&&(t[-1]==' '||t[-1]=='\r'||t[-1]=='\n'||t[-1]=='\t')) t--;
        if((size_t)(t-v)>=capacity) return NULL;
        memcpy(out,v,(size_t)(t-v));out[t-v]=0;return e+strlen(close);
    }
    return NULL;
}
/* The WAN connection service of a device description: WANIPConnection:2, :1, then
 * WANPPPConnection:1. Its control URL is resolved against the description's URL. */
static inline int nat_igd_parse(const char *xml,size_t length,const nat_url *location,char *service,size_t service_capacity,nat_url *control) {
    static const char *const wanted[]={"urn:schemas-upnp-org:service:WANIPConnection:2","urn:schemas-upnp-org:service:WANIPConnection:1",
                                       "urn:schemas-upnp-org:service:WANPPPConnection:1"};
    const char *end=xml+length;
    for(unsigned w=0;w<3;w++) {
        for(const char *p=xml;p<end;) {
            const char *s=NULL;
            for(const char *q=p;q+9<=end;q++) if(!memcmp(q,"<service>",9)) {s=q;break;}
            if(!s) break;
            const char *e=NULL;
            for(const char *q=s;q+10<=end;q++) if(!memcmp(q,"</service>",10)) {e=q;break;}
            if(!e) break;
            char type[96],url[200];
            if(nat_xml_text(s,e,"serviceType",type,sizeof(type))&&!strcmp(type,wanted[w])&&nat_xml_text(s,e,"controlURL",url,sizeof(url))) {
                if(strlen(type)>=service_capacity) return 0;
                strcpy(service,type);
                if(!strncmp(url,"http://",7)) return nat_url_parse(url,control)&&control->ip==location->ip;
                *control=*location;
                if(url[0]!='/') {if(strlen(url)+1>=sizeof(control->path)) return 0;control->path[0]='/';strcpy(control->path+1,url);}
                else {if(strlen(url)>=sizeof(control->path)) return 0;strcpy(control->path,url);}
                for(const char *c=control->path;*c;c++) if((unsigned char)*c<=32||*c=='"'||(unsigned char)*c>=127) return 0;
                return 1;
            }
            p=e+10;
        }
    }
    return 0;
}
/* A complete HTTP POST of one SOAP action; body arguments are pre-escaped digits/addresses. */
static inline int nat_soap_request(char *out,size_t capacity,const nat_url *control,const char *service,const char *action,const char *arguments) {
    char body[1400];
    int bn=snprintf(body,sizeof(body),"<?xml version=\"1.0\"?>\r\n<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:%s xmlns:u=\"%s\">%s</u:%s></s:Body></s:Envelope>\r\n",
        action,service,arguments,action);
    if(bn<0||(size_t)bn>=sizeof(body)) return -1;
    char host[24];nat_addr_format((nat_addr){control->ip,(uint16_t)control->port},host,sizeof(host));
    int n=snprintf(out,capacity,"POST %s HTTP/1.1\r\nHost: %s\r\nContent-Type: text/xml; charset=\"utf-8\"\r\n"
        "SOAPAction: \"%s#%s\"\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",control->path,host,service,action,bn,body);
    return n<0||(size_t)n>=capacity?-1:n;
}
static inline int nat_upnp_add_request(char *out,size_t capacity,const nat_url *control,const char *service,uint16_t external_port,
                                uint16_t internal_port,uint32_t internal_ip,uint32_t lease) {
    char args[600],client[24];nat_addr_format((nat_addr){internal_ip,1},client,sizeof(client));*strchr(client,':')=0;
    snprintf(args,sizeof(args),"<NewRemoteHost></NewRemoteHost><NewExternalPort>%u</NewExternalPort><NewProtocol>UDP</NewProtocol>"
        "<NewInternalPort>%u</NewInternalPort><NewInternalClient>%s</NewInternalClient><NewEnabled>1</NewEnabled>"
        "<NewPortMappingDescription>X4 Multiplayer direct link</NewPortMappingDescription><NewLeaseDuration>%lu</NewLeaseDuration>",
        (unsigned)external_port,(unsigned)internal_port,client,(unsigned long)lease);
    return nat_soap_request(out,capacity,control,service,"AddPortMapping",args);
}
static inline int nat_upnp_delete_request(char *out,size_t capacity,const nat_url *control,const char *service,uint16_t external_port) {
    char args[200];
    snprintf(args,sizeof(args),"<NewRemoteHost></NewRemoteHost><NewExternalPort>%u</NewExternalPort><NewProtocol>UDP</NewProtocol>",(unsigned)external_port);
    return nat_soap_request(out,capacity,control,service,"DeletePortMapping",args);
}
static inline int nat_upnp_address_request(char *out,size_t capacity,const nat_url *control,const char *service) {
    return nat_soap_request(out,capacity,control,service,"GetExternalIPAddress","");
}
/* HTTP status of a response, its body (after the blank line) and the UPnP errorCode (0: none). */
static inline int nat_http_status(const char *text,size_t length,const char **body,size_t *body_length) {
    if(length<12||(strncmp(text,"HTTP/1.1 ",9)&&strncmp(text,"HTTP/1.0 ",9))) return 0;
    int status=0;for(unsigned i=9;i<12;i++) {if(text[i]<'0'||text[i]>'9') return 0;status=status*10+(text[i]-'0');}
    *body=NULL;*body_length=0;
    for(size_t i=0;i+3<length;i++) if(!memcmp(text+i,"\r\n\r\n",4)) {*body=text+i+4;*body_length=length-i-4;break;}
    return *body?status:0;
}
/* Decodes "Transfer-Encoding: chunked" in place; returns the new length or -1. */
static inline long nat_http_unchunk(char *body,size_t length) {
    size_t in=0,out=0;
    for(;;) {
        size_t size=0,digits=0;
        while(in<length&&body[in]!='\r'&&body[in]!=';') {
            char c=body[in++];unsigned v=c>='0'&&c<='9'?(unsigned)(c-'0'):c>='a'&&c<='f'?(unsigned)(c-'a'+10):c>='A'&&c<='F'?(unsigned)(c-'A'+10):16u;
            if(v>15||++digits>6) return -1;
            size=size*16+v;
        }
        while(in<length&&body[in]!='\n') in++;
        if(in>=length||!digits) return -1;
        in++;
        if(!size) return (long)out;
        if(in+size+2>length) return -1;
        memmove(body+out,body+in,size);out+=size;in+=size+2;
    }
}
static inline unsigned nat_upnp_error(const char *body,size_t length) {
    char code[8];uint64_t v;
    if(!nat_xml_text(body,body+length,"errorCode",code,sizeof(code))||!nat_uint(code,9999,&v)) return 0;
    return (unsigned)v;
}
static inline int nat_upnp_external_ip(const char *body,size_t length,uint32_t *ip) {
    char text[24];nat_addr a;
    if(!nat_xml_text(body,body+length,"NewExternalIPAddress",text,sizeof(text))||strlen(text)+3>=sizeof(text)) return 0;
    strcat(text,":1");
    if(!nat_addr_parse(text,&a)) return 0;
    *ip=a.ip;return 1;
}
#endif
