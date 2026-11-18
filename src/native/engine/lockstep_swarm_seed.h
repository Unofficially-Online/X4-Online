/* Swarm missiles fly the same formation on every client (native_gate_swarm_seed, opt-in; Windows gate).
 *
 * Found with every zone at high attention (17 Nov, runs floor24..29): two clients agreed in every digest part for 1,083
 * ticks, then a station's fight.attack.object.station entered its 'Station Attacked' handler on one client only: the
 * station's radar scan (40 km) took a new contact at 99.6% of its bound on that client alone. The contact was one of
 * eight swarm missiles (missile_gen_s_swarm_01_mk1_macro) a ship of another zone had fired at tick 780; the scan probe
 * (native_gate_gravidar_posts_probe with the missile's created ID) had the same missile 33 m apart on the two clients at
 * tick 1083 and 340 m apart four seconds after its launch in another run. The motion watch of two of the missiles
 * (native_gate_motion_watch takes created IDs) shows where it starts: both clients hold the same state in the launch
 * frame, and from the next frame on the flight model's target (the missile's place in its formation) is 58 m and 39 m
 * apart, by a vector that then stays the same for every tick. The frame's shared random word is equal on both clients at
 * every tick of these runs and a missile draws nothing from it there: it is not a random draw the gate could key.
 *
 * Mechanism (X4 9.00, static). U::Missile::Shoot gives a swarm a U::Path::RandomizedFormationShape (vtable 0x2c4fe30,
 * built at 0x60750c). Its slot 6, 0xe9d680 (this, out transform, -, member index, time), answers a member's offset:
 *     seed = (index + 1) * this;                    0xe9d686 inc r9; 0xe9d689 imul r9, rcx
 *     s = pcg(seed); period = 0.5 s + low32(s) * 2^-33 s; seed = s * floor(time / period);
 *     offset = size (this+0x20) * three further pcg values in [-1, 1)
 * The seed is the shape object's ADDRESS, so every client (and every run) places the members of one swarm elsewhere, and
 * with them where each missile flies, what it meets and which radar sees it. A played session has the same hole whenever
 * swarm missiles fly at high attention, i.e. near a player.
 *
 * The fix is one byte: the REX prefix of that imul, 4c -> 4d, makes it `imul r9, r9`: seed = (index + 1)^2. The rest of
 * X4's arithmetic is untouched: a member's offset still changes with its index and with the time, the same on every
 * client. What is lost: two swarms in the air at once now weave alike (the address was the only thing that told them
 * apart).
 * Fail closed: the function is pinned (SHA-256 over all 0x14d bytes with the original prefix), the byte must be the
 * original (or ours); one interlocked byte write, restored on stop; a foreign value is never overwritten.
 * Status installed= why= site=. */
#define SWSEED_FN 0xe9d680u
#define SWSEED_FN_SIZE 0x14du
#define SWSEED_FN_SHA "7fbc82c9fbe274e20f47491cc4e1492f7629aed7fe546f51ee10f20f14f7679e"
#define SWSEED_SITE 0xe9d689u       /* 4c 0f af c9  imul r9, rcx */
#define SWSEED_ORIGINAL 0x4cu
#define SWSEED_PATCHED 0x4du        /* 4d 0f af c9  imul r9, r9 */
static int swseed_installed;
static const char *swseed_why="-";
static unsigned char swseed_site(void) {unsigned char v=0;read_local(image+SWSEED_SITE,&v,1);return v;}
/* The pinned function, whichever of the two prefixes the site holds now. */
static int swseed_pin(void) {
    unsigned char copy[SWSEED_FN_SIZE];
    if(!image || !read_local(image+SWSEED_FN,copy,sizeof(copy))) return 0;
    unsigned char *site=copy+(SWSEED_SITE-SWSEED_FN);
    if(*site!=SWSEED_ORIGINAL && *site!=SWSEED_PATCHED) return 0;
    *site=SWSEED_ORIGINAL;
    return hash_ok(copy,sizeof(copy),SWSEED_FN_SHA);
}
static int swseed_swap(unsigned char expected,unsigned char value) {
    DWORD old,ignored;if(!VirtualProtect(image+SWSEED_SITE,1,PAGE_EXECUTE_READWRITE,&old)) return 0;
    const unsigned char actual=(unsigned char)_InterlockedCompareExchange8((volatile char*)(image+SWSEED_SITE),(char)value,(char)expected);
    VirtualProtect(image+SWSEED_SITE,1,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+SWSEED_SITE,1);
    return actual==expected;
}
/* gate.swarm_seed(1). 1: the site holds our prefix (now or already). */
static int swseed_arm(void) {
    if(swseed_installed) return swseed_site()==SWSEED_PATCHED;
    if(!image) {swseed_why="no_image";return 0;}
    if(!swseed_pin()) {swseed_why="pin";return 0;}
    if(swseed_site()!=SWSEED_ORIGINAL) {swseed_why="site";return 0;}
    if(!swseed_swap(SWSEED_ORIGINAL,SWSEED_PATCHED)) {swseed_why="swap";return 0;}
    swseed_installed=1;swseed_why="-";return 1;
}
static void swseed_restore(void) {
    if(swseed_installed && swseed_swap(SWSEED_PATCHED,SWSEED_ORIGINAL)) swseed_installed=0;
}
static int swseed_status(char *out,size_t capacity) {
    return snprintf(out,capacity,"installed=%d why=%s site=%02x",swseed_installed,swseed_why,image?swseed_site():0u);
}
