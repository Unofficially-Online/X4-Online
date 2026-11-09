/* Determinism fix, DEFAULT OFF: native_gate_portable_math = true.
 *
 * X4.exe links the MSVC CRT statically. 14 of its math entries choose an FMA3 or
 * an SSE2 body at run time (__use_fma3_lib 0x38edec0, set from CPUID by
 * __crt_init_fma3 0x1a310b0), so two Windows PCs with different CPUs run different
 * arithmetic from one binary (LOCKSTEP-CROSSPLATFORM-FP-INVENTORY-20261029.txt,
 * classes M and D). This module sends the 26 transcendental CRT entries X4.exe
 * calls and the three compiler sin/cos helpers (in place of the Linux build's
 * sincos/sincosf) to src/native/portable_math, the library the Linux preload
 * exports for its 28 glibc imports: every peer then computes the same bits on any
 * x86-64 CPU. The CRT's internal log2f body 0x1a10798 is reached only through the
 * hooked entry 0x1a10928. The exact CRT entries (fmod, fmodf, sqrt, sqrtf, round,
 * roundf) stay native.
 *
 * Each entry's first five bytes become jmp rel32 to a 16-byte stub (jmp [rip];
 * target) on a private RX page within +-2 GB; the original body never runs again.
 * ABI: MS x64, xmm0/xmm1 -> xmm0. The helpers keep their lane ABI: 0x1750210
 * xmm0=x -> [sin x, cos x], 0x1750253 xmm0=a, xmm1=b -> [sin a, cos a, sin b,
 * cos b], 0x1b42f20 binary64 [sin x, cos x]. No call site of any entry keeps a
 * volatile register live across the call (all 1,796 direct call and jump sites
 * scanned), so the C replacements' ordinary MS x64 clobbers are enough.
 *
 * Pins: SHA-256 of every entry function (its .pdata range, or the leaf helper's
 * own bytes), the known-answer digest of the linked library, and every gate tick
 * the five entry bytes and the stub page. While an entry holds our jump, other
 * modules reading it through read_local, sample_read or hash_ok see the original
 * bytes (pmath_unpatch_copy), so their pins over these functions (world position
 * RNG: acosf, sinf, 0x1750210) pass in either arming order; this module reads raw. Install and restore suspend every other
 * thread (NtGetNextThread, no heap use while suspended), require that none stands
 * inside a 5-byte window, then write the windows by CAS. Armed on the UI thread
 * before the first admitted frame (boot: before the snapshot load). Stop reason
 * 156; 157-159 are the other floating-point modules. Restored at stop. */
#include <emmintrin.h>
#include "../portable_math/portable_math.h"
#define PMATH_STOP_REASON 156u
#define PMATH_FMA3_FLAG 0x38edec0u
#define PMATH_PAGE_ANCHOR 0x1a00000u
enum pmath_kind {PMATH_F1,PMATH_F2,PMATH_D1,PMATH_D2,PMATH_P1,PMATH_P2,PMATH_PD};
typedef struct {const char *name;unsigned rva,size,kind;const char *sha;void *target;} pmath_site;
/* 0x1750210 [sin x, cos x, 0, 0]; the native helper leaves lanes 2-3 undefined. */
static __m128 pmath_sincosf_one(float x) {float s,c;x4pm_sincosf(x,&s,&c);return _mm_setr_ps(s,c,0.0f,0.0f);}
/* 0x1750253 [sin a, cos a, sin b, cos b] */
static __m128 pmath_sincosf_two(float a,float b) {
    float sa,ca,sb,cb;x4pm_sincosf(a,&sa,&ca);x4pm_sincosf(b,&sb,&cb);return _mm_setr_ps(sa,ca,sb,cb);
}
/* 0x1b42f20 [sin x, cos x] */
static __m128d pmath_sincos_one(double x) {double s,c;x4pm_sincos(x,&s,&c);return _mm_setr_pd(s,c);}
/* JOIN_WATCHACOS (diagnostic, written with the hot join watch's dumps, lockstep_join_world.h): acosf's calls from the weapon
 * request check's aim angle (0x8176dd; its return 0x8176e2): the step, the argument and the result. j1-165..167: actor 5's weapon
 * 0x176ba3 checks its line of fire at 650 on the joiner only, after this angle came within the controller's cone (+0x80). */
#define PMATH_ACOS_RET 0x8176e2u
#define PMATH_ACOS_ROWS 2048u
typedef struct {uint64_t tick;uint32_t x,r;} pmath_acos_row;
static pmath_acos_row pmath_acos_rows[PMATH_ACOS_ROWS];
static volatile LONG pmath_acos_n;static volatile LONG64 pmath_acos_dropped;
static __attribute__((noinline)) float pmath_acosf_watch(float x) {
    const float r=x4pm_acosf(x);
    if((uintptr_t)__builtin_return_address(0)==(uintptr_t)image+PMATH_ACOS_RET) {
        const LONG k=InterlockedIncrement(&pmath_acos_n)-1;
        if(k<(LONG)PMATH_ACOS_ROWS) {pmath_acos_rows[k].tick=stats.issued;memcpy(&pmath_acos_rows[k].x,&x,4);memcpy(&pmath_acos_rows[k].r,&r,4);}
        else InterlockedIncrement64(&pmath_acos_dropped);
    }
    return r;
}
static const pmath_site pmath_sites[]={
    {"sincosf_one",0x1750210u,0x43u,PMATH_P1,"755042c5b869f5435b67606a5382c172cb2d0c3502766c6f8b14be3215b1580e",(void*)pmath_sincosf_one},
    {"sincosf_two",0x1750253u,0xdu,PMATH_P2,"f86f5fecbc91380930213754abf57383eb68bd497bc382a26fd22783994d2897",(void*)pmath_sincosf_two},
    {"log2f",0x1a10928u,0x8u,PMATH_F1,"8222d40caa8f1c2d6680765176fe6487fe38dd383929356a59cc63bd0c3aaf6a",(void*)x4pm_log2f},
    {"exp2f",0x1a11848u,0xeeu,PMATH_F1,"cbce71d42d5158da1f843c0ef27242b4e8f94867d627efa021cfeb8a0b9f2255",(void*)x4pm_exp2f},
    {"acosf",0x1a19250u,0x22cu,PMATH_F1,"69ed9abac17653ec3b63050e5f968c54a39be0da57ad07b292010ccfa38444d5",(void*)pmath_acosf_watch},
    {"sinf",0x1a19480u,0x6f6u,PMATH_F1,"8f322391ace606c3ef0aad82c9e857f684356d0629fb45018c4c38d7de073581",(void*)x4pm_sinf},
    {"cosf",0x1a19b80u,0x4ffu,PMATH_F1,"0528c1f622973552df394bd4eb09f0a4f86c332de49b88b34bdec5f2c9235560",(void*)x4pm_cosf},
    {"atan2f",0x1a1a398u,0x3f7u,PMATH_F2,"989455d5894a8434f90113b2f999fe2e687270d09fb39aab575412b5753af766",(void*)x4pm_atan2f},
    {"powf",0x1a1a790u,0xec6u,PMATH_F2,"21dea6d7e3e3554d2dad15a87e22606e708ce69b098b8ee40b338082dc7919e8",(void*)x4pm_powf},
    {"pow",0x1a1e840u,0x1691u,PMATH_D2,"1f2cfcdafa7cc796ec54d8635d63b950db7f897058599c2f1cd289512d49a91f",(void*)x4pm_pow},
    {"atanf",0x1a23e64u,0x1ecu,PMATH_F1,"9d67369a08b5086464a75000eacf285283e876011cec1f92fd8ac421d085a071",(void*)x4pm_atanf},
    {"tanf",0x1a240d0u,0x673u,PMATH_F1,"63eb0d62a9a1e4171c16289d2e27ac206638dfd907b76246846090b931b41681",(void*)x4pm_tanf},
    {"log2",0x1a24744u,0x327u,PMATH_D1,"8ca8889afd6ad6ae95260e12ba38c2f9e453b0eaa3ae058519265935aecc9efd",(void*)x4pm_log2},
    {"exp2",0x1a250ccu,0xf2u,PMATH_D1,"2a537e2f2b22f1bbe0bb3d37e75960cbe9448cdbe0b65b40ab716ae7f02f1634",(void*)x4pm_exp2},
    {"acos",0x1a251c0u,0x29eu,PMATH_D1,"428d1f92ce7ae1ad1f285f22cbb3334632ab5cfb6f89f8453b34831bcba2c7ef",(void*)x4pm_acos},
    {"asin",0x1a25460u,0x284u,PMATH_D1,"d1cc6ba74006e54a2afe8dde90c31ab306decf0e1c282bdd4674e90c8196d497",(void*)x4pm_asin},
    {"atan",0x1a256e4u,0x215u,PMATH_D1,"8f492285337fbf9a78c5ace9d9a31baed3ec4ef1975dd6df28716c752236d67d",(void*)x4pm_atan},
    {"atan2",0x1a258fcu,0x701u,PMATH_D2,"b4b1c09274d1980d78f3844a369ed753a0d7a90095c78237ee90adc53ba0bfe7",(void*)x4pm_atan2},
    {"cos",0x1a260d0u,0x57au,PMATH_D1,"5c01bf9532552ac5de9d8c7227fcc15a4f7e553c80377fe777def8a67bf8bd48",(void*)x4pm_cos},
    {"exp",0x1a26650u,0x3f5u,PMATH_D1,"b72e5c9194267b9b81ded7471b9d7e150e0399e32cf957af6d62b021c526f63a",(void*)x4pm_exp},
    {"log",0x1a26a50u,0x52bu,PMATH_D1,"1fcc52d9efa650f612f4541a2a35522f06ba45e067d7390baf7d7a3fd43893f0",(void*)x4pm_log},
    {"sin",0x1a26f80u,0x56au,PMATH_D1,"ee6eb01fb2e8bbca19dd4ae3b5c59f738ea03a00992a86cd5c54485a7c73cfba",(void*)x4pm_sin},
    {"tan",0x1a274f0u,0x5b3u,PMATH_D1,"806672e3687d7d928db45d1a9c5d9423c801c0a91011252809039d1168aef5e1",(void*)x4pm_tan},
    {"log10",0x1a38060u,0x5abu,PMATH_D1,"42971550b3e056e16c958311ada63b0ddf12760a5b0646223091ab92b269ddfb",(void*)x4pm_log10},
    {"sincos_one",0x1b42f20u,0x49u,PMATH_PD,"857a4d99ad6d8cebd304ed9a3b40facbe2c3532a3bc69af6e3d734816f4e6e19",(void*)pmath_sincos_one},
    {"asinf",0x1b43ce0u,0x1eau,PMATH_F1,"e99225b86062b23afb5af7d7599fd5834855157d873267cd433397f66f0f86f6",(void*)x4pm_asinf},
    {"expf",0x1b43ed0u,0x2a7u,PMATH_F1,"c91f280f5f70c73aae1ac25626ea7ea5d69752960b0ebda0130e25574433599a",(void*)x4pm_expf},
    {"log10f",0x1b44460u,0x4d5u,PMATH_F1,"a47351dd8dc4f1435565daa5f72d833b53b550c12ee3b1110e3fc9ccb82dc539",(void*)x4pm_log10f},
    {"logf",0x1b44940u,0x3f6u,PMATH_F1,"282d0f9e91ce53809cbccd42c59b5db221b8f041ae0257b0a0a6747b8d43f95e",(void*)x4pm_logf},
};
#define PMATH_COUNT (sizeof(pmath_sites)/sizeof(*pmath_sites))
#define PMATH_STUB 16u
_Static_assert(PMATH_COUNT*PMATH_STUB<=0x1000,"stub page");
static int pmath_installed,pmath_restored,pmath_fma3=-1;
static unsigned char *pmath_page;
static unsigned char pmath_original[PMATH_COUNT][5];
static volatile LONG pmath_any;          /* an entry may hold our bytes: the view is on */
static uintptr_t pmath_lo,pmath_hi;      /* RVA span of the entries' first five bytes */
static uint64_t pmath_kat_value;
static unsigned pmath_busy,pmath_attempts,pmath_frozen_max;
static volatile LONG64 pmath_failures;
static const char *pmath_stage="idle";

/* ---- other threads frozen: no heap, no loader lock, no BCrypt between freeze and thaw ---- */
#define PMATH_MAX_THREADS 4096u
typedef LONG (NTAPI *pmath_next_thread_fn)(HANDLE,HANDLE,ACCESS_MASK,ULONG,ULONG,PHANDLE);
#ifdef WBM_TEST
static pmath_next_thread_fn pmath_test_next;
static DWORD (WINAPI *pmath_test_thread_id)(HANDLE);
static DWORD (WINAPI *pmath_test_suspend)(HANDLE);
static DWORD (WINAPI *pmath_test_wait)(HANDLE, DWORD);
static DWORD (WINAPI *pmath_test_resume)(HANDLE);
static BOOL (WINAPI *pmath_test_close)(HANDLE);
#endif
static HANDLE pmath_frozen[PMATH_MAX_THREADS];
static DWORD pmath_frozen_ids[PMATH_MAX_THREADS];
static unsigned pmath_frozen_count;
typedef struct {
    HANDLE handle;
    int needs_resume;
} pmath_pending_thread;
/* At most all suspended threads plus the old and newly returned enumeration handles. */
static pmath_pending_thread pmath_pending[PMATH_MAX_THREADS + 2u];
static unsigned pmath_pending_count;
static volatile LONG pmath_release_uncertain, pmath_pending_resumes, pmath_pending_closes;
static SRWLOCK pmath_freeze_lock = SRWLOCK_INIT;

/* Diagnostic snapshot for the latest freeze attempt. Updated under the freeze
 * lock and read atomically by status; no I/O or allocation occurs while frozen. */
enum {
    PMATH_FREEZE_IDLE, PMATH_FREEZE_ACTIVE, PMATH_FREEZE_OK, PMATH_FREEZE_PENDING,
    PMATH_FREEZE_NTDLL, PMATH_FREEZE_CURSOR_CLOSE, PMATH_FREEZE_THREAD_ID,
    PMATH_FREEZE_CAPACITY, PMATH_FREEZE_SUSPEND, PMATH_FREEZE_ENUM, PMATH_FREEZE_PASS_LIMIT
};
enum { PMATH_RELEASE_NONE, PMATH_RELEASE_RESUME, PMATH_RELEASE_CLOSE };
static volatile LONG pmath_freeze_diag_stage, pmath_freeze_diag_ntstatus, pmath_freeze_diag_winerror;
static volatile LONG pmath_freeze_diag_thread, pmath_freeze_diag_pass, pmath_freeze_diag_seen, pmath_freeze_diag_suspended;
static volatile LONG pmath_freeze_diag_wait, pmath_freeze_diag_wait_error, pmath_freeze_diag_exited;
/* The attempt flag is accessed only under pmath_freeze_lock. A switch failure
 * remains held across later attempts; release diagnostics describe the latest release. */
static int pmath_freeze_diag_retain_failure;
static volatile LONG pmath_freeze_diag_held;
static volatile LONG pmath_release_diag_stage, pmath_release_diag_winerror, pmath_release_diag_wait, pmath_release_diag_wait_error;
static volatile LONG pmath_release_diag_thread;

static const char *pmath_freeze_diag_name(LONG stage) {
    switch (stage) {
        case PMATH_FREEZE_IDLE: return "idle";
        case PMATH_FREEZE_ACTIVE: return "active";
        case PMATH_FREEZE_OK: return "ok";
        case PMATH_FREEZE_PENDING: return "pending";
        case PMATH_FREEZE_NTDLL: return "ntdll";
        case PMATH_FREEZE_CURSOR_CLOSE: return "cursor_close";
        case PMATH_FREEZE_THREAD_ID: return "thread_id";
        case PMATH_FREEZE_CAPACITY: return "capacity";
        case PMATH_FREEZE_SUSPEND: return "suspend";
        case PMATH_FREEZE_ENUM: return "enumerate";
        case PMATH_FREEZE_PASS_LIMIT: return "pass_limit";
        default: return "unknown";
    }
}
static const char *pmath_release_diag_name(LONG stage) {
    switch (stage) {
        case PMATH_RELEASE_NONE: return "none";
        case PMATH_RELEASE_RESUME: return "resume";
        case PMATH_RELEASE_CLOSE: return "close";
        default: return "unknown";
    }
}
static void pmath_freeze_diag_reset(void) {
    if (InterlockedCompareExchange(&pmath_freeze_diag_held, 0, 0)) return;
    InterlockedExchange(&pmath_freeze_diag_stage, PMATH_FREEZE_ACTIVE);
    InterlockedExchange(&pmath_freeze_diag_ntstatus, 0);
    InterlockedExchange(&pmath_freeze_diag_winerror, 0);
    InterlockedExchange(&pmath_freeze_diag_thread, 0);
    InterlockedExchange(&pmath_freeze_diag_pass, 0);
    InterlockedExchange(&pmath_freeze_diag_seen, 0);
    InterlockedExchange(&pmath_freeze_diag_suspended, 0);
    InterlockedExchange(&pmath_freeze_diag_wait, 0);
    InterlockedExchange(&pmath_freeze_diag_wait_error, 0);
    InterlockedExchange(&pmath_freeze_diag_exited, 0);
    InterlockedExchange(&pmath_release_diag_stage, PMATH_RELEASE_NONE);
    InterlockedExchange(&pmath_release_diag_winerror, 0);
    InterlockedExchange(&pmath_release_diag_wait, 0);
    InterlockedExchange(&pmath_release_diag_wait_error, 0);
    InterlockedExchange(&pmath_release_diag_thread, 0);
}
static void pmath_freeze_diag_fail(LONG stage, LONG status, DWORD error, DWORD thread) {
    if (InterlockedCompareExchange(&pmath_freeze_diag_held, 0, 0)) return;
    InterlockedExchange(&pmath_freeze_diag_ntstatus, status);
    InterlockedExchange(&pmath_freeze_diag_winerror, (LONG)error);
    InterlockedExchange(&pmath_freeze_diag_thread, (LONG)thread);
    InterlockedExchange(&pmath_freeze_diag_stage, stage);
    if (pmath_freeze_diag_retain_failure)
        InterlockedExchange(&pmath_freeze_diag_held, 1);
}

static DWORD pmath_resume_handle(HANDLE handle) {
#ifdef WBM_TEST
    if (pmath_test_resume) {
        return pmath_test_resume(handle);
    }
#endif
    return ResumeThread(handle);
}

static BOOL pmath_close_handle(HANDLE handle) {
#ifdef WBM_TEST
    if (pmath_test_close) {
        return pmath_test_close(handle);
    }
#endif
    return CloseHandle(handle);
}

/* 1 only when the suspension and handle are both settled. An exited thread no
 * longer needs resuming; a live or unknown thread retains its handle for retry. */
static int pmath_release_one(pmath_pending_thread *pending) {
    if (pending->needs_resume) {
        if (pmath_resume_handle(pending->handle) == (DWORD)-1) {
            DWORD error = GetLastError();
            DWORD wait = WaitForSingleObject(pending->handle, 0);
            DWORD wait_error = wait == WAIT_FAILED ? GetLastError() : 0;
            if (wait != WAIT_OBJECT_0) {
                DWORD prior_error = GetLastError();
                DWORD thread = GetThreadId(pending->handle);
                SetLastError(prior_error);
                InterlockedExchange(&pmath_release_diag_winerror, (LONG)error);
                InterlockedExchange(&pmath_release_diag_thread, (LONG)thread);
                InterlockedExchange(&pmath_release_diag_wait, (LONG)wait);
                InterlockedExchange(&pmath_release_diag_wait_error, (LONG)wait_error);
                InterlockedExchange(&pmath_release_diag_stage, PMATH_RELEASE_RESUME);
                return 0;
            }
        }
        pending->needs_resume = 0;
    }
    if (!pmath_close_handle(pending->handle)) {
        DWORD error = GetLastError();
        DWORD thread = GetThreadId(pending->handle);
        SetLastError(error);
        InterlockedExchange(&pmath_release_diag_winerror, (LONG)error);
        InterlockedExchange(&pmath_release_diag_thread, (LONG)thread);
        InterlockedExchange(&pmath_release_diag_wait, 0);
        InterlockedExchange(&pmath_release_diag_wait_error, 0);
        InterlockedExchange(&pmath_release_diag_stage, PMATH_RELEASE_CLOSE);
        return 0;
    }
    return 1;
}

static void pmath_publish_pending(void) {
    LONG resumes = 0, closes = 0;
    for (unsigned i = 0; i < pmath_pending_count; i++) {
        if (pmath_pending[i].needs_resume) {
            ++resumes;
        } else {
            ++closes;
        }
    }
    InterlockedExchange(&pmath_pending_resumes, resumes);
    InterlockedExchange(&pmath_pending_closes, closes);
}

static void pmath_retain(HANDLE handle, int needs_resume) {
    /* The bound follows from the active frozen set and at most two cursor handles. */
    if (pmath_pending_count < PMATH_MAX_THREADS + 2u) {
        pmath_pending[pmath_pending_count++] = (pmath_pending_thread){handle, needs_resume};
    } else {
        InterlockedExchange(&pmath_release_uncertain, 2); /* Never claim cleanup after loss. */
    }
    if (InterlockedCompareExchange(&pmath_release_uncertain, 0, 0) != 2) {
        InterlockedExchange(&pmath_release_uncertain, 1);
    }
    pmath_publish_pending();
}

static void pmath_release_or_retain(HANDLE handle, int needs_resume) {
    pmath_pending_thread pending = {handle, needs_resume};
    if (!pmath_release_one(&pending)) {
        pmath_retain(pending.handle, pending.needs_resume);
    }
}

/* Called only under pmath_freeze_lock, before opening any further thread handles. */
static int pmath_retry_pending_locked(void) {
    unsigned kept = 0;
    for (unsigned i = 0; i < pmath_pending_count; i++) {
        pmath_pending_thread pending = pmath_pending[i];
        if (!pmath_release_one(&pending)) {
            pmath_pending[kept++] = pending;
        }
    }
    pmath_pending_count = kept;
    pmath_publish_pending();
    if (InterlockedCompareExchange(&pmath_release_uncertain, 0, 0) != 2) {
        InterlockedExchange(&pmath_release_uncertain, kept ? 1 : 0);
    }
    return !InterlockedCompareExchange(&pmath_release_uncertain, 0, 0);
}

static int pmath_cleanup_clear(void) {
    return !InterlockedCompareExchange(&pmath_release_uncertain, 0, 0);
}

/* Never block behind a freeze owner while retrying from a lock wait. */
static int pmath_retry_pending(void) {
    if (!TryAcquireSRWLockExclusive(&pmath_freeze_lock)) {
        return 0;
    }
    int clear = pmath_retry_pending_locked();
    ReleaseSRWLockExclusive(&pmath_freeze_lock);
    return clear;
}

static int pmath_thaw(void) {
    for (unsigned i = 0; i < pmath_frozen_count; i++) {
        pmath_release_or_retain(pmath_frozen[i], 1);
    }
    pmath_frozen_count = 0;
    int clear = pmath_cleanup_clear();
    ReleaseSRWLockExclusive(&pmath_freeze_lock);
    return clear;
}

static int pmath_is_frozen(DWORD id) {
    for (unsigned i = 0; i < pmath_frozen_count; i++) {
        if (pmath_frozen_ids[i] == id) {
            return 1;
        }
    }
    return 0;
}

/* Suspend every other thread of the process; passes repeat until one finds no thread it
 * had not suspended yet (threads created meanwhile). Failure retains unresolved handles. */
static int pmath_freeze_attempt(int retain_failure) {
    static pmath_next_thread_fn next;
    AcquireSRWLockExclusive(&pmath_freeze_lock);
    pmath_freeze_diag_retain_failure = retain_failure;
    pmath_freeze_diag_reset();
    if (!pmath_retry_pending_locked()) {
        pmath_freeze_diag_fail(PMATH_FREEZE_PENDING, 0,
                               (DWORD)InterlockedCompareExchange(&pmath_release_diag_winerror, 0, 0),
                               (DWORD)InterlockedCompareExchange(&pmath_release_diag_thread, 0, 0));
        ReleaseSRWLockExclusive(&pmath_freeze_lock);
        return 0;
    }
    if (!next) {
        next = (pmath_next_thread_fn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtGetNextThread");
    }
    if (!next) {
        DWORD error = GetLastError();
        pmath_freeze_diag_fail(PMATH_FREEZE_NTDLL, 0, error, 0);
        ReleaseSRWLockExclusive(&pmath_freeze_lock);
        return 0;
    }
#ifdef WBM_TEST
    pmath_next_thread_fn enumerate = pmath_test_next ? pmath_test_next : next;
#else
    pmath_next_thread_fn enumerate = next;
#endif
    DWORD self = GetCurrentThreadId();
    pmath_frozen_count = 0;
    for (unsigned pass = 0; pass < 16; pass++) {
        if (!InterlockedCompareExchange(&pmath_freeze_diag_held, 0, 0))
            InterlockedExchange(&pmath_freeze_diag_pass, (LONG)(pass + 1));
        HANDLE cursor = NULL, found;
        int kept = 0;
        unsigned added = 0;
        LONG status;
        while ((status = enumerate(GetCurrentProcess(), cursor,
                                   THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT |
                                       THREAD_QUERY_INFORMATION | SYNCHRONIZE,
                                   0, 0, &found)) >= 0) {
            if (!InterlockedCompareExchange(&pmath_freeze_diag_held, 0, 0))
                InterlockedIncrement(&pmath_freeze_diag_seen);
            if (cursor && !kept) {
                pmath_release_or_retain(cursor, 0);
                if (!pmath_cleanup_clear()) {
                    pmath_freeze_diag_fail(PMATH_FREEZE_CURSOR_CLOSE, 0,
                                           (DWORD)InterlockedCompareExchange(&pmath_release_diag_winerror, 0, 0),
                                           (DWORD)InterlockedCompareExchange(&pmath_release_diag_thread, 0, 0));
                    pmath_release_or_retain(found, 0);
                    pmath_thaw();
                    return 0;
                }
            }
            cursor = found;
            kept = 0;
#ifdef WBM_TEST
            DWORD id = pmath_test_thread_id ? pmath_test_thread_id(found) : GetThreadId(found);
#else
            DWORD id = GetThreadId(found);
#endif
            if (!id) {
                DWORD error = GetLastError();
                pmath_freeze_diag_fail(PMATH_FREEZE_THREAD_ID, 0, error, 0);
                pmath_release_or_retain(cursor, 0);
                pmath_thaw();
                return 0;
            }
            if (id == self || pmath_is_frozen(id)) {
                continue;
            }
            if (pmath_frozen_count >= PMATH_MAX_THREADS) {
                pmath_freeze_diag_fail(PMATH_FREEZE_CAPACITY, 0, 0, id);
                pmath_release_or_retain(cursor, 0);
                pmath_thaw();
                return 0;
            }
#ifdef WBM_TEST
            DWORD suspended = pmath_test_suspend ? pmath_test_suspend(found) : SuspendThread(found);
#else
            DWORD suspended = SuspendThread(found);
#endif
            if (suspended == (DWORD)-1) {
                DWORD error = GetLastError();
#ifdef WBM_TEST
                DWORD wait = pmath_test_wait ? pmath_test_wait(found, 0) : WaitForSingleObject(found, 0);
#else
                DWORD wait = WaitForSingleObject(found, 0);
#endif
                DWORD wait_error = wait == WAIT_FAILED ? GetLastError() : 0;
                if (!InterlockedCompareExchange(&pmath_freeze_diag_held, 0, 0)) {
                    InterlockedExchange(&pmath_freeze_diag_wait, (LONG)wait);
                    InterlockedExchange(&pmath_freeze_diag_wait_error, (LONG)wait_error);
                }
                if (wait == WAIT_OBJECT_0) {
                    /* Retain the cursor until enumeration advances; this thread cannot run again. */
                    if (!InterlockedCompareExchange(&pmath_freeze_diag_held, 0, 0))
                        InterlockedIncrement(&pmath_freeze_diag_exited);
                    continue;
                }
                pmath_freeze_diag_fail(PMATH_FREEZE_SUSPEND, 0, error, id);
                pmath_release_or_retain(cursor, 0);
                pmath_thaw();
                return 0;
            }
            if (!InterlockedCompareExchange(&pmath_freeze_diag_held, 0, 0))
                InterlockedIncrement(&pmath_freeze_diag_suspended);
            pmath_frozen[pmath_frozen_count] = found;
            pmath_frozen_ids[pmath_frozen_count++] = id;
            kept = 1;
            ++added;
        }
        if (cursor && !kept) {
            pmath_release_or_retain(cursor, 0);
            if (!pmath_cleanup_clear()) {
                pmath_freeze_diag_fail(PMATH_FREEZE_CURSOR_CLOSE, 0,
                                       (DWORD)InterlockedCompareExchange(&pmath_release_diag_winerror, 0, 0),
                                       (DWORD)InterlockedCompareExchange(&pmath_release_diag_thread, 0, 0));
                pmath_thaw();
                return 0;
            }
        }
        if ((unsigned long)status != 0x8000001au) { /* STATUS_NO_MORE_ENTRIES */
            pmath_freeze_diag_fail(PMATH_FREEZE_ENUM, status, 0, 0);
            pmath_thaw();
            return 0;
        }
        if (!added) {
            if (pmath_frozen_count > pmath_frozen_max) {
                pmath_frozen_max = pmath_frozen_count;
            }
            if (!InterlockedCompareExchange(&pmath_freeze_diag_held, 0, 0))
                InterlockedExchange(&pmath_freeze_diag_stage, PMATH_FREEZE_OK);
            return 1;
        }
    }
    pmath_freeze_diag_fail(PMATH_FREEZE_PASS_LIMIT, 0, 0, 0);
    pmath_thaw();
    return 0;
}
static int pmath_freeze(void) {
    return pmath_freeze_attempt(0);
}
/* 1 when no frozen thread stands inside any [entry+1, entry+5). */
static int pmath_windows_clear(void) {
    for(unsigned i=0;i<pmath_frozen_count;i++) {
        CONTEXT c;memset(&c,0,sizeof(c));c.ContextFlags=CONTEXT_CONTROL;
        if(!GetThreadContext(pmath_frozen[i],&c)) return 0;
        for(unsigned k=0;k<PMATH_COUNT;k++) {
            uintptr_t entry=(uintptr_t)(image+pmath_sites[k].rva);
            if(c.Rip>entry && c.Rip<entry+5) return 0;
        }
    }
    return 1;
}

/* ---- sites ---- */
static void pmath_patch_bytes(unsigned i,unsigned char out[5]) {
    int32_t rel=(int32_t)((intptr_t)(pmath_page+i*PMATH_STUB)-(intptr_t)(image+pmath_sites[i].rva+5));
    out[0]=0xe9;memcpy(out+1,&rel,4);
}
static void pmath_stub_bytes(unsigned i,unsigned char out[PMATH_STUB]) {
    static const unsigned char jump[6]={0xff,0x25,0,0,0,0};void *target=pmath_sites[i].target;
    memcpy(out,jump,6);memcpy(out+6,&target,8);out[14]=out[15]=0xcc;
}
/* ---- original-byte view (lockstep_gate.c fp_patch_view) ---- */
static int pmath_overlaps(const void *source,size_t bytes) {
    if(!*(volatile LONG*)&pmath_any || !image || !source || !bytes || !pmath_page) return 0;
    const unsigned char *p=source;
    return p<image+pmath_hi && p+bytes>image+pmath_lo;
}
/* Bytes of 'copy' (read from 'source') that hold our jump read as the originals. */
static void pmath_unpatch_copy(const void *source,void *copy,size_t bytes) {
    if(!pmath_overlaps(source,bytes)) return;
    const unsigned char *p=source;
    for(unsigned i=0;i<PMATH_COUNT;i++) {
        const unsigned char *at=image+pmath_sites[i].rva;
        if(at>=p+bytes || at+5<=p) continue;
        size_t lo=at<p?(size_t)(p-at):0,hi=at+5>p+bytes?(size_t)(p+bytes-at):5;
        unsigned char ours[5],*c=(unsigned char*)copy+(at+lo-p);pmath_patch_bytes(i,ours);
        if(!memcmp(c,ours+lo,hi-lo)) memcpy(c,pmath_original[i]+lo,hi-lo);
    }
}
static void pmath_bounds(void) {
    pmath_lo=UINTPTR_MAX;pmath_hi=0;
    for(unsigned i=0;i<PMATH_COUNT;i++) {
        if(pmath_sites[i].rva<pmath_lo) pmath_lo=pmath_sites[i].rva;
        if(pmath_sites[i].rva+5u>pmath_hi) pmath_hi=pmath_sites[i].rva+5u;
    }
}
/* 0 original, 1 ours, -1 anything else (needs the page and the captured originals). The
 * raw bytes: read_local shows the originals of our own patch. Code is always readable. */
static int pmath_state(unsigned i) {
    unsigned char now[5],ours[5];
    memcpy(now,image+pmath_sites[i].rva,5);
    if(!memcmp(now,pmath_original[i],5)) return 0;
    if(!pmath_page) return -1;
    pmath_patch_bytes(i,ours);return memcmp(now,ours,5)?-1:1;
}
/* Every entry function hashes to its pin, with our five bytes read as the originals. */
static int pmath_pins(void) {
    static unsigned char copy[0x2000];
    for(unsigned i=0;i<PMATH_COUNT;i++) {
        const pmath_site *s=&pmath_sites[i];
        if(s->size>sizeof(copy)) return 0;
        memcpy(copy,image+s->rva,s->size);  /* raw */
        if(pmath_installed) {
            unsigned char ours[5];pmath_patch_bytes(i,ours);
            if(memcmp(copy,ours,5)) return 0;
            memcpy(copy,pmath_original[i],5);
        }
        if(!hash_ok(copy,s->size,s->sha)) return 0;
    }
    return 1;
}
static int pmath_page_ok(void) {
    if(!pmath_page) return 0;
    for(unsigned i=0;i<PMATH_COUNT;i++) {
        unsigned char want[PMATH_STUB];pmath_stub_bytes(i,want);
        if(memcmp(pmath_page+i*PMATH_STUB,want,PMATH_STUB)) return 0;
    }
    return 1;
}
static unsigned char *pmath_alloc_page(void) {
    uintptr_t at=(uintptr_t)(image+PMATH_PAGE_ANCHOR)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            int reach=1;
            for(unsigned i=0;i<PMATH_COUNT;i++) {
                intptr_t d=(intptr_t)p-(intptr_t)(image+pmath_sites[i].rva+5);
                if(d<=INT32_MIN+0x2000 || d>=INT32_MAX-0x2000) reach=0;
            }
            if(reach) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static int pmath_write_page(void) {
    DWORD old,ignored;
    if(!VirtualProtect(pmath_page,0x1000,PAGE_READWRITE,&old)) return 0;
    memset(pmath_page,0xcc,0x1000);
    for(unsigned i=0;i<PMATH_COUNT;i++) pmath_stub_bytes(i,pmath_page+i*PMATH_STUB);
    if(!VirtualProtect(pmath_page,0x1000,PAGE_EXECUTE_READ,&ignored)) return 0;
    FlushInstructionCache(GetCurrentProcess(),pmath_page,0x1000);return 1;
}
/* All windows from one state to the other with every other thread suspended; a partial
 * write is rolled back. 1 done, 0 refused (nothing changed), -1 partial and stuck. */
static int pmath_switch_once(int install);
static int pmath_switch(int install) {
    if(install) {pmath_bounds();InterlockedExchange(&pmath_any,1);}
    int r=pmath_switch_once(install);
    if(install?r==0:r==1) InterlockedExchange(&pmath_any,0); /* every entry original again */
    return r;
}
static int pmath_switch_once(int install) {
    for(unsigned attempt=0;attempt<200;attempt++) {
        ++pmath_attempts;
        int frozen=pmath_freeze_attempt(1);
        if(!frozen) return 0;
        if(!pmath_windows_clear()) {pmath_thaw();++pmath_busy;Sleep(1);continue;}
        unsigned done=0;int ok=1;
        for(;done<PMATH_COUNT;done++) {
            unsigned char ours[5];pmath_patch_bytes(done,ours);
            if(!view_cas_bytes(pmath_sites[done].rva,install?pmath_original[done]:ours,install?ours:pmath_original[done],5)) {ok=0;break;}
        }
        int stuck=0;
        if(!ok) for(unsigned i=0;i<done;i++) {
            unsigned char ours[5];pmath_patch_bytes(i,ours);
            if(!view_cas_bytes(pmath_sites[i].rva,install?ours:pmath_original[i],install?pmath_original[i]:ours,5)) stuck=1;
        }
        pmath_thaw();
        return ok?1:stuck?-1:0;
    }
    return 0;
}
/* gate.portable_math(): UI thread, before the first admitted frame. */
static int pmath_arm(void) {
    if(pmath_installed) return stats.active!=0;
    if(!stats.active || stats.issued || (boot_mode && boot_phase) || GetCurrentThreadId()!=ui_thread) {pmath_stage="state";return 0;}
    pmath_kat_value=x4pm_kat();
    if(pmath_kat_value!=X4PM_KAT_EXPECTED) {pmath_stage="kat";return 0;}
    if(!pmath_pins()) {pmath_stage="pins";return 0;}
    for(unsigned i=0;i<PMATH_COUNT;i++) memcpy(pmath_original[i],image+pmath_sites[i].rva,5); /* raw, just pinned */
    if(!pmath_page) pmath_page=pmath_alloc_page();
    if(!pmath_page || !pmath_write_page() || !pmath_page_ok()) {pmath_stage="page";return 0;}
    int flag=-1;if(read_local(image+PMATH_FMA3_FLAG,&flag,4)) pmath_fma3=flag;
    pmath_failures=0;pmath_restored=0;
    InterlockedExchange(&pmath_freeze_diag_held, 0);
    int r=pmath_switch(1);
    if(r!=1) {pmath_stage=r<0?"stuck":"busy";if(r<0) InterlockedIncrement64(&pmath_failures);return 0;}
    pmath_installed=1;pmath_stage="armed";return 1;
}
static int pmath_owned(void) {
    if(!pmath_installed) return 1;
    for(unsigned i=0;i<PMATH_COUNT;i++) if(pmath_state(i)!=1) return 0;
    return pmath_page_ok();
}
/* Gate tick: any change stops the gate with reason 156. */
static int pmath_check(void) {
    if(InterlockedCompareExchange64(&pmath_failures,0,0)) return 0;
    if(!pmath_owned()) {pmath_stage="site_changed";InterlockedIncrement64(&pmath_failures);return 0;}
    return 1;
}
static void pmath_restore(void) {
    if(!pmath_installed) return;
    for(unsigned i=0;i<PMATH_COUNT;i++) if(pmath_state(i)!=1) {pmath_stage="restore_foreign";return;}
    int r=pmath_switch(0);
    if(r==1) {pmath_installed=0;pmath_restored=1;pmath_stage="restored";}
    else pmath_stage=r<0?"restore_stuck":"restore_busy";
}
static int pmath_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"installed=%d sites=%u fma3=%d kat=%016llx restored=%d stage=%s attempts=%u busy=%u threads=%u failures=%lld pending_resume=%ld pending_close=%ld release_uncertain=%ld freeze=%s freeze_held=%ld freeze_nt=%08lx freeze_win=%lu freeze_tid=%lu freeze_pass=%lu freeze_seen=%lu freeze_suspended=%lu freeze_wait=%08lx freeze_wait_win=%lu freeze_exited=%lu release_step=%s release_win=%lu release_tid=%lu release_wait=%08lx release_wait_win=%lu",
        pmath_installed,(unsigned)PMATH_COUNT,pmath_fma3,(unsigned long long)pmath_kat_value,pmath_restored,pmath_stage,
        pmath_attempts,pmath_busy,pmath_frozen_max,(long long)pmath_failures,
        (long)InterlockedCompareExchange(&pmath_pending_resumes,0,0),
        (long)InterlockedCompareExchange(&pmath_pending_closes,0,0),
        (long)InterlockedCompareExchange(&pmath_release_uncertain,0,0),
        pmath_freeze_diag_name(InterlockedCompareExchange(&pmath_freeze_diag_stage,0,0)),
        (long)InterlockedCompareExchange(&pmath_freeze_diag_held,0,0),
        (unsigned long)(DWORD)InterlockedCompareExchange(&pmath_freeze_diag_ntstatus,0,0),
        (unsigned long)(DWORD)InterlockedCompareExchange(&pmath_freeze_diag_winerror,0,0),
        (unsigned long)(DWORD)InterlockedCompareExchange(&pmath_freeze_diag_thread,0,0),
        (unsigned long)(DWORD)InterlockedCompareExchange(&pmath_freeze_diag_pass,0,0),
        (unsigned long)(DWORD)InterlockedCompareExchange(&pmath_freeze_diag_seen,0,0),
        (unsigned long)(DWORD)InterlockedCompareExchange(&pmath_freeze_diag_suspended,0,0),
        (unsigned long)(DWORD)InterlockedCompareExchange(&pmath_freeze_diag_wait,0,0),
        (unsigned long)(DWORD)InterlockedCompareExchange(&pmath_freeze_diag_wait_error,0,0),
        (unsigned long)(DWORD)InterlockedCompareExchange(&pmath_freeze_diag_exited,0,0),
        pmath_release_diag_name(InterlockedCompareExchange(&pmath_release_diag_stage,0,0)),
        (unsigned long)(DWORD)InterlockedCompareExchange(&pmath_release_diag_winerror,0,0),
        (unsigned long)(DWORD)InterlockedCompareExchange(&pmath_release_diag_thread,0,0),
        (unsigned long)(DWORD)InterlockedCompareExchange(&pmath_release_diag_wait,0,0),
        (unsigned long)(DWORD)InterlockedCompareExchange(&pmath_release_diag_wait_error,0,0));
    return n>0 && (size_t)n<capacity?n:0;
}
