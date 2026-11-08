/* Lua surface of the default-off piloting prototype (lockstep_pilot_inputs.h). */
static int lua_pilot_inputs(lua_State *L) {
    size_t length=0;const char *manifest=p_lua_tolstring(L,1,NULL),*source=p_lua_tolstring(L,2,&length);
    double delay=p_lua_tonumber(L,3),start=p_lua_tonumber(L,4),stop=p_lua_tonumber(L,5),suppress=p_lua_tonumber(L,6);
    unsigned kind=source && !strcmp(source,"script")?PILOT_SOURCE_SCRIPT:source && !strcmp(source,"wire")?PILOT_SOURCE_WIRE:0;
    int ok=kind && isfinite(delay) && floor(delay)==delay && delay>=0 && delay<=PILOT_DELAY_MAX &&
        isfinite(start) && floor(start)==start && start>=1 && start<=PILOT_TICK_MAX && isfinite(stop) && floor(stop)==stop && stop<=PILOT_TICK_MAX &&
        (suppress==0 || suppress==1) && pilot_arm(manifest,kind,(unsigned)delay,(uint64_t)start,(uint64_t)stop,(unsigned)suppress);
    p_lua_pushnumber(L,ok);return 1;
}
/* Absent slots (hot join; lockstep_pilot_inputs.h): gate.pilot_absent(mask[, rejoin]) before the first tick (bit s-1, never
 * slot 1; rejoin: the subset present from the start, roadmap 15);
 * gate.pilot_handover(k, H) and gate.pilot_abort(k) from the join section's HANDOVER and ABORT records, on every client at
 * the tick they are applied. 1 accepted, 0 refused (the probe stops: a client that disagrees would part from the others). */
static int lua_pilot_absent(lua_State *L) {
    double mask=p_lua_tonumber(L,1),rejoin=p_lua_tonumber(L,2);   /* none: 0 */
    p_lua_pushnumber(L,accept_ui() && isfinite(mask) && floor(mask)==mask && mask>0 && mask<(double)(1u<<PILOT_SLOTS) &&
        isfinite(rejoin) && floor(rejoin)==rejoin && rejoin>=0 && rejoin<(double)(1u<<PILOT_SLOTS) &&
        pilot_absent_arm2((unsigned)mask,(unsigned)rejoin));
    return 1;
}
static int lua_pilot_handover(lua_State *L) {
    double slot=p_lua_tonumber(L,1),tick=p_lua_tonumber(L,2);
    p_lua_pushnumber(L,accept_ui() && stats.active && isfinite(slot) && floor(slot)==slot && slot>=2 && slot<=PILOT_SLOTS &&
        isfinite(tick) && floor(tick)==tick && tick>0 && pilot_handover_apply((unsigned)slot,(uint64_t)tick,stats.issued));
    return 1;
}
static int lua_pilot_abort(lua_State *L) {
    double slot=p_lua_tonumber(L,1);
    p_lua_pushnumber(L,accept_ui() && stats.active && isfinite(slot) && floor(slot)==slot && slot>=2 && slot<=PILOT_SLOTS &&
        pilot_abort_apply((unsigned)slot,stats.issued,wire_lookahead));
    return 1;
}
/* Joining inside the window (lockstep_pilot_join.h): gate.pilot_join_park_at(T) from the join core's save_at on every client
 * (1 armed), gate.pilot_players_state() for the join provider 'pilot_players' (nil when it does not fit),
 * gate.pilot_join_status() for the probe's rows. */
static int lua_pilot_join_park_at(lua_State *L) {
    double tick=p_lua_tonumber(L,1);
    p_lua_pushnumber(L,accept_ui() && stats.active && isfinite(tick) && floor(tick)==tick && tick>0 && pjoin_park_arm((uint64_t)tick));
    return 1;
}
static int lua_pilot_players_state(lua_State *L) {
    static char out[8192];int n=accept_ui() && pilot_armed?pjoin_state(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* gate.pilot_join_state(): the pilot tables a joiner needs at T+1 (lockstep_pilot_join.h 'pilot_state'), nil when too large. */
static int lua_pilot_join_state(lua_State *L) {
    static char out[1u<<18];int n=accept_ui() && pilot_armed?pjoin_tables_state(out,sizeof(out),stats.issued):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* gate.pilot_join_in_window(1): the joiner's resume inside the window (diagnostic, before the first tick). */
/* gate.pilot_join_slotk_quats(1): a joiner's slot k (k >= 2) also takes the session's three PlayerController double quaternions
 * (lockstep_pilot_join.h pjoin_pctl_quat; experiment, before the first tick). */
static int lua_pilot_join_slotk_quats(lua_State *L) {
    double on=p_lua_tonumber(L,1);
    if(accept_ui() && on==1 && !stats.issued) pjoin_slotk_quats=1;
    p_lua_pushnumber(L,pjoin_slotk_quats);return 1;
}
/* gate.pilot_join_slotk_words(mask): the slot-1-only word groups a joiner's slot k takes as well (lockstep_pilot_join.h
 * pjoin_pctl_group; 1..63; experiment, before the first tick). Returns the mask, 0 refused. */
static int lua_pilot_join_slotk_words(lua_State *L) {
    const double m=p_lua_tonumber(L,1);
    if(!accept_ui() || stats.issued || !(m>=1.0 && m<=63.0) || (double)(unsigned)m!=m) {p_lua_pushnumber(L,0);return 1;}
    pjoin_slotk_mask=(unsigned)m;p_lua_pushnumber(L,m);return 1;
}
static int lua_pilot_join_in_window(lua_State *L) {
    double on=p_lua_tonumber(L,1);
    if(accept_ui() && on==1 && !stats.issued) pjoin_in_window_diag=1;
    p_lua_pushnumber(L,pjoin_in_window_diag);return 1;
}
/* gate.pilot_dump(slot, which, offset): 256 bytes of slot k's ship (0), its controller (1, ship+0x200), agent (2), solver (3)
 * or player (4: slot 1 X4's own player, slot k its P_k) from offset, as hex; nil when unreadable. Diagnostic native_gate_pilot_dump_tick: the session's bytes and a joiner's
 * side by side (reports/LOCKSTEP-JOIN-IN-WINDOW-20261104.txt section 15). Reads only. */
static int lua_pilot_dump(lua_State *L) {
    const double slot=p_lua_tonumber(L,1),which=p_lua_tonumber(L,2),offset=p_lua_tonumber(L,3);
    unsigned char bytes[256],*base=NULL;char hex[2*sizeof(bytes)];
    if(!accept_ui() || !(slot>=1 && slot<=PILOT_SLOTS) || floor(slot)!=slot || !(which>=0 && which<=6) || floor(which)!=which ||
       !(offset>=0 && offset<=0x8000) || floor(offset)!=offset) {p_lua_pushnil(L);return 1;}
    if(which>=5) {   /* the resume's snapshots of the slot's controller (lockstep_pilot_join.h pjoin_snap) */
        const unsigned w=(unsigned)which-5u,k=(unsigned)slot;
        if(!pjoin_snap_ok[w][k] || offset+sizeof(bytes)>PJOIN_SNAP) {p_lua_pushnil(L);return 1;}
        memcpy(bytes,pjoin_snap[w][k]+(unsigned)offset,sizeof(bytes));base=bytes;
    }
    const pilot_slot *s=&pilot_slots[(unsigned)slot-1u];
    if(base) ;else if(which==0) base=s->object;else if(which==2) base=s->agent;else if(which==3) base=s->solver;
    else if(which==4) base=slot==1?pslot_p1:pslots[(unsigned)slot].player;
    else if(!s->object || !read_local(s->object+0x200,&base,8)) base=NULL;
    if(!base || (base!=bytes && !read_local(base+(unsigned)offset,bytes,sizeof(bytes)))) {p_lua_pushnil(L);return 1;}
    static const char digits[]="0123456789abcdef";
    for(unsigned i=0;i<sizeof(bytes);i++) {hex[2*i]=digits[bytes[i]>>4];hex[2*i+1]=digits[bytes[i]&15];}
    p_lua_pushlstring(L,hex,sizeof(hex));return 1;
}
static int lua_pilot_join_status(lua_State *L) {
    char out[160];int n=accept_ui()?pjoin_status_text(out,sizeof(out)):0;
    if(n>0 && (size_t)n<sizeof(out)) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* gate.pilot_absent_state() / gate.pilot_absent_restore(text): the join package's copy of the above (a joiner, before T+1). */
/* gate.pilot_slots_max(): how many slots the pilot modules drive (PILOT_SLOTS; the probe's slot checks use it). */
static int lua_pilot_slots_max(lua_State *L) {p_lua_pushnumber(L,PILOT_SLOTS);return 1;}
static int lua_pilot_absent_state(lua_State *L) {
    char out[512];int n=accept_ui() && pilot_absent?pilot_absent_state(out,sizeof(out)):0;   /* 7 slots of 3 ticks */
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_pilot_absent_restore(lua_State *L) {
    const char *text=p_lua_tolstring(L,1,NULL);
    p_lua_pushnumber(L,accept_ui() && pilot_absent_restore(text));return 1;
}
static int lua_pilot_inputs_status(lua_State *L) {
    static char out[16384];   /* stock, slots, presentation, death and foot rows together can pass 4 KiB (nil then) */
    int n=accept_ui()?pilot_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* Every piloting module's stage and why in one short line, for the probe's STOP path: a status row is not written
 * after a stop, and live w1foot (stop 185 at tick 37) lost its foot why that way. 0 when piloting is not armed. */
static int pilot_why_text(char *out,size_t capacity) {
    if(!pilot_armed) return 0;
    int n=snprintf(out,capacity,"reason=%u pilot=%s native=%s fail=%s arm_why=%s",pilot_stop_reason(),pilot_stage?pilot_stage:"-",
        pnat_stage?pnat_stage:"-",pnat_fail_stage?pnat_fail_stage:"-",pstk_arm_why[0]?pstk_arm_why:"-");
    if(n>0 && (size_t)n<capacity && pfoot_armed)
        n+=snprintf(out+n,capacity-(size_t)n," foot=%s foot_why=%s foot_state=%u foot_entries=%u/%lld",pfoot_stage,pfoot_why,pfoot_state,pfoot_entries,
            (long long)pfoot_entries_late);
    if(n>0 && (size_t)n<capacity && pfbody_armed) n+=snprintf(out+n,capacity-(size_t)n," bodies=%s bodies_why=%s bodies_mask=%x",pfbody_stage,pfbody_why,pfbody_mask);
    if(n>0 && (size_t)n<capacity && pfslot_armed) {
        n+=snprintf(out+n,capacity-(size_t)n," foot_slots=%s foot_slots_why=%s",pfslot_stage,pfslot_why);
        for(unsigned k=2;n>0 && (size_t)n<capacity && k<=PILOT_SLOTS;k++) if(pslot_mask>>(k-1)&1u) n+=snprintf(out+n,capacity-(size_t)n," fs%u=%d",k,pfslots[k].walking);
    }
    if(n>0 && (size_t)n<capacity && pslot_armed) n+=snprintf(out+n,capacity-(size_t)n," slots=%s slots_why=%s",pslot_stage,pslot_why);
    if(n>0 && (size_t)n<capacity && pdeath_armed) n+=snprintf(out+n,capacity-(size_t)n," death=%s death_why=%s",pdeath_stage,pdeath_why);
    if(n>0 && (size_t)n<capacity && pres_armed) n+=snprintf(out+n,capacity-(size_t)n," present=%s present_why=%s",pres_stage,pres_why);
    if(n>0 && (size_t)n<capacity && uirep_armed) n+=snprintf(out+n,capacity-(size_t)n," replay=%s replay_why=%s",uirep_stage_name,uirep_why);
    if(n>0 && (size_t)n<capacity && pilot_absent) {int more=pilot_absent_text(out+n,capacity-(size_t)n);if(!more) return 0;n+=more;}
    if(n>0 && (size_t)n<capacity && pjoin_controls[0]) n+=snprintf(out+n,capacity-(size_t)n," join_controls=%s",pjoin_controls);
    if(n>0 && (size_t)n<capacity && pjoin_in_window_diag) n+=snprintf(out+n,capacity-(size_t)n," join_resume=%s",pjoin_resume_why);
    return n>0 && (size_t)n<capacity?n:0;
}
/* gate.pilot_why(): pilot_why_text, or nil. */
static int lua_pilot_why(lua_State *L) {
    char out[1024];int n=accept_ui()?pilot_why_text(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_pilot_inputs_frame(lua_State *L) {
    char out[96];int n=accept_ui() && pilot_armed?pilot_frame_text(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_pilot_capture(lua_State *L) {
    unsigned char record[PILOT_RECORD_BYTES];char hex[2*PILOT_RECORD_BYTES+1];const char *why="ui";
    if(!accept_ui() || boot_phase!=3 || !stats.active || !pilot_capture(stats.issued,record,&why)) {
        p_lua_pushnil(L);p_lua_pushlstring(L,why,strlen(why));return 2;
    }
    pilot_hex(record,PILOT_RECORD_BYTES,hex);p_lua_pushlstring(L,hex,2*PILOT_RECORD_BYTES);return 1;
}
static int lua_pilot_apply(lua_State *L) {
    double slot=p_lua_tonumber(L,1);size_t length=0;const char *text=p_lua_tolstring(L,2,&length);
    unsigned char record[PILOT_RECORD_BYTES];
    int ok=accept_ui() && stats.active && boot_phase==3 && isfinite(slot) && floor(slot)==slot && slot>=1 && slot<=PILOT_SLOTS &&
        pilot_unhex(text,length,record) && pilot_stage_record((unsigned)slot,record,stats.issued);
    if(!ok && pilot_armed && pilot_source==PILOT_SOURCE_WIRE) InterlockedIncrement64(&pilot_failures);
    p_lua_pushnumber(L,ok);return 1;
}
static int lua_pilot_pending(lua_State *L) {
    char out[2*PILOT_LOCAL_RING*(2*PILOT_RECORD_BYTES+48)];int n=accept_ui() && pilot_armed?pilot_pending(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* gate.pilot_device(kind, pad, invert): kind 'kbm' | 'xinput' | 'both', XInput
 * controller index 0..3, invert mask (1 pitch, 2 yaw, 4 roll, 8 strafe x,
 * 16 strafe y). After gate.pilot_inputs, before the first admitted tick;
 * an identical rerun after a loading-UI reload succeeds. */
static int lua_pilot_device(lua_State *L) {
    size_t length=0;const char *kind=p_lua_tolstring(L,1,&length);double pad=p_lua_tonumber(L,2),invert=p_lua_tonumber(L,3);
    unsigned kinds=kind && !strcmp(kind,"kbm")?PILOT_DEVICE_KBM:kind && !strcmp(kind,"xinput")?PILOT_DEVICE_PAD:
        kind && !strcmp(kind,"both")?PILOT_DEVICE_KINDS:0;
    int ok=kinds && pilot_armed && isfinite(pad) && floor(pad)==pad && pad>=0 && pad<=3 &&
        isfinite(invert) && floor(invert)==invert && invert>=0 && invert<=PILOT_INVERT_KNOWN &&
        (pilot_device_armed || (!stats.issued && GetCurrentThreadId()==ui_thread)) &&
        pilot_device_arm(kinds,(unsigned)pad,(unsigned)invert);
    p_lua_pushnumber(L,ok);
    if(ok) p_lua_pushnumber(L,pilot_device_xinput);else p_lua_pushnil(L);
    return 2;
}
/* gate.pilot_camera_follow(): arm camera follow (after gate.pilot_inputs). */
static int lua_pilot_camera_follow(lua_State *L) {p_lua_pushnumber(L,pilot_camera_arm());return 1;}
/* gate.pilot_camera(tick): one follow step for this client's own slot ship;
 * returns the stage (held, applied, own_ship, waiting, not_possible, ...). */
static int lua_pilot_camera(lua_State *L) {
    double tick=p_lua_tonumber(L,1);const char *stage="tick";
    if(isfinite(tick) && tick>=1 && floor(tick)==tick && tick<9007199254740992.0) stage=pilot_camera_step((uint64_t)tick);
    p_lua_pushlstring(L,stage,strlen(stage));return 1;
}
static int lua_pilot_camera_status(lua_State *L) {
    char out[512];int n=accept_ui()?pilot_camera_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* gate.pilot_ship(slot): runtime ID (number) and saved ID of a resolved slot ship. */
static int lua_pilot_ship(lua_State *L) {
    double slot=p_lua_tonumber(L,1);char saved[17];
    if(!accept_ui() || !pilot_armed || !isfinite(slot) || floor(slot)!=slot || slot<1 || slot>pilot_slot_count) {p_lua_pushnil(L);return 1;}
    pilot_slot *s=&pilot_slots[(unsigned)slot-1];
    snprintf(saved,sizeof(saved),"%016llx",(unsigned long long)s->saved);
    if(s->runtime && s->runtime<(1ULL<<53)) p_lua_pushnumber(L,(double)s->runtime);else p_lua_pushnil(L);
    p_lua_pushlstring(L,saved,16);return 2;
}
/* gate.pilot_native_capture(): observation only. The record the native
 * processed channels give on this client now (getters d8e5b0, b09880, ea7600,
 * ea7650; no ring, no proposal, no write). While parked they read zero. */
static int lua_pilot_native_capture(lua_State *L) {
    pilot_record r;unsigned char record[PILOT_RECORD_BYTES];char hex[2*PILOT_RECORD_BYTES+1];const char *why="ui";
    if(!accept_ui() || boot_phase!=3 || !stats.active || !pilot_armed || !pilot_capture_native(stats.issued,&r,&why)) {
        p_lua_pushnil(L);p_lua_pushlstring(L,why,strlen(why));return 2;
    }
    pilot_encode(&r,record);pilot_hex(record,PILOT_RECORD_BYTES,hex);p_lua_pushlstring(L,hex,2*PILOT_RECORD_BYTES);return 1;
}
/* gate.pilot_native(requests): X4's own controls for this client's pilot
 * capture (lockstep_pilot_native.h, stop reason 113). After gate.pilot_inputs
 * with suppression and without a device, before the first admitted tick;
 * requests 1 also records the PlayerController's own velocity requests for
 * the slot that flies the occupied ship. Identical rerun after a reload: 1. */
static int lua_pilot_native(lua_State *L) {
    double requests=p_lua_tonumber(L,1);
    int ok=(requests==0 || requests==1) && pnat_arm((int)requests);
    p_lua_pushnumber(L,ok);return 1;
}
/* gate.pilot_native_refusals(): refused flight actions since the last call,
 * "tick:kind:0xid:NAME:value,..." (nil when none). */
static int lua_pilot_native_refusals(lua_State *L) {
    char out[2048];int n=accept_ui() && pnat_armed?pnat_refusals_text(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* gate.pilot_stock(capture[, slots]): stock piloting (lockstep_pilot_stock.h, stop
 * reason 155) of the slots in the bit mask slots (default 1 = slot 1 only; slots
 * 2..N fly in slot scopes, lockstep_pilot_slots.h, stop reason 161). After
 * gate.pilot_inputs (wire, suppression) and, on clients outside the mask,
 * gate.pilot_device; capture 1 on every client whose own slot is in the mask (X4's
 * own input), 0 on the others. Before the first admitted tick; identical rerun
 * after a reload: 1. Refused: 0 and the reason (a stage, pin tables with the RVA). */
static int lua_pilot_stock(lua_State *L) {
    double capture=p_lua_tonumber(L,1),slots=p_lua_tonumber(L,2); /* nil reads 0: slot 1 only */
    if(slots==0) slots=1;
    pstk_arm_refuse("arguments");
    int ok=(capture==0 || capture==1) && isfinite(slots) && floor(slots)==slots && slots>=1 && slots<(double)(1u<<PILOT_SLOTS) &&
        pnat_arm_stock((int)capture,(unsigned)slots);
    p_lua_pushnumber(L,ok);
    if(ok) return 1;
    p_lua_pushlstring(L,pstk_arm_why,strlen(pstk_arm_why));return 2;
}
/* gate.pilot_present(hud): this client presents its own stock slot as the player
 * (lockstep_pilot_present.h, stop reason 162). hud 1: the UI anchor moves too, 0: camera and UI
 * only. After gate.pilot_stock; on slot 1's client it only records that (X4 presents slot 1
 * itself). 1, or 0 and the reason. */
static int lua_pilot_present(lua_State *L) {
    double hud=p_lua_tonumber(L,1);
    pstk_arm_refuse("arguments");
    if(hud!=0 && hud!=1) {p_lua_pushnumber(L,0);p_lua_pushlstring(L,pstk_arm_why,strlen(pstk_arm_why));return 2;}
    pstk_arm_refuse("thread");
    int ok=accept_ui() && pres_arm((int)hud);
    p_lua_pushnumber(L,ok);
    if(ok) return 1;
    p_lua_pushlstring(L,pstk_arm_why,strlen(pstk_arm_why));return 2;
}
/* gate.cutscene_ready(): a cutscene is ready 2.0 s of game time after the first pass that sees it
 * (lockstep_cutscene_ready.h, stop reason 170). Before the first tick, with serial pathing. */
static int lua_cutscene_ready(lua_State *L) {p_lua_pushnumber(L,accept_ui() && cr_arm());return 1;}
static int lua_cutscene_ready_status(lua_State *L) {
    char out[256];int n=accept_ui()?cr_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushlstring(L,cr_stage,strlen(cr_stage));return 1;
}
/* gate.cutscene_ready_row(): the next posted cutscene (tick= id= key=), nil when none. */
static int lua_cutscene_ready_row(lua_State *L) {
    const char *row=accept_ui()?cr_take_row():NULL;
    if(row) p_lua_pushlstring(L,row,strlen(row));else p_lua_pushnil(L);return 1;
}
/* gate.crash_note(note path, debug log path or nil): an unhandled exception writes one CRASH line
 * there before X4's own crash handling (lockstep_crash_note.h). 1, or 0 for a bad path. The probe
 * arms it as soon as the gate is loaded, before gate.start names the UI thread: it is process-wide
 * and binds to no thread, so it does not ask accept_ui (which refuses before the start). */
static int lua_crash_note(lua_State *L) {
    const char *note=p_lua_tolstring(L,1,NULL),*log=p_lua_tolstring(L,2,NULL);
    p_lua_pushnumber(L,crash_note_arm(note,log));return 1;
}
/* gate.pilot_death(delay, suit): a slot ship's death does not end the session (lockstep_pilot_death.h, stop
 * reason 163); delay > 0: the slot comes back after that many ticks, 0: it stays dead; suit 1: a dead
 * slot 2..N waits in a spacesuit. After gate.pilot_stock, before the first tick. 1, or 0 and the reason. */
static int lua_pilot_death(lua_State *L) {
    double delay=p_lua_tonumber(L,1),suit=p_lua_tonumber(L,2);
    pstk_arm_refuse("arguments");
    if(!isfinite(delay) || delay<0 || floor(delay)!=delay || (suit!=0 && suit!=1)) {p_lua_pushnumber(L,0);p_lua_pushlstring(L,pstk_arm_why,strlen(pstk_arm_why));return 2;}
    pstk_arm_refuse("thread");
    int ok=accept_ui() && pdeath_arm((unsigned)(delay>PILOT_TICK_MAX?PILOT_TICK_MAX+1u:delay),suit==1);
    p_lua_pushnumber(L,ok);
    if(ok) return 1;
    p_lua_pushlstring(L,pstk_arm_why,strlen(pstk_arm_why));return 2;
}
/* gate.pilot_foot(): slot 1 gets up and walks through the stock record on every client (lockstep_pilot_foot.h, stop
 * reason 185); the docked menu's GetUp is claimed in the UI guard. After gate.pilot_stock, before the first tick, on
 * every client. 1, or 0 and the reason. */
static int lua_pilot_foot(lua_State *L) {
    pstk_arm_refuse("thread");
    int ok=accept_ui() && pfoot_arm();
    p_lua_pushnumber(L,ok);
    if(ok) return 1;
    p_lua_pushlstring(L,pstk_arm_why,strlen(pstk_arm_why));return 2;
}
/* gate.pilot_foot_bodies(): slots 2..N's players into their seats' rooms on every client (lockstep_pilot_foot_body.h,
 * stop reason 185). After gate.pilot_foot and the stock slots, before the first tick. 1, or 0 and the reason. */
static int lua_pilot_foot_bodies(lua_State *L) {
    pstk_arm_refuse("thread");
    int ok=accept_ui() && pfbody_arm();
    p_lua_pushnumber(L,ok);
    if(ok) return 1;
    p_lua_pushlstring(L,pstk_arm_why,strlen(pstk_arm_why));return 2;
}
/* gate.pilot_foot_slots(): slots 2..N get up and walk on every client (lockstep_pilot_foot_slots.h, stop reason 185).
 * After gate.pilot_foot (and gate.pilot_foot_bodies when used) and the stock slots, before the first tick. 1, or 0 and
 * the reason. */
static int lua_pilot_foot_slots(lua_State *L) {
    pstk_arm_refuse("thread");
    int ok=accept_ui() && pfslot_arm();
    p_lua_pushnumber(L,ok);
    if(ok) return 1;
    p_lua_pushlstring(L,pstk_arm_why,strlen(pstk_arm_why));return 2;
}
/* gate.pilot_death_frame(): what changed since the last call ("die:1,create:1,live:1"), nil if nothing. */
static int lua_pilot_death_frame(lua_State *L) {
    const char *text=accept_ui()?pdeath_take_events():NULL;
    if(text) p_lua_pushlstring(L,text,strlen(text));else p_lua_pushnil(L);return 1;
}
/* A slot number and a runtime ID as a decimal string, from an MD answer. */
static int lua_pilot_death_answer(lua_State *L,unsigned *slot_out,uint64_t *runtime_out) {
    double slot=p_lua_tonumber(L,1);size_t size=0;const char *text=p_lua_tolstring(L,2,&size);uint64_t runtime=0;
    if(!accept_ui() || !text || !size || size>20 || !isfinite(slot) || slot<1 || slot>PILOT_SLOTS || floor(slot)!=slot) return 0;
    for(size_t i=0;i<size;i++) {
        unsigned digit=(unsigned)(text[i]-'0');
        if(digit>9 || runtime>(UINT64_MAX-digit)/10) return 0;
        runtime=runtime*10+digit;
    }
    *slot_out=(unsigned)slot;*runtime_out=runtime;return 1;
}
/* gate.pilot_death_ship(slot, runtime): the ship MD built for a slot waiting for one. */
static int lua_pilot_death_ship(lua_State *L) {
    unsigned slot=0;uint64_t runtime=0;
    p_lua_pushnumber(L,lua_pilot_death_answer(L,&slot,&runtime) && pdeath_ship(slot,runtime));return 1;
}
/* gate.pilot_death_suit(slot, runtime): the spacesuit MD built for a dead slot 2..N. */
static int lua_pilot_death_suit(lua_State *L) {
    unsigned slot=0;uint64_t runtime=0;
    p_lua_pushnumber(L,lua_pilot_death_answer(L,&slot,&runtime) && pdeath_suit_ship(slot,runtime));return 1;
}
static int lua_pilot_death_status(lua_State *L) {
    char out[512];int n=accept_ui() && pdeath_armed?pdeath_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* gate.pilot_present_diff(): once after the start, what a player's presence set on a ship (PILOT_PRESENT_DIFF). */
static int lua_pilot_present_diff(lua_State *L) {
    const char *text=accept_ui()?pres_diff_take():NULL;
    if(text) p_lua_pushlstring(L,text,strlen(text));else p_lua_pushnil(L);return 1;
}
/* gate.pilot_present_pause() / resume(): the probe's own frame runs as the canonical player; resume answers 1 when
 * the scope is open again (a scripted click inside the probe's frame pauses after it). */
static int lua_pilot_present_pause(lua_State *L) {p_lua_pushnumber(L,accept_ui() && pres_pause());return 1;}
static int lua_pilot_present_resume(lua_State *L) {p_lua_pushnumber(L,accept_ui() && pres_resume());return 1;}
/* gate.pilot_stock_callers(): slot:caller rva:count of a slot controller's stock entries called
 * outside its scope while simulating (nil without slots 2..N). */
static int lua_pilot_stock_callers(lua_State *L) {
    static char out[7168];int n=accept_ui() && pslot_armed?pslot_callers_text(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* gate.pilot_stock_pins(): every pin table of stock piloting with slots 2..N, first
 * failure named (1 or 0,why). The probe calls it before other modules patch X4;
 * gate.pilot_stock then answers from these checks. */
static int lua_pilot_stock_pins(lua_State *L) {
    pstk_arm_refuse("thread");
    int ok=accept_ui() && pnat_stock_pins(3u);
    p_lua_pushnumber(L,ok);
    if(ok) return 1;
    p_lua_pushlstring(L,pstk_arm_why,strlen(pstk_arm_why));return 2;
}
static int pstk_unhex(const char *text,size_t length,unsigned char *out,unsigned capacity,unsigned *n) {
    if(!text || !length || length%2 || length/2>capacity) return 0;
    for(size_t i=0;i<length;i++) {
        char c=text[i];unsigned d=c>='0' && c<='9'?(unsigned)(c-'0'):c>='a' && c<='f'?(unsigned)(c-'a'+10):16;
        if(d>15) return 0;
        if(i&1) out[i/2]=(unsigned char)(out[i/2]|d);else out[i/2]=(unsigned char)(d<<4);
    }
    *n=(unsigned)(length/2);return 1;
}
/* gate.pilot_stock_capture(): this tick's stock record of the capturing client
 * (hex), or nil,why (not_capturing, not_live before the window, local_ring_full). */
static int lua_pilot_stock_capture(lua_State *L) {
    unsigned char record[PSTK_BYTES_MAX];char hex[2*PSTK_BYTES_MAX+1];unsigned length=0;const char *why="ui";
    if(!accept_ui() || boot_phase!=3 || !stats.active || !pnat_armed || pnat_mode!=PNAT_MODE_STOCK || !pstk_capture(stats.issued,record,&length,&why)) {
        p_lua_pushnil(L);p_lua_pushlstring(L,why,strlen(why));return 2;
    }
    pilot_hex(record,length,hex);p_lua_pushlstring(L,hex,2*length);return 1;
}
/* gate.pilot_stock_apply(slot, hex): stage an admitted stock record (every client). */
static int lua_pilot_stock_apply(lua_State *L) {
    double slot=p_lua_tonumber(L,1);size_t length=0;const char *text=p_lua_tolstring(L,2,&length);
    unsigned char record[PSTK_BYTES_MAX];unsigned n=0;
    int ok=accept_ui() && stats.active && boot_phase==3 && pnat_armed && pnat_mode==PNAT_MODE_STOCK && isfinite(slot) && floor(slot)==slot &&
        slot>=1 && slot<=PILOT_SLOTS && pstk_unhex(text,length,record,sizeof(record),&n) && pstk_stage((unsigned)slot,record,n,stats.issued);
    if(!ok && pnat_armed && pnat_mode==PNAT_MODE_STOCK) InterlockedIncrement64(&pilot_failures);
    p_lua_pushnumber(L,ok);return 1;
}
/* gate.pilot_stock_inject(kind, id, value): scripted input of the capturing client,
 * kind 'action' | 'state' | 'range', id of the replay set, value 0|1 (state) or a
 * range value; recorded in this tick's record as if X4 had dispatched it. */
static int lua_pilot_stock_inject(lua_State *L) {
    size_t length=0;const char *kind=p_lua_tolstring(L,1,&length);double id=p_lua_tonumber(L,2),value=p_lua_tonumber(L,3);
    unsigned k=kind && !strcmp(kind,"action")?PSTK_ACTION:kind && !strcmp(kind,"state")?PSTK_STATE:kind && !strcmp(kind,"range")?PSTK_RANGE:PSTK_KINDS;
    uint32_t bits=0;int ok=0;
    if(k<PSTK_KINDS && isfinite(id) && floor(id)==id && id>=0 && id<512 && isfinite(value) && accept_ui() && stats.active && boot_phase==3) {
        if(k==PSTK_RANGE) {float f=(float)value;memcpy(&bits,&f,4);}
        else if(value==0 || value==1) bits=(uint32_t)value;
        else bits=2;
        ok=bits<=1 || k==PSTK_RANGE?pstk_inject(k,(unsigned)id,bits):0;
    }
    p_lua_pushnumber(L,ok);return 1;
}
/* gate.pilot_stock_local(kind, id, value): scripted local input, a presentation id of this client, queued
 * for the next pass, which runs it as its own key would (lockstep_pilot_stock.h pstk_local_press); 1 when queued. */
static int lua_pilot_stock_local(lua_State *L) {
    size_t length=0;const char *kind=p_lua_tolstring(L,1,&length);double id=p_lua_tonumber(L,2),value=p_lua_tonumber(L,3);
    unsigned k=kind && !strcmp(kind,"action")?PSTK_ACTION:kind && !strcmp(kind,"state")?PSTK_STATE:kind && !strcmp(kind,"range")?PSTK_RANGE:PSTK_KINDS;
    uint32_t bits=0;int ok=0;
    if(k<PSTK_KINDS && isfinite(id) && floor(id)==id && id>=0 && id<512 && isfinite(value) && accept_ui() && stats.active && boot_phase==3) {
        if(k==PSTK_RANGE) {float f=(float)value;memcpy(&bits,&f,4);}
        else if(value==0 || value==1) bits=(uint32_t)value;
        else bits=2;
        ok=bits<=1 || k==PSTK_RANGE?pstk_local_press(k,(unsigned)id,bits):0;
    }
    p_lua_pushnumber(L,ok);return 1;
}
/* gate.pilot_stock_frame(): PSTOCK_1:tick:events:record hash:flight:settings for the digest,
 * then |Sk:events:record hash:flight for every stock slot k of 2..N. */
static int lua_pilot_stock_frame(lua_State *L) {
    char out[320];int n=accept_ui() && pnat_armed && pnat_mode==PNAT_MODE_STOCK?pstk_frame_text(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
