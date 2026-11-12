#define PLAYER_VIEW_QUERY_TEST
#include "player_view_query_preload.c"
#include <assert.h>
#include <sys/wait.h>

static unsigned calls;
static int invalid_pose;
static uintptr_t fake_pose(void *self, void *output, unsigned mode, double time) {
    assert(self == (void *)0x12345000 && time == 7.125);
    (void)mode;
    _Alignas(16) float pose[16] = {0};
    pose[0] = 2;
    pose[3] = 7;
    pose[4] = invalid_pose ? 2 : 1;
    pose[9] = pose[14] = 1;
    memcpy(output, pose, 64);
    ++calls;
    errno = ERANGE;
    return UINT64_C(0xfedcba9876543210);
}
static void expect_exit_223(pid_t child) {
    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 223);
}
int main(void) {
    unsigned char *caller_page = mmap((void *)0x1314000, PVQ_PAGE, PROT_READ | PROT_WRITE,
                                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(caller_page != MAP_FAILED);
    unsigned char code[] = {
        0x48,0x83,0xec,0x08, 0x48,0xb8,0,0,0,0,0,0,0,0,
        0xff,0xd0, 0x48,0x83,0xc4,0x08,0xc3
    };
    uintptr_t target = (uintptr_t)live_pose_dispatch;
    memcpy(code + 6, &target, 8);
    memcpy(caller_page + 0x307, code, sizeof code);
    assert(!mprotect(caller_page, PVQ_PAGE, PROT_READ | PROT_EXEC));
    uintptr_t (*call)(void *, void *, unsigned, double) = (void *)0x1314307;
    live_pose_information.installed = 1;
    live_pose_original = fake_pose;
    atomic_store(&owner_tid, current_thread());
    ui_depth[0] = 1;
    assert(x4mp_live_pose_bind(current_thread(), 0x12345000));
    _Alignas(16) float delta[16] = {0}, output[16] = {0};
    delta[0] = 3;
    delta[4] = delta[9] = delta[14] = 1;
    assert(x4mp_live_correction_configure(current_thread(), delta, (uintptr_t)output,
                                           (uintptr_t)output + sizeof output));
    assert(!x4mp_live_correction_configure(current_thread(), delta, (uintptr_t)output,
                                            (uintptr_t)output + sizeof output));
    assert(call((void *)0x12345000, output, 1, 7.125) == UINT64_C(0xfedcba9876543210));
    assert(output[0] == 2 && calls == 1 && !live_correction_capture.calls);
    assert(x4mp_live_pose_scope_begin(PLAYER_LIVE_POSE_OPEN));
    assert(call((void *)0x12345000, output, 1, 7.125) == UINT64_C(0xfedcba9876543210));
    assert(output[0] == 2 && calls == 2 && !live_correction_capture.calls);
    assert(x4mp_live_pose_scope(PLAYER_LIVE_POSE_OPEN));
    errno = EINVAL;
    assert(call((void *)0x12345000, output, 1, 7.125) == UINT64_C(0xfedcba9876543210));
    assert(output[0] == 5 && output[3] == 7 && errno == ERANGE && calls == 3);
    assert(live_correction_capture.calls == 1 && live_correction_capture.count == 1);
    float raw[16];
    memcpy(raw, live_correction_capture.rows[0].raw, sizeof raw);
    assert(raw[0] == 2 && live_pose_capture.rows[2].after_valid);
    assert(!memcmp(live_pose_capture.rows[2].after, raw, sizeof raw));
    assert(!memcmp(live_correction_capture.rows[0].corrected, output, sizeof output));
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        live_pose_owner = current_thread();
        call((void *)0x12345000, output, 0, 7.125);
        _exit(1);
    }
    expect_exit_223(child);
    child = fork();
    assert(child >= 0);
    if (!child) {
        live_pose_owner = current_thread();
        invalid_pose = 1;
        call((void *)0x12345000, output, 1, 7.125);
        _exit(1);
    }
    expect_exit_223(child);
    assert(x4mp_live_pose_scope_begin(PLAYER_LIVE_POSE_PAUSED));
    assert(x4mp_live_pose_scope(PLAYER_LIVE_POSE_PAUSED));
    assert(call((void *)0x12345000, output, 1, 7.125) == UINT64_C(0xfedcba9876543210));
    assert(output[0] == 2 && calls == 4 && live_correction_capture.calls == 1);
    return 0;
}
