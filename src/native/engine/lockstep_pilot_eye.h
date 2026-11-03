/* The presented eye (part of lockstep_pilot_present.h, on with it; presentation only, this client only).
 * Client k sees slot k's cockpit from the place where X4 seats a player in slot k's ship.
 *
 * What X4 does for its own player [F, 9.00]: 716670 connects the player to the cockpit room's player
 * slot (filter TemplateConnectionFilterByIsPlayerSlot) and its U::FirstPersonController (component
 * +0x200, vtable 0x2c3eec0) holds the eye there: the ship's UI-anchor connection of the control
 * position's group (con_uianchor, group pilotcontrol) expressed in the room. 0x75d110(ship, out, room,
 * &position) computes exactly that transform (filter TemplateConnectionFilterByIsUIAnchor, then
 * GetTransformRelativeTo); 0x8e8450 places the HUD's anchor with it. A save shows it: the player of the
 * scout 0ae79c sits at (0, -0.0383, -0.1493) in its cockpit room = con_uianchor - con_cockpit of
 * ship_arg_s_scout_01.
 * The camera in cockpit mode is the anchor's origin (Camera::GetOffset 0x97a300, mode 0, player in its
 * cockpit: 0x9fc670/0xd7d2e0 skip the head offset), composed up the LIVE chain (0x3db8a0: each link's
 * Positional::vfunc648 0x7461c0 = its movement controller's slot 6); the HUD's anchor and the render
 * walks go through the PUBLISHED view cache ([A]+0x28: a pointer to the link's local PosRot).
 *
 * The scope (lockstep_pilot_present.h) re-points the canonical player's parent to slot k's room but kept
 * its local pose, slot 1's seat in slot 1's room. For the same ship model that is slot k's seat too
 * (the scouts); for another model it is not: ship_arg_s_fighter_01 has its UI anchor at (0.006, -0.0707,
 * 0.2384) in its room, so slot 1's scout pose put the eye 0.39 m back and 3 cm up, behind the pilot
 * seat (see2: a dark wall with a lit edge, the HUD right).
 *
 * The correction, in the scope only, every byte back at its exit:
 *   D = slot k's seat after the inverse of slot 1's seat, both from 0x75d110 (taken once per tick
 *     outside any scope, canonical state, simulation RNG word held; refused unless slot 1's player
 *     sits in its ship's room or the ship itself and both seats are rigid). D = identity (the same
 *     model) changes nothing at all.
 *   live: the canonical player's movement controller, when it is a U::FirstPersonController, gets a
 *     copy of its vtable whose slot 6 answers X4's own pose with D applied (one CAS each way; another
 *     controller class is left alone and named, eye_controller=vtable:count, and nothing is applied);
 *   published: the view cache entry of the bank this thread reads points to a block holding the
 *     published pose with D applied (the original pointer back at the exit; a block the engine
 *     replaced meanwhile stays the engine's, counted lost).
 * The player's pose relative to its seat is X4's in both: the controller's own offsets, any head
 * movement, the w lanes. Nothing outside the scope sees it, and the simulation never runs in it.
 * Slot 1 on foot (native_gate_pilot_foot, lockstep_pilot_foot.h): the canonical player stands in a station room, its
 * pose there means nothing in slot k's room. Then the presented pose is slot k's seat itself (stage 'seat', from
 * 0x75d110 for slot k's ship only), the w lanes X4's: client k's eye stays at its own seat while slot 1 walks.
 * Slot k on foot (native_gate_pilot_foot_slots, lockstep_pilot_foot_slots.h, K2-9): the presented pose is P_k's own
 * FirstPersonController pose (its room is the scope's parent: P_k never leaves it), live and published (stage 'walk'):
 * d81ec0 on P_k's FPC reads only that FPC's fields once its first update ran, so nothing simulated is touched here.
 * Status (PILOT_INPUTS present row): eye=stage/scopes/poses/lost/dx,dy,dz (stage: on, same, skip:...,
 * off:...; dx..dz: D's translation in slot k's room, metres). */
#define PEYE_SEAT 0x75d110u           /* (component, out PosRot, relative to, &control position) -> found */
#define PEYE_SEAT_SIZE 0x142u
#define PEYE_FPC_VTABLE 0x2c3eec0u    /* U::FirstPersonController */
#define PEYE_FPC_SLOTS 49u
#define PEYE_FPC_POSE 0xd81ec0u       /* its slot 6: (this, out PosRot, time, overwrite); else out = out in its frame */
#define PEYE_FPC_POSE_SIZE 0x46fu
#define PEYE_POSE_SLOT 6u
#define PEYE_MOVEMENT 0x200u          /* Component: movement controller */
#define PEYE_VIEW_BLOCK 0x28u         /* view cache [A]: the published local PosRot, NULL = identity */
#define PEYE_POSITION 0x6ab8u         /* Player: control position */
#define PEYE_SAME_TRANSLATION 1e-4    /* m: a smaller D is the same seat */
#define PEYE_SAME_ROTATION 1e-5
#define PEYE_RIGID 1e-3               /* a seat's rows must be orthonormal to this */
typedef void (*peye_pose_fn)(void *,float *,double,unsigned char);
typedef unsigned char (*peye_seat_fn)(void *,float *,void *,const int *);
static void peye_pose(void *self,float *out,double time,unsigned char overwrite);
static peye_seat_fn peye_seat;
static peye_pose_fn peye_fpc_pose;
static void **peye_fpc_stock;                     /* the stock vtable */
static void *peye_vt[1+PEYE_FPC_SLOTS];           /* the column object, then the slots */
static __attribute__((aligned(16))) float peye_delta[16],peye_block[2][16];
static int peye_on,peye_vt_ready,peye_live,peye_published;
static int peye_seat_mode;                        /* slot 1 on foot: the presented pose is slot k's seat itself */
static unsigned char *peye_walk_fpc;              /* slot k on foot: its FPC, whose pose is presented */
static unsigned char *pfslot_walking_fpc(unsigned k);   /* lockstep_pilot_foot_slots.h */
static __attribute__((aligned(16))) float peye_seat_pose[16];
static unsigned peye_bank;
static const char *peye_stage="off";
static unsigned char *peye_key[4];                /* ship 1, room 1, ship k, room k of D */
static unsigned char *peye_ctl,*peye_block_saved,*volatile *peye_block_at;
static volatile LONG64 peye_updates,peye_scopes,peye_poses,peye_lost,peye_other;
static unsigned peye_other_vtable;
/* X4's composition of PosRots (16 floats: translation, then three rows; row vectors): the child's pose
 * in the parent's frame, all four lanes as X4's SSE code computes them (0x3dde10, 0xd81ec0). */
static void peye_compose(const float child[16],const float parent[16],float out[16]) {
    float r[16];
    for(unsigned j=0;j<4;j++) {
        float x=child[j<3?0:3],y=child[j<3?1:3],z=child[j<3?2:3];
        r[j]=x*parent[4+j]+y*parent[8+j]+z*parent[12+j]+parent[j];
    }
    for(unsigned i=1;i<4;i++) for(unsigned j=0;j<4;j++) r[4*i+j]=child[4*i]*parent[4+j]+child[4*i+1]*parent[8+j]+child[4*i+2]*parent[12+j];
    memcpy(out,r,sizeof(r));
}
static void peye_identity(float m[16]) {memset(m,0,16*sizeof(float));m[4]=m[9]=m[14]=1.0f;}
/* A pose with D applied; the w lanes stay the pose's own. */
static void peye_apply(const float pose[16],float out[16]) {
    float r[16];
    if(peye_seat_mode) memcpy(r,peye_seat_pose,sizeof(r));
    else peye_compose(pose,peye_delta,r);
    for(unsigned i=0;i<4;i++) r[4*i+3]=pose[4*i+3];
    memcpy(out,r,sizeof(r));
}
/* A seat as X4 returns it: finite, orthonormal rows. */
static int peye_rigid(const float s[16]) {
    for(unsigned i=0;i<16;i++) if(!isfinite(s[i])) return 0;
    for(unsigned a=1;a<4;a++) for(unsigned b=1;b<4;b++) {
        double dot=0;for(unsigned k=0;k<3;k++) dot+=(double)s[4*a+k]*(double)s[4*b+k];
        if(fabs(dot-(a==b?1.0:0.0))>PEYE_RIGID) return 0;
    }
    return 1;
}
/* D = slot k's seat after the inverse of slot 1's (row vectors: v*R1^T*Rk + (Tk - T1*R1^T*Rk)); 1 when it moves
 * the eye, 0 when it is the same seat (D then identity). */
static int peye_delta_from(const float s1[16],const float sk[16],float d[16]) {
    double r1[3][3],rk[3][3],t1[3],tk[3],dr[3][3],it[3],dt[3];int moves=0;
    for(unsigned i=0;i<3;i++) {t1[i]=s1[i];tk[i]=sk[i];for(unsigned k=0;k<3;k++) {r1[i][k]=s1[4*(i+1)+k];rk[i][k]=sk[4*(i+1)+k];}}
    for(unsigned j=0;j<3;j++) it[j]=-(t1[0]*r1[j][0]+t1[1]*r1[j][1]+t1[2]*r1[j][2]);   /* -T1*R1^T */
    for(unsigned i=0;i<3;i++) for(unsigned j=0;j<3;j++) dr[i][j]=r1[0][i]*rk[0][j]+r1[1][i]*rk[1][j]+r1[2][i]*rk[2][j];
    for(unsigned j=0;j<3;j++) dt[j]=it[0]*rk[0][j]+it[1]*rk[1][j]+it[2]*rk[2][j]+tk[j];
    memset(d,0,16*sizeof(float));
    for(unsigned j=0;j<3;j++) {
        d[j]=(float)dt[j];if(fabs(dt[j])>PEYE_SAME_TRANSLATION) moves=1;
        for(unsigned i=0;i<3;i++) {d[4*(i+1)+j]=(float)dr[i][j];if(fabs(dr[i][j]-(i==j?1.0:0.0))>PEYE_SAME_ROTATION) moves=1;}
    }
    if(!moves) peye_identity(d);
    return moves;
}
static int peye_set(const char *stage,int on) {peye_stage=stage;peye_on=on;if(!on) {peye_identity(peye_delta);peye_seat_mode=0;peye_walk_fpc=NULL;}return on;}
/* Slot 1 on foot: slot k's seat alone (canonical state, simulation RNG word held, as below). */
static int peye_update_seat(const pslot_slot *s,unsigned char *room1) {
    int posk=0;__attribute__((aligned(16))) float b[16];unsigned char found=0;int fault=0;
    if(!read_local(s->player+PEYE_POSITION,&posk,4)) return peye_set("skip:position",0);
    memset(peye_key,0,sizeof(peye_key));
    uint64_t word=0,*rng=pres_rng_hold(&word);
    peye_identity(b);
    __try {found=peye_seat(s->ship,b,s->room,&posk);}
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
    pres_rng_back(rng,word);
    if(fault) return peye_set("skip:fault",0);
    if(!found) return peye_set("skip:no_seat",0);
    if(!peye_rigid(b)) return peye_set("skip:not_rigid",0);
    memcpy(peye_seat_pose,b,sizeof(b));peye_seat_mode=1;peye_identity(peye_delta);
    peye_key[0]=pilot_slots[0].object;peye_key[1]=room1;peye_key[2]=s->ship;peye_key[3]=s->room;
    return peye_set("seat",1);
}
/* The vtable copy: built once from the stock vtable (read only, the same for the whole process). */
static void peye_prepare(void) {
    if(peye_vt_ready || !peye_fpc_stock) return;
    peye_vt[0]=peye_fpc_stock[-1];
    for(unsigned i=0;i<PEYE_FPC_SLOTS;i++) peye_vt[1+i]=peye_fpc_stock[i];
    peye_vt[1+PEYE_POSE_SLOT]=(void*)peye_pose;
    peye_vt_ready=1;
}
/* D for this tick (frame thread, no scope open: the canonical player as the simulation has it). Taken every
 * tick: a seat is static, but a slot's ship may be a new one after a death. */
static int peye_update(const pslot_slot *s) {
    unsigned char *p1=pslot_p1,*ship1=pilot_slots[0].object,*room1=NULL,*cockpit=NULL;int pos1=0,posk=0;
    __attribute__((aligned(16))) float a[16],b[16];
    InterlockedIncrement64(&peye_updates);
    peye_seat_mode=0;peye_walk_fpc=NULL;
    if(!peye_seat || !peye_fpc_stock || !peye_fpc_pose) return peye_set("off:functions",0);
    peye_prepare();
    {   unsigned char *walker=pfslot_walking_fpc(pres_slot);   /* slot k on foot: its own FPC's pose (K2-9) */
        if(walker) {
            if(!s->seated || !s->ship || !s->room || !p1 || !read_local(p1+PRES_PARENT,&room1,8) || !room1) return peye_set("skip:parent",0);
            peye_identity(peye_delta);memset(peye_key,0,sizeof(peye_key));
            peye_key[0]=ship1;peye_key[1]=room1;peye_key[2]=s->ship;peye_key[3]=s->room;peye_walk_fpc=walker;
            return peye_set("walk",1);
        }
    }
    if(!s->seated || !s->ship || !s->room || !s->player || !p1 || !ship1 || !read_local(p1+PRES_PARENT,&room1,8) || !room1)
        return peye_set("skip:parent",0);
    if(!read_local(ship1+PSLOT_ROOM,&cockpit,8)) cockpit=NULL;
    if(room1!=ship1 && room1!=cockpit) return pfoot_armed?peye_update_seat(s,room1):peye_set("skip:slot1_seat",0);
    if(!read_local(p1+PEYE_POSITION,&pos1,4) || !read_local(s->player+PEYE_POSITION,&posk,4)) return peye_set("skip:position",0);
    memset(peye_key,0,sizeof(peye_key));
    uint64_t word=0,*rng=pres_rng_hold(&word);unsigned char found1=0,foundk=0;int fault=0;
    peye_identity(a);peye_identity(b);
    __try {found1=peye_seat(ship1,a,room1,&pos1);foundk=peye_seat(s->ship,b,s->room,&posk);}
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
    pres_rng_back(rng,word);
    if(fault) return peye_set("skip:fault",0);
    if(!found1 || !foundk) return peye_set("skip:no_seat",0);
    if(!peye_rigid(a) || !peye_rigid(b)) return peye_set("skip:not_rigid",0);
    __attribute__((aligned(16))) float d[16];int moves=peye_delta_from(a,b,d);
    memcpy(peye_delta,d,sizeof(d));
    peye_key[0]=ship1;peye_key[1]=room1;peye_key[2]=s->ship;peye_key[3]=s->room;
    return peye_set(moves?"on":"same",moves);
}
/* The live pose: X4's own answer, then D, only for the presented controller in the scope. */
static void peye_pose(void *self,float *out,double time,unsigned char overwrite) {
    if(!(time>=0.0) || self!=(void*)peye_ctl || !peye_live || !InterlockedCompareExchange(&pres_open,0,0)) {peye_fpc_pose(self,out,time,overwrite);return;}
    __attribute__((aligned(16))) float local[16];
    if(peye_walk_fpc) peye_fpc_pose(peye_walk_fpc,local,time,1);   /* slot k on foot: its own pose in its room */
    else {peye_fpc_pose(self,local,time,1);peye_apply(local,local);}
    if(overwrite) memcpy(out,local,sizeof(local));
    else {float r[16];peye_compose(out,local,r);memcpy(out,r,sizeof(r));}
    InterlockedIncrement64(&peye_poses);
}
/* In pres_enter, after the identity: both poses of the canonical player p1 (view = its cache entry of this bank). */
static void peye_enter(unsigned char *p1,unsigned char *view,unsigned bank,const pslot_slot *s,unsigned char *room1) {
    peye_live=peye_published=0;
    if(!peye_on || !peye_vt_ready || bank>1 || peye_key[1]!=room1 || peye_key[2]!=s->ship || peye_key[3]!=s->room) return;
    unsigned char *ctl=NULL;void *vt=NULL;
    if(read_local(p1+PEYE_MOVEMENT,&ctl,8) && ctl && read_local(ctl,&vt,8)) {
        if(vt==(void*)peye_fpc_stock) {
            peye_ctl=ctl;
            if(InterlockedCompareExchangePointer((void*volatile*)ctl,&peye_vt[1],vt)==vt) peye_live=1;
        } else if(vt!=(void*)&peye_vt[1]) {InterlockedIncrement64(&peye_other);peye_other_vtable=(unsigned)((uintptr_t)vt-(uintptr_t)image);}
    }
    /* Both poses or neither: the camera (live) and the HUD's anchor (published) must agree. */
    if(!peye_live) return;
    unsigned char *volatile *at=(unsigned char *volatile*)(view+PEYE_VIEW_BLOCK);
    unsigned char *published=*at;__attribute__((aligned(16))) float pose[16];
    if(published) memcpy(pose,published,sizeof(pose));
    else peye_identity(pose);
    double now=0;
    if(peye_walk_fpc && pnat_fn.time(&now)) peye_fpc_pose(peye_walk_fpc,peye_block[bank],now,1);
    else peye_apply(pose,peye_block[bank]);
    peye_block_at=at;peye_block_saved=published;peye_bank=bank;
    *at=(unsigned char*)peye_block[bank];peye_published=1;
    InterlockedIncrement64(&peye_scopes);
}
/* In pres_leave, before the identity goes back. */
static void peye_leave(void) {
    if(peye_published) {
        if(*peye_block_at==(unsigned char*)peye_block[peye_bank]) *peye_block_at=peye_block_saved;
        else InterlockedIncrement64(&peye_lost);
        peye_published=0;
    }
    if(peye_live) {
        peye_live=0;
        if(InterlockedCompareExchangePointer((void*volatile*)peye_ctl,(void*)peye_fpc_stock,&peye_vt[1])!=(void*)&peye_vt[1]) InterlockedIncrement64(&peye_lost);
    }
}
static void peye_reset(void) {
    peye_on=0;peye_seat_mode=0;peye_walk_fpc=NULL;peye_live=peye_published=0;peye_stage="off";memset(peye_key,0,sizeof(peye_key));peye_ctl=NULL;
    peye_updates=peye_scopes=peye_poses=peye_lost=peye_other=0;peye_other_vtable=0;peye_identity(peye_delta);
}
static int peye_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity," eye=%s/%lld/%lld/%lld/%.3f,%.3f,%.3f",peye_stage,(long long)peye_scopes,(long long)peye_poses,(long long)peye_lost,
        (double)peye_delta[0],(double)peye_delta[1],(double)peye_delta[2]);
    if(n>0 && (size_t)n<capacity && peye_other) {
        int more=snprintf(out+n,capacity-(size_t)n," eye_controller=%llx:%lld",(unsigned long long)peye_other_vtable,(long long)peye_other);
        n=more>0?n+more:-1;
    }
    return n>0 && (size_t)n<capacity?n:0;
}
