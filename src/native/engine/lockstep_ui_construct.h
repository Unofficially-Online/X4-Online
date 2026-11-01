/* Station construction for the 'build' family (lockstep_ui_build.h; DEFAULT OFF with it, stop reason 164). Report:
 * reports/LOCKSTEP-UI-CONSTRUCT-20261101.txt. Static reading of the pinned X4.exe 9.00; RVAs and layouts only, own code.
 *
 *   plan      the station configuration's confirm (SetConstructionSequenceFromConstructionMap(container, holomap)) is a
 *             claim of the family (spec "IQ"). The plan the person edited lives in this client's construction map (the
 *             holomap's behaviour +0x2c0, its sequence +0x300): at the capture its whole sequence becomes one record in a
 *             portable form: per entry the construction ID (0 for an entry the station's planned sequence does not hold:
 *             every client draws it at the apply), the macro and connection names, the predecessor's index and
 *             connection name, the 64-byte transform as exact bits, the flags and the module loadout (X4's own conversion
 *             to a UILoadout, macro based); plus the station's planned sequence's fingerprint and the plot (size, centre
 *             bits) as the base the plan was made against. The menu's local edits stay local; nothing runs here.
 *   apply     every client, the batch's apply (capture tick + delay, slot order): the base and the plot must still match
 *             (else skipped plan_changed / plot_changed, the same everywhere), the station must be the slot faction's
 *             (coop: the player faction; factions: the slot's own, inside the trade family's faction scope), the names
 *             resolve through X4's macro database and the macro definition's connection table; then X4's own commit
 *             path as the stock function walks it (own code mirroring 0x1dc030): the container's build context and its
 *             build parent, U::BuildTask's constructor on a sequence built from the record (X4's sequence copy makes the
 *             real entries, X4's converter makes each loadout), X4's own validity test and delete. The answer is the
 *             station's planned sequence's fingerprint after the commit (0 when X4 made no valid task): it joins the
 *             batch hash, so a plan that took on one client only is a digest mismatch at that tick.
 *   counter   [F] construction entry IDs come from one global counter (0x38f4dd0, under the lock 0x3d6b940); the
 *             construction map draws from it while a person edits, on that client only. While the family is armed the
 *             counter is canonical at the edges of this client's UI work: kept at a UI move's start and at the end of
 *             the probe's frame (applies), put back at the probe frame's start and at the UI move's end. Its value joins
 *             the family's receipt (:C<hex>), so a draw this does not catch is a digest mismatch, never a silent split.
 * Fail closed: the pins below (every function called or mirrored, the layouts read), a holomap that is no construction
 * map, a name that does not resolve back to the same object at the capture, a plan over the limits: refused at the
 * capture (nothing ran anywhere); a fault at the apply: stop 164 (ui_fault). */
#define UICONS_PLAN_VERSION 1u
#define UICONS_ENTRIES_MAX 200u
#define UICONS_NONE 0xffffu
#define UICONS_BASE_NONE 0x4e4f504c414e3031ULL   /* "NOPLAN01": the station has no planned sequence */
#define UICONS_FNV 0x55494355434f4e53ULL         /* "UICUCONS" */
/* Layouts [F]. */
#define UICONS_HOLOMAP_OF 0x1beb30u        /* holomap ID -> U::HoloMap (class 0x3a) or NULL */
#define UICONS_BEHAVIOR 0x2c0u             /* U::HoloMap: its behaviour; vfunc +0x18 (kind 2) = a construction map */
#define UICONS_BEHAVIOR_KIND 3u
#define UICONS_BEHAVIOR_SEQUENCE 0x300u    /* ConstructionMapBehavior: the sequence being edited */
#define UICONS_MACRO_LOOKUP 0xa2f0d0u      /* MacroDB find: (db, {name, length}*, 0) -> U::Macro or NULL */
#define UICONS_MACRO_DB 0x6cf1628u
#define UICONS_MACRO_NAME 0x20u            /* U::Macro: std::string */
#define UICONS_MACRO_DEF 0x48u             /* U::Macro: its component definition */
#define UICONS_DEF_CONNECTIONS 0x170u      /* definition: connections begin, end; sorted by the name's hash at +8 */
#define UICONS_CONNECTION_SIZE 0x160u
#define UICONS_CONNECTION_HASH 8u
#define UICONS_CONNECTION_NAME 0x10u       /* std::string */
#define UICONS_ENTRY_SIZE 0x210u           /* a sequence entry: ID, macro, connection, predecessor, its connection, transform, */
#define UICONS_E_ID 0x0u                   /* the upgrade plan, three flags */
#define UICONS_E_MACRO 0x8u
#define UICONS_E_CONNECTION 0x10u
#define UICONS_E_PRED 0x18u
#define UICONS_E_PRED_CONNECTION 0x20u
#define UICONS_E_TRANSFORM 0x30u           /* 64 bytes: position (x, y, z, w) and three rotation rows */
#define UICONS_E_UPGRADE 0x70u
#define UICONS_E_FIXED 0x208u
#define UICONS_E_FLAG 0x209u
#define UICONS_E_BOOKMARK 0x20au
#define UICONS_SEQUENCE_SIZE 0x38u         /* entries vector, bookmark index vector (+0x18), current bookmark (+0x30), flag (+0x34) */
#define UICONS_PLANNED_SLOT 576u           /* Container vfunc +0x1200: the planned sequence or NULL (0x3f48a0) */
#define UICONS_IS_CLASS_SLOT 572u          /* +0x11e0 */
#define UICONS_IS_DERIVED_SLOT 573u        /* +0x11e8 */
#define UICONS_CLASS_CONTAINER 0x62u
#define UICONS_CLASS_BUILD_PARENT 0x6fu
#define UICONS_PARENT 0x70u
#define UICONS_REFS 0x40u                  /* XLib reference count (+0x40), state (+0x44), its interface at +0x38 */
#define UICONS_BUILD_CONTEXT 0x3f76f0u
#define UICONS_ALLOCATE 0x14b7140u
#define UICONS_BUILD_TASK 0x3aa1d0u        /* U::BuildTask's constructor */
#define UICONS_BUILD_TASK_SIZE 0x390u
#define UICONS_UPGRADE_EMPTY 0xe2acd0u
#define UICONS_UPGRADE_INIT 0xf16d0u
#define UICONS_UPGRADE_FREE 0xf03c0u
#define UICONS_UPGRADE_FROM 0x16ede0u
#define UICONS_LOADOUT 0x1cb770u           /* GetConstructionMapItemLoadout2 */
#define UICONS_LOADOUT_COUNTS 0x1cba20u    /* GetConstructionMapItemLoadoutCounts2 */
#define UICONS_STATION_VTABLE 0x2b764d8u
#define UICONS_PLANNED_FN 0x3f48a0u
#define UICONS_OWNER_SLOT 708u            /* U::Object::GetTrueOwner (+0x1620): the owner faction (the trade family's reading) */
#define UICONS_OWNER_FN 0x352b10u
#define UICONS_CID_COUNTER 0x38f4dd0u
#define UICONS_CID_LOCK 0x3d6b940u
enum {UICONS_F_FIXED=1,UICONS_F_FLAG=2,UICONS_F_BOOKMARK=4,UICONS_F_LOADOUT=8};
static const struct {unsigned rva,size;const char *sha;} uicons_pin_table[]={
    {0x1dc030u,0x26b,"03becb1ab7bb6df4a86d09b5a46a0ca9cdd3a816ee2727887fe1a33100e81934"},  /* the stock commit, mirrored in uicons_commit */
    {UICONS_BUILD_TASK,0x200,"381d0a719276870253cb9df24b411c9111dd99d4bb30634707c40e5d59cacca2"},
    {0xd57a50u,0x200,"19da3c95d42dfb01ac2bde2ee31acc66d7f5b9da15c185dbe7382279d064d50f"},   /* the sequence copy: the source fields it reads */
    {0xd57880u,0xd8,"8ceb30b115cfd1c365e91e59090aea8058910aac332d277df81e7c78f9921e18"},    /* the entry: layout, the ID counter and its lock */
    {0xd57fb0u,0xcc,"c094fbbc5b436f360167d71bdf7b345bff013cbcd7202170116523b3c0023ba1"},    /* the entry count */
    {UICONS_UPGRADE_EMPTY,0xb8,"19b19027479c7de3295f1939092fe4988968f93bf6a0e308b29471b011b17d3d"},
    {UICONS_UPGRADE_INIT,0x138,"28fad574d19c1094cd57257d8fe8f797ca1d899bfd57d2d9c7da695e69e30341"},
    {UICONS_UPGRADE_FREE,0xc6,"b7c21392072df6a2e7adea762f61be58461219f615b1991f977ae3dfe3b432c6"},
    {UICONS_UPGRADE_FROM,0x1b7,"a5100f6a2ef1c0819fd7e2e7c817408d1d691f00d1206f6cb49207f2935ddaf0"},
    {UICONS_MACRO_LOOKUP,0x200,"1429334fbb7608289771aab3cc39d865c4db0580738c170af0a96479beeccb0c"},
    {0x22c580u,0xf2,"1f6f6f4eebb56450666e0ce60a132a8e8e07d4ccb4b9fec212c23aed5ad25913"},    /* AddMacroToConstructionMap: the database and the view */
    {UICONS_HOLOMAP_OF,0x4f,"b5bdd5b4ec701d4a01390c16084468cbcbaaa46fe4b94a466e18bb198aae668a"},
    {UICONS_BUILD_CONTEXT,0x7d,"fa9bc4d531cb476e99ec1d2629b43b2d42fe65c49e0e4827e396a49affd94783"},
    {UICONS_ALLOCATE,0x40,"c4f2b09fc3f68e4e1d7c8b4e8abe7bfea677b0331eb1a26fb413a91abe45283d"},
    {UICONS_PLANNED_FN,0x9b,"b360a0ecd9de255eb86ef29b024771f02dd28929ff893dbfff7538f397ce116f"},
    {0x1c8cc3u,0x2ac,"6b5b8a07f8c29034628b079344094a394bea9732d6479fbf57bf3a13469cb333"},   /* GetBuildMapConstructionPlan2's entry reads */
    {0xe652cfu,0xc6,"86238d333629cc9278632e34b46419fb6137c7ede15516415925a28ada6fc5ec"},    /* the plan import's connection lookup */
    {UICONS_LOADOUT,0x194,"6f20d58e86193688adab765d3d6c105482f1843efcd40fe337f7e813a0743f00"},
    {UICONS_LOADOUT_COUNTS,0x104,"a8456c606f100f4c96105106b5662b9343e361b272acc064f25ac0de123a2ab9"},
    {0x1c3ef0u,0x149,"2c43f500796229d196896ffc979d1183e45ddb5c9910dd0589f673b599856ec9"},   /* CompareMapConstructionSequenceWithPlanned */
    {UICONS_OWNER_FN,0x8,"7900022f876803a52ef0ebfca5f0f9ae79603c127b6ad4a32bc834758e27be53"},      /* GetTrueOwner: +0x348 (the station's owner check) */
};
#define UICONS_PINS (sizeof(uicons_pin_table)/sizeof(*uicons_pin_table))
/* The stock UI's UILoadout (version 1: GetConstructionMapItemLoadout2's and the converter's), its counts. */
typedef struct {const char *macro,*type;size_t slot;unsigned char optional;} uicons_macro1;
typedef struct {const char *macro,*path,*group;uint32_t count;unsigned char optional;} uicons_group1;
typedef struct {
    uicons_macro1 *weapons;uint32_t nweapons;uicons_macro1 *turrets;uint32_t nturrets;uicons_macro1 *shields;uint32_t nshields;
    uicons_macro1 *engines;uint32_t nengines;uicons_group1 *turretgroups;uint32_t nturretgroups;uicons_group1 *shieldgroups;uint32_t nshieldgroups;
    uibuild_ammo *ammo;uint32_t nammo;uibuild_ammo *units;uint32_t nunits;uibuild_software *software;uint32_t nsoftware;uibuild_vmacro thruster;
} uicons_loadout1;
typedef struct {uint32_t nweapons,nturrets,nshields,nengines,nturretgroups,nshieldgroups,nammo,nunits,nsoftware;} uicons_counts1;
_Static_assert(sizeof(uicons_macro1)==32 && sizeof(uicons_group1)==32 && sizeof(uicons_loadout1)==160 && __builtin_offsetof(uicons_loadout1,thruster)==144,"UILoadout");
_Static_assert(sizeof(uicons_counts1)==36,"UILoadoutCounts");
/* X4's functions (tests: stand-ins); virtual calls go through the objects' own tables. */
typedef struct {
    void *(*holomap)(uint64_t);
    void *(*macro)(void *db,const void *view,uint64_t zero);
    void (*counts)(uicons_counts1*,uint64_t holomap,size_t idx,uint64_t defensible,uint64_t module);
    void (*loadout)(uicons_loadout1*,uint64_t holomap,size_t idx,uint64_t defensible,uint64_t module);
    unsigned char (*upgrade_empty)(const void*);
    void *(*upgrade_init)(void*);
    void (*upgrade_free)(void*);
    void (*upgrade_from)(void *plan,void *macro,const uicons_loadout1*);
    void **(*build_context)(void *container,void **out);
    void *(*allocate)(size_t,uint64_t,uint64_t,uint64_t,uint32_t alignment);
    void *(*build_task)(void *task,void **container,void **parent,void *faction,void *sequence,uint32_t bookmark,uint64_t zero);
} uicons_fns;
static uicons_fns uicons_fn;
typedef struct {
    uint64_t id;const char *macro,*conn,*pred_conn;unsigned pred;uint32_t transform[16];unsigned char flags;uicons_loadout1 *loadout;
    void *macro_ptr,*conn_ptr,*pred_conn_ptr;
} uicons_entry;
typedef struct {uint64_t base;uint32_t plot[6],bookmark;unsigned char seq_flag;unsigned count;uicons_entry e[UICONS_ENTRIES_MAX];} uicons_plan;

/* ---- reads ---- */
static void *uicons_ptr(const void *at) {void *p=NULL;if(!at || !read_local(at,&p,8)) return NULL;return p;}
/* An MSVC std::string (buffer or pointer, size +0x10, capacity +0x18): ASCII, at most 255 bytes. */
static int uicons_std_string(const unsigned char *at,char *out,unsigned *length) {
    uint64_t size=0,cap=0;const char *data=(const char*)at;
    if(!at || !read_local(at+0x10,&size,8) || !read_local(at+0x18,&cap,8) || size>UIBUILD_STRING_MAX) return 0;
    if(cap>=16 && !read_local(at,&data,8)) return 0;
    if(size && (!data || !read_local(data,out,(size_t)size))) return 0;
    out[size]=0;
    for(uint64_t i=0;i<size;i++) if(!out[i] || (unsigned char)out[i]>=0x80) return 0;
    *length=(unsigned)size;return 1;
}
static int uicons_macro_name(void *macro,char *out,unsigned *length) {return macro && uicons_std_string((unsigned char*)macro+UICONS_MACRO_NAME,out,length);}
static int uicons_connection_name(void *conn,char *out,unsigned *length) {return conn && uicons_std_string((unsigned char*)conn+UICONS_CONNECTION_NAME,out,length);}
/* X4's macro database by name. */
static void *uicons_macro(const char *name,unsigned length) {
    struct {const char *data;uint64_t length;} view={name,length};
    void *db=uicons_ptr(pnat_base+UICONS_MACRO_DB);
    return db && length && uicons_fn.macro?uicons_fn.macro(db,&view,0):NULL;
}
/* The macro definition's connection by name, as X4's plan import finds it: the lower-case name's hash (64-bit, basis
 * 0x811c9dc5, multiply by 0x1000193 then xor), a lower bound over the table sorted by that hash, an equal hash. */
static uint64_t uicons_name_hash(const char *name,unsigned length) {
    uint64_t h=0x811c9dc5ULL;
    for(unsigned i=0;i<length;i++) {unsigned c=(unsigned char)name[i];if(c>='A' && c<='Z') c+=32;h*=0x1000193ULL;h^=(uint64_t)c;}
    return h;
}
static void *uicons_connection(void *macro,const char *name,unsigned length) {
    unsigned char *def=macro?uicons_ptr((unsigned char*)macro+UICONS_MACRO_DEF):NULL;
    if(!def || !length) return NULL;
    uint64_t h=uicons_name_hash(name,length);
    if(h==0x811c9dc5ULL) return NULL;
    unsigned char *begin=uicons_ptr(def+UICONS_DEF_CONNECTIONS),*end=uicons_ptr(def+UICONS_DEF_CONNECTIONS+8);
    if(!begin || end<begin || (size_t)(end-begin)%UICONS_CONNECTION_SIZE) return NULL;
    size_t lo=0,hi=(size_t)(end-begin)/UICONS_CONNECTION_SIZE,n=hi;
    while(lo<hi) {
        size_t mid=lo+(hi-lo)/2;uint64_t at=0;
        if(!read_local(begin+mid*UICONS_CONNECTION_SIZE+UICONS_CONNECTION_HASH,&at,8)) return NULL;
        if(at>=h) hi=mid;else lo=mid+1;
    }
    uint64_t at=0;
    if(lo>=n || !read_local(begin+lo*UICONS_CONNECTION_SIZE+UICONS_CONNECTION_HASH,&at,8) || at!=h) return NULL;
    return begin+lo*UICONS_CONNECTION_SIZE;
}
/* A sequence's entries (begin, count), NULL when it has none or they do not read. */
static void **uicons_entries(void *sequence,size_t *count) {
    void **begin=sequence?uicons_ptr(sequence):NULL,**end=sequence?uicons_ptr((unsigned char*)sequence+8):NULL;
    *count=0;
    if(!begin || end<begin) return NULL;
    *count=(size_t)(end-begin);return begin;
}
static unsigned uicons_index_of(void **entries,size_t count,void *entry) {
    for(size_t i=0;entry && i<count && i<UICONS_NONE;i++) if(uicons_ptr(entries+i)==entry) return (unsigned)i;
    return UICONS_NONE;
}
/* The object's own class test (Component vfunc +0x11e0 / +0x11e8, as the stock code asks any component it found). */
static int uicons_is(void *object,unsigned slot,unsigned kind) {
    return object && ((unsigned char(*)(void*,unsigned))(*(void***)object)[slot])(object,kind);
}
static void *uicons_planned(void *container) {
    return container?((void*(*)(void*))(*(void***)container)[UICONS_PLANNED_SLOT])(container):NULL;
}
/* The fingerprint of a sequence (a station's planned one): every entry's ID, names, predecessor, transform, flags. */
static uint64_t uicons_fingerprint(void *sequence) {
    if(!sequence) return UICONS_BASE_NONE;
    size_t count=0;void **entries=uicons_entries(sequence,&count);
    uint64_t h=pstk_fnv((const unsigned char*)&count,8,UICONS_FNV);
    for(size_t i=0;entries && i<count;i++) {
        unsigned char *e=uicons_ptr(entries+i),raw[0x40];char name[UIBUILD_STRING_MAX+1];unsigned n=0;uint64_t id=0;unsigned char flags[3]={0};
        if(!e) {h=pstk_fnv((const unsigned char*)"-",1,h);continue;}
        read_local(e+UICONS_E_ID,&id,8);h=pstk_fnv((const unsigned char*)&id,8,h);
        if(uicons_macro_name(uicons_ptr(e+UICONS_E_MACRO),name,&n)) h=pstk_fnv((const unsigned char*)name,n+1,h);
        if(uicons_connection_name(uicons_ptr(e+UICONS_E_CONNECTION),name,&n)) h=pstk_fnv((const unsigned char*)name,n+1,h);
        unsigned pred=uicons_index_of(entries,count,uicons_ptr(e+UICONS_E_PRED));h=pstk_fnv((const unsigned char*)&pred,4,h);
        if(uicons_connection_name(uicons_ptr(e+UICONS_E_PRED_CONNECTION),name,&n)) h=pstk_fnv((const unsigned char*)name,n+1,h);
        if(read_local(e+UICONS_E_TRANSFORM,raw,sizeof(raw))) h=pstk_fnv(raw,sizeof(raw),h);
        if(read_local(e+UICONS_E_FIXED,flags,3)) h=pstk_fnv(flags,3,h);
    }
    uint32_t bookmark=0;unsigned char flag=0;
    read_local((unsigned char*)sequence+0x30,&bookmark,4);read_local((unsigned char*)sequence+0x34,&flag,1);
    h=pstk_fnv((const unsigned char*)&bookmark,4,h);return pstk_fnv(&flag,1,h);
}
static int uicons_known_id(void *planned,uint64_t id) {
    size_t count=0;void **entries=uicons_entries(planned,&count);
    for(size_t i=0;id && entries && i<count;i++) {uint64_t at=0;unsigned char *e=uicons_ptr(entries+i);if(e && read_local(e,&at,8) && at==id) return 1;}
    return 0;
}
static void uicons_plot(uint64_t container,uint32_t out[6]) {
    memset(out,0,24);
    if(uibuild_fn.plot_size && uibuild_fn.plot_center) {uibuild_fn.plot_size(out,container);uibuild_fn.plot_center(out+3,container);}
}

/* ---- capture: the holomap's sequence as one record (the claim's 'Q') ---- */
static uicons_macro1 uicons_cap_macro[4][UIBUILD_ARRAY_MAX];
static uicons_group1 uicons_cap_group[2][UIBUILD_ARRAY_MAX];
static uibuild_ammo uicons_cap_ammo[2][UIBUILD_ARRAY_MAX];
static uibuild_software uicons_cap_software[UIBUILD_ARRAY_MAX];
static void uicons_loadout_out(uibuild_writer *w,uint64_t holomap,size_t idx) {
    uicons_counts1 c;memset(&c,0,sizeof(c));
    if(!uicons_fn.counts || !uicons_fn.loadout) {w->why="loadout";return;}
    uicons_fn.counts(&c,holomap,idx,0,0);
    const uint32_t *n=&c.nweapons;
    for(unsigned k=0;k<9;k++) if(n[k]>UIBUILD_ARRAY_MAX) {w->why="array";return;}
    uicons_loadout1 l;memset(&l,0,sizeof(l));
    l.weapons=uicons_cap_macro[0];l.nweapons=c.nweapons;l.turrets=uicons_cap_macro[1];l.nturrets=c.nturrets;
    l.shields=uicons_cap_macro[2];l.nshields=c.nshields;l.engines=uicons_cap_macro[3];l.nengines=c.nengines;
    l.turretgroups=uicons_cap_group[0];l.nturretgroups=c.nturretgroups;l.shieldgroups=uicons_cap_group[1];l.nshieldgroups=c.nshieldgroups;
    l.ammo=uicons_cap_ammo[0];l.nammo=c.nammo;l.units=uicons_cap_ammo[1];l.nunits=c.nunits;l.software=uicons_cap_software;l.nsoftware=c.nsoftware;
    memset(uicons_cap_macro,0,sizeof(uicons_cap_macro));memset(uicons_cap_group,0,sizeof(uicons_cap_group));
    memset(uicons_cap_ammo,0,sizeof(uicons_cap_ammo));memset(uicons_cap_software,0,sizeof(uicons_cap_software));
    uicons_fn.loadout(&l,holomap,idx,0,0);
    const uicons_macro1 *m[4]={l.weapons,l.turrets,l.shields,l.engines};uint32_t mn[4]={l.nweapons,l.nturrets,l.nshields,l.nengines};
    for(unsigned k=0;k<4 && !w->why;k++) {
        uibuild_u16(w,uibuild_count(mn[k],w));
        for(uint32_t e=0;e<mn[k] && !w->why;e++) {
            uibuild_string(w,m[k][e].macro,1);uibuild_string(w,m[k][e].type,1);
            if((uint64_t)m[k][e].slot>0xffffffffu) w->why="slot";
            uibuild_u32(w,(uint32_t)m[k][e].slot);uibuild_u8(w,m[k][e].optional);
        }
    }
    const uicons_group1 *g[2]={l.turretgroups,l.shieldgroups};uint32_t gn[2]={l.nturretgroups,l.nshieldgroups};
    for(unsigned k=0;k<2 && !w->why;k++) {
        uibuild_u16(w,uibuild_count(gn[k],w));
        for(uint32_t e=0;e<gn[k] && !w->why;e++) {
            uibuild_string(w,g[k][e].macro,1);uibuild_string(w,g[k][e].path,1);uibuild_string(w,g[k][e].group,1);
            uibuild_u32(w,g[k][e].count);uibuild_u8(w,g[k][e].optional);
        }
    }
    uibuild_ammo_out(w,l.ammo,l.nammo);uibuild_ammo_out(w,l.units,l.nunits);
    uibuild_u16(w,uibuild_count(l.nsoftware,w));
    for(uint32_t e=0;e<l.nsoftware && !w->why;e++) uibuild_string(w,l.software[e].ware,1);
    uibuild_string(w,l.thruster.macro,1);uibuild_u8(w,l.thruster.optional);
}
static void uicons_transform_out(uibuild_writer *w,const uint32_t t[16]) {
    unsigned mask=0;
    for(unsigned i=0;i<16;i++) if(t[i]) mask|=1u<<i;
    uibuild_u16(w,mask);
    for(unsigned i=0;i<16;i++) if(t[i]) uibuild_u32(w,t[i]);
}
/* The head every plan record starts with: the station's base (planned sequence) and plot as they are now. */
static unsigned char *uicons_head_out(uibuild_writer *w,uint64_t container) {
    unsigned char *object=uirep_component(container);
    if(!object) {w->why="unmapped";return NULL;}
    if(!uicons_is(object,UICONS_IS_CLASS_SLOT,UICONS_CLASS_CONTAINER)) {w->why="container";return NULL;}
    uint32_t plot[6];uicons_plot(container,plot);
    uibuild_u8(w,UICONS_PLAN_VERSION);uibuild_u64(w,uicons_fingerprint(uicons_planned(object)));
    for(unsigned i=0;i<6;i++) uibuild_u32(w,plot[i]);
    return object;
}
static void uicons_plan_out(uibuild_writer *w,uint64_t container,uint64_t holomap) {
    if(w->why) return;
    if(!uicons_ready) {w->why="plan_off";return;}
    unsigned char *object=uicons_head_out(w,container);
    if(!object) return;
    unsigned char *map=uicons_fn.holomap?uicons_fn.holomap(holomap):NULL,*behavior=map?uicons_ptr(map+UICONS_BEHAVIOR):NULL;
    if(!behavior || !((unsigned char(*)(void*,unsigned))(*(void***)behavior)[UICONS_BEHAVIOR_KIND])(behavior,2)) {w->why="holomap";return;}
    void *sequence=uicons_ptr(behavior+UICONS_BEHAVIOR_SEQUENCE),*planned=uicons_planned(object);
    size_t count=0;void **entries=uicons_entries(sequence,&count);
    if(!sequence || !entries) {w->why="sequence";return;}
    if(count>UICONS_ENTRIES_MAX) {w->why="entries";return;}
    uint32_t bookmark=0;unsigned char flag=0;
    if(!read_local((unsigned char*)sequence+0x30,&bookmark,4) || !read_local((unsigned char*)sequence+0x34,&flag,1)) {w->why="sequence";return;}
    uibuild_u32(w,bookmark);uibuild_u8(w,flag);uibuild_u16(w,(unsigned)count);
    for(size_t i=0;i<count && !w->why;i++) {
        unsigned char *e=uicons_ptr(entries+i);uint64_t id=0;uint32_t t[16];unsigned char flags[3];char name[UIBUILD_STRING_MAX+1];unsigned n=0;
        if(!e || !read_local(e+UICONS_E_ID,&id,8) || !read_local(e+UICONS_E_TRANSFORM,t,64) || !read_local(e+UICONS_E_FIXED,flags,3)) {w->why="memory";return;}
        void *macro=uicons_ptr(e+UICONS_E_MACRO),*conn=uicons_ptr(e+UICONS_E_CONNECTION),*pred=uicons_ptr(e+UICONS_E_PRED),*pred_conn=uicons_ptr(e+UICONS_E_PRED_CONNECTION);
        uibuild_u64(w,uicons_known_id(planned,id)?id:0);   /* an entry new to the station: every client draws its ID */
        /* each name must find the same object again (the lookup the apply makes) */
        if(!uicons_macro_name(macro,name,&n) || uicons_macro(name,n)!=macro) {w->why="macro";return;}
        uibuild_string(w,name,0);
        if(conn) {if(!uicons_connection_name(conn,name,&n) || uicons_connection(macro,name,n)!=conn) {w->why="connection";return;}uibuild_string(w,name,0);}
        else uibuild_string(w,NULL,1);
        unsigned p=pred?uicons_index_of(entries,count,pred):UICONS_NONE;
        if(pred && (p==UICONS_NONE || p>=i)) {w->why="order";return;}   /* a predecessor is built earlier */
        uibuild_u16(w,p);
        if(pred_conn) {
            void *pmacro=pred?uicons_ptr((unsigned char*)pred+UICONS_E_MACRO):NULL;
            if(!uicons_connection_name(pred_conn,name,&n) || uicons_connection(pmacro,name,n)!=pred_conn) {w->why="connection";return;}
            uibuild_string(w,name,0);
        } else uibuild_string(w,NULL,1);
        uicons_transform_out(w,t);
        int loadout=uicons_fn.upgrade_empty && !uicons_fn.upgrade_empty(e+UICONS_E_UPGRADE);
        uibuild_u8(w,(flags[0]?UICONS_F_FIXED:0)|(flags[1]?UICONS_F_FLAG:0)|(flags[2]?UICONS_F_BOOKMARK:0)|(loadout?UICONS_F_LOADOUT:0));
        if(loadout) uicons_loadout_out(w,holomap,i);
    }
}
/* A plan from a script's text (native_gate_ui_build_script 'plan': "macro,connection,pred,predconnection,x,y,z;..." with
 * '-' for none, the identity rotation): the same record the capture makes, without a construction map. */
static void uicons_spec_out(uibuild_writer *w,uint64_t container,const char *spec) {
    if(w->why) return;
    if(!uicons_ready) {w->why="plan_off";return;}
    if(!uicons_head_out(w,container)) return;
    static char text[2048];size_t length=strlen(spec);
    if(length>=sizeof(text)) {w->why="spec";return;}
    memcpy(text,spec,length+1);
    char *rows[UICONS_ENTRIES_MAX];unsigned count=0;
    for(char *s=text;s && *s && count<UICONS_ENTRIES_MAX;) {rows[count++]=s;char *semi=strchr(s,';');if(semi) {*semi=0;s=semi+1;} else s=NULL;}
    uibuild_u32(w,0);uibuild_u8(w,0);uibuild_u16(w,count);
    void *macros[UICONS_ENTRIES_MAX];
    for(unsigned i=0;i<count && !w->why;i++) {
        char *f[7];unsigned nf=0;
        for(char *s=rows[i];s && nf<7;) {f[nf++]=s;char *comma=strchr(s,',');if(comma) {*comma=0;s=comma+1;} else s=NULL;}
        if(nf!=7) {w->why="spec";return;}
        unsigned n=(unsigned)strlen(f[0]);macros[i]=uicons_macro(f[0],n);
        if(!macros[i]) {w->why="macro";return;}
        uibuild_u64(w,0);uibuild_string(w,f[0],0);
        int has_conn=strcmp(f[1],"-")!=0;
        if(has_conn && !uicons_connection(macros[i],f[1],(unsigned)strlen(f[1]))) {w->why="connection";return;}
        uibuild_string(w,has_conn?f[1]:NULL,1);
        unsigned pred=strcmp(f[2],"-")?(unsigned)strtoul(f[2],NULL,10):UICONS_NONE;
        if(pred!=UICONS_NONE && pred>=i) {w->why="order";return;}
        uibuild_u16(w,pred);
        int has_pc=strcmp(f[3],"-")!=0;
        if(has_pc && (pred==UICONS_NONE || !uicons_connection(macros[pred],f[3],(unsigned)strlen(f[3])))) {w->why="connection";return;}
        uibuild_string(w,has_pc?f[3]:NULL,1);
        float v[16]={0};v[0]=strtof(f[4],NULL);v[1]=strtof(f[5],NULL);v[2]=strtof(f[6],NULL);v[4]=v[9]=v[14]=1.0f;   /* position, w 0, identity rows */
        uint32_t t[16];memcpy(t,v,64);uicons_transform_out(w,t);uibuild_u8(w,0);
    }
}

/* ---- the record at the apply ---- */
static __attribute__((aligned(16))) unsigned char uicons_arena[0x10000];
static unsigned uicons_arena_used;
static uicons_plan uicons_rx_plan;
static void *uicons_alloc(uibuild_reader *r,size_t n) {
    size_t need=(n+15)&~(size_t)15;
    if(r->why) return NULL;
    if(!r->run) return uicons_arena;   /* the check pass only walks */
    if(need>sizeof(uicons_arena)-uicons_arena_used) {r->why="arena";return NULL;}
    void *p=uicons_arena+uicons_arena_used;uicons_arena_used+=(unsigned)need;memset(p,0,n);return p;
}
static uicons_macro1 *uicons_gmacro1(uibuild_reader *r,uint32_t *count) {
    unsigned n=uibuild_g16(r);uicons_macro1 *a=n?uicons_alloc(r,sizeof(*a)*n):NULL;*count=n;
    if(n>UIBUILD_ARRAY_MAX && !r->why) r->why="array";
    for(unsigned i=0;i<n && !r->why;i++) {
        uicons_macro1 e;memset(&e,0,sizeof(e));e.macro=uibuild_gstring(r,1);e.type=uibuild_gstring(r,1);e.slot=uibuild_g32(r);e.optional=(unsigned char)uibuild_g8(r);
        if(r->run && a) a[i]=e;
    }
    return r->run?a:NULL;
}
static uicons_group1 *uicons_ggroup1(uibuild_reader *r,uint32_t *count) {
    unsigned n=uibuild_g16(r);uicons_group1 *a=n?uicons_alloc(r,sizeof(*a)*n):NULL;*count=n;
    if(n>UIBUILD_ARRAY_MAX && !r->why) r->why="array";
    for(unsigned i=0;i<n && !r->why;i++) {
        uicons_group1 e;memset(&e,0,sizeof(e));e.macro=uibuild_gstring(r,1);e.path=uibuild_gstring(r,1);e.group=uibuild_gstring(r,1);
        e.count=uibuild_g32(r);e.optional=(unsigned char)uibuild_g8(r);
        if(r->run && a) a[i]=e;
    }
    return r->run?a:NULL;
}
static uicons_loadout1 *uicons_gloadout(uibuild_reader *r) {
    uicons_loadout1 *l=uicons_alloc(r,sizeof(*l)),t;memset(&t,0,sizeof(t));
    t.weapons=uicons_gmacro1(r,&t.nweapons);t.turrets=uicons_gmacro1(r,&t.nturrets);t.shields=uicons_gmacro1(r,&t.nshields);t.engines=uicons_gmacro1(r,&t.nengines);
    t.turretgroups=uicons_ggroup1(r,&t.nturretgroups);t.shieldgroups=uicons_ggroup1(r,&t.nshieldgroups);
    t.ammo=uibuild_gammo(r,&t.nammo);t.units=uibuild_gammo(r,&t.nunits);   /* from the batch arena (reset per record) */
    t.nsoftware=uibuild_g16(r);t.software=t.nsoftware?uicons_alloc(r,sizeof(uibuild_software)*t.nsoftware):NULL;
    if(t.nsoftware>UIBUILD_ARRAY_MAX && !r->why) r->why="array";
    for(unsigned e=0;e<t.nsoftware && !r->why;e++) {const char *ware=uibuild_gstring(r,1);if(r->run && t.software) t.software[e].ware=ware;}
    t.thruster.macro=uibuild_gstring(r,1);t.thruster.optional=(unsigned char)uibuild_g8(r);
    if(!r->run) return NULL;
    if(l) *l=t;
    return l;
}
/* The record into uicons_rx_plan (run) or only walked (check). 1, or 0 with r->why. */
static int uicons_gplan(uibuild_reader *r) {
    uicons_plan *p=&uicons_rx_plan;
    if(r->run) {uicons_arena_used=0;memset(p,0,sizeof(*p));}
    if(uibuild_g8(r)!=UICONS_PLAN_VERSION && !r->why) r->why="plan_version";
    uint64_t base=uibuild_g64(r);uint32_t plot[6];for(unsigned i=0;i<6;i++) plot[i]=uibuild_g32(r);
    uint32_t bookmark=uibuild_g32(r);unsigned flag=uibuild_g8(r),count=uibuild_g16(r);
    if(count>UICONS_ENTRIES_MAX && !r->why) r->why="entries";
    if(r->run) {p->base=base;memcpy(p->plot,plot,24);p->bookmark=bookmark;p->seq_flag=(unsigned char)flag;p->count=count;}
    for(unsigned i=0;i<count && !r->why;i++) {
        uicons_entry e;memset(&e,0,sizeof(e));
        e.id=uibuild_g64(r);e.macro=uibuild_gstring(r,0);e.conn=uibuild_gstring(r,1);e.pred=uibuild_g16(r);e.pred_conn=uibuild_gstring(r,1);
        if(e.pred!=UICONS_NONE && e.pred>=i && !r->why) r->why="order";
        if(e.pred==UICONS_NONE && e.pred_conn && !r->why) r->why="order";
        unsigned mask=uibuild_g16(r);
        for(unsigned k=0;k<16;k++) e.transform[k]=(mask>>k)&1u?uibuild_g32(r):0;
        e.flags=(unsigned char)uibuild_g8(r);
        if(e.flags>15 && !r->why) r->why="flags";
        if(e.flags&UICONS_F_LOADOUT) e.loadout=uicons_gloadout(r);
        if(r->run) p->e[i]=e;
    }
    return !r->why;
}

/* ---- the commit (own code following SetConstructionSequenceFromConstructionMap 0x1dc030) ---- */
static __attribute__((aligned(16))) unsigned char uicons_source[UICONS_ENTRIES_MAX][UICONS_ENTRY_SIZE];
static void *uicons_source_ptrs[UICONS_ENTRIES_MAX];
static uint64_t uicons_source_bookmarks[UICONS_ENTRIES_MAX];
static __attribute__((aligned(16))) unsigned char uicons_sequence[UICONS_SEQUENCE_SIZE];
static void uicons_acquire(unsigned char *o) {
    InterlockedCompareExchange((volatile LONG*)(o+UICONS_REFS+4),2,1);InterlockedExchangeAdd((volatile LONG*)(o+UICONS_REFS),1);
}
static void uicons_release(unsigned char *o) {
    if(!o) return;
    unsigned char *c=o+UICONS_REFS-8;
    if(InterlockedExchangeAdd((volatile LONG*)(c+8),-1)==1 && InterlockedCompareExchange((volatile LONG*)(c+0xc),3,2)==2)
        ((void(*)(void*))(**(void***)c))(c);
}
/* Resolve the names, check base, plot and owner: 1 ready, 0 skipped (*skip, the same on every client). */
static int uicons_prepare(unsigned char *object,uint64_t container,uicons_plan *p,const char **skip) {
    if(!uicons_is(object,UICONS_IS_CLASS_SLOT,UICONS_CLASS_CONTAINER)) {*skip="container";return 0;}
    if(uicons_fingerprint(uicons_planned(object))!=p->base) {*skip="plan_changed";return 0;}
    uint32_t plot[6];uicons_plot(container,plot);
    if(memcmp(plot,p->plot,24)) {*skip="plot_changed";return 0;}
    for(unsigned i=0;i<p->count;i++) {
        uicons_entry *e=&p->e[i];char name[UIBUILD_STRING_MAX+1];unsigned n=0;
        e->macro_ptr=uicons_macro(e->macro,(unsigned)strlen(e->macro));
        if(!e->macro_ptr || !uicons_macro_name(e->macro_ptr,name,&n) || strcmp(name,e->macro)) {*skip="macro";return 0;}
        e->conn_ptr=e->conn?uicons_connection(e->macro_ptr,e->conn,(unsigned)strlen(e->conn)):NULL;
        if(e->conn && !e->conn_ptr) {*skip="connection";return 0;}
        e->pred_conn_ptr=e->pred_conn?uicons_connection(p->e[e->pred].macro_ptr,e->pred_conn,(unsigned)strlen(e->pred_conn)):NULL;
        if(e->pred_conn && !e->pred_conn_ptr) {*skip="connection";return 0;}
    }
    return 1;
}
/* The stock function's walk with the built sequence; *valid = X4 kept the task. 1 ran, 0 skipped (*skip). */
static int uicons_build(unsigned char *object,void *faction,int *valid,const char **skip) {
    *valid=0;
    void *context=NULL;
    if(!uicons_fn.build_context || !uicons_fn.build_context(object,&context) || !context) {*skip="build_context";return 0;}
    unsigned char *parent=uicons_ptr((unsigned char*)context+UICONS_PARENT);
    while(parent && !uicons_is(parent,UICONS_IS_DERIVED_SLOT,UICONS_CLASS_BUILD_PARENT)) parent=uicons_ptr(parent+UICONS_PARENT);
    if(!parent) {*skip="build_parent";return 0;}
    unsigned char *task=uicons_fn.allocate(UICONS_BUILD_TASK_SIZE,0,0,0,0x10);
    if(!task) {*skip="allocate";return 0;}
    uicons_acquire(parent);uicons_acquire(object);
    void *holder_object=object,*holder_parent=parent;
    task=uicons_fn.build_task(task,&holder_object,&holder_parent,faction,uicons_sequence,0,0);
    uicons_release(holder_object);uicons_release(holder_parent);
    uint32_t a=0;uint64_t b=0,c=0;unsigned char d=0;
    read_local(task+0x10,&a,4);read_local(task+0x18,&b,8);read_local(task+0x20,&c,8);read_local(task+0x28,&d,1);
    *valid=a && b && (!d || c);
    if(!*valid) ((void(*)(void*,unsigned))(*(void***)task)[0])(task,1);
    return 1;
}
/* One plan at its apply: 1 ran (*value: the planned sequence's fingerprint after, 0 when X4 kept no task), 0 skipped. */
static int uicons_commit(uint64_t container,uicons_plan *p,void *faction,uint64_t *value,const char **skip) {
    unsigned char *object=uirep_component(container);*value=0;
    if(!object) {*skip="gone";return 0;}
    if(!uicons_ready) {*skip="plan_off";return 0;}
    if(!uicons_prepare(object,container,p,skip)) return 0;
    if(!faction || uitrade_owner(object)!=faction) {*skip="owner";return 0;}
    /* the source sequence: every entry X4's copy reads, its upgrade plan made by X4 (empty, or from the loadout) */
    unsigned bookmarks=0;
    for(unsigned i=0;i<p->count;i++) {
        unsigned char *e=uicons_source[i];const uicons_entry *s=&p->e[i];memset(e,0,UICONS_ENTRY_SIZE);
        uicons_fn.upgrade_init(e+UICONS_E_UPGRADE);
        memcpy(e+UICONS_E_ID,&s->id,8);memcpy(e+UICONS_E_MACRO,&s->macro_ptr,8);memcpy(e+UICONS_E_CONNECTION,&s->conn_ptr,8);
        void *pred=s->pred!=UICONS_NONE?(void*)uicons_source[s->pred]:NULL;
        memcpy(e+UICONS_E_PRED,&pred,8);memcpy(e+UICONS_E_PRED_CONNECTION,&s->pred_conn_ptr,8);memcpy(e+UICONS_E_TRANSFORM,s->transform,64);
        e[UICONS_E_FIXED]=(s->flags&UICONS_F_FIXED)!=0;e[UICONS_E_FLAG]=(s->flags&UICONS_F_FLAG)!=0;e[UICONS_E_BOOKMARK]=(s->flags&UICONS_F_BOOKMARK)!=0;
        if(s->loadout) uicons_fn.upgrade_from(e+UICONS_E_UPGRADE,s->macro_ptr,s->loadout);
        uicons_source_ptrs[i]=e;
        if(e[UICONS_E_BOOKMARK]) uicons_source_bookmarks[bookmarks++]=i;
    }
    void *v[6]={uicons_source_ptrs,uicons_source_ptrs+p->count,uicons_source_ptrs+p->count,uicons_source_bookmarks,uicons_source_bookmarks+bookmarks,uicons_source_bookmarks+bookmarks};
    memset(uicons_sequence,0,sizeof(uicons_sequence));memcpy(uicons_sequence,v,sizeof(v));
    memcpy(uicons_sequence+0x30,&p->bookmark,4);uicons_sequence[0x34]=p->seq_flag;
    int valid=0,ran=uicons_build(object,faction,&valid,skip);
    for(unsigned i=0;i<p->count;i++) uicons_fn.upgrade_free(uicons_source[i]+UICONS_E_UPGRADE);
    if(!ran) return 0;
    InterlockedIncrement64(&uicons_ran);if(valid) InterlockedIncrement64(&uicons_valid);
    *value=valid?uicons_fingerprint(uicons_planned(object))|1u:0;
    return 1;
}
/* The claim's apply (uibuild_step, inside the slot's faction scope in factions mode): the faction X4's commit names is
 * X4's player faction as it is now, as the stock function reads it. */
static uint64_t uicons_apply(unsigned slot,uint64_t container,const char **skip,int *ran) {
    (void)slot;uint64_t value=0;
    *ran=uicons_commit(container,&uicons_rx_plan,*uitrade_player_faction(),&value,skip);
    return value;
}

/* gate.ui_build_plan: a scripted plan commit of this client's slot, the record a person's confirm makes (a local phase;
 * native_gate_ui_build_script 'plan'). 1 in the open batch, or 0 and why (a row, nothing anywhere). */
static int uicons_script_plan(uint64_t container,const char *spec,const char **why) {
    const uibuild_claim *c=NULL;unsigned index=0;
    for(unsigned i=0;i<UIBUILD_CLAIMS;i++) if(!strcmp(uibuild_claims[i].name,UIBUILD_PLAN_CLAIM)) {c=&uibuild_claims[i];index=i;}
    if(!c || !uibuild_armed || !(uirep_families&UIREP_FAMILY_BUILD) || !uicons_ready) {*why="plan_off";return 0;}
    if(!uirep_capture_now() || InterlockedCompareExchange(&rid_probe_frame,0,0)) {*why="not_local";return 0;}
    unsigned slot=pstk_capture_slot;
    if(uibuild_slot_refusal(c,slot,0,why)) {uibuild_note(c->name,*why);return 0;}
    unsigned char *object=uirep_component(container);
    void *own=uibuild_factions && slot>=2?uitrade_slot_faction(slot):*uitrade_player_faction();
    if(!object || !own || uitrade_owner(object)!=own) {*why=object?"owner":"unmapped";uibuild_note(c->name,*why);return 0;}
    for(int attempt=0;attempt<2;attempt++) {
        uibuild_writer w;uibuild_writer_start(&w);
        uibuild_component(&w,container,0);uicons_spec_out(&w,container,spec);
        if(w.why) {*why=w.why;uibuild_note(c->name,w.why);return 0;}
        int appended=uibuild_append('N',index,0,w.b,w.n,&w);
        if(appended>0) {InterlockedIncrement64(&uibuild_captured);InterlockedIncrement64(&uicons_captured);return 1;}
        if(appended==0 || attempt || !uibuild_open_records) break;
        uibuild_flush("full");
    }
    *why="too_long";uibuild_note(c->name,*why);return 0;
}

/* ---- the construction ID counter ---- */
static int uicons_cid_on,uicons_cid_window;
static uint64_t uicons_cid_canon;
static volatile uint64_t *uicons_cid_counter(void) {return (volatile uint64_t*)(pnat_base+UICONS_CID_COUNTER);}
static CRITICAL_SECTION *uicons_cid_lock(void) {return (CRITICAL_SECTION*)(void*)(pnat_base+UICONS_CID_LOCK);}
static int uicons_cid_live(void) {return uicons_cid_on && uibuild_armed && !uirep_stopped && GetCurrentThreadId()==ui_thread;}
static void uicons_cid_snapshot(void) {
    EnterCriticalSection(uicons_cid_lock());uicons_cid_canon=*uicons_cid_counter();LeaveCriticalSection(uicons_cid_lock());uicons_cid_window=1;
}
static void uicons_cid_rewind(void) {
    if(!uicons_cid_window) return;
    EnterCriticalSection(uicons_cid_lock());
    uint64_t now=*uicons_cid_counter();
    if(now>uicons_cid_canon) {InterlockedExchangeAdd64(&uicons_cid_rewound,(LONG64)(now-uicons_cid_canon));InterlockedIncrement64(&uicons_cid_rewinds);*uicons_cid_counter()=uicons_cid_canon;}
    LeaveCriticalSection(uicons_cid_lock());
}
/* uirep_ui_move: this client's UI move starts (the simulation ran before: canonical) or ends (its draws go back). */
static void uicons_cid_ui(int enter) {
    if(!uicons_cid_live()) {uicons_cid_window=0;return;}
    if(enter) uicons_cid_snapshot();else {uicons_cid_rewind();uicons_cid_window=0;}
}
/* gate.runtime_ids_probe: the probe's frame (applies, every client alike) starts (local draws before it go back) or ends. */
static void uicons_cid_probe(int on) {
    if(!uicons_cid_live() || !uicons_cid_window) return;
    if(on) uicons_cid_rewind();else uicons_cid_snapshot();
}

/* ---- arming ---- */
static int uicons_pins(void) {
    for(unsigned i=0;i<UICONS_PINS;i++) if(!hash_ok(image+uicons_pin_table[i].rva,uicons_pin_table[i].size,uicons_pin_table[i].sha)) {pin_failed_rva=uicons_pin_table[i].rva;return 0;}
    unsigned char *slot=NULL;memcpy(&slot,image+UICONS_STATION_VTABLE+8u*UICONS_PLANNED_SLOT,8);
    if(slot!=image+UICONS_PLANNED_FN) {pin_failed_rva=UICONS_STATION_VTABLE;return 0;}
    memcpy(&slot,image+UICONS_STATION_VTABLE+8u*UICONS_OWNER_SLOT,8);
    if(slot!=image+UICONS_OWNER_FN) {pin_failed_rva=UICONS_STATION_VTABLE;return 0;}
    return 1;
}
static void uicons_bind_image(void) {
    uicons_fn=(uicons_fns){
        .holomap=(void*(*)(uint64_t))(void*)(image+UICONS_HOLOMAP_OF),.macro=(void*(*)(void*,const void*,uint64_t))(void*)(image+UICONS_MACRO_LOOKUP),
        .counts=(void(*)(uicons_counts1*,uint64_t,size_t,uint64_t,uint64_t))(void*)(image+UICONS_LOADOUT_COUNTS),
        .loadout=(void(*)(uicons_loadout1*,uint64_t,size_t,uint64_t,uint64_t))(void*)(image+UICONS_LOADOUT),
        .upgrade_empty=(unsigned char(*)(const void*))(void*)(image+UICONS_UPGRADE_EMPTY),.upgrade_init=(void*(*)(void*))(void*)(image+UICONS_UPGRADE_INIT),
        .upgrade_free=(void(*)(void*))(void*)(image+UICONS_UPGRADE_FREE),.upgrade_from=(void(*)(void*,void*,const uicons_loadout1*))(void*)(image+UICONS_UPGRADE_FROM),
        .build_context=(void**(*)(void*,void**))(void*)(image+UICONS_BUILD_CONTEXT),
        .allocate=(void*(*)(size_t,uint64_t,uint64_t,uint64_t,uint32_t))(void*)(image+UICONS_ALLOCATE),
        .build_task=(void*(*)(void*,void**,void**,void*,void*,uint32_t,uint64_t))(void*)(image+UICONS_BUILD_TASK)};
}
static void uicons_reset(void) {
    uicons_captured=uicons_refused=uicons_ran=uicons_valid=uicons_skipped=0;uicons_last_why="-";
    uicons_cid_window=0;uicons_cid_rewound=uicons_cid_rewinds=0;
}
