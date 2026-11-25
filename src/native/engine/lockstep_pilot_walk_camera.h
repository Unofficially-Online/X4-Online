/* The camera of a walking or seated slot on its own client (part of lockstep_pilot_present.h, the presentation scope;
 * native_gate_pilot_present_walk_camera and _seat_camera, session defaults; Windows gate; presentation only, this client only).
 *
 * What X4 does for its own player [F, pinned 9.00]. The player camera (Universe+0x3d0) hangs on X4's player
 * (camera +0x770) and keeps a chain of movement controllers at +0x20 (each link's next at +0x80, 9828a0 unlinks one).
 * A seated player's chain holds two of them: the U::UIAnchorCameraMovementController (class 0xc, vtable 0x2b34cb0) the
 * sit-down pushes with the seat's UI anchor (7173f0 / 717a90), and the U::CockpitHeadCameraMovementController (class 7,
 * vtable 0x2bae898) its PlayerController brings (d895a0). X4's stand-up takes both off: 7176f0 / 717c30 unlink the class
 * 0xc controller (9828a0), and the PlayerController's end unlinks the class 7 one (d84730 -> d89840). So a walking player
 * has no camera chain. Camera::GetOffset (97a300) composes the camera from its anchor's pose and then, only when the
 * chain is not empty and d7d2e0 says the player is not seated (a control position, or an AnimationCameraController,
 * class 6, on the chain: 70eaa0), the chain's pose and the camera's own offset (+0x590). Seated: never; walking: no chain.
 * Either way the camera is the player's own pose, and the HUD's anchor scene (UIAnchorController, 574ef0: walking,
 * GetOffset relative to the player) publishes the identity: the HUD sits on the eye.
 *
 * On client k (k = 2..N) X4's player is slot 1's, seated in slot 1's ship, so the camera keeps slot 1's chain while slot
 * k walks. In the presentation scope X4's player has slot k's control position, none (lockstep_pilot_present.h), so
 * d7d2e0 answers 'walking' and GetOffset applies slot 1's chain: slot 1's seat UI anchor and its head on top of slot k's
 * eye (lockstep_pilot_eye.h stage 'walk'). The camera left the eye while the HUD's anchor stayed on it (the eye module
 * publishes the identity there, as X4 does for a walker): every world-space presentation of the walking UI (the crosshair
 * and its interaction text, the target monitor, the dialog menu of a conversation or the transporter) was drawn from a
 * camera in front of it, much too large and cut off at the screen's edges (user report 24 Nov, player 2 standing in the
 * cockpit of a Kukri: 'Take Command of Ship', the target monitor and 'Travel to the cockpit of Kukri' about twice their
 * size; seated and in the suit X4's seated path skips the chain and all was right). FUN_14097c140, the camera's zoom
 * (the FOV the refill 979ce0 writes to +0x878), reads the chain too. The menus and the map on foot are such
 * presentations as well: they hang on the same anchor about 0.4 m in front of the eye.
 * Seen live (one X4, player 2 of a two-player session, slot 1 seated in the start's scout, read with ReadProcessMemory):
 * the camera's chain was the CockpitHead link, then the UIAnchorCamera link with slot 1's seat anchor (0, -0.038, -0.149)
 * (the scout's con_uianchor in its room). Player 2 standing in its Kukri: the walking HUD at about half size, the map on
 * foot 1796 px wide where seated it is 2515 (2560x1440 window). The user's Kukri as slot 1 puts the anchor in front of
 * the eye instead (as the Argon fighter's 0.238 m), and the walking UI came out about twice its size.
 *
 * Here, in the presentation scope while this client's own slot walks (the eye's stage 'walk'): the camera's chain is
 * empty, as X4's stand-up leaves it, when every link is one of those two classes (their vtables); one compare-and-swap
 * of camera+0x20 at the scope's entry and one back at its exit. A link of another class (an AnimationCameraController of
 * a cutscene) leaves the chain as it is (counted, other=). X4's chain is never unlinked or freed, and nothing outside the
 * scope sees the change; the simulation never runs in it. Live with it: the walking HUD at its seated size and the map on
 * foot 2515 px wide, as seated. Status (PILOT_INPUTS present row): walk_cam=on/scopes/other/lost[/other_vtable] (lost:
 * the chain was not ours at the exit; X4's value stays).
 *
 * Seated: the head movement (native_gate_pilot_present_seat_camera, a session default). X4 moves a seated player's head
 * with the chain's class 7 link: the CockpitHead controller pushes it back 2 cm in travel mode, eases it back in about a
 * second when a menu opens or travel ends, and leans it with the ship's moves. d83220 (from the FirstPersonController's
 * update while seated) takes the head's offset from the camera: its +0x590, then the chain's pose (slot 6 of the first
 * link); the player's pose, and so the eye and the camera, carry it, and the HUD's anchor scene answers its inverse
 * (574ef0, seated: the seat's UI anchor relative to the player): the HUD stays at the seat while the eye moves. Read in
 * a test X4 as player 1 (stock path) in travel: the head link (0, 0, -0.02), the FPC's head offset (+0x2a0) the scout's
 * seat (0, -0.038, -0.149) plus that, the scene (0, 0, +0.02); the radar 398 -> 394 px, the map's buttons unchanged
 * (the 'No' of its first-run dialog 394 px wide with and without the push).
 * On client k the eye module presents slot k at its seat without any head movement (lockstep_pilot_eye.h stage 'seat':
 * the FPC and the head controller are slot 1's), and the scene's published pose in the scope is the identity, but the
 * chain on X4's camera kept its head link, and in the scope X4 still put that link's push on the world-space UI, not
 * on the eye: the UI moved toward the eye. Read in a test X4 as player 2 (slot 2 in a fighter, slot 1 in the scout),
 * travel engaged with the map open: the 'No' 418 px (394 at rest), the HUD's mission bar 626 px (590) and shifted
 * toward the screen's corner, and every change of the push showed as the UI gliding: travel on and off (a sector
 * crossed in travel), the map or a mission menu opening (user reports 24 Nov, player 2: the map drawn too large after a
 * sector change, then right again; the UI gliding when M opened the map; the UI zooming at a click on a mission). Holding the
 * head link's translation at 0 from outside put the HUD back (590); the published scene held at the identity from
 * outside did not. With the head link off the chain in the scope: 394 under the same push, and back to 418 with it on
 * again (one run, switched three times).
 * Here, in the presentation scope while this client's own slot sits and the eye presents its seat (stage 'seat'): the
 * camera's chain starts at the head link's next link (camera+0x20 = that link: one compare-and-swap each way, no link
 * written), so the UI stays at the seat where the eye is, as X4's HUD stays at a seat. A head link that is not the
 * first takes the whole chain off (GetOffset skips the chain of a seated player anyway); no head link: nothing to do;
 * another class on the chain (a zoom, a cutscene): left as it is (other=). The eye with X4's head movement (the D pose,
 * peye_own_seat 0) keeps X4's chain. Status: seat_cam=on/scopes/whole (whole: the head link was not the first). */
#define PWCAM_CHAIN 0x20u                 /* Camera: its movement controller chain */
#define PWCAM_NEXT 0x80u                  /* a controller: the next link (9828a0) */
#define PWCAM_LINKS 8u
#define PWCAM_UIANCHOR_VTABLE 0x2b34cb0u  /* U::UIAnchorCameraMovementController (class 0xc) */
#define PWCAM_HEAD_VTABLE 0x2bae898u      /* U::CockpitHeadCameraMovementController (class 7) */
#define PWCAM_ISA_SLOT 3u                 /* vtable +0x18: IsOfClass */
#define PWCAM_POSE_SLOT 6u                /* vtable +0x30: the pose (CameraMovementController 986a10) */
#define PWCAM_POSE_FN 0x986a10u
static const struct {unsigned rva,size;const char *sha;} pwcam_pin_table[]={
    {0x97a300u,0x1d0u,"917a4cccca3ee8fb79c3b118ecfa6c2741423b2c097b21402f420f56abe5729e"},   /* Camera::GetOffset: the chain's test */
    {0x97c140u,0x1f4u,"94efbaaf5b7548c876d7e17596cca2682403186e105f242bd7c4e2a7e8974089"},   /* the camera's zoom */
    {0x9828a0u,0x70u,"651d156fc90ec1c2f7ca9880b980ef0f3406571dc7f4de6a023d20c9d0632272"},    /* a link unlinked (+0x20, +0x80) */
    {0xd89840u,0x7fu,"468d2289030d02b2d7eab8859fc95d1da1540840de540623458f6eee266d4f07"},    /* the class 7 link at a PlayerController's end */
    {0x70eaa0u,0x6au,"aa7e69d816c8b34b9f316a55e55d612ff848e6e999ab36bcfd31413e97697d6d"},    /* an AnimationCameraController on the chain */
    {0xd7d2e0u,0x84u,"bb97323466584f509d5a158283f79a6a71e421b05b9a6593f1ac88ea64d7c5be"},    /* seated: a control position or class 6 */
    {0x6f7d90u,0x1bu,"74b3c12ed261ee88a9028dd909f60194012cea9f2291e921e726f2e1758d662a"},    /* class 0xc's IsOfClass */
    {0x9778e0u,0x1bu,"df61489cf196cab0497d8707664da8785fd601151084783f7fdd187f75b444c1"},    /* class 7's IsOfClass */
    {0xd83220u,0xfdu,"53a8b16b72bb867d2eb6a54d59ea18a15bd1c623533de2237c6418a550c10582"},    /* a seated head: +0x590, then the chain */
    {0x574ef0u,0x374u,"53298edf586526542c5be1645cf4d32b60fa9f25a6c08adc5f8726180f4612f2"}};  /* the HUD scene: the seat's anchor */
#define PWCAM_PIN_COUNT (sizeof(pwcam_pin_table)/sizeof(*pwcam_pin_table))
static int pwcam_on,pwcam_asked;          /* armed on a client of slot 2..N; asked (gate.pilot_present_walk_camera) */
static int pwcam_seat_on,pwcam_seat_asked;   /* the same for a seated slot (gate.pilot_present_seat_camera) */
static const void *pwcam_vtables[2];      /* the image's two vtables, a test's */
static unsigned char *pwcam_camera,*pwcam_saved,*pwcam_put;   /* the camera, X4's chain, what the scope put there */
static int pwcam_open;
static unsigned pwcam_other_vtable;
static volatile LONG64 pwcam_scopes,pwcam_other,pwcam_lost,pwcam_seat_scopes,pwcam_seat_whole;
/* The pins and the two vtables' slots as X4 has them; 1 = usable. */
static int pwcam_pins(void) {
    for(unsigned i=0;i<PWCAM_PIN_COUNT;i++) if(!hash_ok(image+pwcam_pin_table[i].rva,pwcam_pin_table[i].size,pwcam_pin_table[i].sha)) {pin_failed_rva=pwcam_pin_table[i].rva;return 0;}
    void *const *a=(void *const*)(image+PWCAM_UIANCHOR_VTABLE),*const *h=(void *const*)(image+PWCAM_HEAD_VTABLE);
    if(a[PWCAM_ISA_SLOT]!=(void*)(image+0x6f7d90u) || h[PWCAM_ISA_SLOT]!=(void*)(image+0x9778e0u) ||
       a[PWCAM_POSE_SLOT]!=(void*)(image+PWCAM_POSE_FN) || h[PWCAM_POSE_SLOT]!=(void*)(image+PWCAM_POSE_FN)) {pin_failed_rva=PWCAM_UIANCHOR_VTABLE;return 0;}
    pwcam_vtables[0]=image+PWCAM_UIANCHOR_VTABLE;pwcam_vtables[1]=image+PWCAM_HEAD_VTABLE;
    return 1;
}
/* The chain holds only what X4's stand-up takes off (at most PWCAM_LINKS links); 0 with the first other vtable. */
static int pwcam_seated_chain(unsigned char *head,const void **other) {
    unsigned char *link=head;
    for(unsigned n=0;link && n<PWCAM_LINKS;n++) {
        void *vt=NULL;
        if(!read_local(link,&vt,8) || (vt!=pwcam_vtables[0] && vt!=pwcam_vtables[1])) {*other=vt;return 0;}
        if(!read_local(link+PWCAM_NEXT,&link,8)) {*other=NULL;return 0;}
    }
    if(link) {*other=NULL;return 0;}
    return 1;
}
/* The head link's place on a chain of the two classes: 0 the first, 1.. later, -1 none. */
static int pwcam_head_at(unsigned char *head) {
    unsigned char *link=head,*next=NULL;
    for(int n=0;link && n<(int)PWCAM_LINKS;n++) {
        void *vt=NULL;
        if(!read_local(link,&vt,8) || !read_local(link+PWCAM_NEXT,&next,8)) return -1;
        if(vt==pwcam_vtables[1]) return n;
        link=next;
    }
    return -1;
}
/* In pres_enter, with the eye's pose in place: a walking slot's camera without slot 1's seated chain, a seated slot's
 * without the head link. */
static void pwcam_enter(unsigned char *camera,const pslot_slot *s) {
    pwcam_open=0;
    const int walk=pwcam_on && s->walking && peye_walk_fpc,seat=!walk && pwcam_seat_on && peye_seat_mode;
    if(!camera || !peye_published || !(walk || seat)) return;
    unsigned char *head=*(unsigned char *volatile*)(camera+PWCAM_CHAIN),*put=NULL;const void *other=NULL;
    if(!head) return;   /* nothing to take off */
    if(!pwcam_seated_chain(head,&other)) {
        InterlockedIncrement64(&pwcam_other);
        pwcam_other_vtable=other && image && (const unsigned char*)other>=image?(unsigned)((const unsigned char*)other-image):0;
        return;
    }
    const int at=seat?pwcam_head_at(head):0;
    if(at<0) return;   /* seated without a head link: no head movement to keep off the UI */
    if(seat && !at && !read_local(head+PWCAM_NEXT,&put,8)) return;   /* the head first: the chain from its next link */
    if(InterlockedCompareExchangePointer((void *volatile*)(camera+PWCAM_CHAIN),put,head)!=head) return;
    pwcam_camera=camera;pwcam_saved=head;pwcam_put=put;pwcam_open=1;
    InterlockedIncrement64(!seat?&pwcam_scopes:at?&pwcam_seat_whole:&pwcam_seat_scopes);
}
/* In pres_leave: slot 1's chain back where X4 keeps it (a value X4 wrote in the scope stays X4's, counted lost). */
static void pwcam_leave(void) {
    if(!pwcam_open) return;
    pwcam_open=0;
    if(InterlockedCompareExchangePointer((void *volatile*)(pwcam_camera+PWCAM_CHAIN),pwcam_saved,pwcam_put)!=pwcam_put) InterlockedIncrement64(&pwcam_lost);
}
static void pwcam_reset(void) {pwcam_open=0;pwcam_camera=pwcam_saved=pwcam_put=NULL;pwcam_scopes=pwcam_other=pwcam_lost=pwcam_seat_scopes=pwcam_seat_whole=0;pwcam_other_vtable=0;}
static int pwcam_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity," walk_cam=%d/%lld/%lld/%lld",pwcam_on,(long long)pwcam_scopes,(long long)pwcam_other,(long long)pwcam_lost);
    if(n>0 && (size_t)n<capacity && pwcam_other_vtable) n+=snprintf(out+n,capacity-(size_t)n,"/%x",pwcam_other_vtable);
    if(n>0 && (size_t)n<capacity) n+=snprintf(out+n,capacity-(size_t)n," seat_cam=%d/%lld/%lld",pwcam_seat_on,(long long)pwcam_seat_scopes,(long long)pwcam_seat_whole);
    return n>0 && (size_t)n<capacity?n:0;
}
