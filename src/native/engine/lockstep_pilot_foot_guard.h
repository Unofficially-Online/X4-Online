/* Slot 1 on foot (lockstep_pilot_foot.h), its UI guard part: the docked menu's Get Up button.
 * The GetUp export is an entry of the guard's inventory (group flight). With native_gate_pilot_foot the module
 * claims it (docs/LOCKSTEP-REGISTRY.txt section 3): on slot 1's client, in a local phase, while slot 1 sits,
 * the call becomes the GET_UP key in this tick's stock record (X4's 71bd80 posts the same GetUpEvent as the export,
 * lockstep_pilot_foot.h) and the menu gets true; it runs on every client at the apply tick. Otherwise (slots 2..N,
 * outside the live window, already up) the guard's refusal applies when its flight group is armed, and this claim
 * refuses by itself when it is not (a claimed entry is installed whatever the groups say: nothing runs on one client). */
static unsigned pfoot_getup_index=UINT_MAX;
static volatile LONG64 pfoot_getups_refused;
/* Slots 2..N (native_gate_pilot_foot_slots, lockstep_pilot_foot_slots.h): client k's button while slot k sits in its ship
 * becomes slot k's GET_UP in its record, consumed at the apply in slot k's scope (never X4's GetUp, never 71bac0). */
static int pfslot_claim_ok(unsigned k);static volatile LONG64 pfslot_getups_claimed;
static int uiguard_armed_now(void) {return uiguard_armed;}
static int pfoot_claim_decide(unsigned index,const uint64_t *frame,uint64_t *ret) {
    (void)frame;*ret=0;
    if(!pfoot_armed || index!=pfoot_getup_index) return 0;
    if(pnat_is_live() && pstk_capturing && pstk_capture_slot==1 && pfoot_state==PFOOT_SEATED &&
       pstk_record_event(0u,PFOOT_GET_UP,0u)) {
        InterlockedIncrement64(&pfoot_getups_claimed);*ret=1;return 1;
    }
    if(pnat_is_live() && pstk_capturing && pstk_capture_slot>=2 && pfslot_claim_ok(pstk_capture_slot) && pstk_record_event(0u,PFOOT_GET_UP,0u)) {
        InterlockedIncrement64(&pfslot_getups_claimed);*ret=1;return 1;
    }
    InterlockedIncrement64(&pfoot_getups_refused);
    if(index<uiguard_count && uiguard_selected(&uiguard_tab[index])) return 0;   /* the guard refuses: its row, its info line */
    return 1;                                                                    /* refused here: X4's GetUp does not run */
}
/* At the arming: GetUp's entry is this module's (0 and pstk_arm_why when the table has none or another holds it). */
static int pfoot_claim(void) {
    pfoot_getup_index=UINT_MAX;
    for(unsigned i=0;i<uiguard_count && i<UIGUARD_MAX;i++)
        if(uiguard_tab[i].kind==UIGUARD_FFI && uiguard_tab[i].name && !strcmp(uiguard_tab[i].name,"GetUp")) {pfoot_getup_index=i;break;}
    if(pfoot_getup_index==UINT_MAX) return pstk_arm_refuse("foot_guard_entry");
    if(!uiguard_claim(pfoot_getup_index,pfoot_claim_decide)) {
        snprintf(pstk_arm_why,sizeof(pstk_arm_why),"foot_%s",uiguard_claim_why);pfoot_getup_index=UINT_MAX;return 0;
    }
    return 1;
}
static int pfoot_claim_ready(void) {
    return pfoot_getup_index<UIGUARD_MAX && uiguard_claimed[pfoot_getup_index] && uiguard_claim_by[pfoot_getup_index]==pfoot_claim_decide;
}
