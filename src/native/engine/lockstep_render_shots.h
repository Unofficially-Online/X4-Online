/* Bullets drawn moving between the steps, DEFAULT ON (Windows gate, stop reason 231). Display only.
 *   native_gate_render_shots = true ('lead') | 'motion' | 'probe'
 *       motion  every live bullet of the camera's zone drawn at the world's display offset (stage 3's rdisp_s_world)
 *               along its motion of the last two steps;
 *       lead    and a bullet of a ship the own-ship predictor draws ahead (any slot's) leaves that ship's drawn guns:
 *               shifted by the ship's lead, fading with the bullet's age (RSHOT_TAU);
 *       probe   diagnostic (the quick check): in every render-only frame one live bullet of the zone 5 m aside along its
 *               own x axis through rdisp_override, nothing else (a tracer that jumps sideways in a PrintWindow burst
 *               proves that X4 draws bullets from their view cache's pose block).
 * Needs native_gate_render_motion (stage 3: its offset, its zone and its step sample) and the bullet Move wrapper
 * (lockstep_bullet.h, native_gate_bullet_rng or native_gate_bullet_diag).
 * Found 24 Nov (player 2: projectiles stutter). A U::Bullet (vtable 0x2c42ac8) moves only in its Move (vtable +0x1510,
 * 0xd4f090, from the zone update in slot 3, never in a render-only frame) and is drawn by Positional::vfunc693 0x751770
 * (vtable +0x15a8), which composes its model-view through the render walk 0x3de630 from its view cache's pose block
 * (vc[A]+0x28, as every component lockstep_render_display.h swaps). Bullets are in no zone's children vector (hot join
 * j1-29: 1,428 zones and 14,044 children walked, none), so stage 3 never moved one: every bullet stood still in the
 * render-only frames and jumped two frames' worth at the next step (30-170 m at 1-5 km/s), out of step with its shooter
 * (drawn at the world's offset), and an own shot came out of the simulated guns, the predictor's lead behind the drawn ship.
 *
 * So: the Move wrapper notes every bullet it moves (gate memory). At the step frame's slot 10 entry (after rdisp_sample,
 * stage 3's step sample) each noted bullet that is live (Find's reading of the component registry 0x6cf1500 by its runtime
 * ID, mode 4: generation, live nibble, the pointer is the object; destruction is posted during Move), a U::Bullet, cached
 * in the camera's zone (vc[A]+0 is stage 3's zone) with an aligned pose block is sampled (pose, first sample's tick,
 * the shooter's slot ship: bullet+0x2a0 the weapon, up its parents +0x70) and matched with the previous step by pointer
 * and ID: its motion N-1 -> N (rdisp_motion_of; a bullet flies straight: the translation alone, the rotation stays; more
 * than 1000 m a step is a snap, drawn as simulated). In every slot 10 call after rdisp_others_enter, every sampled bullet
 * that is still live and whose block pointer and pose are still the sampled ones (else stale=/gone=) gets a private copy
 * of the engine's 0x70 bytes with the pose advanced to the world's display time (rdisp_s_world: extrapolate 0..1,
 * interpolate -1 in a step frame, about -0.5 in a render-only frame), never before its first sample (clamped=), plus the
 * lead; the view cache's +0x28 pointer is swapped to it by compare-and-swap and swapped back the same way after the
 * original (also on unwind, the wrapper's __finally). A pointer that is not ours when it goes back stops the gate (231
 * why=block_foreign, the foreign pointer stays). The engine's block and every simulation field stay as they are; the
 * bullet's own readers (UpdateFlight's hit ray 0x3dde10, the hit claims) run in slots 3 and 6, never inside slot 10.
 * Pins (the gate's first tick; 231 why=pins): rdisp's, Move 0xd4f090, Find 0xced70, the render queue 0x751770 (.pdata
 * extents) and the U::Bullet render slot; the Move slot must hold the gate's wrapper (231 why=bullet_move). Nested: 231.
 * Status RENDER_SHOTS (gate.render_shots_status). */
#define RSHOT_STOP_REASON 231u
#define RSHOT_VTABLE 0x2c42ac8u          /* U::Bullet */
#define RSHOT_RENDER_SLOT 0x15a8u        /* vfunc693: Positional's render queue 0x751770 */
#define RSHOT_RENDER 0x751770u
#define RSHOT_SHOOTER 0x2a0u             /* the weapon that fired it */
#define RSHOT_PARENT 0x70u
#define RSHOT_REGISTRY 0x6cf1500u        /* *: the component registry (Find 0xced70) */
#define RSHOT_REGISTRY_BLOCKS 0x58u
#define RSHOT_NOTES 8192u
#define RSHOT_ITEMS 2048u
#define RSHOT_HASH_BITS 13u
#define RSHOT_HASH (1u<<RSHOT_HASH_BITS)
#define RSHOT_GAP 4u                     /* steps between two samples of a bullet that still give its motion */
#define RSHOT_TAU 0.15                   /* s: how fast an own shot's lead fades */
#define RSHOT_PROBE_M 5.0
enum {RSHOT_OFF,RSHOT_LEAD,RSHOT_MOTION,RSHOT_PROBE};
typedef struct {
    unsigned char *object,*block;uint64_t id,tick,first;
    float pose[16];double dp[3],w[3];
    unsigned own;                        /* 1..PILOT_SLOTS: fired by that slot's ship; 0 */
    unsigned char motion,snap;
} rshot_item;
static int rshot_mode,rshot_armed,rshot_pending,rshot_pins_checked,rshot_restored=1;
static volatile LONG rshot_active;
static volatile LONG64 rshot_failures;
static volatile int rshot_open;
static volatile DWORD rshot_open_thread;
static const char *rshot_stage="off",*rshot_why="-";
/* ---- the Move wrapper's notes (MoveWorker, slot 3) ---- */
static unsigned char *rshot_notes[RSHOT_NOTES];
static unsigned rshot_note_count;
/* ---- the step samples ---- */
static rshot_item rshot_items[2][RSHOT_ITEMS];
static uint32_t rshot_hash[2][RSHOT_HASH];
static unsigned rshot_count[2],rshot_gen;
static int rshot_valid,rshot_window;
static uint64_t rshot_tick;
static double rshot_s;
/* ---- slot 10's swaps ---- */
static __declspec(align(64)) unsigned char rshot_blocks[RSHOT_ITEMS][RDISP_BLOCK_BYTES];
static rdisp_swap rshot_swaps[RSHOT_ITEMS];
static unsigned rshot_swap_count;
/* ---- accounting ---- */
static uint64_t rshot_noted,rshot_notes_capped,rshot_samples,rshot_sample_skip,rshot_items_max,rshot_capped,rshot_dead,rshot_not_bullet;
static uint64_t rshot_elsewhere,rshot_no_block,rshot_first,rshot_moved,rshot_snaps,rshot_reparented,rshot_frames,rshot_swapped;
static uint64_t rshot_restores,rshot_stale,rshot_gone,rshot_clamped,rshot_own,rshot_probe,rshot_probe_refused,rshot_full,rshot_foreign,rshot_nested;
static uint64_t rshot_sample_us,rshot_sample_us_sum,rshot_sample_us_max,rshot_apply_us,rshot_apply_us_sum,rshot_apply_us_max;
static double rshot_lead_last,rshot_lead_max,rshot_disp_max;
static const struct {unsigned rva,size;const char *sha;} rshot_pin_table[]={
    {0xd4f090u,0x1acu,"1690c279aeac79136f9234dd4f6144e59e653bb06a3c6dc101d3b4b67b397a5f"},   /* U::Bullet Move */
    {0xced70u,0xb8u,"3209f7be0616de6d0ecc20ce3cfd82fd16d0524c09635d86995210598b0a5f36"},     /* ComponentRegistry::Find */
    {RSHOT_RENDER,0x9deu,"f09e3b9aa5fb5ba4aaecd064b37a2bbfa6a56805eb110b715e8fb58706a803b2"}}; /* Positional render queue */
static void bullet_move(void *self);
static int rshot_pins(void) {
    if(!rshot_pins_checked) {
        int ok=image!=NULL;unsigned char *at=NULL;
        for(unsigned i=0;ok && i<sizeof(rshot_pin_table)/sizeof(*rshot_pin_table);i++) ok=hash_ok(image+rshot_pin_table[i].rva,rshot_pin_table[i].size,rshot_pin_table[i].sha);
        ok=ok && sample_read(image+RSHOT_VTABLE+RSHOT_RENDER_SLOT,&at,8) && at==image+RSHOT_RENDER;
        rshot_pins_checked=ok?1:-1;
    }
    return rshot_pins_checked==1;
}
static int rshot_fail(const char *why) {
    if(!strcmp(rshot_why,"-")) rshot_why=why;
    rshot_stage="failed";InterlockedIncrement64(&rshot_failures);return 0;
}
/* The Move wrapper (lockstep_bullet.h bullet_move, the MoveWorker in slot 3): this bullet moves in this step. */
static void rshot_note(void *bullet) {
    if(!rshot_armed || !bullet) return;
    if(rshot_note_count>=RSHOT_NOTES) {++rshot_notes_capped;return;}
    rshot_notes[rshot_note_count++]=(unsigned char*)bullet;++rshot_noted;
}
/* Find's reading (0xced70, mode 4) by guarded reads: the object of a live runtime ID; NULL when there is none. */
static unsigned char *rshot_live(uint64_t id) {
    unsigned char *map=NULL,*block=NULL,*object=NULL,bucket[32];uint32_t flags;uint16_t generation;
    const uint32_t slot=(uint32_t)(id&0x1ffffffu);
    if(!slot || !image || !sample_read(image+RSHOT_REGISTRY,&map,8) || !map) return NULL;
    const uint32_t index=slot-1u,b=index>>20,i=index&0xfffffu,k=i/3u,e=i%3u;
    if(b>=32u || !sample_read(map+RSHOT_REGISTRY_BLOCKS+8u*b,&block,8) || !block || !sample_read(block+(size_t)k*32u,bucket,32)) return NULL;
    memcpy(&flags,bucket+0x1c,4);memcpy(&generation,bucket+0x18+2u*e,2);
    if(((flags>>(16u+4u*e))&5u)!=5u || generation!=(uint16_t)(id>>25)) return NULL;
    memcpy(&object,bucket+8u*e,8);return object;
}
static uint32_t rshot_slot_of(const void *p) {
    uint64_t x=((uint64_t)(uintptr_t)p>>4)*0x9e3779b97f4a7c15ULL;return (uint32_t)(x>>(64-RSHOT_HASH_BITS));
}
static int rshot_lookup(unsigned gen,const void *object) {
    uint32_t h=rshot_slot_of(object);
    for(unsigned probe=0;probe<RSHOT_HASH;probe++,h=(h+1)&(RSHOT_HASH-1)) {
        uint32_t v=rshot_hash[gen][h];
        if(!v) return -1;
        if(rshot_items[gen][v-1].object==object) return (int)(v-1);
    }
    return -1;
}
static void rshot_insert(unsigned gen,unsigned index) {
    uint32_t h=rshot_slot_of(rshot_items[gen][index].object);
    while(rshot_hash[gen][h]) h=(h+1)&(RSHOT_HASH-1);
    rshot_hash[gen][h]=index+1;
}
/* The slot (1..N) whose ship fired it: the weapon and its parents up to the ship. */
static unsigned rshot_shooter_slot(unsigned char *bullet) {
    unsigned char *at=NULL;
    if(!sample_read(bullet+RSHOT_SHOOTER,&at,8)) return 0;
    for(unsigned depth=0;at && depth<6;depth++) {
        for(unsigned j=0;j<pilot_slot_count && j<PILOT_SLOTS;j++) if(pilot_slots[j].object && pilot_slots[j].object==at) return j+1;
        if(!sample_read(at+RSHOT_PARENT,&at,8)) return 0;
    }
    return 0;
}
static void rshot_reset(void) {rshot_valid=0;rshot_count[0]=rshot_count[1]=0;}
/* Slot 10 entry, right after rdisp_sample (stage 3's offset and, in a step frame, its sample). */
static void rshot_sample(void) {
    rshot_window=0;
    if(!rshot_armed || !InterlockedCompareExchange(&rshot_active,0,0) || !stats.active || !rdisp_window) {
        if(!rdisp_render_frame) rshot_note_count=0;
        return;
    }
    rshot_window=1;rshot_s=rdisp_s_world;
    if(rdisp_render_frame) {
        /* The quick check: one live bullet of the zone 5 m aside (rdisp_override, swapped by rdisp_enter). */
        if(rshot_mode==RSHOT_PROBE && rshot_valid) {
            const rshot_item *items=rshot_items[rshot_gen];int done=0;
            for(unsigned i=0;i<rshot_count[rshot_gen] && !done;i++) {
                const rshot_item *it=&items[i];unsigned char *block=NULL;float now[16],pose[12];
                if(rshot_live(it->id)!=it->object || !sample_read(rdisp_vc(it->object,rdisp_bank)+RDISP_VC_BLOCK,&block,8) || block!=it->block ||
                   !sample_read(block,now,64) || memcmp(now,it->pose,64)) continue;
                for(unsigned r=0;r<4;r++) memcpy(pose+3*r,now+4*r,12);
                for(unsigned j=0;j<3;j++) pose[j]+=(float)(RSHOT_PROBE_M*now[4+j]);
                if(rdisp_override(it->object,pose)) {++rshot_probe;done=1;} else ++rshot_probe_refused;
            }
        }
        return;
    }
    /* A step frame: this step's noted bullets against the previous step's sample. */
    LARGE_INTEGER t0,t1;QueryPerformanceCounter(&t0);
    const unsigned next=rshot_gen^1,bank=rdisp_bank;unsigned n=0;rshot_item *items=rshot_items[next];
    memset(rshot_hash[next],0,sizeof(rshot_hash[next]));rshot_count[next]=0;
    if(!rdisp_items_valid || !rdisp_zone || rdisp_last_tick!=stats.issued || bank>1) {
        ++rshot_sample_skip;rshot_note_count=0;rshot_valid=0;rshot_gen=next;return;
    }
    ++rshot_samples;
    for(unsigned c=0;c<rshot_note_count;c++) {
        unsigned char *object=rshot_notes[c],*parent=NULL;
        if(rshot_lookup(next,object)>=0) continue;               /* noted twice */
        if(n>=RSHOT_ITEMS) {++rshot_capped;break;}
        rshot_item *it=&items[n];uint64_t id=0;
        if(!sample_read(object+RDISP_ID,&id,8) || rshot_live(id)!=object) {++rshot_dead;continue;}
        if(rradar_vt(object)!=RSHOT_VTABLE) {++rshot_not_bullet;continue;}
        unsigned char *vc=rdisp_vc(object,bank);
        if(!sample_read(vc,&parent,8) || parent!=rdisp_zone) {++rshot_elsewhere;continue;}
        if(!sample_read(vc+RDISP_VC_BLOCK,&it->block,8) || !it->block || ((uintptr_t)it->block&15) || !sample_read(it->block,it->pose,64) ||
           !rdisp_finite(it->pose)) {++rshot_no_block;continue;}
        it->object=object;it->id=id;it->tick=stats.issued;it->first=stats.issued;it->motion=it->snap=0;it->own=0;
        memset(it->dp,0,sizeof(it->dp));memset(it->w,0,sizeof(it->w));
        int k=rshot_valid?rshot_lookup(rshot_gen,object):-1;
        if(k>=0) {
            const rshot_item *p=&rshot_items[rshot_gen][k];
            if(p->id!=id) {++rshot_reparented;k=-1;}
            else {
                it->first=p->first;it->own=p->own;
                const uint64_t gap=stats.issued-p->tick;
                if(gap>=1 && gap<=RSHOT_GAP && memcmp(p->pose,it->pose,64)) {
                    rdisp_xf a,b;rdisp_xf_from(p->pose,&a);rdisp_xf_from(it->pose,&b);rdisp_motion_of(&a,&b,it->dp,it->w);
                    for(unsigned j=0;j<3;j++) {it->dp[j]/=(double)gap;it->w[j]/=(double)gap;}
                    if(rdisp_norm3(it->dp)>RDISP_SNAP_POSITION || rdisp_norm3(it->w)>RDISP_SNAP_ANGLE) {it->snap=1;++rshot_snaps;}
                    else {it->motion=1;++rshot_moved;}
                }
            }
        }
        if(k<0) {++rshot_first;it->own=rshot_shooter_slot(object);}
        rshot_insert(next,n);++n;
    }
    rshot_note_count=0;rshot_count[next]=n;rshot_gen=next;rshot_valid=1;rshot_tick=stats.issued;
    if(n>rshot_items_max) rshot_items_max=n;
    QueryPerformanceCounter(&t1);
    rshot_sample_us=rf_us(t1.QuadPart-t0.QuadPart);rshot_sample_us_sum+=rshot_sample_us;if(rshot_sample_us>rshot_sample_us_max) rshot_sample_us_max=rshot_sample_us;
}
/* A ship's lead over the world's timeline in this call: the drawn pose (the predictor's) minus the engine's advanced by s
 * along its step motion (stage 3's sample of it); 0 when that slot's ship is not drawn ahead in the camera's zone. */
static int rshot_lead_of(unsigned own,double s,double lead[3]) {
    const unsigned j=own-1;unsigned char *ship=j<PILOT_SLOTS?pilot_slots[j].object:NULL,*parent=NULL,*block=NULL;float sim[16];
    if(!opred_open || !ship || !opred_open_at[j] || opred_open_object[j]!=ship || !sample_read(rdisp_vc(ship,rdisp_bank),&parent,8) ||
       parent!=rdisp_zone || !sample_read((const void*)opred_open_at[j],&block,8) || block!=(unsigned char*)opred_blocks[j] ||
       !sample_read(opred_open_saved[j],sim,64) || !rdisp_finite(sim)) return 0;
    double dp[3]={0,0,0};int k=rdisp_items_valid?rdisp_lookup(rdisp_gen,ship):-1;
    if(k>=0 && rdisp_items[rdisp_gen][k].motion && !rdisp_items[rdisp_gen][k].snap) memcpy(dp,rdisp_items[rdisp_gen][k].dp,sizeof(dp));
    for(unsigned i=0;i<3;i++) lead[i]=(double)opred_blocks[j][i]-((double)sim[i]+s*dp[i]);
    return isfinite(lead[0]) && isfinite(lead[1]) && isfinite(lead[2]) && rdisp_norm3(lead)<=RDISP_SNAP_POSITION;
}
static int rshot_swap_in(unsigned char *vc,unsigned char *engine,const float pose[16]) {
    if(rshot_swap_count>=RSHOT_ITEMS) {++rshot_full;return 0;}
    unsigned char *mine=rshot_blocks[rshot_swap_count];
    if(!sample_read(engine,mine,RDISP_BLOCK_BYTES)) return 0;
    for(unsigned i=0;i<4;i++) memcpy(mine+16*i,pose+4*i,12);
    if(InterlockedCompareExchangePointer((void*volatile*)(vc+RDISP_VC_BLOCK),mine,engine)!=engine) return 0;
    rshot_swaps[rshot_swap_count].slot=vc+RDISP_VC_BLOCK;rshot_swaps[rshot_swap_count].engine=engine;rshot_swaps[rshot_swap_count].mine=mine;
    ++rshot_swap_count;return 1;
}
/* Slot 10, after rdisp_others_enter, before the original: 1 when a bullet is drawn moved. */
static int rshot_enter(void) {
    const int window=rshot_window;rshot_window=0;rshot_swap_count=0;
    if(!window || !rshot_armed || !InterlockedCompareExchange(&rshot_active,0,0) || !stats.active || !rshot_valid || rshot_mode==RSHOT_PROBE) return 0;
    if(rshot_open) {++rshot_nested;rshot_fail("nested");return 0;}
    LARGE_INTEGER t0,t1;QueryPerformanceCounter(&t0);
    const double s=rshot_s;const unsigned count=rshot_count[rshot_gen];const rshot_item *items=rshot_items[rshot_gen];
    double leads[PILOT_SLOTS][3];int lead_ok[PILOT_SLOTS];
    for(unsigned j=0;j<PILOT_SLOTS;j++) lead_ok[j]=rshot_mode==RSHOT_LEAD && rshot_lead_of(j+1,s,leads[j]);
    rshot_open_thread=GetCurrentThreadId();rshot_open=1;
    for(unsigned i=0;i<count;i++) {
        const rshot_item *it=&items[i];
        const int lead=it->own && lead_ok[it->own-1];
        if(!(it->motion && s!=0) && !lead) continue;
        unsigned char *vc=rdisp_vc(it->object,rdisp_bank),*block=NULL;float now[16],shown[16];
        if(rshot_live(it->id)!=it->object) {++rshot_gone;continue;}
        if(!sample_read(vc+RDISP_VC_BLOCK,&block,8) || block!=it->block || !sample_read(block,now,64) || memcmp(now,it->pose,64)) {++rshot_stale;continue;}
        /* Never before its first sample. */
        double se=s;const double earliest=-(double)(rshot_tick-it->first);
        if(se<earliest) {se=earliest;++rshot_clamped;}
        rdisp_xf b,d;rdisp_xf_from(it->pose,&b);
        if(it->motion) rdisp_advance(&b,it->dp,it->w,se,&d);else d=b;
        double moved=it->motion?fabs(se)*rdisp_norm3(it->dp):0.0;
        if(lead) {
            const double age=((double)(rshot_tick-it->first)+se)/(gate_tick_hz?(double)gate_tick_hz:30.0),fade=exp(-(age>0?age:0)/RSHOT_TAU);
            double shift[3];for(unsigned j=0;j<3;j++) {shift[j]=fade*leads[it->own-1][j];d.t[j]+=shift[j];}
            rshot_lead_last=rdisp_norm3(shift);if(rshot_lead_last>rshot_lead_max) rshot_lead_max=rshot_lead_last;
            ++rshot_own;
        }
        if(moved>rshot_disp_max) rshot_disp_max=moved;
        memcpy(shown,it->pose,64);rdisp_xf_to(&d,shown);
        if(rshot_swap_in(vc,block,shown)) ++rshot_swapped;else ++rshot_stale;
    }
    QueryPerformanceCounter(&t1);rshot_apply_us=rf_us(t1.QuadPart-t0.QuadPart);
    if(!rshot_swap_count) {rshot_open=0;return 0;}
    ++rshot_frames;rshot_stage="live";
    return 1;
}
/* After the original (also on unwind): every pointer back by compare-and-swap; one that is not ours stops the gate. */
static void rshot_leave(int entered) {
    if(!entered) return;
    LARGE_INTEGER t0,t1;QueryPerformanceCounter(&t0);
    for(unsigned i=rshot_swap_count;i-->0;) {
        rdisp_swap *w=&rshot_swaps[i];
        if(InterlockedCompareExchangePointer((void*volatile*)w->slot,w->engine,w->mine)!=w->mine) {++rshot_foreign;rshot_fail("block_foreign");}
        else ++rshot_restores;
    }
    rshot_swap_count=0;rshot_open=0;
    QueryPerformanceCounter(&t1);rshot_apply_us+=rf_us(t1.QuadPart-t0.QuadPart);
    rshot_apply_us_sum+=rshot_apply_us;if(rshot_apply_us>rshot_apply_us_max) rshot_apply_us_max=rshot_apply_us;
}
/* ---- arming, the gate's checks, status ---- */
static int rshot_mode_of(const char *text,size_t length) {
    if(!text) return RSHOT_OFF;
    if(length==4 && !memcmp(text,"lead",4)) return RSHOT_LEAD;
    if(length==6 && !memcmp(text,"motion",6)) return RSHOT_MOTION;
    if(length==5 && !memcmp(text,"probe",5)) return RSHOT_PROBE;
    return RSHOT_OFF;
}
static const char *rshot_mode_name(int mode) {return mode==RSHOT_LEAD?"lead":mode==RSHOT_MOTION?"motion":mode==RSHOT_PROBE?"probe":"off";}
/* gate.render_shots(mode): after gate.render_motion, before the start; afterwards the same value only. Reads no engine memory;
 * the gate's first tick checks the pins and the Move wrapper and activates it (rshot_check). */
static int rshot_arm(int mode,int started) {
    if(mode!=RSHOT_LEAD && mode!=RSHOT_MOTION && mode!=RSHOT_PROBE) return 0;
    if(started) return rshot_armed && rshot_mode==mode;
    if(!rdisp_motion_mode || rshot_pins_checked<0) {rshot_stage="refused";return 0;}
    rshot_mode=mode;rshot_armed=1;rshot_pending=1;rshot_restored=1;rshot_stage="pending";rshot_why="-";rshot_failures=0;
    rshot_reset();rshot_note_count=0;
    InterlockedExchange(&rshot_active,0);return 1;
}
static int rshot_check(void) {
    if(!rshot_armed) return 1;
    if(rshot_pending) {
        rshot_pending=0;
        if(!image || !rdisp_pins() || !rshot_pins()) return rshot_fail("pins");
        unsigned char *at=NULL;
        if(!(bullet_installed&2) || !sample_read(image+bullet_entries[1],&at,8) || at!=(unsigned char*)(void*)bullet_move) return rshot_fail("bullet_move");
        rshot_stage="armed";InterlockedExchange(&rshot_active,1);
    }
    return !InterlockedCompareExchange64(&rshot_failures,0,0);
}
static void rshot_restore(void) {
    InterlockedExchange(&rshot_active,0);rshot_pending=0;
    if(!rshot_armed) return;
    for(unsigned i=0;i<200 && rshot_open && GetCurrentThreadId()!=rshot_open_thread;i++) Sleep(1);
    rshot_restored=!rshot_open && !rshot_swap_count;
    if(strcmp(rshot_stage,"failed")) rshot_stage="stopped";
}
static int rshot_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,
        "mode=%s armed=%d active=%ld stage=%s why=%s noted=%llu notes_capped=%llu samples=%llu sample_skip=%llu items=%u items_max=%llu capped=%llu "
        "dead=%llu not_bullet=%llu elsewhere=%llu no_block=%llu first=%llu moved=%llu snaps=%llu reparented=%llu frames=%llu swapped=%llu "
        "restores=%llu stale=%llu gone=%llu clamped=%llu own=%llu lead_m=%.3g/%.3g disp_m=%.4g probe=%llu probe_refused=%llu full=%llu "
        "foreign=%llu nested=%llu failures=%lld restored=%d sample_us=%llu/%llu/%llu apply_us=%llu/%llu/%llu",
        rshot_mode_name(rshot_mode),rshot_armed,rshot_active,rshot_stage,rshot_why,(unsigned long long)rshot_noted,(unsigned long long)rshot_notes_capped,
        (unsigned long long)rshot_samples,(unsigned long long)rshot_sample_skip,rshot_valid?rshot_count[rshot_gen]:0u,(unsigned long long)rshot_items_max,
        (unsigned long long)rshot_capped,(unsigned long long)rshot_dead,(unsigned long long)rshot_not_bullet,(unsigned long long)rshot_elsewhere,
        (unsigned long long)rshot_no_block,(unsigned long long)rshot_first,(unsigned long long)rshot_moved,(unsigned long long)rshot_snaps,
        (unsigned long long)rshot_reparented,(unsigned long long)rshot_frames,(unsigned long long)rshot_swapped,(unsigned long long)rshot_restores,
        (unsigned long long)rshot_stale,(unsigned long long)rshot_gone,(unsigned long long)rshot_clamped,(unsigned long long)rshot_own,
        rshot_lead_last,rshot_lead_max,rshot_disp_max,(unsigned long long)rshot_probe,(unsigned long long)rshot_probe_refused,(unsigned long long)rshot_full,
        (unsigned long long)rshot_foreign,(unsigned long long)rshot_nested,(long long)rshot_failures,rshot_restored,
        (unsigned long long)rshot_sample_us,(unsigned long long)(rshot_samples?rshot_sample_us_sum/rshot_samples:0),(unsigned long long)rshot_sample_us_max,
        (unsigned long long)rshot_apply_us,(unsigned long long)(rshot_frames?rshot_apply_us_sum/rshot_frames:0),(unsigned long long)rshot_apply_us_max);
    return n>0 && (size_t)n<capacity?n:0;
}
