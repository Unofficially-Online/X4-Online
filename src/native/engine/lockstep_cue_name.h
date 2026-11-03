/* The static name of an MD cue, from the cue's own fields: one implementation for the hot join's static cue key
 * (lockstep_join.h), the script RNG's cue identity (lockstep_cue_identity.h: SCRIPT_RNG, md_resume_rng, md_timed_rng,
 * guidance_local) and the NPC traces (lockstep_npc_action.h and its users), on both platforms.
 *
 * A static cue's name is its path "<script>.<root>.<...>.<cue>": the name of its MD script and the own name of every
 * static cue from its root (parent 0) down to it. The fields, as the pinned code writes them (Windows CUE_PATH_PIN_*:
 * the XML constructor 0x1111170, a library user's sub-cue copy 0x1111dd0, the MD import 0x11121d0 and its second pass
 * 0x1118d70, the script constructors 0x110dff0 and 0x110e180; Linux gate_sites.h cue_path_*, the same fields):
 *   cue +0xc8   its own name, a std::string (a library copy: its library cue's). The loader refuses an unnamed and a
 *               duplicate cue, so roots and siblings have distinct names and the path names every static cue.
 *   cue +0x150  its MD script (a copy: its parent's); the script's name is the std::string at +0x18
 *   cue +0x158  its parent (0: a root; also a <library> nested in a cue's <cues>)
 *   cue +0x160  its static cue (a static cue: itself)
 *   cue +0x190  its sub-cues by slot, a vector of pointers (only for a nameless cue below a root, which the loader does
 *               not make: "#<slot>")
 * It replaces the walk of the script's name map (+0x60, "name" -> Cue*, a library copy entered as "<user>.<sub>"): in
 * key order with a budget of 8,192 steps it found only the first ~2,700 keys, the large story scripts hold 2,900-4,400,
 * so Start/Setup/... of six Story_* scripts read as unnamed and their draws stayed unkeyed (reports/LOCKSTEP-HOTJOIN-
 * 20261101.txt 1.2, LOCKSTEP-CENSUS-20261028.txt). A root's name is the same as the map's; a cue below a root now
 * carries its ancestors, which also tells a library copy from its library cue and from another user's copy.
 *
 * Two forms: the key, FNV-1a 64 of the raw text (the hot join's static key), and the text with each name percent-
 * encoded (npc_encode's alphabet; '.' separates the levels) for the modules, whose keys hash that text. Neither holds an
 * address: every client reads the names its loader built. A level that is not as expected is refused, loudly: the check
 * and its fields (cue_path_text; the join's refusal detail, the modules' cue_refused= and cue_refusal= in SCRIPT_RNG
 * and MD_RESUME_RNG status), never a guess and never the map.
 *
 * Every read goes through read: read_local (a system call per read) for the trace modules, a caller's cheaper guarded
 * copy on a per-entry hot path, the join's own reader. */
typedef int (*npc_reader)(const void *,void *,size_t);
#define CUE_PATH_NAME 0xc8u           /* a static cue's own name (std::string) */
#define CUE_PATH_SCRIPT 0x150u        /* its MD script */
#define CUE_PATH_PARENT 0x158u        /* its parent, 0 for a root */
#define CUE_PATH_DEF 0x160u           /* its static cue: itself */
#define CUE_PATH_SLOTS 0x190u         /* its sub-cues by slot {begin, end, capacity} */
#define CUE_PATH_SCRIPT_NAME 0x18u    /* the MD script's name (std::string) */
#define CUE_PATH_NAME_MAX 512u        /* a name of 512 bytes or more is refused (static_string) */
#define CUE_PATH_LEVELS 64u           /* more levels than this: a cycle (static_depth) */
#define CUE_PATH_SLOT_MAX 1000000u
#define CUE_PATH_TEXT 1024u           /* the script RNG's text buffer; a longer encoded path is refused (static_long) */
#ifndef CUE_PATH_VT
#define CUE_PATH_VT 0x2c75170u        /* MD::Cue::vftable, image relative (SCRIPT_RNG_CUE_VT, JOIN_CUE_VTABLE) */
#endif
#ifndef NPC_STRING_LIBSTDCXX
/* Windows: the code that writes the fields (rva, size, SHA-256 of X4.exe's bytes). The hot join's file pins hold the same
 * ranges (join_pin_table), the modules check them live (npc_layout_pinned); the layout bytes inside them are checked
 * against the image in test_join_native_image and test_cue_path_native_image. */
#define CUE_PATH_PIN_XML {0x1111170,0x8ca,"27a2dc5c33ef1f48abda7e838b94698be140ef08b3536b1504d78db485f0f3d6"}         /* static cue from XML */
#define CUE_PATH_PIN_COPY {0x1111dd0,0x38a,"f4daaad92a9ee79f0c0e0b2adfccd1aab91b6590717dd6ba89ecf92e1438f07f"}        /* library sub-cue copy */
#define CUE_PATH_PIN_IMPORT {0x11121d0,0x1724,"36b70b49de75b47b5e6f6d13a17131fa48184588abb8a8159074c605e1a5799f"}     /* MD cue import */
#define CUE_PATH_PIN_SECOND {0x1118d70,0x14f0,"ecd69fcb764f7fcd7fa98091707b20ba4e885723dfaa3436ce3cf52da31529aa"}     /* MD import, second pass */
#define CUE_PATH_PIN_SCRIPT_XML {0x110dff0,0x183,"9a09124c464ec585596f69c29ff3c001f7ee3d21f73a21b19fd0d073759e1388"}  /* MD script from XML */
#define CUE_PATH_PIN_SCRIPT_SAVE {0x110e180,0x6af,"5b3ef5a387eb8d87e21466b9748786274530c56a7a44c3e1ce180785f3da4e60"} /* MD script from a save */
#define CUE_PATH_PINS CUE_PATH_PIN_XML,CUE_PATH_PIN_COPY,CUE_PATH_PIN_IMPORT,CUE_PATH_PIN_SECOND,CUE_PATH_PIN_SCRIPT_XML,CUE_PATH_PIN_SCRIPT_SAVE
#endif
#ifdef NPC_STRING_LIBSTDCXX
static int npc_string_with(npc_reader read,uintptr_t address,char *out,size_t capacity) {
    uintptr_t data=0;uint64_t size=0; /* libstdc++: pointer, size */
    if(!read((void*)address,&data,8) || !read((void*)(address+8),&size,8)) return 0;
    if(!size || size>=capacity || size>1048576 || !data || !read((void*)data,out,(size_t)size)) return 0;
    for(size_t i=0;i<size;i++) if(!out[i]) return 0;
    out[size]=0;return 1;
}
/* A std::string's text (libstdc++: pointer +0, size +8): its length, 0 empty, -1 unreadable or not shorter than the
 * buffer; *size the size its header holds. */
static int cue_string_with(npc_reader read,uintptr_t at,char *out,size_t capacity,uint64_t *size) {
    uintptr_t data=0;uint64_t n=0;*size=0;out[0]=0;
    if(!read((void*)at,&data,8) || !read((void*)(at+8),&n,8)) return -1;
    *size=n;
    if(!n) return 0;
    if(n>=capacity || !data || !read((void*)data,out,(size_t)n)) return -1;
    out[n]=0;return (int)n;
}
#else
static int npc_string_with(npc_reader read,uintptr_t address,char *out,size_t capacity) {
    unsigned char str[32];uintptr_t data;uint64_t size,cap;
    if(!read((void*)address,str,32)) return 0;
    memcpy(&size,str+16,8);memcpy(&cap,str+24,8);
    if(!size || size>=capacity || cap<size || cap>1048576) return 0;
    if(cap<16) data=address;else memcpy(&data,str,8);
    if(!data || !read((void*)data,out,(size_t)size)) return 0;
    for(size_t i=0;i<size;i++) if(!out[i]) return 0;
    out[size]=0;return 1;
}
/* A std::string's text (MSVC: inline while the capacity +0x18 is below 16, else behind the pointer at +0; size +0x10):
 * its length, 0 empty, -1 unreadable or not shorter than the buffer; *size the size its header holds. */
static int cue_string_with(npc_reader read,uintptr_t at,char *out,size_t capacity,uint64_t *size) {
    unsigned char s[32];uint64_t cap=0;uintptr_t data=at;*size=0;out[0]=0;
    if(!read((void*)at,s,32)) return -1;
    memcpy(size,s+16,8);memcpy(&cap,s+24,8);
    if(!*size) return 0;
    if(*size>=capacity || cap<*size) return -1;
    if(cap>=16) memcpy(&data,s,8);
    if(!data || !read((void*)data,out,(size_t)*size)) return -1;
    out[*size]=0;return (int)*size;
}
#endif
/* raw[0..n) percent-encoded into out (letters, digits, '_', '.', '-' as they are): the length, (size_t)-1 when out is too
 * small. */
static size_t cue_encode(const char *raw,size_t n,char *out,size_t capacity) {
    static const char hex[]="0123456789abcdef";size_t used=0;
    for(size_t i=0;i<n;i++) {
        unsigned char c=(unsigned char)raw[i];
        if((c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='_' || c=='.' || c=='-') {
            if(used+1>=capacity) return (size_t)-1;out[used++]=(char)c;
        } else {
            if(used+3>=capacity) return (size_t)-1;out[used++]='%';out[used++]=hex[c>>4];out[used++]=hex[c&15];
        }
    }
    if(used>=capacity) return (size_t)-1;
    out[used]=0;return used;
}
__attribute__((unused)) static int npc_encode(const char *raw,char *out,size_t capacity) {return cue_encode(raw,strlen(raw),out,capacity)!=(size_t)-1;}

enum {CUE_PATH_OK,CUE_PATH_BAD_SCRIPT,CUE_PATH_BAD_LEVEL,CUE_PATH_TOO_DEEP,CUE_PATH_BAD_STRING,CUE_PATH_NAMELESS_ROOT,CUE_PATH_BAD_SLOT,
      CUE_PATH_TOO_LONG};
/* The path of static cue cue, or the check it was refused with and the fields that check read. */
typedef struct {
    uint64_t key;size_t length;            /* FNV-1a 64 of the raw text; the encoded text's length */
    unsigned check,depth;uintptr_t cue,script,at,parent,vtable,def,own;uint64_t size;uint32_t slots;int why;
} cue_path;
#define CUE_FNV_BASIS 14695981039346656037ULL
static uint64_t cue_fnv(uint64_t h,const char *text,size_t n) {
    for(size_t i=0;i<n;i++) h=(h^(unsigned char)text[i])*1099511628211ULL;
    return h;
}
/* item's position among the slots of the vector {begin, end} at at, every slot counted: -1 absent, -2 unreadable;
 * *count the vector's length (0 when unreadable). */
static int64_t cue_slot_with(npc_reader read,uintptr_t at,uintptr_t item,uint32_t *count) {
    uintptr_t bounds[2]={0,0};*count=0;
    if(!at || !read((void*)at,bounds,16) || bounds[1]<bounds[0] || (bounds[1]-bounds[0])%8 || (bounds[1]-bounds[0])/8>CUE_PATH_SLOT_MAX) return -2;
    const uint32_t n=(uint32_t)((bounds[1]-bounds[0])/8);
    for(uint32_t i=0;i<n;) {
        uintptr_t chunk[64];const uint32_t m=n-i<64u?n-i:64u;
        if(!read((void*)(bounds[0]+(uintptr_t)i*8u),chunk,(size_t)m*8u)) return -2;
        for(uint32_t j=0;j<m;j++) if(chunk[j]==item) {*count=n;return (int64_t)(i+j);}
        i+=m;
    }
    *count=n;return -1;
}
/* "." and name[0..n), encoded, at text+*used (no "." in front of the script). */
static int cue_path_put(char *text,size_t capacity,size_t *used,const char *name,size_t n,int dot) {
    if(dot) {if(*used+1>=capacity) return 0;text[(*used)++]='.';text[*used]=0;}
    size_t put=cue_encode(name,n,text+*used,capacity-*used);
    if(put==(size_t)-1) return 0;
    *used+=put;return 1;
}
/* The path of the static cue s: every level an MD::Cue (vtable, image relative or a test's), a static cue (+0x160 itself)
 * of the same script, up to its root; then the script's name and each level's own name from the root down. text (NULL:
 * the key alone): the encoded text, capacity bytes with its NUL. 1 with p->key (and the text), 0 with p->check and its
 * fields. */
static int cue_path_with(npc_reader read,uintptr_t vtable,uintptr_t s,char *text,size_t capacity,cue_path *p) {
    uintptr_t chain[CUE_PATH_LEVELS],cur=s;unsigned depth=0;char name[CUE_PATH_NAME_MAX];uint64_t size=0;size_t used=0;
    memset(p,0,sizeof(*p));p->cue=s;
    if(text) {if(!capacity) {p->check=CUE_PATH_TOO_LONG;return 0;}text[0]=0;}
    if(!read((void*)(s+CUE_PATH_SCRIPT),&p->script,8) || !p->script) {p->check=CUE_PATH_BAD_SCRIPT;return 0;}
    for(;;) {
        uintptr_t vt=0,def=0,own=0,parent=0;
        if(depth==CUE_PATH_LEVELS) {p->check=CUE_PATH_TOO_DEEP;p->at=cur;p->depth=depth;return 0;}
        if(!read((void*)cur,&vt,8) || vt!=vtable || !read((void*)(cur+CUE_PATH_DEF),&def,8) || def!=cur ||
           !read((void*)(cur+CUE_PATH_SCRIPT),&own,8) || own!=p->script || !read((void*)(cur+CUE_PATH_PARENT),&parent,8)) {
            p->check=CUE_PATH_BAD_LEVEL;p->at=cur;p->depth=depth;p->vtable=vt;p->def=def;p->own=own;return 0;
        }
        chain[depth++]=cur;
        if(!parent) break;
        cur=parent;
    }
    int n=cue_string_with(read,p->script+CUE_PATH_SCRIPT_NAME,name,sizeof(name),&size);
    if(n<=0) {p->check=CUE_PATH_BAD_SCRIPT;p->size=size;return 0;}
    uint64_t h=cue_fnv(CUE_FNV_BASIS,name,(size_t)n);
    if(text && !cue_path_put(text,capacity,&used,name,(size_t)n,0)) {p->check=CUE_PATH_TOO_LONG;p->at=p->script;p->depth=depth;return 0;}
    for(unsigned i=depth;i-->0;) {                                    /* the root first */
        n=cue_string_with(read,chain[i]+CUE_PATH_NAME,name,sizeof(name),&size);
        if(n<0) {p->check=CUE_PATH_BAD_STRING;p->at=chain[i];p->size=size;return 0;}
        if(!n) {
            if(i+1==depth) {p->check=CUE_PATH_NAMELESS_ROOT;p->at=chain[i];p->depth=depth;return 0;}
            uint32_t slots=0;int64_t slot=cue_slot_with(read,chain[i+1]+CUE_PATH_SLOTS,chain[i],&slots);
            if(slot<0) {p->check=CUE_PATH_BAD_SLOT;p->at=chain[i];p->parent=chain[i+1];p->slots=slots;p->why=(int)slot;return 0;}
            n=snprintf(name,sizeof(name),"#%lld",(long long)slot);
        }
        h=cue_fnv(cue_fnv(h,".",1),name,(size_t)n);
        if(text && !cue_path_put(text,capacity,&used,name,(size_t)n,1)) {p->check=CUE_PATH_TOO_LONG;p->at=chain[i];p->depth=depth;return 0;}
    }
    p->key=h;p->length=used;return 1;
}
#define CUE_P(p) ((unsigned long long)(uintptr_t)(p))
/* A refusal as comma-separated name=value pairs (no spaces or '|'): the hot join's check names and fields. */
static int cue_path_text(const cue_path *p,char *out,size_t capacity) {
    switch(p->check) {
    case CUE_PATH_BAD_SCRIPT: return snprintf(out,capacity,"check=static_script,static=%llx,script=%llx,size=%llu",CUE_P(p->cue),CUE_P(p->script),
                                              (unsigned long long)p->size);
    case CUE_PATH_BAD_LEVEL: return snprintf(out,capacity,"check=static_parent,static=%llx,at=%llx,depth=%u,vtable=%llx,def=%llx,script=%llx,"
                                             "expected=%llx",CUE_P(p->cue),CUE_P(p->at),p->depth,CUE_P(p->vtable),CUE_P(p->def),CUE_P(p->own),
                                             CUE_P(p->script));
    case CUE_PATH_TOO_DEEP: return snprintf(out,capacity,"check=static_depth,static=%llx,at=%llx,depth=%u",CUE_P(p->cue),CUE_P(p->at),p->depth);
    case CUE_PATH_BAD_STRING: return snprintf(out,capacity,"check=static_string,static=%llx,at=%llx,size=%llu",CUE_P(p->cue),CUE_P(p->at),
                                              (unsigned long long)p->size);
    case CUE_PATH_NAMELESS_ROOT: return snprintf(out,capacity,"check=static_root,static=%llx,root=%llx,depth=%u",CUE_P(p->cue),CUE_P(p->at),p->depth);
    case CUE_PATH_BAD_SLOT: return snprintf(out,capacity,"check=static_slot,static=%llx,at=%llx,parent=%llx,slots=%u,why=%d",CUE_P(p->cue),
                                            CUE_P(p->at),CUE_P(p->parent),p->slots,p->why);
    case CUE_PATH_TOO_LONG: return snprintf(out,capacity,"check=static_long,static=%llx,at=%llx,depth=%u",CUE_P(p->cue),CUE_P(p->at),p->depth);
    default: return snprintf(out,capacity,"check=none");
    }
}
/* The modules' refusals: counted, the first one's text kept (status cue_refused=, cue_refusal=). A refused cue is
 * unnamed to its module (SCRIPT_RNG unnamed=, md_resume unnamed=, an NPC trace's unreadable name). */
static volatile LONG64 cue_path_refusals;
static char cue_path_refusal[200]="-";
static void cue_path_refused(const cue_path *p) {
    if(InterlockedIncrement64(&cue_path_refusals)!=1) return;
    char text[sizeof(cue_path_refusal)];
    if(cue_path_text(p,text,sizeof(text))>0) memcpy(cue_path_refusal,text,sizeof(text));
}
/* The encoded path of the static cue behind an exec's cue value (tag 0x5d, payload cue+0xb0). Return 1 for a named NPC
 * module cue, 2 for a known different module/context, and 0 for unreadable identity. A missing name must never silently
 * drop a cue: a refused path is counted (cue_path_refused) and unnamed. The NPC traces are Windows': unused on Linux. */
__attribute__((unused)) static int npc_cue_name_with(npc_reader read,void *exec,char out[256],int npc_only) {
    uint32_t tag;uintptr_t value,definition,module;
    if(!read((unsigned char*)exec+0x38,&tag,4)) return 0;
    if(tag!=0x5d) return 2;
    if(!read((unsigned char*)exec+0x40,&value,8) || value<0xb0 ||
       !read((void*)(value-0xb0+CUE_PATH_DEF),&definition,8) || !definition ||
       !read((void*)(definition+CUE_PATH_SCRIPT),&module,8) || !module) return 0;
    char module_name[96];if(!npc_string_with(read,module+CUE_PATH_SCRIPT_NAME,module_name,sizeof(module_name))) return 0;
    int cache=!strcmp(module_name,"NPC_UseCases");
    if(npc_only && !cache) return 2;
    unsigned start=(unsigned)((definition>>4)%NPC_NAME_LIMIT),slot=start;
    for(unsigned count=0;cache && count<NPC_NAME_LIMIT;count++,slot=(slot+1)%NPC_NAME_LIMIT) {
        if(npc_names[slot].definition==definition) {strcpy(out,npc_names[slot].cue);return 1;}
        if(!npc_names[slot].definition) break;
        if(count+1==NPC_NAME_LIMIT) return 0;
    }
    cue_path p;char text[256];
    if(!cue_path_with(read,(uintptr_t)(image+CUE_PATH_VT),definition,text,sizeof(text),&p)) {cue_path_refused(&p);return 0;}
    strcpy(out,text);
    if(cache) {npc_names[slot].definition=definition;strcpy(npc_names[slot].cue,text);}
    return 1;
}
