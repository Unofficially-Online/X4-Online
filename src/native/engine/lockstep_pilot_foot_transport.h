/* Slots 2..N take X4's transporter (part of lockstep_pilot_foot_body.h's room change; on with it when the pins match;
 * no stop reason of its own: a carry that cannot be made is counted and the player stays where it is).
 *
 * What X4 does [F, pinned 9.00]. The transporter menu's "Travel to" is the export TransportPlayerToTarget (0x1bcc70) ->
 * U::TransportPlayerToTargetAction's execute 0xef3bf0: for a place (not a zone) it reads the floor under X4's player
 * from its FirstPersonController (the component +0x390 and its part +0x398, whose connection is +0x170), finds the
 * target's connection by its name, starts the two transporters' effects on the player (0x73b410, 0x73b390) and posts
 * U::PerformTransporterTransitionEvent (vtable 0x2baa770; dispatch 0x9ab1d0; the source's ID +0x18 and connection
 * name +0x20, the target's ID +0x48 and name +0x50) to the container (class 0x6f) a setting's delay later. The
 * container's handler (U::Container::vfunc25 0x3f67d0) calls 0x445bf0(container, out, source, its connection, target,
 * its connection, entity = none, time) -> 0x445fc0: every entity in the container's tree that stands on the source
 * transporter gets a U::ChangeRoomEvent with the target's room and a place: the entity's transform in the source's
 * room, taken relative to the source transporter (0x74be30 for the connection, then
 * Component::GetTransformRelativeTo 0x3db8a0 into the room) and put at the target transporter the same way, flags 1/1.
 * U::Entity::vfunc24 (0x55d2a0) then connects it to that room with the place, or, in the same room, sets the place
 * (its vfunc 0x1468) and posts TransportedToOffsetEvent and EntityTransportationFinishedEvent.
 *
 * A walking slot's player is not in the tree (its parent is an upward pointer), so X4's carry does not find it.
 * Here, for a slot 2..N that walks:
 *   capture  lockstep_ui_compslot.h: the menu's "Travel to" of a walking slot is the same event as slot 1's (the
 *            command's class, the target and its transporter connection's name).
 *   apply    X4's own export in the slot's scope (its player is X4's player there): the floor under P_k, the effects,
 *            the event to the container, all X4's. The gate notes (slot, target ID) for the event.
 *   dispatch the event's dispatch entry points at a wrapper: an event whose target ID a slot noted is that slot's.
 *   carry    for a slot's event the handler's call of 0x445bf0 points at a wrapper that carries P_k instead of X4's
 *            search (which would carry whoever else stands there, never P_k): the two transporters' transforms from
 *            X4's own two routines, P_k's own from GetTransformRelativeTo, the same re-expression (pftr_place), then
 *            the upward pointer to the target's room and X4's own handler 0x55d2a0 with (room, place, 1, 1), which
 *            finds P_k in that room already and does the rest (the place, the two events). MD hears the change of
 *            object and room as for a walk (pfbody_room_notice).
 * STOPGAPS, named as such: nobody else who stands on the transporter with the slot is carried by the slot's event; a
 * slot's player that stands on a transporter when another player travels is not carried.
 * Status (PILOT_FOOT bodies row): travel=installed/noted/carried/skipped/idle (idle: the slot did not stand on the
 * source transporter, so nobody was to be carried: in a small ship the menu's only target is its own cockpit). */
#define PFTR_EVENT_DISPATCH_SLOT 0x2baa780u   /* U::PerformTransporterTransitionEvent's vtable 0x2baa770, slot 2 */
#define PFTR_EVENT_DISPATCH 0x9ab1d0u
#define PFTR_EVENT_DISPATCH_SIZE 0x17au
#define PFTR_EVENT_DISPATCH_SHA "bf7d95ed023cde825c771dd4840eaaf278b6b7e2e240766105c113117e690aa8"
#define PFTR_EVENT_TARGET 0x48u
#define PFTR_HANDLER 0x3f67d0u                /* U::Container::vfunc25 */
#define PFTR_HANDLER_SIZE 0x9eu
#define PFTR_HANDLER_SHA "3bf443e958a6c029e54c42dc160a697a53e74843cd9f5173a0c992b557527cec"
#define PFTR_CARRY_SITE 0x3f6822u             /* its call of 0x445bf0 */
#define PFTR_CARRY 0x445bf0u
#define PFTR_CARRY_STUB 0xf40u                /* on the slots' near page, after the UI holds' (0xf00, 0xf20) */
#define PFTR_EXECUTE 0xef3bf0u
#define PFTR_EXECUTE_SIZE 0x442u
#define PFTR_EXECUTE_SHA "35f2a5c63f574bd62a6ff3f21c8974af7cece884c92b85d09abfdb3ed2273e0e"
#define PFTR_CONNECTION 0x74be30u             /* (component, out PosRot, connection, time, 1, 1, NULL) */
#define PFTR_CONNECTION_SIZE 0x437u
#define PFTR_CONNECTION_SHA "28bb0f12f429c4657160213cbbfb0ca89c321fe20ab4d832f62b2f3b28718c17"
#define PFTR_RELATIVE 0x3db8a0u               /* U::Component::GetTransformRelativeTo(component, inout PosRot, time, space, 0) */
#define PFTR_RELATIVE_SIZE 0x465u
#define PFTR_RELATIVE_SHA "6e28847e1ab6eb2e206ec564645b32dee94a0fdd2eefe0884bdedad46da1ad90"
#define PFTR_WAITS 8u
#define PFTR_WAIT_TICKS 900u                  /* a noted travel whose event never came is forgotten after this */
typedef void (*pftr_dispatch_fn)(void *event,void *listener,void *target,double time);
typedef void (*pftr_carry_fn)(void *container,void *out,void *from,void *from_connection,void *to,void *to_connection,void *entity,double time);
typedef void (*pftr_connection_fn)(void *component,float *out,void *connection,double time,unsigned char a,unsigned char b,void *c);
typedef void (*pftr_relative_fn)(void *component,float *inout,double time,void *space,unsigned char flag);
static pftr_connection_fn pftr_connection;    /* the image's at the arming, a test's */
static pftr_relative_fn pftr_relative;
static pfbody_room_fn pftr_room_handler;
static unsigned char pftr_site_patched[5];
static struct {unsigned slot;uint64_t to,tick;} pftr_waits[PFTR_WAITS];
static unsigned pftr_current;                 /* the slot whose event is being dispatched (the simulation thread) */
/* The apply (lockstep_ui_compslot.h, before X4's export runs): this slot travels to the target with that ID. Slot 1
 * is noted too, so its own event is not taken for a slot's that goes to the same place. */
static void pftr_note(unsigned slot,uint64_t to) {
    unsigned at=0;
    if(!pftr_installed || !slot || !to) return;
    for(unsigned i=0;i<PFTR_WAITS;i++) {
        if(!pftr_waits[i].slot || pftr_waits[i].tick+PFTR_WAIT_TICKS<stats.issued) {at=i;break;}
        if(pftr_waits[i].tick<pftr_waits[at].tick) at=i;   /* full: the oldest goes */
    }
    pftr_waits[at].slot=slot;pftr_waits[at].to=to;pftr_waits[at].tick=stats.issued;
    InterlockedIncrement64(&pftr_notes);
}
/* The oldest noted travel to that target: its slot (0: none), taken. */
static unsigned pftr_take(uint64_t to) {
    int best=-1;
    for(unsigned i=0;i<PFTR_WAITS;i++) {
        if(!pftr_waits[i].slot || pftr_waits[i].to!=to || pftr_waits[i].tick+PFTR_WAIT_TICKS<stats.issued) continue;
        if(best<0 || pftr_waits[i].tick<pftr_waits[best].tick) best=(int)i;
    }
    if(best<0) return 0;
    const unsigned slot=pftr_waits[best].slot;pftr_waits[best].slot=0;return slot;
}
/* X4's re-expression (0x445fc0): a transform given in the source room, relative to the source transporter, put at the
 * target transporter in the target's room. PosRots: translation, then three rows; the w lanes stay the entity's. */
static void pftr_place(const float entity[16],const float from[16],const float to[16],float out[16]) {
    float r[16];
    const float d[3]={entity[0]-from[0],entity[1]-from[1],entity[2]-from[2]};
    float local[3];
    for(unsigned i=0;i<3;i++) local[i]=d[0]*from[4*(i+1)]+d[1]*from[4*(i+1)+1]+d[2]*from[4*(i+1)+2];
    for(unsigned j=0;j<3;j++) r[j]=local[0]*to[4+j]+local[1]*to[8+j]+local[2]*to[12+j]+to[j];
    r[3]=entity[3];
    for(unsigned row=1;row<4;row++) {
        for(unsigned i=0;i<3;i++) local[i]=entity[4*row]*from[4*(i+1)]+entity[4*row+1]*from[4*(i+1)+1]+entity[4*row+2]*from[4*(i+1)+2];
        for(unsigned j=0;j<3;j++) r[4*row+j]=local[0]*to[4+j]+local[1]*to[8+j]+local[2]*to[12+j];
        r[4*row+3]=entity[4*row+3];
    }
    memcpy(out,r,sizeof(r));
}
static void pftr_identity(float m[16]) {memset(m,0,16*sizeof(float));m[4]=m[9]=m[14]=1.0f;}
/* X4's own class test of a component (vtable +0x11e8); 0 also when it cannot be asked. */
static int pftr_is(unsigned char *c,int cls) {
    void **vt=NULL;unsigned char is=0;int fault=0;
    if(!c || !read_local(c,&vt,8) || !vt) return 0;
    __try {is=((unsigned char(*)(void*,int))vt[0x11e8/8])(c,cls);}
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
    return !fault && is;
}
/* The room a transporter's entities stand in, as 0x445bf0 finds it: the nearest walkable room (class 0x54) from the
 * transporter's component up to its container (class 0x6f), else the container's own room (+0x410, a class 0x19
 * room: a small ship's cockpit). */
static unsigned char *pftr_room_of(unsigned char *transporter) {
    unsigned char *container=transporter,*c=transporter,*room=NULL;
    for(unsigned depth=0;container && !pftr_is(container,0x6f);depth++) {
        if(depth>=32 || !read_local(container+0x70,&container,8)) return NULL;
    }
    if(!container) return NULL;
    for(unsigned depth=0;c && c!=container && depth<32;depth++) {
        if(pftr_is(c,0x54)) return c;
        if(!read_local(c+0x70,&c,8)) return NULL;
    }
    if(!read_local(container+PSLOT_ROOM,&room,8) || !room || !pftr_is(room,0x19)) return NULL;
    return room;
}
/* Slot k's carry (header). 1 carried, 2 the player does not stand on the source transporter (X4 carries nobody
 * then either: 0x445fc0 takes an entity only when the connection of the part under it (+0x170) is of the source
 * connection's group (+0x120) and carries the transporter tag, bit 59 of +0xf0), 0 it could not be made. */
static int pftr_slot_carry(unsigned k,unsigned char *from,void *from_connection,unsigned char *to,void *to_connection,double time) {
    pslot_slot *s=&pslots[k];unsigned char *parent=NULL,life=1,in=0;double now=0;int fault=0;
    {   unsigned char *fpc=pfslot_walking_fpc(k),*part=NULL,*under=NULL;int group=0,want=-1;uint64_t tags=0;
        if(!fpc || !from_connection || !read_local(fpc+0x398,&part,8) || !part || !read_local(part+0x170,&under,8) || !under ||
           !read_local(under+0x120,&group,4) || !read_local((unsigned char*)from_connection+0x120,&want,4) || !read_local(under+0xf0,&tags,8)) return 0;
        if(group!=want || !(tags&0x800000000000000ULL)) return 2;
    }
    __attribute__((aligned(16))) float pad_from[16],pad_to[16],entity[16],place[16];
    if(!pftr_connection || !pftr_relative || !pftr_room_handler || !pfslot_walking_fpc(k) || !s->walking || (pfbody_mask>>(k-1)&1u) ||
       pilot_slots[k-1].dead || pslot_ship_dying(s) || !from || !to || !pnat_fn.time || !pnat_fn.time(&now)) return 0;
    unsigned char *const room_from=pftr_room_of(from),*const room_to=pftr_room_of(to);
    if(!room_from || !room_to || !read_local(s->player+0x70,&parent,8) || parent!=pslot_at(s) || parent!=room_from) return 0;
    if(!read_local(room_to+PFBODY_LIFECYCLE,&life,1) || life || !read_local(room_to+PFBODY_PLACED,&in,1) || !in) return 0;
    const int own=pstk_scope_slot==k;
    if(!own && (pstk_scope_slot || !pslot_enter(k))) return 0;
    pftr_identity(pad_from);pftr_identity(pad_to);pftr_identity(entity);
    __try {
        pftr_connection(from,pad_from,from_connection,now,1,1,NULL);pftr_relative(from,pad_from,time,room_from,0);
        pftr_connection(to,pad_to,to_connection,now,1,1,NULL);pftr_relative(to,pad_to,time,room_to,0);
        pftr_relative(s->player,entity,now,room_from,0);
        pftr_place(entity,pad_from,pad_to,place);
        *(unsigned char *volatile*)(s->player+0x70)=room_to;s->walk_room=room_to==s->room?NULL:room_to;
        pftr_room_handler(s->player,s->player,time,room_to,place,1,1);   /* X4's own: it is in that room, so the place and the events */
    }
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
    if(!own && !pslot_leave(k)) pslot_async("travel_leave");
    if(fault) {pslot_async("travel_fault");return 0;}
    pfbody_room_notice(k,parent,room_to);
    return 1;
}
static void pftr_dispatch(void *event,void *listener,void *target,double time) {
    uint64_t to=0;unsigned k=0;
    if(pftr_installed && event && read_local((unsigned char*)event+PFTR_EVENT_TARGET,&to,8) && to) k=pftr_take(to);
    const unsigned was=pftr_current;
    pftr_current=k>=2 && k<=PILOT_SLOTS?k:0;
    ((pftr_dispatch_fn)(image+PFTR_EVENT_DISPATCH))(event,listener,target,time);
    pftr_current=was;
}
static void pftr_carry(void *container,void *out,void *from,void *from_connection,void *to,void *to_connection,void *entity,double time) {
    const unsigned k=pftr_current;
    if(k && !entity) {
        const int did=pftr_slot_carry(k,from,from_connection,to,to_connection,time);
        InterlockedIncrement64(did==1?&pftr_carried:did==2?&pftr_idle:&pftr_skipped);
        return;
    }
    ((pftr_carry_fn)(image+PFTR_CARRY))(container,out,from,from_connection,to,to_connection,entity,time);
}
static void pftr_install(int on) {
    if(on && !pftr_installed) {
        if(!pfbody_room_installed || !pnat_base || (!pslot_page && !(pslot_page=pslot_alloc_page()))) return;
        if(!hash_ok(image+PFTR_EVENT_DISPATCH,PFTR_EVENT_DISPATCH_SIZE,PFTR_EVENT_DISPATCH_SHA) || !hash_ok(image+PFTR_HANDLER,PFTR_HANDLER_SIZE,PFTR_HANDLER_SHA) ||
           !hash_ok(image+PFTR_EXECUTE,PFTR_EXECUTE_SIZE,PFTR_EXECUTE_SHA) || !hash_ok(image+PFTR_CONNECTION,PFTR_CONNECTION_SIZE,PFTR_CONNECTION_SHA) ||
           !hash_ok(image+PFTR_RELATIVE,PFTR_RELATIVE_SIZE,PFTR_RELATIVE_SHA)) return;
        pftr_connection=(pftr_connection_fn)(image+PFTR_CONNECTION);pftr_relative=(pftr_relative_fn)(image+PFTR_RELATIVE);
        pftr_room_handler=(pfbody_room_fn)(image+PFBODY_ROOM_HANDLER);
        memset(pftr_waits,0,sizeof(pftr_waits));pftr_current=0;pftr_notes=pftr_carried=pftr_skipped=pftr_idle=0;
        if(!pres_site_install(PFTR_CARRY_SITE,PFTR_CARRY,PFTR_CARRY_STUB,(void*)pftr_carry,pftr_site_patched)) return;
        if(!script_rng_slot_swap(PFTR_EVENT_DISPATCH_SLOT,image+PFTR_EVENT_DISPATCH,(void*)pftr_dispatch)) {
            (void)pres_site_uninstall(PFTR_CARRY_SITE,PFTR_CARRY,pftr_site_patched);return;
        }
        pftr_installed=1;
    } else if(!on && pftr_installed) {
        if(script_rng_slot_swap(PFTR_EVENT_DISPATCH_SLOT,(void*)pftr_dispatch,image+PFTR_EVENT_DISPATCH) &&
           pres_site_uninstall(PFTR_CARRY_SITE,PFTR_CARRY,pftr_site_patched)) pftr_installed=0;
    }
}
