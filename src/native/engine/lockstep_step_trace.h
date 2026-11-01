/* Diagnostic, DEFAULT OFF, read-only: entry and exit of LinearFlightControlModel's step for watched ships
 * (native_gate_step_trace = {saved IDs, up to four}, native_gate_step_trace_ticks = N, ticks 1..N, default 3;
 * needs native_gate_world_identities). Option B evidence E2 (reports/LOCKSTEP-LINUX-PORT-PLAN-20261030.txt).
 *
 * win-sub3 against vps-linux-23: every object agrees at tick 1, and at tick 2 574 ships differ in motion by
 * one ulp, positions still equal. W1 (motion watch): the three watched ones are formation members at
 * attention 2 on the Linear model, no Jolt body (the one active scene's 37 bodies do not change). So the step
 * that wrote their tick-2 state is the Linear model's: Windows 0xeb27c0, slot 14 of the vtable 0x2c4f610
 * (entry 0x2c4f680); Linux 0x13ed050 in its vtable entry 0x3b62558. Both take (model, const vector *, float
 * dt, double time) and return nothing; the model's agent is at +0x10 (Linux +0x8), the agent's ship at
 * +0xef0 (Linux +0xf10). Model fields sit 0x10 lower on Linux (+0x88 the last step time is +0x78 there).
 *
 * The vtable entry is swapped by compare-exchange to st_step, which calls the step it replaced. For a model
 * whose ship's portable key is watched, in ticks 1..N, it keeps a record: the tick, the call's ordinal for that
 * ship in the tick, the thread (MoveWorker or other), the vector argument (16 bytes), dt and time, and the
 * model's first 0x200 bytes and the agent's first 0x540 bytes (its three 0x1a0 flight parameter slots) before
 * and after the call, and the agent's two pathing state banks (pose: position, three rotation rows, linear and
 * angular velocity, 0x60 bytes of each 0x1a0; Windows agent+0xad0, Linux +0xaf0, ST_AGENT_POSE) with the read and
 * written bank index (Windows 0x38f4ee4 / 0x2f4b6f4) before and after. W4 vs L4 (FR6's redirect on): at tick 2
 * the one watched ship that still differs is 0x7dd7c, in rotation rows of its pose, which the first 0x540 bytes
 * never covered. Each record prints as 25 STEP_TRACE_ROW lines of at most 0x100 bytes. Nothing is written to the
 * game; a full ring drops records (counted). Stop puts the entry back.
 *
 * FR8, the pitch chain (native_gate_step_chain = true with the step trace; default off, read-only): FR7 found the
 * Linear step writing 7dd7c's tick-2 rotation from equal inputs, sin(pitch) one ulp apart. The calls of the step's
 * attitude block are hooked by their rel32 (each hook calls the original and records): the rotation builder (three
 * angles in, rows +0x10..+0x3f of the written bank after), asinf of the current and of the target pitch, and on
 * Windows the approach helper's three calls (yaw, pitch, roll: angle in, target, rate, dt, wrap period, angle out;
 * Linux has it inline, so there the builder's stub also passes the step's dt [rsp+0x80] and its agent r12, whose
 * rates +0x378..+0x380 the row carries). Only inside a watched step (a per-thread mark set by st_step) is anything
 * recorded: one more row per record, part=chain, ST_CHAIN_WORDS words (layout at st_chain_call). The sites are the
 * platform's (lockstep_step_chain_win.h, linux/gate_step_trace.h); arming fails closed on any byte that differs. */
#define ST_IDS 4u
#define ST_RECORDS 512u /* FR17: four ships over 60 ticks; the ring is taken from the heap at arming */
#define ST_MODEL_BYTES 0x200u
#define ST_AGENT_BYTES 0x540u
#define ST_POSE_BYTES 0x340u /* both banks */
#define ST_CHUNK 0x100u
#define ST_CHUNKS 25u /* args, model before 2, model after 2, agent before 6, agent after 6, pose before 4, pose after 4 */
#define ST_CHAIN_WORDS 56u
#define ST_CHAIN_TAG 0x46523801u /* "FR8", layout 1 */
#define ST_CHAIN_HEAD 21u
#define ST_CHAIN_SLOTS 5u
#define ST_LIVE 64u
/* FR9 (with FR8): FR8 found the split one level up, in T: 0x7dd7c's tick-2 target pitch asinf input T.y, W ba6fab46 /
 * L ba6fab45, everything else of the chain equal. T.y is lane 1 of the normalised tangent of the model's path spline
 * (Math::CompositeSpline<5> at model+ST_MODEL_SPLINE, Windows 0x30fc40 -> BezierSpline 0x14e4a80, Linux 0x11672e0
 * inline: the same operations), which the step itself builds when it sets model+0x130 (Windows 0xeb1e60, Linux
 * 0x1151760). So after the step the spline is kept: n, its 0x30-byte header (+0x8 k, +0x10/+0x18 segments begin/end,
 * +0x28 starts), the first ST_SPLINE_SEGMENTS starts (8 bytes each) and segments (0xa0 each: +0x18 length, +0x20/+0x30
 * /+0x40 the tangent's three coefficient vectors); four more rows a record, part=spline. */
#define ST_SPLINE_SEGMENTS 5u
#define ST_SPLINE_SEGMENT 0xa0u
#define ST_SPLINE_BYTES (0x38u+ST_SPLINE_SEGMENTS*8u+ST_SPLINE_SEGMENTS*ST_SPLINE_SEGMENT) /* 0x380 */
#define ST_SPLINE_CHUNKS 4u
/* FR17 (with FR8): W9 vs L6 splits whole groups of formation wingmen at once while their leader stays equal, so the
 * wingman's target from an equal leader is measured. Formation::GetTargetPositionForWingMan (Windows 0xe99d70, Linux
 * 0x1e592d0; formation, out mat4, member index, double time; the members' agents at formation+0x48, [0] the leader, the
 * shape at +0x60) is hooked at its two calls among the platform's chain sites. Its leader pose is a plain copy of the
 * leader agent's read bank (Windows 0xe8a740 flag 0, Linux 0x1427900), so the hook reads the same bytes. For a watched
 * wingman in ticks 1..N it keeps the first ST_FM_ENTRIES calls since its previous step record, ST_FM_WORDS words each:
 * [0] ST_FM_TAG, [1] tick, [2] which call (0 the agent's wrapper Windows 0xe8b990 / Linux 0x14285e0, 1 the other),
 * [3] index, [4] members, [5..6] time, [7] result, [8] thread (0 ui), [9] read bank, [10..11] the wingman's saved ID,
 * [12..13] the leader's, [14] the shape's vtable less the image base, [15] the call's ordinal; [16..31] the leader's read
 * bank +0x00..+0x3f (position, three rows), [32..63] the shape's first 0x80 bytes, [64..79] out after, [80..95] out
 * before, [96..111] the wingman's own read bank +0x00..+0x3f, [112..126] the leader's bank +0x40..+0x7b (velocities),
 * [127] calls since the previous record (set when copied). The next step record of that ship carries them:
 * ST_FM_CHUNKS more rows, part=formation. */
#define ST_FM_ENTRIES 3u
#define ST_FM_WORDS 128u
#define ST_FM_TAG 0x46521701u /* "FR17", layout 1 */
#define ST_FM_CHUNKS (ST_FM_ENTRIES*ST_FM_WORDS*4u/ST_CHUNK) /* 6 */
#define ST_FM_MEMBERS 0x48
#define ST_FM_SHAPE 0x60
#define ST_BANK_STRIDE 0x1a0u
/* FR18 (with FR8): FR17 cleared the formation call: every complete call is equal on both platforms. The wingman's model
 * target is instead replaced once by a far, yaw-only point (08474d tick 58, 09d046 57, 0adfd8 48, 0adbe8 27), at the
 * same tick on both but not to the same value: UpdatePathing's non-formation SetTarget, whose matrix is the agent's
 * first path point (a reference object's runtime id at point+ST_PT_REF, a sub id after it, an offset mat4 at +0x20)
 * converted into the frame the model flies in. Windows converts in one call (0xe8be60 at 0xe8cf11: rdx out, r8 the
 * point, r9 the frame object, two more objects on the stack; its rcx is dead, so the stub passes the agent there);
 * Linux has those cases inline in 0x1431270 and calls the converters itself: the point's local transform 0x14bd2d0 at
 * 0x1431aca, the conversion into a frame 0x141fa50 at 0x14326d4 and 0x14326eb (its stubs pass the agent, rbx, in rcx).
 * Inside, the step from the reference object's frame into the frame object's is one call on both (Windows 0xe8a4a0 in
 * 0xe8bd20 at 0xe8bdeb, Linux 0x141f7e0 in 0x141fa50 at 0x141fab0: out, velocity, &ref, &frame, flag); it is hooked as
 * well and records only inside an outer call on the same thread (a thread key table like FR8's; no TLS). For a watched
 * ship in ticks 1..N the first ST_PT_ENTRIES outer calls since its previous step record are kept, COMPLETE ONLY: each
 * entry is built on the stack and published after the call under the ship's slot lock, which the step record's take
 * holds as well (FR17's formation entries likewise now: FR17 had torn ones). ST_PT_WORDS words an entry: [0] ST_PT_TAG,
 * [1] tick, [2] site (Windows 0; Linux 0 the local 0x1431aca, 1 0x14326d4, 2 0x14326eb), [3] result (al; Linux 1),
 * [4] thread (0 ui), [5] read bank, [6..7] the ship's saved ID, [8..9] the point's reference object's key, [10..11] the
 * frame object's, [12..15] Windows' two stack objects', [16] conversions inside, [17] the entry's ordinal since the
 * previous record, [18] its ordinal in the tick, [19..20] the conversion's reference object's key, [21..22] its frame
 * object's; [24..55] the point's first 0x80 bytes, [56..71] out after, [72..87] the conversion's matrix before (the
 * local transform), [88..103] after, [104..127] the conversion's reference object's controller (+0x200) bytes
 * +0x00..+0x5f, [128..151] its frame object's, [152..167] the agent's read bank +0x00..+0x3f, [168..169] / [170..171]
 * the reference / frame object's parent's (+0x70) key, [511] calls since the previous record (set when taken). Keys are
 * world_identity_key of the runtime id (any class), 0 when none. ST_PT_CHUNKS more rows a record, part=path.
 *
 * FR20 (layout 2, diagnostic): FR19 put the tick-2 split of 8bd90 at UpdatePathing's last SetTarget (Windows caller
 * 0xe8d777, Linux 0x1431b62) with the stored point equal on both and the matrix passed 1-16 ulp apart. Its conversions
 * are other calls than FR18's outer ones: Windows 0xe8bd20 at 0xe8d502 / 0xe8d60f / 0xe8d6bc and the object path
 * 0x3dde10 at 0xe8d5ea (the reference object's own transform into the frame), Linux 0x141fa50 at 0x143235b and
 * 0x18a2900 at 0x1432611. Their outer sites are added (which: Windows 1 / 2 / 3 / 4, Linux 3 / 4, 4 the object path,
 * whose conversion is the call itself: [19..22] its object and frame, [72..103] the matrix before and after). Every
 * float operation of the conversion read so far has the same order on both (the composition up the parent chain,
 * Windows 0xe8a110 / 0x3dde10 / 0x7461c0, Linux 0x141f420 / 0x18a2900 / 0x13142a0, and the transposed inverse), no
 * division, the inputs copies; so its inputs are measured: after the conversion, each object of the reference's and
 * of the frame's parent chain (+0x70) is put through the platform's own hierarchy function from itself to its parent
 * on an identity matrix (Windows 0x3dde10(from, inout, to, 0), Linux 0x18a2900(rdi, rsi, rdx, ecx 0)), which yields
 * that object's local transform exactly as the engine composes it (identity times M is M, bit for bit).
 * [192..351] the reference chain, [352..511) the frame chain: ST_PT_LEVELS levels of ST_PT_LEVEL_WORDS words: [0..1]
 * the object's key, [2] 1 when it has a parent and was probed (0: the chain's top), [3] its vtable less the image base,
 * [4..19] its local transform (pos, three rows). [172..175] hold the two object pointers between the conversion's
 * begin and end only (zero when kept). */
#define ST_PT_ENTRIES 3u
#define ST_PT_WORDS 512u
#define ST_PT_TAG 0x46521802u /* "FR18", layout 2 (FR20) */
#define ST_PT_CHUNKS (ST_PT_ENTRIES*ST_PT_WORDS*4u/ST_CHUNK) /* 24 */
#define ST_PT_SCRATCH 172u
#define ST_PT_LEVELS 8u
#define ST_PT_LEVEL_WORDS 20u
#define ST_PT_REF_LEVELS 192u
#define ST_PT_FRAME_LEVELS (ST_PT_REF_LEVELS+ST_PT_LEVELS*ST_PT_LEVEL_WORDS) /* 352 */
#define ST_OBJ_CONTROLLER 0x200
#define ST_OBJ_PARENT 0x70
/* FR19 (with FR8): FR18's run (W fr18 / L FR18) never reached Windows' 0xe8cf11 (pt:0) and caught Linux's local path
 * only at ticks 3-4 and 53/56, not at the replacements (58/48/27): the target is set elsewhere. So SetTarget itself is
 * hooked: the Linear model's vtable slot (Windows 0x2c4f628 -> 0xeaf620, Linux 0x3b62500 -> 0x141d140; no direct calls
 * on either; it copies the mat4 to model+ST_MODEL_TARGET and stores a3 and a4 != 0 after it), swapped like the step's
 * entry. For a watched ship in ticks 1..N the first ST_SG_ENTRIES calls since its previous step record are kept,
 * complete only, ST_SG_WORDS words each: [0] ST_SG_TAG, [1] tick, [2] the caller (return address less the image base;
 * Linux add 0x400000), [3] thread (0 ui), [4] read bank, [5] ordinal in the tick, [6..7] the ship's saved ID, [8..9] a3,
 * [10..11] a3's key (as an object), [12..13] a4, [14] the entry's ordinal since the previous record; [16..31] the mat4
 * passed, [32..47] the model's target before, [48..51] the 16 bytes after it before (a3, flags), [52..83] the agent's
 * first path point's 0x80 bytes (Windows [agent+0x6a8], Linux [agent+0x6d0], a pointer each), [84..99] the agent's read
 * bank +0x00..+0x3f, [100..115] the model's target after, [127] calls since the previous record. ST_SG_CHUNKS more rows a
 * record, part=settarget. */
#define ST_SG_ENTRIES 4u
#define ST_SG_WORDS 128u
#define ST_SG_TAG 0x46521901u /* "FR19", layout 1 */
#define ST_SG_CHUNKS (ST_SG_ENTRIES*ST_SG_WORDS*4u/ST_CHUNK) /* 8 */
/* FR20 (Windows only, with FR8): the wingmen's stored path point already differs when SetTarget converts it (08474d
 * tick 58, 0adbe8 27, 0adfd8 48). The agent's path is built by 0xe86bb0(rcx agent, rdx the path description, r8 a
 * string, r9 an out pointer), the only builder of the agent's FlightPoints from a description (through 0xf22ee0 /
 * 0xf29970), called by the PathingManager at 0xeac498. Its rel32 is hooked: for a watched agent in ticks 1..N the
 * first ST_PS_ENTRIES calls since the previous step record are kept, ST_PS_WORDS words each: [0] ST_PS_TAG, [1] tick,
 * [2] thread (0 ui), [3] the result's low word, [4..5] the ship's saved ID, [6] the entry's ordinal since the previous
 * record, [7] frames captured, [8..39] the call stack (RtlCaptureStackBackTrace from the hook: return addresses less
 * the image base, 0xfffffffe outside the image, 0xffffffff none), [40..167] the description's first 0x200 bytes,
 * [168..199] the agent's first path point before, [200..231] after (0xff when none), [232] 1 when the point's address
 * changed, [233] the ordinal in the tick, [255] calls since the previous record. ST_PS_CHUNKS more rows, part=pathset
 * (zero on Linux). */
#define ST_PS_ENTRIES 2u
#define ST_PS_WORDS 256u
#define ST_PS_FRAMES 32u
#define ST_PS_TAG 0x46522001u /* "FR20", layout 1 */
#define ST_PS_CHUNKS (ST_PS_ENTRIES*ST_PS_WORDS*4u/ST_CHUNK) /* 8 */
/* FR21 (with FR8): FR20's 8bd90 conversion at tick 2 has a HIGHWAY as its reference (saved 0xa6, runtime 0x1b76d; the
 * level keys are saved IDs). The conversion (Windows 0xe8a4a0, Linux 0x141f7e0) first calls the reference's vfunc that
 * maps a point given in its own coordinates (Windows vtable +0x1520, Linux +0x1568; an empty stub for zones, sectors
 * and the like): the Highway's (Windows 0x5f6480, Linux 0x15cf8d0) reads (x, y, t) on its spline [hw+ST_HW_SPLINE], a
 * Math::AffineSpline<3> over a Math::CompositeSpline<3> of Bezier segments, with the banking 0x5f7520 / 0x15cf540
 * (hw, t). Its slot in the Highway vtable is swapped (like FR19's SetTarget slot): inside a watched ship's open path
 * entry (FR18/FR20) the first call since the previous record is kept, COMPLETE ONLY, with the memory the spline
 * evaluation reads, for an offline emulation of both natives (scratchpad fr/hw21emu.py); the banking's call sites
 * record their result into the open capture. ST_HW_WORDS words: [0] ST_HW_TAG, [1] tick, [2] thread (0 ui), [3]
 * read bank, [4..5] the ship's saved ID, [6..7] the image base, [8..9] the highway, [10..11] its key, [12] banking calls
 * inside, [13] the first one's t, [14] its result, [15] memory blocks, [16..31] the matrix before, [32..47] after,
 * [48..127] the highway's bytes +ST_HW_BASE..+0x140, [128..] blocks of {address (2 words), bytes, 0} and the bytes
 * (padded to 16): the AffineSpline (0x40), its knots, the CompositeSpline (0x40), its knot pairs, its segments (0xa0
 * each, as many as fit), [ST_HW_WORDS-1] calls since the previous record. ST_HW_CHUNKS more rows, part=hwcap. */
#define ST_HW_WORDS 2048u
#define ST_HW_TAG 0x46522101u /* "FR21", layout 1 */
#define ST_HW_CHUNKS (ST_HW_WORDS*4u/ST_CHUNK) /* 32 */
#define ST_HW_BASE 0x500u
#define ST_HW_BLOCKS 128u
/* FR21 (Windows only, with FR8): who builds the wingmen's new path point (FR20's builder hook 0xe86bb0 kept nothing).
 * New points are appended to the agent's pending points (+0x6c0) by 0xe89e40 (rcx agent; from 0xe95f7e), 0xe8afd0
 * (from 0xe9acd0) and 0xe89ed0 (rcx the vector; from 0xe8ef47), and the path is cleared by 0xe89f10 (rcx agent, dl
 * flag; seven calls). Each call of a watched agent keeps a path-set entry (FR20's layout, tag ST_PC_TAG): [3] the site,
 * [8..39] the call stack, [40..45] the pending and current points' vector headers, [52..87] the first pending point,
 * [168..199] / [200..231] the first current point before / after. FR22: 0xe89ed0's vector is the CURRENT points'
 * (agent +0x6a8; FR21 looked for the agent at rcx-0x6c0 and kept nothing), and the entry also holds [88..123] the point
 * the call adds (0x90 bytes; pc_add2's 0x40), [124..159] the current points' last one after the call, [234..239] their
 * vector header after it. */
#define ST_PC_TAG 0x46522102u
typedef void (*st_hier_fn)(void *,void *,void *,uint64_t);
typedef void (*st_sg_fn)(void *,const void *,void *,void *);
typedef void (*st_step_fn)(void *,const void *,float,double);
typedef uint64_t (*st_fm_fn)(void *,void *,uint64_t,double);
typedef struct {
    volatile LONG64 ready;uint64_t tick,key;uint32_t ordinal,thread,has_agent;float dt;double time;uint32_t banks_pre[2],banks_post[2];
    unsigned char vector[16],model_pre[ST_MODEL_BYTES],model_post[ST_MODEL_BYTES],agent_pre[ST_AGENT_BYTES],agent_post[ST_AGENT_BYTES];
    unsigned char pose_pre[ST_POSE_BYTES],pose_post[ST_POSE_BYTES];
    uint32_t chain[ST_CHAIN_WORDS];
    unsigned char spline[ST_SPLINE_BYTES]; /* [0..4) n, [8..0x38) header, then starts, then segments */
    uint32_t formation[ST_FM_ENTRIES*ST_FM_WORDS]; /* FR17 */
    uint32_t path[ST_PT_ENTRIES*ST_PT_WORDS]; /* FR18 */
    uint32_t settarget[ST_SG_ENTRIES*ST_SG_WORDS]; /* FR19 */
    uint32_t pathset[ST_PS_ENTRIES*ST_PS_WORDS]; /* FR20 */
    uint32_t hwcap[ST_HW_WORDS]; /* FR21 */
} st_record_t;
/* FR9: the model's spline after the step, read only. */
static void st_spline_read(const unsigned char *model,unsigned char *out) {
    memset(out,0,ST_SPLINE_BYTES);
    if(!read_local(model+ST_MODEL_SPLINE,out+8,0x30)) {memset(out,0xff,ST_SPLINE_BYTES);return;}
    uint64_t begin,end,starts;memcpy(&begin,out+8+0x10,8);memcpy(&end,out+8+0x18,8);memcpy(&starts,out+8+0x28,8);
    const uint64_t n=(end>begin && (end-begin)%ST_SPLINE_SEGMENT==0 && end-begin<=0x100000)?(end-begin)/ST_SPLINE_SEGMENT:0;
    const uint32_t n32=(uint32_t)n,m=n32<ST_SPLINE_SEGMENTS?n32:ST_SPLINE_SEGMENTS;memcpy(out,&n32,4);
    if(!m) return;
    if(!starts || !read_local((const void*)(uintptr_t)starts,out+0x38,m*8u)) memset(out+0x38,0xff,m*8u);
    if(!read_local((const void*)(uintptr_t)begin,out+0x38+ST_SPLINE_SEGMENTS*8u,m*ST_SPLINE_SEGMENT)) memset(out+0x38+ST_SPLINE_SEGMENTS*8u,0xff,m*ST_SPLINE_SEGMENT);
}
/* The read and written pathing bank index (0xffffffff when unreadable). */
static void st_banks(uint32_t out[2]) {
    if(!read_local(ST_BANK_READ,&out[0],4)) out[0]=0xffffffffu;
    if(!read_local(ST_BANK_WRITE,&out[1],4)) out[1]=0xffffffffu;
}
static st_record_t *st_ring; /* ST_RECORDS records, calloc at the first arming, kept */
static uint64_t st_ids[ST_IDS],st_ordinals[ST_IDS],st_ordinal_tick[ST_IDS];
static unsigned st_id_count,st_installed,st_restored;
static uint64_t st_ticks=3;
static volatile LONG64 st_records,st_calls,st_watched_calls,st_dropped;
static void *st_native; /* the step the entry held */
static const char *st_stage="idle";
static int st_slot_swap(void *expected,void *value);
/* FR8: the platform's sites (ST_CHAIN_SITE_COUNT of them), their arming, restoration and ownership. */
static unsigned st_chain_wanted,st_chain_installed,st_chain_restored,st_chunks=ST_CHUNKS;
static const char *st_chain_stage="off";
static volatile LONG64 st_chain_hits[ST_CHAIN_SITE_COUNT],st_chain_recorded,st_chain_no_slot;
static int st_chain_arm(void);
static void st_chain_restore(void);
static int st_chain_owned(void);
static const char *st_chain_site_name(unsigned i);
/* The record of the watched step running on this thread: thread keys claimed in order and kept (X4's threads are a
 * fixed pool), each with the record it is inside; st_live_open counts the open ones so other ships' calls return at once. */
static volatile LONG64 st_live_keys[ST_LIVE],st_live_open;
static st_record_t *st_live_records[ST_LIVE];
static st_record_t *st_live(void) {
    if(!InterlockedCompareExchange64(&st_live_open,0,0)) return NULL;
    const LONG64 key=(LONG64)GetCurrentThreadId()+1;
    for(unsigned i=0;i<ST_LIVE;i++) {const LONG64 k=st_live_keys[i];if(k==key) return st_live_records[i];if(!k) break;}
    return NULL;
}
static int st_live_begin(st_record_t *r,st_record_t **prior) {
    const LONG64 key=(LONG64)GetCurrentThreadId()+1;
    for(unsigned i=0;i<ST_LIVE;i++) {
        LONG64 k=st_live_keys[i];
        if(!k && (k=InterlockedCompareExchange64(&st_live_keys[i],key,0))==0) k=key;
        if(k==key) {*prior=st_live_records[i];st_live_records[i]=r;InterlockedIncrement64(&st_live_open);return (int)i;}
    }
    InterlockedIncrement64(&st_chain_no_slot);return -1;
}
static void st_live_end(int i,st_record_t *prior) {if(i>=0) {st_live_records[i]=prior;InterlockedAdd64(&st_live_open,-1);}}
static uint32_t st_bits(float f) {uint32_t u;memcpy(&u,&f,4);return u;}
/* The chain row's words: [0] ST_CHAIN_TAG; [1] builder calls, [2..4] its yaw, pitch, roll, [5..16] rows +0x10..+0x3f
 * after it, [17] dt and [18..20] the rates (Linux; 0 on Windows); then five slots of 7 words from [21]: calls, in,
 * target, rate, dt, period, out (0..2 Windows' approach helper for yaw, pitch, roll; 3 asinf of the current pitch,
 * 4 asinf of the target pitch: in and out only). A slot keeps its first call in the step and counts the rest. */
static void st_chain_call(unsigned site,unsigned slot,float in,float target,float rate,float dt,float period,float out) {
    InterlockedIncrement64(&st_chain_hits[site]);
    st_record_t *r=st_live();if(!r || slot>=ST_CHAIN_SLOTS) return;
    uint32_t *w=r->chain+ST_CHAIN_HEAD+slot*7u;
    if(!w[0]++) {w[1]=st_bits(in);w[2]=st_bits(target);w[3]=st_bits(rate);w[4]=st_bits(dt);w[5]=st_bits(period);w[6]=st_bits(out);}
    InterlockedIncrement64(&st_chain_recorded);
}
static void st_chain_builder(unsigned site,const unsigned char *rows,float yaw,float pitch,float roll,const unsigned char *dt,const unsigned char *rates) {
    InterlockedIncrement64(&st_chain_hits[site]);
    st_record_t *r=st_live();if(!r) return;
    uint32_t *w=r->chain;
    if(!w[1]++) {
        w[2]=st_bits(yaw);w[3]=st_bits(pitch);w[4]=st_bits(roll);
        if(!rows || !read_local(rows,w+5,0x30)) memset(w+5,0xff,0x30);
        if(dt && !read_local(dt,w+17,4)) w[17]=0xffffffffu;
        if(rates && !read_local(rates,w+18,12)) memset(w+18,0xff,12);
    }
    InterlockedIncrement64(&st_chain_recorded);
}
/* step_chain(): before arming; the reload's rerun once armed answers what was chosen. */
static int st_chain_want(void) {
    if(st_installed) return (int)st_chain_wanted;
    st_chain_wanted=1;return 1;
}
/* The agent's ship's saved ID in *key (0 if none) and its watched index, or -1. */
static int st_agent_watched(const unsigned char *agent,uint64_t *key) {
    const unsigned char *ship=NULL;uint64_t runtime=0;unsigned klass=WORLD_KEY_NONE;
    *key=0;
    if(!agent || !read_local(agent+ST_AGENT_OWNER,&ship,8) || !ship || !read_local(ship+8,&runtime,8) || !runtime) return -1;
    const uint64_t saved=world_identity_key(runtime,&klass);
    if(!saved || klass!=WORLD_KEY_SAVED) return -1;
    *key=saved;
    for(unsigned i=0;i<st_id_count;i++) if(st_ids[i]==saved) return (int)i;
    return -1;
}
/* The watched index of the model's ship, or -1. */
static int st_watched(const unsigned char *model,const unsigned char **agent_out,uint64_t *key_out) {
    const unsigned char *agent=NULL;uint64_t key=0;int i;
    *agent_out=NULL;*key_out=0;
    if(!model || !read_local(model+ST_MODEL_AGENT,&agent,8) || !agent || (i=st_agent_watched(agent,&key))<0) return -1;
    *agent_out=agent;*key_out=key;return i;
}
/* FR17/FR18: each watched ship's formation calls and path point conversions since its previous step record. The calls
 * run on worker threads and may overlap the ship's own step (FR17 measured it), so an entry is built on the caller's
 * stack and published complete under the slot's lock; the step record's take holds the same lock. */
static struct {
    volatile LONG64 lock;uint32_t calls,pt_calls,pt_tick,pt_in_tick,sg_calls,sg_tick,sg_in_tick,ps_calls,ps_tick,ps_in_tick,hw_calls;
    uint32_t hwcap[ST_HW_WORDS];
    uint32_t words[ST_FM_ENTRIES*ST_FM_WORDS];
    uint32_t path[ST_PT_ENTRIES*ST_PT_WORDS];
    uint32_t settarget[ST_SG_ENTRIES*ST_SG_WORDS];
    uint32_t pathset[ST_PS_ENTRIES*ST_PS_WORDS];
} st_fm[ST_IDS];
static volatile LONG64 st_fm_kept,st_pt_kept,st_pt_conversions,st_pt_no_slot,st_sg_calls,st_sg_kept,st_pt_levels_probed,st_ps_kept;
static volatile LONG64 st_hw_calls,st_hw_kept,st_hw_banking,st_pc_kept; /* FR21 */
static void st_slot_lock(int w) {while(InterlockedCompareExchange64(&st_fm[w].lock,1,0)!=0) {}}
static void st_slot_unlock(int w) {InterlockedExchange64(&st_fm[w].lock,0);}
/* The read bank +offset of an agent, n bytes (0xff when unreadable). */
static void st_fm_bank(const unsigned char *agent,uint32_t bank,unsigned offset,void *out,size_t n) {
    if(!agent || bank>1 || !read_local(agent+ST_AGENT_POSE+bank*ST_BANK_STRIDE+offset,out,n)) memset(out,0xff,n);
}
/* A hooked call of GetTargetPositionForWingMan: the original exactly once, with the same arguments and result. */
static uint64_t st_fm_call(unsigned site,unsigned which,void *native,void *formation,void *out,uint64_t index,double time) {
    InterlockedIncrement64(&st_chain_hits[site]);
    const uint64_t tick=stats.issued;const unsigned char *f=formation,*agent=NULL,*leader=NULL;
    uint64_t begin=0,end=0,key=0;int w=-1;
    if(stats.active && boot_phase==3 && tick>=1 && tick<=st_ticks && f && read_local(f+ST_FM_MEMBERS,&begin,8) &&
       read_local(f+ST_FM_MEMBERS+8,&end,8) && end>begin && end-begin<=0x10000 && index<(end-begin)/8 &&
       read_local((const void*)(uintptr_t)(begin+index*8),&agent,8))
        w=st_agent_watched(agent,&key);
    if(w<0) return ((st_fm_fn)native)(formation,out,index,time);
    uint32_t e[ST_FM_WORDS];memset(e,0,sizeof(e));
    e[0]=ST_FM_TAG;e[1]=(uint32_t)tick;e[2]=which;e[3]=(uint32_t)index;e[4]=(uint32_t)((end-begin)/8);memcpy(e+5,&time,8);
    e[8]=GetCurrentThreadId()==ui_thread?0:1;
    uint32_t bank=0xffffffffu;if(!read_local(ST_BANK_READ,&bank,4)) bank=0xffffffffu;
    e[9]=bank;memcpy(e+10,&key,8);
    uint64_t leader_key=0;const unsigned char *shape=NULL,*vtable=NULL;
    if(read_local((const void*)(uintptr_t)begin,&leader,8) && leader) st_agent_watched(leader,&leader_key);
    memcpy(e+12,&leader_key,8);
    if(!read_local(f+ST_FM_SHAPE,&shape,8) || !shape || !read_local(shape,&vtable,8)) vtable=NULL;
    e[14]=vtable?(uint32_t)((uintptr_t)vtable-(uintptr_t)image):0xffffffffu;
    st_fm_bank(leader,bank,0,e+16,0x40);
    if(!shape || !read_local(shape,e+32,0x80)) memset(e+32,0xff,0x80);
    if(!out || !read_local(out,e+80,0x40)) memset(e+80,0xff,0x40);
    st_fm_bank(agent,bank,0,e+96,0x40);
    st_fm_bank(leader,bank,0x40,e+112,0x3c);
    const uint64_t result=((st_fm_fn)native)(formation,out,index,time);
    e[7]=(uint32_t)(result&0xff);
    if(!out || !read_local(out,e+64,0x40)) memset(e+64,0xff,0x40);
    st_slot_lock(w);
    const uint32_t n=st_fm[w].calls++;
    if(n<ST_FM_ENTRIES) {e[15]=n;memcpy(st_fm[w].words+n*ST_FM_WORDS,e,sizeof(e));InterlockedIncrement64(&st_fm_kept);}
    st_slot_unlock(w);
    return result;
}
/* FR18: the outer call open on this thread, for the conversion inside it (claimed and kept like st_live's keys). */
static volatile LONG64 st_pt_keys[ST_LIVE],st_pt_open;
static uint32_t *st_pt_entries[ST_LIVE];
static uint32_t *st_pt_live(void) {
    if(!InterlockedCompareExchange64(&st_pt_open,0,0)) return NULL;
    const LONG64 key=(LONG64)GetCurrentThreadId()+1;
    for(unsigned i=0;i<ST_LIVE;i++) {const LONG64 k=st_pt_keys[i];if(k==key) return st_pt_entries[i];if(!k) break;}
    return NULL;
}
static int st_pt_live_begin(uint32_t *e,uint32_t **prior) {
    const LONG64 key=(LONG64)GetCurrentThreadId()+1;
    for(unsigned i=0;i<ST_LIVE;i++) {
        LONG64 k=st_pt_keys[i];
        if(!k && (k=InterlockedCompareExchange64(&st_pt_keys[i],key,0))==0) k=key;
        if(k==key) {*prior=st_pt_entries[i];st_pt_entries[i]=e;InterlockedIncrement64(&st_pt_open);return (int)i;}
    }
    InterlockedIncrement64(&st_pt_no_slot);return -1;
}
static void st_pt_live_end(int i,uint32_t *prior) {if(i>=0) {st_pt_entries[i]=prior;InterlockedAdd64(&st_pt_open,-1);}}
/* The key of a runtime id / of the object at p (its runtime id at +8); 0 when none. */
static uint64_t st_runtime_key(uint64_t runtime) {unsigned klass=WORLD_KEY_NONE;return runtime?world_identity_key(runtime,&klass):0;}
static uint64_t st_object_key(const unsigned char *object) {
    uint64_t runtime=0;
    return object && read_local(object+8,&runtime,8)?st_runtime_key(runtime):0;
}
/* An object's controller bytes +0x00..+0x5f (0xff when unreadable) and its parent's key. */
static void st_pt_object(const unsigned char *object,uint32_t *controller,uint32_t *parent_key) {
    const unsigned char *c=NULL,*parent=NULL;uint64_t k=0;
    if(!object || !read_local(object+ST_OBJ_CONTROLLER,&c,8) || !c || !read_local(c,controller,0x60)) memset(controller,0xff,0x60);
    if(object && read_local(object+ST_OBJ_PARENT,&parent,8) && parent) k=st_object_key(parent);
    memcpy(parent_key,&k,8);
}
typedef struct {int watched,live;uint32_t *prior;uint32_t e[ST_PT_WORDS];} st_pt_ctx;
/* Before the outer converter call: 1 when the agent's ship is watched in the window (the entry begun), else 0. */
static int st_pt_begin(st_pt_ctx *c,unsigned site,unsigned which,const unsigned char *agent,const unsigned char *point,
                       const unsigned char *frame,const unsigned char *a5,const unsigned char *a6) {
    InterlockedIncrement64(&st_chain_hits[site]);
    const uint64_t tick=stats.issued;uint64_t key=0,k=0,runtime=0;
    c->watched=-1;c->live=-1;c->prior=NULL;
    if(!stats.active || boot_phase!=3 || tick<1 || tick>st_ticks || (c->watched=st_agent_watched(agent,&key))<0) return 0;
    uint32_t *e=c->e;memset(c->e,0,sizeof(c->e));
    e[0]=ST_PT_TAG;e[1]=(uint32_t)tick;e[2]=which;e[4]=GetCurrentThreadId()==ui_thread?0:1;
    uint32_t bank=0xffffffffu;if(!read_local(ST_BANK_READ,&bank,4)) bank=0xffffffffu;
    e[5]=bank;memcpy(e+6,&key,8);e[23]=(uint32_t)c->watched+1u; /* FR21: the slot, for the highway capture */
    if(point && read_local(point+ST_PT_REF,&runtime,8)) k=st_runtime_key(runtime);
    memcpy(e+8,&k,8);
    k=st_object_key(frame);memcpy(e+10,&k,8);
    k=st_object_key(a5);memcpy(e+12,&k,8);
    k=st_object_key(a6);memcpy(e+14,&k,8);
    if(!point || !read_local(point,e+24,0x80)) memset(e+24,0xff,0x80);
    st_fm_bank(agent,bank,0,e+152,0x40);
    c->live=st_pt_live_begin(e,&c->prior);
    return 1;
}
/* After it: out, then the entry published complete. */
static void st_pt_end(st_pt_ctx *c,const void *out,uint32_t result) {
    st_pt_live_end(c->live,c->prior);
    uint32_t *e=c->e;const int w=c->watched;
    e[3]=result;
    if(!out || !read_local(out,e+56,0x40)) memset(e+56,0xff,0x40);
    st_slot_lock(w);
    if(st_fm[w].pt_tick!=e[1]) {st_fm[w].pt_tick=e[1];st_fm[w].pt_in_tick=0;}
    e[18]=st_fm[w].pt_in_tick++;
    const uint32_t n=st_fm[w].pt_calls++;
    if(n<ST_PT_ENTRIES) {e[17]=n;memcpy(st_fm[w].path+n*ST_PT_WORDS,e,sizeof(c->e));InterlockedIncrement64(&st_pt_kept);}
    st_slot_unlock(w);
}
/* The conversion (out, velocity, &ref, &frame, flag): inside an open outer call on this thread, its first one is
 * recorded into that entry (before: the matrix and both objects; after: the matrix). *entry NULL: nothing to do after. */
static void st_pt_conversion_take(const void *out,const void *ref,const void *frame,uint32_t **entry) {
    uint32_t *e=st_pt_live();*entry=NULL;
    if(!e || e[16]++) return;
    *entry=e;
    const unsigned char *r=NULL,*f=NULL;uint64_t k;
    if(!ref || !read_local(ref,&r,8)) r=NULL;
    if(!frame || !read_local(frame,&f,8)) f=NULL;
    k=st_object_key(r);memcpy(e+19,&k,8);k=st_object_key(f);memcpy(e+21,&k,8);
    if(!out || !read_local(out,e+72,0x40)) memset(e+72,0xff,0x40);
    st_pt_object(r,e+104,e+168);st_pt_object(f,e+128,e+170);
    memcpy(e+ST_PT_SCRATCH,&r,8);memcpy(e+ST_PT_SCRATCH+2,&f,8);
}
static void st_pt_conversion_begin(unsigned site,const void *out,const void *ref,const void *frame,uint32_t **entry) {
    InterlockedIncrement64(&st_chain_hits[site]);
    st_pt_conversion_take(out,ref,frame,entry);
}
/* FR20: each object of a parent chain from itself to its parent through the platform's hierarchy function, on an
 * identity matrix (16-byte aligned: Linux reads it with movaps), at most ST_PT_LEVELS. */
static void st_pt_levels(uint32_t *out,const unsigned char *object) {
    static const float identity[16]={0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0};
    for(unsigned level=0;level<ST_PT_LEVELS && object;level++) {
        uint32_t *l=out+level*ST_PT_LEVEL_WORDS;const unsigned char *parent=NULL;uintptr_t vtable=0;
        uint64_t k=st_object_key(object);memcpy(l,&k,8);
        if(read_local(object,&vtable,8)) l[3]=(uint32_t)(vtable-(uintptr_t)image);
        if(!read_local(object+ST_OBJ_PARENT,&parent,8) || !parent) break;
        struct {float m[16];} __attribute__((aligned(16))) local;memcpy(local.m,identity,sizeof(identity));
        ((st_hier_fn)(void*)ST_HIER)((void*)object,local.m,(void*)parent,0);
        l[2]=1;memcpy(l+4,local.m,0x40);InterlockedIncrement64(&st_pt_levels_probed);
        object=parent;
    }
}
static void st_pt_conversion_end(uint32_t *e,const void *out) {
    if(!e) return;
    if(!out || !read_local(out,e+88,0x40)) memset(e+88,0xff,0x40);
    const unsigned char *r,*f;memcpy(&r,e+ST_PT_SCRATCH,8);memcpy(&f,e+ST_PT_SCRATCH+2,8);memset(e+ST_PT_SCRATCH,0,16);
    st_pt_levels(e+ST_PT_REF_LEVELS,r);st_pt_levels(e+ST_PT_FRAME_LEVELS,f);
    InterlockedIncrement64(&st_pt_conversions);
}
/* FR20: the object path (Windows 0xe8d5ea, Linux 0x1432611): the hierarchy function itself converts the object's own
 * transform into the frame; it is the entry's conversion (no inner call). */
static void st_pt_object_call(unsigned site,unsigned which,void *object,void *mat,void *frame,void *agent) {
    st_pt_ctx c;const int on=st_pt_begin(&c,site,which,agent,NULL,frame,NULL,NULL);
    uint32_t *e=NULL;
    if(on) {void *o=object,*f=frame;st_pt_conversion_take(mat,&o,&f,&e);}
    ((st_hier_fn)(void*)ST_HIER)(object,mat,frame,0);
    if(on) {st_pt_conversion_end(e,mat);st_pt_end(&c,mat,1);}
}
/* FR21: the Highway's point map (platform: ST_HW_SLOT holding image+ST_HW_FN, st_hw_swap, ST_HW_SPLINE). While it
 * runs, the open path entry holds the capture's address at [ST_HW_OPEN] for the banking's call sites. */
#define ST_HW_OPEN 176u
typedef void (*st_hw_fn)(void *,void *);
typedef float (*st_bank_fn)(void *,float);
static void *st_hw_native;
static unsigned st_hw_installed;
static int st_hw_swap(void *expected,void *value);
/* One memory block at *at: {address, bytes, 0, 0} then the bytes padded to 16; 0 when it does not fit or is unreadable. */
static int st_hw_block(uint32_t *h,unsigned *at,const void *p,size_t bytes) {
    const unsigned words=4u+(unsigned)(((bytes+15u)&~(size_t)15u)/4u);
    if(!p || !bytes || *at+words>ST_HW_WORDS-1u) return 0;
    uint32_t *b=h+*at;memset(b,0,words*4u);
    if(!read_local(p,b+4,bytes)) {memset(b,0,words*4u);return 0;}
    const uint64_t a=(uint64_t)(uintptr_t)p;memcpy(b,&a,8);b[2]=(uint32_t)bytes;
    *at+=words;h[15]++;return 1;
}
/* The vector [begin, end) at object+offset, at most cap bytes and what still fits, whole units. */
static void st_hw_vector(uint32_t *h,unsigned *at,const unsigned char *object,unsigned offset,size_t cap,size_t unit) {
    uint64_t v[2];
    if(!object || !read_local(object+offset,v,16) || v[1]<=v[0] || v[1]-v[0]>0x100000u) return;
    size_t n=(size_t)(v[1]-v[0]);const size_t room=(size_t)(ST_HW_WORDS-1u-*at)*4u;
    if(n>cap) n=cap;
    if(room<16u+unit) return;
    if(n>room-16u) n=(room-16u)/unit*unit;
    st_hw_block(h,at,(const void*)(uintptr_t)v[0],n);
}
static void st_hw_memory(uint32_t *h,const unsigned char *hw) {
    if(!read_local(hw+ST_HW_BASE,h+48,0x140)) memset(h+48,0xff,0x140);
    unsigned at=ST_HW_BLOCKS;const unsigned char *affine=NULL,*composite=NULL;
    if(!read_local(hw+ST_HW_SPLINE,&affine,8) || !affine || !st_hw_block(h,&at,affine,0x40)) return;
    st_hw_vector(h,&at,affine,0x20,0x100,4);
    if(!read_local(affine+0x10,&composite,8) || !composite || !st_hw_block(h,&at,composite,0x40)) return;
    st_hw_vector(h,&at,composite,0x28,0x100,8);
    st_hw_vector(h,&at,composite,0x10,0x100000,0xa0);
}
static void st_hw_transform(void *hw,void *mat) {
    InterlockedIncrement64(&st_hw_calls);
    const st_hw_fn native=(st_hw_fn)st_hw_native;
    uint32_t *e=st_pt_live();
    const int w=e && e[23] && e[23]<=ST_IDS?(int)e[23]-1:-1;
    if(w<0) {native(hw,mat);return;}
    uint32_t h[ST_HW_WORDS];memset(h,0,sizeof(h));
    h[0]=ST_HW_TAG;h[1]=e[1];h[2]=e[4];h[3]=e[5];h[4]=e[6];h[5]=e[7];
    uint64_t k=(uint64_t)(uintptr_t)image;memcpy(h+6,&k,8);
    k=(uint64_t)(uintptr_t)hw;memcpy(h+8,&k,8);
    k=st_object_key((const unsigned char*)hw);memcpy(h+10,&k,8);
    if(!mat || !read_local(mat,h+16,0x40)) memset(h+16,0xff,0x40);
    st_hw_memory(h,(const unsigned char*)hw);
    uint32_t *prior,*open=h;memcpy(&prior,e+ST_HW_OPEN,8);memcpy(e+ST_HW_OPEN,&open,8);
    native(hw,mat);
    memcpy(e+ST_HW_OPEN,&prior,8);
    if(!mat || !read_local(mat,h+32,0x40)) memset(h+32,0xff,0x40);
    st_slot_lock(w);
    if(!st_fm[w].hw_calls++) {memcpy(st_fm[w].hwcap,h,sizeof(h));InterlockedIncrement64(&st_hw_kept);}
    st_slot_unlock(w);
}
/* The banking's call sites: the original once; inside an open capture its first result is kept. */
static float st_hw_banking_call(unsigned site,void *native,void *hw,float t) {
    InterlockedIncrement64(&st_chain_hits[site]);
    const float r=((st_bank_fn)native)(hw,t);
    uint32_t *e=st_pt_live(),*h=NULL;
    if(e) memcpy(&h,e+ST_HW_OPEN,8);
    if(h) {if(!h[12]++) {memcpy(h+13,&t,4);memcpy(h+14,&r,4);}InterlockedIncrement64(&st_hw_banking);}
    return r;
}
/* With the chain, after SetTarget's slot: the Highway's slot must hold its map (and on Windows its bytes their pin). */
static int st_hw_arm(void) {
    void *now=*(void *volatile*)(image+ST_HW_SLOT);
    if(now!=(void*)(image+ST_HW_FN)) return 0;
#ifdef ST_HW_SHA
    if(!hash_ok(image+ST_HW_FN,ST_HW_SIZE,ST_HW_SHA)) return 0;
#endif
    if(!st_hw_swap(now,(void*)st_hw_transform)) return 0;
    st_hw_native=now;st_hw_installed=1;return 1;
}
static void st_hw_restore(void) {if(st_hw_installed && st_hw_swap((void*)st_hw_transform,st_hw_native)) st_hw_installed=0;}
/* Into the step record (after the step): the slot's kept entries, the rest zero; the slot starts again. */
static void st_fm_take(int watched,st_record_t *r) {
    memset(r->formation,0,sizeof(r->formation));memset(r->path,0,sizeof(r->path));
    st_slot_lock(watched);
    const uint32_t calls=st_fm[watched].calls,kept=calls<ST_FM_ENTRIES?calls:ST_FM_ENTRIES;
    memcpy(r->formation,st_fm[watched].words,kept*ST_FM_WORDS*4u);
    for(uint32_t k=0;k<kept;k++) r->formation[k*ST_FM_WORDS+ST_FM_WORDS-1]=calls;
    st_fm[watched].calls=0;
    const uint32_t pt=st_fm[watched].pt_calls,pt_kept=pt<ST_PT_ENTRIES?pt:ST_PT_ENTRIES;
    memcpy(r->path,st_fm[watched].path,pt_kept*ST_PT_WORDS*4u);
    for(uint32_t k=0;k<pt_kept;k++) r->path[k*ST_PT_WORDS+ST_PT_WORDS-1]=pt;
    st_fm[watched].pt_calls=0;
    memset(r->settarget,0,sizeof(r->settarget));
    const uint32_t sg=st_fm[watched].sg_calls,sg_kept=sg<ST_SG_ENTRIES?sg:ST_SG_ENTRIES;
    memcpy(r->settarget,st_fm[watched].settarget,sg_kept*ST_SG_WORDS*4u);
    for(uint32_t k=0;k<sg_kept;k++) r->settarget[k*ST_SG_WORDS+ST_SG_WORDS-1]=sg;
    st_fm[watched].sg_calls=0;
    memset(r->pathset,0,sizeof(r->pathset));
    const uint32_t ps=st_fm[watched].ps_calls,ps_kept=ps<ST_PS_ENTRIES?ps:ST_PS_ENTRIES;
    memcpy(r->pathset,st_fm[watched].pathset,ps_kept*ST_PS_WORDS*4u);
    for(uint32_t k=0;k<ps_kept;k++) r->pathset[k*ST_PS_WORDS+ST_PS_WORDS-1]=ps;
    st_fm[watched].ps_calls=0;
    if(st_fm[watched].hw_calls) {memcpy(r->hwcap,st_fm[watched].hwcap,sizeof(r->hwcap));r->hwcap[ST_HW_WORDS-1]=st_fm[watched].hw_calls;}
    else memset(r->hwcap,0,sizeof(r->hwcap));
    st_fm[watched].hw_calls=0;
    st_slot_unlock(watched);
}
/* FR20: an agent's first path point (0x80 bytes, 0xff when none) and its address (Windows' path set hook only). */
__attribute__((unused)) static const unsigned char *st_ps_point(const unsigned char *agent,uint32_t *out) {
    const unsigned char *point=NULL;
    if(!agent || !read_local(agent+ST_AGENT_POINT,&point,8) || !point || !read_local(point,out,0x80)) {memset(out,0xff,0x80);return NULL;}
    return point;
}
/* FR20: a finished path-set entry into the ship's slot. */
__attribute__((unused)) static void st_ps_publish(int w,uint32_t *e) {
    st_slot_lock(w);
    if(st_fm[w].ps_tick!=e[1]) {st_fm[w].ps_tick=e[1];st_fm[w].ps_in_tick=0;}
    e[233]=st_fm[w].ps_in_tick++;
    const uint32_t n=st_fm[w].ps_calls++;
    if(n<ST_PS_ENTRIES) {e[6]=n;memcpy(st_fm[w].pathset+n*ST_PS_WORDS,e,ST_PS_WORDS*4u);InterlockedIncrement64(&st_ps_kept);}
    st_slot_unlock(w);
}
/* FR19: the Linear model's SetTarget slot (platform: ST_SG_SLOT holding image+ST_SG_FN, st_sg_swap). */
static void *st_sg_native;
static unsigned st_sg_installed;
static int st_sg_swap(void *expected,void *value);
static int st_watched(const unsigned char *model,const unsigned char **agent_out,uint64_t *key_out);
static void st_settarget(void *model,const void *mat,void *a3,void *a4) {
    const uintptr_t caller=(uintptr_t)__builtin_return_address(0);
    InterlockedIncrement64(&st_sg_calls);
    const st_sg_fn native=(st_sg_fn)st_sg_native;
    const uint64_t tick=stats.issued;const unsigned char *agent=NULL,*point=NULL;uint64_t key=0;int w=-1;
    if(!stats.active || boot_phase!=3 || tick<1 || tick>st_ticks || (w=st_watched((const unsigned char*)model,&agent,&key))<0) {
        native(model,mat,a3,a4);return;
    }
    uint32_t e[ST_SG_WORDS];memset(e,0,sizeof(e));
    e[0]=ST_SG_TAG;e[1]=(uint32_t)tick;
    e[2]=caller>=(uintptr_t)image && caller-(uintptr_t)image<0x10000000u?(uint32_t)(caller-(uintptr_t)image):0xffffffffu;
    e[3]=GetCurrentThreadId()==ui_thread?0:1;
    uint32_t bank=0xffffffffu;if(!read_local(ST_BANK_READ,&bank,4)) bank=0xffffffffu;
    e[4]=bank;memcpy(e+6,&key,8);
    uint64_t k=(uint64_t)(uintptr_t)a3;memcpy(e+8,&k,8);k=st_object_key((const unsigned char*)a3);memcpy(e+10,&k,8);
    k=(uint64_t)(uintptr_t)a4;memcpy(e+12,&k,8);
    if(!mat || !read_local(mat,e+16,0x40)) memset(e+16,0xff,0x40);
    if(!read_local((const unsigned char*)model+ST_MODEL_TARGET,e+32,0x50)) memset(e+32,0xff,0x50);
    if(!agent || !read_local(agent+ST_AGENT_POINT,&point,8) || !point || !read_local(point,e+52,0x80)) memset(e+52,0xff,0x80);
    st_fm_bank(agent,bank,0,e+84,0x40);
    native(model,mat,a3,a4);
    if(!read_local((const unsigned char*)model+ST_MODEL_TARGET,e+100,0x40)) memset(e+100,0xff,0x40);
    st_slot_lock(w);
    if(st_fm[w].sg_tick!=e[1]) {st_fm[w].sg_tick=e[1];st_fm[w].sg_in_tick=0;}
    e[5]=st_fm[w].sg_in_tick++;
    const uint32_t n=st_fm[w].sg_calls++;
    if(n<ST_SG_ENTRIES) {e[14]=n;memcpy(st_fm[w].settarget+n*ST_SG_WORDS,e,sizeof(e));InterlockedIncrement64(&st_sg_kept);}
    st_slot_unlock(w);
}
/* With the chain, after its sites and before the step's entry: the slot must hold SetTarget (and on Windows its bytes
 * their pin). */
static int st_sg_arm(void) {
    void *now=*(void *volatile*)(image+ST_SG_SLOT);
    if(now!=(void*)(image+ST_SG_FN)) return 0;
#ifdef ST_SG_SHA
    if(!hash_ok(image+ST_SG_FN,ST_SG_SIZE,ST_SG_SHA)) return 0;
#endif
    if(!st_sg_swap(now,(void*)st_settarget)) return 0;
    st_sg_native=now;st_sg_installed=1;return 1;
}
static void st_sg_restore(void) {if(st_sg_installed && st_sg_swap((void*)st_settarget,st_sg_native)) st_sg_installed=0;}
static void st_step(void *model,const void *vector,float dt,double time) {
    const st_step_fn native=(st_step_fn)st_native;
    InterlockedIncrement64(&st_calls);
    const uint64_t tick=stats.issued;const unsigned char *agent;uint64_t key;int watched;
    if(!stats.active || boot_phase!=3 || tick<1 || tick>st_ticks || (watched=st_watched((const unsigned char*)model,&agent,&key))<0) {
        native(model,vector,dt,time);return;
    }
    InterlockedIncrement64(&st_watched_calls);
    LONG64 index=InterlockedIncrement64(&st_records)-1;
    if(index>=(LONG64)ST_RECORDS) {InterlockedIncrement64(&st_dropped);native(model,vector,dt,time);return;}
    st_record_t *r=&st_ring[index];
    r->tick=tick;r->key=key;r->thread=GetCurrentThreadId()==ui_thread?0:1;r->dt=dt;r->time=time;r->has_agent=agent!=NULL;
    if(st_ordinal_tick[watched]!=tick) {st_ordinal_tick[watched]=tick;st_ordinals[watched]=0;}
    r->ordinal=(uint32_t)st_ordinals[watched]++; /* one ship's steps run on one thread at a time */
    if(!vector || !read_local(vector,r->vector,16)) memset(r->vector,0xff,16);
    if(!read_local(model,r->model_pre,ST_MODEL_BYTES)) memset(r->model_pre,0xff,ST_MODEL_BYTES);
    if(!agent || !read_local(agent,r->agent_pre,ST_AGENT_BYTES)) memset(r->agent_pre,0xff,ST_AGENT_BYTES);
    if(!agent || !read_local(agent+ST_AGENT_POSE,r->pose_pre,ST_POSE_BYTES)) memset(r->pose_pre,0xff,ST_POSE_BYTES);
    st_banks(r->banks_pre);
    int live=-1;st_record_t *prior=NULL;
    if(st_chain_installed) {memset(r->chain,0,sizeof(r->chain));r->chain[0]=ST_CHAIN_TAG;live=st_live_begin(r,&prior);}
    native(model,vector,dt,time);
    st_live_end(live,prior);
    if(st_chain_installed) {st_spline_read((const unsigned char*)model,r->spline);st_fm_take(watched,r);}
    if(!read_local(model,r->model_post,ST_MODEL_BYTES)) memset(r->model_post,0xff,ST_MODEL_BYTES);
    if(!agent || !read_local(agent,r->agent_post,ST_AGENT_BYTES)) memset(r->agent_post,0xff,ST_AGENT_BYTES);
    if(!agent || !read_local(agent+ST_AGENT_POSE,r->pose_post,ST_POSE_BYTES)) memset(r->pose_post,0xff,ST_POSE_BYTES);
    st_banks(r->banks_post);
    InterlockedExchange64(&r->ready,index+1);
}
/* A saved ID to watch; before arming. A loading-UI reload reruns the configuration: once armed, an ID already
 * watched is accepted and a new one refused. */
static int st_watch(uint64_t id) {
    if(!id) return 0;
    for(unsigned i=0;i<st_id_count;i++) if(st_ids[i]==id) return 1;
    if(st_installed || st_id_count>=ST_IDS) return 0;
    st_ids[st_id_count++]=id;return 1;
}
/* Before the first admitted frame, on the MoveWorker; a rerun with the same ticks is accepted. */
static int st_arm(uint64_t ticks) {
    if(st_installed) return stats.active && ticks==st_ticks;
    if(!ticks || ticks>600 || !st_id_count || !stats.active || (boot_mode && boot_phase) || stats.issued ||
       GetCurrentThreadId()!=ui_thread || !world_identity_installed) {st_stage="refused";return 0;}
    void *entry=*(void *volatile*)(image+ST_SLOT);
    if(entry!=(void*)(image+ST_STEP)) {st_stage="entry";return 0;}
    if(!st_ring && !(st_ring=calloc(ST_RECORDS,sizeof(st_record_t)))) {st_stage="memory";return 0;}
    st_native=entry;st_ticks=ticks;st_records=st_calls=st_watched_calls=st_dropped=0;
    memset(st_ordinal_tick,0,sizeof(st_ordinal_tick));memset((void*)st_fm,0,sizeof(st_fm));st_fm_kept=st_pt_kept=st_pt_conversions=st_pt_no_slot=st_sg_calls=st_sg_kept=0;
    st_pt_levels_probed=st_ps_kept=st_hw_calls=st_hw_kept=st_hw_banking=st_pc_kept=0;
    /* FR8's sites first: the entry leads into them only once they are all in place. */
    if(st_chain_wanted && !st_chain_installed && !st_chain_arm()) {st_stage="chain";return 0;}
    /* FR19: SetTarget's slot with the chain, after its sites. */
    if(st_chain_installed && !st_sg_installed && !st_sg_arm()) {st_chain_restore();st_stage="settarget";return 0;}
    /* FR21: the Highway's point map with the chain, after SetTarget's slot. */
    if(st_chain_installed && !st_hw_installed && !st_hw_arm()) {st_sg_restore();st_chain_restore();st_stage="highway";return 0;}
    if(!st_slot_swap(entry,(void*)st_step)) {st_hw_restore();st_sg_restore();if(st_chain_installed) st_chain_restore();st_stage="cas";return 0;}
    st_chunks=st_chain_installed?ST_CHUNKS+1+ST_SPLINE_CHUNKS+ST_FM_CHUNKS+ST_PT_CHUNKS+ST_SG_CHUNKS+ST_PS_CHUNKS+ST_HW_CHUNKS:ST_CHUNKS;
    st_installed=1;st_restored=0;st_stage="armed";return 1;
}
static void st_restore(void) {
    if(st_installed && st_slot_swap((void*)st_step,st_native)) {st_installed=0;st_restored=1;}
    st_hw_restore();st_sg_restore();
    if(st_chain_installed) st_chain_restore();
}
static int st_status(char *out,size_t cap) {
    int n=snprintf(out,cap,"installed=%u restored=%u ids=%u ticks=%llu calls=%lld watched_calls=%lld records=%lld dropped=%lld stage=%s",
        st_installed,st_restored,st_id_count,(unsigned long long)st_ticks,(long long)st_calls,(long long)st_watched_calls,
        (long long)(st_records<(LONG64)ST_RECORDS?st_records:(LONG64)ST_RECORDS),(long long)st_dropped,st_stage);
    if(n<=0 || (size_t)n>=cap) return 0;
    if(!st_chain_wanted) return n;
    int m=snprintf(out+n,cap-(size_t)n," chain=%s chain_installed=%u chain_restored=%u chain_owned=%d chain_recorded=%lld chain_no_slot=%lld chain_hits=",
        st_chain_stage,st_chain_installed,st_chain_restored,st_chain_installed?st_chain_owned():-1,(long long)st_chain_recorded,(long long)st_chain_no_slot);
    if(m<=0 || (size_t)(n+=m)>=cap) return 0;
    for(unsigned i=0;i<ST_CHAIN_SITE_COUNT;i++) {
        m=snprintf(out+n,cap-(size_t)n,"%s%s:%lld",i?",":"",st_chain_site_name(i),(long long)st_chain_hits[i]);
        if(m<=0 || (size_t)(n+=m)>=cap) return 0;
    }
    m=snprintf(out+n,cap-(size_t)n," fm_kept=%lld pt_kept=%lld pt_conv=%lld pt_no_slot=%lld pt_levels=%lld sg_installed=%u sg_calls=%lld sg_kept=%lld ps_kept=%lld",
        (long long)st_fm_kept,(long long)st_pt_kept,(long long)st_pt_conversions,(long long)st_pt_no_slot,(long long)st_pt_levels_probed,
        st_sg_installed,(long long)st_sg_calls,(long long)st_sg_kept,(long long)st_ps_kept);
    if(m<=0 || (size_t)(n+=m)>=cap) return 0;
    m=snprintf(out+n,cap-(size_t)n," hw_installed=%u hw_calls=%lld hw_kept=%lld hw_banking=%lld pc_kept=%lld",
        st_hw_installed,(long long)st_hw_calls,(long long)st_hw_kept,(long long)st_hw_banking,(long long)st_pc_kept);
    if(m<=0 || (size_t)(n+=m)>=cap) return 0;
    return n;
}
static int st_hex(char *out,size_t cap,const unsigned char *bytes,size_t n) {
    static const char digits[]="0123456789abcdef";
    if(cap<n*2+1) return 0;
    for(size_t i=0;i<n;i++) {out[i*2]=digits[bytes[i]>>4];out[i*2+1]=digits[bytes[i]&15];}
    out[n*2]=0;return 1;
}
/* Row index = record * 25 + chunk: 0 args, 1-2 model before, 3-4 model after, 5-10 agent before, 11-16 agent after,
 * 17-20 pose before, 21-24 pose after (offset from the first bank; banks= read,written index before / after); with
 * FR8 armed 53 rows a record, 25 the chain's words, 26-29 the spline after the step (FR9), 30-35 the formation calls
 * (FR17, 0x200 bytes a call), 36-59 the path point conversions (FR18/FR20, 0x800 bytes an entry), 60-67 the SetTarget calls
 * (FR19, 0x200 bytes a call), 68-75 the path sets (FR20, 0x400 bytes an entry; FR21's point calls too), 76-107 the
 * highway capture (FR21, 0x2000 bytes): 108 rows a record. */
static int st_row(uint64_t index,char *out,size_t cap) {
    const uint64_t record=index/st_chunks,chunk=index%st_chunks;
    LONG64 count=InterlockedCompareExchange64(&st_records,0,0);
    if(!st_ring || record>=ST_RECORDS || (LONG64)record>=count) return 0;
    const st_record_t *r=&st_ring[record];
    if(InterlockedCompareExchange64((volatile LONG64*)&r->ready,0,0)!=(LONG64)record+1) return 0;
    static const char *const parts[14]={"args","model_pre","model_post","agent_pre","agent_post","pose_pre","pose_post","chain","spline","formation","path","settarget","pathset","hwcap"};
    unsigned part,offset;const unsigned char *bytes;size_t size;
    if(chunk==ST_CHUNKS) {part=7;offset=0;bytes=(const unsigned char*)r->chain;size=sizeof(r->chain);}
    else if(chunk>ST_CHUNKS+ST_SPLINE_CHUNKS+ST_FM_CHUNKS+ST_PT_CHUNKS+ST_SG_CHUNKS+ST_PS_CHUNKS) {
          part=13;offset=(unsigned)(chunk-ST_CHUNKS-1-ST_SPLINE_CHUNKS-ST_FM_CHUNKS-ST_PT_CHUNKS-ST_SG_CHUNKS-ST_PS_CHUNKS)*ST_CHUNK;
          bytes=(const unsigned char*)r->hwcap+offset;size=ST_CHUNK;}
    else if(chunk>ST_CHUNKS+ST_SPLINE_CHUNKS+ST_FM_CHUNKS+ST_PT_CHUNKS+ST_SG_CHUNKS) {
          part=12;offset=(unsigned)(chunk-ST_CHUNKS-1-ST_SPLINE_CHUNKS-ST_FM_CHUNKS-ST_PT_CHUNKS-ST_SG_CHUNKS)*ST_CHUNK;
          bytes=(const unsigned char*)r->pathset+offset;size=ST_CHUNK;}
    else if(chunk>ST_CHUNKS+ST_SPLINE_CHUNKS+ST_FM_CHUNKS+ST_PT_CHUNKS) {part=11;offset=(unsigned)(chunk-ST_CHUNKS-1-ST_SPLINE_CHUNKS-ST_FM_CHUNKS-ST_PT_CHUNKS)*ST_CHUNK;
          bytes=(const unsigned char*)r->settarget+offset;size=ST_CHUNK;}
    else if(chunk>ST_CHUNKS+ST_SPLINE_CHUNKS+ST_FM_CHUNKS) {part=10;offset=(unsigned)(chunk-ST_CHUNKS-1-ST_SPLINE_CHUNKS-ST_FM_CHUNKS)*ST_CHUNK;
          bytes=(const unsigned char*)r->path+offset;size=ST_CHUNK;}
    else if(chunk>ST_CHUNKS+ST_SPLINE_CHUNKS) {part=9;offset=(unsigned)(chunk-ST_CHUNKS-1-ST_SPLINE_CHUNKS)*ST_CHUNK;
          bytes=(const unsigned char*)r->formation+offset;size=ST_CHUNK;}
    else if(chunk>ST_CHUNKS) {part=8;offset=(unsigned)(chunk-ST_CHUNKS-1)*ST_CHUNK;bytes=r->spline+offset;
          size=ST_SPLINE_BYTES-offset<ST_CHUNK?ST_SPLINE_BYTES-offset:ST_CHUNK;}
    else if(chunk==0) {part=0;offset=0;bytes=r->vector;size=16;}
    else if(chunk<5) {part=chunk<3?1:2;offset=(unsigned)((chunk-1)%2)*ST_CHUNK;bytes=(part==1?r->model_pre:r->model_post)+offset;size=ST_CHUNK;}
    else if(chunk<17) {part=chunk<11?3:4;offset=(unsigned)((chunk-5)%6)*ST_CHUNK;bytes=(part==3?r->agent_pre:r->agent_post)+offset;
          size=ST_AGENT_BYTES-offset<ST_CHUNK?ST_AGENT_BYTES-offset:ST_CHUNK;}
    else {part=chunk<21?5:6;offset=(unsigned)((chunk-17)%4)*ST_CHUNK;bytes=(part==5?r->pose_pre:r->pose_post)+offset;
          size=ST_POSE_BYTES-offset<ST_CHUNK?ST_POSE_BYTES-offset:ST_CHUNK;}
    char hex[ST_CHUNK*2+1];
    if(!st_hex(hex,sizeof(hex),bytes,size)) return 0;
    uint32_t dt_bits;uint64_t time_bits;memcpy(&dt_bits,&r->dt,4);memcpy(&time_bits,&r->time,8);
    int n=snprintf(out,cap,"index=%llu tick=%llu key=%016llx ordinal=%u thread=%s part=%s offset=%x dt=%08x time=%016llx bytes=%s banks=%x,%x/%x,%x",
        (unsigned long long)index,(unsigned long long)r->tick,(unsigned long long)r->key,r->ordinal,r->thread?"other":"ui",parts[part],offset,
        dt_bits,(unsigned long long)time_bits,hex,r->banks_pre[0],r->banks_pre[1],r->banks_post[0],r->banks_post[1]);
    return n>0 && (size_t)n<cap?n:0;
}
