/* The HUD on the displayed camera (native_gate_own_predict_hud; needs native_gate_own_predict = 'present'). Display only.
 *
 * The own-ship prediction draws this client's ship, and the cockpit camera on it, ahead of the simulation by the input
 * delay (lockstep_own_predict.h: slot 10 swaps the ship's view-cache pose block to the displayed pose and the camera's
 * view bank follows). The stock target system places its brackets, lead marks and icons in slot 6, through X4's screen
 * projections (ui/core/lua/targetsystem.lua: GetUIElementScreenPosition, GetUIElementRectangleScreenPosition,
 * GetMessageScreenPosition, C.GetRelativeAimScreenPosition, C.GetVelocityScreenPosition, C.GetComponentScreenPosition).
 * Those read the same view caches and camera bank, which in slot 6 still held the simulated pose: while the ship turned,
 * every marker was placed for a camera a few degrees behind the drawn one, and they all slid off their targets together
 * (user report 9 Nov; hud1: a target 10 to 39 px apart between the two cameras at the demo's yaw).
 *
 * So the same display state is in place for the UI's own work in slot 6:
 *   UI::CUIController::Move entry   ophud_enter: the predicted ships' pose blocks and the camera bank as slot 10 will draw
 *                                   them. This tick's prediction is made later, in the probe's frame, so the pose here is
 *                                   the previous tick's display advanced by one step (provisional).
 *   the probe's frame               gate.own_predict_hud(0) before it (the gate's modules read the simulated state),
 *                                   gate.own_predict_hud(1) after it: this tick's own display pose (exact).
 *   the entry's return              ophud_leave: every pointer and bank entry back (as after slot 10).
 * On a presenting client (slots 2..N) the calls sit in pres_ui_move, around its presentation scope in the order slot 10
 * uses (the poses, the bank, the scope). On a client without a presentation (slot 1) this module holds the UI entry
 * (vtable entry PRES_UI_ENTRY) itself. Render-only frames run no slot 6: between two steps the markers stay where the
 * step put them while the picture moves on. So the poses here are not the step's own but the display's at the mean
 * offset slot 10 drew with during the last step (opred_hud_offset: with one render-only frame per step, a quarter step):
 * the markers are then off by half the spread of the frames' offsets at most, to both sides, not by a whole step of
 * camera motion in every render-only frame (user report after the first form: better, still lagging).
 * Nothing here is read by the simulation: the same swaps as slot 10's, under the same checks (a nested entry or a failed
 * restore stops the gate, 184). */
static int ophud_armed,ophud_open,ophud_presented,ophud_followed,ophud_ui_installed,ophud_restored;
static int ophud_in_ui;   /* inside a UI entry that closes the scope at its return (ophud_ui_move, pres_ui_move) */
static uint64_t ophud_enters,ophud_applied,ophud_nested,ophud_unbalanced,ophud_ui_calls,ophud_forced,ophud_outside;
static const char *ophud_why="-";
/* 1 = the displayed poses are in place until ophud_leave. force: the diagnostic's own scope (its caller closes it), also
 * when not armed. Otherwise only inside a UI entry of ours, which closes the scope at its return: hud2's client 2 opened it
 * from the probe before its presentation held the UI entry, nothing closed it, and slot 10's own swap stopped (184). */
static int ophud_enter_as(int force) {
    if(!force && !ophud_armed) return 0;
    if(!force && !ophud_in_ui) {++ophud_outside;return 0;}
    if(ophud_open) {++ophud_nested;return 0;}
    ++ophud_enters;
    ophud_presented=opred_present_enter_as(1);
    if(!ophud_presented) return 0;
    ophud_followed=rdisp_follow_enter(0);ophud_open=1;++ophud_applied;
    if(force && !ophud_armed) ++ophud_forced;
    return 1;
}
static int ophud_enter(void) {return ophud_enter_as(0);}
static void ophud_leave(void) {
    if(!ophud_open) return;
    rdisp_follow_leave(ophud_followed);opred_present_leave(ophud_presented);
    ophud_open=0;ophud_presented=ophud_followed=0;
}
/* The UI entry on a client without a presentation scope. */
static void ophud_ui_move(void *self) {
    void (*const original)(void*)=(void(*)(void*))(image+PRES_UI_MOVE);
    ++ophud_ui_calls;
    ophud_in_ui=1;ophud_enter();
    __try {original(self);} __finally {ophud_leave();ophud_in_ui=0;}
}
static int ophud_arm(void) {
    if(ophud_armed) return 1;
    if(GetCurrentThreadId()!=ui_thread) {ophud_why="thread";return 0;}
    if(!opred_armed || opred_mode!=OPRED_PRESENT) {ophud_why="own_predict_present";return 0;}
    if(!rdisp_pins() || !pres_pins()) {ophud_why="pins";return 0;}
    if(!pres_slot) {   /* no presentation here: the UI entry is ours (pres_ui_move never takes it on this client) */
        if(!pnat_swap8(pnat_base+PRES_UI_ENTRY,(void*)(image+PRES_UI_MOVE),(void*)ophud_ui_move,1)) {ophud_why="ui_entry";return 0;}
        ophud_ui_installed=1;
    }
    ophud_restored=0;ophud_armed=1;return 1;
}
/* The session's end (with the pilot pieces): the entry back; a scope still open is closed first. */
static void ophud_restore(void) {
    if(!ophud_armed) return;
    if(GetCurrentThreadId()==ui_thread) ophud_leave();
    if(ophud_ui_installed && pnat_swap8(pnat_base+PRES_UI_ENTRY,(void*)ophud_ui_move,(void*)(image+PRES_UI_MOVE),1)) ophud_ui_installed=0;
    ophud_restored=!ophud_ui_installed;ophud_armed=0;
}
static int ophud_status(char *out,size_t cap) {
    return snprintf(out,cap,"armed=%d why=%s open=%d entry=%d restored=%d ui_calls=%llu enters=%llu applied=%llu exact=%llu provisional=%llu "
        "stale=%llu refused=%llu nested=%llu unbalanced=%llu forced=%llu outside=%llu offset=%.3f",ophud_armed,ophud_why,ophud_open,ophud_ui_installed,ophud_restored,
        (unsigned long long)ophud_ui_calls,(unsigned long long)ophud_enters,(unsigned long long)ophud_applied,(unsigned long long)opred_hud_exact,
        (unsigned long long)opred_hud_provisional,(unsigned long long)opred_hud_stale,(unsigned long long)opred_hud_refused,
        (unsigned long long)ophud_nested,(unsigned long long)ophud_unbalanced,(unsigned long long)ophud_forced,(unsigned long long)ophud_outside,opred_hud_offset());
}
