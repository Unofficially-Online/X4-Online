"""Real census source and recurring adapter against a private task-folder fixture."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class DispatchForkParkTests(unittest.TestCase):
    def test_owned_retained_park_and_census_refusals(self):
        root = Path(__file__).resolve().parents[1]
        engine = root / 'src/native/engine/linux'
        fork_source = (engine / 'gate_fork_save.h').read_text()

        def function(signature):
            start = fork_source.index(signature)
            opening = fork_source.index('{', start)
            depth, end = 1, opening + 1
            while depth:
                depth += (fork_source[end] == '{') - (fork_source[end] == '}')
                end += 1
            return fork_source[start:end]

        source = r'''
#define main original_recurring_fixture_main
#include "test_gate_player_control_recurring.c"
#undef main
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#define LX_FS_JTM_VA 1
#define LX_PATH_MANAGER_VA 2
#define LX_FS_MASS_VA 3
#define LX_FS_REGION_VA 4
#define LX_FS_SIM_START_VA 0x3e59e20
#define FS_FUTEX 202
static const char *fs_task_root;
static struct {
    unsigned threads, engine_threads, other_running;
    pid_t worker_tid;
    char reason[40], blocker[64];
} fs;
static unsigned char *fs_pointer(uintptr_t address) { (void)address; return NULL; }
static void fs_set(char *out, size_t capacity, const char *text) { snprintf(out, capacity, "%s", text); }
'''
        typedef_start = fork_source.index('typedef struct {const char *prefix;')
        typedef_end = fork_source.index(';', fork_source.index('} fs_rule', typedef_start)) + 1
        source += fork_source[typedef_start:typedef_end] + '\n'
        source += function('static int fs_read_text(') + '\n'
        source += function('static int fs_census(') + '\n'
        source += r'''
static void task_row(const char *name, const char *call) {
    char path[512]; snprintf(path, sizeof path, "%s/2000000000/comm", fs_task_root);
    FILE *file = fopen(path, "w"); assert(file); assert(fprintf(file, "%s\n", name) > 0); assert(!fclose(file));
    snprintf(path, sizeof path, "%s/2000000000/syscall", fs_task_root);
    file = fopen(path, "w"); assert(file); assert(fprintf(file, "%s\n", call) > 0); assert(!fclose(file));
}
static void prepare(void) {
    fake_thread = 1; stats.active = 1; stats.issued = stats.completed = 5;
    player_dispatch_enabled = 1;
    atomic_store(&player_recurring_mode, 2);
    player_dispatch.main_thread = player_recurring_rendezvous.main_thread = main_thread;
    player_dispatch.worker_thread = player_recurring_rendezvous.worker_thread = stock_observer_worker_thread;
    player_dispatch.contract.rendezvous = &player_recurring_rendezvous;
    player_dispatch_main_os_tid = (long)syscall(SYS_gettid);
    atomic_store(&player_dispatch_worker_os_tid, 2000000000);
    fs.worker_tid = 2000000000;
    atomic_store(&player_dispatch.phase, PWDA_PARKED);
    atomic_store(&player_dispatch.generation, 5);
    atomic_store(&player_recurring_worker_last, 5);
    atomic_store(&player_recurring_worker_busy, 0);
    atomic_store(&player_recurring_rendezvous.failure, 0);
    atomic_store(&player_recurring_rendezvous.state, fwrr_pack(5,
        FWRR_ARMED | FWRR_MAIN_PARKED | FWRR_WORKER_ARRIVED | FWRR_ENTERED | FWRR_RELEASED | FWRR_MAIN_AUTHORIZED));
    atomic_store(&player_dispatch.contract.state, fwrr_pack(5,
        PWDC_POST_STARTED | PWDC_POST_RETURNED | PWDC_WAIT_CONSUMED | PWDC_JOINED));
    player_fork_owned_worker_park = player_dispatch_parked_for_fork;
    task_row("Movement worker", "230 0 0");
}
int main(int argc, char **argv) {
    assert(argc == 2); fs_task_root = argv[1];
    prepare();
    /* Same owned park may be sleeping, running in its pause bookkeeping, or
     * preempted outside a syscall. No sample authorizes native movement. */
    const char *samples[] = {"230 0 0", "running", "-1 0 0"};
    pthread_mutex_t held = PTHREAD_MUTEX_INITIALIZER;
    for (unsigned i = 0; i < sizeof samples / sizeof *samples; ++i) {
        task_row("Movement worker", samples[i]);
        assert(fs_census());
        assert(!pthread_mutex_lock(&held));
        assert(fs_census()); /* repeated census while caller retains its locks */
        assert(!pthread_mutex_unlock(&held));
        assert(fs.engine_threads == 1 && fs.threads == 1 && !fs.other_running);
    }
    for (unsigned failure = 0; failure < 14; ++failure) {
        prepare();
        switch (failure) {
            case 0: player_dispatch_enabled = 0; break;
            case 1: atomic_store(&player_dispatch_worker_os_tid, 1999999999); break;
            case 2: fs.worker_tid = 1999999999; break;
            case 3: fake_thread = 2; break;
            case 4: --player_dispatch_main_os_tid; break;
            case 5: atomic_store(&player_dispatch.phase, PWDA_TAIL); break;
            case 6: atomic_store(&player_dispatch.phase, PWDA_ARMED); break;
            case 7: stats.completed = 4; break;
            case 8: atomic_store(&player_recurring_worker_last, 4); break;
            case 9: atomic_store(&player_recurring_worker_busy, 1); break;
            case 10: atomic_fetch_and(&player_recurring_rendezvous.state, ~FWRR_MAIN_AUTHORIZED); break;
            case 11: atomic_fetch_and(&player_dispatch.contract.state, ~PWDC_JOINED); break;
            case 12: stats.issued = 6; break;
            case 13: task_row("Gravidar", "running"); break;
        }
        assert(!fs_census());
    }
    /* An owned movement park cannot exempt another engine thread. */
    prepare();
    char extra[512]; snprintf(extra, sizeof extra, "%s/2000000001", fs_task_root);
    assert(!mkdir(extra, 0700));
    snprintf(extra, sizeof extra, "%s/2000000001/comm", fs_task_root);
    FILE *file = fopen(extra, "w"); assert(file); fputs("Gravidar\n", file); assert(!fclose(file));
    snprintf(extra, sizeof extra, "%s/2000000001/syscall", fs_task_root);
    file = fopen(extra, "w"); assert(file); fputs("running\n", file); assert(!fclose(file));
    assert(!fs_census() && strstr(fs.blocker, "Gravidar"));
    assert(!unlink(extra));
    snprintf(extra, sizeof extra, "%s/2000000001/comm", fs_task_root); assert(!unlink(extra));
    snprintf(extra, sizeof extra, "%s/2000000001", fs_task_root); assert(!rmdir(extra));
    /* The default native futex rule remains usable, with no callback. */
    prepare(); player_fork_owned_worker_park = NULL;
    task_row("Movement worker", "202 3e59e48 0"); assert(fs_census());
    task_row("Movement worker", "230 0 0"); assert(!fs_census());
    puts("dispatch fork census: retained park, 14 state refusals and other-thread refusal ok (synthetic task folder)");
}
'''
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            (temporary / 'tasks/2000000000').mkdir(parents=True)
            fixture = temporary / 'census.c'
            fixture.write_text(source)
            executable = temporary / 'census'
            subprocess.run(['cc', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror', '-pthread',
                            '-I', str(engine), str(fixture), '-o', str(executable)], check=True, timeout=30)
            subprocess.run([str(executable), str(temporary / 'tasks')], check=True, timeout=10)
